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

int iterator(double minX, double minY, double maxX, double maxY, void *item, void *userdata){
	return 1;
}

int main(){
	srand(time(NULL)/clock());
	printf("rtree implementation\n");
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



		double rtA = 0;
		double rtB = 0;
		double rtC = 0;
		n = 100000;
		start = clock();
		for (int i=0;i<n;i++){
			double minX = randx();
			double minY = randy();
			double maxX = minX+(randd()*10+0.0001);
			double maxY = minY+(randd()*10+0.0001);
			rtC += rtreeSearch(tr, 0, 0, 1, 1, iterator, NULL);//iterator, void *userdata);
		}
		elapsed = ((double)(clock()-start) / (double)CLOCKS_PER_SEC);
		printf("searched %d queries in %.2f secs, %.0f ops/s (%.0f/%.0f/%.0f items)\n", n, elapsed, (double)n/elapsed, rtA/(double)n, rtB/(double)n, rtC/(double)n);

		rtreeFree(tr);
		break;
	}
	return 0;
}
