// #include <stdio.h>
// #include <stdlib.h>
// #include <math.h>
// #include <string.h>
// #include <assert.h>
// #include <time.h>

#define NUM_DIMS 2

#include "rtree_base.c"
#include "rtree.h"

typedef struct rtree {
	nodeT *root;
} rtree;

rtree *rtreeNew() {
	rtree *tr = malloc(sizeof(rtree));
	memset(tr, 0, sizeof(rtree));
	return tr;
}

void rtreeFree(rtree *tr){
	if (!tr){
		return;
	}
	if (tr->root){
		freeNode(tr->root);
	}
	free(tr);
}

// Search finds all items in bounding box.
int rtreeSearch(rtree *tr, double minX, double minY, double maxX, double maxY) {
	if (!tr->root){
		return 0;
	}
	return search(tr->root, itemRect(minX, minY, maxX, maxY, NULL));
}

// Remove removes item from rtree
int rtreeRemove(rtree *tr, double minX, double minY, double maxX, double maxY, void *item) {
	if (tr->root) {
		removeRect(itemRect(minX, minY, maxX, maxY, item), item, &(tr->root));
	}
	return 1;
}

// Count return the number of items in rtree.
int rtreeCount(rtree *tr) {
	if (!tr || !tr->root){
		return 0;
	}
	return countRec(tr->root, 0);
}

// Insert inserts item into rtree
int rtreeInsert(rtree *tr, double minX, double minY, double maxX, double maxY, void *item) {
	if (!tr->root) {
		tr->root = malloc(sizeof(nodeT));
		memset(tr->root, 0, sizeof(nodeT));
	}
	insertRect(itemRect(minX, minY, maxX, maxY, item), item, &(tr->root), 0);
	return 1;
}