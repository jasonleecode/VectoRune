add_library(vectorune_core STATIC
    mapbackend.cpp mapbackend.h mapregistry.cpp mapdocument.cpp mapdocument.h
    routing/routeservice.cpp routing/routeservice.h
    placeindex.cpp importcache.cpp runtimepaths.cpp tracks/gpxtrack.cpp tracks/gpxtrack.h)
target_include_directories(vectorune_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/tracks")
target_link_libraries(vectorune_core PUBLIC Qt6::Core Qt6::Positioning Qt6::Sql)
add_library(mapviewer_routing STATIC routing/router.cpp routing/graphstorage.cpp)
target_include_directories(mapviewer_routing PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/routing")
target_link_libraries(mapviewer_routing PUBLIC Qt6::Core Qt6::Positioning)

if(VECTORUNE_WITH_GARMIN)
    find_package(Boost CONFIG REQUIRED)
    add_library(vectorune_geometry STATIC geometry/geometryindex.cpp)
    target_include_directories(vectorune_geometry PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/geometry")
    target_link_libraries(vectorune_geometry PUBLIC Qt6::Core Qt6::Sql PRIVATE Boost::headers)
endif()
