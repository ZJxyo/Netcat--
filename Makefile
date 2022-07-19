all:objclean ncTh

#The following lines contain the generic build options
CC=gcc
CPPFLAGS=
CFLAGS=-g

#These are the options for building the threaded version of the program
#Add  the specification of any libraries needed to the next lint
TH_CLIBS=-pthread

#List all the .o files that need to be linked for the thread version of the program
#What you need to put on the following line depends upon the .c files required to 
#build the ncTh version of the program. Basically take the name of each required .c file 
#and change the .c to .o

THREADSOURCE=ncTh.c Thread.c usage.c parseOptions.c
THREADOBJS = $(THREADSOURCE:.c=.o)
#These are the options for building the version of the program that uses
#the poll() system call. 
#Add  the specification of any libraries needed to the next lint
POLL_CLIBS=

ncTh: $(THREADOBJS)
	$(CC) -o ncTh $(THREADOBJS)  $(TH_CLIBS)

objclean:
	rm -f $(THREADOBJS)

clean:
	rm -f *.o
	rm -f ncTh


