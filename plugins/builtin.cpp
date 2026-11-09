#include "builtin.h"
#ifdef VECTORUNE_GARMIN
#include "garmin/backend.h"
#endif
#ifdef VECTORUNE_MBTILES
#include "mbtiles/backend.h"
#endif
MapRegistry builtinMaps() {
    MapRegistry registry;
#ifdef VECTORUNE_GARMIN
    registry.add("Garmin", {"img"}, [] { return std::make_shared<GarminBackend>(); });
#endif
#ifdef VECTORUNE_MBTILES
    registry.add("MBTiles", {"mbtiles"}, [] { return std::make_shared<MbtilesBackend>(); });
#endif
    return registry;
}
