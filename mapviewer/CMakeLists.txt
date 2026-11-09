# Generated from mapviewer.pro.

cmake_minimum_required(VERSION 3.16)
project(qml_location_mapviewer LANGUAGES C CXX)
option(MAPVIEWER_BUILD_TESTS "Build map interaction regression tests" OFF)
if(MAPVIEWER_BUILD_TESTS)
    enable_testing()
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT DEFINED INSTALL_EXAMPLESDIR)
  set(INSTALL_EXAMPLESDIR "examples")
endif()

set(INSTALL_EXAMPLEDIR "${INSTALL_EXAMPLESDIR}/location/mapviewer")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Network Quick Positioning Location Sql QuickDialogs2)
find_package(QMapLibre REQUIRED COMPONENTS Location)
find_package(ZLIB REQUIRED)
add_executable(mapviewer_place_search place_search.cpp)
target_link_libraries(mapviewer_place_search PRIVATE Qt6::Core Qt6::Sql ZLIB::ZLIB)
add_subdirectory(garmin)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(qml_location_mapviewer WIN32 MACOSX_BUNDLE
    main.cpp
    mapdocument.cpp
    mapdocument.h
    gpxtrack.cpp
    gpxtrack.h
)
add_dependencies(qml_location_mapviewer mapviewer_garmin_import mapviewer_garmin_route mapviewer_place_search)
set_property(TARGET qml_location_mapviewer APPEND PROPERTY LINK_DEPENDS
    $<TARGET_FILE:mapviewer_garmin_import> $<TARGET_FILE:mapviewer_garmin_route> $<TARGET_FILE:mapviewer_place_search>)
add_custom_command(TARGET qml_location_mapviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:mapviewer_place_search>
        $<TARGET_FILE_DIR:qml_location_mapviewer>/mapviewer_place_search)
add_custom_command(TARGET qml_location_mapviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:mapviewer_garmin_import>
        $<TARGET_FILE_DIR:qml_location_mapviewer>/mapviewer_garmin_import)
add_custom_command(TARGET qml_location_mapviewer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:mapviewer_garmin_route>
        $<TARGET_FILE_DIR:qml_location_mapviewer>/mapviewer_garmin_route)

target_link_libraries(qml_location_mapviewer PUBLIC
    QMapLibre::Location
    Qt6::Sql
    Qt6::QuickDialogs2
    Qt::Core
    Qt::Gui
    Qt::Location
    Qt::Network
    Qt::Positioning
    Qt::Qml
    Qt::Quick
)
qmaplibre_location_setup_plugins(qml_location_mapviewer)
target_compile_definitions(qml_location_mapviewer PRIVATE
    MAPVIEWER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
qt_add_resources(qml_location_mapviewer "offline_style"
    PREFIX "/offline"
    FILES resources/beijing-style.json
          resources/garmin-style.json
          resources/fonts/0-255.pbf
          resources/fonts/256-511.pbf
          resources/fonts/8192-8447.pbf
)

qt_add_qml_module(qml_location_mapviewer
    URI MapViewer
    VERSION 1.0
    QML_FILES
        "forms/Geocode.qml"
        "forms/GeocodeForm.ui.qml"
        "forms/Locale.qml"
        "forms/LocaleForm.ui.qml"
        "forms/Message.qml"
        "forms/MessageForm.ui.qml"
        "forms/ReverseGeocode.qml"
        "forms/ReverseGeocodeForm.ui.qml"
        "forms/RouteAddress.qml"
        "forms/RouteAddressForm.ui.qml"
        "forms/RouteCoordinate.qml"
        "forms/RouteCoordinateForm.ui.qml"
        "forms/RouteList.qml"
        "forms/RouteListDelegate.qml"
        "forms/RouteListHeader.qml"
        "helper.js"
        "map/MapComponent.qml"
        "map/InteractiveMapView.qml"
        "map/MapSliders.qml"
        "map/Marker.qml"
        "map/MiniMap.qml"
        "Main.qml"
        "map/NavigationController.qml"
        "NavigationPanel.qml"
        "map/MapTools.qml"
        "menus/ItemPopupMenu.qml"
        "menus/MainMenu.qml"
        "menus/MapPopupMenu.qml"
        "menus/MarkerPopupMenu.qml"
    RESOURCES
        "resources/search.svg"
        "resources/compass.svg"
        "resources/locate.svg"
        "resources/marker.png"
        "resources/marker_blue.png"
        "resources/scale.png"
        "resources/scale_end.png"
    )

if(QT_FEATURE_geoservices_maplibregl)
    target_link_libraries(qml_location_mapviewer PUBLIC
        Qt::Sql
    )
endif()

if (ANDROID)
    # Download & link to OpenSSL libs
    include(FetchContent)
    FetchContent_Declare(
      android_openssl
      DOWNLOAD_EXTRACT_TIMESTAMP true
      URL https://github.com/KDAB/android_openssl/archive/refs/heads/master.zip
    )
    FetchContent_MakeAvailable(android_openssl)

    include(${android_openssl_SOURCE_DIR}/android_openssl.cmake)
    add_android_openssl_libraries(qml_location_mapviewer)
endif()

install(TARGETS qml_location_mapviewer
    RUNTIME DESTINATION "${INSTALL_EXAMPLEDIR}"
    BUNDLE DESTINATION "${INSTALL_EXAMPLEDIR}"
    LIBRARY DESTINATION "${INSTALL_EXAMPLEDIR}"
)
if(APPLE)
    set_target_properties(qml_location_mapviewer PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.vectorune.mapviewer"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
endif()

if(MAPVIEWER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS QuickTest)
    qt_add_executable(mapviewer_interaction_tests MACOSX_BUNDLE tests/interaction_main.cpp)
    target_link_libraries(mapviewer_interaction_tests PRIVATE Qt6::QuickTest Qt6::Quick Qt6::Qml QMapLibre::Location)
    target_compile_definitions(mapviewer_interaction_tests PRIVATE
        QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    qmaplibre_location_setup_plugins(mapviewer_interaction_tests)
    add_test(NAME mapviewer_interactions COMMAND mapviewer_interaction_tests)
endif()
