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

#include "server.h"
#include "spatial.h"
#include "rtree.h"
#include "geom.h"

/* Importing some stuff from t_hash.c but these should exist in server.h */
#define HASH_SET_TAKE_FIELD (1<<0)
#define HASH_SET_TAKE_VALUE (1<<1)
#define HASH_SET_COPY 0
int hashTypeSet(robj *o, sds field, sds value, int flags);
void hashTypeTryConversion(robj *o, robj **argv, int start, int end);
sds hashTypeGetFromHashTable(robj *o, sds field);
size_t hashTypeGetValueLength(robj *o, sds field);


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

static robj *createGeomStringObject(const char *ptr, size_t len) {
    char *wkt = geomEncodeWKT((geom)ptr, 0);
    if (!wkt){
        return createRawStringObject("", 0);
    }
    robj *o;
    len = strlen(wkt);
    if (len <= OBJ_ENCODING_EMBSTR_SIZE_LIMIT){
        o = createEmbeddedStringObject(wkt,len);
    } else {
        o = createRawStringObject(wkt,len);
    }    
    geomFreeWKT(wkt);
    return o;
}


/* These are direct copies from t_hash.c because they're defined as static and 
 * I didn't want to change the source file. */
static void addGeomHashFieldToReply(client *c, robj *o, sds field) {
    if (o == NULL) {
        addReply(c, shared.nullbulk);
        return;
    }
    sds value = hashTypeGetFromHashTable(o, field);
    if (value == NULL)
        addReply(c, shared.nullbulk);
    else
        addGeomReplyBulkCBuffer(c, value, sdslen(value));
}

static void addHashIteratorCursorToReply(client *c, hashTypeIterator *hi, int what) {
    sds value = hashTypeCurrentFromHashTable(hi, what);
    if (what == OBJ_HASH_VALUE){
        addGeomReplyBulkCBuffer(c, value, sdslen(value));
    }else{
        addReplyBulkCBuffer(c, value, sdslen(value));
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
        val = createGeomStringObject(sdsval,sdslen(sdsval));
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
    ht = o->ptr;
    count *= 2; /* We return key / value for this type. */

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
    while ((node = listFirst(keys)) != NULL) {
        robj *kobj = listNodeValue(node);
        addReplyBulk(c, kobj);
        decrRefCount(kobj);
        listDelNode(keys, node);
    }

cleanup:
    listSetFreeMethod(keys,decrRefCountVoid);
    listRelease(keys);
}




struct spatial {
    robj *h;
    rtree *tr;
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
    if (!s->h){
        goto err;
    }
    hashTypeConvert(s->h, OBJ_ENCODING_HT);
    s->tr = rtreeNew();
    if (!s->tr){
        goto err;
    }
    return s;
err:
    spatialFree(s);
    return NULL;
}

static void spatialDelValue(spatial *s, sds key){
    void *value = dictFetchValue(s->h->ptr, key);
    if (!value){
        return;
    }
    printf("del: %s (%x)\n", key, value);
}

static void spatialSetValue(spatial *s, sds key, sds val){
    spatialDelValue(s, key);
    printf("set: %s (%x)\n", key, val);
}


/* robjSpatialNewHash creates a new spatial object with an existing hash.
 * This is is called from rdbLoad(). */
void *robjSpatialNewHash(void *o){
    robj *so = createSpatialObject();
    spatial *s = so->ptr;
    freeHashObject(s->h);
    s->h = o;
    hashTypeConvert(s->h, OBJ_ENCODING_HT);

    /* Iterate through the hash and index all objects. */
    hashTypeIterator *hi = hashTypeInitIterator(s->h);
    while (hashTypeNext(hi) != C_ERR) {
        sds key = hashTypeCurrentFromHashTable(hi, OBJ_HASH_KEY);
        sds val = hashTypeCurrentFromHashTable(hi, OBJ_HASH_VALUE);
        spatialSetValue(s, key, val);
    }
    return so;
}


void spatialFree(spatial *s){
    if (s){
        printf("released\n");
        if (s->h){
            freeHashObject(s->h);
        }
        if (s->tr){
            rtreeFree(s->tr);   
        }
        zfree(s);
    }
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


/* ====================================================================
 * Commands
 * ==================================================================== */

void gsetCommand(client *c) {
    int update;
    robj *o, *h;
    
    geom g = NULL;
    int sz = 0;
    geomErr err = geomDecode(c->argv[3]->ptr, sdslen(c->argv[3]->ptr), 0, &g, &sz);
    if (err!=GEOM_ERR_NONE){
        addReplyError(c,"invalid geometry");
        return;        
    }
    decrRefCount(c->argv[3]);
    c->argv[3] = createRawStringObject((char*)g, sz);
    geomFree(g);
    if ((o = spatialTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    spatial *s = o->ptr;
    h = spatialGetHash(o);
    hashTypeTryConversion(h,c->argv,2,3);
    spatialSetValue(s,c->argv[2]->ptr,c->argv[3]->ptr);
    update = hashTypeSet(h,c->argv[2]->ptr,c->argv[3]->ptr,HASH_SET_TAKE_VALUE);
    c->argv[3] = createRawStringObject("", 0); // assign empty.
    addReply(c, update ? shared.czero : shared.cone);
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"gset",c->argv[1],c->db->id);
    server.dirty++;
}

void ggetCommand(client *c) {
    robj *o, *h;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.nullbulk)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);
    addGeomHashFieldToReply(c, h, c->argv[2]->ptr);
}

void gmgetCommand(client *c) {
    robj *o, *h;
    int i;

    o = lookupKeyRead(c->db, c->argv[1]);
    if (o != NULL && o->type != OBJ_SPATIAL) {
        addReply(c, shared.wrongtypeerr);
        return;
    }
    h = spatialGetHash(o);

    addReplyMultiBulkLen(c, c->argc-2);
    for (i = 2; i < c->argc; i++) {
        addGeomHashFieldToReply(c, h, c->argv[i]->ptr);
    }
}

void gdelCommand(client *c) {
    robj *o, *h;
    int j, deleted = 0, keyremoved = 0;

    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);
    spatial *s = o->ptr;

    for (j = 2; j < c->argc; j++) {
        spatialDelValue(s, c->argv[j]->ptr);
        if (hashTypeDelete(h,c->argv[j]->ptr)) {
            deleted++;
            if (hashTypeLength(h) == 0) {
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
    robj *o, *h;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);
    addReplyLongLong(c,hashTypeLength(h));
}


void gstrlenCommand(client *c) {
    robj *o, *h;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);
    addReplyLongLong(c,hashTypeGetValueLength(h,c->argv[2]->ptr));
}

void gsetnxCommand(client *c) {
    robj *o, *h;
    if ((o = spatialTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    spatial *s = o->ptr;
    h = spatialGetHash(o);
    hashTypeTryConversion(h,c->argv,2,3);

    if (hashTypeExists(h, c->argv[2]->ptr)) {
        addReply(c, shared.czero);
    } else {
        geom g = NULL;
        int sz = 0;
        geomErr err = geomDecode(c->argv[3]->ptr, sdslen(c->argv[3]->ptr), 0, &g, &sz);
        if (err!=GEOM_ERR_NONE){
            addReplyError(c,"invalid geometry");
            return;
        }
        decrRefCount(c->argv[3]);
        c->argv[3] = createRawStringObject((char*)g, sz);
        geomFree(g);
        spatialSetValue(s, c->argv[2]->ptr,c->argv[3]->ptr);
        hashTypeSet(h,c->argv[2]->ptr,c->argv[3]->ptr,HASH_SET_TAKE_VALUE);
        c->argv[3] = createRawStringObject("", 0); // assign empty.
        addReply(c, shared.cone);
        signalModifiedKey(c->db,c->argv[1]);
        notifyKeyspaceEvent(NOTIFY_HASH,"gset",c->argv[1],c->db->id);
        server.dirty++;
    }
}

void gmsetCommand(client *c) {
    int i;
    robj *o, *h;
    if ((c->argc % 2) == 1) {
        addReplyError(c,"wrong number of arguments for GMSET");
        return;
    }
    if ((o = spatialTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;
    spatial *s = o->ptr;
    h = spatialGetHash(o);
    hashTypeTryConversion(h,c->argv,2,c->argc-1);
    for (i = 2; i < c->argc; i += 2) {
        geom g = NULL;
        int sz = 0;
        geomErr err = geomDecode(c->argv[i+1]->ptr, sdslen(c->argv[i+1]->ptr), 0, &g, &sz);
        if (err!=GEOM_ERR_NONE){
            addReplyError(c,"invalid geometry");
            return;
        }
        decrRefCount(c->argv[i+1]);
        c->argv[i+1] = createRawStringObject((char*)g, sz);
        geomFree(g);
        spatialSetValue(s,c->argv[i]->ptr,c->argv[i+1]->ptr);
        hashTypeSet(h,c->argv[i]->ptr,c->argv[i+1]->ptr,HASH_SET_TAKE_VALUE);
        c->argv[i+1] = createRawStringObject("", 0); // assign empty.
    }
    addReply(c, shared.ok);
    signalModifiedKey(c->db,c->argv[1]);
    notifyKeyspaceEvent(NOTIFY_HASH,"gset",c->argv[1],c->db->id);
    server.dirty++;
}

void genericGgetallCommand(client *c, int flags) {
    robj *o, *h;
    hashTypeIterator *hi;
    int multiplier = 0;
    int length, count = 0;

    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptymultibulk)) == NULL
        || checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);
    if (flags & OBJ_HASH_KEY) multiplier++;
    if (flags & OBJ_HASH_VALUE) multiplier++;

    length = hashTypeLength(h) * multiplier;
    addReplyMultiBulkLen(c, length);

    hi = hashTypeInitIterator(h);
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
    robj *o, *h;
    unsigned long cursor;

    if (parseScanCursorOrReply(c,c->argv[2],&cursor) == C_ERR) return;
    if ((o = lookupKeyReadOrReply(c,c->argv[1],shared.emptyscan)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);        
    scanGeomCommand(c,h,cursor);
}
