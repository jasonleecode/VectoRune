// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

import QtQuick
import QtQuick.Controls

Menu {
    property variant coordinate
    property int markersCount
    property int mapItemsCount
    property bool routingAvailable: false
    property bool nearbyAvailable: false
    signal itemClicked(string item)

    MenuItem {
        text: qsTr("加载地图…")
        onTriggered: itemClicked("loadMap")
    }
    MenuSeparator {}
    MenuItem {
        text: qsTr("加载轨迹…")
        onTriggered: itemClicked("loadTrack")
    }
    MenuItem {
        text: qsTr("附近地点详情")
        enabled: nearbyAvailable
        onTriggered: itemClicked("poiDetails")
    }
    MenuSeparator {}
    MenuItem {
        text: qsTr("设为路线起点")
        enabled: routingAvailable
        onTriggered: itemClicked("routeStart")
    }
    MenuItem {
        text: qsTr("设为路线终点")
        enabled: routingAvailable
        onTriggered: itemClicked("routeFinish")
    }
    MenuItem {
        text: qsTr("规划路线")
        enabled: routingAvailable
        onTriggered: itemClicked("planRoute")
    }

    MenuItem {
        text: qsTr("Add Marker")
        onTriggered: itemClicked("addMarker")
    }
    MenuItem {
        text: qsTr("Get coordinate")
        onTriggered: itemClicked("getCoordinate")
    }
    MenuItem {
        text: qsTr("Fit Viewport To Markers")
        onTriggered: itemClicked("fitViewport")
    }
    MenuItem {
        text: qsTr("Delete all markers")
        enabled: markersCount > 0
        onTriggered: itemClicked("deleteMarkers")
    }
}
