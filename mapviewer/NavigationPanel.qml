import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Frame {
    id: panel
    required property var controller
    property real maximumHeight: parent ? Math.max(0, parent.height - 24) : 640
    width: 360
    padding: 16
    implicitHeight: contents.implicitHeight + topPadding + bottomPadding
    height: Math.min(implicitHeight, maximumHeight)
    visible: controller.start.isValid || controller.finish.isValid || controller.busy
    background: Rectangle { color: "#fcffffff"; radius: 12; border.color: "#dbe3ec" }

    component ActionButton: Button {
        id: action
        property bool primary: false
        implicitHeight: 38
        Layout.minimumWidth: 0
        font.pixelSize: 13
        contentItem: Text {
            text: action.text
            font: action.font
            color: !action.enabled ? "#9aa6b5" : action.primary ? "white" : "#34465c"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
        background: Rectangle {
            radius: 7
            color: !action.enabled ? "#edf1f6" : action.primary
                   ? (action.down ? "#1755b3" : action.hovered ? "#2268d2" : "#2878e5")
                   : (action.down ? "#dce6f2" : action.hovered ? "#e9eff7" : "#f2f5f9")
        }
    }
    component RouteCombo: ComboBox {
        id: combo
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: 38
        font.pixelSize: 13
        enabled: !panel.controller.busy
        background: Rectangle {
            radius: 7
            color: combo.enabled ? "#f7f9fc" : "#edf1f6"
            border.color: combo.activeFocus ? "#2878e5" : "#dbe3ec"
        }
    }
    contentItem: ScrollView {
        id: scroll
        objectName: "routePanelScroll"
        clip: true
        contentWidth: availableWidth
        contentHeight: contents.implicitHeight
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            id: contents
            width: scroll.availableWidth
            height: implicitHeight
            spacing: 12
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Rectangle {
                    width: 30; height: 30; radius: 8; color: "#e8f1ff"
                    Label { anchors.centerIn: parent; text: "↗"; color: "#2878e5"; font.pixelSize: 23 }
                }
                Label {
                    text: "离线路线规划"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    font.pixelSize: 16; font.bold: true; color: "#20344d"
                    elide: Text.ElideRight
                }
                ActionButton {
                    objectName: "routeClearButton"
                    text: "清除"
                    Layout.preferredWidth: 48
                    implicitHeight: 30
                    enabled: !panel.controller.busy
                    onClicked: panel.controller.reset(panel.controller.available)
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: [panel.controller.start.isValid, panel.controller.finish.isValid]
                    delegate: RowLayout {
                        required property int index
                        required property bool modelData
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: 6
                        Rectangle {
                            width: 18; height: 18; radius: 9
                            color: modelData ? (index === 0 ? "#20a86b" : "#e65353") : "#d7dfe9"
                            Label { anchors.centerIn: parent; text: index === 0 ? "起" : "终"; color: "white"; font.pixelSize: 10 }
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: modelData ? "已设置" : "请右键选择"
                            color: modelData ? "#42546b" : "#8896a8"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: panel.controller.status
                color: "#69788c"; font.pixelSize: 12
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 5
                    Label { text: "出行方式"; font.pixelSize: 11; color: "#7b899b" }
                    RouteCombo {
                        objectName: "routeMode"
                        model: ["驾车", "骑行", "步行"]
                        currentIndex: Math.max(0, ["car", "bicycle", "walk"].indexOf(panel.controller.mode))
                        onActivated: {
                            panel.controller.stop()
                            panel.controller.mode = ["car", "bicycle", "walk"][currentIndex]
                            panel.controller.route = null
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 5
                    Label { text: "路线偏好"; font.pixelSize: 11; color: "#7b899b" }
                    RouteCombo {
                        objectName: "routeStrategy"
                        model: ["较快路线", "较短路线"]
                        currentIndex: panel.controller.strategy === "shortest" ? 1 : 0
                        onActivated: {
                            panel.controller.stop()
                            panel.controller.strategy = currentIndex === 0 ? "fastest" : "shortest"
                            panel.controller.route = null
                        }
                    }
                }
            }
            ActionButton {
                objectName: "routePlanButton"
                Layout.fillWidth: true
                primary: true
                text: panel.controller.busy ? "正在规划…" : "规划路线"
                enabled: panel.controller.available && !panel.controller.busy
                         && panel.controller.start.isValid && panel.controller.finish.isValid
                onClicked: panel.controller.plan()
            }
            ProgressBar { visible: panel.controller.busy; indeterminate: true; Layout.fillWidth: true; Layout.preferredHeight: 3 }
            Rectangle {
                visible: panel.controller.route !== null
                Layout.fillWidth: true
                implicitHeight: 64
                radius: 8; color: "#f2f6fc"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: panel.controller.route ? (panel.controller.route.distance / 1000).toFixed(2) + " km" : ""
                        font.pixelSize: 21; font.bold: true; color: "#20344d"
                        elide: Text.ElideRight
                    }
                    Label {
                        text: panel.controller.route ? "约 " + Math.ceil(panel.controller.route.seconds / 60) + " 分钟" : ""
                        font.pixelSize: 13; color: "#61758e"
                    }
                }
            }
            Label {
                visible: panel.controller.route !== null && (panel.controller.route.startOffset > 20 || panel.controller.route.finishOffset > 20)
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 11; color: "#69788c"
                text: panel.controller.route ? "起点距道路 " + panel.controller.route.startOffset.toFixed(0)
                      + " m · 终点距道路 " + panel.controller.route.finishOffset.toFixed(0) + " m" : ""
            }
            Label {
                visible: panel.controller.active
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                font.pixelSize: 12; color: "#2878e5"
                text: panel.controller.route ? "剩余 "
                      + (Math.max(0, panel.controller.route.distance - panel.controller.progress) / 1000).toFixed(2) + " km"
                      + (panel.controller.nextInstruction ? " · 下一提示 "
                         + Math.max(0, panel.controller.nextInstruction.distanceAlong - panel.controller.progress).toFixed(0) + " m" : "") : ""
            }
            RowLayout {
                visible: panel.controller.route !== null
                Layout.fillWidth: true
                spacing: 6
                ActionButton { objectName: "routeLiveButton"; Layout.fillWidth: true; text: "开始导航"; enabled: !panel.controller.busy; onClicked: panel.controller.beginLive() }
                ActionButton { objectName: "routeSimulationButton"; Layout.fillWidth: true; text: "模拟导航"; enabled: !panel.controller.busy; onClicked: panel.controller.beginSimulation() }
                ActionButton { objectName: "routeStopButton"; Layout.fillWidth: true; text: "停止"; enabled: panel.controller.active; onClicked: panel.controller.stop() }
            }
            ListView {
                visible: panel.controller.route !== null
                Layout.fillWidth: true
                Layout.preferredHeight: 130
                clip: true
                spacing: 5
                model: panel.controller.route ? panel.controller.route.instructions : []
                ScrollBar.vertical: ScrollBar {}
                delegate: Label {
                    required property var modelData
                    width: ListView.view.width
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    padding: 6
                    font.pixelSize: 12; color: "#42546b"
                    text: (modelData.distanceAlong / 1000).toFixed(2) + " km · " + modelData.text
                    background: Rectangle { radius: 5; color: "#f7f9fc" }
                }
            }
            Label {
                visible: panel.controller.route !== null && text.length > 0
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                font.pixelSize: 11; color: "#8896a8"
                text: panel.controller.route ? (panel.controller.route.limitations || "") : ""
            }
        }
    }
}
