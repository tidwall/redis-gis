/*
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
