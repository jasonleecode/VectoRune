# Mapviewer

Qt Quick / Qt Location viewer using **MapLibre Native Qt** (`maplibre` geoservices
plugin). The default map is `../data/maps/beijing.mbtiles`, read directly through
MapLibre's `mbtiles://` protocol. No tile server, API key, or network connection is
needed for the default map.

## Build on this Mac

Use CMake (the older qmake build predates the MapLibre integration). Dependencies:
Qt 6.5+ with Quick, Location and Positioning, and QMapLibre with its Location
component and native MBTiles support.

From the repository root:

```sh
cmake -S mapviewer -B mapviewer/build/cmake-local \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DQMapLibre_DIR="$PWD/../maplibre-native-qt/install/lib/cmake/QMapLibre"
cmake --build mapviewer/build/cmake-local --parallel 8
open mapviewer/build/cmake-local/qml_location_mapviewer.app
```

The QMapLibre path above selects this machine's recent local build. The older
Homebrew copy crashed in its Metal renderer during verification. On other
machines, set `QMapLibre_DIR` to a compatible QMapLibre installation. CMake copies
its geoservices plugin into the app bundle; the app still depends on the installed
Qt and QMapLibre frameworks (this is a development build, not a standalone bundle).

## Map data and style

Right-click the map and choose **加载地图…** to select a `.mbtiles` or Garmin `.img` file.
The viewer checks the database before switching, updates the window title, and
moves to the center/zoom stored in the map metadata. Canceling or selecting an
invalid file leaves the current map in place. OpenMapTiles-schema PBF vector maps
and PNG/JPEG/WebP raster MBTiles are supported; other vector schemas require a
matching style. The file picker starts in the current map's directory.

Garmin IMG maps use the bundled `lib/libgarmin` C parser. CMake builds it without
autotools and includes the `mapviewer_garmin_import` helper beside the app binary.
Loading an IMG converts its finest available geometry to a temporary GeoJSON
source in a separate process, then renders it with `resources/garmin-style.json`.
The viewer stays responsive during conversion, retains the current map on import
failure, and terminates imports taking more than three minutes. Temporary data is
removed on exit; source IMG files are opened read-only. The helper must accompany
the app when installing or moving it.

The adapter displays areas, water, roads, trails, railways, boundaries and categorized POI
dots. It uses a generic style rather than Garmin TYP styling. Latin/pinyin road, area and POI names are rendered with bundled offline Noto Sans
glyphs. This particular IMG uses Windows-1252 transliterations, not Chinese text.
Right-click near a POI and choose **附近地点详情** to view available names, category,
street/house number, phone and source address fields within 200 metres. Some
source region records contain opening hours; they are deliberately labeled as
raw fields instead of being presented as administrative regions. Road direction
flags are retained in the exported GeoJSON. Original TYP graphics and
encrypted/unsupported Garmin variants are not implemented.
The tested `Beijing_gmapsupp.img` yielded 284,231 features (50,348 points,
102,636 lines and 131,247 polygons), with bounds approximately
116.08–116.77°E, 39.68–40.18°N. The earlier loaded GPX west of 116.08°E lies
outside this IMG's coverage, though it remains available as an overlay.

The parser is GPLv2; see `lib/libgarmin/COPYING` for its license.
To check the adapter against the supplied Beijing IMG:

```sh
python3 mapviewer/tests/test_garmin_import.py \
  mapviewer/build/cmake-local/garmin/mapviewer_garmin_import \
  data/maps/Beijing_gmapsupp.img
```

Right-click and choose **加载轨迹…** to open a GPX file. Track segments and route
points are drawn as red lines, with a green start and red finish. Loading fits
the view to the complete track and displays its point count and horizontal
distance. Separate segments are not connected across recording gaps. A new GPX
replaces the previous track; changing the base map retains the loaded track.
The picker starts in `data/gpx`. The supplied `17-8月-2025-1044.gpx` contains
1,611 track points and approximately 8.47 km of recorded track.

The default MBTiles path is resolved from the source directory at build time, so
launching through Finder does not depend on the working directory. To use another
OpenMapTiles-schema vector database, pass an absolute or working-directory-relative
path:

```sh
mapviewer/build/cmake-local/qml_location_mapviewer.app/Contents/MacOS/qml_location_mapviewer \
  --mbtiles data/maps/beijing.mbtiles
```

The bundled `resources/beijing-style.json` draws land, parks, water, roads,
railways, boundaries and buildings. It deliberately has no remote glyph or sprite
URLs: **road/place text labels and POI icons are not yet included**. The viewer
starts in central Beijing at zoom 12. The supplied database has zoom levels 0–14;
higher display zoom levels enlarge the available vector detail.

An external style can be supplied with
`--plugin.maplibre.map.styles file:///absolute/path/style.json`. Such a style may
have its own network dependencies. Pan, zoom, rotate, tilt and map markers use
the existing Qt Location interface. Routing and geocoding require separate
services and are unavailable through this MapLibre rendering plugin. Location
permission is requested when the user selects “Follow me” or starts live navigation.

## Offline routing and navigation (development)

After loading a Garmin IMG, right-click to select **设为路线起点** and
**设为路线终点**, then choose **规划路线** or press **规划** in the panel.
The separate routing helper builds a directed graph from NET-associated road
geometry and Garmin junction flags. It supports car, bicycle and walking modes,
shortest/estimated-fastest routes, and one-way streets. Internal shape crossings
do not connect unless marked as junctions. The Beijing fixture exports 92,479
road polylines; a central Beijing test route measured approximately 4.33 km.

The blue route includes distance, estimated time and turn instructions.
**模拟导航** exercises progress and arrival without location permission.
**开始导航** requests system location permission and uses position updates for
progress, turn prompts and automatic rerouting when off route. A desktop Mac
may not provide continuous or sufficiently accurate positions for driving.
The panel reports the distance from each selected point to its matched road;
the route ends on that road, which can differ from the selected destination.

NOD Table A supplies vehicle access, one-way, carpool and no-through-road flags.
Car, bicycle and foot modes use those flags; ordinary car mode excludes carpool
roads. No-through roads permit access to the origin/destination, but not shortcuts.
Table C supplies forbidden path sequences and vehicle exceptions, including
multi-road via restrictions. Search state retains the incoming path so that
forbidden turns cannot be lost when two paths meet at the same junction.

The Beijing IMG contains 602 restriction records (421 unique), all in the known
static format, and decoded access rules for all 92,479 exported road polylines.
Three real restricted turns were checked against calculations with restrictions
removed: routes changed from 313.0 to 605.5 m, 139.6 to 339.8 m, and 216.3 to
607.8 m. Duplicate shape records cannot bypass the restriction matcher.

**Conditional/time limitation:** this file has no recognized encoded time
conditions. Arbitrary Garmin time/conditional formats are not decoded; unknown
restriction headers, conditional payloads, extension sections or damaged rule
data cause routing to fail explicitly, rather than being ignored. This does not
reconstruct time restrictions absent from the source map. No clock-based Garmin
schedule support is claimed. Actual speed limits are also not decoded; travel
times continue to use road-type estimates. This remains a development navigator.
MapLibre renders these results; it does not supply the routing engine.

To independently check real forbidden movements after importing the IMG:

```sh
python3 mapviewer/tests/test_garmin_routing.py \
  mapviewer/build/cmake-local/garmin/mapviewer_garmin_route /tmp/imported-map/routing.json
```

Format implementation references are the primary mkgmap writers:
[TableA.java](https://github.com/openstreetmap/mkgmap/blob/master/src/uk/me/parabola/imgfmt/app/net/TableA.java),
[RouteRestriction.java](https://github.com/openstreetmap/mkgmap/blob/master/src/uk/me/parabola/imgfmt/app/net/RouteRestriction.java),
[RouteCenter.java](https://github.com/openstreetmap/mkgmap/blob/master/src/uk/me/parabola/imgfmt/app/net/RouteCenter.java).

Map data © OpenStreetMap contributors.

## Interaction regression tests

The local `map/InteractiveMapView.qml` handles wheel and touchpad zoom around the
event position, clamps zoom limits, and rejects invalid geographic anchors.
Double-click zoom uses the same anchor alignment rather than subtracting latitude
and longitude differences. The zoom slider and keyboard shortcuts zoom about the
map center.

```sh
cmake -S mapviewer -B mapviewer/build/cmake-local -DMAPVIEWER_BUILD_TESTS=ON
cmake --build mapviewer/build/cmake-local --parallel 8
ctest --test-dir mapviewer/build/cmake-local --output-on-failure
```

These GUI tests need a desktop session and the same installed QMapLibre frameworks
as the application. They check mouse anchor drift, tilted/rotated zoom, repeated
touchpad pixel scrolling and the minimum zoom boundary using the real MapLibre
plugin with a local empty style.

## Garmin dataset inspection

`garmin/mapviewer_garmin_inspect` (in the build directory) inventories IMG sections,
label encodings, object types, names, POI fields and road flags. The actual Beijing
report is saved in `doc/beijing-garmin-analysis.json`. It counts records, not
unique real-world entities. The two map sections each include NET/NOD route data;
that alone does not establish working navigation. The IMG also contains a TYP
section and MDR/SRT search/sort data; no DEM section was found.

Parser fixes made during inspection cover unaligned little-endian reads and
pointer-offset precedence; hybrid-endian complex house/phone label pointers;
bounded base-11 decoding; POI index widths derived from header counts; unsigned
city point indexes; and null-safe search codepage access. The phone/house-number
format was checked against the primary mkgmap writer:
https://github.com/openstreetmap/mkgmap/blob/master/src/uk/me/parabola/imgfmt/app/lbl/POIRecord.java

```sh
mapviewer/build/cmake-local/garmin/mapviewer_garmin_inspect \
  data/maps/Beijing_gmapsupp.img /tmp/beijing-inspection.json
mapviewer/build/cmake-local/qml_location_mapviewer.app/Contents/MacOS/qml_location_mapviewer \
  --map data/maps/Beijing_gmapsupp.img
```
