import QtQuick
import QtTest
import QtLocation
import QtPositioning
import "../../mapviewer/map"

Item {
    width: 900; height: 600
    InteractiveMapView {
        id: viewer
        anchors.fill: parent
        map.plugin: Plugin {
            name: "maplibre"
            PluginParameter { name: "maplibre.map.styles"; value: Qt.resolvedUrl("empty-style.json") }
        }
    }
    MapPointMarker {
        id: marker
        parent: viewer.map
        coordinate: QtPositioning.coordinate(39.9042, 116.4074)
        label: "起"
        markerColor: "#20b36a"
    }
    TestCase {
        name: "MapPointMarkers"
        when: windowShown
        function test_fixedScreenSize_data() {
            return [{tag: "start", label: "起", color: "#20b36a"},
                    {tag: "finish", label: "终", color: "#e53c3c"}]
        }
        function test_fixedScreenSize(data) {
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            viewer.map.center = marker.coordinate
            marker.label = data.label
            marker.markerColor = data.color
            for (const zoom of [10, 14, 18, 20]) {
                viewer.map.zoomLevel = zoom
                viewer.map.tilt = zoom === 18 ? 45 : 0
                viewer.map.bearing = zoom === 18 ? 30 : 0
                waitForRendering(viewer)
                wait(100)
                const pixels = input.markerPixels(viewer, viewer.map.fromCoordinate(marker.coordinate, false), data.color)
                verify(pixels.width >= 22 && pixels.width <= 27, "Fixed marker width: " + JSON.stringify(pixels))
                verify(pixels.height >= 22 && pixels.height <= 27, "Fixed marker height: " + JSON.stringify(pixels))
                verify(Math.abs(pixels.x) < 2 && Math.abs(pixels.y) < 2, "Marker stays centered on coordinate")
            }
        }
    }
}
