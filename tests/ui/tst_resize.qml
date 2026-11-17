import QtQuick
import QtTest
import QtLocation
import QtPositioning
import QtQuick.Window
import "../../mapviewer/map"

Item {
    id: root
    width: 640
    height: 480
    Rectangle { anchors.fill: parent; color: "#ff00ff" }
    InteractiveMapView {
        id: viewer
        anchors.fill: parent
        map.plugin: Plugin {
            name: "maplibre"
            PluginParameter {
                name: "maplibre.map.styles"
                value: input.localLabelStyle(Qt.resolvedUrl("render-labels.json")).toString()
            }
        }
    }
    TestCase {
        name: "MapResize"
        when: windowShown
        function checkMap() {
            tryCompare(viewer, "width", viewer.Window.window.width)
            tryCompare(viewer, "height", viewer.Window.window.height)
            wait(300)
            for (const color of input.mapEdgeSamples(viewer))
                compare(color, "#f3f0e8", "Map must fill the resized window")
            const pixels = input.labelPixels(viewer, Qt.point(viewer.width / 2, viewer.height / 2))
            verify(pixels.count > 100, "Center label missing after resize: " + JSON.stringify(pixels))
            verify(pixels.markerCount > 10, "Center marker missing after resize")
            verify(Math.abs(pixels.markerX) < 2 && Math.abs(pixels.markerY - 25) < 2,
                   "Resize must keep the geographic center: " + JSON.stringify(pixels))
            return pixels.count
        }
        function test_resizeAndMaximize() {
            const window = viewer.Window.window
            // QuickTest's root item is independent of the window size.
            root.width = Qt.binding(function() { return window.width })
            root.height = Qt.binding(function() { return window.height })
            window.showNormal()
            window.width = 640
            window.height = 480
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            viewer.map.center = QtPositioning.coordinate(39.9042, 116.4074)
            viewer.map.zoomLevel = 16
            wait(1500)
            const baseline = checkMap()
            for (const size of [Qt.size(1100, 760), Qt.size(480, 360), Qt.size(900, 600)]) {
                window.width = size.width
                window.height = size.height
                const count = checkMap()
                verify(count > baseline * 0.8 && count < baseline * 1.2,
                       "Resize must preserve label scale")
            }
            window.showMaximized()
            tryCompare(window, "visibility", Window.Maximized)
            checkMap()
            window.showNormal()
            tryCompare(window, "visibility", Window.Windowed)
            checkMap()
        }
    }
}
