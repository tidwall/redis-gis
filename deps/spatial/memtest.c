#include <string.h>
#include <assert.h>
#include "geom.h"

int main(){

	char *input = 
        "GEOMETRYCOLLECTION ("
            "POINT Z(10 11 12),"
            "MULTIPOINT(10 11, 12 13, 14 15),"
            "POLYGON((101 111, 121 131, 141 151),(9 8, 12 13)),"
            "POINTZM(10 11 12 13),"
            "POLYGON((10 11, 12 13, 14 15),(9 8, 12 13)),"
            "GEOMETRYCOLLECTION ("
                "MULTIPOINT(10 11, 12 13, 14 15),"
                "POLYGON((101 111, 121 131, 141 151),(9 8, 12 13))"
            "),"
            "POINTZ(10 11 12),"
            "LINESTRINGZ(10 11 9,12 13 8,14 15 7),"
            "LINESTRING ZM(10 11 9 100,12 13 8 101,14 15 7 102),"
            "POINT ZM(10 11 12 13)"
        ")";
    geom g;
    int sz;
    geomErr err = geomDecode(input, strlen(input), 0, &g, &sz);
    assert(err == GEOM_ERR_NONE);
    for (int i=0;i<2;i++){
        int count = 0;
        
        geomIterator *itr = geomNewGeometryCollectionIterator(g, i);
        assert(itr);
        while (geomIteratorNext(itr)){
            geom ig;
            int sz;
            assert(geomIteratorValues(itr, &ig, &sz));
            if (geomGetType(ig) == GEOM_GEOMETRYCOLLECTION){
                geomIterator *itr2 = geomNewGeometryCollectionIterator(ig, 0);
                assert(itr2);
                while (geomIteratorNext(itr2)){
                    geom ig2;
                    int sz2;
                    assert(geomIteratorValues(itr2, &ig2, &sz2));
                    count++;
                }
                geomFreeIterator(itr2);        
            }
            count++;
        }
        geomFreeIterator(itr);
        if (i==0){
            assert(count==12);
        }else{
            assert(count==11);
        }
    }
    int count;
    geom *arr = geomGeometryCollectionFlattenedArray(g, &count);
    assert(arr);
    assert(count==11);
    geomFreeFlattenedArray(arr);
    geomFree(g);


	return 0;
}