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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "wkt.h"

typedef struct {
    wktErr err;
    char *p;
    double x, y;
} d2PairResult;

typedef struct {
    wktErr err;
    char *p;
} stdResult;

static stdResult wktParseGeometry(char *p, wktGeometry **wkt);

static inline char *ignorews(char *p){
    for (;;) {
        switch (*p) {
        default:
            return p;
        case '\t': case ' ': case '\r': case '\v': case '\n': case '\f':
            p++;
            continue;
        }
    }
}

static inline d2PairResult wktParse2DPair(char *p){
    d2PairResult pair = {0};
    char *ptr = NULL;
    pair.x = strtod(p, &ptr);
    if (ptr-p == 0){
        pair.err = WKT_ERRINPUT;
        return pair;
    }
    p += ptr-p;
    p = ignorews(p);
    pair.y = strtod(p, &ptr);
    if (ptr-p == 0){
        pair.err = WKT_ERRINPUT;
        return pair;
    }
    p += ptr-p;
    pair.p = p;
    return pair;
}

static inline wktGeometry *wktMake(wktType type) {
    wktGeometry *wkt = malloc(sizeof(wktGeometry));
    if (!wkt){
        return NULL;
    }
    memset(wkt, 0, sizeof(wktGeometry));
    wkt->type = type;
    return wkt;
}

static stdResult wktParsePoint(char *p, wktGeometry **wkt){
    wktErr err = WKT_ERRNONE;
    stdResult res = {WKT_ERRNONE, p};
    p = ignorews(p);
    if (*p!='('){
        err = WKT_ERRINPUT;
        goto err;
    }
    p++;
    p = ignorews(p);
    d2PairResult pair = wktParse2DPair(p);
    if (pair.err){
        err = pair.err;
        goto err;
    }
    p = pair.p;
    p = ignorews(p);
    if (*p!=')'){
        err = WKT_ERRINPUT;
        goto err;
    }
    p++;
    if (wkt){
        wktGeometry *nwkt = wktMake(WKT_POINT);
        if (!nwkt){
            err = WKT_ERRMEM;
            goto err;
        }
        nwkt->point.x = pair.x;
        nwkt->point.y = pair.y;
        *wkt = nwkt;
    }
    res.err = err;
    res.p = p;
    return res;
err:
    res.err = err;
    res.p = p;
    return res;
}

static stdResult wktParseLineStringOrMultiPoint(wktType type, char *p, wktGeometry **wkt){
    wktErr err = WKT_ERRNONE;
    stdResult res = {WKT_ERRNONE, p};

    wktPoint *points = NULL;
    int cap = 0;
    int size = 0;

    p = ignorews(p);
    if (*p!='('){
        err = WKT_ERRINPUT;
        goto err;
    }
    p++;
    p = ignorews(p);
    if (*p == ')'){
        p++; 
    } else {
        for(;;){
            d2PairResult pair = wktParse2DPair(p);
            if (pair.err){
                err = pair.err;
                goto err;
            }
            p = pair.p;
            if (wkt){
                if (size == cap){
                    if (cap == 0){
                        cap = 1;
                    } else{
                        cap *= 2;
                    }
                    wktPoint *npoints = realloc(points, sizeof(wktPoint)*cap);
                    if (!npoints){
                        err = WKT_ERRMEM;
                        goto err;   
                    }
                    points = npoints;
                }
                points[size].x = pair.x;
                points[size].y = pair.y;
                size++;
            }
            p = ignorews(p);
            if (*p == ','){
                p++;
                p = ignorews(p);
                continue;
            } else if (*p == ')'){
                p++;
                break;
            } else {
                err = WKT_ERRINPUT;
                goto err;
            }
        }
    }
    if (wkt){
        wktGeometry *nwkt = wktMake(type);
        if (!nwkt){
            err = WKT_ERRMEM;
            goto err;
        }
        if (type == WKT_LINESTRING){
            nwkt->lineString.points = points;
            nwkt->lineString.size = size;
        } else{
            nwkt->multiPoint.points = points;
            nwkt->multiPoint.size = size;
        }
        *wkt = nwkt;
    }
    res.err = err;
    res.p = p;
    return res;
err:
    if (points){
        free(points);
    }
    res.err = err;
    res.p = p;
    return res;
}

static stdResult wktParseMultiLineStringOrPolygon(wktType type, char *p, wktGeometry **wkt){
    wktErr err = WKT_ERRNONE;
    stdResult res = {WKT_ERRNONE, p};
    wktLineString *lineStrings = NULL;
    wktGeometry *nwkt = NULL;
    int cap = 0;
    int size = 0;

    p = ignorews(p);
    if (*p!='('){
        err = WKT_ERRINPUT;
        goto err;
    }
    p++;
    p = ignorews(p);
    if (*p == ')'){
        p++;
    } else {
        for (;;){
            if (wkt){
                stdResult res = wktParseLineStringOrMultiPoint(WKT_LINESTRING, p, &nwkt);
                p = res.p;
                err = res.err;
            } else {
                stdResult res = wktParseLineStringOrMultiPoint(WKT_LINESTRING, p, NULL);
                p = res.p;
                err = res.err;
            }
            if (err){
                goto err;
            }
            if (wkt){
                if (size == cap){
                    if (cap == 0){
                        cap = 1;
                    } else{
                        cap *= 2;
                    }
                    wktLineString *nlineStrings = realloc(lineStrings, sizeof(wktLineString)*cap);
                    if (!nlineStrings){
                        wktFree(nwkt);
                        nwkt = NULL;
                        err = WKT_ERRMEM;
                        goto err;   
                    }
                    lineStrings = nlineStrings;
                }
                lineStrings[size].points = nwkt->lineString.points;
                lineStrings[size].size = nwkt->lineString.size;
                size++;
                free(nwkt); // only free the container, we are borrowing the contents.
                nwkt = NULL;
            }
            p = ignorews(p);
            if (*p == ','){
                p++;
                continue;
            } else if (*p == ')'){
                p++;
                break;
            } else {
                err = WKT_ERRINPUT;
                goto err;
            }
        }
    }
    if (wkt){
        wktGeometry *nwkt = wktMake(type);
        if (!nwkt){
            err = WKT_ERRMEM;
            goto err;
        }
        if (type == WKT_MULTILINESTRING){
            nwkt->multiLineString.lineStrings = lineStrings;
            nwkt->multiLineString.size = size;
        } else{
            nwkt->polygon.lineStrings = lineStrings;
            nwkt->polygon.size = size;
        }
        *wkt = nwkt;
    }
    res.err = err;
    res.p = p;
    return res;
err:
    if (lineStrings){
        free(lineStrings);
    }
    res.err = err;
    res.p = p;
    return res;
}

static stdResult wktParseMultiPolygon(char *p, wktGeometry **wkt){
    wktErr err = WKT_ERRNONE;
    stdResult res = {WKT_ERRNONE, p};
    wktPolygon *polygons = NULL;
    wktGeometry *nwkt = NULL;
    int cap = 0;
    int size = 0;

    p = ignorews(p);
    if (*p!='('){
        err = WKT_ERRINPUT;
        goto err;
    }
    p++;
    p = ignorews(p);
    if (*p == ')'){
        p++;
    } else {
        for (;;){
            if (wkt){
                stdResult res = wktParseMultiLineStringOrPolygon(WKT_POLYGON, p, &nwkt);
                p = res.p;
                err = res.err;
            } else {
                stdResult res = wktParseMultiLineStringOrPolygon(WKT_POLYGON, p, NULL);
                p = res.p;
                err = res.err;
            }
            if (err){
                goto err;
            }
            if (wkt){
                if (size == cap){
                    if (cap == 0){
                        cap = 1;
                    } else{
                        cap *= 2;
                    }
                    wktPolygon *npolygons = realloc(polygons, sizeof(wktPolygon)*cap);
                    if (!npolygons){
                        wktFree(nwkt);
                        nwkt = NULL;
                        err = WKT_ERRMEM;
                        goto err;   
                    }
                    polygons = npolygons;
                }
                polygons[size].lineStrings = nwkt->polygon.lineStrings;
                polygons[size].size = nwkt->polygon.size;
                size++;
                free(nwkt); // only free the container, we are borrowing the contents.
                nwkt = NULL;
            }
            p = ignorews(p);
            if (*p == ','){
                p++;
                continue;
            } else if (*p == ')'){
                p++;
                break;
            } else {
                err = WKT_ERRINPUT;
                goto err;
            }
        }
    }
    if (wkt){
        wktGeometry *nwkt = wktMake(WKT_MULTIPOLYGON);
        if (!nwkt){
            err = WKT_ERRMEM;
            goto err;
        }
        nwkt->multiPolygon.polygons = polygons;
        nwkt->multiPolygon.size = size;
        *wkt = nwkt;
    }
    res.err = err;
    res.p = p;
    return res;
err:
    if (polygons){
        free(polygons);
    }
    res.err = err;
    res.p = p;
    return res;
}

static stdResult wktParseGeometryCollection(char *p, wktGeometry **wkt){
    wktErr err = WKT_ERRNONE;
    stdResult res = {WKT_ERRNONE, p};
    wktGeometry **geometries = NULL;
    wktGeometry *nwkt = NULL;
    int cap = 0;
    int size = 0;

    p = ignorews(p);
    if (*p!='('){
        err = WKT_ERRINPUT;
        goto err;
    }
    p++;
    p = ignorews(p);
    if (*p == ')'){
        p++;
    } else {
        for (;;){
            if (wkt){
                stdResult res = wktParseGeometry(p, &nwkt);
                p = res.p;
                err = res.err;
            } else {
                stdResult res = wktParseGeometry(p, NULL);
                p = res.p;
                err = res.err;
            }
            if (err){
                goto err;
            }
            if (wkt){
                if (size == cap){
                    if (cap == 0){
                        cap = 1;
                    } else{
                        cap *= 2;
                    }
                    wktGeometry **ngeometries = realloc(geometries, sizeof(wktGeometry*)*cap);
                    if (!ngeometries){
                        wktFree(nwkt);
                        nwkt = NULL;
                        err = WKT_ERRMEM;
                        goto err;   
                    }
                    geometries = ngeometries;
                }
                geometries[size] = nwkt;
                size++;
                nwkt = NULL;
            }
            p = ignorews(p);
            if (*p == ','){
                p++;
                continue;
            } else if (*p == ')'){
                p++;
                break;
            } else {
                err = WKT_ERRINPUT;
                goto err;
            }
        }
    }
    if (wkt){
        wktGeometry *nwkt = wktMake(WKT_GEOMETRYCOLLECTION);
        if (!nwkt){
            err = WKT_ERRMEM;
            goto err;
        }
        nwkt->geometryCollection.geometries = geometries;
        nwkt->geometryCollection.size = size;
        *wkt = nwkt;
    }
    res.err = err;
    res.p = p;
    return res;
err:
    if (geometries){
        free(geometries);
    }
    res.err = err;
    res.p = p;
    return res;
}

static stdResult wktParseGeometry(char *p, wktGeometry **wkt){
    p = ignorews(p);
    if ((p[0]=='G'||p[0]=='g')&&(p[1]=='E'||p[1]=='e')&&(p[2]=='O'||p[2]=='o')&&(p[3]=='M'||p[3]=='m')&&(p[4]=='E'||p[4]=='e')&&(p[5]=='T'||p[5]=='t')&&(p[6]=='R'||p[6]=='r')&&(p[7]=='Y'||p[7]=='y')&&(p[8]=='C'||p[8]=='c')&&(p[9]=='O'||p[9]=='o')&&(p[10]=='L'||p[10]=='l')&&(p[11]=='L'||p[11]=='l')&&(p[12]=='E'||p[12]=='e')&&(p[13]=='C'||p[13]=='c')&&(p[14]=='T'||p[14]=='t')&&(p[15]=='I'||p[15]=='i')&&(p[16]=='O'||p[16]=='o')&&(p[17]=='N'||p[17]=='n')){
        p += 18;
        return wktParseGeometryCollection(p, wkt);
    } else if ((p[0]=='M'||p[0]=='m')&&(p[1]=='U'||p[1]=='u')&&(p[2]=='L'||p[2]=='l')&&(p[3]=='T'||p[3]=='t')&&(p[4]=='I'||p[4]=='i')){
        p += 5;
        if ((p[0]=='L'||p[0]=='l')&&(p[1]=='I'||p[1]=='i')&&(p[2]=='N'||p[2]=='n')&&(p[3]=='E'||p[3]=='e')&&(p[4]=='S'||p[4]=='s')&&(p[5]=='T'||p[5]=='t')&&(p[6]=='R'||p[6]=='r')&&(p[7]=='I'||p[7]=='i')&&(p[8]=='N'||p[8]=='n')&&(p[9]=='G'||p[9]=='g')){
            p += 10;
            return wktParseMultiLineStringOrPolygon(WKT_MULTILINESTRING, p, wkt);
        } else if ((p[0]=='P'||p[0]=='p')&&(p[1]=='O'||p[1]=='o')){
            p += 2;
            if ((p[0]=='I'||p[0]=='i')&&(p[1]=='N'||p[1]=='n')&&(p[2]=='T'||p[2]=='t')){
                p += 3;
                return wktParseLineStringOrMultiPoint(WKT_MULTIPOINT, p, wkt);
            } else if ((p[0]=='L'||p[0]=='l')&&(p[1]=='Y'||p[1]=='y')&&(p[2]=='G'||p[2]=='g')&&(p[3]=='O'||p[3]=='o')&&(p[4]=='N'||p[4]=='n')){
                p += 5;
                return wktParseMultiPolygon(p, wkt);
            }       
        }
    } else if ((p[0]=='L'||p[0]=='l')&&(p[1]=='I'||p[1]=='i')&&(p[2]=='N'||p[2]=='n')&&(p[3]=='E'||p[3]=='e')&&(p[4]=='S'||p[4]=='s')&&(p[5]=='T'||p[5]=='t')&&(p[6]=='R'||p[6]=='r')&&(p[7]=='I'||p[7]=='i')&&(p[8]=='N'||p[8]=='n')&&(p[9]=='G'||p[9]=='g')){
        p += 10;
        return wktParseLineStringOrMultiPoint(WKT_LINESTRING, p, wkt);
    } else if ((p[0]=='P'||p[0]=='p')&&(p[1]=='O'||p[1]=='o')){
        p += 2;
        if ((p[0]=='I'||p[0]=='i')&&(p[1]=='N'||p[1]=='n')&&(p[2]=='T'||p[2]=='t')){
            p += 3;
            return wktParsePoint(p, wkt);
        } else if ((p[0]=='L'||p[0]=='l')&&(p[1]=='Y'||p[1]=='y')&&(p[2]=='G'||p[2]=='g')&&(p[3]=='O'||p[3]=='o')&&(p[4]=='N'||p[4]=='n')){
            p += 5;
            return wktParseMultiLineStringOrPolygon(WKT_POLYGON, p, wkt);
        }       
    }
    stdResult res = {WKT_ERRINPUT, p};
    return res;
}

wktErr wktParse(const char *input, wktGeometry **wkt){
    if (input == 0 || *input == 0){
        return WKT_ERRINPUT; 
    }
    stdResult res = wktParseGeometry((char*)input, wkt);
    if (res.err){
        return res.err;
    }
    char *p = ignorews(res.p);
    if (*p != 0){
        return WKT_ERRINPUT;
    }
    return WKT_ERRNONE;
}

static void wktFreeLineString(wktLineString *lineString){
    if (lineString->points){
        free(lineString->points);
    }
}

static void wktFreePoint(wktPoint *point){
    
}

static void wktFreeMultiPoint(wktMultiPoint *multiPoint){
    if (multiPoint->points){
        free(multiPoint->points);
    }
}

static void wktFreePolygon(wktPolygon *polygon){
    if (polygon->lineStrings){
        for (int i=0;i<polygon->size;i++){
            wktFreeLineString(&polygon->lineStrings[i]);
        }
        free(polygon->lineStrings);
    }
}

static void wktFreeMultiLineString(wktMultiLineString *multiLineString){
    if (multiLineString->lineStrings){
        for (int i=0;i<multiLineString->size;i++){
            wktFreeLineString(&multiLineString->lineStrings[i]);
        }
        free(multiLineString->lineStrings);
    }
}

void wktFree(wktGeometry *wkt) {
    if (!wkt){
        return;
    }
    switch (wkt->type){
    default:
        break;
    case WKT_POINT:
        wktFreePoint(&wkt->point);
        break;
    case WKT_LINESTRING:
        wktFreeLineString(&wkt->lineString);
        break;
    case WKT_MULTIPOINT:
        wktFreeMultiPoint(&wkt->multiPoint);
        break;
    case WKT_POLYGON:
        wktFreePolygon(&wkt->polygon);
        break;
    case WKT_MULTILINESTRING:
        wktFreeMultiLineString(&wkt->multiLineString);
        break;
    }
    memset(wkt, 0, sizeof(wktGeometry));
    free(wkt);
}

static inline char *appendStr(char *str, int *size, int *cap, char *s){
    int l = strlen(s);
    if (*size+l >= *cap){
        int ncap = *cap;
        if (ncap == 0){
            ncap = 16;
        }
        while (*size+l >= ncap){
            ncap *= 2;
        }
        char *nstr = realloc(str, ncap+1);
        if (!nstr){
            if (str){
                free(str);
            }
            return NULL;
        }
        str = nstr;
        *cap = ncap;
    }
    memcpy(str+(*size), s, l);
    (*size) += l;
    str[(*size)] = 0;
    return str;
}

static char *dstr(double n, char *str){
    sprintf(str, "%.15f", n);
    char *p = str+strlen(str)-1;
    while (*p=='0'&&p>str){
        *p = 0;
        p--;
    }
    if (*p == '.'){
        *p = 0;
    }
    return str;
}

char *wktText(wktGeometry *wkt){
    #define APPEND(s) {if (!(str = appendStr(str, &size, &cap, (s)))) goto oom;}
    if (wkt == NULL){
        return NULL;
    }
    char *str = NULL;
    int size = 0;
    int cap = 0;
    char output[50];

    switch (wkt->type){
    default:
        return NULL;
    case WKT_POINT:
        APPEND("POINT(");
        APPEND(dstr(wkt->point.x, output));
        APPEND(" ");
        APPEND(dstr(wkt->point.y, output));
        APPEND(")");
        break;
    case WKT_LINESTRING:
        APPEND("LINESTRING(");
        for (int i=0;i<wkt->lineString.size;i++){
            if (i!=0){
                APPEND(",");    
            }
            wktPoint point = wkt->lineString.points[i];
            APPEND(dstr(point.x, output));
            APPEND(" ");
            APPEND(dstr(point.y, output));
        }
        APPEND(")");
        break;
    case WKT_POLYGON:
        APPEND("POLYGON(");
        for (int i=0;i<wkt->polygon.size;i++){
            if (i!=0){
                APPEND(",");    
            }
            wktLineString lineString = wkt->polygon.lineStrings[i];
            for (int i=0;i<lineString.size;i++){
                if (i!=0){
                    APPEND(",");    
                }
                wktPoint point = lineString.points[i];
                APPEND(dstr(point.x, output));
                APPEND(" ");
                APPEND(dstr(point.y, output));
            }
        }
        APPEND(")");
        break;
    case WKT_MULTIPOINT:
        APPEND("MULTIPOINT(");
        for (int i=0;i<wkt->multiPoint.size;i++){
            if (i!=0){
                APPEND(",");    
            }
            wktPoint point = wkt->multiPoint.points[i];
            APPEND(dstr(point.x, output));
            APPEND(" ");
            APPEND(dstr(point.y, output));
        }
        APPEND(")");
        break;
    case WKT_MULTILINESTRING:
        APPEND("MULTILINESTRING(");
        for (int i=0;i<wkt->multiLineString.size;i++){
            if (i!=0){
                APPEND(",");
            }
            wktLineString lineString = wkt->multiLineString.lineStrings[i];
            for (int i=0;i<lineString.size;i++){
                if (i!=0){
                    APPEND(",");    
                }
                wktPoint point = lineString.points[i];
                APPEND(dstr(point.x, output));
                APPEND(" ");
                APPEND(dstr(point.y, output));
            }
        }
        APPEND(")");
        break;
    case WKT_MULTIPOLYGON:
        APPEND("MULTIPOLYGON(");
        for (int i=0;i<wkt->multiPolygon.size;i++){
            if (i!=0){
                APPEND(",");    
            }
            wktPolygon polygon = wkt->multiPolygon.polygons[i];
            for (int i=0;i<polygon.size;i++){
                if (i!=0){
                    APPEND(",");    
                }
                wktLineString lineString = polygon.lineStrings[i];
                for (int i=0;i<lineString.size;i++){
                    if (i!=0){
                        APPEND(",");    
                    }
                    wktPoint point = lineString.points[i];
                    APPEND(dstr(point.x, output));
                    APPEND(" ");
                    APPEND(dstr(point.y, output));
                }
            }
        }
        break;
    case WKT_GEOMETRYCOLLECTION:
        APPEND("GEOMETRYCOLLECTION(");
        for (int i=0;i<wkt->geometryCollection.size;i++){
            if (i!=0){
                APPEND(",");    
            }
            wktGeometry *geometry = wkt->geometryCollection.geometries[i];
            char *text = wktText(geometry);
            if (!text){
                goto oom;
            }
            if (!(str = appendStr(str, &size, &cap, text))) {
                free(text);
                goto oom;
            }
            free(text);
        }
        APPEND(")");
        break;
    }
    return str;
oom:
    if (str){
        free(str);
    }
    return NULL;
}


const char *wktErrText(wktErr err){
    switch (err){
    default:
        return "unknown error";
    case WKT_ERRNONE:
        return "no error";
    case WKT_ERRUNKN:
        return "unknown error";
    case WKT_ERRMEM:
        return "out of memory";
    case WKT_ERRINPUT:
        return "invalid input";
    }
}
