import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Frame {
    id: summary
    required property var track
    readonly property bool hasSpeed: !!track && track.recordedEdgeCount + track.derivedEdgeCount > 0
    visible: track !== null
    padding: 10
    background: Rectangle { color: "#f5ffffff"; radius: 6; border.color: "#cbd3dd" }
    function stopColor(index) { return track && track.speedStops ? track.speedStops[index].color : "#8a929e" }
    function stopSpeed(index) { return track && track.speedStops ? track.speedStops[index].speed : "" }
    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        Label {
            Layout.fillWidth: true
            text: summary.track ? summary.track.name + " · " + summary.track.pointCount + " 点 · "
                  + (summary.track.distance / 1000).toFixed(2) + " km" : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.pixelSize: 12
        }
        Label { visible: summary.hasSpeed; text: "轨迹速度（km/h）"; font.pixelSize: 11; color: "#536173" }
        Rectangle {
            visible: summary.hasSpeed
            Layout.fillWidth: true
            implicitHeight: 8
            radius: 4
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: summary.stopColor(0) }
                GradientStop { position: 1 / 3; color: summary.stopColor(1) }
                GradientStop { position: 2 / 3; color: summary.stopColor(2) }
                GradientStop { position: 1; color: summary.stopColor(3) }
            }
        }
        RowLayout {
            visible: summary.hasSpeed
            Layout.fillWidth: true
            Label { text: summary.stopSpeed(0) + " · 慢"; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label { text: summary.stopSpeed(1); font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label { text: summary.stopSpeed(2); font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Label { text: summary.stopSpeed(3) + "+ · 快"; font.pixelSize: 11 }
        }
        Label {
            visible: !!summary.track && summary.track.unknownEdgeCount > 0
            text: "灰色：无可用速度信息"
            font.pixelSize: 11
            color: "#6b7280"
        }
    }
}
