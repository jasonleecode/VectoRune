add_library(vectorune_core STATIC
    mapbackend.cpp mapbackend.h mapregistry.cpp mapdocument.cpp mapdocument.h
    routing/routeservice.cpp routing/routeservice.h
    placeindex.cpp importcache.cpp runtimepaths.cpp stylepalette.cpp tracks/gpxtrack.cpp tracks/gpxtrack.h)
target_include_directories(vectorune_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tracks")
target_link_libraries(vectorune_core PUBLIC Qt6::Core Qt6::Positioning Qt6::Sql)
add_library(vectorune_conversion STATIC conversion/mapconverter.cpp conversion/mapconverter.h)
find_package(Qt6 REQUIRED COMPONENTS Concurrent)
target_include_directories(vectorune_conversion PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/conversion")
target_link_libraries(vectorune_conversion PUBLIC vectorune_core Qt6::Concurrent)
if(VECTORUNE_WITH_TILEMAKER)
    target_compile_definitions(vectorune_conversion PRIVATE VECTORUNE_WITH_TILEMAKER)
    qt_add_resources(vectorune_conversion conversion_profiles PREFIX /conversion
        BASE "${PROJECT_SOURCE_DIR}/tools/tilemaker/resources"
        FILES "${PROJECT_SOURCE_DIR}/tools/tilemaker/resources/config-openmaptiles.json"
              "${PROJECT_SOURCE_DIR}/tools/tilemaker/resources/process-openmaptiles.lua")
endif()
qt_add_resources(vectorune_conversion conversion_style PREFIX /conversion
    BASE "${PROJECT_SOURCE_DIR}/plugins/mbtiles/resources"
    FILES "${PROJECT_SOURCE_DIR}/plugins/mbtiles/resources/beijing-style.json")
add_library(mapviewer_routing STATIC routing/router.cpp routing/graphstorage.cpp)
target_include_directories(mapviewer_routing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/routing")
target_link_libraries(mapviewer_routing PUBLIC Qt6::Core Qt6::Positioning)

if(VECTORUNE_WITH_GARMIN)
    find_package(Boost CONFIG REQUIRED)
    add_library(vectorune_geometry STATIC geometry/geometryindex.cpp)
    target_include_directories(vectorune_geometry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/geometry")
    target_link_libraries(vectorune_geometry PUBLIC Qt6::Core Qt6::Sql PRIVATE Boost::headers)
endif()
