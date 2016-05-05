#ifndef FROM_GEOM_C
#error This is not a standalone source file.
#endif


void geomFreePolyMap(geomPolyMap *m){
	if (m){
		if (m->geoms && m->collection){
			geomFreeFlattenedArray(m->geoms);
		}
		if (m->polygons && m->multipoly){
			free(m->polygons);
		}
		if (m->holes && m->multipoly){
			free(m->holes);
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
		m->geoms = geomGeometryCollectionFlattenedArray(g, &m->geomCount);
		if (!m->geoms){
			goto err;
		}
	} else {
		m->geoms = &g;
		m->geomCount = 1;
	}
	m->type = h.type;
	m->z = h.z;
	m->m = h.m;
	m->dims = dims;

	switch (h.type){
	case GEOM_POINT:	
	case GEOM_MULTIPOINT:
	case GEOM_LINESTRING:
		switch (h.type){
		case GEOM_POINT:
			m->ppoly.len = 1;
			break;
		case GEOM_MULTIPOINT:
		case GEOM_LINESTRING:
			m->ppoly.len = *((uint32_t*)ptr);
			ptr+=4;
			break;
		}
		m->ppoly.dims = dims;
		m->ppoly.values = (double*)ptr;
		m->polygonCount = 1;
		m->polygons = &m->ppoly;
		m->holes = &m->pholes;
		break;
	case GEOM_POLYGON: 
	single_polygon:{
		int count = *((uint32_t*)ptr);
		ptr+=4;
		if (count>0){
			m->ppoly.len = *((uint32_t*)ptr);
			ptr+=4;
			m->ppoly.dims = dims;
			m->ppoly.values = (double*)ptr;
			ptr+=dims*8*m->ppoly.len;
			if (count>1){
				m->pholes.len = count-1;
				m->pholes.dims = dims;
				m->pholes.values = (double*)ptr;
			}
		} 
		m->polygonCount = 1;
		m->polygons = &m->ppoly;
		m->holes = &m->pholes;
		break;
	}
	case GEOM_MULTILINESTRING:{
		int count = *((uint32_t*)ptr);
		ptr+=4;
		m->multipoly = 1;
		m->polygonCount = count;
		m->polygons = malloc(count*sizeof(polyPolygon));
		if (!m->polygons){
			goto err;
		}
		memset(m->polygons, 0, count*sizeof(polyPolygon));
		m->holes = malloc(count*sizeof(polyMultiPolygon));
		if (!m->holes){
			goto err;
		}
		memset(m->holes, 0, count*sizeof(polyMultiPolygon));
		for (int i=0;i<count;i++){
			m->polygons[i].len = *((uint32_t*)ptr);
			ptr+=4;
			m->polygons[i].dims = dims;
			m->polygons[i].values = (double*)ptr;
			ptr+=dims*8*m->polygons[i].len;
		}
		break;
	}
	case GEOM_MULTIPOLYGON:{
		int count = *((uint32_t*)ptr);
		if (count<=1){
			goto single_polygon;
		}
		ptr+=4;
		m->multipoly = 1;
		m->polygonCount = count;
		m->polygons = malloc(count*sizeof(polyPolygon));
		if (!m->polygons){
			goto err;
		}
		memset(m->polygons, 0, count*sizeof(polyPolygon));
		m->holes = malloc(count*sizeof(polyMultiPolygon));
		if (!m->holes){
			goto err;
		}
		memset(m->holes, 0, count*sizeof(polyMultiPolygon));
		for (int i=0;i<count;i++){
			int count2 = *((uint32_t*)ptr);
			ptr+=4;
			if (count2==0){
				continue;
			}
			m->polygons[i].len = *((uint32_t*)ptr);
			ptr+=4;
			m->polygons[i].dims = dims;
			m->polygons[i].values = (double*)ptr;
			ptr+=dims*8*m->polygons[i].len;	
			if (count2>1){
				m->holes[i].len = count2-1;
				m->holes[i].dims = dims;
				m->holes[i].values = (double*)ptr;
				for (int j=1;j<count2;j++){
					int count3 = *((uint32_t*)ptr);
					ptr+=4;
					ptr+=dims*8*count3;
				}
			}
		}
		break;
	}
	case GEOM_GEOMETRYCOLLECTION:{
		int cap = 1;
		int len = 0;
		polyPolygon *polygons = NULL;
		polyMultiPolygon *holes = NULL;
		geomPolyMap *m2 = NULL;
		polygons = malloc(cap*sizeof(polyPolygon));
		if (!polygons){
			goto err2;
		}
		holes = malloc(cap*sizeof(polyMultiPolygon));
		if (!holes){
			goto err2;
		}
		for (int i=0;i<m->geomCount;i++){
			geom g2 = m->geoms[i];
			m2 = geomNewPolyMap(g2);
			if (!m2){
				goto err2;
			}
			for (int j=0;j<m2->polygonCount;j++){
				if (len==cap){
					int ncap = cap==0?1:cap*2;
					polyPolygon *npolygons = realloc(polygons, ncap*sizeof(polyPolygon));
					if (!npolygons){
						goto err2;
					}
					polyMultiPolygon *nholes = realloc(holes, ncap*sizeof(polyMultiPolygon));
					if (!nholes){
						goto err2;
					}
					polygons = npolygons;
					holes = nholes;
					cap = ncap;
				}
				polygons[len] = m2->polygons[j];
				holes[len] = m2->holes[j];
				len++;
			}
			geomFreePolyMap(m2);	
			m2 = NULL;
		}
		m->multipoly = 1;
		m->polygonCount = len;
		m->polygons = polygons;
		m->holes = holes;
		break;
	err2:
		if (polygons){
			free(polygons);
		}
		if (holes){
			free(holes);
		}
		if (m2){
			geomFreePolyMap(m2);
		}
		goto err;
	}}
	return m;
err:
	if (m){
		geomFreePolyMap(m);
	}
	return NULL;
}