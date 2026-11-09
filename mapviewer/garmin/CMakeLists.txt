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
add_executable(mapviewer_garmin_import import_main.cpp)
find_package(Iconv REQUIRED)
add_library(mapviewer_garmin_attributes STATIC attributes.cpp network.cpp)
target_compile_definitions(mapviewer_garmin_attributes PRIVATE DEBUG)
target_compile_options(mapviewer_garmin_attributes PRIVATE -fno-operator-names)
target_include_directories(mapviewer_garmin_attributes PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mapviewer_garmin_attributes PUBLIC mapviewer_garmin Qt6::Core Iconv::Iconv)
target_link_libraries(mapviewer_garmin_import PRIVATE mapviewer_garmin_attributes)
add_library(mapviewer_routing STATIC router.cpp)
target_link_libraries(mapviewer_routing PUBLIC Qt6::Core Qt6::Positioning)
add_executable(mapviewer_garmin_route route_main.cpp)
target_link_libraries(mapviewer_garmin_route PRIVATE mapviewer_routing)
add_executable(mapviewer_garmin_inspect inspect_main.cpp)
target_compile_definitions(mapviewer_garmin_inspect PRIVATE DEBUG)
target_compile_options(mapviewer_garmin_inspect PRIVATE -fno-operator-names)
target_include_directories(mapviewer_garmin_inspect PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(mapviewer_garmin_inspect PRIVATE mapviewer_garmin Qt6::Core Iconv::Iconv)
if(MAPVIEWER_BUILD_TESTS)
    add_executable(garmin_network_tests test_network.cpp)
    target_link_libraries(garmin_network_tests PRIVATE mapviewer_garmin_attributes)
    add_test(NAME garmin_network_rules COMMAND garmin_network_tests)
    add_executable(garmin_router_tests test_router.cpp)
    target_link_libraries(garmin_router_tests PRIVATE mapviewer_routing)
    add_test(NAME garmin_router COMMAND garmin_router_tests)
    add_executable(garmin_align_tests test_align.c)
    target_include_directories(garmin_align_tests PRIVATE "${GARMIN_SOURCE}/src")
    add_test(NAME garmin_integer_reads COMMAND garmin_align_tests)
endif()
