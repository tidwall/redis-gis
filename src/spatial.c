#include "server.h"
#include "spatial.h"
#include "wkt.h"

/* Importing some stuff from t_hash.c but these should exist in server.h */
#define HASH_SET_TAKE_FIELD (1<<0)
#define HASH_SET_TAKE_VALUE (1<<1)
#define HASH_SET_COPY 0
int hashTypeSet(robj *o, sds field, sds value, int flags);
void hashTypeTryConversion(robj *o, robj **argv, int start, int end);
int hashTypeGetFromZiplist(robj *o, sds field, unsigned char **vstr, unsigned int *vlen, long long *vll);
sds hashTypeGetFromHashTable(robj *o, sds field);
size_t hashTypeGetValueLength(robj *o, sds field);

/* These are direct copies from t_hash.c because they're defined as static and 
 * I didn't want to change the source file. */
static void addHashFieldToReply(client *c, robj *o, sds field) {
    int ret;

    if (o == NULL) {
        addReply(c, shared.nullbulk);
        return;
    }

    if (o->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        ret = hashTypeGetFromZiplist(o, field, &vstr, &vlen, &vll);
        if (ret < 0) {
            addReply(c, shared.nullbulk);
        } else {
            if (vstr) {
                addReplyBulkCBuffer(c, vstr, vlen);
            } else {
                addReplyBulkLongLong(c, vll);
            }
        }

    } else if (o->encoding == OBJ_ENCODING_HT) {
        sds value = hashTypeGetFromHashTable(o, field);
        if (value == NULL)
            addReply(c, shared.nullbulk);
        else
            addReplyBulkCBuffer(c, value, sdslen(value));
    } else {
        serverPanic("Unknown hash encoding");
    }
}

static void addHashIteratorCursorToReply(client *c, hashTypeIterator *hi, int what) {
    if (hi->encoding == OBJ_ENCODING_ZIPLIST) {
        unsigned char *vstr = NULL;
        unsigned int vlen = UINT_MAX;
        long long vll = LLONG_MAX;

        hashTypeCurrentFromZiplist(hi, what, &vstr, &vlen, &vll);
        if (vstr)
            addReplyBulkCBuffer(c, vstr, vlen);
        else
            addReplyBulkLongLong(c, vll);
    } else if (hi->encoding == OBJ_ENCODING_HT) {
        sds value = hashTypeCurrentFromHashTable(hi, what);
        addReplyBulkCBuffer(c, value, sdslen(value));
    } else {
        serverPanic("Unknown hash encoding");
    }
}


struct spatial {
    robj *h;
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
        return NULL;
    }
    s->h = createHashObject();
    if (!s->h){
        spatialFree(s);
        return NULL;
    }
    return s;
}

void *robjSpatialNewHash(void *o){
    robj *so = createSpatialObject();
    spatial *s = so->ptr;
    freeHashObject(s->h);
    s->h = o;
    return so;
}

void spatialFree(spatial *s){
    if (s){
        if (s->h){
            freeHashObject(s->h);
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
    
    wktGeometry *geom;
    wktErr err = wktParse(c->argv[3]->ptr, &geom);
    if (err!=WKT_ERRNONE){
        addReplyError(c,"invalid geometry");
        return;        
    }
    decrRefCount(c->argv[3]);
    char *text = wktText(geom);
    if (!text){
        wktFree(geom);
        addReplyError(c,"invalid geometry");
        return;    
    }
    c->argv[3] = createRawStringObject(text, strlen(text));
    wktFreeText(text);
    wktFree(geom);


    if ((o = spatialTypeLookupWriteOrCreate(c,c->argv[1])) == NULL) return;

    h = spatialGetHash(o);
    hashTypeTryConversion(h,c->argv,2,3);
    update = hashTypeSet(h,c->argv[2]->ptr,c->argv[3]->ptr,HASH_SET_COPY);
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

    addHashFieldToReply(c, h, c->argv[2]->ptr);
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
        addHashFieldToReply(c, h, c->argv[i]->ptr);
    }
}

void gdelCommand(client *c) {
    robj *o, *h;
    int j, deleted = 0, keyremoved = 0;

    if ((o = lookupKeyWriteOrReply(c,c->argv[1],shared.czero)) == NULL ||
        checkType(c,o,OBJ_SPATIAL)) return;
    h = spatialGetHash(o);

    for (j = 2; j < c->argc; j++) {
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
    h = spatialGetHash(o);
    hashTypeTryConversion(h,c->argv,2,3);

    if (hashTypeExists(h, c->argv[2]->ptr)) {
        addReply(c, shared.czero);
    } else {
        hashTypeSet(h,c->argv[2]->ptr,c->argv[3]->ptr,HASH_SET_COPY);
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
    h = spatialGetHash(o);
    hashTypeTryConversion(h,c->argv,2,c->argc-1);
    for (i = 2; i < c->argc; i += 2) {
        hashTypeSet(h,c->argv[i]->ptr,c->argv[i+1]->ptr,HASH_SET_COPY);
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
    scanGenericCommand(c,h,cursor);
}
