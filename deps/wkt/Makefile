STD=
WARN= -Wall
OPT= -O2

R_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
R_LDFLAGS= $(LDFLAGS)
DEBUG= -g

R_CC=$(CC) $(R_CFLAGS)
R_LD=$(CC) $(R_LDFLAGS)

all: wkt.o

.PHONY: all

wkt.o: wkt.h wkt.c

.c.o:
	$(R_CC) -c $<

clean:
	rm -f *.o
