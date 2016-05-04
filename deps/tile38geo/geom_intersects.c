#ifndef FROM_GEOM_C
#error This is not a standalone source file.
#endif

int geomIntersects(geom g, geom target){
	printf("intersects 2\n");
	return 0;
}
int geomIntersectsRadius(geom g, geomCoord center, double meters, geom circlePolygon){
	geomRect bounds = geomBounds(g);
	if (bounds.min.x == bounds.max.x && bounds.min.y == bounds.max.y){
		return geoutilDistance(bounds.min.y, bounds.min.x, center.y, center.x) <= meters ? 1 : 0;
	}
	int freecp = 0;
	if (circlePolygon == NULL){
		circlePolygon = geomNewCirclePolygon(center, meters, 12);
		if (!circlePolygon){
			return 0;
		}
		freecp = 1;
	}
	int res = geomIntersects(g, circlePolygon);
	if (freecp){
		geomFree(circlePolygon);
	}
	return res;
}
int geomIntersectsBounds(geom g, geomRect bounds){
	return 0;
}
