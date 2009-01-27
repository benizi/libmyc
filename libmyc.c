#include "libmyc.h"

#include <stdlib.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <glob.h>

/* AUTOMAKE is kind of fun. If it sees that this source file is newer 
 * than the executable called, it calls "make". The Makefile is set up 
 * to not create the executable directly, but to
 * 1. create it as (exec).tmp, 
 * 2. then mv (exec).tmp (exec).
 */
#ifndef AUTOMAKE
#define AUTOMAKE 1
#endif

#if AUTOMAKE
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
void auto_remake (char **argv) {
	struct stat stat1, stat2;
	int status;
	pid_t pid;
	char *prog = my_strcpy(argv[0]);
	char *source = my_sprintf("%s.c",prog);
	stat(prog,&stat1);
	stat(source,&stat2);
	if (stat1.st_mtime < stat2.st_mtime) {
		warn("Re-making old %s\n%s is older than %s (%d < %d)\n",
			prog,
			prog,source,
			stat1.st_mtime,stat2.st_mtime);
		if ((pid = fork())) {
			wait(&status);
			if (WIFEXITED(status) && WEXITSTATUS(status))
				die("Compilation errors. Correct and rerun\n");
			execvp(prog,argv);
		} else {
			dup2(2,1); // redirect stdout to stderr
			execlp("make","make",(char*)NULL);
		}
	} else if (verbose) warn("Not remaking %s from %s.\n",prog,source);
}
#else
void auto_remake (char **argv) {}
#endif

#ifndef MYMALLOC
#define MYMALLOC 0
#endif

/* TRY_TO_FREE wraps all 'free()' calls. I come from Perl, and I tend not 
 * to call it when I should. This is just an easy way for me to disable 
 * my buggy free() calls.
 */
#ifndef TRY_TO_FREE
#define TRY_TO_FREE 1
#endif

/* TOOMANY = most elements in one direction before an array is 'too big' 
 * to print.
 */
#define TOOMANY 100

static int only_nonzero, perturb, randomize;
static int selected_fd;
int verbose, quiet;
int float_precision;
double program_start;
long seed, use_seed;
int myc_debug_malloc;

double now (void);
void default_file(char **filename, char *base, char *specific);
inline int is (char *a, char *b) { return (a && b && !strcmp(a,b)) ? 1 : 0; }
int is_in (char *target, char *potential, ...);
void *my_malloc(size_t n, char *what);
char *my_mallocc(size_t n, char *what);
int *my_malloci(size_t n, char *what);
double *my_mallocd(size_t n, char *what);

typedef struct d_array {char *name; int ndim; int *dim; double *data;} *dArray;
typedef struct i_array {char *name; int ndim; int *dim; int *data;} *iArray;
typedef struct timeval tv;
#define COUNT_T long long
#define COUNT_GET(X) X##ll
#define COUNT_FMT "%lld"
#define TIME_T double
struct counter {
	char *display;
	int mod; int wait;
	int down;
	int persec;
	COUNT_T c; COUNT_T expect;
	double t; double nt;
	COUNT_T *ctrack;
	TIME_T *ttrack;
	int trackfill; int trackcur; int average;
	int title;
	int hms; int date;
	int finish;
};
static int counters_OK;

typedef struct _fifo_node {
	struct _fifo_node *next;
	char *val;
} *FIFOnode;
typedef struct _fifo { FIFOnode nodes; FIFOnode after; } *FIFO;
FIFO fifo_new(void);

void without_counters (void(*func)(void)) {
	int i = counters_OK;
	counters_OK = 0;
	func();
	counters_OK = i;
}

static char *all_base;
static struct {
	char *specific;
	int changed;
	int nodefault;
	char **filename;
} file_names[] = {
	{ "sig", 1 },
	{ "eta", 1 },
	{ "gam", 1 },
	{ "seq", 1 },
	{ "out", 1, 1 },
	{ NULL }
};
static FIFO output_files, mod_basenames, globs;
static struct {
	FIFO *fifo;
} fifo_toinit[] = {
	{ &output_files }, { &mod_basenames }, { &globs }, { NULL }
};

void initialize_globals (void) {
	int i;
	counters_OK = 1;
	verbose = 0; quiet = 0;
	float_precision = 2;
	all_base = NULL;
	program_start = now();
	for (i = 0; fifo_toinit[i].fifo; i++) *fifo_toinit[i].fifo = fifo_new();
	selected_fd = 1;
	only_nonzero = 1; perturb = 0; randomize = 1;
	seed = 618L; use_seed = 0;
	myc_debug_malloc = 0;
	for (i = 0; file_names[i].specific; i++)
		file_names[i].filename =
			(char **)my_malloc(sizeof(char**),"filename pointer");
}

void warn (const char *fmt, ...) {
	va_list s;
	va_start(s,fmt);
	vfprintf(stderr,fmt,s);
	va_end(s);
	fflush(stderr);
}

void warnq (const char *fmt, ...) {
	if (quiet) return;
	va_list s;
	va_start(s,fmt);
	vfprintf(stderr,fmt,s);
	va_end(s);
	fflush(stderr);
}

void die (const char *fmt, ...) {
	va_list s;
	va_start(s,fmt);
	vfprintf(stderr,fmt,s);
	va_end(s);
	fflush(stderr);
	exit(1);
}

#if MYMALLOC
#define MEMORY 100000000
static long long malloced;
static char memory[MEMORY];
void init_malloc (void) {
	static int inited = 0;
	if (inited++) return;
	malloced = 0;
	memset(memory,0,MEMORY);
}
void *my_malloc (size_t n, char *what) {
	void *where = memory+malloced;
	if (myc_debug_malloc) warn("MYC-MYMALLOC(%d,%s)\n",n,what);
	malloced+=n;
	if (malloced > MEMORY)
		die("MALLOCED TOO MUCH while mallocing %s\n",what);
	return where;
}
void my_free (void *tofree) { }

#else
#include <malloc.h>
void *my_malloc (size_t n, char *what) {
	void *new = malloc(n);
	if (myc_debug_malloc) warn("MYC-MALLOC(%d,%s)\n",n,what);
	if (!new) die("Couldn't allocate %s (%d byte%s)\n", what, n, n==1?"":"s");
	memset(new,0,n);
	return new;
}
void my_free (void *tofree) { if (TRY_TO_FREE) free(tofree); }
#endif

int *my_malloci (size_t n, char *what) {
	return (int *)my_malloc((n?n:1) * sizeof(int), what);
}
double *my_mallocd (size_t n, char *what) {
	return (double *)my_malloc((n?n:1) * sizeof(double), what);
}
char *my_mallocc (size_t n, char *what) {
	return (char *)my_malloc((n?n:1) * sizeof(char), what);
}
char *my_strcpy (char *orig) {
	char *new;
	if (!orig) die("Copying empty string\n");
	new = my_mallocc(1+strlen(orig),"string");
	strcpy(new, orig);
	return new;
}
char *ltoa (long in) {
	char *r = my_mallocc(16,"string");
	sprintf(r,"%ld",in);
	return r;
}
char *lltoa (long long in) {
	char *r = my_mallocc(40,"string");
	sprintf(r,"%lld",in);
	return r;
}
int int_free (char *tmp) {
	int r = atol(tmp);
	my_free(tmp);
	return r;
}

char *my_sprintf (const char *fmt, ...) {
	char *new = my_mallocc(512,"string for my_sprintf");
	va_list s;
	va_start(s,fmt);
	vsprintf(new,fmt,s);
	va_end(s);
	return new;
}

char *argval (char *opt) {
	int i, l;
	if (!opt) die("strlen(empty-string)\n");
	l = strlen(opt);
	for (i = 0; i < l; i++) if (opt[i]=='=') break;
	if (i==l) return NULL;
	i++;
	return my_strcpy(opt+i);
}
int starts_with (char *string, char *with) {
	if (!with) die("strlen(empty-string)\n");
	return strncmp(string,with,strlen(with)) ? 0 : 1;
}
int ends_with (char *string, char *with) {
	if (!string || !with) die("strlen(empty-string)\n");
	int sl = strlen(string);
	int wl = strlen(with);
	if (wl > sl) return 0;
	return strncmp(string+(sl-wl),with,wl) ? 0 : 1;
}

FIFO fifo_new (void) {
	FIFO new = (FIFO)my_malloc(sizeof(struct _fifo),"FIFO");
	new->nodes = NULL;
	return new;
}

int fifo_len (FIFO fifo) {
	int i;
	FIFOnode n;
	for (i = 0, n = fifo->nodes; n; i++, n = n->next);
	return i;
}

FIFOnode fifo_new_node (void) {
	FIFOnode new = my_malloc(sizeof(struct _fifo_node),"FIFO node");
	new->next = NULL;
	new->val = NULL;
	return new;
}

void fifo_push (FIFO fifo, char *str) {
	FIFOnode after;
	FIFOnode new = fifo_new_node();
	new->val = str;
	if (!fifo->nodes) {
		fifo->nodes = new;
	} else {
		after = fifo->after;
		after->next = new;
	}
	fifo->after = new;
}

char *fifo_pop (FIFO fifo) {
	FIFOnode tmp;
	char *ret = NULL;
	if (fifo->nodes) {
		ret = fifo->nodes->val;
		if ((tmp = fifo->nodes->next)) {
			my_free(fifo->nodes);
			fifo->nodes = tmp;
		}
	}
	return ret;
}

iArray namei (char *name, iArray arr) { arr->name = name; return arr; }

iArray initiArray (int ndim, ...) {
	iArray new;
	va_list s;
	int i = 0, d = 1;
	new = (iArray)my_malloc(sizeof(struct i_array), "array");
	new->name = NULL;
	new->ndim = ndim;
	new->dim = my_malloci(ndim,"dim array");
	va_start(s,ndim);
	while (i < ndim) d *= (new->dim[i++] = va_arg(s,int));
	va_end(s);
	new->data = my_malloci(d,"data array");
	for (i = 0; i < d; i++) new->data[i]=0;
	return new;
}

void free_iArr (iArray arr) {
	if (!arr) return;
	my_free(arr->data);
	my_free(arr->dim);
	my_free(arr);
	arr = NULL;
}

int iGetSet (iArray arr, int set, va_list *s) {
	int val, prev;
	int d, j;
	long off = 0;
	for (j = 0; j < arr->ndim; j++) {
		if (j) off *= arr->dim[j];
		d = va_arg(*s,int);
		off += d;
	}
	val = set ? va_arg(*s,int) : arr->data[off];
	prev = set ? arr->data[off] : val;
	if (set) arr->data[off] = val;
	va_end(*s);
	return prev;
}

int iGet (iArray arr, ...) {
	va_list s;
	va_start(s,arr);
	return iGetSet(arr,0,&s);
}

int iSet (iArray arr, ...) {
	va_list s;
	va_start(s,arr);
	return iGetSet(arr,1,&s);
}

int iGetSetP (iArray arr, int set, int *dim, int val) {
	int prev;
	int i;
	long off = 0;
	for (i = 0; i < arr->ndim; i++) {
		if (i) off *= arr->dim[i];
		off += dim[i];
	}
	if (set) {
		prev = arr->data[off];
		arr->data[off] = val;
		return prev;
	} else {
		return arr->data[off];
	}
}
int iGetP (iArray arr, int *d) { return iGetSetP(arr,0,d,0); }
int iSetP (iArray arr, int *d, int v) { return iGetSetP(arr,1,d,v); }

dArray named (char *name, dArray arr) { arr->name = name; return arr; }

dArray initdArray (int ndim, ...) {
	dArray new;
	va_list s;
	int i = 0, d = 1, v;
	new = (dArray)my_malloc(sizeof(struct d_array), "array");
	new->name = NULL;
	new->ndim = ndim;
	new->dim = my_malloci(ndim,"dim array");
	va_start(s,ndim);
	while (i < ndim) {
		v = va_arg(s,int);
		d *= (new->dim[i++] = v);
	}
	va_end(s);
	new->data = my_mallocd(d,"data array");
	for (i = 0; i < d; i++) new->data[i]=0.0;
	if (verbose > 1) {
		warn("Created array of size");
		for (i = 0; i < ndim; i++) warn("[%d]",new->dim[i]);
		warn("\n");
	}
	return new;
}

void free_dArr (dArray arr) {
	if (!arr) return;
	my_free(arr->data);
	my_free(arr->dim);
	my_free(arr);
	arr = NULL;
}

double dGetSet (dArray arr, int set, va_list *s) {
	double val, prev;
	int d, j;
	long off = 0;
	for (j = 0; j < arr->ndim; j++) {
		if (j) off *= arr->dim[j];
		d = va_arg(*s,int);
		off += d;
	}
	val = set ? va_arg(*s,double) : arr->data[off];
	va_end(*s);
	prev = set ? arr->data[off] : val;
	if (set > 1) val += prev;
	if (set) arr->data[off] = val;
	return prev;
}

double dGet (dArray arr, ...) {
	va_list s;
	va_start(s,arr);
	return dGetSet(arr,0,&s);
}

double dSet (dArray arr, ...) {
	va_list s;
	va_start(s,arr);
	return dGetSet(arr,1,&s);
}

double dInc (dArray arr, ...) {
	va_list s;
	va_start(s,arr);
	return dGetSet(arr,2,&s);
}

double dGetSetP (dArray arr, int set, int *dim, double val) {
	double prev;
	int i;
	long off = 0;
	for (i = 0; i < arr->ndim; i++) {
		if (i) off *= arr->dim[i];
		off += dim[i];
	}
	if (set) {
		prev = arr->data[off];
		arr->data[off] = val;
		return prev;
	} else {
		return arr->data[off];
	}
}
double dGetP (dArray arr, int *d) { return dGetSetP(arr,0,d,0.0); }
double dSetP (dArray arr, int *d, double v) { return dGetSetP(arr,1,d,v); }

/*
#define log0(X) log(X)//double log0 (double x) { return x ? log(x) : -inf; }
char *log0i (double x) {
	char *r;
	double l = log0(x);
	if (l!=0) sprintf(r,"%.*f",float_precision,l);
	else r = "-inf";
	return r;
}

double normalizeBut1 (dArray arr, ...) {
	// normalizes a subArray [all but 1 dimension specified]
	int i, max, ndim;
	double sum = 0;
	ndim = arr->ndim;
	max = arr->dim[ndim-1];
	int *dims = my_malloci(ndim,"normalization dimensions");
	va_list s;
	va_start(s,arr);
	for (i = 0; i < ndim - 1; i++) dims[i] = va_arg(s,int);
	va_end(s);
	switch (ndim) {
		case 3:
			for (i = 0; i < max; i++) sum += dGet(arr,dims[0],dims[1],i);
			if (sum) for (i = 0; i < max; i++) dSet(arr,dims[0],dims[1],i,dGet(arr,dims[0],dims[1],i)/sum);
		break;
		case 2:
			for (i = 0; i < max; i++) sum += dGet(arr,dims[0],i);
			if (sum) for (i = 0; i < max; i++) dSet(arr,dims[0],i,dGet(arr,dims[0],i)/sum);
		break;
		case 1:
			for (i = 0; i < max; i++) sum += dGet(arr,i);
			if (sum) for (i = 0; i < max; i++) dSet(arr,i,dGet(arr,i)/sum);
		break;
		default:
			for (i = 0; i < max; i++) {
				dims[ndim-1] = i;
				sum += dGetP(arr,dims);
			}
			for (i = 0; i < max; i++) {
				if (!sum) break;
				dims[ndim-1] = i;
				dSetP(arr,dims,dGetP(arr,dims)/sum);
			}
	}
	my_free(dims);
	return log0(sum);
}
*/

void _printArray (int isd, int toobig, void *arr);
void printiArray (iArray arr) { _printArray(0,0,(void*)arr); }
void printdArray (dArray arr) { _printArray(1,0,(void*)arr); }
void printiArrayL (iArray arr) { _printArray(0,1,(void*)arr); }
void printdArrayL (dArray arr) { _printArray(1,1,(void*)arr); }
void _printArray (int isd, int toobig, void *arr) {
	int i, j, k;
	char *name;
	int ndim, *dims;
	int im = 1, jm = 1, km = 1;
	long d = 1;
	iArray ir = NULL;
	dArray dr = NULL;
	if (isd) dr = (dArray)arr;
	else ir = (iArray)arr;
	name = isd ? dr->name : ir->name;
	if (name && !quiet) printf("%s\n",name);
	ndim = isd ? dr->ndim : ir->ndim;
	dims = isd ? dr->dim : ir->dim;
	for (i = 0; i < ndim; i++) if (dims[i]>TOOMANY) toobig = 1;
	switch (ndim){//toobig?0:ndim) {
		case 3:
			km = dims[2];
		case 2:
			jm = dims[1];
		case 1:
			im = dims[0];
			if (km>TOOMANY) km=TOOMANY;
			if (jm>TOOMANY) jm=TOOMANY;
			if (im>TOOMANY) im=TOOMANY;
			for (k = 0; k < km; k++)
				for (i = 0; i < im; i++)
					for (j = 0; j < jm; j++)
						if (isd)
						printf("%s%s%s%.*f",
							(k&&!i&&!j)?"\n":"",
							((k||i)&&!j)?"\n":"",
							j?" ":"",
							float_precision,
							dGet(dr,i,j,k)
						);
						else
						printf("%s%s%s%d",
							(k&&!i&&!j)?"\n":"",
							((k||i)&&!j)?"\n":"",
							j?" ":"",
							iGet(ir,i,j,k)
						);
		break;
		default:
			for (i = 0; i < ndim; i++) d *= dims[i];
			if (isd) {
				for (i = 0; i < d; i++)
					printf("%s%.*f",i?" ":"",float_precision,dr->data[i]);
			} else {
				for (i = 0; i < d; i++)
					printf("%s%d",i?" ":"",ir->data[i]);
			}
		break;
	}
	printf("\n");
}

/*
void _printNormArray (int toobig, dArray arr, dArray norm);
void printNormArray (dArray arr, dArray norm) { _printNormArray(0,arr,norm); }
void printNormArrayL (dArray arr, dArray norm) { _printNormArray(1,arr,norm); }
void _printNormArray (int toobig, dArray arr, dArray norm) {
	int i, j, k;
	int im = 1, jm = 1, km = 1;
	long d = 1;
	if (arr->name && !quiet) printf("%s\n",arr->name);
	for (i = 0; i < arr->ndim; i++) if (arr->dim[i]>TOOMANY) toobig = 1;
	switch (arr->ndim){//toobig?0:arr->ndim) {
		case 3:
			km = arr->dim[2];
		case 2:
			jm = arr->dim[1];
		case 1:
			im = arr->dim[0];
			if (km>TOOMANY) km=TOOMANY;
			if (jm>TOOMANY) jm=TOOMANY;
			if (im>TOOMANY) im=TOOMANY;
			for (k = 0; k < km; k++) {
				for (i = 0; i < im; i++) {
					for (j = 0; j < jm; j++) {
						printf("%s%s%s%.*f",
							(k&&!i&&!j)?"\n":"",
							((k||i)&&!j)?"\n":"",
							j?" ":"",
							float_precision,
							dGet(arr,i,j,k)*exp(dGet(norm,i,j,k))
						);
						if (j==jm-1) printf(" [%.*f]",
							float_precision,
							dGet(norm,i,j,k)
						);
					}
				}
			}
		break;
		default:
			for (i = 0; i < arr->ndim; i++) d *= arr->dim[i];
			for (i = 0; i < d; i++) printf("%s%.*f",i?" ":"",float_precision,arr->data[i]);
		break;
	}
	printf("\n");
}
*/


int _my_open (char *file, int flags, void(*warn_die)(const char *fmt,...)) {
	int fd;
	int reading = (flags && O_RDWR) ? 0 : 1;
	char *desc = reading ? "reading" : "writing";
	if (is(file,"-") && reading) fd = 0;
	else if (is(file,"-") && !reading) fd = 1;
	else fd = open(file,flags,0666);
	if (fd < 0) warn_die("Couldn't open %s for %s\n", file, desc);
	if (verbose > 1) warnq("Opened %s for %s\n",file,desc);
	return fd;
}
int my_open (char *file) { return _my_open(file,O_RDONLY,die); }
int my_open_warn (char *file) { return _my_open(file,O_RDONLY,warnq); }
int my_openout (char *file) {return _my_open(file,O_RDWR|O_CREAT|O_TRUNC,die);}
int my_select (int fd) { int r = selected_fd; selected_fd = fd; return r; }

int readi (int fd, int *dest) {
	int r = read(fd,dest,sizeof(int));
	if (r != sizeof(int) && r) die("Couldn't read integer (Got %d)\n",r);
	return r;
}
int readl (int fd, long *dest) {
	int r = read(fd,dest,sizeof(long));
	if (r != sizeof(long) && r) die("Couldn't read long (Got %d)\n", r);
	return r;
}
int readll (int fd, long long *dest) {
	int r = read(fd,dest,sizeof(long long));
	if (r!=sizeof(long long) && r) die("Couldn't read long long (Got %d)\n", r);
	return r;
}
void _writei (int fd, int i) { write(fd,&i,sizeof(int)); }
void writei (int i) { _writei(selected_fd,i); }
void _writel (int fd, long l) { write(fd,&l,sizeof(long)); }
void writel (long l) { _writel(selected_fd,l); }
void _writell (int fd, long long ll) { write(fd,&ll,sizeof(long long)); }
void writell (long long ll) { _writell(selected_fd,ll); }
void _writed (int fd, double d) { write(fd,&d,sizeof(double)); }
void writed (double d) { _writed(selected_fd,d); }
void _writes (int fd, char *s) { write(fd,s,strlen(s)); }
void writes (char *s) { _writes(selected_fd,s); }
void _writeslen (int fd, char *s, int i) { _writei(fd,i); write(fd,s,i); }
void writeslen (char *s) { _writeslen(selected_fd,s,strlen(s)); }

int readd (int fd, double *dest) {
	int r = read(fd,dest,sizeof(double));
	if (r != sizeof(double) && r) die("Couldn't read double (Got %d)\n",r);
	return r;
}

int readslen (int fd, char **dest) {
	int i;
	int r = readi(fd,&i);
	if (!r) return r;
	(*dest) = my_mallocc(i+1,"string read");
	r = read(fd,*dest,i);
	if (r != i) die("Couldn't read full string (Expected %d, got %d)\n", i, r);
	return r;
}

char *_get_filename (char *specific, int nodefault) {
	int i;
	for (i = 0; file_names[i].specific; i++)
		if (is(specific,file_names[i].specific))
			break;
	if (!file_names[i].specific) return NULL;
	if (!file_names[i].filename || !*file_names[i].filename) if (!nodefault)
		default_file(file_names[i].filename,all_base,file_names[i].specific);
	file_names[i].changed = 0;
	return *file_names[i].filename;
}
int new_file (char *spec) {
	int i;
	for (i = 0; file_names[i].specific; i++)
		if (is(spec,file_names[i].specific))
			return file_names[i].changed ? 1 : 0;
	return 0;
}
void claim_not_new (char *spec) {
	int i;
	for (i = 0; file_names[i].specific; i++)
		if (is(spec,file_names[i].specific))
			file_names[i].changed = 0;
}
char *get_filename (char *spec) { return _get_filename(spec,0); }
char *get_filename_nod (char *spec) { return _get_filename(spec,1); }

void set_filename (char *specific, char *name) {
	int i;
	for (i = 0; file_names[i].specific; i++)
		if (is(specific,file_names[i].specific))
			break;
	if (!file_names[i].specific) return;
	if (is(*file_names[i].filename,name)) return;
	file_names[i].changed = 1;
	*file_names[i].filename = name;
}

void dump_counter (Counter c) {
	if (!c) { warn("Counter is null!\n"); return; }
	warn("DISPLAY={%s} <mod=%d wait=%d> %s c=%lld expect=%lld\nt=%f nt=%f\n%s %s\n",
		c->display, c->mod, c->wait, c->persec?"persec":"!persec",
		c->c, c->expect, c->t,c->nt,c->title?"title":"!title",c->finish?"finished":"!finished");
}

double now (void) {
	struct timeval t;
	gettimeofday(&t,NULL);
	return t.tv_sec + (0.000001 * t.tv_usec);
}

void count (Counter c) {
	TIME_T time, diff, tavg, finish;
	double rate, add;
	COUNT_T cavg, left;
	long tofinish;
	struct tm *localtm;
	char howlong[100], date[100];
	int i, j, curr, first_time, has_rate = 0;
	howlong[0] = date[0] = '\0';
	first_time = (c->down ? (c->c == c->expect) : !c->c);
	if (!c->t && first_time) c->t = now();
	if (c->display &&
		(c->finish
		|| (c->mod && !(c->c % c->mod))
		|| (now() > c->nt))
		) {
		time = now();
		diff = time - c->t;
		if (!c->finish
			&& !first_time
			&& (c->expect || c->persec)
			&& diff) {
			curr = c->trackcur;
			c->ctrack[curr] = (c->down ? (c->expect - c->c) : c->c);
			c->ttrack[curr] = diff;
			c->trackfill++;
			if (c->trackfill > c->average) c->trackfill = c->average;
			cavg = 0;
			tavg = 0;
			for (i = 0; i < c->trackfill; i++) {
				j = curr - i;
				if (j < 0) j += c->average;
				cavg += c->ctrack[j];
				tavg += c->ttrack[j];
				rate += ((double)cavg)/tavg;
			}
			cavg /= c->trackfill;
			tavg /= c->trackfill;
			rate = ((double)cavg/tavg);
//			rate /= c->trackfill;
			if (!rate) rate = 1.0;
			has_rate = 1;
			if (c->expect) {
				if (c->down) left = c->c;
				else left = c->expect - c->c;
				add = left / rate;
				finish = c->t + diff + add;
			}
			c->trackcur++;
			c->trackcur = c->trackcur % c->average;
		}
		if (counters_OK) {
			if (c->title) warnq("\e]2;%s %lld\007", c->display, c->c);
			warnq("%s %6lld",c->display,c->c);
			if (c->expect) warnq("/%lld",c->expect);
			warnq(" -- %4d ",(int)diff);
			if (c->persec && has_rate) {
				if (rate > .8) warnq("[%.2f/sec] ",rate);
				else warnq("[%.2f seconds per] ",1/rate);
			}
			warnq("%5.2f",time-program_start);
			if (c->expect && has_rate && (c->hms || c->date)) {
				tofinish = finish;
				if (c->hms) {
					long fin = finish - time;
					int dhms[4], intvl[] = { 86400, 3600, 60, 1, 0 };
					char *label[] = { "d", "h", "m", "s", NULL };
					int i, printed = 0;
					if (fin > 3600) c->date = 1;
					for (i = 0; intvl[i]; i++) {
						if (i) fin %= intvl[i-1];
						if (!(fin / intvl[i])) continue;
						sprintf(howlong+strlen(howlong),
							"%s%02d%s",
							(printed?":":""),
							fin / intvl[i],
							label[i]
						);
						printed++;
					}
				}
				if (c->date) {
					localtm = localtime(&tofinish);
					strftime(date,100,"%Y/%m/%d@%H:%M:%S",localtm);
				}
				if (*howlong && *date) warnq(" [%s->%s]",howlong,date);
				else warnq(" [->%s]",(*howlong ? howlong : date));
			}
			warnq("\n");
		}
		if (!c->mod) c->nt = c->t + c->wait * (1 + (diff/c->wait));
	}
	c->c += c->down ? -1 : 1;
}
void count_inc (Counter c, COUNT_T inc) { inc--; count(c); c->c += inc; }
void count_dec (Counter c, COUNT_T dec) { dec++; count(c); c->c -= dec; }
void finish (Counter c) { c->finish = 1; count(c); }

char *get_optpart (char *arg) {
	int i;
	char *tmp = NULL;
	if (!arg) return tmp;
	for (i = 0; arg[i] && arg[i]!='='; i++);
	tmp = my_mallocc(i+1,"option string");
	tmp[i] = 0;
	while (i-- >= 0) tmp[i] = arg[i];
	return tmp;
}

int is_in (char *target, char *potential, ...) {
	va_list s;
	va_start(s,potential);
	while (potential) {
		if (is(potential,target)) return 1;
		potential = va_arg(s,char*);
	}
	va_end(s);
	return 0;
}

char *get_next_argp (char ***argv, char *arg) {
	char *tmp;
	tmp = argval(arg);
	if (!tmp) {
		tmp = *(++(*argv));
		if (tmp) tmp = my_strcpy(tmp);
	}
	if (!tmp) die("Option %s needs an argument\n",arg);
	return tmp;
}
double get_next_argpd (char ***argv, char *arg) {
	char *tmp = get_next_argp(argv,arg);
	return atof(tmp);
}
long get_next_argpl (char ***argv, char *arg) {
	char *tmp = get_next_argp(argv,arg);
	return atol(tmp);
}
long long get_next_argpll (char ***argv, char *arg) {
	char *tmp = get_next_argp(argv,arg);
	return atoll(tmp);
}
int get_next_argpi (char ***argv, char *arg) {
	return (int)get_next_argpl(argv,arg);
}
char *get_next_arg (va_list *t, char *opt) {
	char *tmp;
	tmp = argval(opt);
	if (!tmp) {
		tmp = va_arg(*t,char*);
		if (tmp) tmp = my_strcpy(tmp);
	}
	if (!tmp) die("Option %s needs an argument\n",opt);
	return tmp;
}
double get_next_argd (va_list *t, char *opt) {
	char *tmp = get_next_arg(t,opt);
	return atof(tmp);
}
long long get_next_argll (va_list *t, char *opt) {
	char *tmp = get_next_arg(t,opt);
	return atoll(tmp);
}
long get_next_argl (va_list *t, char *opt) {
	char *tmp = get_next_arg(t,opt);
	return atol(tmp);
}
int get_next_argi (va_list *t, char *opt) {
	return (int)get_next_argl(t,opt);
}
Counter gen_counter (char *arg, ...) {
	char *opt;
	Counter new = (Counter)my_malloc(sizeof(struct counter),"counter");
	new->display = my_strcpy("counter");
	new->c = 0;
	new->mod = 0;
	new->wait = 5;
	new->persec = 0;
	new->expect = 0;
	new->title = 1;
	new->finish = 0;
	new->average = 5;
	new->hms = 1;
	new->date = 0;
	va_list s;
	va_start(s,arg);
	while (arg) {
		opt = get_optpart(arg);
		if (is_in(opt,"disp","display",(char*)NULL)) {
			new->display = get_next_arg(&s,arg);
		} else if (is(opt,"mod")) {
			new->mod = get_next_argi(&s,arg);
		} else if (is_in(opt,"down","backwards",(char*)NULL)) {
			new->down = 1;
		} else if (is(opt,"persec")) {
			new->persec = 1;
		} else if (is(opt,"wait")) {
			new->wait = get_next_argi(&s,arg);
		} else if (is_in(opt,"avg","average",(char*)NULL)) {
			new->average = get_next_argi(&s,arg);
		} else if (starts_with(opt,"expect")) {
			new->expect = COUNT_GET(get_next_arg)(&s,arg);
		} else if (is(opt,"date")) {   new->date = 1;
		} else if (is(opt,"nodate")) { new->date = 0;
		} else if (is(opt,"hms")) {    new->hms = 1;
		} else if (is(opt,"nohms")) {  new->hms = 0;
		} else {
			die("Unknown counter option: %s\n",arg,is_in(opt,"down",(char*)NULL));
		}
		arg = va_arg(s,char *);
	}
	if (new->down) new->c = new->expect;
	new->ctrack = (COUNT_T *)my_malloc(new->average*sizeof(COUNT_T),"ctrack");
	new->ttrack = (TIME_T *)my_malloc(new->average*sizeof(TIME_T),"ttrack");
	va_end(s);
	return new;
}

// # back(i,c') = \sum_{c\in\Sigma} \eta(s_{i+1} | c) \cdot \gamma(c|c') \cdot back(i+1,c)

void init_rand (void) {
	static int initialized = 0;
	if (!initialized++) {
		if (!use_seed) srand48((long)now());
		else srand48(seed);
	}
}

double random_number (void) {
	init_rand();
	return drand48();
}
double random_array_entry (void) {
	if (perturb) return 1.0 + random_number() * .05;
	else return 0.5 + random_number();
}

/*
void randomizeArray (dArray arr) {
	long i, d = 1;
	if (!randomize) return;
	for (i = 0; i < arr->ndim; i++) d *= arr->dim[i];
	for (i = 0; i < d; i++)
		if (!only_nonzero || arr->data[i])
			arr->data[i] = random_array_entry();
	if (arr->ndim == 2)
		for (i = 0; i < arr->dim[0]; i++)
			normalizeBut1(arr,i);
}
*/

/* FORMULAE:

\alpha = forward
B = backward

xi_t(i,j)=
	\frac{\alpha_t(i) a_{ij} B_{t+1}(j) b_j(\alpha_{t+1})}
		{\sum_{i=1}^N \sum_{j=1}^N [same quant]}
gam_t(i) =
	\frac{\alpha_t(i) B_t(i)}
		{\sum_{i=1}^N \alpha_t(i) B_t(i)}

*/

void default_file (char **filename, char *base, char *specific) {
	int i, nl, l = 0;
	char *template = "%s-%s.bin";
	if (filename && *filename) return;
	if (!base) die("BASE=%s filename=%s specific=%s\n",base,*filename,specific);
	if (!base) die("Must specify base=(prefix) or %s=(filename)\n",specific);
	l = strlen(template);
	nl = l;
	for (i = 0; i < l; i++) if (template[i]=='%') nl -= 2;
	nl += strlen(base);
	nl += strlen(specific);
	*filename = my_mallocc(nl+1,"filename");
	sprintf(*filename,template,base,specific);
	warnq("%s filename defaulted to %s\n",specific,*filename);
}

/*
int pick_from_dist (dArray dist) {
	int i;
	double d, tot;
	normalizeBut1(dist);
	if (verbose > 3) printdArray(dist);
	d = random_number();
	if (verbose > 2) warn("DRAND{%.*f}\n",float_precision,d);
	for (i = 0, tot = 0; i < dist->dim[0]; i++) {
		tot += dGet(dist,i);
		if (tot >= d) break;
	}
	if (i==dist->dim[0]) i = dist->dim[0] - 1;
	return i;
}
*/

typedef struct _printfuncs {
	void(*String)(char *s);
	void(*Index)(int i);
	void(*I)(int i);
	void(*D)(double d);
	void(*NL)(void);
	void(*SP)(void);
	void(*TAB)(void);
} printfuncs;

void with_outfile (void(*func)(void)) {
	int selected = selected_fd;
	char *fn = get_filename_nod("out");
	if (fn) selected_fd = my_openout(fn);
	func();
	if (fn) close(selected_fd);
	selected_fd = selected;
	set_filename("out",NULL);
}

void _printString_txt (char *s) { dprintf(selected_fd,"%s",s); }
void _printIndex_txt (int i) { }
void _printI_txt (int i) { dprintf(selected_fd,"%d",i); }
void _printD_txt (double d) {
	dprintf(selected_fd,"%.*f",(float_precision>2)?float_precision:7,d);
}
void _printNL_txt (void) { dprintf(selected_fd,"\n"); }
void _printSP_txt (void) { dprintf(selected_fd," "); }
void _printTAB_txt (void) { dprintf(selected_fd,"\t"); }
static printfuncs pf_txt = {
	_printString_txt,
	_printIndex_txt,
	_printI_txt,
	_printD_txt,
	_printNL_txt,
	_printSP_txt,
	_printTAB_txt
};

void _printString_bin (char *s) { int l = strlen(s); writei(l); writes(s); }
void _printNL_bin (void) { }
void _printSP_bin (void) { }
void _printTAB_bin (void) { }
static printfuncs pf_bin = {
	_printString_bin,
	writei,
	writei,
	writed,
	_printNL_bin,
	_printSP_bin,
	_printTAB_bin
};
#warning - following code is just to get rid of more warnings
void _use_it (printfuncs pf, int i) { pf.I(i); }
void _use_txt (void) { _use_it(pf_txt, 10); }
void _use_bin (void) { _use_it(pf_bin, 10); }

void _print_dims (dArray arr, printfuncs pf) {
	int i;
	for (i = 0; i < arr->ndim; i++) pf.Index(arr->dim[i]);
}

void consume_filename (void) { set_filename("out",fifo_pop(output_files)); }

void with_outfile_named (char *outfn, void (*func)(void)) {
	set_filename("out",outfn);
	with_outfile(func);
}

char *filename_from_base (char *base, char *type, char *ext) {
	char *ret = my_mallocc(strlen(base)+2+strlen(type)+strlen(ext),"filename");
	sprintf(ret,"%s-%s%s",base,type,ext);
	return ret;
}

void print_fifo (FIFO fifo) {
	FIFOnode n;
	printf("PRINTING FIFO\n");
	for (n = fifo->nodes; n; n = n->next)
		printf("FIFO entry: {%s}\n",n->val);
	printf("DONE PRINTING FIFO\n");
}

double time_to_double (struct timeval t) {
	return t.tv_sec + ((double)t.tv_usec/1000000);
}

double NOW (void) {
	struct timeval now;
	if (gettimeofday(&now,NULL)) return 0.0;
	return time_to_double(now);
}
