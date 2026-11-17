# Mapviewer

Qt Quick / Qt Location viewer using **MapLibre Native Qt** (`maplibre` geoservices
plugin). The default map is `data/maps/beijing.mbtiles`, read directly through
MapLibre's `mbtiles://` protocol. No tile server, API key, or network connection is
needed for the default map.

Application-independent components now live in `core/` and `plugins/`; helpers live
in `tools/`. See [implementation status](../docs/refactoring-progress-2026-11-09.md).

## Build on this Mac

Use CMake (the older qmake build predates the MapLibre integration). Dependencies:
Qt 6.5+ with Quick, Location and Positioning, and QMapLibre with its Location
component and native MBTiles support.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DQMapLibre_DIR="$PWD/../maplibre-native-qt/install/lib/cmake/QMapLibre"
cmake --build build --parallel 8
open build/mapviewer/qml_location_mapviewer.app
```

The QMapLibre path above selects this machine's recent local build. The older
Homebrew copy crashed in its Metal renderer during verification. On other
machines, set `QMapLibre_DIR` to a compatible QMapLibre installation. CMake copies
its geoservices plugin into the app bundle; the app still depends on the installed
Qt and QMapLibre frameworks (this is a development build, not a standalone bundle).

## Linux OpenGL rendering

The local QMapLibre 4.0.0 checkout at commit
`b58dfacf7548b0b5ec8a815028cab54d7951039f` needs
[`maplibre-opengl-state.patch`](../patches/maplibre-opengl-state.patch).
Its OpenGL framebuffer binding code clears color/depth/stencil outside the
renderer state cache and assumes a viewport that was never applied. On the
tested NVIDIA desktop this causes black backgrounds while panning and tilting.
The patch leaves clearing to MapLibre's render passes and synchronizes the
viewport/scissor state with the shared Qt Quick context. It does not change the
Metal backend or disable threaded rendering.

Apply once to that dependency checkout, then rebuild and install QMapLibre:

```sh
git -C ../maplibre-native-qt apply "$PWD/patches/maplibre-opengl-state.patch"
cmake --build build/deps/maplibre-build --parallel 8
cmake --install build/deps/maplibre-build
```

These build paths refer to the local Linux dependency build. Stop the viewer
before replacing its shared libraries. Do not apply this version-specific patch
blindly to other QMapLibre revisions.

The `MapRendering::test_panKeepsBackground` GUI regression captures the actual
window while panning and changing tilt, and checks a solid local background for
corrupted pixels. Run it with the same Qt/MapLibre library and plugin paths as
the viewer, with `QSG_RHI_BACKEND=opengl`:

```sh
build/tests/mapviewer_interaction_tests -input tests/ui/tst_rendering.qml
```

## Linux label stability

The local QMapLibre 4.0.0 checkout at commit
`b58dfacf7548b0b5ec8a815028cab54d7951039f` needs
[`maplibre-opengl-labels.patch`](../patches/maplibre-opengl-labels.patch).
Its UBO allocator maps recycled storage with `GL_MAP_UNSYNCHRONIZED_BIT`.
On the tested NVIDIA desktop, repeated pans corrupt label glyphs and positions;
an isolated local label reproduces the failure without map tile downloads.
The patch lets OpenGL synchronize both ordinary writes and compaction writes,
while retaining buffer pooling, compaction and fence-based reclamation. These
maps may wait for GPU work; the Metal backend is unaffected.

Apply once from the dependency checkout root, then rebuild and install
(stop the viewer before installation). These paths refer to the local Linux
dependency build; do not apply this patch blindly to other revisions:

```sh
git -C ../maplibre-native-qt apply "$PWD/patches/maplibre-opengl-labels.patch"
cmake --build build/deps/maplibre-build --parallel 8
cmake --install build/deps/maplibre-build
```

`tests/ui/tst_labels.qml` checks label pixels and their position relative to a
fixed geographic anchor through 600 horizontal pan frames and 120 frames with
rotation and tilt. It uses the bundled offline glyphs. Run with the viewer's
Qt/MapLibre library and plugin paths and `QSG_RHI_BACKEND=opengl`:

```sh
build/tests/mapviewer_interaction_tests -input tests/ui/tst_labels.qml
```

## Linux window resizing

For the same QMapLibre revision, apply
[`maplibre-opengl-resize.patch`](../patches/maplibre-opengl-resize.patch) to fix
maps retaining their original size after maximizing or resizing the window.
The QML view already follows the window size. The OpenGL backend must resize
its existing framebuffer attachments when `Map::resize()` omits an FBO, and
the Qt texture wrapper must follow physical size changes even when OpenGL
reuses a texture ID. The scene graph rectangle also follows the logical size.

Stop the viewer before installing rebuilt libraries:

```sh
git -C ../maplibre-native-qt apply "$PWD/patches/maplibre-opengl-resize.patch"
cmake --build build/deps/maplibre-build --parallel 8
cmake --install build/deps/maplibre-build
```

`tests/ui/tst_resize.qml` grows and shrinks the window, maximizes it and restores
it. It checks rendered pixels at all four corners, the geographic center and
label scale. Run with the same Qt/MapLibre environment as the viewer:

```sh
build/tests/mapviewer_interaction_tests -input tests/ui/tst_resize.qml
QT_SCALE_FACTOR=1.5 build/tests/mapviewer_interaction_tests -input tests/ui/tst_resize.qml
```

## Linux panning performance

Also apply [`maplibre-opengl-ubo-occupancy.patch`](../patches/maplibre-opengl-ubo-occupancy.patch)
after the streaming patch below. The allocator must count the alignment padding
reserved for each uniform block when deciding whether a page is fragmented.
Counting only payload bytes misclassifies pages filled with small blocks, causing
repeated relocation within the same compaction pass and a growing buffer pool.
This is particularly visible with the many text labels in OpenMapTiles MBTiles.
The fix counts the same aligned size on allocation and release.

After the label-synchronization patch above, apply
[`maplibre-opengl-ubo-streaming.patch`](../patches/maplibre-opengl-ubo-streaming.patch)
to the same dependency revision. Synchronously mapping every small uniform
update can stall the CPU on GPU work, especially in a maximized window.
This patch stages updates in each 8 KiB buffer page, then uploads the changed
range with ordered `glBufferSubData` when the page is first bound for drawing.
Compaction uses the same staged path. Buffer pooling, GPU fences and reclamation
remain enabled; no unsynchronized mapping or map-detail reduction is used.
Each allocated page has an additional 8 KiB CPU staging allocation, released
when the page is reclaimed.

Stop the viewer before installing the rebuilt libraries:

```sh
git -C ../maplibre-native-qt apply "$PWD/patches/maplibre-opengl-ubo-streaming.patch"
git -C ../maplibre-native-qt apply "$PWD/patches/maplibre-opengl-ubo-occupancy.patch"
cmake --build build/deps/maplibre-build --parallel 8
cmake --install build/deps/maplibre-build
```

The opt-in benchmark `tests/benchmarks/tst_pan.qml` compares a 1080×640 window
with a maximized window, using a time-based pan trajectory at zoom 15. Each
case warms up for five seconds, then records frame-swap intervals for five
seconds. It reports mean FPS and median/95th-percentile frame intervals, without
taking screenshots during measurement. Use the viewer's Qt/MapLibre environment
and a style whose tile and glyph sources remain available throughout the run:

```sh
VECTORUNE_BENCH_STYLE=file:///absolute/path/style.json \
QT_LOGGING_RULES='maplibre.debug.debug=false' \
build/tests/mapviewer_interaction_tests -input tests/benchmarks/tst_pan.qml
```

For a repeatable mouse-drag test on a denser view, additionally set
`VECTORUNE_BENCH_OPTIONS='{"zoom":13,"width":1600,"height":1000,"drag":true}'`.
Optional `warmupMs` and `measureMs` extend each case for sustained-panning tests.

With the occupancy fix, the local Beijing MBTiles at zoom 13 on an RTX 3080
improved from 49.9 to 60.2 FPS at 1600×1000 and from 31.4 to 43.6 FPS at
3368×1376. Maximized p95 frame time fell from 72.6 to 33.6 ms. The final
mouse-drag run used 10 seconds of warmup and 20 seconds of measurement per size.
Profiling before/after showed the buffer pool growing to thousands of pages
before the fix, then stabilizing at roughly 245 pages for this trajectory.
These measurements retain the same map detail and labels.

On the local NVIDIA RTX 3080, the Beijing Garmin fixture at 3368×1376 improved
from 15.7 to 39.7 mean FPS, with the 95th-percentile frame interval decreasing
from 186 to 50 ms. The comparison uses the label/resize fixes in both builds;
the 1080×640 case remained around 62 FPS. These are local measurements rather
than portable test thresholds.

## Map data and style

### Chinese input on Linux

The Qt SDK used to build the viewer must have an input-context plugin compatible
with the desktop input method. The standalone Qt 6 SDK does not necessarily ship
Fcitx support, and a distribution's Qt 5 plugin cannot serve a Qt 6 application.
For Fcitx or Sogou running on Fcitx, build the pinned official Fcitx Qt 6 plugin
with the same SDK (it supports both Fcitx 4 and 5):

```sh
# Build dependencies: git, CMake, extra-cmake-modules, libxkbcommon-dev,
# libxcb1-dev, libx11-dev and the Qt SDK's Core/Gui/Widgets/DBus development files.
bash tools/build-fcitx-plugin.sh /path/to/Qt/6.5.3/gcc_64
cmake -S . -B build \
  -DVECTORUNE_QT_INPUTCONTEXT_PLUGIN="$PWD/build/deps/fcitx5-qt-build/qt6/platforminputcontext/libfcitx5platforminputcontextplugin.so"
cmake --build build --parallel 4
QT_IM_MODULE=fcitx build/mapviewer/qml_location_mapviewer
```

Set `ECM_DIR` when extra-cmake-modules is unpacked outside the system prefix.
The build copies the plugin beside the viewer and the UI test executable;
installation includes it under `bin/platforminputcontexts`. Qt discovers this
directory automatically. The search-box regression test exercises preedit and
Chinese commit events before submitting the exact Chinese search text.

### Search and location

The upper-right toolbar provides place search, a rotating compass (click to
restore north-up), and recentering. Recenter requests the operating system's
current position (macOS Core Location, including Wi-Fi positioning, without
requiring GPS hardware). The first request prompts for location permission.
A recent position can display immediately while refreshing; if permission is
denied, location services are unavailable, or the request times out without a
usable recent position, it returns to the loaded map's metadata center.
Search is offline: Garmin uses imported POI names (including accent-insensitive
pinyin), while vector MBTiles builds a persistent name index from embedded MVT names
in a separate worker. Later queries reuse it. Both formats use Unicode bigram/name
and R-tree indexes; a one-character substring query scans indexed name rows.
Results are globally ranked before limiting to 50. MBTiles search caches have a
separate 128 MiB disk budget; source content is hashed to invalidate changed maps.
Select a result to center/zoom and mark it. Raster tiles contain no searchable
text. No query is sent to a remote geocoding service.

Right-click the map and choose **加载地图…** to select a `.mbtiles` or Garmin `.img` file.
The viewer checks the database before switching, updates the window title, and
moves to the center/zoom stored in the map metadata. Canceling or selecting an
invalid file leaves the current map in place. OpenMapTiles-schema PBF vector maps
and PNG/JPEG/WebP raster MBTiles are supported; other vector schemas require a
matching style. The file picker starts in the current map's directory.

Garmin IMG maps use the bundled `lib/libgarmin` C parser. CMake builds it without
autotools and includes the `mapviewer_garmin_import` helper beside the app binary.
Loading an IMG precomputes a SQLite spatial/LOD geometry index and compiled routing
graphs in separate processes. A bundled worker reads geometry for requested tiles,
clips/encodes MVT, and serves it on a random loopback port to MapLibre with
`plugins/garmin/resources/garmin-style.json` (repository-relative). This needs no Internet
connection. Geometry is generalized at lower zooms; the finest source tile level is 14.
Runtime rendering no longer loads a whole GeoJSON document. The default diagnostic
import command still exports GeoJSON; `--compact` skips that export.
The viewer stays responsive during conversion, retains the current map on import
failure, and limits each import/graph-compilation phase to three minutes. Content-addressed imports
are reused across sessions. The 512 MiB cache evicts inactive entries; live renderer leases
are never evicted and may temporarily exceed the aggregate budget. A single import
exceeding the budget is rejected. Source IMG files are opened read-only. Installation
includes helper programs beside the executable (and inside the macOS app bundle).

The adapter displays areas, water, roads, trails, railways, boundaries and categorized POI
dots. It uses a generic style rather than Garmin TYP styling. Latin/pinyin road, area and POI names are rendered with bundled offline Noto Sans
glyphs. This particular IMG uses Windows-1252 transliterations, not Chinese text.
Right-click near a POI and choose **附近地点详情** to view available names, category,
street/house number, phone and source address fields within 200 metres. Some
source region records contain opening hours; they are deliberately labeled as
raw fields instead of being presented as administrative regions. Road direction
flags are retained in the diagnostic GeoJSON export and routing graph. Original TYP graphics and
encrypted/unsupported Garmin variants are not implemented.
The tested `Beijing_gmapsupp.img` yielded 284,231 features (50,348 points,
102,636 lines and 131,247 polygons), with bounds approximately
116.08–116.77°E, 39.68–40.18°N. The earlier loaded GPX west of 116.08°E lies
outside this IMG's coverage, though it remains available as an overlay.

The parser is GPLv2; see `lib/libgarmin/COPYING` for its license.
To check the adapter against the supplied Beijing IMG:

```sh
python3 tests/integration/test_garmin_import.py \
  build/tools/mapviewer_garmin_import \
  data/maps/Beijing_gmapsupp.img
```

Right-click and choose **加载轨迹…** to open a GPX file. Track segments and route
points are drawn as red lines, with a green start and red finish. Loading fits
the view to the complete track and displays its point count and horizontal
distance. Separate segments are not connected across recording gaps. A new GPX
replaces the previous track; changing the base map retains the loaded track.
The picker starts in `data/gpx`. The supplied `17-8月-2025-1044.gpx` contains
1,611 track points and approximately 8.47 km of recorded track.

Set `VECTORUNE_DATA_DIR` to a directory containing `maps/` and `gpx/`, or place data
in the installation data directory. A repository-root launch also finds `data/`.
The application starts with an empty local style when no default map is present;
it does not embed the build machine's source path. To use another
OpenMapTiles-schema vector database, pass an absolute or working-directory-relative
path:

```sh
build/mapviewer/qml_location_mapviewer.app/Contents/MacOS/qml_location_mapviewer \
  --mbtiles data/maps/beijing.mbtiles
```

The bundled `plugins/mbtiles/resources/beijing-style.json` (repository-relative) draws land, parks, water, roads,
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
Initial import compiles directed graphs from NET-associated road geometry and Garmin
junction flags into `car.vgr`, `bicycle.vgr` and `walk.vgr`. The separate routing service
memory-maps the selected mode, verifies its checksum/structure, and directly reads its
nodes, edges and turn automaton. A spatial grid selects nearby snap candidates.
Repeated queries reuse the mapping; changing travel mode replaces it; 60 seconds idle
releases the worker. Runtime performs no graph construction. The temporary `routing.cbor`
is removed from managed caches after compilation. Preprocessing still loads the full
network; memory mapping also does not impose a hard query-memory budget.
It supports car, bicycle and walking modes,
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
python3 tests/integration/test_garmin_routing.py \
  build/tools/mapviewer_garmin_route /tmp/imported-map/routing.json
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
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --parallel 8
ctest --test-dir build --output-on-failure
```

These GUI tests need a desktop session and the same installed QMapLibre frameworks
as the application. They check mouse anchor drift, tilted/rotated zoom, repeated
touchpad pixel scrolling and the minimum zoom boundary using the real MapLibre
plugin with a local empty style.

## Garmin dataset inspection

`tools/mapviewer_garmin_inspect` (in the build directory) inventories IMG sections,
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
build/tools/mapviewer_garmin_inspect \
  data/maps/Beijing_gmapsupp.img /tmp/beijing-inspection.json
build/mapviewer/qml_location_mapviewer.app/Contents/MacOS/qml_location_mapviewer \
  --map data/maps/Beijing_gmapsupp.img
```
