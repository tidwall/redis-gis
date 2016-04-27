#include "test.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "qtree.h"


void qtreeFreeAndItems(qtree *t, int freeItems);

typedef struct point {
    double x, y;
    void *item;
} point;

// randd creates a random double [1-0].
static double randd(){
    return ((rand()%RAND_MAX) / (double)RAND_MAX);
}

// randx create a random longitude.
static double randx() {
    return randd() * 360.0 - 180.0;
}

// randy create a random latitude.
static double randy() {
    return randd() * 180.0 - 90.0;
}

static point randPoint(){
    point p;
    p.x = randx();
    p.y = randy();
    p.item = malloc(100);
    assert(p.item);
    sprintf(p.item, "%fx%f", p.x, p.y);
    return p;
}

int test_QTreeInsert(){
    srand(time(NULL)/clock());
    qtree *t = qtreeNew(-180, -90, 180, 90);
    int l = 1000000;
    for (int i = 0; i < l; i++) {
        point p = randPoint();
        assert(qtreeInsert(t, p.x, p.y, p.item));
    }
    stopClock();
    int count = 0;
    qtreeIterator *qi = qtreeNewIterator(t, -180, -90, 180, 90);
    while (qtreeIteratorNext(qi)){
        count++;
    }
    qtreeFreeIterator(qi);
    qtreeFreeAndItems(t, 1);
    return l;
}
