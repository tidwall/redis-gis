#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <stdbool.h>

#define MAX_NODES            16
#define MIN_NODES            (MAX_NODES/2)
#define USE_SPHERICAL_VOLUME true
#define UNIT_SPHERE_VOLUME   3.141593  

// unitSphereVolume1 = 2.000000
// unitSphereVolume2 = 3.141593
// unitSphereVolume3 = 4.188790
// unitSphereVolume4 = 4.934802

typedef double float64;
typedef struct branchT branchT;
typedef struct nodeT nodeT;
typedef struct rectT rectT;
typedef struct listNodeT listNodeT;
typedef struct partitionVarsT partitionVarsT;
typedef struct RTree RTree;
typedef struct Item Item;

struct Item {
	double minX, minY, maxX, maxY;
	void *ptr;
};

/// Minimal bounding rectangle (n-dimensional)
struct rectT {
	float64 min[2]; ///< Min dimensions of bounding box
	float64 max[2]; ///< Max dimensions of bounding box
};

/// May be data or may be another subtree
/// The parents level determines this.
/// If the parents level is 0, then this is data
struct branchT {
	rectT rect;    ///< Bounds
	nodeT *child;  ///< Child node
	void  *item;   ///< Data ID or Ptr
};

/// nodeT for each branch level
struct nodeT {
	int     count;               ///< Count
	int     level;               ///< Leaf is zero, others positive
	branchT branch[MAX_NODES];   ///< branchT
};

static bool isInternalNode(nodeT *node) { 
	// Not a leaf, but a internal node
	return node->level > 0;
}

/// A link list of nodes for reinsertion after a delete operation
struct listNodeT {
	listNodeT *next; ///< Next in list
	nodeT     *node; ///< nodeT
};

/// Variables for finding a split partition
struct partitionVarsT {
	int     partition[MAX_NODES+1];
	int     total;
	int     minFill;
	bool    taken[MAX_NODES+1];
	int     count[2];
	rectT   cover[2];
	float64  area[2];
	branchT branchBuf[MAX_NODES+1];
	int     branchCount;
	rectT   coverSplit;
	float64  coverSplitArea;
};

// RTree is an implementation of an rtree
struct RTree {
	nodeT *root;
};


static inline rectT itemRect(Item item) {
	rectT r;
	r.min[0] = item.minX;
	r.min[1] = item.minY;
	r.max[0] = item.maxX;
	r.max[1] = item.maxY;
	return r;
}

static inline float64 min(float64 a, float64 b) {
	if (a < b) {
		return a;
	}
	return b;
}
static inline float64 max(float64 a, float64 b) {
	if (a > b) {
		return a;
	}
	return b;
}



static bool insertRect(rectT rect, Item item, nodeT **root, int level);
static bool insertRectRec(rectT rect, Item item, nodeT *node, nodeT **newNode, int level);
static int pickBranch(rectT rect, nodeT *node);
static float64 rectVolume(rectT rect);
static float64 rectSphericalVolume(rectT rect);
static float64 calcRectVolume(rectT rect);
static rectT combineRect(rectT rectA, rectT rectB);
static rectT nodeCover(nodeT *node);
static bool addBranch(branchT *branch, nodeT *node, nodeT **newNode);
static void splitNode(nodeT *node, branchT *branch, nodeT **newNode);
static void getBranches(nodeT *node, branchT *branch, partitionVarsT *parVars);
static void choosePartition(partitionVarsT *parVars, int minFill);
static void classify(int index, int group, partitionVarsT *parVars);
static void pickSeeds(partitionVarsT *parVars);
static void initParVars(partitionVarsT *parVars, int maxRects, int minFill);
static void loadNodes(nodeT *nodeA, nodeT *nodeB, partitionVarsT *parVars);
static int countRec(nodeT *node, int counter);
static bool removeRect(rectT rect, Item item, nodeT **root);
static bool removeRectRec(rectT rect, Item item, nodeT *node, listNodeT **listNode);
static void reInsert(nodeT *node, listNodeT **listNode);
static bool overlap(rectT rectA, rectT rectB);
static void disconnectBranch(nodeT *node, int index);


// New creates a new RTree
RTree *rtreeNew() {
	RTree *tr = malloc(sizeof(RTree));
	assert(tr);
	memset(tr, 0, sizeof(RTree));
	return tr;
}

void rtreeFree(RTree *tr){
	if (!tr){
		return;
	}
	free(tr);
}

// Insert inserts item into rtree
int rtreeInsert(RTree *tr, double minX, double minY, double maxX, double maxY, void *item) {
	if (!tr->root) {
		tr->root = malloc(sizeof(nodeT));
		assert(tr->root);
		memset(tr->root, 0, sizeof(nodeT));
	}
	Item m = {minX, minY, maxX, maxY, item};
	insertRect(itemRect(m), m, &(tr->root), 0);
	return 1;
}

// Insert a data rectangle into an index structure.
// InsertRect provides for splitting the root;
// returns 1 if root was split, 0 if it was not.
// The level argument specifies the number of steps up from the leaf
// level to insert; e.g. a data rectangle goes in at level = 0.
// InsertRect2 does the recursion.
static bool insertRect(rectT rect, Item item, nodeT **root, int level) {
	nodeT *newRoot = NULL;
	nodeT *newNode = NULL;
	branchT branch;
	memset(&branch, 0, sizeof(branchT));

	// if *root == nil {
	// 	println(">> root is nil")
	// }
	assert(*root);
	
	if (insertRectRec(rect, item, *root, &newNode, level)) { // Root split
		newRoot = malloc(sizeof(nodeT)); // Grow tree taller and new root
		assert(newRoot);
		memset(newRoot, 0, sizeof(nodeT));
		newRoot->level = (*root)->level + 1;
		branch.rect = nodeCover(*root);
		branch.child = *root;
		// if branch.child == nil {
		// 	println(">> child assigned is nil 2")
		// }
		assert(branch.child);
		addBranch(&branch, newRoot, NULL);
		branch.rect = nodeCover(newNode);
		branch.child = newNode;
		// if branch.child == nil {
		// 	println(">> child assigned is nil 3")
		// }
		assert(branch.child);
		addBranch(&branch, newRoot, NULL);
		*root = newRoot;
		return true;
	}
	return false;
}


// Inserts a new data rectangle into the index structure.
// Recursively descends tree, propagates splits back up.
// Returns 0 if node was not split.  Old node updated.
// If node was split, returns 1 and sets the pointer pointed to by
// new_node to point to the new node.  Old node updated to become one of two.
// The level argument specifies the number of steps up from the leaf
// level to insert; e.g. a data rectangle goes in at level = 0.
static bool insertRectRec(rectT rect, Item item, nodeT *node, nodeT **newNode, int level) {
	int index = 0;
	branchT branch;
	memset(&branch, 0, sizeof(branchT));
	nodeT *otherNode = NULL;
	// Still above level for insertion, go down tree recursively
	if (node == NULL) {
		return false;
	}
	if (node->level > level) {
		index = pickBranch(rect, node);
		if (!insertRectRec(rect, item, node->branch[index].child, &otherNode, level)) {
		 	// Child was not split
		 	node->branch[index].rect = combineRect(rect, node->branch[index].rect);
		 	return false;
		} // Child was split
		node->branch[index].rect = nodeCover(node->branch[index].child);
		branch.child = otherNode;
		// if branch.child == nil {
		// 	println(">> child assigned is nil")
		// }
		assert(branch.child);
		branch.rect = nodeCover(otherNode);
		return addBranch(&branch, node, newNode);
	} else if (node->level == level) { // Have reached level for insertion. Add rect, split if necessary
		branch.rect = rect;
		branch.item = item.ptr;
		// Child field of leaves contains id of data record
		return addBranch(&branch, node, newNode);
	}
	// Should never occur
	return false;
}


// Pick a branch.  Pick the one that will need the smallest increase
// in area to accommodate the new rectangle.  This will result in the
// least total area for the covering rectangles in the current node.
// In case of a tie, pick the one which was smaller before, to get
// the best resolution when searching.
static int pickBranch(rectT rect, nodeT *node) {
	bool firstTime = true;
	float64 increase = 0;
	float64 bestIncr = -1;
	float64 area = 0;
	float64 bestArea = 0;
	int best = 0;
	rectT tempRect;
	memset(&tempRect, 0, sizeof(rectT));
	for (int index = 0; index < node->count; index++) {
	 	rectT curRect = node->branch[index].rect;
	 	area = calcRectVolume(curRect);
	 	tempRect = combineRect(rect, curRect);
		increase = calcRectVolume(tempRect) - area;
		if ((increase < bestIncr) || firstTime) {
			best = index;
			bestArea = area;
			bestIncr = increase;
			firstTime = false;
		} else if ((increase == bestIncr) && (area < bestArea)) {
			best = index;
			bestArea = area;
			bestIncr = increase;
		}
	}
	return best;
}


// Calculate the n-dimensional volume of a rectangle
static float64 rectVolume(rectT rect) {
	float64 volume = 1;
	for (int index = 0; index < 2; index++) {
		volume *= rect.max[index] - rect.min[index];
	}
	return volume;
}

// The exact volume of the bounding sphere for the given rectT
static float64 rectSphericalVolume(rectT rect) {
	float64 sumOfSquares = 0;
	float64 radius = 0;
	for (int index = 0; index < 2; index++) {
		float64 halfExtent = (rect.max[index] - rect.min[index]) * 0.5;
		sumOfSquares += halfExtent * halfExtent;
	}
	radius = sqrt(sumOfSquares);
	return radius * radius * UNIT_SPHERE_VOLUME;
}

// Use one of the methods to calculate retangle volume
static float64 calcRectVolume(rectT rect) {
	if (USE_SPHERICAL_VOLUME) {
		return rectSphericalVolume(rect); // Slower but helps certain merge cases
	}
	return rectVolume(rect); // Faster but can cause poor merges
}

// Combine two rectangles into larger one containing both
static rectT combineRect(rectT rectA, rectT rectB) {
	rectT newRect;
	memset(&newRect, 0, sizeof(rectT));
	for (int index = 0; index < 2; index++) {
		newRect.min[index] = min(rectA.min[index], rectB.min[index]);
		newRect.max[index] = max(rectA.max[index], rectB.max[index]);
	}
	return newRect;
}


// Find the smallest rectangle that includes all rectangles in branches of a node.
static rectT nodeCover(nodeT *node) {
	bool firstTime = true;
	rectT rect;
	memset(&rect, 0, sizeof(rectT));
	for (int index = 0; index < node->count; index++) {
		if (firstTime) {
			rect = node->branch[index].rect;
			firstTime = false;
		} else {
			rect = combineRect(rect, node->branch[index].rect);
		}
	}
	return rect;
}


// Add a branch to a node.  Split the node if necessary.
// Returns 0 if node not split.  Old node updated.
// Returns 1 if node split, sets *new_node to address of new node.
// Old node updated, becomes one of two.
static bool addBranch(branchT *branch, nodeT *node, nodeT **newNode) {
	if (node->count < MAX_NODES) { // Split won't be necessary
		node->branch[node->count] = *branch;
		node->count++;
		return false;
	}
	splitNode(node, branch, newNode);
	return true;
}


// Split a node.
// Divides the nodes branches and the extra one between two nodes.
// Old node is one of the new ones, and one really new one is created.
// Tries more than one method for choosing a partition, uses best result.
static void splitNode(nodeT *node, branchT *branch, nodeT **newNode) {
	// Could just use local here, but member or external is faster since it is reused
	partitionVarsT localVars;
	memset(&localVars, 0, sizeof(partitionVarsT));
	partitionVarsT *parVars = &localVars;
	int level = 0;
	// Load all the branches into a buffer, initialize old node
	level = node->level;
	getBranches(node, branch, parVars);
	// Find partition
	choosePartition(parVars, MIN_NODES);
	// Put branches from buffer into 2 nodes according to chosen partition
	*newNode = malloc(sizeof(nodeT));
	assert(*newNode);
	memset(*newNode, 0, sizeof(nodeT));
	node->level = level;
	(*newNode)->level = node->level;
	loadNodes(node, *newNode, parVars);
}

// Load branch buffer with branches from full node plus the extra branch.
static void getBranches(nodeT *node, branchT *branch, partitionVarsT *parVars) {
	// Load the branch buffer
	for (int index = 0; index < MAX_NODES; index++) {
		parVars->branchBuf[index] = node->branch[index];
	}
	parVars->branchBuf[MAX_NODES] = *branch;
	parVars->branchCount = MAX_NODES + 1;
	// Calculate rect containing all in the set
	parVars->coverSplit = parVars->branchBuf[0].rect;
	for (int index = 1; index < MAX_NODES+1; index++) {
		parVars->coverSplit = combineRect(parVars->coverSplit, parVars->branchBuf[index].rect);
	}
	parVars->coverSplitArea = calcRectVolume(parVars->coverSplit);
	node->count = 0;
	node->level = -1;
}

// Method #0 for choosing a partition:
// As the seeds for the two groups, pick the two rects that would waste the
// most area if covered by a single rectangle, i.e. evidently the worst pair
// to have in the same group.
// Of the remaining, one at a time is chosen to be put in one of the two groups.
// The one chosen is the one with the greatest difference in area expansion
// depending on which group - the rect most strongly attracted to one group
// and repelled from the other.
// If one group gets too full (more would force other group to violate min
// fill requirement) then other group gets the rest.
// These last are the ones that can go in either group most easily.
static void choosePartition(partitionVarsT *parVars, int minFill) {
	float64 biggestDiff = 0;
	int group = 0;
	int chosen = 0;
	int betterGroup = 0;
	initParVars(parVars, parVars->branchCount, minFill);
	pickSeeds(parVars);
	while (((parVars->count[0] + parVars->count[1]) < parVars->total) &&
		(parVars->count[0] < (parVars->total - parVars->minFill)) &&
		(parVars->count[1] < (parVars->total - parVars->minFill))) {
		biggestDiff = -1;
		for (int index = 0; index < parVars->total; index++) {
			if (!parVars->taken[index]) {
				rectT curRect = parVars->branchBuf[index].rect;
				rectT rect0 = combineRect(curRect, parVars->cover[0]);
				rectT rect1 = combineRect(curRect, parVars->cover[1]);
				float64 growth0 = calcRectVolume(rect0) - parVars->area[0];
				float64 growth1 = calcRectVolume(rect1) - parVars->area[1];
				float64 diff = growth1 - growth0;
				if (diff >= 0) {
					group = 0;
				} else {
					group = 1;
					diff = -diff;
				}
				if (diff > biggestDiff) {
					biggestDiff = diff;
					chosen = index;
					betterGroup = group;
				} else if ((diff == biggestDiff) && (parVars->count[group] < parVars->count[betterGroup])) {
					chosen = index;
					betterGroup = group;
				}
			}
		}
		classify(chosen, betterGroup, parVars);
	}
	// If one group too full, put remaining rects in the other
	if ((parVars->count[0] + parVars->count[1]) < parVars->total) {
		if (parVars->count[0] >= parVars->total-parVars->minFill) {
			group = 1;
		} else {
			group = 0;
		}
		for (int index = 0; index < parVars->total; index++) {
			if (!parVars->taken[index]) {
				classify(index, group, parVars);
			}
		}
	}
}


// Copy branches from the buffer into two nodes according to the partition.
static void loadNodes(nodeT *nodeA, nodeT *nodeB, partitionVarsT *parVars) {
	for (int index = 0; index < parVars->total; index++) {
		if (parVars->partition[index] == 0) {
			addBranch(&parVars->branchBuf[index], nodeA, NULL);
		} else if (parVars->partition[index] == 1) {
			addBranch(&parVars->branchBuf[index], nodeB, NULL);
		}
	}
}

// Initialize a partitionVarsT structure.
static void initParVars(partitionVarsT *parVars, int maxRects, int minFill) {
	parVars->count[1] = 0;
	parVars->count[0] = parVars->count[1];
	parVars->area[1] = 0;
	parVars->area[0] = parVars->area[1];
	parVars->total = maxRects;
	parVars->minFill = minFill;
	for (int index = 0; index < maxRects; index++) {
		parVars->taken[index] = false;
		parVars->partition[index] = -1;
	}
}

static void pickSeeds(partitionVarsT *parVars) {
	int seed0 = 0;
	int seed1 = 0;
	float64 worst = 0;
	float64 waste = 0;
	float64 area[MAX_NODES+1];
	memset(&area, 0, sizeof(area));
	for (int index = 0; index < parVars->total; index++) {
		area[index] = calcRectVolume(parVars->branchBuf[index].rect);
	}
	worst = -parVars->coverSplitArea - 1;
	for (int indexA = 0; indexA < parVars->total-1; indexA++) {
		for (int indexB = indexA + 1; indexB < parVars->total; indexB++) {
			rectT oneRect = combineRect(parVars->branchBuf[indexA].rect, parVars->branchBuf[indexB].rect);
			waste = calcRectVolume(oneRect) - area[indexA] - area[indexB];
			if (waste > worst) {
				worst = waste;
				seed0 = indexA;
				seed1 = indexB;
			}
		}
	}
	classify(seed0, 0, parVars);
	classify(seed1, 1, parVars);
}

// Put a branch in one of the groups.
static void classify(int index, int group, partitionVarsT *parVars) {
	parVars->partition[index] = group;
	parVars->taken[index] = true;

	if (parVars->count[group] == 0) {
		parVars->cover[group] = parVars->branchBuf[index].rect;
	} else {
		parVars->cover[group] = combineRect(parVars->branchBuf[index].rect, parVars->cover[group]);
	}
	parVars->area[group] = calcRectVolume(parVars->cover[group]);
	parVars->count[group]++;
}

// Count return the number of items in rtree.
int rtreeCount(RTree *tr) {
	if (!tr || !tr->root){
		return 0;
	}
	return countRec(tr->root, 0);
}


static int countRec(nodeT *node, int counter) {
	if (node->level > 0) { // not a leaf node
		for (int index = 0; index < node->count; index++) {
			counter = countRec(node->branch[index].child, counter);
		}
	} else { // A leaf node
		counter += node->count;
	}
	return counter;
}

// // Search finds all items in bounding box.
// static void rtreeSearch(RTree *tr, minX, minY, maxX, maxY float64, iterator func(item Item) bool) {
// 	if iterator == nil {
// 		return
// 	}
// 	rect := rectT{
// 		min: [2]float64{minX, minY},
// 		max: [2]float64{maxX, maxY},
// 	}
// 	// NOTE: May want to return search result another way, perhaps returning the number of found elements here.
// 	if tr.root == nil {
// 		tr.root = &nodeT{}
// 	}
// 	search(tr.root, rect, iterator)
// }


// Remove removes item from rtree
int rtreeRemove(RTree *tr, double minX, double minY, double maxX, double maxY, void *item) {
	if (tr->root) {
		Item m = {minX, minY, maxX, maxY, item};
		removeRect(itemRect(m), m, &(tr->root));
	}
	return 1;
}

// Delete a data rectangle from an index structure.
// Pass in a pointer to a rectT, the tid of the record, ptr to ptr to root node.
// Returns 1 if record not found, 0 if success.
// RemoveRect provides for eliminating the root.
static bool removeRect(rectT rect, Item item, nodeT **root) {
	nodeT *tempNode = NULL;
	listNodeT *reInsertList = NULL;
	if (!removeRectRec(rect, item, *root, &reInsertList)) {
		// Found and deleted a data item
		// Reinsert any branches from eliminated nodes
		while (reInsertList != NULL) {
			tempNode = reInsertList->node;
			for (int index = 0; index < tempNode->count; index++) {
				insertRect(tempNode->branch[index].rect,
					tempNode->branch[index].item,
					root,
					tempNode->level);
			}
			reInsertList = reInsertList->next;
		}
		// Check for redundant root (not leaf, 1 child) and eliminate
		if ((*root)->count == 1 && (*root)->level > 0) {
			tempNode = (*root)->branch[0].child;
			*root = tempNode;
		}
		return false;
	}
	return true;
}

// Delete a rectangle from non-root part of an index structure.
// Called by RemoveRect.  Descends tree recursively,
// merges branches on the way back up.
// Returns 1 if record not found, 0 if success.
static bool removeRectRec(rectT rect, Item item, nodeT *node, listNodeT **listNode) {
	if (node == NULL) {
		return true;
	}
	if (node->level > 0) { // not a leaf node
		for (int index = 0; index < node->count; index++) {
			if (overlap(rect, node->branch[index].rect)) {
				if (!removeRectRec(rect, item, node->branch[index].child, listNode)) {
					if (node->branch[index].child->count >= MIN_NODES) {
						// child removed, just resize parent rect
						node->branch[index].rect = nodeCover(node->branch[index].child);
					} else {
						// child removed, not enough entries in node, eliminate node
						reInsert(node->branch[index].child, listNode);
						disconnectBranch(node, index); // Must return after this call as count has changed
					}
					return false;
				}
			}
		}
		return true;
	}
	// A leaf node
	for (int index = 0; index < node->count; index++) {
		if (node->branch[index].item == item.ptr) {
			disconnectBranch(node, index); // Must return after this call as count has changed
			return false;
		}
	}
	return true;
}

// Decide whether two rectangles overlap.
static bool overlap(rectT rectA, rectT rectB) {
	for (int index = 0; index < 2; index++) {
		if (rectA.min[index] > rectB.max[index] ||
			rectB.min[index] > rectA.max[index]) {
			return false;
		}
	}
	return true;
}

// Add a node to the reinsertion list.  All its branches will later
// be reinserted into the index structure.
static void reInsert(nodeT *node, listNodeT **listNode) {
	listNodeT *nlistNode = malloc(sizeof(listNodeT));
	assert(nlistNode);
	memset(nlistNode, 0, sizeof(listNodeT));
	nlistNode->node = node;
	nlistNode->next = *listNode;
	*listNode = nlistNode;
}


// Disconnect a dependent node.
// Caller must return (or stop using iteration index) after this as count has changed
static void disconnectBranch(nodeT *node, int index) {
	// Remove element by swapping with the last element to prevent gaps in array
	node->branch[index] = node->branch[node->count-1];
	node->count--;
}




static double randd() { return ((rand()%RAND_MAX) / (double)RAND_MAX);}
static double randx() { return randd() * 360.0 - 180.0;}
static double randy() { return randd() * 180.0 - 90.0;}

int main(){
	printf("rtree implementation\n");
	for (;;){
		RTree *tr = rtreeNew();
		assert(tr);

		double minX1111,minY1111,maxX1111,maxY1111;

		clock_t start = clock();
		for (int i=0;i<100000;i++){
			double minX = randd()*10;
			double minY = randd()*10;
			double maxX = minX+(randd()*5);
			double maxY = minY+(randd()*5);
			if (i == 1111){
				minX1111 = minX;
				minY1111 = minY;
				maxX1111 = maxX;
				maxY1111 = maxY;
			}
			assert(rtreeInsert(tr, minX, minY, maxX, maxY, (void*)(long)i));
		}
		printf("%f\n", (double)(clock()-start)/(double)CLOCKS_PER_SEC);
		printf("%d\n", rtreeCount(tr));
		assert(rtreeRemove(tr, minX1111, minY1111, maxX1111, maxY1111, (void*)(long)1111));
		printf("%d\n", rtreeCount(tr));
		//rtreeInsert(tr, 5, 5, 10, 10, (void*)(long)1);

		rtreeFree(tr);
		break;
	}
	return 0;
}





