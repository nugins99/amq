
all: test
CFLAGS=-g
CPPFLAGS=-g
test.o: test.cc amq.h Makefile
amq.o: amq.h amq.c  Makefile

test: test.o amq.o Makefile
	g++ test.o amq.o -o test -lpthread

clean: 
	rm *.o test 
