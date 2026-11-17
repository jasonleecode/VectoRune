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
    locationpermissionfallback.h
)
vectorune_deploy_inputcontext(qml_location_mapviewer)
qt_add_resources(qml_location_mapviewer app_icon PREFIX /icons BASE resources
    FILES resources/mapviewer.svg)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    configure_file(org.vectorune.mapviewer.desktop.in org.vectorune.mapviewer.desktop @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/org.vectorune.mapviewer.desktop"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES resources/mapviewer.svg
        DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
        RENAME org.vectorune.mapviewer.svg)
endif()
vectorune_deploy_maplibre_qml(qml_location_mapviewer)
get_target_property(maplibre_qml_type QMapLibre::PluginQmlLocation TYPE)
if(NOT APPLE AND NOT maplibre_qml_type STREQUAL "STATIC_LIBRARY")
    install(DIRECTORY "$<TARGET_FILE_DIR:QMapLibre::PluginQmlLocation>/"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/MapLibre/Location")
endif()
if(VECTORUNE_QT_INPUTCONTEXT_PLUGIN)
    install(FILES "${VECTORUNE_QT_INPUTCONTEXT_PLUGIN}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}/platforminputcontexts")
endif()
foreach(helper IN LISTS VECTORUNE_HELPERS)
    add_dependencies(qml_location_mapviewer ${helper})
    set_property(TARGET qml_location_mapviewer APPEND PROPERTY LINK_DEPENDS $<TARGET_FILE:${helper}>)
    add_custom_command(TARGET qml_location_mapviewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:${helper}>
            $<TARGET_FILE_DIR:qml_location_mapviewer>/${helper})
endforeach()
if(VECTORUNE_WITH_TILEMAKER)
    set(conversion_font "${PROJECT_SOURCE_DIR}/tools/tilemaker/server/static/fonts/KlokanTech Noto Sans CJK Regular")
    add_custom_command(TARGET qml_location_mapviewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${conversion_font}"
            "$<TARGET_FILE_DIR:qml_location_mapviewer>/fonts/KlokanTech Noto Sans CJK Regular")
    install(DIRECTORY "${conversion_font}" DESTINATION "${CMAKE_INSTALL_DATADIR}/vectorune/fonts")
    install(FILES "${PROJECT_SOURCE_DIR}/tools/tilemaker/server/static/fonts/README.md"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/vectorune/fonts")
endif()

target_link_libraries(qml_location_mapviewer PUBLIC
    QMapLibre::Location
    vectorune_plugins
    vectorune_conversion
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
        "ConversionDialog.qml"
        "map/MapTools.qml"
        "map/TrackSummary.qml"
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
