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
    property var options: input.benchmarkOptions()
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
            if (root.options.drag) benchmark.mouseMove(viewer, 300 + offset, 300, 0)
            else viewer.map.pan(offset - lastOffset, 0)
            lastOffset = offset
        }
    }
    TestCase {
        id: benchmark
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
                window.width = root.options.width || 1080
                window.height = root.options.height || 640
            }
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            viewer.map.center = QtPositioning.coordinate(39.9042, 116.4074)
            viewer.map.zoomLevel = root.options.zoom || 15
            if (root.options.drag) mousePress(viewer, 300, 300)
            pan.started = Date.now()
            pan.lastOffset = 0
            pan.start()
            wait(root.options.warmupMs || 5000)
            input.startFrameTiming(viewer)
            wait(root.options.measureMs || 5000)
            const timing = input.finishFrameTiming()
            pan.stop()
            if (root.options.drag) mouseRelease(viewer, 300 + pan.lastOffset, 300)
            verify(timing.frames > 0)
            console.log("PAN_BENCHMARK", data.tag, viewer.width, viewer.height, JSON.stringify(timing))
        }
    }
}
