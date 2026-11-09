// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtLocation
import QtPositioning
import MapViewer

ApplicationWindow {
    id: appWindow
    property variant mapview
    property variant minimap
    property variant plugin
    property variant parameters
    property string currentMapName: initialMap.name
    property var loadedTrack: null
    property bool mapLoading: false
    property string currentMapAttribution: initialMap.attribution
    NavigationController {
        id: navigation
        mapView: appWindow.mapview
        permissionGranted: permission.status === Qt.Granted
        onPermissionRequested: permissionDialog.open()
        onMessage: (title, body) => stackView.showMessage(title, body)
    }
    NavigationPanel {
        controller: navigation
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.topMargin: 12
        anchors.leftMargin: 125
        z: 100
    }

    function loadTrackFile(url) {
        const result = gpxTrack.read(url)
        if (result.error) {
            stackView.showMessage(qsTr("加载轨迹失败"), result.error)
            return
        }
        loadedTrack = result
        trackFileDialog.currentFolder = result.folder
        mapview.followme = false
        mapview.map.bearing = 0
        mapview.map.tilt = 0
        mapview.map.visibleRegion = result.bounds
    }

    FileDialog {
        id: trackFileDialog
        title: qsTr("加载轨迹")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("GPX 轨迹 (*.gpx)")]
        currentFolder: initialTrackFolder
        onAccepted: appWindow.loadTrackFile(selectedFile)
    }

    function loadMapFile(url) {
        if (mapLoading) return
        mapLoading = true
        mapDocument.load(url)
    }

    Connections {
        target: mapDocument
        function onPrepared(result) { appWindow.applyMapResult(result) }
    }

    function applyMapResult(result) {
        mapLoading = false
        if (result.error) {
            stackView.showMessage(qsTr("加载地图失败"), result.error)
            return
        }
        const nextParameters = []
        for (let i = 0; i < parameters.length; ++i)
            nextParameters.push(parameterComponent.createObject(appWindow, {
                name: parameters[i].name,
                value: parameters[i].name === "maplibre.map.styles"
                       ? result.styleUrl : parameters[i].value
            }))
        parameters = nextParameters
        mainMenu.selectProvider("maplibre")
        mapview.map.center = QtPositioning.coordinate(result.latitude, result.longitude)
        mapview.map.zoomLevel = result.zoom
        mapview.map.bearing = 0
        mapview.map.tilt = 0
        currentMapName = result.name
        currentMapAttribution = result.attribution
        navigation.reset(!!result.routingAvailable)
        mapFileDialog.currentFolder = result.folder
    }

    FileDialog {
        id: mapFileDialog
        title: qsTr("加载地图")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("地图文件 (*.mbtiles *.img)"), qsTr("MBTiles (*.mbtiles)"), qsTr("佳明地图 (*.img)")]
        currentFolder: initialMap.folder
        onAccepted: appWindow.loadMapFile(selectedFile)
    }

    //defaults
    //! [routecoordinate]
    property variant fromCoordinate: QtPositioning.coordinate(59.9483, 10.7695)
    property variant toCoordinate: QtPositioning.coordinate(59.9645, 10.671)
    //! [routecoordinate]

    function createMap(provider)
    {
        if (parameters && parameters.length>0)
            plugin = Qt.createQmlObject ('import QtLocation; Plugin{ name:"' + provider + '"; parameters: appWindow.parameters}', appWindow)
        else
            plugin = Qt.createQmlObject ('import QtLocation; Plugin{ name:"' + provider + '"}', appWindow)

        if (minimap) {
            minimap.destroy()
            minimap = null
        }

        var zoomLevel = null
        var tilt = null
        var bearing = null
        var fov = null
        var center = null
        var panelExpanded = null
        if (mapview) {
            zoomLevel = mapview.map.zoomLevel
            tilt = mapview.map.tilt
            bearing = mapview.map.bearing
            fov = mapview.map.fieldOfView
            center = mapview.map.center
            panelExpanded = mapview.slidersExpanded
            mapview.destroy()
        }
        mapview = mapComponent.createObject(page);
        mapview.map.plugin = plugin;

        if (zoomLevel != null) {
            mapview.map.tilt = tilt
            mapview.map.bearing = bearing
            mapview.map.fieldOfView = fov
            mapview.map.zoomLevel = zoomLevel
            mapview.map.center = center
            mapview.slidersExpanded = panelExpanded
        } else {
            // Use an integer ZL to enable nearest interpolation, if possible.
            mapview.map.zoomLevel = 12
            // defaulting to 45 degrees, if possible.
            mapview.map.fieldOfView = Math.min(Math.max(45.0, mapview.map.minimumFieldOfView), mapview.map.maximumFieldOfView)
        }

        mapview.forceActiveFocus()
    }

    function getPlugins()
    {
        var plugin = Qt.createQmlObject ('import QtLocation; Plugin {}', appWindow)
        var myArray = new Array()
        for (var i = 0; i<plugin.availableServiceProviders.length; i++) {
            var tempPlugin = Qt.createQmlObject ('import QtLocation; Plugin {name: "' + plugin.availableServiceProviders[i]+ '"}', appWindow)
            if (tempPlugin.supportsMapping())
                myArray.push(tempPlugin.name)
        }
        myArray.sort()
        return myArray
    }

    function initializeProviders(pluginParameters)
    {
        var parameters = new Array()
        for (var prop in pluginParameters){
            var parameter = parameterComponent.createObject(appWindow,
                {name: prop, value: pluginParameters[prop]})
            parameters.push(parameter)
        }
        appWindow.parameters = parameters
        var plugins = getPlugins()
        mainMenu.providerMenu.createMenu(plugins)
        for (var i = 0; i<plugins.length; i++) {
            if (plugins[i] === "maplibre")
                mainMenu.selectProvider(plugins[i])
        }
        if (plugins.indexOf("maplibre") < 0)
            stackView.showMessage(qsTr("MapLibre unavailable"),
                                 qsTr("The MapLibre geoservices plugin could not be loaded."))
    }

    Component {
        id: parameterComponent
        PluginParameter {}
    }

    title: "Mapviewer — " + currentMapName + " · MapLibre"
    height: 640
    width: 1080
    visible: true
    menuBar: mainMenu

    Popup {
        anchors.centerIn: parent
        visible: appWindow.mapLoading
        modal: true
        closePolicy: Popup.NoAutoClose
        padding: 24
        contentItem: Column {
            spacing: 12
            BusyIndicator { running: appWindow.mapLoading; anchors.horizontalCenter: parent.horizontalCenter }
            Label { text: qsTr("正在加载地图，请稍候…") }
        }
    }

    //! [geocode0]
    Address {
        id :fromAddress
        street: "Sandakerveien 116"
        city: "Oslo"
        country: "Norway"
        state : ""
        postalCode: "0484"
    }
    //! [geocode0]

    Address {
        id: toAddress
        street: "Holmenkollveien 140"
        city: "Oslo"
        country: "Norway"
        postalCode: "0791"
    }

    MainMenu {
        id: mainMenu
        plugin: appWindow.plugin

        function toggleMiniMapState()
        {
            console.log("MiniMap with " + plugin)
            if (minimap) {
                minimap.destroy()
                minimap = null
            } else {
                minimap = Qt.createQmlObject ('import "map"; MiniMap{ z: mapview.z + 2 }', mapview)
            }
        }

        function setLanguage(lang)
        {
            mapview.map.plugin.locales = lang;
            stackView.pop(page)
        }

        onSelectProvider: (providerName) => {
            stackView.pop()
            for (var i = 0; i < providerMenu.count; i++) {
                providerMenu.actionAt(i).checked = providerMenu.actionAt(i).text === providerName
            }

            createMap(providerName)
            if (mapview.map.error === Map.NoError) {
                selectMapType(mapview.map.activeMapType)
            } else {
                mainMenu.clearMenu(mapTypeMenu)
            }
        }

        onSelectMapType: (mapType) => {
            stackView.pop(page)
            for (var i = 0; i < mapTypeMenu.count; i++) {
                mapTypeMenu.actionAt(i).checked = mapTypeMenu.actionAt(i).text === mapType.name
            }
            mapview.map.activeMapType = mapType
        }


        onSelectTool: (tool) => {
            switch (tool) {
            case "AddressRoute":
                stackView.pop({item:page, immediate: true})
                stackView.push("forms/RouteAddress.qml" ,
                                   { "plugin": mapview.map.plugin,
                                       "toAddress": toAddress,
                                       "fromAddress": fromAddress})
                stackView.currentItem.showRoute.connect(mapview.calculateCoordinateRoute)
                stackView.currentItem.showMessage.connect(stackView.showMessage)
                stackView.currentItem.closeForm.connect(stackView.closeForm)
                break
            case "CoordinateRoute":
                stackView.pop({item:page, immediate: true})
                stackView.push("forms/RouteCoordinate.qml" ,
                                    { "toCoordinate": toCoordinate,
                                       "fromCoordinate": fromCoordinate})
                stackView.currentItem.showRoute.connect(mapview.calculateCoordinateRoute)
                stackView.currentItem.closeForm.connect(stackView.closeForm)
                break
            case "Geocode":
                stackView.pop({item:page, immediate: true})
                stackView.push("forms/Geocode.qml",
                                   { "address": fromAddress})
                stackView.currentItem.showPlace.connect(mapview.geocode)
                stackView.currentItem.closeForm.connect(stackView.closeForm)
                break
            case "RevGeocode":
                stackView.pop({item:page, immediate: true})
                stackView.push("forms/ReverseGeocode.qml",
                                    { "coordinate": fromCoordinate })
                stackView.currentItem.showPlace.connect(mapview.geocode)
                stackView.currentItem.closeForm.connect(stackView.closeForm)
                break
            case "Language":
                stackView.pop({item:page, immediate: true})
                stackView.push("forms/Locale.qml",
                                   { "locale":  mapview.map.plugin.locales[0]})
                stackView.currentItem.selectLanguage.connect(setLanguage)
                stackView.currentItem.closeForm.connect(stackView.closeForm)
                break
            case "Clear":
                mapview.map.clearData()
                break
            case "Prefetch":
                mapview.map.prefetchData()
                break
            default:
                console.log("Unsupported operation")
            }
        }

        onToggleMapState: (state) => {
            stackView.pop(page)
            switch (state) {
            case "FollowMe":
                if (!mapview.followme && (permission.status !== Qt.Granted))
                    permissionDialog.open();
                else
                    mapview.followme = !mapview.followme
                break
            case "MiniMap":
                toggleMiniMapState()
                isMiniMap = minimap
                break
            default:
                console.log("Unsupported operation")
            }
        }
    }

    MapPopupMenu {
        id: mapPopupMenu
        routingAvailable: navigation.available

        function show(coordinate)
        {
            stackView.pop(page)
            mapPopupMenu.coordinate = coordinate
            mapPopupMenu.markersCount = mapview.markers.length
            mapPopupMenu.mapItemsCount = mapview.mapItems.length
            mapPopupMenu.popup()
        }

        onItemClicked: (item) => {
            stackView.pop(page)
            switch (item) {
            case "loadMap":
                mapFileDialog.open()
                break
            case "loadTrack":
                trackFileDialog.open()
                break
            case "routeStart":
                navigation.setStart(coordinate)
                break
            case "routeFinish":
                navigation.setFinish(coordinate)
                break
            case "planRoute":
                navigation.plan()
                break
            case "poiDetails": {
                const result = mapDocument.poiAt(coordinate, 200)
                if (result.error) {
                    stackView.showMessage(qsTr("地点详情"), result.error)
                    break
                }
                const fields = {name: "名称", category: "类别", street: "街道",
                    house_number: "门牌", city: "城市/地址原始字段", region_raw: "区域原始字段",
                    postcode_raw: "邮编原始字段", country: "国家", phone: "电话",
                    garmin_type_hex: "佳明类型", map_id: "子地图编号"}
                let details = ""
                for (const key in fields) {
                    if (!result[key]) continue
                    const value = String(result[key]).replace(/&/g, "&amp;")
                        .replace(/</g, "&lt;").replace(/>/g, "&gt;")
                    details += "<b>" + fields[key] + "：</b>" + value + "<br/>"
                }
                details += "距离点击位置：" + result.distance.toFixed(0) + " 米"
                stackView.showMessage(qsTr("地点详情"), details)
                break
            }
            case "addMarker":
                mapview.addMarker()
                break
            case "getCoordinate":
                mapview.coordinatesCaptured(coordinate.latitude, coordinate.longitude)
                break
            case "fitViewport":
                mapview.map.fitViewportToMapItems()
                break
            case "deleteMarkers":
                mapview.deleteMarkers()
                break
            default:
                console.log("Unsupported operation:", item)
            }
        }
    }

    MarkerPopupMenu {
        id: markerPopupMenu

        function show(coordinate)
        {
            stackView.pop(page)
            markerPopupMenu.markersCount = mapview.markers.length
            markerPopupMenu.currentMarker = mapview.currentMarker
            markerPopupMenu.popup()
        }

        function askForCoordinate()
        {
            stackView.push("forms/ReverseGeocode.qml",
                                { "title": qsTr("New Coordinate"),
                                   "coordinate":   mapview.markers[mapview.currentMarker].coordinate})
            stackView.currentItem.showPlace.connect(moveMarker)
            stackView.currentItem.closeForm.connect(stackView.closeForm)
        }

        function moveMarker(coordinate)
        {
            mapview.markers[mapview.currentMarker].coordinate = coordinate;
            mapview.map.center = coordinate;
            stackView.pop(page)
        }

        onItemClicked: (item) => {
            stackView.pop(page)
            switch (item) {
            case "deleteMarker":
                mapview.deleteMarker(mapview.currentMarker)
                break;
            case "getMarkerCoordinate":
                mapview.coordinatesCaptured(mapview.markers[mapview.currentMarker].coordinate.latitude,
                                            mapview.markers[mapview.currentMarker].coordinate.longitude)
                break;
            case "moveMarkerTo":
                askForCoordinate()
                break;
            case "routeToNextPoint":
            case "routeToNextPoints":
                mapview.calculateMarkerRoute()
                break
            case "distanceToNextPoint":
                var coordinate1 = mapview.markers[mapview.currentMarker].coordinate;
                var coordinate2 = mapview.markers[mapview.currentMarker+1].coordinate;
                var distance = Helper.formatDistance(coordinate1.distanceTo(coordinate2));
                stackView.showMessage(qsTr("Distance"),"<b>" + qsTr("Distance:") + "</b> " + distance)
                break
            default:
                console.log("Unsupported operation:", item)
            }
        }
    }

    ItemPopupMenu {
        id: itemPopupMenu

        function show(type,coordinate)
        {
            stackView.pop(page)
            itemPopupMenu.type = type
            itemPopupMenu.popup()
        }

        onItemClicked: {
            stackView.pop(page)
            switch (item) {
            case "showRouteInfo":
                stackView.showRouteListPage()
                break;
            case "deleteRoute":
                mapview.routeModel.reset();
                break;
            case "showPointInfo":
                mapview.showGeocodeInfo()
                break;
            case "deletePoint":
                geocodeModel.reset()
                break;
            default:
                console.log("Unsupported operation")
            }
        }
    }

    StackView {
        id: stackView
        anchors.fill: parent
        focus: true
        initialItem: Item {
            id: page

            Text {
                visible: !supportsSsl && map && mapview.activeMapType && activeMapType.metadata.isHTTPS
                text: "The active map type\n
requires (missing) SSL\n
support"
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: appWindow.width / 12
                font.bold: true
                color: "grey"
                anchors.centerIn: parent
                z: 12
            }
        }

        function showMessage(title,message,backPage)
        {
            push("forms/Message.qml",
                               {
                                   "title" : title,
                                   "message" : message,
                                   "backPage" : backPage
                               })
            currentItem.closeForm.connect(closeMessage)
        }

        function closeMessage(backPage)
        {
            pop(backPage)
        }

        function closeForm()
        {
            pop(page)
        }

        function showRouteListPage()
        {
            push("forms/RouteList.qml",
                               {
                                   "routeModel" : mapview.routeModel
                               })
            currentItem.closeForm.connect(closeForm)
        }
    }

    Component {
        id: mapComponent

        MapComponent {
            mapAttribution: appWindow.currentMapAttribution
            navigationRoute: navigation.route
            navigationStart: navigation.start
            navigationFinish: navigation.finish
            navigationPosition: navigation.position
            navigationActive: navigation.active
            navigationSimulating: navigation.simulating
            onGpsUpdated: (coordinate, accuracy) => navigation.updatePosition(coordinate, accuracy)
            track: appWindow.loadedTrack
            width: page.width
            height: page.height
            onFollowmeChanged: mainMenu.isFollowMe = followme
            map.onSupportedMapTypesChanged: {
                if (map.supportedMapTypes.length)
                    map.activeMapType = map.supportedMapTypes[0]
                mainMenu.mapTypeMenu.createMenu(map)
            }
            onCoordinatesCaptured: (latitude, longitude) => {
                var text = "<b>" + qsTr("Latitude:") + "</b> " + Helper.roundNumber(latitude,4) + "<br/><b>" + qsTr("Longitude:") + "</b> " + Helper.roundNumber(longitude,4)
                stackView.showMessage(qsTr("Coordinates"),text);
            }
            onGeocodeFinished:{
                if (geocodeModel.status == GeocodeModel.Ready) {
                    if (geocodeModel.count == 0) {
                        stackView.showMessage(qsTr("Geocode Error"),qsTr("Unsuccessful geocode"))
                    } else if (geocodeModel.count > 1) {
                        stackView.showMessage(qsTr("Ambiguous geocode"), geocodeModel.count + " " +
                                              qsTr("results found for the given address, please specify location"))
                    } else {
                        stackView.showMessage(qsTr("Location"), geocodeMessage(),page)
                    }
                } else if (geocodeModel.status == GeocodeModel.Error) {
                    stackView.showMessage(qsTr("Geocode Error"),qsTr("Unsuccessful geocode"))
                }
            }
            onRouteError: stackView.showMessage(qsTr("Route Error"),qsTr("Unable to find a route for the given points"),page)

            onShowGeocodeInfo: stackView.showMessage(qsTr("Location"),geocodeMessage(),page)

            map.onErrorChanged: {
                if (map.error !== Map.NoError) {
                    var title = qsTr("ProviderError")
                    var message = map.errorString + "<br/><br/><b>" + qsTr("Try to select other provider") + "</b>"
                    if (map.error === Map.MissingRequiredParameterError)
                        message += "<br/>" + qsTr("or see") + " \'mapviewer --help\' "
                                + qsTr("how to pass plugin parameters.")
                    stackView.showMessage(title,message);
                }
            }
            onShowMainMenu: (coordinate) => mapPopupMenu.show(coordinate)
            onShowMarkerMenu: (coordinate) => markerPopupMenu.show(coordinate)
            onShowRouteMenu: (coordinate) => itemPopupMenu.show("Route",coordinate)
            onShowPointMenu: (coordinate) => itemPopupMenu.show("Point",coordinate)
            onShowRouteList: stackView.showRouteListPage()

        }
    }

    LocationPermission {
        id: permission
        accuracy: LocationPermission.Precise
        availability: LocationPermission.WhenInUse
    }

    Dialog {
        id: permissionDialog
        anchors.centerIn: parent
        padding: 20
        standardButtons: (permission.status === Qt.Denied) ? Dialog.Close
                                                           : Dialog.Close | Dialog.Ok
        closePolicy: Dialog.NoAutoClose
        title: qsTr("Permission")

        Label {
            id: permissionRequestText
            text: (permission.status === Qt.Denied)
                  ? qsTr("Grant the location permission then open the app again.")
                  : qsTr("Location permission is needed.")
        }

        onAccepted: {
            if (permission.status !== Qt.Denied)
                permission.request();
        }

        onStandardButtonsChanged: {
            if (standardButtons & Dialog.Ok)
                standardButton(Dialog.Ok).text = qsTr("Request Permission");
        }

    }
}
