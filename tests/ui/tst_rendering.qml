import QtQuick
import QtTest
import QtLocation
import QtPositioning
import QtQuick.Window
import "../../mapviewer/map"

Item {
    width: 1000
    height: 700
    InteractiveMapView {
        id: viewer
        anchors.fill: parent
        map.plugin: Plugin {
            name: "maplibre"
            PluginParameter {
                name: "maplibre.map.styles"
                value: Qt.resolvedUrl("render-background.json").toString()
            }
        }
    }
    TestCase {
        name: "MapRendering"
        when: windowShown
        function test_panKeepsBackground() {
            viewer.Window.window.width = 1000
            viewer.Window.window.height = 700
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            viewer.map.center = QtPositioning.coordinate(39.9042, 116.4074)
            viewer.map.zoomLevel = 12
            wait(1000)
            // Capture the actual window, including the MapLibre native texture.
            // No tile server or external data is needed for this regression.
            for (let frame = 0; frame < 60; ++frame) {
                viewer.map.pan(frame % 2 ? -12 : 12, 4)
                viewer.map.tilt = frame % 3 === 0 ? 45 : 0
                waitForRendering(viewer)
                for (const color of input.backgroundSamples(viewer)) {
                    compare(color, "#f3f0e8", "Background corrupted at frame " + frame)
                }
            }
        }
    }
}
