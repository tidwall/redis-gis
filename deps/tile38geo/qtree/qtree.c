/* 2D Quadratic Tree for points and rectangles primed for Geospatial indexing. */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>

#define MAX_OVERLAPS  16 // max number of overlaps before the node splinters.
#define MAX_RECTS     16 // max number of rects before the node shakes.
#define APPEND_GROW   1 // True for faster inserts, False for lower memory.

typedef struct node node;
typedef struct qtree qtree;
typedef struct ritem ritem;

struct ritem {
	double minX, minY, maxX, maxY;
	void *item;
};

struct node {
	double minX, minY, maxX, maxY;
	double midX, midY;
	int rlen, rcap;
	ritem *ritems;
	int olen, ocap;
	ritem *overlaps;
	node *nodes[4];
	node *onodes[4];
};

struct qtree {
	node *root;
};

static int nodeInsert(node *n, ritem item);
// static node *nodeNew(double minX, double minY, double maxX, double maxY);
// static node *nodeNewQuad(double minX, double minY, double maxX, double maxY, int quad);

static node *nodeNew(double minX, double minY, double maxX, double maxY){
	node *n = malloc(sizeof(node));
	if (!n){
		return NULL;
	}
	memset(n, 0, sizeof(node));
	n->minX = minX;
	n->minY = minY;
	n->maxX = maxX;
	n->maxY = maxY;
	n->midX = (maxX-minX)/2+minX;
	n->midY = (maxY-minY)/2+minY;
	return n;
}

static node *nodeNewQuad(double minX, double minY, double maxX, double maxY, int quad){
	switch (quad){
	default:
		return NULL;
	case 0:
		return nodeNew(minX, minY, (maxX-minX)/2+minX, (maxY-minY)/2+minY);
	case 1:
		return nodeNew((maxX-minX)/2+minX, minY, maxX, (maxY-minY)/2+minY);
	case 2:
		return nodeNew(minX, (maxY-minY)/2+minY, (maxX-minX)/2+minX, maxY);
	case 3:
		return nodeNew((maxX-minX)/2+minX, (maxY-minY)/2+minY, maxX, maxY);
	}
}

static void nodeFree(node *n){
	if (!n){
		return;
	}
	if (n->ritems){
		free(n->ritems);
	}
	if (n->overlaps){
		free(n->overlaps);
	}
	for (int i=0;i<4;i++){
		if (n->nodes[i]){
			nodeFree(n->nodes[i]);
		}
	}
	free(n);
}

static int nodeDetectQuad(node *n, ritem item){
	int quad = -1;
	if (item.minX < n->minX){
		return -1;
	} else if (item.minX < n->midX){
		if (item.maxX > n->midX){
			return -1;
		}
		quad = 0;
	} else if (item.minX > n->maxX) {
		return -1;
	} else {
		if (item.maxX > n->maxX){
			return -1;
		}
		quad = 1;
	}
	if (item.minY < n->minY){
		return -1;
	} else if (item.minY < n->midY){
		if (item.maxY > n->midY){
			return -1;
		}
		quad += 2;
	} else if (item.minY > n->maxY) {
		return -1;
	} else {
		if (item.maxY > n->maxY){
			return -1;
		}
		quad += 0;
	}
	return quad;
}

static int nodeInsertOverlap(node *n, ritem item){
	if (n->olen == n->ocap){
		int nocap = n->ocap;
		if (APPEND_GROW){
			if (nocap==0){
				nocap = 1;
			} else {
				nocap *= 2;
			}
		} else{
			nocap++;
		}
		ritem *noverlaps = realloc(n->overlaps, nocap*sizeof(ritem));
		if (!noverlaps){
			return 0;
		}
		n->ocap = nocap;
		n->overlaps = noverlaps;
	}
	n->overlaps[n->olen] = item;
	n->olen++;
	return 1;
}

static int nodeShake(node *n){
	/* Move all ritems to quad nodes or the overlap bucket. */
	for (int i=0;i<n->rlen;i++){
		ritem item = n->ritems[i];
		int quad = nodeDetectQuad(n, item);
		if (quad == -1){
			if (!nodeInsertOverlap(n, item)){
				return 0;
			}
		} else {
			node *q = n->nodes[quad];
			if (q == NULL){
				q = nodeNewQuad(n->minX, n->minY, n->maxX, n->maxY, quad);
				if (!q){
					return 0;
				}
				n->nodes[quad] = q;
			}
			if (!nodeInsert(q, item)){
				return 0;
			}
		}
		//printf("%d %.0fx%.0f,%.0fx%.0f\n", quad, item.minX,item.minY,item.maxX,item.maxY);
	}
	n->rlen = 0;
	return 1;
}

static int nodeInsert(node *n, ritem item){
	if (n->rlen == MAX_RECTS){
		if (!nodeShake(n)){
			return 0;
		}
	}
	if (n->rlen == n->rcap){
		int nrcap = n->rcap;
		if (APPEND_GROW){
			if (nrcap==0){
				nrcap = 1;
			} else {
				nrcap *= 2;
			}
		} else{
			nrcap++;
		}
		ritem *nritems = realloc(n->ritems, nrcap*sizeof(ritem));
		if (!nritems){
			return 0;
		}
		n->rcap = nrcap;
		n->ritems = nritems;
	}
	n->ritems[n->rlen] = item;
	n->rlen++;
	return 1;
}


static int nodeRemove(node *n, ritem item){
	// search overlaps
	for (int i=0;i<n->olen;i++){
		if (n->overlaps[i].item == item.item){
			n->overlaps[i] = n->overlaps[n->olen-1];
			n->olen--;
			return 1;
		}
	}
	// search items
	for (int i=0;i<n->rlen;i++){
		if (n->ritems[i].item == item.item){
			n->ritems[i] = n->ritems[n->rlen-1];
			n->rlen--;
			return 1;
		}	
	}
	// search quads
	for (int i=0;i<4;i++){
		if (n->nodes[i]){
			if (nodeRemove(n->nodes[i], item)){
				return 1;
			}
		}
	}
	return 0;
}

static int nodeCount(node *n){
	int counter = 0;
	counter += n->olen;
	counter += n->rlen;
	for (int i=0;i<4;i++){
		if (n->nodes[i]){
			counter += nodeCount(n->nodes[i]);
		}
	}
	return counter;
}

static int overlaps(ritem item1, ritem item2) {
    if (item1.minX > item2.maxX || item2.minX > item1.maxX) {
        return 0;
    }
    if (item1.minY > item2.maxY || item2.minY > item1.maxY) {
        return 0;
    }
    return 1;
}


static int nodeSearch(node *n, ritem item){
	int counter = 0;
	// for (int i=0;i<n->olen;i++){
	// 	if (overlaps(n->overlaps[i], item)){
	// 		counter++;
	// 	}	
	// }
	for (int i=0;i<n->rlen;i++){
		if (overlaps(n->ritems[i], item)){
			counter++;
		}	
	}
	for (int i=0;i<4;i++){
		if (n->nodes[i]){
			ritem qitem = {n->nodes[i]->minX, n->nodes[i]->minY, n->nodes[i]->maxX, n->nodes[i]->maxY};
			if (overlaps(qitem, item)){
				counter += nodeSearch(n->nodes[i], item);
			}
		}
	}
	return counter;
}



qtree *qtreeNew(){
	qtree *tr = malloc(sizeof(qtree));
	if (!tr){
		return NULL;
	}
	memset(tr, 0, sizeof(qtree));
	return tr;
}

void qtreeFree(qtree *tr){
	if (!tr){
		return;
	}
	if (tr->root){
		nodeFree(tr->root);
	}
	free(tr);
}




int qtreeInsert(qtree *tr, double minX, double minY, double maxX, double maxY, void *item){
	if (!tr){
		return 0;
	}
	if (minX > maxX || minY > maxY){
		return 0;
	}
	if (!tr->root){
		tr->root = nodeNew(-180, -90, 180, 90);
		if (!tr->root){
			return 0;
		}
	}
	ritem ri = {minX, minY, maxX, maxY, item};
	return nodeInsert(tr->root, ri);
}

int qtreeRemove(qtree *tr, double minX, double minY, double maxX, double maxY, void *item){
	if (!tr || !tr->root){
		return 0;
	}
	if (minX > maxX || minY > maxY){
		return 0;
	}
	ritem ri = {minX, minY, maxX, maxY, item};
	return nodeRemove(tr->root, ri);
}

int qtreeCount(qtree *tr){
	if (!tr || !tr->root){
		return 0;
	}
	return nodeCount(tr->root);
}

int qtreeSearch(qtree *tr, double minX, double minY, double maxX, double maxY){
	if (!tr || !tr->root){
		return 0;
	}
	ritem ri = {minX, minY, maxX, maxY, 0};
	return nodeSearch(tr->root, ri);
}

//////////////////////////

static double randd() { return ((rand()%RAND_MAX) / (double)RAND_MAX);}
static double randx() { return randd() * 360.0 - 180.0;}
static double randy() { return randd() * 180.0 - 90.0;}

int main(){
	srand(time(NULL)/clock());
	qtree *tr = qtreeNew();
	assert(tr);
	int n = 1000000;
	clock_t start = clock();
	for (long i=0;i<n;i++){
		double minX = randx();
		double minY = randy();
		double maxX = minX+(randd()*10+0.0001);
		double maxY = minY+(randd()*10+0.0001);
		assert(qtreeInsert(tr, minX, minY, maxX, maxY, (void*)i));
	}
	double elapsed = ((double)(clock()-start) / (double)CLOCKS_PER_SEC);
	assert(qtreeCount(tr) == n);
	printf("inserted %d items in %.2f secs, %.0f ops/s\n", n, elapsed, (double)n/elapsed);

	// assert(qtreeInsert(tr, -10, -10, 1, 1, (void*)2));
	// assert(qtreeInsert(tr, 11, 11, 12, 12, (void*)3));
	// assert(qtreeInsert(tr, 0, -10, 10, 0, (void*)4));
	// assert(qtreeInsert(tr, 0, -10, 10, 0, (void*)5));
	//assert(qtreeInsert(tr, 33, 115, 10, 0, "12x12,13x13"));


//	assert(qtreeRemove(tr, 11, 11, 12, 12, "11x11,12x12"));

	n = 100;
	start = clock();
	for (int i=0;i<n;i++){
		double minX = randx();
		double minY = randy();
		double maxX = minX+(randd()*10+0.0001);
		double maxY = minY+(randd()*10+0.0001);
		qtreeSearch(tr, minX, minY, maxX, maxY);	
	}
	elapsed = ((double)(clock()-start) / (double)CLOCKS_PER_SEC);

	printf("searched %d queries in %.2f secs, %.0f ops/s\n", n, elapsed, (double)n/elapsed);

	qtreeFree(tr);
	return 0;
}