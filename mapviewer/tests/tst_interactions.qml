import QtQuick
import QtTest
import QtLocation
import QtPositioning
import QtQuick.Window
import "../map"

Item {
    width: 1000
    height: 700

    MapComponent {
        id: viewer
        anchors.fill: parent
        map.plugin: Plugin {
            name: "maplibre"
            PluginParameter {
                name: "maplibre.map.styles"
                value: Qt.resolvedUrl("empty-style.json").toString()
            }
        }
    }

    TestCase {
        name: "MapInteractions"
        when: windowShown
        function initTestCase() {
            viewer.Window.window.width = 1000
            viewer.Window.window.height = 700
        }

        function init() {
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.center = QtPositioning.coordinate(39.9042, 116.4074)
            viewer.map.zoomLevel = 12
            viewer.map.bearing = 0
            viewer.map.tilt = 0
            waitForRendering(viewer)
        }

        function checkAnchor(anchor, point) {
            const pixel = viewer.map.fromCoordinate(anchor, false)
            const error = Math.hypot(pixel.x - point.x, pixel.y - point.y)
            verify(error < 2, "Mouse anchor drifted " + error + " pixels; center=" + viewer.map.center)
        }

        function test_wheelAnchored_data() {
            return [
                {tag: "top-right-in", x: 780, y: 160, delta: 120},
                {tag: "bottom-right-out", x: 740, y: 530, delta: -120},
                {tag: "top-left-in", x: 280, y: 180, delta: 120}
            ]
        }

        function test_wheelAnchored(data) {
            const point = Qt.point(data.x, data.y)
            const anchor = viewer.map.toCoordinate(point, false)
            mouseMove(viewer, data.x, data.y)
            mouseWheel(viewer, data.x, data.y, 0, data.delta)
            waitForRendering(viewer)
            compare(viewer.map.zoomLevel, 12 + data.delta / 120)
            checkAnchor(anchor, point)
        }

        function test_zoomAtAnchored() {
            const point = Qt.point(780, 160)
            const anchor = viewer.map.toCoordinate(point, false)
            viewer.zoomAt(point, viewer.map.zoomLevel + 1)
            waitForRendering(viewer)
            compare(viewer.map.zoomLevel, 13)
            checkAnchor(anchor, point)
        }

        function test_tiltedWheelAnchored() {
            viewer.map.bearing = 35
            viewer.map.tilt = 40
            waitForRendering(viewer)
            const point = Qt.point(780, 160)
            for (let i = 0; i < 8; ++i) {
                const anchor = viewer.map.toCoordinate(point, false)
                mouseWheel(viewer, point.x, point.y, 0, i < 4 ? 120 : -120)
                waitForRendering(viewer)
                checkAnchor(anchor, point)
            }
        }

        function test_worldEdgeWheel() {
            viewer.map.zoomLevel = viewer.map.minimumZoomLevel
            waitForRendering(viewer)
            const oldCenter = viewer.map.center
            mouseWheel(viewer, 950, 10, 0, -120)
            waitForRendering(viewer)
            verify(viewer.map.center.isValid)
            verify(oldCenter.distanceTo(viewer.map.center) < 1,
                   "Zoom-out at minimum unexpectedly moved center to " + viewer.map.center)
        }

        function test_trackpadPixelDelta() {
            const point = Qt.point(780, 160)
            const anchor = viewer.map.toCoordinate(point, false)
            for (let i = 0; i < 12; ++i) {
                input.sendPixelWheel(viewer.map, point, 20)
                waitForRendering(viewer)
                checkAnchor(anchor, point)
            }
            verify(Math.abs(viewer.map.zoomLevel - 13) < 0.0001)
        }
    }
}
