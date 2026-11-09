# Build the bundled legacy C parser without its obsolete autotools toolchain.
set(GARMIN_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../lib/libgarmin")
configure_file(compat/config.h "${CMAKE_CURRENT_BINARY_DIR}/config.h" COPYONLY)
configure_file(compat/version.h "${CMAKE_CURRENT_BINARY_DIR}/version.h" COPYONLY)
if(APPLE)
    configure_file(compat/malloc.h "${CMAKE_CURRENT_BINARY_DIR}/malloc.h" COPYONLY)
endif()
add_library(mapviewer_garmin STATIC
    "${GARMIN_SOURCE}/src/garmin_fat.c"
    "${GARMIN_SOURCE}/src/bsp.c"
    "${GARMIN_SOURCE}/src/garmin_lbl.c"
    "${GARMIN_SOURCE}/src/garmin_subdiv.c"
    "${GARMIN_SOURCE}/src/garmin_typ.c"
    "${GARMIN_SOURCE}/src/list.c"
    "${GARMIN_SOURCE}/src/garmin_obj.c"
    "${GARMIN_SOURCE}/src/geoutils.c"
    "${GARMIN_SOURCE}/src/garmin.c"
    "${GARMIN_SOURCE}/src/garmin_rgn.c"
    "${GARMIN_SOURCE}/src/garmin_order.c"
    "${GARMIN_SOURCE}/src/garmin_net.c"
    "${GARMIN_SOURCE}/src/garmin_tdb.c"
    "${GARMIN_SOURCE}/src/array.c"
    "${GARMIN_SOURCE}/src/garmin_nod.c"
    "${GARMIN_SOURCE}/src/garmin_mdr.c"
    "${GARMIN_SOURCE}/src/garmin_route.c")
target_include_directories(mapviewer_garmin PUBLIC "${GARMIN_SOURCE}/src"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(mapviewer_garmin PRIVATE DEBUG DATAPREFIX="${GARMIN_SOURCE}/data")
set_target_properties(mapviewer_garmin PROPERTIES C_STANDARD 99)
find_package(Iconv REQUIRED)
add_library(mapviewer_garmin_attributes STATIC attributes.cpp network.cpp)
target_compile_definitions(mapviewer_garmin_attributes PRIVATE DEBUG)
target_compile_options(mapviewer_garmin_attributes PRIVATE -fno-operator-names)
target_include_directories(mapviewer_garmin_attributes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mapviewer_garmin_attributes PUBLIC mapviewer_garmin Qt6::Core Iconv::Iconv)
target_include_directories(mapviewer_garmin_attributes PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(vectorune_garmin STATIC backend.cpp)
target_link_libraries(vectorune_garmin PUBLIC vectorune_core)
qt_add_resources(vectorune_garmin garmin_style PREFIX /offline
    FILES resources/garmin-style.json resources/fonts/0-255.pbf
          resources/fonts/256-511.pbf resources/fonts/8192-8447.pbf)
