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

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include "geom.h"

char *randgeometrycollection(int vary, int dim);

// parseSerializeAndCompare parses, serializes, parses again, serializes again
// and compares the the first and second serialized strings.
void parseSerializeAndCompare(const char *input){
    geom *g = NULL;
    int sz = 0;
    geomErr err = geomDecodeWKT(input, 0, &g, &sz);
    assert(err == GEOM_ERR_NONE);
    char *text1 = geomEncodeWKT(g, 0);
    assert(text1 != NULL);
    geomFree(g);
    err = geomDecodeWKT(input, 0, &g, &sz);
    assert(err == GEOM_ERR_NONE);
    char *text2 = geomEncodeWKT(g, 0);
    assert(text2 != NULL);
    geomFree(g);
    assert(strcmp(text1, text2)==0);
    free(text1);
    free(text2);
}

// dstr converts a double to a string and keeps much of the precision.
// This is quick but not anything like the Grisu algorightm.
char *dstr(double n, char *str){
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

// randd creates a random double [1-0].
double randd(){
    return ((rand()%RAND_MAX) / (double)RAND_MAX);
}

// randx create a random longitude.
double randx() {
    return randd() * 360.0 - 180.0;
}

// randy create a random latitude.
double randy() {
    return randd() * 180.0 - 90.0;
}

#define VINIT()\
    char *str = malloc(257);\
    int sz = 256;\
    int idx = 0;\
    str[0] = 0;

#define VCHAR(ch){\
    char cch = ch;\
    if (idx == sz){\
        sz *= 2;\
        str = realloc(str, sz+1);\
        assert(str);\
    }\
    if (vary && cch == ' '){\
        switch (rand()%6){\
        case 0: cch = '\t'; break;\
        case 1: cch = ' '; break;\
        case 2: cch = '\r'; break;\
        case 3: cch = '\v'; break;\
        case 4: cch = '\n'; break;\
        case 5: cch = '\f'; break;\
        }\
    }\
    str[idx++] = cch;\
    str[idx] = 0;\
}

#define VSPACES()\
    if (vary){\
        int n = rand() % 5;\
        for (int i=0;i<n;i++){\
            VCHAR(' ');\
        }\
    }

#define VSTR(sstr){\
    VSPACES();\
    char *p = sstr;\
    while (*p){\
        char ch = *p;\
        if (vary){\
            if (rand()%2==0){\
                ch = tolower(ch);\
            } else{\
                ch = toupper(ch);\
            }\
        }\
        VCHAR(ch);\
        p++;\
    }\
    VSPACES();\
}

#define VPOINT(dim){\
    char *pstr = randpoint(vary, (dim));\
    VSTR(pstr);\
    free(pstr);\
}

#define VLINESTRING(dim){\
    char *pstr = randlinestring(vary, (dim));\
    VSTR(pstr);\
    free(pstr);\
}

#define VPOLYGON(dim){\
    char *pstr = randpolygon(vary, (dim));\
    VSTR(pstr);\
    free(pstr);\
}

#define VMULTIPOLYGON(dim){\
    char *pstr = randmultipolygon(vary, (dim));\
    VSTR(pstr);\
    free(pstr);\
}

#define VGEOMETRYCOLLECTION(dim){\
    char *pstr = randgeometrycollection(vary, (dim));\
    VSTR(pstr);\
    free(pstr);\
}


// randx create a random string lat lon in XY format.
char *randpoint(int vary, int dim){
    char output[50];
    VINIT();
    for (int i=0;i<dim;i++){
        if (i!=0){
            VCHAR(' ');     
        }
        dstr(randx(), output);
        VSTR(output);   
    }
    return str;
}


char *randlinestring(int vary, int dim){
    VINIT();
    int n = 5;
    if (vary){
        n = rand() % 25;
    }
    for (int i=0;i<n;i++){
        if (i!=0){
            VCHAR(',');
        }
        VPOINT(2);
    }
    return str;
}


char *randpolygon(int vary, int dim){
    VINIT();
    int n = 3;
    if (vary){
        n = (rand() % 10) + 1;
    }
    for (int i=0;i<n;i++){
        if (i!=0){
            VCHAR(',');
        }
        VCHAR('('); 
        VLINESTRING(2);
        VCHAR(')');
    }
    return str;
}

char *randmultipolygon(int vary, int dim){
    VINIT();
    int n = 3;
    if (vary){
        n = (rand() % 10) + 1;
    }
    for (int i=0;i<n;i++){
        if (i!=0){
            VCHAR(',');
        }
        VCHAR('('); 
        VPOLYGON(2);
        VCHAR(')');
    }
    return str;
}

// randGeomPoint creates a random POINT(1 2) geometry string.
char *randGeomPoint(int vary, int dim){
    VINIT();
    VSTR("POINT");
    VCHAR('(');
    VPOINT(dim);
    VCHAR(')');
    return str; 
}

// randGeomLineString creates a random LINESTRING(1 2, 3 4, 5 6) geometry string.
char *randGeomLineString(int vary, int dim){
    VINIT();
    VSTR("LINESTRING");
    VCHAR('(');
    VLINESTRING(dim);
    VCHAR(')');
    return str; 
}

char *randGeomMultiPoint(int vary, int dim){
    VINIT();
    VSTR("MULTIPOINT");
    VCHAR('(');
    VLINESTRING(dim);
    VCHAR(')');
    return str; 
}

char *randGeomPolygon(int vary, int dim){
    VINIT();
    VSTR("POLYGON");
    VCHAR('(');
    VPOLYGON(dim);
    VCHAR(')');
    return str; 
}

char *randGeomMultiLineString(int vary, int dim){
    VINIT();
    VSTR("MULTILINESTRING");
    VCHAR('(');
    VPOLYGON(dim);
    VCHAR(')');
    return str; 
}

char *randGeomMultiPolygon(int vary, int dim){
    VINIT();
    VSTR("MULTIPOLYGON");
    VCHAR('(');
    VMULTIPOLYGON(dim);
    VCHAR(')');
    return str; 
}

char *randGeomGeometryCollection(int vary, int dim){
    VINIT();
    VSTR("GEOMETRYCOLLECTION");
    VCHAR('(');
    VGEOMETRYCOLLECTION(dim);
    VCHAR(')');
    return str; 
}


char *randgeometrycollection(int vary, int dim){
    VINIT();
    int n = 3;
    if (vary){
        n = (rand() % 10) + 1;
    }
    for (int i=0;i<n;i++){
        if (i!=0){
            VCHAR(',');
        }
        char *sstr = NULL;
        switch (rand() % 6){
        case 0: sstr = randGeomPoint(vary, dim); break;
        case 1: sstr = randGeomLineString(vary, dim); break;
        case 2: sstr = randGeomMultiPoint(vary, dim); break;
        case 3: sstr = randGeomPolygon(vary, dim); break;
        case 4: sstr = randGeomMultiLineString(vary, dim); break;
        case 5: sstr = randGeomMultiPolygon(vary, dim); break;
        }
        VSTR(sstr);
        free(sstr);
    }
    return str;
}

void testGeom(int count, int dim, char *(*randGeomCreate)(int, int)){
    for (int i=0;i<count;i++){
        char *tstr = randGeomCreate(0, dim);
        parseSerializeAndCompare(tstr);
        free(tstr);
    }
    for (int i=0;i<count;i++){
        char *tstr = randGeomCreate(1, dim);
        parseSerializeAndCompare(tstr);
        free(tstr);
    }
}

int test_Geom(){
    srand(time(NULL)/clock());
    testGeom(400, 2, randGeomPoint);
    testGeom(300, 2, randGeomLineString);
    testGeom(300, 2, randGeomMultiPoint);
    testGeom(200, 2, randGeomPolygon);
    testGeom(200, 2, randGeomMultiLineString);
    testGeom(100, 2, randGeomMultiPolygon);
    testGeom(50,  2, randGeomGeometryCollection);
    return 1;
}

int test_GeomZ(){
    srand(time(NULL)/clock());
    testGeom(400, 3, randGeomPoint);
    testGeom(300, 3, randGeomLineString);
    testGeom(300, 3, randGeomMultiPoint);
    testGeom(200, 3, randGeomPolygon);
    testGeom(200, 3, randGeomMultiLineString);
    testGeom(100, 3, randGeomMultiPolygon);
    testGeom(50,  3, randGeomGeometryCollection);
    return 1;
}

int test_GeomZM(){
    srand(time(NULL)/clock());
    testGeom(400, 4, randGeomPoint);
    testGeom(300, 4, randGeomLineString);
    testGeom(300, 4, randGeomMultiPoint);
    testGeom(200, 4, randGeomPolygon);
    testGeom(200, 4, randGeomMultiLineString);
    testGeom(100, 4, randGeomMultiPolygon);
    testGeom(50,  4, randGeomGeometryCollection);
    return 1;
}
