Redis-GIS
---------

Geospatial and realtime geofencing commands for [Redis](https://github.com/tidwall/redis) based on the [Tile38](https://github.com/tidwall/tile38) project.

*Please note that this is an experimental project and is not ready for production use.*


## Features

- Spatial index with a new [search command](#search-command) supporting Within and Intersect searches.
- Geometry types including Points, Polygons, LineStrings, [Geohash](https://en.wikipedia.org/wiki/Geohash), [QuadKey](https://msdn.microsoft.com/en-us/library/bb259689.aspx), and [XYZ Tiles](https://en.wikipedia.org/wiki/Tiled_web_map).
- Support for [GeoJSON](http://geojson.org/) and [Well-known text](https://en.wikipedia.org/wiki/Well-known_text).
- Realtime [geofencing](#geofencing) through persistent sockets.
- Works with standard Redis [Replication](http://redis.io/topics/replication) and [Cluster](http://redis.io/topics/cluster-tutorial) modes.

<h4>Search Within, Intersecting, and Nearby Radius</h4>
<img src="http://tile38.com/assets/img/search-within.png" width="175" height="175" border="0" alt="Search Within">
<img src="http://tile38.com/assets/img/search-intersects.png" width="175" height="175" border="0" alt="Search Intersects">
<img src="http://tile38.com/assets/img/search-nearby.png" width="175" height="175" border="0" alt="Search Radius">
<br>

#### Basic example

```
# add a couple of points named 'truck1' and 'truck2' to a collection named 'fleet'.
> gset fleet truck1 "POINT(-112.2693 33.5123)"  # on the Loop 101 in Phoenix
> gset fleet truck2 "POINT(-112.1695 33.4626)"  # on the I-10 in Phoenix

# search the 'fleet' collection.
> gsearch fleet radius -112.268 33.462 6000     # search 6 kilometers around a point. returns one truck.

# key value operations
> gget fleet truck1                             # returns 'truck1'
> gdel fleet truck2                             # deletes 'truck2'
```

#### Basic commands

All GIS commands have the 'G' prefix and work just like the hash commands including:  
GSET, GSETNX, GGET, GMSET, GMGET, GDEL, GLEN, GSTRLEN, GKEYS, GVALS, GGETALL, GEXISTS, GSCAN

#### Search command

The new GSEARCH command can find objects in your key based on specified query parameters. 

```
GSEARCH key 
  [WITHIN|INTERSECTS] 
  [CURSOR cursor]
  [MATCH pattern]
  [FENCE]
  [OUTPUT COUNT|FIELD|WKT|WKB|JSON|POINT|BOUNDS|(HASH precision)|(QUAD level)|(TILE z)]
  (RADIUS lon lat meters)|(GEOMETRY wkt|wkb|json)|(BOUNDS minlon minlat maxlon maxlat)|(HASH geohash)|(QUAD key)|(TILE x y z)|(MEMBER key field)
  
```

**GSEARCH Parameters**
- WITHIN: Only objects that are fully contained within the target object are returned.
- INTERSECTS: All objects that are contained within or overlaps the target object are returned. This is the default option.
- CURSOR: Allows for paging through queries that have huge sets of data. Works similar to the standard Redis [SCAN](http://redis.io/commands/scan) cursor.
- MATCH: Filters the search results which have field names that match the provided patten.
- FENCE: Turns the search into a [Geofence](#geofencing) mode.

**Output Formats**
- OUTPUT COUNT: Returns the number of results.
- OUTPUT FIELD: Only returns the field names. The object values will be omitted.
- OUTPUT WKT: Returns results as [Well-known text](https://en.wikipedia.org/wiki/Well-known_text). This is the default option.
- OUTPUT WKB: Returns results as [Well-known binary](https://en.wikipedia.org/wiki/Well-known_text).
- OUTPUT JSON: Returns results as [GeoJSON](http://geojson.org/).
- OUTPUT POINT: Returns results as a XY (Longitude, Latitude) points.
- OUTPUT BOUNDS: Returns results as a [Minimum bounding rectangle](https://en.wikipedia.org/wiki/Minimum_bounding_rectangle) (MinX, MinY, MaxX, MaxY).
- OUTPUT HASH: Returns results as a [Geohash](https://en.wikipedia.org/wiki/Geohash). A precision between 1 and 22 is required.
- OUTPUT TILE: Returns results as [XYZ Tiles](https://en.wikipedia.org/wiki/Tiled_web_map). The Z level must be between 1 and 22.
- OUTPUT QUAD: Returns results as [QuadKey](https://msdn.microsoft.com/en-us/library/bb259689.aspx). The level must be between 1 and 22.

**Targets**
- RADIUS: Search inside a radius. Also known as a Nearby search. Requires a center longitude,latitude, and meters.
- GEOMETRY: Search inside a geometry. This can be WKT, WKB, or Geojson.
- BOUNDS: Search inside a Bounding rectangle. Requires minlon, minlat, maxlon, maxlat.
- HASH: Search inside a Geohash rectangle. Requires a valid geohash value.
- QUAD: Search inside a QuadKey. Requires a valid quadkey value.
- TILE: Search inside an XYZ Tile
- MEMBER: Search inside an existing object already in the database.



### Geofencing

<img src="http://tile38.com/assets/img/geofence.gif" width="200" height="200" border="0" alt="Geofence animation" align="left">
A [geofence](https://en.wikipedia.org/wiki/Geo-fence) is a virtual boundary that can detect when an object enters or exits the area. This boundary can be a radius, bounding box, or a polygon. Turn any standard search into a geofence monitor by adding the FENCE parameter to the search. 
<br clear="all">

```
# subscribe to a geofence using client 1.
> redis-cli gsearch fleet radius -112.268 33.462 6000 fence
Reading messages... (press Ctrl-C to quit)
1) "subscribe"
2) "fence$eb4f0daa41c4$fleet"
3) (integer) 1

# from a different connection using client 2.
> redis-cli gset fleet truck1 "POINT(-112.2693 33.5123)"
> redis-cli gset fleet truck2 "POINT(-113.9082 33.9022)"

# messages will appear on client 1.
1) "message"
2) "fence$06c8f711dbc1$fleet"
3) "inside:truck1"
1) "message"
2) "fence$06c8f711dbc1$fleet"
3) "outside:truck2"
```

The message format is `{notifiation}:{field}`. A notification can have the value of `inside`, `outside`.

## Technical Details

This project is a fork of the Redis project and it tracks the Unstable branch. The primary bulk of the logic is contained in the `src/spatial.c` file and the `deps/spatial` directory.

Redis-GIS introduces a new Spatial type which joins the standard Hash type with an r-tree structure. The Spatial type is distinct from other types in the database and only 'g' commands can operate on it. All data is stored as WKB binary both in memory and in the RDB format. AOF rewrites will also use `gmset` with binary values.

Full replication and cluster support is available. 

When a Geofence is initiated the server puts the client into PubSub mode and subscribes to a special unique channel that is only available to the single client. All writable data operations on a Spatial type will pass through a list of geofences that match its search parameters. When the client disconnects the Geofence channel is released.

The current Redis codebase is quite good and I'm still learning its APIs, idioms, and patterns. Therefore the new Redis-GIS code may not be doing things the 100% Redis way. In all likelihood it's probably at 25%. But I hope to get it there soon.

## Contact
Josh Baker [@tidwall](http://twitter.com/tidwall)

## License
Redis-GIS source code is available under the BSD [License](/COPYING).

