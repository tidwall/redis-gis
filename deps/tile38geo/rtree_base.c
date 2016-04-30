/* Package rtree - An r-tree implementation.
 *
 * This file is derived from the work done by Toni Gutman. R-Trees: A Dynamic Index Structure for
 * Spatial Searching, Proc. 1984 ACM SIGMOD International Conference on Management of Data, pp.
 * 47-57. 
 *
 * The original C code can be found at "http://www.superliminal.com/sources/sources.htm".
 *
 * And the website carries this message: "Here are a few useful bits of free source code. You're
 * completely free to use them for any purpose whatsoever. All I ask is that if you find one to
 * be particularly valuable, then consider sending feedback. Please send bugs and suggestions too.
 * Enjoy"
 *
 * -------
 *
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

/* Template options */
#ifndef NUMBER
#   define NUMBER double
#endif
#ifndef NUM_DIMS
#   define NUM_DIMS 2
#endif
#ifndef USE_SPHERICAL_VOLUME
#   define USE_SPHERICAL_VOLUME 1
#endif
#ifndef MAX_NODES
#   define MAX_NODES            16
#endif

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#define MIN_NODES (MAX_NODES/2)

#if NUM_DIMS == 2
#   define UNIT_SPHERE_VOLUME   3.141593
#elif NUM_DIMS == 3
#   define UNIT_SPHERE_VOLUME   4.188790
#elif NUM_DIMS == 4
#   define UNIT_SPHERE_VOLUME   4.934802
#else
#   error invalid NUM_DIMS only 2,3,4 allowed
#endif

typedef struct branchT branchT;
typedef struct nodeT nodeT;
typedef struct rectT rectT;
typedef struct listNodeT listNodeT;
typedef struct partitionVarsT partitionVarsT;
typedef struct stackT stackT;
typedef struct iteratorT iteratorT;

/// Minimal bounding rectangle (n-dimensional)
struct rectT {
    NUMBER min[NUM_DIMS]; ///< Min dimensions of bounding box
    NUMBER max[NUM_DIMS]; ///< Max dimensions of bounding box
};

/// May be data or may be another subtree
/// The parents level determines this.
/// If the parents level is 0, then this is data
struct branchT {
    rectT rect;    ///< Bounds
    void  *item;   ///< Data ID or Ptr
    nodeT *child;  ///< Child node
};

/// nodeT for each branch level
struct nodeT {
    int     count;               ///< Count
    int     level;               ///< Leaf is zero, others positive
    branchT branch[MAX_NODES];   ///< branchT
};

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
    int     taken[MAX_NODES+1];
    int     count[2];
    rectT   cover[2];
    NUMBER  area[2];
    branchT branchBuf[MAX_NODES+1];
    int     branchCount;
    rectT   coverSplit;
    NUMBER  coverSplitArea;
};

struct stackT {
    nodeT *node;
    int index;
};

struct iteratorT {
    int len;
    stackT stack[64];
    rectT rect;

    rectT resRect;
    void *resItem;
};

static int addBranch(branchT *branch, nodeT *node, nodeT **newNode);
static int pickBranch(rectT rect, nodeT *node);

#if NUM_DIMS == 2
static inline rectT makeRect(NUMBER minX, NUMBER minY, NUMBER maxX, NUMBER maxY) {
    rectT rect;
    rect.min[0] = minX;
    rect.min[1] = minY;
    rect.max[0] = maxX;
    rect.max[1] = maxY;
    return rect;
}
static inline void getRect(rectT rect, NUMBER *minX, NUMBER *minY, NUMBER *maxX, NUMBER *maxY) {
    *minX = rect.min[0];
    *minY = rect.min[1];
    *maxX = rect.max[0];
    *maxY = rect.max[1];
}
#elif NUM_DIMS == 3
static inline rectT makeRect(NUMBER minX, NUMBER minY, NUMBER minZ, NUMBER maxX, NUMBER maxY, NUMBER maxZ) {
    rectT rect;
    rect.min[0] = minX;
    rect.min[1] = minY;
    rect.min[2] = minZ;
    rect.max[0] = maxX;
    rect.max[1] = maxY;
    rect.max[2] = maxZ;
    return rect;
}
static inline void getRect(rectT rect, NUMBER *minX, NUMBER *minY, NUMBER *minZ, NUMBER *maxX, NUMBER *maxY, NUMBER *maxZ) {
    *minX = rect.min[0];
    *minY = rect.min[1];
    *minZ = rect.min[2];
    *maxX = rect.max[0];
    *maxY = rect.max[1];
    *maxZ = rect.max[2];
}
#elif NUM_DIMS == 4
static inline rectT makeRect(NUMBER minX, NUMBER minY, NUMBER minZ, NUMBER minM, NUMBER maxX, NUMBER maxY, NUMBER maxZ, NUMBER maxM) {
    rectT rect;
    rect.min[0] = minX;
    rect.min[1] = minY;
    rect.min[2] = minZ;
    rect.min[3] = minM;
    rect.max[0] = maxX;
    rect.max[1] = maxY;
    rect.max[2] = maxZ;
    rect.max[3] = maxM;
    return rect;
}
static inline void getRect(rectT rect, NUMBER *minX, NUMBER *minY, NUMBER *minZ, NUMBER *minM, NUMBER *maxX, NUMBER *maxY, NUMBER *maxZ, NUMBER *maxM) {
    *minX = rect.min[0];
    *minY = rect.min[1];
    *minZ = rect.min[2];
    *minM = rect.min[3];
    *maxX = rect.max[0];
    *maxY = rect.max[1];
    *maxZ = rect.max[2];
    *maxM = rect.max[3];
}
#endif

static inline NUMBER min(NUMBER a, NUMBER b) {
    if (a < b) {
        return a;
    }
    return b;
}

static inline NUMBER max(NUMBER a, NUMBER b) {
    if (a > b) {
        return a;
    }
    return b;
}

// Decide whether two rectangles overlap.
static inline int overlap(rectT rectA, rectT rectB) {
    for (int index = 0; index < NUM_DIMS; index++) {
        if (rectA.min[index] > rectB.max[index] ||
            rectB.min[index] > rectA.max[index]) {
            return 0;
        }
    }
    return 1;
}

// Add a node to the reinsertion list.  All its branches will later
// be reinserted into the index structure.
static void reInsert(nodeT *node, listNodeT **listNode) {
    listNodeT *nlistNode = malloc(sizeof(listNodeT));
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
    int firstTime = 1;
    rectT rect;
    memset(&rect, 0, sizeof(rectT));
    for (int index = 0; index < node->count; index++) {
        if (firstTime) {
            rect = node->branch[index].rect;
            firstTime = 0;
        } else {
            rect = combineRect(rect, node->branch[index].rect);
        }
    }
    return rect;
}

// Calculate the n-dimensional volume of a rectangle
static NUMBER rectVolume(rectT rect) {
    NUMBER volume = 1;
    for (int index = 0; index < 2; index++) {
        volume *= rect.max[index] - rect.min[index];
    }
    return volume;
}

// The exact volume of the bounding sphere for the given rectT
static NUMBER rectSphericalVolume(rectT rect) {
    NUMBER sumOfSquares = 0;
    NUMBER radius = 0;
    for (int index = 0; index < 2; index++) {
        NUMBER halfExtent = (rect.max[index] - rect.min[index]) * 0.5;
        sumOfSquares += halfExtent * halfExtent;
    }
    radius = sqrt(sumOfSquares);
    if (NUM_DIMS==2){
        return radius*radius*UNIT_SPHERE_VOLUME;
    } else if (NUM_DIMS==3){
        return radius*radius*radius*UNIT_SPHERE_VOLUME;
    } else if (NUM_DIMS==4){
        return radius*radius*radius*radius*UNIT_SPHERE_VOLUME;
    } else{
        return pow(radius, NUM_DIMS) * UNIT_SPHERE_VOLUME;
    }
}

// Use one of the methods to calculate retangle volume
static NUMBER calcRectVolume(rectT rect) {
    if (USE_SPHERICAL_VOLUME) {
        return rectSphericalVolume(rect); // Slower but helps certain merge cases
    }
    return rectVolume(rect); // Faster but can cause poor merges
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

// Initialize a partitionVarsT structure.
static void initParVars(partitionVarsT *parVars, int maxRects, int minFill) {
    parVars->count[1] = 0;
    parVars->count[0] = parVars->count[1];
    parVars->area[1] = 0;
    parVars->area[0] = parVars->area[1];
    parVars->total = maxRects;
    parVars->minFill = minFill;
    for (int index = 0; index < maxRects; index++) {
        parVars->taken[index] = 0;
        parVars->partition[index] = -1;
    }
}

// Put a branch in one of the groups.
static void classify(int index, int group, partitionVarsT *parVars) {
    parVars->partition[index] = group;
    parVars->taken[index] = 1;

    if (parVars->count[group] == 0) {
        parVars->cover[group] = parVars->branchBuf[index].rect;
    } else {
        parVars->cover[group] = combineRect(parVars->branchBuf[index].rect, parVars->cover[group]);
    }
    parVars->area[group] = calcRectVolume(parVars->cover[group]);
    parVars->count[group]++;
}

static void pickSeeds(partitionVarsT *parVars) {
    int seed0 = 0;
    int seed1 = 0;
    NUMBER worst = 0;
    NUMBER waste = 0;
    NUMBER area[MAX_NODES+1];
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
    NUMBER biggestDiff = 0;
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
                NUMBER growth0 = calcRectVolume(rect0) - parVars->area[0];
                NUMBER growth1 = calcRectVolume(rect1) - parVars->area[1];
                NUMBER diff = growth1 - growth0;
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
    memset(*newNode, 0, sizeof(nodeT));
    node->level = level;
    (*newNode)->level = node->level;
    loadNodes(node, *newNode, parVars);
}

// Add a branch to a node.  Split the node if necessary.
// Returns 0 if node not split.  Old node updated.
// Returns 1 if node split, sets *new_node to address of new node.
// Old node updated, becomes one of two.
static int addBranch(branchT *branch, nodeT *node, nodeT **newNode) {
    if (node->count < MAX_NODES) { // Split won't be necessary
        node->branch[node->count] = *branch;
        node->count++;
        return 0;
    }
    splitNode(node, branch, newNode);
    return 1;
}

static void freeNode(nodeT *node){
    if (!node){
        return;
    }
    for (int i=0;i<node->count;i++){
        if (node->branch[i].child){
            freeNode(node->branch[i].child);
        }
    }
    free(node);
}

// Inserts a new data rectangle into the index structure.
// Recursively descends tree, propagates splits back up.
// Returns 0 if node was not split.  Old node updated.
// If node was split, returns 1 and sets the pointer pointed to by
// new_node to point to the new node.  Old node updated to become one of two.
// The level argument specifies the number of steps up from the leaf
// level to insert; e.g. a data rectangle goes in at level = 0.
static int insertRectRec(rectT rect, void *item, nodeT *node, nodeT **newNode, int level) {
    int index = 0;
    branchT branch;
    memset(&branch, 0, sizeof(branchT));
    nodeT *otherNode = NULL;
    // Still above level for insertion, go down tree recursively
    if (node == NULL) {
        return 0;
    }
    if (node->level > level) {
        index = pickBranch(rect, node);
        if (!insertRectRec(rect, item, node->branch[index].child, &otherNode, level)) {
            // Child was not split
            node->branch[index].rect = combineRect(rect, node->branch[index].rect);
            return 0;
        } // Child was split
        node->branch[index].rect = nodeCover(node->branch[index].child);
        branch.child = otherNode;
        // if branch.child == nil {
        //  println(">> child assigned is nil")
        // }
        branch.rect = nodeCover(otherNode);
        return addBranch(&branch, node, newNode);
    } else if (node->level == level) { // Have reached level for insertion. Add rect, split if necessary
        branch.rect = rect;
        branch.item = item;
        // Child field of leaves contains id of data record
        return addBranch(&branch, node, newNode);
    }
    // Should never occur
    return 0;
}

// Insert a data rectangle into an index structure.
// InsertRect provides for splitting the root;
// returns 1 if root was split, 0 if it was not.
// The level argument specifies the number of steps up from the leaf
// level to insert; e.g. a data rectangle goes in at level = 0.
// InsertRect2 does the recursion.
static int insertRect(rectT rect, void *item, nodeT **root, int level) {
    nodeT *newRoot = NULL;
    nodeT *newNode = NULL;
    branchT branch;
    memset(&branch, 0, sizeof(branchT));
    
    if (insertRectRec(rect, item, *root, &newNode, level)) { // Root split
        newRoot = malloc(sizeof(nodeT)); // Grow tree taller and new root
        memset(newRoot, 0, sizeof(nodeT));
        newRoot->level = (*root)->level + 1;
        branch.rect = nodeCover(*root);
        branch.child = *root;
        addBranch(&branch, newRoot, NULL);
        branch.rect = nodeCover(newNode);
        branch.child = newNode;
        addBranch(&branch, newRoot, NULL);
        *root = newRoot;
        return 1;
    }
    return 0;
}

// Pick a branch.  Pick the one that will need the smallest increase
// in area to accommodate the new rectangle.  This will result in the
// least total area for the covering rectangles in the current node.
// In case of a tie, pick the one which was smaller before, to get
// the best resolution when searching.
static int pickBranch(rectT rect, nodeT *node) {
    int firstTime = 1;
    NUMBER increase = 0;
    NUMBER bestIncr = -1;
    NUMBER area = 0;
    NUMBER bestArea = 0;
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
            firstTime = 0;
        } else if ((increase == bestIncr) && (area < bestArea)) {
            best = index;
            bestArea = area;
            bestIncr = increase;
        }
    }
    return best;
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

// Delete a rectangle from non-root part of an index structure.
// Called by RemoveRect.  Descends tree recursively,
// merges branches on the way back up.
// Returns 1 if record not found, 0 if success.
static int removeRectRec(rectT rect, void *item, nodeT *node, listNodeT **listNode) {
    if (node == NULL) {
        return 1;
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
                    return 0;
                }
            }
        }
        return 1;
    }
    // A leaf node
    for (int index = 0; index < node->count; index++) {
        if (node->branch[index].item == item) {
            disconnectBranch(node, index); // Must return after this call as count has changed
            return 0;
        }
    }
    return 1;
}

// Delete a data rectangle from an index structure.
// Pass in a pointer to a rectT, the tid of the record, ptr to ptr to root node.
// Returns 1 if record not found, 0 if success.
// RemoveRect provides for eliminating the root.
static int removeRect(rectT rect, void *item, nodeT **root) {
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
            listNodeT *prev = reInsertList;
            reInsertList = reInsertList->next;
            freeNode(prev->node);
            free(prev);
        }
        // Check for redundant root (not leaf, 1 child) and eliminate
        if ((*root)->count == 1 && (*root)->level > 0) {
            tempNode = (*root)->branch[0].child;
            *root = tempNode;
        }
        return 0;
    }
    return 1;
}

// Search for all items which overlap a specified rectangle.
// The iterator function is called for each item that overlaps the rectangle.
// The search can be cancelled mid way through by returning Zero from the iterator function.
// The final return value is the total count of items found.
static int search(nodeT *node, rectT rect, int(*iterator)(rectT rect, void *item, void *userdata), void *userdata){
    int counter = 0;
    if (node) {
        if (node->level > 0) { // This is an internal node in the tree
            for (int index = 0; index < node->count; index++) {
                if (overlap(rect, node->branch[index].rect)) {
                    counter += search(node->branch[index].child, rect, iterator, userdata);
                }
            }
        } else { // This is a leaf node
            for (int index = 0; index < node->count; index++) {
                if (overlap(rect, node->branch[index].rect)) {
                    if (iterator){
                        if (!iterator(node->branch[index].rect, node->branch[index].item, userdata)){
                            return counter;
                        }
                    }
                    counter++;
                }
            }
        }
    }
    return counter;
}


