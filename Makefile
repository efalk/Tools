
OS = -DMAC
#OS = -DLINUX

#CFLAGS = -O -Wall ${INC} ${OS}
CFLAGS = -g -Wall -DDEBUG ${INC} ${OS}
#CFLAGS = -g -Wall -Werror -DDEBUG ${INC} ${OS}

PROGS =	superlog

superlog: superlog.o libsuperlog.o
	cc -o $@ superlog.o libsuperlog.o

clean:
	rm -f *.o

clobber: clean
	rm -f ${PROGS}

