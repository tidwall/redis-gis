#ifndef FROM_GEOM_C
#error This is not a standalone source file.
#endif

int geomWithin(geom g, geom target){
	return 0;
}
int geomWithinRadius(geom g, geomCoord center, double meters, geom circlePolygon){
	geomRect bounds = geomBounds(g);
	if (bounds.min.x == bounds.max.x && bounds.min.y == bounds.max.y){
		return geoutilDistance(bounds.min.y, bounds.min.x, center.y, center.x) <= meters ? 1 : 0;
	}
	printf("within 2\n");
	return 0;
}
int geomWithinBounds(geom g, geomRect bounds){
	return 0;
}


