#ifndef FROM_GEOM_C
#error This is not a standalone source file.
#endif

typedef struct geomPolyMap{
	geom g;
	geom *geoms;
	geomType type;
	int z,m;
	int dims;
	int count;
	int collection;

	// polys
	polyPoint center;
	polyRect bounds;


} geomPolyMap;


void geomFreePolyMap(geomPolyMap *m){
	if (m){
		if (m->geoms && m->collection){
			geomFreeFlattenedArray(m->geoms);
		}
		free(m);
	}
}

geomPolyMap *geomNewPolyMap(geom g){
	if (!g){
		return NULL;
	}
	uint8_t *ptr = (uint8_t*)g;
	if (*ptr!=0&&*ptr!=1){
		return NULL;
	}
	ptr++;
	ghdr h = readhdr(ptr);
	ptr+=4;
	if (h.type == GEOM_UNKNOWN){
		return NULL;
	}
	int dims = 2;
	if (h.z){
		dims++;
	}
	if (h.m){
		dims++;
	}
	geomPolyMap *m = malloc(sizeof(geomPolyMap));
	if (!m){
		return NULL;
	}
	memset(m, 0, sizeof(geomPolyMap));
	m->g = g;
	geomCoord center = geomCenter(g);
	m->center.x = center.x;
	m->center.y = center.y;
	geomRect bounds = geomBounds(g);
	m->bounds.min.x = bounds.min.x;
	m->bounds.min.y = bounds.min.y;
	m->bounds.max.x = bounds.max.x;
	m->bounds.max.y = bounds.max.y;

	if (h.type==GEOM_GEOMETRYCOLLECTION){
		m->collection = 1;
		m->geoms = geomGeometryCollectionFlattenedArray(g, &m->count);
		if (!m->geoms){
			goto err;
		}
	} else {
		m->geoms = &g;
		m->count = 1;
	}
	m->type = h.type;
	m->z = h.z;
	m->m = h.m;
	m->dims = dims;
	


	return m;
err:
	if (m){
		geomFreePolyMap(m);
	}
	return NULL;
}