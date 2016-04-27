/*
 * Copyright (c) 2016, Josh Baker <joshbaker77@gmail.com>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of Redis nor the names of its contributors may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <string.h>
#include <stdlib.h>
#include "qtree.h"

const int MAX_POINTS = 32;
const int APPEND_GROWTH = 0; // Set 'true' for faster inserts, Set 'false' for smaller memory size

typedef struct node node;

typedef struct point {
    double x, y;
    void *item;
} point;

typedef struct node {
    int quad; // this property is probably not needed.
    double minX, minY, maxX, maxY;
    int count;
    int length;
    point *points;
    node **nodes;
} node;

typedef struct qtree {
    node *root;
} qtree;

typedef struct splitResult {
    double minX, minY, maxX, maxY;
} splitResult;

typedef struct stackItem {
    node *n;
    int pointIdx;
    int quadIdx;
} stackItem;

typedef struct qtreeIterator{
    int len;
    int cap;
    stackItem *stack;
    double minX, minY, maxX, maxY; // search bounds
    int mem;     // out of memory error

    double x, y; // result x, y
    void *item;  // result item
} qtreeIterator;

static node *nodeNew(int quad, double minX, double minY, double maxX, double maxY){
    node *n = malloc(sizeof(node));
    if (!n){
        return NULL;
    }
    memset(n, 0, sizeof(node));
    n->quad = quad;
    n->minX = minX;
    n->minY = minY;
    n->maxX = maxX;
    n->maxY = maxY;
    n->nodes = malloc(sizeof(node*)*4);
    if (!n->nodes){
        free(n);
        return NULL;
    }
    memset(n->nodes, 0, sizeof(node*)*4);
    return n;
}

static void nodeFree(node *n, int freeItems){
    if (n){
        if (n->points){
            if (freeItems){
                for (int i=0;i<n->count;i++){
                    free(n->points[i].item);
                }
            }
            free(n->points);
        }
        for (int i=0;i<4;i++){
            if (n->nodes[i]){
                nodeFree(n->nodes[i], freeItems);
            }
        }
        free(n->nodes);
        free(n);
    }
}

qtree *qtreeNew(double minX, double minY, double maxX, double maxY){
    qtree *t = malloc(sizeof(qtree));
    if (!t){
        return NULL;
    }
    memset(t, 0, sizeof(qtree));
    t->root = nodeNew(0, minX, minY, maxX, maxY);
    if (!t->root){
        free(t);
        return NULL;
    }
    return t;
}

void qtreeFreeAndItems(qtree *t, int freeItems){
    if (t){
        nodeFree(t->root, freeItems);
        free(t);
    }
}
void qtreeFree(qtree *t){
    return qtreeFreeAndItems(t, 0);
}

static double nodeClipX(node *n, double x){
    if (x < n->minX){
        return n->minX;
    }
    if (x > n->maxX){
        return n->maxX;
    }
    return x;
}

static double nodeClipY(node *n, double y){
    if (y < n->minY){
        return n->minY;
    }
    if (y > n->maxY){
        return n->maxY;
    }
    return y;
}

static splitResult split(int quad, double minX, double minY, double maxX, double maxY){
    splitResult sr;
    switch (quad){
    default:
        sr.minX = 0;
        sr.minY = 0;
        sr.maxX = 0;
        sr.maxY = 0;
        return sr;
    case 0:
        sr.minX = minX;
        sr.minY = minY;
        sr.maxX = ((maxX-minX)/2)+minX;
        sr.maxY = ((maxY-minY)/2)+minY;
        return sr;
    case 1:
        sr.minX = ((maxX-minX)/2)+minX;
        sr.minY = minY;
        sr.maxX = maxX;
        sr.maxY = ((maxY-minY)/2)+minY;
        return sr;
    case 2:
        sr.minX = minX;
        sr.minY = ((maxY-minY)/2)+minY;
        sr.maxX = ((maxX-minX)/2)+minX;
        sr.maxY = maxY;
        return sr;
    case 3:
        sr.minX = ((maxX-minX)/2)+minX;
        sr.minY = ((maxY-minY)/2)+minY;
        sr.maxX = maxX;
        sr.maxY = maxY;
        return sr;
    }
}

static int nodeInsert(node *n, double x, double y, void *item){
    //printf("A %fx%f, %f, %f, %f, %f\n", x, y, n->minX, n->minY, n->maxX, n->maxY);
    // attempt to add point if there's room.
    if (n->count < MAX_POINTS) {
        point p = {x, y, item};
        if (n->length == n->count) {
            int length;
            if (n->length==0){
                length = 1;
            } else if (APPEND_GROWTH){
                length = n->length * 2;
            } else {
                length = n->length + 1;
            }
            point *npoints = realloc(n->points, length*sizeof(point));
            if (!npoints){
                //printf("A\n");
                return 0;
            }
            n->points = npoints;
            n->length = length;
        }
        n->points[n->count] = p;
        n->count++;
        return 1;
    }
    // find the quad to insert item into.
    //printf("B %fx%f, %f, %f, %f, %f\n", x, y, n->minX, n->minY, n->maxX, n->maxY);
    for (int i=0;i<4;i++){
        node *q = n->nodes[i];
        if (q==NULL){
            splitResult sr = split(i, n->minX, n->minY, n->maxX, n->maxY);
            if (x >= sr.minX && x <= sr.maxX && y >= sr.minY && y <= sr.maxY){
                //printf("  B1:%d %f, %f, %f, %f\n", i, sr.minX ,sr.maxX ,sr.minY ,sr.maxY);
                n->nodes[i] = nodeNew(i, sr.minX, sr.minY, sr.maxX, sr.maxY);    
                if (!n->nodes[i]){
                    //printf("B\n");
                    return 0;
                }
                q = n->nodes[i];
                return nodeInsert(q, x, y, item);
            } else{
                //printf("  B2:%d %f, %f, %f, %f\n", i, sr.minX ,sr.maxX ,sr.minY ,sr.maxY);
            }
        } else {
            if (x >= q->minX && x <= q->maxX && y >= q->minY && y <= q->maxY){
                //printf("  B3:%d %f, %f, %f, %f\n", i, q->minX, q->maxX, q->minY, q->maxY);
                return nodeInsert(q, x, y, item);
            } else{
                //printf("  B4:%d %f, %f, %f, %f\n", i, q->minX, q->maxX, q->minY, q->maxY);
            }
        }
    }
    //printf("C %fx%f, %f, %f, %f, %f\n", x, y, n->minX, n->minY, n->maxX, n->maxY);
    return 0;
}

int qtreeInsert(qtree *t, double x, double y, void *item){
    return nodeInsert(t->root, nodeClipX(t->root, x), nodeClipY(t->root, y), item);
}

static int nodeCount(node *n, int counter){
    counter += n->count;
    for (int i=0;i<4;i++) {
        if (n->nodes[i] != NULL) {
            counter = nodeCount(n->nodes[i], counter);
        }
    }
    return counter;
}

int qtreeCount(qtree *t){
    return nodeCount(t->root, 0);
}

static int nodeRemove(node *n, double x, double y, void *item) {
    for (int i=0;i<n->count;i++) {
        if (n->points[i].item == item) {
            n->points[i] = n->points[n->count-1];
            n->count--;
            return 1;
        }
    }
    for (int i=0;i<4;i++){
        node *q = n->nodes[i];
        if (q){
            if (x >= q->minX && x <= q->maxX && y >= q->minY && y <= q->maxY){
                return nodeRemove(q, x, y, item);
            }
        }
    }
    return 0;
}

int qtreeRemove(qtree *t, double x, double y, void *item){
    return nodeRemove(t->root, nodeClipX(t->root, x), nodeClipY(t->root, y), item);
}

static int pushStack(qtreeIterator *qi, node *n){
    if (qi->len == qi->cap){
        int cap = qi->cap;
        if (!qi->cap){
            cap = 1;
        } else{
            cap *= 2;
        }
        stackItem *nstack = realloc(qi->stack, sizeof(stackItem)*cap);
        if (!nstack){
            return 0;
        }
        qi->stack = nstack;
        qi->cap = cap;
    }
    stackItem item;
    memset(&item, 0, sizeof(stackItem));
    item.n = n;
    qi->stack[qi->len] = item;
    qi->len++;
    return 1;
}

static int overlaps(double nMinX, double nMinY, double nMaxX, double nMaxY, double minX, double minY, double maxX, double maxY) {
    if (nMinX > maxX || minX > nMaxX) {
        return 0;
    }
    if (nMinY > maxY || minY > nMaxY) {
        return 0;
    }
    return 1;
}



qtreeIterator *qtreeNewIterator(qtree *t, double minX, double minY, double maxX, double maxY){
    qtreeIterator *qi = malloc(sizeof(qtreeIterator));
    if (!qi){
        return NULL;
    }
    memset(qi, 0, sizeof(qtreeIterator));
    qi->minX = minX;
    qi->minY = minY;
    qi->maxX = maxX;
    qi->maxY = maxY;
    node *n = t->root;
    if (overlaps(qi->minX, qi->minY, qi->maxX, qi->maxY, n->minX, n->minY, n->maxX, n->maxY)){
        if (!pushStack(qi, n)){
            free(qi);
            return NULL;
        }
    }
    return qi;
}

void qtreeFreeIterator(qtreeIterator *qi){
    if (qi){
        free(qi->stack);
        free(qi);
    }
}


int qtreeIteratorNext(qtreeIterator *qi){
    if (!qi->len){
        return 0;
    }
    stackItem *item = &(qi->stack[qi->len-1]);

    while (item->pointIdx < item->n->count){
        point p = item->n->points[item->pointIdx];
        item->pointIdx++;
        if (p.x >= qi->minX && p.x <= qi->maxX && p.y >= qi->minY && p.y <= qi->maxY){
            qi->x = p.x;
            qi->y = p.y;
            qi->item = p.item;
            return 1;
        }
    }

    while (item->quadIdx < 4){
        node *n = item->n->nodes[item->quadIdx];
        item->quadIdx++;
        if (n){
            if (overlaps(qi->minX, qi->minY, qi->maxX, qi->maxY, n->minX, n->minY, n->maxX, n->maxY)){
                if (!pushStack(qi, n)){
                    qi->len = 0;
                    qi->mem = 1;
                    return 0;
                }
                return qtreeIteratorNext(qi);
            }
        }
    }
    qi->len--;
    return qtreeIteratorNext(qi);
}
