# Compatibility entry point: new builds should use cmake -S . -B build.
cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(VectoRuneStandalone LANGUAGES C CXX)
    add_subdirectory(.. engine)
    return()
endif()
find_package(Qt6 REQUIRED COMPONENTS Gui Qml Network Quick Location QuickDialogs2)
find_package(QMapLibre REQUIRED COMPONENTS Location)
qt_standard_project_setup(REQUIRES 6.5)
qt_add_executable(qml_location_mapviewer WIN32 MACOSX_BUNDLE
    main.cpp
)
foreach(helper IN LISTS VECTORUNE_HELPERS)
    add_dependencies(qml_location_mapviewer ${helper})
    set_property(TARGET qml_location_mapviewer APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:${helper}>)
    add_custom_command(TARGET qml_location_mapviewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${helper}>
            $<TARGET_FILE_DIR:qml_location_mapviewer>/${helper})
endforeach()

target_link_libraries(qml_location_mapviewer PUBLIC
    QMapLibre::Location
    vectorune_plugins
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
qt_add_resources(qml_location_mapviewer "offline_style"
    PREFIX "/offline" FILES resources/empty-style.json)

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
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    BUNDLE DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
if(APPLE)
    set_target_properties(qml_location_mapviewer PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.vectorune.mapviewer"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in")
endif()
