import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtPositioning

Item {
    id: tools
    required property var mapView
    required property var backend
    property var mapCenter: QtPositioning.coordinate()
    property bool searchAvailable: true
    property bool searchOpen: false
    property bool searching: false
    property var results: []
    property string status: "输入地名后按回车搜索本地地图"
    property bool locating: false
    property string locationNotice: ""
    signal placeSelected(var coordinate, string name)
    signal locationRequested()
    width: searchOpen ? Math.min(390, parent.width - 24) : 44
    height: searchOpen ? Math.min(470, parent.height - 24) : 148

    function resetSearch() {
        locating = false
        locationTimeout.stop()
        locationNotice = ""
        searching = false
        results = []
        status = "输入地名后按回车搜索本地地图"
    }
    function search() {
        if (searching || !query.text.trim()) return
        searching = true
        results = []
        status = "正在搜索本地地图…"
        backend.searchPlaces(query.text.trim())
    }
    function northUp() {
        if (mapView) mapView.map.bearing = 0
    }
    function locate() {
        if (!mapView) return
        mapView.followme = false
        const position = mapView.gpsCoordinate
        const age = Date.now() - new Date(mapView.gpsTimestamp).getTime()
        if (position && position.isValid && isFinite(age) && age >= 0 && age < 120000)
            mapView.map.center = position
        locating = true
        locationNotice = "正在获取系统位置…"
        noticeTimer.stop()
        locationRequested()
    }
    function waitForPosition() {
        locationNotice = "正在获取系统位置…"
        locationTimeout.restart()
    }
    function positionReceived(coordinate, timestamp) {
        if (!locating || !coordinate.isValid) return
        const age = Date.now() - new Date(timestamp).getTime()
        if (!isFinite(age) || age < 0 || age > 120000) return
        mapView.followme = false
        mapView.map.center = coordinate
        locating = false
        locationTimeout.stop()
        locationNotice = "已回到当前位置"
        noticeTimer.restart()
    }
    function locationFailed(reason) {
        if (!locating) return
        locating = false
        locationTimeout.stop()
        const position = mapView ? mapView.gpsCoordinate : null
        const age = mapView ? Date.now() - new Date(mapView.gpsTimestamp).getTime() : Infinity
        const usable = position && position.isValid && isFinite(age) && age >= 0 && age < 120000
        if (mapView) { mapView.followme = false; mapView.map.center = usable ? position : mapCenter }
        locationNotice = usable ? "使用最近的系统位置" : reason + "，已回到地图中心"
        noticeTimer.restart()
    }
    Timer { id: locationTimeout; interval: 15000; onTriggered: tools.locationFailed("系统暂时无法定位") }
    Timer { id: noticeTimer; interval: 4500; onTriggered: tools.locationNotice = "" }
    Label {
        anchors.right: parent.right
        y: 158
        visible: tools.locationNotice.length > 0
        text: tools.locationNotice
        width: Math.min(260, tools.parent.width - 24)
        wrapMode: Text.Wrap
        padding: 8
        background: Rectangle { color: "#f5ffffff"; radius: 6; border.color: "#cbd3dd" }
    }

    Column {
        anchors.top: parent.top
        anchors.right: parent.right
        spacing: 8
        Repeater {
            model: [
                {kind: "search", label: "搜索地名", icon: "search.svg"},
                {kind: "north", label: "恢复北向在上", icon: "compass.svg"},
                {kind: "locate", label: "回到定位位置（无定位时回地图中心）", icon: "locate.svg"}
            ]
            delegate: Button {
                required property var modelData
                width: 44; height: 44
                Accessible.name: modelData.label
                enabled: (modelData.kind !== "locate" || !tools.locating)
                         && (modelData.kind !== "search" || tools.searchAvailable)
                ToolTip.visible: hovered
                ToolTip.delay: 500
                ToolTip.text: modelData.label
                background: Rectangle {
                    color: parent.down ? "#dceaff" : parent.hovered ? "#eef5ff" : "#fafcffff"
                    border.color: "#cbd3dd"
                    radius: 8
                }
                contentItem: Item {
                    Image {
                        anchors.centerIn: parent
                        width: 24; height: 24
                        source: "../resources/" + modelData.icon
                        rotation: modelData.kind === "north" && tools.mapView ? -tools.mapView.map.bearing : 0
                        smooth: true
                    }
                }
                onClicked: {
                    if (modelData.kind === "north") tools.northUp()
                    else if (modelData.kind === "locate") tools.locate()
                    else {
                        tools.searchOpen = !tools.searchOpen
                        if (tools.searchOpen) query.forceActiveFocus()
                    }
                }
            }
        }
    }
    Frame {
        visible: tools.searchOpen
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.rightMargin: 56
        width: Math.min(330, tools.width - 56)
        height: parent.height
        background: Rectangle { color: "#fafcffff"; radius: 8; border.color: "#cbd3dd" }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            Label { text: "搜索地名"; font.bold: true }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: query
                    objectName: "placeQuery"
                    Layout.fillWidth: true
                    placeholderText: "地名 / 地图中的拼音名称"
                    Accessible.name: "地名搜索输入"
                    selectByMouse: true
                    onAccepted: tools.search()
                }
                Button { text: "搜索"; enabled: !tools.searching && query.text.trim().length > 0; onClicked: tools.search() }
            }
            Label { text: tools.status; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 12 }
            BusyIndicator { running: tools.searching; visible: running; Layout.alignment: Qt.AlignHCenter }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: tools.results
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property var modelData
                    width: ListView.view.width
                    contentItem: Column {
                        spacing: 3
                        Label { text: modelData.name; width: parent.width; wrapMode: Text.Wrap; textFormat: Text.PlainText }
                        Label { text: modelData.subtitle || ""; width: parent.width; wrapMode: Text.Wrap; color: "#6a7685"; font.pixelSize: 11; textFormat: Text.PlainText }
                    }
                    onClicked: {
                        tools.placeSelected(QtPositioning.coordinate(modelData.latitude, modelData.longitude), modelData.name)
                        tools.searchOpen = false
                    }
                }
            }
        }
    }
    Connections {
        target: tools.backend
        function onPlacesReady(result) {
            tools.searching = false
            tools.results = result.items || []
            tools.status = result.error || (tools.results.length ? "找到 " + tools.results.length + " 个地点，点击跳转" : "没有找到地点，请换一个关键词（佳明地图使用源数据中的拼音名）")
        }
    }
}
