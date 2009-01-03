#ifndef __LIBMYC_H__
#define __LIBMYC_H__

#define _GNU_SOURCE // for dprintf
#include <stdio.h> // for size_t
#include <stdarg.h> // for va_list

#define GUARD ((char*)NULL)

void default_file(char **filename, char *base, char *specific);
inline int is (char *a, char *b);
int is_in (char *target, char *potential, ...);

void   *my_malloc(size_t n,  char *what);
char   *my_mallocc(size_t n, char *what);
int    *my_malloci(size_t n, char *what);
double *my_mallocd(size_t n, char *what);

extern int verbose, quiet;
extern int float_precision;
extern double program_start;
extern long seed, use_seed;
extern int myc_debug_malloc;

void initialize_globals (void);
void warn (const char *fmt, ...);
void warnq (const char *fmt, ...);
void die (const char *fmt, ...);

void init_malloc (void);
void *my_malloc (size_t n, char *what);
void my_free (void *tofree);
int *my_malloci (size_t n, char *what);
double *my_mallocd (size_t n, char *what);
char *my_mallocc (size_t n, char *what);

char *my_strcpy (char *orig);
char *my_sprintf (const char *fmt, ...);

char *ltoa (long in);
char *lltoa (long long in);
int int_free (char *tmp);

char *argval (char *opt);
int starts_with (char *string, char *with);
int ends_with (char *string, char *with);

char *log0i (double x);

int my_open (char *file);
int my_open_warn (char *file);
int my_openout (char *file);

int my_select (int newfd);

int readi (int fd, int *dest);
int readl (int fd, long *dest);
int readll (int fd, long long *dest);
int readd (int fd, double *dest);
int readslen (int fd, char **dest);
void writei (int i);
void writel (long l);
void writell (long long ll);
void writed (double d);
void writes (char *s);
void writeslen (char *s);

double now (void);

typedef struct counter *Counter;
Counter gen_counter (char *arg, ...);
void dump_counter (Counter c);
void count (Counter c);
void count_inc (Counter c, long long inc);
void finish (Counter c);
void without_counters(void(*func)(void));

char *get_optpart (char *arg);

char *    get_next_argp   (char ***argv, char *arg);
double    get_next_argpd  (char ***argv, char *arg);
long      get_next_argpl  (char ***argv, char *arg);
long long get_next_argpll (char ***argv, char *arg);
int       get_next_argpi  (char ***argv, char *arg);

char *    get_next_arg   (va_list *t, char *opt);
double    get_next_argd  (va_list *t, char *opt);
long long get_next_argll (va_list *t, char *opt);
long      get_next_argl  (va_list *t, char *opt);
int       get_next_argi  (va_list *t, char *opt);

void init_rand (void);
double random_number (void);

void auto_remake (char **argv);

#endif
