import QtQuick
import QtTest
import QtLocation
import QtPositioning
import QtQuick.Window
import "../../mapviewer/map"

Item {
    id: root
    width: 1080
    height: 640
    InteractiveMapView {
        id: viewer
        anchors.fill: parent
        map.plugin: Plugin {
            name: "maplibre"
            PluginParameter { name: "maplibre.map.styles"; value: input.benchmarkStyle() }
        }
    }
    Timer {
        id: pan
        interval: 16
        repeat: true
        property double started: 0
        property real lastOffset: 0
        onTriggered: {
            // Follow the same trajectory even if the renderer drops frames.
            const phase = (Date.now() - started) % 1920
            const offset = (phase < 960 ? phase : 1920 - phase) / 4
            viewer.map.pan(offset - lastOffset, 0)
            lastOffset = offset
        }
    }
    TestCase {
        name: "MapPanBenchmark"
        when: windowShown
        function test_pan_data() {
            return [{tag: "windowed", maximized: false}, {tag: "maximized", maximized: true}]
        }
        function test_pan(data) {
            verify(input.benchmarkStyle().length > 0, "Set VECTORUNE_BENCH_STYLE to a local map style URL")
            const window = viewer.Window.window
            root.width = Qt.binding(function() { return window.width })
            root.height = Qt.binding(function() { return window.height })
            if (data.maximized) {
                window.showMaximized()
                tryCompare(window, "visibility", Window.Maximized)
            } else {
                window.showNormal()
                window.width = 1080
                window.height = 640
            }
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            viewer.map.center = QtPositioning.coordinate(39.9042, 116.4074)
            viewer.map.zoomLevel = 15
            pan.started = Date.now()
            pan.lastOffset = 0
            pan.start()
            wait(5000)
            input.startFrameTiming(viewer)
            wait(5000)
            const timing = input.finishFrameTiming()
            pan.stop()
            verify(timing.frames > 0)
            console.log("PAN_BENCHMARK", data.tag, viewer.width, viewer.height, JSON.stringify(timing))
        }
    }
}
