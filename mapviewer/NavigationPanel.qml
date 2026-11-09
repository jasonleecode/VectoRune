import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Frame {
    id: panel
    required property var controller
    width: 370
    visible: controller.start.isValid || controller.finish.isValid || controller.busy
    background: Rectangle { color: "#f5ffffff"; radius: 8; border.color: "#d6dce2" }
    ColumnLayout {
        width: parent.width
        spacing: 8
        Label { text: "离线路线规划"; font.bold: true }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: panel.controller.status
        }
        RowLayout {
            ComboBox {
                model: ["驾车", "骑行", "步行"]
                onActivated: {
                    panel.controller.stop()
                    panel.controller.mode = ["car", "bicycle", "walk"][currentIndex]
                    panel.controller.route = null
                }
            }
            ComboBox {
                model: ["较快路线", "较短路线"]
                onActivated: {
                    panel.controller.stop()
                    panel.controller.strategy = currentIndex === 0 ? "fastest" : "shortest"
                    panel.controller.route = null
                }
            }
            Button { text: "规划"; enabled: !panel.controller.busy; onClicked: panel.controller.plan() }
        }
        BusyIndicator { running: panel.controller.busy; visible: running; Layout.alignment: Qt.AlignHCenter }
        Label {
            visible: panel.controller.route !== null
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: panel.controller.route ? (panel.controller.route.distance / 1000).toFixed(2)
                + " km · 估算 " + Math.ceil(panel.controller.route.seconds / 60) + " 分钟"
                + "\n道路匹配偏移：起点 " + panel.controller.route.startOffset.toFixed(0)
                + " m，终点 " + panel.controller.route.finishOffset.toFixed(0) + " m" : ""
        }
        Label {
            visible: panel.controller.active
            text: panel.controller.route ? "剩余 "
                + ((panel.controller.route.distance - panel.controller.progress) / 1000).toFixed(2) + " km"
                + (panel.controller.nextInstruction ? " · 下一提示 "
                    + Math.max(0, panel.controller.nextInstruction.distanceAlong - panel.controller.progress).toFixed(0) + " m" : "") : ""
        }
        RowLayout {
            visible: panel.controller.route !== null
            Button { text: "开始导航"; enabled: !panel.controller.busy; onClicked: panel.controller.beginLive() }
            Button { text: "模拟导航"; enabled: !panel.controller.busy; onClicked: panel.controller.beginSimulation() }
            Button { text: "停止"; onClicked: panel.controller.stop() }
        }
        ScrollView {
            visible: panel.controller.route !== null
            Layout.fillWidth: true
            Layout.preferredHeight: 130
            clip: true
            ListView {
                model: panel.controller.route ? panel.controller.route.instructions : []
                delegate: Label {
                    required property var modelData
                    width: ListView.view.width
                    wrapMode: Text.Wrap
                    padding: 3
                    text: (modelData.distanceAlong / 1000).toFixed(2) + " km · " + modelData.text
                }
            }
        }
        Label {
            visible: panel.controller.route !== null
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: 11
            text: panel.controller.route ? panel.controller.route.limitations : ""
        }
        Button { text: "清除路线"; onClicked: panel.controller.reset(panel.controller.available) }
    }
}
