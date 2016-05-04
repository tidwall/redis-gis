STD= 
WARN= -Wall
OPT= -O2

R_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
R_LDFLAGS= $(LDFLAGS)
DEBUG= -g

R_CC=$(CC) $(R_CFLAGS)
R_LD=$(CC) $(R_LDFLAGS)

all: geom.o grisu3.o rtree.o geoutil.o \
	 poly.o polyinside.o polyraycast.o polyintersects.o
test: all
	-@$(R_CC) -o test test.c grisu3.o -I. \
		geom_test.c geom.o \
		rtree_test.c rtree.o \
		geoutil_test.c geoutil.o \
		polyinside_test.c polyintersects_test.c poly_test.c \
			poly.o polyinside.o polyraycast.o polyintersects.o
	-@./test

.PHONY: all

geom.o: geom.h geom.c geom_levels.c geom_intersects.c geom_within.c
grisu3.o: grisu3.h grisu3.c
rtree.o: rtree.h rtree.c rtree_tmpl.c
geoutil.o: geoutil.h geoutil.c
poly.o: poly.h poly.c
polyinside.o: poly.h polyinside.c
polyraycast.o: poly.h polyraycast.c
polyintersects.o: poly.h polyintersects.c

.c.o:
	$(R_CC) -c $<

clean:
	rm -f *.o
