#ifndef FROM_GEOM_C
#error This is not a standalone source file.
#endif

int geomIntersects(geom g, geom target){
	return 0;
}
int geomIntersectsRadius(geom g, geomCoord center, double meters){
	geomRect bounds = geomBounds(g);
	if (bounds.min.x == bounds.max.x && bounds.min.y == bounds.max.y){
		return geoutilDistance(bounds.min.y, bounds.min.x, center.y, center.x) <= meters ? 1 : 0;
	}


	printf("intersects 2\n");
	return 0;
}
int geomIntersectsBounds(geom g, geomRect bounds){
	return 0;
}
