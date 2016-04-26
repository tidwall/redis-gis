STD=
WARN= -Wall
OPT= -O2

R_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
R_LDFLAGS= $(LDFLAGS)
DEBUG= -g

R_CC=$(CC) $(R_CFLAGS)
R_LD=$(CC) $(R_LDFLAGS)

all: geom.o grisu3.o
test: all
	-@$(R_CC) -o test test.c geom_test.c geom.o grisu3.o -I.
	-@./test

.PHONY: all

geom.o: geom.h geom.c 
grisu3.o: grisu3.h grisu3.c

.c.o:
	$(R_CC) -c $<

clean:
	rm -f *.o
