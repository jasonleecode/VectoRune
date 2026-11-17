import QtQuick
import QtQuick.Controls
import QtTest

Item {
    id: root
    width: 900
    height: 820
    QtObject {
        id: backend
        property bool available: true
        property bool running: false
        property string status: ""
        property string log: ""
        property url result: ""
        property bool canOpen: false
        property var request: null
        property var elements: [{key: "building", label: "建筑物"}, {key: "water", label: "水域"}]
        function fileUrl(path) { return "file://" + path }
        function localPath(url) { return String(url).replace("file://", "") }
        function suggestedOutput(url, format) { return String(url).replace(".osm.pbf", format === "xyz" ? "-tiles" : "." + format) }
        function start(source, output, groups, preset, format, minZoom, maxZoom) {
            request = {source: source, output: output, groups: groups, preset: preset, format: format, minZoom: minZoom, maxZoom: maxZoom}
            running = true
            status = "正在转换"
        }
        function cancel() { running = false; status = "已取消" }
    }
    SignalSpy { id: opened; signalName: "openMap" }
    SignalSpy { id: menuSelected; signalName: "selectTool" }
    TestCase {
        name: "ConversionDialog"
        when: windowShown
        property var dialog
        function init() {
            backend.running = false
            backend.canOpen = false
            backend.result = ""
            backend.request = null
            const component = Qt.createComponent(Qt.resolvedUrl("../../mapviewer/ConversionDialog.qml"))
            compare(component.status, Component.Ready, component.errorString())
            dialog = component.createObject(root, {converter: backend})
            verify(dialog !== null)
            dialog.open()
            tryCompare(dialog, "visible", true)
            opened.target = dialog
            opened.clear()
        }
        function cleanup() { dialog.close(); dialog.destroy() }
        function test_optionsAndBackgroundJob() {
            findChild(dialog, "conversionSource").text = "/tmp/map.osm.pbf"
            findChild(dialog, "conversionOutput").text = "/tmp/map.mbtiles"
            findChild(dialog, "conversionElements").itemAt(1).checked = false
            findChild(dialog, "conversionStyle").currentIndex = 2
            const start = findChild(dialog, "conversionStart")
            mouseClick(start)
            verify(backend.request !== null)
            compare(backend.request.groups, ["building"])
            compare(backend.request.preset, "dark")
            compare(backend.request.format, "mbtiles")
            compare(backend.request.maxZoom, 14)
            compare(start.enabled, false)
            dialog.close()
            verify(backend.running, "closing the form must not abandon conversion")
            dialog.open()
            compare(findChild(dialog, "conversionSource").text, "/tmp/map.osm.pbf")
            backend.cancel()
            compare(start.enabled, true)
        }
        function test_formatsAndOpenResult() {
            findChild(dialog, "conversionSource").text = "/tmp/map.osm.pbf"
            const format = findChild(dialog, "conversionFormat")
            format.currentIndex = 1
            dialog.suggestOutput()
            compare(findChild(dialog, "conversionOutput").text, "/tmp/map.pmtiles")
            format.currentIndex = 2
            dialog.suggestOutput()
            compare(findChild(dialog, "conversionOutput").text, "/tmp/map-tiles")
            verify(!findChild(dialog, "conversionOpen").visible)
            backend.result = "file:///tmp/map.mbtiles"
            backend.canOpen = true
            mouseClick(findChild(dialog, "conversionOpen"))
            compare(opened.count, 1)
            compare(String(opened.signalArguments[0][0]), "file:///tmp/map.mbtiles")
        }
        function test_menuEntry() {
            const component = Qt.createComponent(Qt.resolvedUrl("../../mapviewer/menus/MainMenu.qml"))
            compare(component.status, Component.Ready, component.errorString())
            const menu = component.createObject(root)
            menuSelected.target = menu
            menuSelected.clear()
            const item = findChild(menu, "convertMapMenuItem")
            verify(item !== null)
            item.triggered()
            compare(menuSelected.count, 1)
            compare(menuSelected.signalArguments[0][0], "ConvertMap")
            menu.destroy()
        }
    }
}
