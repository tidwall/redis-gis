STD=
WARN= -Wall
OPT= -O2

R_CFLAGS= $(STD) $(WARN) $(OPT) $(DEBUG) $(CFLAGS)
R_LDFLAGS= $(LDFLAGS)
DEBUG= -g

R_CC=$(CC) $(R_CFLAGS)
R_LD=$(CC) $(R_LDFLAGS)

all: wkt.o btree.o
test: all
	-@$(R_CC) -o test tile38_test.c btree_test.c wkt_test.c btree.o wkt.o -I.
	-@./test
	-@rm -f ./test

.PHONY: all

wkt.o: wkt.h wkt.c
btree.o: btree.h btree.c

.c.o:
	$(R_CC) -c $<

clean:
	rm -f *.o
