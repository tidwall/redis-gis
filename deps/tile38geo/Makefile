STD= 
WARN= -Wall
OPT= -O2

R_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
R_LDFLAGS= $(LDFLAGS)
DEBUG= -g

R_CC=$(CC) $(R_CFLAGS)
R_LD=$(CC) $(R_LDFLAGS)

all: geom.o grisu3.o rtree.o
test: all
	-@$(R_CC) -o test test.c grisu3.o -I. \
		geom_test.c geom.o \
		rtree_test.c rtree.o
	-@./test

.PHONY: all

geom.o: geom.h geom.c 
grisu3.o: grisu3.h grisu3.c
rtree.o: rtree.h rtree.c

.c.o:
	$(R_CC) -c $<

clean:
	rm -f *.o
