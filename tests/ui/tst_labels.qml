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
                value: input.localLabelStyle(Qt.resolvedUrl("render-labels.json")).toString()
            }
        }
    }
    TestCase {
        name: "MapLabels"
        when: windowShown
        function test_labelRemainsAnchoredWhilePanning_data() {
            return [
                {tag: "horizontal", dx: 4, dy: 0, bearing: 0, tilt: 0, frames: 600},
                {tag: "tilted-diagonal", dx: 2, dy: 1, bearing: 35, tilt: 40, frames: 120}
            ]
        }
        function test_labelRemainsAnchoredWhilePanning(data) {
            viewer.Window.window.width = 1000
            viewer.Window.window.height = 700
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            const coordinate = QtPositioning.coordinate(39.9042, 116.4074)
            viewer.map.center = coordinate
            viewer.map.zoomLevel = 16
            viewer.map.bearing = data.bearing
            viewer.map.tilt = data.tilt
            wait(2000)
            const baseline = input.labelPixels(viewer, viewer.map.fromCoordinate(coordinate, false))
            verify(baseline.count > 100, "Label did not render: " + JSON.stringify(baseline))
            verify(baseline.markerCount > 10, "Geographic anchor did not render")
            // An isolated label avoids collision/placement changes. This must
            // outlive multiple UBO recycling cycles, not just the first pan.
            for (let frame = 0; frame < data.frames; ++frame) {
                const direction = frame % 60 < 30 ? 1 : -1
                viewer.map.pan(direction * data.dx, direction * data.dy)
                waitForRendering(viewer)
                wait(16)
                const pixels = input.labelPixels(viewer, viewer.map.fromCoordinate(coordinate, false))
                verify(pixels.markerCount > 10, "Geographic anchor disappeared at frame " + frame)
                verify(pixels.count > baseline.count * 0.8 && pixels.count < baseline.count * 1.2,
                       "Label corrupted at frame " + frame + ": " + JSON.stringify(pixels))
                verify(Math.abs(pixels.x - baseline.x) < 2 && Math.abs(pixels.y - baseline.y) < 2,
                       "Label jumped at frame " + frame + ": " + JSON.stringify(pixels))
            }
        }
    }
}
