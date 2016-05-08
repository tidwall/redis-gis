/* Redis Geospatial implementation.
 *
 * Copyright (c) 2016, Josh Baker <joshbaker77@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <ctype.h>
#include "spatial.h"
#include "rtree.h"
#include "geoutil.h"
#include "geom.h"
#include "hash.h"
#include "bing.h"


int hashTypeGetValue(robj *o, sds field, unsigned char **vstr, unsigned int *vlen, long long *vll);
int hashTypeSet(robj *o, sds field, sds value, int flags);
sds hashTypeGetFromHashTable(robj *o, sds field);
size_t hashTypeGetValueLength(robj *o, sds field);
int pubsubSubscribeChannel(client *c, robj *channel);
int pubsubUnsubscribePattern(client *c, robj *pattern, int notify);


#define FENCE_ENTER    (1<<1)
#define FENCE_EXIT     (1<<2)
#define FENCE_CROSS    (1<<3)
#define FENCE_INSIDE   (1<<4)
#define FENCE_OUTSIDE  (1<<5)
#define FENCE_KEYDEL   (1<<6)
#define FENCE_FIELDDEL (1<<7)
#define FENCE_FLUSH    (1<<8)
#define FENCE_ALL      (FENCE_ENTER|FENCE_EXIT|FENCE_CROSS|\
                       FENCE_INSIDE|FENCE_OUTSIDE|FENCE_KEYDEL|\
                       FENCE_FIELDDEL|FENCE_FIELDDEL)

#define FENCE_NOTIFY_SET 1
#define FENCE_NOTIFY_DEL 2

#define WITHIN     1
#define INTERSECTS 2
#define RADIUS     1
#define GEOMETRY   2
#define BOUNDS     3

#define OUTPUT_COUNT    1
#define OUTPUT_FIELD    2
#define OUTPUT_WKT      3
#define OUTPUT_WKB      4
#define OUTPUT_JSON     5
#define OUTPUT_POINT    6
#define OUTPUT_BOUNDS   7
#define OUTPUT_HASH     8
#define OUTPUT_QUAD     9
#define OUTPUT_TILE    10


typedef struct resultItem {
    char *field;
    int fieldLen;
    char *value;
    int valueLen;

} resultItem;

typedef struct searchContext {
    spatial *s;
    client *c;
    int searchType;
    int targetType;
    int fail;
    int len;
    int cap;
    resultItem *results;
    long long cursor;
    sds pattern;
    int allfields;
    int output;
    int precision;
    int nofields;
    int fence;
    int releaseg;

    // bounds
    geomRect bounds;

    // radius
    geomCoord center;
    double meters;

    // geometry
    geom g;
    int sz;
    geomPolyMap *m;

} searchContext;


typedef struct fence {
    robj *channel;
    int allfields;
    sds pattern;
    int targetType;
    geomCoord center;
    double meters;
    int searchType;
    geom g;
    int sz;
    geomPolyMap *m;
} fence;

void freeFence(fence *f){
    if (!f){
        return;
    }
    decrRefCount(f->channel);
    if (f->pattern) sdsfree(f->pattern);
    if (f->m) geomFreePolyMap(f->m);
    if (f->g) geomFree(f->g);
    zfree(f);
}


// get an sds based on the key. 
// return value must be freed by the caller.
static sds hashTypeGetNewSds(robj *o, sds key){
    unsigned char *vstr = NULL;
    unsigned int vlen = UINT_MAX;
    long long vll = LLONG_MAX;
    if (hashTypeGetValue(o, key, &vstr, &vlen, &vll) == C_ERR) return NULL;
    if (!vstr) return NULL;
    return sdsnewlen(vstr, vlen);
}

// returns a raw pointer to the value
static void *hashTypeGetRaw(robj *o, sds key){
    unsigned char *vstr = NULL;
    unsigned int vlen = UINT_MAX;
    long long vll = LLONG_MAX;
    if (hashTypeGetValue(o, key, &vstr, &vlen, &vll) == C_ERR) return NULL;
    return vstr;
}

static void hashTypeIteratorValue(hashTypeIterator *hi, int what, unsigned char **vstr, unsigned int *vlen, long long *vll) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {
        hashTypeCurrentFromZiplist(hi, what, vstr, vlen, vll);
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        sds ele = hashTypeCurrentFromHashTable(hi, what);
        *vstr = (unsigned char*) ele;
        *vlen = sdslen(ele);
    } else {
        serverPanic("Unknown hash encoding");
    }
}

int spatialTypeSet(robj *o, sds field, sds val, int notify);
int spatialTypeDelete(robj *o, sds field, geomRect *rin, int notify);
unsigned long spatialTypeLength(robj *o);
size_t spatialTypeGetValueLength(robj *o, sds field);
int spatialTypeExists(robj *o, sds field);

struct spatial {
    robj *h;        // main hash store that persists to RDB.
    rtree *tr;      // underlying spatial index.
    fence **fences; // the stored fences
    int fcap, flen; // the cap/len for fence array


    // The following fields are for mapping an idx to a key and 
    // vice versa. The rtree expects that each entry has a pointer to
    // an object in memory. This should be the base address of the 
    // sds key that is stored in the main hash store, that way there is
    // no extra memory overhead except a pointer. But at the moment I'm 
    // 100% sure how safe it is to expect that a key in the hash will 
    // not change it's base pointer address. So until further testing
    // around hash key stability we will increment an idx pointer and
    // map this value to a key, then assign this idx value to each 
    // rtree entry. This allows a reverse lookup to the key. This is a 
    // safe (albeit slower and higher mem usage) way to track entries.
    char *idx;     // pointer that acts as a private id for entries.
    robj *keyhash; // stores key -> idx
    robj *idxhash; // stores idx -> key
};

static robj *spatialGetHash(robj *o){
    return ((spatial*)o->ptr)->h;
}

void *robjSpatialGetHash(void *o){
    return spatialGetHash((robj*)o);
}

spatial *spatialNew(){
    spatial *s = zcalloc(sizeof(spatial));
    if (!s){
        goto err;
    }
    s->h = createHashObject();
    s->keyhash = createHashObject();
    s->idxhash = createHashObject();
    s->tr = rtreeNew();
    if (!s->tr){
        goto err;
    }
    return s;
err:
    spatialFree(s);
    return NULL;
}


/* robjSpatialNewHash creates a new spatial object with an existing hash.
 * This is is called from rdbLoad(). */
void *robjSpatialNewHash(void *o) {
    robj *so;
    hashTypeIterator *hi;
    so = createSpatialObject();

    // iterator through the rdb hash and insert items into 
    // spatial one by one. This is important because rdb values may
    // come from systems that have a different endianess. we also
    // want the opportunity to check for wkb corruption.
    hi = hashTypeInitIterator(o);
    while (hashTypeNext(hi) != C_ERR) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;
        sds field;
        sds value;
        hashTypeIteratorValue(hi, OBJ_HASH_KEY, &vstr, &vlen, &vll);
        if (!vstr){
            continue;
        }
        field = sdsnewlen(vstr, vlen);
        hashTypeIteratorValue(hi, OBJ_HASH_VALUE, &vstr, &vlen, &vll);
        if (!vstr){
            sdsfree(field);
            continue;
        }
        value = sdsnewlen(vstr, vlen);
        spatialTypeSet(so, field, value, 0);
        sdsfree(value);
        sdsfree(field);
    }
    freeHashObject(o); // free the old hash
    return so;
}

void spatialFree(spatial *s){
    if (s){
        if (s->h){
            freeHashObject(s->h);
        }
        if (s->keyhash){
            freeHashObject(s->keyhash);
        }
        if (s->idxhash){
            freeHashObject(s->idxhash);
        }
        if (s->tr){
            rtreeFree(s->tr);   
        }
        if (s->fences){
            for (int i=0;i<s->flen;i++){
                freeFence(s->fences[i]);
            }
            zfree(s->fences);
        }
        zfree(s);
    }
}

int matchSearch(
    geom g, geomPolyMap *targetMap,
    int targetType, int searchType, 
    geomCoord center, double meters
){
    int match = 0;
    if (geomIsSimplePoint(g) && targetType == RADIUS){
        match = geomCoordWithinRadius(geomCenter(g), center, meters);
    } else {
        geomPolyMap *m = geomNewPolyMapSingleThreaded(g);
        if (!m){
            return 0;
        }
        if (searchType==WITHIN){
            match = geomPolyMapWithin(m, targetMap);
        } else {
            match = geomPolyMapIntersects(m, targetMap);
        }
        geomFreePolyMap(m);
    }
    return match;
}


static robj *extractFenceKey(client *c){
    if (!c->spatial_fence){
        return NULL;
    }
    if (!strncmp(c->spatial_fence, "fence$", 6)){
        char *p = c->spatial_fence+6;
        while (*p){
            if (*p=='$'){
                return createStringObject(p+1, sdslen(c->spatial_fence)-(p-c->spatial_fence)-1);
            }
            p++;
        }
    }
    return NULL;
}

void spatialReleaseAllFences(client *c){
    if (!c->spatial_fence){
        return;
    }
    robj *key = extractFenceKey(c);
    if (key){
        robj *o = lookupKeyRead(c->db, key);
        if (o != NULL && o->type == OBJ_SPATIAL) {
            spatial *s = o->ptr;
            for (int i=0;i<s->flen;i++){
                fence *f = s->fences[i];
                if (!sdscmp(f->channel->ptr, c->spatial_fence)){
                    freeFence(f);
                    s->fences[i] = s->fences[s->flen-1];
                    s->flen--;
                    break;
                }
            }
        }
        decrRefCount(key);
    }
    robj *pchan = createStringObject(c->spatial_fence, sdslen(c->spatial_fence));
    pubsubUnsubscribePattern(c,pchan,0);
    decrRefCount(pchan);
    sdsfree(c->spatial_fence);
    c->spatial_fence = NULL;
}


static robj *newinoutmsg(char *prefix, sds field){
    int l = strlen(prefix);
    char *str = zmalloc(l+sdslen(field));
    memcpy(str, prefix, l);
    memcpy(str+l, field, sdslen(field));
    return createStringObject(str, sdslen(field)+l);
}

void processFences(spatial *s, sds field, geom g, int fenceNotify){
    robj *imsg = NULL;
    robj *omsg = NULL;
    if (fenceNotify == FENCE_NOTIFY_DEL){
        for (int i=0;i<s->flen;i++){
            fence *f = s->fences[i];
            if (f->allfields || 
                stringmatchlen(f->pattern,sdslen(f->pattern),(const char*)field,sdslen(field),0)
            ) {
                if (!imsg) imsg = newinoutmsg("outside:", field);
                pubsubPublishMessage(f->channel, imsg);
            }
        }
    } else {
        for (int i=0;i<s->flen;i++){
            fence *f = s->fences[i];
            if (f->allfields || 
                stringmatchlen(f->pattern,sdslen(f->pattern),(const char*)field,sdslen(field),0)
            ) {
                if (matchSearch(g, f->m, f->targetType, f->searchType, f->center, f->meters)){
                    if (!imsg) imsg = newinoutmsg("inside:", field);
                    pubsubPublishMessage(f->channel, imsg);
                } else {
                    if (!omsg) omsg = newinoutmsg("inside:", field);
                    pubsubPublishMessage(f->channel, omsg);
                }
            }
        }
    }    
    if (imsg) decrRefCount(imsg);
    if (omsg) decrRefCount(omsg);
}

// notify is used to broadcast fence notifications
int spatialTypeDelete(robj *o, sds field, geomRect *rin, int notify) {
    geomRect r;
    sds sidx;
    char *idx;
    spatial *s;
    int res;

    s = (spatial*)(o->ptr);

    // get the idx
    sidx = hashTypeGetNewSds(s->keyhash, field);
    if (!sidx || sdslen(sidx) != 8) return 0;
    idx = (char*)(*((uint64_t*)sidx));
    sdsfree(sidx);


    geom g = NULL;
    if (rin) {
        r = *rin;
    } else {
        void *raw = hashTypeGetRaw(s->h, field);
        if (!raw) return 0;
        g = (geom)raw;
        r = geomBounds(g);
    }
    res = hashTypeDelete(s->h, field);
    rtreeRemove(s->tr, r.min.x, r.min.y, r.max.x, r.max.y, idx);

    if (notify){
        processFences(s, field, g, FENCE_NOTIFY_DEL);
    }
    return res;
}

int spatialTypeSet(robj *o, sds field, sds val, int notify){

    int updated;
    geom g;
    geomRect r;
    sds sidx;
    uint64_t nidx;
    spatial *s;

    s = (spatial*)(o->ptr);

    g = (geom)val;
    r = geomBounds(g);
    updated = spatialTypeDelete(o, field, &r, 0);

    // create a new idx/field entry
    s->idx++;
    nidx = (uint64_t)s->idx;
    sidx = sdsnewlen(&nidx,8);
    hashTypeSet(s->idxhash,sidx,field,0);
    hashTypeSet(s->keyhash,field,sidx,0);
    sdsfree(sidx);
    
    // update the underlying hash
    hashTypeSet(s->h,field,val,0);

    // update the rtree
    rtreeInsert(s->tr, r.min.x, r.min.y, r.max.x, r.max.y, s->idx);

    if (notify){
        processFences(s, field, g, FENCE_NOTIFY_SET);
    }

    return updated;
}

unsigned long spatialTypeLength(robj *o){
    return hashTypeLength(((spatial*)(o->ptr))->h);
}

size_t spatialTypeGetValueLength(robj *o, sds field){
    return hashTypeGetValueLength(((spatial*)(o->ptr))->h, field);   
}

int spatialTypeExists(robj *o, sds field){
    return hashTypeExists(((spatial*)(o->ptr))->h, field);
}

robj *spatialTypeLookupWriteOrCreate(client *c, robj *key) {
    robj *o = lookupKeyWrite(c->db,key);
    if (o == NULL) {
        o = createSpatialObject();
        dbAdd(c->db,key,o);
    } else {
        if (o->type != OBJ_SPATIAL) {
            addReply(c,shared.wrongtypeerr);
            return NULL;
        }
    }
    return o;
}

int subscribeSearchContextFence(client *c, sds key, searchContext *ctx){
    char rchan[19];
    strcpy(rchan, "fence$");
    getRandomHexChars(rchan+6, 18-6);
    sds keych = sdscatfmt(sdsnewlen(rchan, 18), "$%S", key);
    robj *channel = createStringObject(keych, sdslen(keych));
    sdsfree(keych);
    
    if (ctx->s->flen==ctx->s->fcap){
        int ncap = ctx->s->fcap==0?1:ctx->s->fcap*2;
        fence **nfences = zrealloc(ctx->s->fences, ncap*sizeof(fence*));
        ctx->s->fences = nfences;
        ctx->s->fcap = ncap;
    }
    // copy stuff from context.
    fence *f = zmalloc(sizeof(fence));
    memset(f, 0, sizeof(fence));
    if (ctx->pattern){
        f->pattern = sdsdup(ctx->pattern);
    }
    f->allfields = ctx->allfields;
    f->channel = channel;
    if (ctx->g){
        f->g = zmalloc(ctx->sz);
        memcpy(f->g, ctx->g, ctx->sz);
        f->sz = ctx->sz;
        f->m = geomNewPolyMap(f->g);
        if (!f->m){
            decrRefCount(f->channel);
            zfree(f->g);
            if (f->pattern) sdsfree(f->pattern);
            zfree(f);
            return 0;
        }
    }
    ctx->s->fences[ctx->s->flen] = f;
    ctx->s->flen++;
    if (c->spatial_fence){
        spatialReleaseAllFences(c);
    }
    c->spatial_fence = sdsdup(keych);

    pubsubSubscribeChannel(c,f->channel);
    
    c->flags |= CLIENT_PUBSUB;
    return 1;
}



/* Importing some stuff from t_hash.c but these should exist in server.h */
static void addGeomReplyBulkCBuffer(client *c, const void *p, size_t len) {
    len = len-0; // noop for now.
    char *wkt = geomEncodeWKT((geom)p, 0);
    if (!wkt){
        addReplyError(c, "failed to encode wkt");
        return;
    }
    addReplyBulkCBuffer(c, wkt, strlen(wkt));
    geomFreeWKT(wkt);
}


#define OBJ_ENCODING_EMBSTR_SIZE_LIMIT 44

/* These are direct copies from t_hash.c because they're defined as static and 
 * I didn't want to change the source file. */
static void addGeomHashFieldToReply(client *c, robj *o, sds field) {
    if (o == NULL) {
        addReply(c, shared.nullbulk);
        return;
    }
    unsigned char *vstr = NULL;
    unsigned int vlen = UINT_MAX;
    long long vll = LLONG_MAX;
    if (hashTypeGetValue(((spatial*)(o->ptr))->h, field, &vstr, &vlen, &vll) == C_ERR){
        addReply(c, shared.nullbulk);
        return;
    }
    if (!vstr){
        addReply(c, shared.nullbulk);
        return;
    }
    addGeomReplyBulkCBuffer(c, vstr, vlen);
}

static void addHashIteratorCursorToReply(client *c, hashTypeIterator *hi, int what) {
    unsigned char *vstr = NULL;
    unsigned int vlen = UINT_MAX;
    long long vll = LLONG_MAX;
    hashTypeIteratorValue(hi, what, &vstr, &vlen, &vll);
    if (what == OBJ_HASH_VALUE){
        addGeomReplyBulkCBuffer(c, vstr, vlen);
    }else{
        addReplyBulkCBuffer(c, vstr, vlen);
    }
}

static void scanGeomCallback(void *privdata, const dictEntry *de) {
    void **pd = (void**) privdata;
    list *keys = pd[0];
    robj *o = pd[1];
    robj *key, *val = NULL;

    if (o == NULL) {
        sds sdskey = dictGetKey(de);
        key = createStringObject(sdskey, sdslen(sdskey));
    } else if (o->type == OBJ_SET) {
        sds keysds = dictGetKey(de);
        key = createStringObject(keysds,sdslen(keysds));
    } else if (o->type == OBJ_HASH) {
        sds sdskey = dictGetKey(de);
        sds sdsval = dictGetVal(de);
        key = createStringObject(sdskey,sdslen(sdskey));
        val = createStringObject(sdsval,sdslen(sdsval));
    } else if (o->type == OBJ_ZSET) {
        sds sdskey = dictGetKey(de);
        key = createStringObject(sdskey,sdslen(sdskey));
        val = createStringObjectFromLongDouble(*(double*)dictGetVal(de),0);
    } else {
        serverPanic("Type not handled in SCAN callback.");
    }

    listAddNodeTail(keys, key);
    if (val) listAddNodeTail(keys, val);
}


static void scanGeomCommand(client *c, robj *o, unsigned long cursor) {
    int i, j;
    list *keys = listCreate();
    listNode *node, *nextnode;
    long count = 10;
    sds pat = NULL;
    int patlen = 0, use_pattern = 0;
    dict *ht;

    /* Object must be NULL (to iterate keys names), or the type of the object
     * must be Set, Sorted Set, or Hash. */
    serverAssert(o == NULL || o->type == OBJ_SET || o->type == OBJ_HASH ||
                o->type == OBJ_ZSET);

    /* Set i to the first option argument. The previous one is the cursor. */
    i = (o == NULL) ? 2 : 3; /* Skip the key argument if needed. */

    /* Step 1: Parse options. */
    while (i < c->argc) {
        j = c->argc - i;
        if (!strcasecmp(c->argv[i]->ptr, "count") && j >= 2) {
            if (getLongFromObjectOrReply(c, c->argv[i+1], &count, NULL)
                != C_OK)
            {
                goto cleanup;
            }

            if (count < 1) {
                addReply(c,shared.syntaxerr);
                goto cleanup;
            }

            i += 2;
        } else if (!strcasecmp(c->argv[i]->ptr, "match") && j >= 2) {
            pat = c->argv[i+1]->ptr;
            patlen = sdslen(pat);

            /* The pattern always matches if it is exactly "*", so it is
             * equivalent to disabling it. */
            use_pattern = !(pat[0] == '*' && patlen == 1);

            i += 2;
        } else {
            addReply(c,shared.syntaxerr);
            goto cleanup;
        }
    }

    /* Step 2: Iterate the collection.
     *
     * Note that if the object is encoded with a ziplist, intset, or any other
     * representation that is not a hash table, we are sure that it is also
     * composed of a small number of elements. So to avoid taking state we
     * just return everything inside the object in a single call, setting the
     * cursor to zero to signal the end of the iteration. */

    /* Handle the case of a hash table. */
    ht = NULL;
    if (o == NULL) {
        ht = c->db->dict;
    } else if (o->type == OBJ_SET && o->encoding == OBJ_ENCODING_HT) {
        ht = o->ptr;
    } else if (o->type == OBJ_HASH && o->encoding == OBJ_ENCODING_HT) {
        ht = o->ptr;
        count *= 2; /* We return key / value for this type. */
    } else if (o->type == OBJ_ZSET && o->encoding == OBJ_ENCODING_SKIPLIST) {
        zset *zs = o->ptr;
        ht = zs->dict;
        count *= 2; /* We return key / value for this type. */
    }

    if (ht) {
        void *privdata[2];
        /* We set the max number of iterations to ten times the specified
         * COUNT, so if the hash table is in a pathological state (very
         * sparsely populated) we avoid to block too much time at the cost
         * of returning no or very few elements. */
        long maxiterations = count*10;

        /* We pass two pointers to the callback: the list to which it will
         * add new elements, and the object containing the dictionary so that
         * it is possible to fetch more data in a type-dependent way. */
        privdata[0] = keys;
        privdata[1] = o;
        do {
            cursor = dictScan(ht, cursor, scanGeomCallback, privdata);
        } while (cursor &&
              maxiterations-- &&
              listLength(keys) < (unsigned long)count);
    } else if (o->type == OBJ_SET) {
        int pos = 0;
        int64_t ll;

        while(intsetGet(o->ptr,pos++,&ll))
            listAddNodeTail(keys,createStringObjectFromLongLong(ll));
        cursor = 0;
    } else if (o->type == OBJ_HASH || o->type == OBJ_ZSET) {
        unsigned char *p = ziplistIndex(o->ptr,0);
        unsigned char *vstr;
        unsigned int vlen;
        long long vll;

        while(p) {

            ziplistGet(p,&vstr,&vlen,&vll);
            listAddNodeTail(keys,
                (vstr != NULL) ? createStringObject((char*)vstr,vlen) :
                                 createStringObjectFromLongLong(vll));
            p = ziplistNext(o->ptr,p);
        }
        cursor = 0;
    } else {
        serverPanic("Not handled encoding in SCAN.");
    }

    /* Step 3: Filter elements. */
    node = listFirst(keys);
    while (node) {
        robj *kobj = listNodeValue(node);
        nextnode = listNextNode(node);
        int filter = 0;

        /* Filter element if it does not match the pattern. */
        if (!filter && use_pattern) {
            if (sdsEncodedObject(kobj)) {
                if (!stringmatchlen(pat, patlen, kobj->ptr, sdslen(kobj->ptr), 0))
                    filter = 1;
            } else {
                char buf[LONG_STR_SIZE];
                int len;

                serverAssert(kobj->encoding == OBJ_ENCODING_INT);
                len = ll2string(buf,sizeof(buf),(long)kobj->ptr);
                if (!stringmatchlen(pat, patlen, buf, len, 0)) filter = 1;
            }
        }

        /* Filter element if it is an expired key. */
        if (!filter && o == NULL && expireIfNeeded(c->db, kobj)) filter = 1;

        /* Remove the element and its associted value if needed. */
        if (filter) {
            decrRefCount(kobj);
            listDelNode(keys, node);
        }

        /* If this is a hash or a sorted set, we have a flat list of
         * key-value elements, so if this element was filtered, remove the
         * value, or skip it if it was not filtered: we only match keys. */
        if (o && (o->type == OBJ_ZSET || o->type == OBJ_HASH)) {
            node = nextnode;
            nextnode = listNextNode(node);
            if (filter) {
                kobj = listNodeValue(node);
                decrRefCount(kobj);
                listDelNode(keys, node);
            }
        }
        node = nextnode;
    }

    /* Step 4: Reply to the client. */
    addReplyMultiBulkLen(c, 2);
    addReplyBulkLongLong(c,cursor);

    addReplyMultiBulkLen(c, listLength(keys));
    int idx = 0;
    while ((node = listFirst(keys)) != NULL) {
        robj *kobj = listNodeValue(node);
        if (idx%2==0){
            addReplyBulk(c, kobj);
        } else {
            addGeomReplyBulkCBuffer(c, kobj->ptr, sdslen(kobj->ptr));
        }
        decrRefCount(kobj);
        listDelNode(keys, node);
        idx++;
    }

cleanup:
    listSetFreeMethod(keys,decrRefCountVoid);
    listRelease(keys);
}

static sds decodeSdsOrReply(client *c, sds value){
    geom g = NULL;
    int sz = 0;
    geomErr err = geomDecode(value,sdslen(value),0,&g,&sz);
    if (err!=GEOM_ERR_NONE){
        addReplyError(c,"invalid geometry");
        return NULL;
    }
    sds gvalue = sdsnewlen(g,sz);
    geomFree(g);
    return gvalue;
}

/* ====================================================================
 * Commands
 * ==================================================================== */

void gsetCommand(client *c) {
    int update;
    robj *o;
    sds value;
    if ((o = spatialTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    if ((value = decodeSdsOrReply(c,c->argv[3]->ptr)) == NULL) return;
    update = spatialTypeSet(o,c->argv[2]->ptr,value, 1);
    sdsfree(value);
    addReply(c, update ? shared.czero : shared.cone);
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"gset",c->argv[1],c->db->id);
    server.dirty++;
}

void ggetCommand(client *c) {
    robj *o;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    addGeomHashFieldToReply(c, o, c->argv[2]->ptr);
}

void gmgetCommand(client *c) {
    robj *o;
    int i;

    o = lookupKeyRead(c->db, c->argv[1]);
    if (o != NULL && o->type != OBJ_SPATIAL) {
        addReply(c, shared.wrongtypeerr);
        return;
    }

    addReplyMultiBulkLen(c, c->argc-2);
    for (i = 2; i < c->argc; i++) {
        addGeomHashFieldToReply(c, o, c->argv[i]->ptr);
    }
}

void gdelCommand(client *c) {
    robj *o;
    int j, deleted = 0, keyremoved = 0;
    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    for (j = 2; j < c->argc; j++) {
        if (spatialTypeDelete(o, c->argv[j]->ptr, NULL, 1)) {
            deleted++;
            if (spatialTypeLength(o) == 0) {
                dbDelete(c->db,c->argv[1]);
                keyremoved = 1;
                break;
            }
        }
    }
    if (deleted) {
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_HASH,"gdel",c->argv[1],c->db->id);
        if (keyremoved)
            notifyKeyspaceEvent(NOTIFY_GENERIC,"del",c->argv[1],
                                c->db->id);
        server.dirty += deleted;
    }
    addReplyLongLong(c,deleted);
}

void glenCommand(client *c) {
    robj *o;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    addReplyLongLong(c,spatialTypeLength(o));
}

void gstrlenCommand(client *c) {
    robj *o;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    addReplyLongLong(c,spatialTypeGetValueLength(o,c->argv[2]->ptr));
}

void gsetnxCommand(client *c) {
    robj *o;
    if ((o = spatialTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    if (spatialTypeExists(o, c->argv[2]->ptr)) {
        addReply(c, shared.czero);
    } else {
        geom g = NULL;
        int sz = 0;
        geomErr err = geomDecode(c->argv[3]->ptr, sdslen(c->argv[3]->ptr), 0, &g, &sz);
        if (err!=GEOM_ERR_NONE){
            addReplyError(c,"invalid geometry");
            return;
        }
        sds value = sdsnewlen(g, sz);
        geomFree(g);
        spatialTypeSet(o,c->argv[2]->ptr,value,1);
        sdsfree(value);
        addReply(c, shared.cone);
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_HASH,"gset",c->argv[1],c->db->id);
        server.dirty++;
    }
}

void gmsetCommand(client *c) {
    int i;
    robj *o;
    if ((c->argc % 2) == 1) {
        addReplyError(c,"wrong number of arguments for GMSET");
        return;
    }
    if ((o = spatialTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    for (i = 2; i < c->argc; i += 2) {
        geom g = NULL;
        int sz = 0;
        geomErr err = geomDecode(c->argv[i+1]->ptr, sdslen(c->argv[i+1]->ptr), 0, &g, &sz);
        if (err!=GEOM_ERR_NONE){
            addReplyError(c,"invalid geometry");
            return;
        }
        sds value = sdsnewlen(g, sz);
        geomFree(g);
        spatialTypeSet(o,c->argv[i+0]->ptr,value,1);
        sdsfree(value);
    }
    addReply(c, shared.ok);
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"gset",c->argv[1],c->db->id);
    server.dirty++;
}

void genericGgetallCommand(client *c, int flags) {
    robj *o;
    hashTypeIterator *hi;
    int multiplier = 0;
    int length, count = 0;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk)) == NULL
        || checkType(c,o,OBJ_SPATIAL)) return;

    if (flags & OBJ_HASH_KEY) multiplier++;
    if (flags & OBJ_HASH_VALUE) multiplier++;

    length = spatialTypeLength(o) * multiplier;
    addReplyMultiBulkLen(c, length);

    hi = hashTypeInitIterator(((spatial*)(o->ptr))->h);
    while (hashTypeNext(hi) != C_ERR) {
        if (flags & OBJ_HASH_KEY) {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_KEY);
            count++;
        }
        if (flags & OBJ_HASH_VALUE) {
            addHashIteratorCursorToReply(c, hi, OBJ_HASH_VALUE);
            count++;
        }
    }
    hashTypeReleaseIterator(hi);
    serverAssert(count == length);
}

void gkeysCommand(client *c) {
    genericGgetallCommand(c,OBJ_HASH_KEY);
}

void gvalsCommand(client *c) {
    genericGgetallCommand(c,OBJ_HASH_VALUE);
}

void ggetallCommand(client *c) {
    genericGgetallCommand(c,OBJ_HASH_KEY|OBJ_HASH_VALUE);
}

void gexistsCommand(client *c) {
    robj *o, *h;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);

    addReply(c, hashTypeExists(h,c->argv[2]->ptr) ? shared.cone : shared.czero);
}

void gscanCommand(client *c) {
    robj *o;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == C_ERR) return;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;

    scanGeomCommand(c,((spatial*)(o->ptr))->h,cursor);
}

static int strieq(const char *str1, const char *str2){
    for (int i=0;;i++){
        if (tolower(str1[i]) != tolower(str2[i])){
            return 0;
        }
        if (!str1[i]){
            break;
        }
    }
    return 1;
}





static int searchIterator(double minX, double minY, double maxX, double maxY, void *item, void *userdata){
    (void)(minX);(void)(minY);(void)(maxX);(void)(maxY); // unused vars.

    searchContext *ctx = userdata;

    // retreive the field
    uint64_t nidx = (uint64_t)item;
    sds sidx = sdsnewlen(&nidx, 8);
    unsigned char *vstr = NULL;
    unsigned int vlen = UINT_MAX;
    long long vll = LLONG_MAX;
    int res = hashTypeGetValue(ctx->s->idxhash, sidx, &vstr, &vlen, &vll);
    sdsfree(sidx);
    if (res == C_ERR){
        return 1;
    }
    if (!(ctx->allfields || 
        stringmatchlen(ctx->pattern,sdslen(ctx->pattern),(const char*)vstr,vlen,0))) {
        return 1;
    }
    char *field = (char*)vstr;
    int fieldLen = vlen;
    sds sfield = sdsnewlen(field, fieldLen);


    // retreive the geom
    res = hashTypeGetValue(ctx->s->h, sfield, &vstr, &vlen, &vll);
    sdsfree(sfield);
    if (res == C_ERR){
        return 1;
    }
    char *value = (char*)vstr;
    int valueLen = vlen;

    geom g = (geom)value;

    int match = matchSearch(g, ctx->m, ctx->targetType, ctx->searchType, ctx->center, ctx->meters);
    if (!match){
        return 1;
    }
    // append item
    if (ctx->len == ctx->cap){
        int ncap = ctx->cap;
        if (ncap == 0){
            ncap = 1;
        } else {
            ncap *= 2;
        }
        resultItem *nresults = zrealloc(ctx->results, ncap*sizeof(resultItem));
        if (!nresults){
            addReplyError(ctx->c, "out of memory");
            ctx->fail = 1;
            return 0;
        }
        ctx->results = nresults;
        ctx->cap = ncap;
    }
    ctx->results[ctx->len].field = field;    
    ctx->results[ctx->len].fieldLen = fieldLen;
    ctx->results[ctx->len].value = value;  
    ctx->results[ctx->len].valueLen = valueLen;  
    ctx->len++;
    return 1;
}

static void addInvalidSearchReplyError(client *c){
    addReplyError(c, "invalid arguments for 'gsearch' command"); \
}

#define CHECKON(which) \
    if ((which)){ \
        addInvalidSearchReplyError(c); \
        goto done; \
    } \
    (which) = 1;

// GSEARCH key 
//   [WITHIN|INTERSECTS] 
//   [CURSOR cursor]
//   [MATCH pattern]
//   [FENCE]
//   [OUTPUT COUNT|FIELD|WKT|WKB|JSON|POINT|BOUNDS|(HASH precision)|(QUAD level)|(TILE z)]
//   (MEMBER key field)|
//      (BOUNDS minlon minlat maxlon maxlat)|
//      (GEOMETRY wkt|wkb|json)|
//      (TILE x y z)|
//      (QUAD key)|
//      (HASH geohash)
//      (RADIUS lon lat meters)
void gsearchCommand(client *c){
    robj *o;
    searchContext ctx;
    memset(&ctx, 0, sizeof(searchContext));
    ctx.c = c;
    ctx.releaseg = 1;
    ctx.searchType = INTERSECTS;
    ctx.cursor = -1;
    ctx.allfields = 1;
    ctx.output = OUTPUT_WKT;
    int i = 2;

    int typeon = 0;
    int cursoron = 0;
    int geomon = 0;
    int matchon = 0;
    int outputon = 0;
    int fenceon = 0;
    
    for (;i<c->argc;){
        /* TYPE */
        if (strieq(c->argv[i]->ptr, "within")){
            CHECKON(typeon);
            ctx.searchType = WITHIN;
            i++;
        } else if (strieq(c->argv[i]->ptr, "intersects")){
            CHECKON(typeon);
            ctx.searchType = INTERSECTS;
            i++;
        } 
        /* MATCH */
        else if (strieq(c->argv[i]->ptr, "match")){
            CHECKON(matchon);
            if (i>=c->argc-1){
                addReplyError(c, "need match pattern");
                goto done;
            }
            ctx.pattern = c->argv[i+1]->ptr;
            ctx.allfields = (ctx.pattern[0] == '*' && ctx.pattern[1] == '\0');
            i+=2;
        }
        /* FENCE */
        else if (strieq(c->argv[i]->ptr, "fence")){
            CHECKON(fenceon);
            // if (i>=c->argc-1){
            //     addReplyError(c, "need fence type");
            //     goto done;
            // }
            // if (!strieq(c->argv[i+1]->ptr, "all")){
            //     addReplyError(c, "fence type must be 'all'");
            //     goto done;
            // }
            ctx.fence = FENCE_ALL;
            i+=1;
        }
        /* OUTPUT */
        else if (strieq(c->argv[i]->ptr, "output")){
            CHECKON(outputon);
            if (i>=c->argc-1){
                addReplyError(c, "need output type (count,field,wkt,wkb,json,point,bounds,hash)");
                goto done;
            }
            if (strieq(c->argv[i+1]->ptr, "count")){
                ctx.output = OUTPUT_COUNT;
            } else if (strieq(c->argv[i+1]->ptr, "field")){
                ctx.output = OUTPUT_FIELD;
            } else if (strieq(c->argv[i+1]->ptr, "wkt")){
                ctx.output = OUTPUT_WKT;
            } else if (strieq(c->argv[i+1]->ptr, "wkb")){
                ctx.output = OUTPUT_WKB;
            } else if (strieq(c->argv[i+1]->ptr, "json")){
                ctx.output = OUTPUT_JSON;
            } else if (strieq(c->argv[i+1]->ptr, "point")){
                ctx.output = OUTPUT_POINT;
            } else if (strieq(c->argv[i+1]->ptr, "bounds")){
                ctx.output = OUTPUT_BOUNDS;
            } else if (strieq(c->argv[i+1]->ptr, "hash")){
                ctx.output = OUTPUT_HASH;
                if (i>=c->argc-2){
                    addReplyError(c, "need hash precision");
                    goto done;
                }
                long precision = 0;
                if (getLongFromObjectOrReply(c, c->argv[i+2], &precision, "need numeric precision") != C_OK) return;
                if (precision < 1 || precision > 22){
                    addReplyError(c, "invalid hash precision");
                    goto done;
                }
                ctx.precision = (int)precision;
                i++;
            } else if (strieq(c->argv[i+1]->ptr, "quad")){
                ctx.output = OUTPUT_QUAD;
                if (i>=c->argc-2){
                    addReplyError(c, "need quad level");
                    goto done;
                }
                long precision = 0;
                if (getLongFromObjectOrReply(c, c->argv[i+2], &precision, "need numeric level") != C_OK) return;
                if (precision < 1 || precision > 22){
                    addReplyError(c, "invalid quad level");
                    goto done;
                }
                ctx.precision = (int)precision;
                i++;
            } else if (strieq(c->argv[i+1]->ptr, "tile")){
                ctx.output = OUTPUT_TILE;
                if (i>=c->argc-2){
                    addReplyError(c, "need tile z");
                    goto done;
                }
                long precision = 0;
                if (getLongFromObjectOrReply(c, c->argv[i+2], &precision, "need numeric z") != C_OK) return;
                if (precision < 1 || precision > 22){
                    addReplyError(c, "invalid tile z");
                    goto done;
                }
                ctx.precision = (int)precision;
                i++;
            } else {
                addInvalidSearchReplyError(c);
                goto done;
            }
            i+=2;
        }
        /* CURSOR */
        else if (strieq(c->argv[i]->ptr, "cursor")){
            CHECKON(cursoron);
            if (i>=c->argc-1){
                addReplyError(c, "need cursor");
                goto done;
            }
            if (getLongLongFromObjectOrReply(c, c->argv[i+1], &ctx.cursor, "need numeric cursor") != C_OK) return;
            if (ctx.cursor < 0){
                addReplyError(c, "invalid cursor");
                goto done;
            }
            i+=2;
        } 
        /* GEOM */
        else if (strieq(c->argv[i]->ptr, "radius")){
            CHECKON(geomon);
            if (i>=c->argc-3){
                addReplyError(c, "need longitude, latitude, meters");
                goto done;
            }
            if (getDoubleFromObjectOrReply(c, c->argv[i+1], &ctx.center.x, "need numeric longitude") != C_OK) return;
            if (getDoubleFromObjectOrReply(c, c->argv[i+2], &ctx.center.y, "need numeric latitude") != C_OK) return;
            if (getDoubleFromObjectOrReply(c, c->argv[i+3], &ctx.meters, "need numeric meters") != C_OK) return;
            if (ctx.center.x < -180 || ctx.center.x > 180 || ctx.center.y < -90 || ctx.center.y > 90){
                addReplyError(c, "invalid longitude/latitude pair");
                goto done;
            }
            ctx.targetType = RADIUS;
            ctx.bounds = geoutilBoundsFromLatLon(ctx.center.y, ctx.center.x, ctx.meters);
            ctx.g = geomNewCirclePolygon(ctx.center, ctx.meters, 12, &ctx.sz);
            i+=4;
        } else if (strieq(c->argv[i]->ptr, "geom") || strieq(c->argv[i]->ptr, "geometry")){
            CHECKON(geomon);
            if (i==c->argc-1){
                addReplyError(c, "need geometry");
                goto done; 
            }
            geom g = NULL;
            int sz = 0;
            geomErr err = geomDecode(c->argv[i+1]->ptr, sdslen(c->argv[i+1]->ptr), 0, &g, &sz);
            if (err!=GEOM_ERR_NONE){
                addReplyError(c, "invalid geometry");
                goto done;
            }
            ctx.g = g;
            ctx.sz = sz;
            ctx.targetType = GEOMETRY;
            ctx.bounds = geomBounds(ctx.g);
            i+=2;
        } else if (strieq(c->argv[i]->ptr, "bounds")){
            CHECKON(geomon);
            if (i>=c->argc-4){
                addReplyError(c, "need min longitude, min latitude, max longitude, max latitude");
                goto done;
            }
            if (getDoubleFromObjectOrReply(c, c->argv[i+1], &ctx.bounds.min.x, "need numeric min longitude") != C_OK) return;
            if (getDoubleFromObjectOrReply(c, c->argv[i+2], &ctx.bounds.min.y, "need numeric min latitude") != C_OK) return;
            if (getDoubleFromObjectOrReply(c, c->argv[i+3], &ctx.bounds.max.x, "need numeric max longitude") != C_OK) return;
            if (getDoubleFromObjectOrReply(c, c->argv[i+4], &ctx.bounds.max.y, "need numeric max latitude") != C_OK) return;
            if (ctx.bounds.min.x < -180 || ctx.bounds.min.x > 180 || ctx.bounds.min.y < -90 || ctx.bounds.min.y > 90 ||
                ctx.bounds.max.x < -180 || ctx.bounds.max.x > 180 || ctx.bounds.max.y < -90 || ctx.bounds.max.y > 90 ||
                ctx.bounds.min.x > ctx.bounds.max.x || ctx.bounds.min.y > ctx.bounds.max.y){
                addReplyError(c, "invalid longitude/latitude pairs");
                goto done;
            }
            ctx.targetType = BOUNDS;
            ctx.g = geomNewRectPolygon(ctx.bounds, &ctx.sz);
            i+=5;
        } else if (strieq(c->argv[i]->ptr, "tile")){
            CHECKON(geomon);
            if (i>=c->argc-3){
                addReplyError(c, "need x,y,z");
                goto done;
            }
            double x,y,z;
            if (getDoubleFromObjectOrReply(c, c->argv[i+1], &x, "need numeric x") != C_OK) return;
            if (getDoubleFromObjectOrReply(c, c->argv[i+2], &y, "need numeric y") != C_OK) return;
            if (getDoubleFromObjectOrReply(c, c->argv[i+3], &z, "need numeric z") != C_OK) return;
            bingTileXYToBounds(x,y,z, &ctx.bounds.min.y, &ctx.bounds.min.x, &ctx.bounds.max.y, &ctx.bounds.max.x);
            ctx.targetType = BOUNDS;
            ctx.g = geomNewRectPolygon(ctx.bounds, &ctx.sz);
            i+=4;
        } else if (strieq(c->argv[i]->ptr, "quad")){
            CHECKON(geomon);
            if (i>=c->argc-1){
                addReplyError(c, "need key");
                goto done;
            }
            if (!bingQuadKeyToBounds(c->argv[i+1]->ptr, &ctx.bounds.min.y, &ctx.bounds.min.x, &ctx.bounds.max.y, &ctx.bounds.max.x)){
                addReplyError(c, "invalid quad key");
                goto done;
            }
            ctx.targetType = BOUNDS;
            ctx.g = geomNewRectPolygon(ctx.bounds, &ctx.sz);
            i+=2;
        } else if (strieq(c->argv[i]->ptr, "hash")){
            CHECKON(geomon);
            if (i>=c->argc-1){
                addReplyError(c, "need hash");
                goto done;
            }
            if (!hashBounds(c->argv[i+1]->ptr, 
                    &ctx.bounds.min.y, &ctx.bounds.min.x, 
                    &ctx.bounds.max.y, &ctx.bounds.max.x)
            ){
                addReplyError(c, "invalid hash");
                goto done;   
            }
            ctx.targetType = BOUNDS;    
            ctx.g = geomNewRectPolygon(ctx.bounds, &ctx.sz);
            i+=2;
        } else if (strieq(c->argv[i]->ptr, "member")){
            CHECKON(geomon);
            if (i>=c->argc-2){
                addReplyError(c, "need member key, field");
                goto done;
            }
            robj *o2 = lookupKeyRead(c->db, c->argv[i+1]);
            if (o2 == NULL){
                addReplyError(c, "member is not available in database");
                goto done;
            }
            if (o2 != NULL && o2->type != OBJ_SPATIAL) {
                addReplyError(c, "member key is holding the wrong kind of value");
                goto done;
            }
            robj *h2 = spatialGetHash(o2);
            sds value = hashTypeGetRaw(h2, c->argv[i+2]->ptr);
            if (value==NULL){
                addReplyError(c, "member is not available in database");
                goto done;
            }
            ctx.releaseg=0;
            ctx.g = (geom)value;
            ctx.sz = sdslen(value);
            ctx.targetType = GEOMETRY;
            ctx.bounds = geomBounds(ctx.g);
            i+=3;
        } else {
            addReplyError(c, "invalid arguments for 'gsearch' command");
            return;
        }
    }

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk)) == NULL || checkType(c,o,OBJ_SPATIAL)) {
        goto done;
    }

    if (ctx.g&&!ctx.fence){
        ctx.m = geomNewPolyMap(ctx.g);
        if (!ctx.m){
            addReplyError(c, "poly map failure");
            goto done;
        }
    }
    ctx.s = o->ptr;
    if (ctx.fence){
        if (!subscribeSearchContextFence(c, c->argv[1]->ptr, &ctx)){
            addReplyError(c, "fence failure");
            goto done;    
        }
        goto done;
    }

    char output[128];
    if (!cursoron||ctx.cursor==0){ // ATM only zero cursor is allowed
       rtreeSearch(ctx.s->tr, ctx.bounds.min.x, ctx.bounds.min.y, ctx.bounds.max.x, ctx.bounds.max.y, searchIterator, &ctx);
    }
    if (!ctx.fail){
        if (ctx.output == OUTPUT_COUNT) {
            addReplyLongLong(c, ctx.len);
        } else {
            addReplyMultiBulkLen(c, 2);
            addReplyBulkLongLong(c, 0); // future cursor support
            if (ctx.output == OUTPUT_FIELD){
                addReplyMultiBulkLen(c, ctx.len);
            } else {
                addReplyMultiBulkLen(c, ctx.len*2);
            }
            for (int i=0;i<ctx.len;i++){
                addReplyBulkCBuffer(c, ctx.results[i].field, ctx.results[i].fieldLen);
                if (ctx.output != OUTPUT_FIELD){
                    switch (ctx.output){
                    default:
                        addReplyBulkCBuffer(c, "", 0);
                        break;
                    case OUTPUT_WKT:{
                        char *wkt = geomEncodeWKT((geom)ctx.results[i].value, 0);
                        if (!wkt){
                            addReplyBulkCBuffer(c, "", 0);
                        } else {
                            addReplyBulkCBuffer(c, wkt, strlen(wkt));
                            geomFreeWKT(wkt);
                        }
                        break;
                    }
                    case OUTPUT_JSON:{
                        char *json = geomEncodeJSON((geom)ctx.results[i].value);
                        if (!json){
                            addReplyBulkCBuffer(c, "", 0);
                        } else {
                            addReplyBulkCBuffer(c, json, strlen(json));
                            geomFreeJSON(json);
                        }
                        break;
                    }
                    case OUTPUT_WKB:
                        addReplyBulkCBuffer(c, ctx.results[i].value, ctx.results[i].valueLen);
                        break;
                    case OUTPUT_POINT:{
                        geomCoord center = geomCenter((geom)ctx.results[i].value);
                        addReplyMultiBulkLen(c, 2);
                        addReplyDouble(c, center.x);
                        addReplyDouble(c, center.y);
                        break;
                    }
                    case OUTPUT_BOUNDS:{
                        geomRect bounds = geomBounds((geom)ctx.results[i].value);
                        addReplyMultiBulkLen(c, 4);
                        addReplyDouble(c, bounds.min.x);
                        addReplyDouble(c, bounds.min.y);
                        addReplyDouble(c, bounds.max.x);
                        addReplyDouble(c, bounds.max.y);
                        break;
                    }
                    case OUTPUT_HASH:{
                        geomCoord center = geomCenter((geom)ctx.results[i].value);
                        hashEncode(center.x, center.y, ctx.precision, output);
                        addReplyBulkCBuffer(c, output, strlen(output));
                        break;
                    }
                    case OUTPUT_QUAD:{
                        geomCoord center = geomCenter((geom)ctx.results[i].value);
                        bingLatLongToQuadKey(center.y, center.x, ctx.precision, output);
                        addReplyBulkCBuffer(c, output, strlen(output));
                        break;
                    }
                    case OUTPUT_TILE:{
                        geomCoord center = geomCenter((geom)ctx.results[i].value);
                        int x, y;
                        bingLatLonToTileXY(center.y, center.x, ctx.precision, &x, &y);
                        addReplyMultiBulkLen(c, 2);
                        addReplyDouble(c, x);
                        addReplyDouble(c, y);
                        break;
                    }

                    }
                }
            }
        }
    }
done:
    if (ctx.g&&ctx.releaseg){
        geomFree(ctx.g);
    }
    if (ctx.m){
        geomFreePolyMap(ctx.m);
    }
    if (ctx.results){
        zfree(ctx.results);
    }
}



