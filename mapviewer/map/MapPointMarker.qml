import QtQuick
import QtLocation

MapQuickItem {
    id: marker
    property color markerColor: "#20b36a"
    property string label: ""
    property real diameter: 28
    // Keep a constant screen size rather than a radius measured in meters.
    zoomLevel: 0
    anchorPoint: Qt.point(diameter / 2, diameter / 2)
    sourceItem: Rectangle {
        width: marker.diameter
        height: marker.diameter
        radius: width / 2
        color: marker.markerColor
        border.color: "white"
        border.width: 2
        antialiasing: true
        Text {
            anchors.centerIn: parent
            text: marker.label
            color: "white"
            font.pixelSize: 12
            font.bold: true
        }
    }
}
