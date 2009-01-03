OUTPUT=/home/bhaskell/lib
INCLUDE=/home/bhaskell/include

CC=gcc
CPPFLAGS= -O3 -I$(INCLUDE)

all: $(OUTPUT)/libmyc.a	$(INCLUDE)/libmyc.h

$(OUTPUT)/libmyc.a:	libmyc.o $(INCLUDE)/libmyc.h
	ar rc $@ libmyc.o
	ranlib $@

$(INCLUDE)/libmyc.h:	libmyc.h
	cp $< $@
