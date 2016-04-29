#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <assert.h>
#include <time.h>

#include "rtree.h"

static double randd() { return ((rand()%RAND_MAX) / (double)RAND_MAX);}
static double randx() { return randd() * 360.0 - 180.0;}
static double randy() { return randd() * 180.0 - 90.0;}

int main(){
	srand(time(NULL)/clock());
	printf("rtree implementation\n");
	for(int jj=0;jj<100;jj++){
		rtree *tr = rtreeNew();
		assert(tr);
		int n = 10000;
		clock_t start = clock();
		for (int i=0;i<n;i++){
			double minX = randx();
			double minY = randy();
			double maxX = minX+(randd()*10+0.0001);
			double maxY = minY+(randd()*10+0.0001);
			assert(rtreeInsert(tr, minX, minY, maxX, maxY, (void*)(long)i));
		}
		// double elapsed = (double)(clock()-start)/(double)CLOCKS_PER_SEC;
		// printf("inserted %d items in %.2f secs, %.0f ops/s\n", n, elapsed, (double)n/elapsed);
		assert(rtreeCount(tr)==n);

		for (int i=0;i<n;i++){
			assert(rtreeRemove(tr, -180, -90, 180+11, 90+11, (void*)(long)i));
		}


		rtreeFree(tr);
	}
	return 0;

	for (;;){
		rtree *tr = rtreeNew();
		assert(tr);

		int n = 1000000;
		clock_t start = clock();
		for (int i=0;i<n;i++){
			double minX = randx();
			double minY = randy();
			double maxX = minX+(randd()*10+0.0001);
			double maxY = minY+(randd()*10+0.0001);
			assert(rtreeInsert(tr, minX, minY, maxX, maxY, (void*)(long)i));
		}
		double elapsed = (double)(clock()-start)/(double)CLOCKS_PER_SEC;
		printf("inserted %d items in %.2f secs, %.0f ops/s\n", n, elapsed, (double)n/elapsed);
		assert(rtreeCount(tr)==n);


		double rt = 0;
		n = 100000;
		start = clock();
		for (int i=0;i<n;i++){
			double minX = randx();
			double minY = randy();
			double maxX = minX+(randd()*10+0.0001);
			double maxY = minY+(randd()*10+0.0001);
			rt += (double)rtreeSearch(tr, 0, 0, 1, 1);
		}
		elapsed = ((double)(clock()-start) / (double)CLOCKS_PER_SEC);
		printf("searched %d queries in %.2f secs, %.0f ops/s (~%.0f items)\n", n, elapsed, (double)n/elapsed, rt/(double)n);

		rtreeFree(tr);
		break;
	}
	return 0;
}
