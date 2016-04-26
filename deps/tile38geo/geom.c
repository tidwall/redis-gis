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

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include "geom.h"
#include "grisu3.h"

static geomErr geomDecodeWKTInner(const char *input, geomWKTDecodeOpts opts, geom **g, int *size, int *read);

typedef struct ctx{
    geomErr err;
    geomWKTDecodeOpts opts;
    char *p;     // rolling pointer
    char *g;     // g buffer
    int len;     // length of g buffer

    char hasZ, hasM;  // is the current geometry defined 'Z' or 'M' or 'ZM'
    char isEmpty;     // is the current geometry defined 'EMPTY'
    char validBuffer; // is the g bytes buffer writable

    // placeholder for parsing doubles
    double x, y, z, m;
    int foundZ, foundM; // indicated that z and m in the previous read.

    int writtenPoint; // indicates that at least one point has been written.
    int mustZ, mustM; // indicates that the stream must make space for Z or M.

} ctx;

const char *geomErrText(geomErr err){
    switch (err){
    default:
        return "unknown error";
    case GEOM_ERR_NONE:
        return "no error";
    case GEOM_ERR_INPUT:
        return "input";
    case GEOM_ERR_MEMORY:
        return "memory";
    case GEOM_ERR_UNSUPPORTED:
        return "unsupported";
    }
}

static ctx *ignorews(ctx *c){
    for (;;) {
        switch (c->p[0]) {
        default:
            return c;
        case '\t': case ' ': case '\r': case '\v': case '\n': case '\f':
            c->p++;
            continue;
        }
    }
}
/* grow will increase the c->g buffer by len. */
static ctx *grow(ctx *c, int len){
    if (!c->validBuffer){
        return c;
    }
    char *m = realloc(c->g, c->len+len);
    if (!m){
        c->err = GEOM_ERR_MEMORY;
        return c;
    }
    c->g = m;
    c->len += len;
    return c;
}

static ctx *appendByte(ctx *c, uint8_t b){
    if (!c->validBuffer){
        return c;
    }
    c = grow(c, 1);
    if (c->err){
        return c;
    }
    *(c->g+c->len-1) = b;
    return c;
}

static ctx *appendBytes(ctx *c, uint8_t *b, int sz){
    if (!c->validBuffer){
        return c;
    }
    c = grow(c, sz);
    if (c->err){
        return c;
    }
    memcpy(c->g+c->len-sz, b, sz);
    return c;
}

static ctx *appendUint32(ctx *c, uint32_t n){
    if (!c->validBuffer){
        return c;
    }
    c = grow(c, 4);
    if (c->err){
        return c;
    }
    *((uint32_t*)(c->g+c->len-4)) = n;
    return c;
}

static ctx *decodeHead(ctx *c){
    c->hasZ = 0;
    c->hasM = 0;
    c->isEmpty = 0;
    int read = 0;
read_next:
    switch (c->p[0]){
    case '\t': case ' ': case '\r': case '\v': case '\n': case '\f':
        c = ignorews(c);
        if ((c->p[0]=='E'||c->p[0]=='e')&&(c->p[1]=='M'||c->p[1]=='m')&&(c->p[2]=='P'||c->p[2]=='p')&&(c->p[3]=='T'||c->p[3]=='t')&&(c->p[4]=='Y'||c->p[4]=='y')){
            if (c->isEmpty){
                c->err = GEOM_ERR_INPUT;
                return c;    
            }
            c->p += 5;
            c->isEmpty = 1;
        } else if (c->p[0]=='Z'||c->p[0]=='z'){
            if (c->hasZ||c->hasM||c->isEmpty){
                c->err = GEOM_ERR_INPUT;
                return c;    
            }
            c->p++;
            c->hasZ = 1;
            if (c->p[0]=='M'||c->p[0]=='m'){
                c->p++;
                c->hasM = 1;
            }
        } else if (c->p[0]=='M'||c->p[0]=='m'){
            if (c->hasZ||c->hasM||c->isEmpty){
                c->err = GEOM_ERR_INPUT;
                return c;    
            }
            c->p++;
            c->hasM = 1;
        }
        break;
    }
    read++;

    if ((c->hasZ || c->hasM) && !c->isEmpty && read <= 2){
        goto read_next;
    }
    c = ignorews(c);
    if (!c->isEmpty){
        if (c->p[0]!='('){
            c->err = GEOM_ERR_INPUT;
            return c;
        }
    }
    return c;
}

static ctx *decodeNumbers(ctx *c){
    char *ptr = NULL;
    c->x = strtod(c->p, &ptr);
    if (ptr-c->p == 0){
        c->err = GEOM_ERR_INPUT;
        return c;
    }
    c->p += ptr-c->p;
    c = ignorews(c);
    c->y = strtod(c->p, &ptr);
    if (ptr-c->p == 0){
        c->err = GEOM_ERR_INPUT;
        return c;
    }
    c->p += ptr-c->p;
    if (c->hasZ){
        c = ignorews(c);
        c->z = strtod(c->p, &ptr);
        if (ptr-c->p == 0){
            c->err = GEOM_ERR_INPUT;
            return c;
        }
        c->p += ptr-c->p;
        c->foundZ = 1;
    } else {
        c->z = 0;
        if (c->hasM){
            c->foundZ = 0;
        } else {
            // Z is not specified, but we should see if it inputed.
            c = ignorews(c);
            if (c->p[0]==')'||c->p[0]==','){
                return c;
            }
            c->z = strtod(c->p, &ptr);
            if (ptr-c->p == 0){
                c->err = GEOM_ERR_INPUT;
                return c;
            }
            c->p += ptr-c->p;
            c->foundZ = 1;
        }
    }
    if (c->hasM){
        c = ignorews(c);
        c->m = strtod(c->p, &ptr);
        if (ptr-c->p == 0){
            c->err = GEOM_ERR_INPUT;
            return c;
        }
        c->p += ptr-c->p;
        c->foundM = 1;
    } else {
        c->m = 0;
        if (c->hasZ){
           c->foundM = 0;
        } else{
            // M is not specified, but we should see if it inputed.
            c = ignorews(c);
            if (c->p[0]==')'||c->p[0]==','){
                return c;
            }
            c->m = strtod(c->p, &ptr);
            if (ptr-c->p == 0){
                c->err = GEOM_ERR_INPUT;
                return c;
            }
            c->p += ptr-c->p;
            c->foundM = 1;
        }
    }
    return c;
}

static ctx *appendType(ctx *c, int type){
    if (c->hasZ && c->hasM){
        c = appendUint32(c, 3000+type);
    } else if (c->hasM){
        c = appendUint32(c, 2000+type);
    } else if (c->hasZ){
        c = appendUint32(c, 1000+type);
    } else {
        c = appendUint32(c, type);
    }
    return c;
}

static ctx *appendPoint(ctx *c){
    if (!c->validBuffer){
        return c;
    }
    int sz = 16;
    if (!c->writtenPoint){
        if (c->hasZ||c->foundZ){
            sz += 8;
            c->mustZ = 1;
        }
        if (c->hasM||c->foundM){
            sz += 8;
            c->mustM = 1;
        }
    }
    c = grow(c, sz);
    if (c->err){
        return c;
    }
    double *g = (double*)(c->g+c->len-sz);
    *(g++) = c->x;
    *(g++) = c->y;
    if (c->mustZ){
        *(g++) = c->z;
    }
    if (c->mustM){
        *(g++) = c->m;
    }
    if (!c->writtenPoint){
        // reset the type
        uint32_t type = *((uint32_t*)(c->g+1));
        if (type>3000) type -= 3000;
        else if (type>2000) type -= 2000;
        else if (type>1000) type -= 1000;
        if (c->mustZ && c->mustM){
            type = 3000+type;
        } else if (c->mustM){
            type = 2000+type;
        } else if (c->mustZ){
            type = 1000+type;
        }
        *((uint32_t*)(c->g+1)) = type;
    }
    c->writtenPoint = 1;

    return c;
}

static ctx *geomDecodePoint(ctx *c){
    c = decodeHead(c);
    if (c->err){
        return c;
    }
    c = appendType(c, GEOM_POINT);
    if (c->err){
        return c;
    }
    if (c->isEmpty){
        c->x = 0;
        c->y = 0;
        c->z = 0;
        c->m = 0;
    } else{
        if (c->p[0]!='('){
            c->err = GEOM_ERR_INPUT;
            return c;   
        }
        c->p++;
        c = ignorews(c);
        c = decodeNumbers(c);
        if (c->err){
            return c;
        }
        c = ignorews(c);
        if (c->p[0]!=')'){
            c->err = GEOM_ERR_INPUT;
            return c;   
        }
        c->p++;
    }
    c = appendPoint(c);

    return c;
}

static ctx *decodeSeriesSegment(ctx *c, int level){
    int szpos = c->len; // track the pos of size
    c = appendUint32(c, 0);
    if (c->err){
        return c;
    }
    if (c->isEmpty){
        return c;
    }
    if (c->p[0]!='('){
        c->err = GEOM_ERR_INPUT;
        return c;   
    }
    c->p++;
    c = ignorews(c);
    if (c->p[0]==')'){
        c->p++;
        return c;   
    }
    int sz = 0;
    for (;;){
        c = ignorews(c);
        if (level<0){
            // decode geometries
            geom *g = NULL;
            int sz = 0;
            int read = 0;
            geomErr err = geomDecodeWKTInner(c->p, c->opts|GEOM_WKT_LEAVE_OPEN, &g, &sz, &read);
            if (err){
                c->err = err;
                return c;
            }
            c->p += read;
            c = appendBytes(c, (uint8_t*)g, sz);
            if (c->err){
                geomFree(g);
                return c;
            }
            geomFree(g);
        } else if (level==0){
            // decode the numbers
            c = decodeNumbers(c);
            if (c->err){
                return c;
            }
            c = appendPoint(c);
            if (c->err){
                return c;
            }
        } else {
            // decode nested series
            c = decodeSeriesSegment(c, level-1);
            if (c->err){
                return c;
            }
        }
        c = ignorews(c);
        sz++;
        if (c->p[0] == ','){
            c->p++;
            c = ignorews(c);
            continue;
        } else if (c->p[0] == ')'){
            c->p++;
            break;
        } else {
            c->err = GEOM_ERR_INPUT;
            return c;   
        }
    }
    *((uint32_t*)(c->g+szpos)) = sz;
    return c;
}

static ctx *geomDecodeSeries(ctx *c, geomType type){
    c = decodeHead(c);
    if (c->err){
        return c;
    }
    c = appendType(c, type);
    if (c->err){
        return c;
    }
    switch (type){
    default:
        c->err = GEOM_ERR_INPUT;
        return c;
    case GEOM_LINESTRING:
    case GEOM_MULTIPOINT:
        c = decodeSeriesSegment(c, 0);
        break;
    case GEOM_POLYGON:
    case GEOM_MULTILINESTRING:
        c = decodeSeriesSegment(c, 1);
        break;
    case GEOM_MULTIPOLYGON:
        c = decodeSeriesSegment(c, 2);
        break;
    case GEOM_GEOMETRYCOLLECTION:
        c = decodeSeriesSegment(c, -1);
        break;
    }
    return c;
}


ctx *geomDecodeGeometry(ctx *c){
    c = ignorews(c);
    if ((c->p[0]=='G'||c->p[0]=='g')&&(c->p[1]=='E'||c->p[1]=='e')&&(c->p[2]=='O'||c->p[2]=='o')&&(c->p[3]=='M'||c->p[3]=='m')&&(c->p[4]=='E'||c->p[4]=='e')&&(c->p[5]=='T'||c->p[5]=='t')&&(c->p[6]=='R'||c->p[6]=='r')&&(c->p[7]=='Y'||c->p[7]=='y')&&(c->p[8]=='C'||c->p[8]=='c')&&(c->p[9]=='O'||c->p[9]=='o')&&(c->p[10]=='L'||c->p[10]=='l')&&(c->p[11]=='L'||c->p[11]=='l')&&(c->p[12]=='E'||c->p[12]=='e')&&(c->p[13]=='C'||c->p[13]=='c')&&(c->p[14]=='T'||c->p[14]=='t')&&(c->p[15]=='I'||c->p[15]=='i')&&(c->p[16]=='O'||c->p[16]=='o')&&(c->p[17]=='N'||c->p[17]=='n')){
        c->p += 18;
        return geomDecodeSeries(c, GEOM_GEOMETRYCOLLECTION);
    } else if ((c->p[0]=='M'||c->p[0]=='m')&&(c->p[1]=='U'||c->p[1]=='u')&&(c->p[2]=='L'||c->p[2]=='l')&&(c->p[3]=='T'||c->p[3]=='t')&&(c->p[4]=='I'||c->p[4]=='i')){
        c->p += 5;
        if ((c->p[0]=='L'||c->p[0]=='l')&&(c->p[1]=='I'||c->p[1]=='i')&&(c->p[2]=='N'||c->p[2]=='n')&&(c->p[3]=='E'||c->p[3]=='e')&&(c->p[4]=='S'||c->p[4]=='s')&&(c->p[5]=='T'||c->p[5]=='t')&&(c->p[6]=='R'||c->p[6]=='r')&&(c->p[7]=='I'||c->p[7]=='i')&&(c->p[8]=='N'||c->p[8]=='n')&&(c->p[9]=='G'||c->p[9]=='g')){
            c->p += 10;
            return geomDecodeSeries(c, GEOM_MULTILINESTRING);
        } else if ((c->p[0]=='P'||c->p[0]=='p')&&(c->p[1]=='O'||c->p[1]=='o')){
            c->p += 2;
            if ((c->p[0]=='I'||c->p[0]=='i')&&(c->p[1]=='N'||c->p[1]=='n')&&(c->p[2]=='T'||c->p[2]=='t')){
                c->p += 3;
                return geomDecodeSeries(c, GEOM_MULTIPOINT);
            } else if ((c->p[0]=='L'||c->p[0]=='l')&&(c->p[1]=='Y'||c->p[1]=='y')&&(c->p[2]=='G'||c->p[2]=='g')&&(c->p[3]=='O'||c->p[3]=='o')&&(c->p[4]=='N'||c->p[4]=='n')){
                c->p += 5;
                return geomDecodeSeries(c, GEOM_MULTIPOLYGON);
            }       
        }
    } else if ((c->p[0]=='L'||c->p[0]=='l')&&(c->p[1]=='I'||c->p[1]=='i')&&(c->p[2]=='N'||c->p[2]=='n')&&(c->p[3]=='E'||c->p[3]=='e')&&(c->p[4]=='S'||c->p[4]=='s')&&(c->p[5]=='T'||c->p[5]=='t')&&(c->p[6]=='R'||c->p[6]=='r')&&(c->p[7]=='I'||c->p[7]=='i')&&(c->p[8]=='N'||c->p[8]=='n')&&(c->p[9]=='G'||c->p[9]=='g')){
        c->p += 10;
        return geomDecodeSeries(c, GEOM_LINESTRING);
    } else if ((c->p[0]=='P'||c->p[0]=='p')&&(c->p[1]=='O'||c->p[1]=='o')){
        c->p += 2;
        if ((c->p[0]=='I'||c->p[0]=='i')&&(c->p[1]=='N'||c->p[1]=='n')&&(c->p[2]=='T'||c->p[2]=='t')){
            c->p += 3;
            return geomDecodePoint(c);
        } else if ((c->p[0]=='L'||c->p[0]=='l')&&(c->p[1]=='Y'||c->p[1]=='y')&&(c->p[2]=='G'||c->p[2]=='g')&&(c->p[3]=='O'||c->p[3]=='o')&&(c->p[4]=='N'||c->p[4]=='n')){
            c->p += 5;
            return geomDecodeSeries(c, GEOM_POLYGON);
        }       
    }
    c->err = GEOM_ERR_INPUT;
    return c;
}

static geomErr geomDecodeWKTInner(const char *input, geomWKTDecodeOpts opts, geom **g, int *size, int *read){
    if (input == 0 || *input == 0){
        return GEOM_ERR_INPUT; 
    }
    ctx cc;
    memset(&cc, 0, sizeof(ctx));
    ctx *c = &cc;
    c->opts = opts;
    c->p = (char*)input;
    c->validBuffer = g && size;
    if (LITTLE_ENDIAN){
        c = appendByte(c, 0x01);
    } else{
        c = appendByte(c, 0x00);
    }
    if (c->err){
        return c->err;
    }
    c = ignorews(c);
    c = geomDecodeGeometry(c);
    if (c->err){
        free(c->g);
        return c->err;
    }
    if (!(opts&GEOM_WKT_LEAVE_OPEN)){
        c = ignorews(c);
        if (c->p[0]){
            // invalid characters found at end of input.
            free(c->g);
            return GEOM_ERR_INPUT; 
        }
    }
    if (g && size){
        *g = (geom*)c->g;
        *size = c->len;
    }
    if (read){
        *read = c->p-input;
    }
    return GEOM_ERR_NONE;
}

static int geomIsZ(uint8_t *g){
    uint32_t type = *((uint32_t*)(g+1));
    if (type > 3000){
        return 1;
    } else if (type > 2000){
        return 0;
    } else if (type > 1000){
        return 1;
    }
    return 0;
}
static int geomIsM(uint8_t *g){
    uint32_t type = *((uint32_t*)(g+1));
    if (type > 3000){
        return 1;
    } else if (type > 2000){
        return 1;
    } else if (type > 1000){
        return 0;
    }
    return 0;
}

// static int geomGenericLength(uint8_t *g){
//     return (int)*((uint32_t*)(g+5));
// }

////////////////////////////////////////////////
// public apis
////////////////////////////////////////////////
geomType geomGetType(geom *g){
    uint32_t type = *((uint32_t*)((uint8_t*)(g)+1));
    if (type > 3000){
        type = type-3000;
    } else if (type > 2000){
        type = type-2000;
    } else if (type > 1000){
        type = type-1000;
    }
    if (!GEOM_VALID_TYPE(type)){
        return GEOM_UNKNOWN;
    }
    return (geomType)type;
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
    dtoa_grisu3(n, str);
    //sprintf(str, "%.15f", n);
    // char *p = str+nn;
    // while (*p=='0'&&p>str){
    //     *p = 0;
    //     p--;
    // }
    // if (*p == '.'){
    //     *p = 0;
    // }
    return str;
}

void geomFreeWKT(char *wkt){
    if (wkt){
        free(wkt);
    }
}


static char *geomEncodeWKTInner(geom *g, geomWKTEncodeOpts opts, int *read){
    #define APPEND(s) {if (!(str = appendStr(str, &size, &cap, (s)))) goto oom;}
    #define APPEND_POINT(){\
        APPEND(dstr(*((double*)gb), output));\
        gb += 8;\
        APPEND(" ");\
        APPEND(dstr(*((double*)gb), output));\
        gb += 8;\
        if (isZ){\
            APPEND(" ");\
            APPEND(dstr(*((double*)gb), output));\
            gb += 8;\
        }\
        if (isM){\
            APPEND(" ");\
            APPEND(dstr(*((double*)gb), output));\
            gb += 8;\
        }\
    }
    #define APPEND_HEAD(type){\
        if (showZM){\
            if (isZ&&isM){\
                APPEND(type " ZM")\
            }else if (isZ){\
                APPEND(type " Z")\
            }else if (isM){\
                APPEND(type " M")\
            }else{\
                APPEND(type)\
            }\
        } else{\
            APPEND(type)\
        }\
        gb+=5;\
    }

    #define APPEND_HEAD_SERIES(type){\
        APPEND_HEAD(type);\
        len = *((uint32_t*)(gb));\
        gb+=4;\
        if (len == 0){\
            if (opts&GEOM_WKT_SHOW_EMPTY){\
                APPEND(" EMPTY");\
            }else{\
                APPEND("()");\
            }\
            break;\
        }\
    }

    if (g == NULL){
        return NULL;
    }
    char *str = NULL;
    int size = 0;
    int cap = 0;
    char output[50];
    uint8_t *gb = (uint8_t*)g;
    int isZ = geomIsZ(gb);
    int isM = geomIsM(gb);
    int len = 0;
    int showZM = (isM&&!isZ)||(opts&GEOM_WKT_SHOW_ZM);
    char *wkt = NULL;

    int pointSize = 16;
    if (isZ){
        pointSize+=8;
    }
    if (isM){
        pointSize+=8;
    }
    geomType type = geomGetType(g);
    switch (type){
    default:
        return NULL;
    case GEOM_POINT:
        APPEND_HEAD("POINT");
        APPEND("(")
        APPEND_POINT();
        APPEND(")");
        break;
    case GEOM_LINESTRING:
    case GEOM_MULTIPOINT:
        if (type == GEOM_LINESTRING){
            APPEND_HEAD_SERIES("LINESTRING");
        } else {
            APPEND_HEAD_SERIES("MULTIPOINT");
        }
        APPEND("(")
        for (int i=0;i<len;i++){
            if (i!=0){
                APPEND(",");    
            }
            APPEND_POINT();
        }
        APPEND(")");
        break;
    case GEOM_POLYGON:
    case GEOM_MULTILINESTRING:
        if (type == GEOM_POLYGON){
            APPEND_HEAD_SERIES("POLYGON");
        } else {
            APPEND_HEAD_SERIES("MULTILINESTRING");
        }
        APPEND("(")
        for (int i=0;i<len;i++){
            if (i!=0){
                APPEND(",");
            }
            int len2 = *((uint32_t*)gb);
            gb += 4;
            if (len2 == 0){
                if (opts&GEOM_WKT_SHOW_EMPTY){
                    APPEND("EMPTY");
                } else{
                    APPEND("()");
                }
            } else {
                APPEND("(");
                for (int i=0;i<len2;i++){
                    if (i!=0){
                        APPEND(",");
                    }
                    APPEND_POINT();
                }
                APPEND(")");
            }
        }
        APPEND(")");
        break;
    case GEOM_MULTIPOLYGON:
        APPEND_HEAD_SERIES("MULTIPOLYGON");
        APPEND("(")
        for (int i=0;i<len;i++){
            if (i!=0){
                APPEND(",");
            }
            int len2 = *((uint32_t*)gb);
            gb += 4;
            if (len2 == 0){
                if (opts&GEOM_WKT_SHOW_EMPTY){
                    APPEND("EMPTY");
                } else{
                    APPEND("()");
                }
            } else {
                APPEND("(");
                for (int i=0;i<len2;i++){
                    if (i!=0){
                        APPEND(",");
                    }
                    int len3 = *((uint32_t*)gb);
                    gb += 4;
                    if (len3 == 0){
                        if (opts&GEOM_WKT_SHOW_EMPTY){
                            APPEND("EMPTY");
                        } else{
                            APPEND("()");
                        }
                    } else {
                        APPEND("(");
                        for (int i=0;i<len3;i++){
                            if (i!=0){
                                APPEND(",");
                            }
                            APPEND_POINT();
                        }
                        APPEND(")");
                    }
                }
                APPEND(")");
            }
        }
        APPEND(")");
        break;
    case GEOM_GEOMETRYCOLLECTION:
        APPEND_HEAD_SERIES("GEOMETRYCOLLECTION");
        APPEND("(");
        for (int i=0;i<len;i++){
            if (i!=0){
                APPEND(",");
            }
            int read = 0;
            wkt = geomEncodeWKTInner((geom*)gb, opts, &read);
            if (!wkt){
                goto oom;
            }
            gb += read;
            APPEND(wkt);
            geomFreeWKT(wkt);
            wkt = NULL;
        }
        APPEND(")");
        break;
    }
    if (read){
        *read = (void*)gb-(void*)g;
    }
    if (wkt){
        geomFreeWKT(wkt);
    }
    return str;
oom:
    if (str){
        free(str);
    }
    if (wkt){
        geomFreeWKT(wkt);
    }
    return NULL;
}

void geomFree(geom *g){
    if (g){
        free(g);
    }
}

char *geomEncodeWKT(geom *g, geomWKTEncodeOpts opts){
    return geomEncodeWKTInner(g, opts, NULL);
}

geomErr geomDecodeWKT(const char *input, geomWKTDecodeOpts opts, geom **g, int *size){
    return geomDecodeWKTInner(input, opts, g, size, NULL);
}

geomPoint geomGetPoint(geom *g){
    geomPoint point;
    memset(&point, 0, sizeof(point));
    switch (geomGetType(g)){
    default:
        break;        
    case GEOM_UNKNOWN:
        break;
    case GEOM_POINT:
        point.x = ((double*)(((uint8_t*)g)+5))[0];
        point.y = ((double*)(((uint8_t*)g)+5))[1];
        break;
    }
    return point;
}

geomRect geomGetRect(geom *g){
    geomRect rect;
    memset(&rect, 0, sizeof(rect));
    switch (geomGetType(g)){
    default:
        break;        
    case GEOM_UNKNOWN:
        break;
    case GEOM_POINT:
        rect.min.x = ((double*)(((uint8_t*)g)+5))[0];
        rect.min.y = ((double*)(((uint8_t*)g)+5))[1];
        rect.max.x = rect.min.x;
        rect.max.y = rect.min.y;
        break;
    }
    return rect;
}



