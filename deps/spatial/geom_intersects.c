#ifndef FROM_GEOM_C
#error This is not a standalone source file.
#endif

static int geomIntersectsBase(uint8_t *g, geom target, int *read);

static int pointIntersectsBase(uint8_t *ptr, ghdr h, int dims, geom target){
    //polyPoint p = {((double*)ptr)[0], ((double*)ptr)[1]};
    ghdr th = readhdr(((uint8_t*)target)+1);
    switch (th.type){
    default:
        return 0;
    case GEOM_POLYGON:
        printf("polygon!\n");
        return 0;
    }

    printf("point intersects?\n");

    return 0;
}

static int lineIntersectsBase(uint8_t *ptr, ghdr h, int dims, geom target){
    printf("line intersects?\n");
    return 0;
}

static int polygonIntersectsBase(uint8_t *ptr, ghdr h, int dims, geom target, int *read){
    uint8_t *optr = ptr;
    int count = (int)*((uint32_t*)ptr);
    ptr+=4;
    int intersects = 0;
    for (int i=0;i<count;i++){
        if (i==0){
            //polyPolygon p = polyPolygonFromGeomSegment(ptr, dims);
            // test if intersects here.
            // intersects = 0;
        }
        int count2 = (int)*((uint32_t*)ptr);
        ptr+=4+count2*(8*dims);
    }
    if (read){
        *read = optr-ptr;
    }
    return intersects;
}


static int linestringIntersectsBase(uint8_t *ptr, ghdr h, int dims, geom target, int *read){
    int count = (int)*((uint32_t*)ptr);
    ptr+=4;
    if (read){
        *read = 4+(count*(dims*8));
    }
    if (count==1){
        return pointIntersectsBase(ptr, h, dims, target);
    }
    for (int i=0;i<count-1;i++){
        if (lineIntersectsBase(ptr, h, dims, target)){
            return 1;
        }
        ptr+=dims*8;
    }
    return 0;
}

static int anyIntersectsBase(uint8_t *ptr, ghdr h, int dims, geom target, int *read){
    uint8_t *optr = ptr;
    switch (h.type){
    default:{
        break;
    }   
    case GEOM_GEOMETRYCOLLECTION:{
        int count = (int)*((uint32_t*)ptr);
        ptr+=4;
        for (int i=0;i<count;i++){
            int nread = 0;
            int res = geomIntersectsBase(ptr, target, &nread);
            ptr += nread;
            if (res){
                goto intersects;
            }
        }
        break;
    }
    case GEOM_MULTIPOLYGON:{
        int count = (int)*((uint32_t*)ptr);
        ptr+=4;
        for (int i=0;i<count;i++){
            int nread = 0;
            int res = polygonIntersectsBase(ptr, h, dims, target, &nread);
            ptr += nread;
            if (res){
                goto intersects;
            }
        }
        break;
    }
    case GEOM_MULTILINESTRING:{
        int count = (int)*((uint32_t*)ptr);
        ptr+=4;
        for (int i=0;i<count;i++){
            int nread = 0;
            int res = linestringIntersectsBase(ptr, h, dims, target, &nread);
            ptr += nread;
            if (res){
                goto intersects;
            }
        }
        break;
    }
    case GEOM_POLYGON:{
        int nread = 0;
        int res = polygonIntersectsBase(ptr, h, dims, target, &nread);
        ptr += nread;
        if (res){
            goto intersects;
        }
        break;
    }
    case GEOM_MULTIPOINT:{
        int count = (int)*((uint32_t*)ptr);
        ptr+=4;
        for (int i=0;i<count;i++){
            int res = pointIntersectsBase(ptr, h, dims, target);
            ptr+=dims*8;
            if (res){
                goto intersects;
            }
        }
        break;
    }
    case GEOM_LINESTRING:{
        int nread = 0;
        int res = linestringIntersectsBase(ptr, h, dims, target, &nread);
        ptr += nread;
        if (res){
            goto intersects;
        }
        break;
    }
    case GEOM_POINT:{
        int res = pointIntersectsBase(ptr, h, dims, target);
        ptr+=dims*8;
        if (res){
            goto intersects;
        }
        break;
    }}
    if (read){
        *read = ptr-optr;
    }
    return 0;
intersects:
    if (read){
        *read = ptr-optr;
    }
    return 1;
}


static int geomIntersectsBase(uint8_t *ptr, geom target, int *read){
    uint8_t *optr = ptr;
    ptr++; // skip the endian byte.
    ghdr h = readhdr(ptr);
    ptr+=4; // move past type.
    int dims = 2;
    if (h.z){
        dims++;
    }
    if (h.m){
        dims++;
    }
    int nread = 0;
    int res = anyIntersectsBase(ptr, h, dims, target, &nread);
    ptr+=nread;
    if (read){
        *read = ptr-optr;
    }
    return res;
}

int geomIntersects(geom g, geom target){
    return geomIntersectsBase((uint8_t*)g, target, NULL);
}

int geomIntersectsBounds(geom g, geomRect bounds){
    return 0;
}
