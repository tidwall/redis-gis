#ifndef __SPATIAL_H__
#define __SPATIAL_H__

#include "server.h"

typedef struct spatial spatial;

spatial *spatialNew();
void spatialFree(spatial *s);

/* robjSpatialGetHash retreives the pointer to the hash robj. */
void *robjSpatialGetHash(void *o);

/* robjSpatialNewHash creates a spatial robj from a base hash. */
void *robjSpatialNewHash(void *o);

#endif
