STD=
WARN= -Wall
OPT= -O2

R_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
R_LDFLAGS= $(LDFLAGS)
DEBUG= -g

R_CC=$(CC) $(R_CFLAGS)
R_LD=$(CC) $(R_LDFLAGS)

all: geom.o
test: all
	-@$(R_CC) -o test test.c geom_test.c geom.o -I.
	-@./test

.PHONY: all

geom.o: geom.h geom.c

.c.o:
	$(R_CC) -c $<

clean:
	rm -f *.o
