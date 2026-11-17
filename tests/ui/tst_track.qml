import QtQuick
import QtTest
import QtLocation
import QtPositioning
import "../../mapviewer/map"

Item {
    width: 1000
    height: 700
    InteractiveMapView {
        id: viewer
        anchors.fill: parent
        map.plugin: Plugin {
            name: "maplibre"
            PluginParameter { name: "maplibre.map.styles"; value: Qt.resolvedUrl("empty-style.json") }
        }
    }
    TrackSummary { id: summary; track: viewer.track; width: 330 }
    TestCase {
        name: "TrackSpeedColors"
        when: windowShown
        function pixels(lat, lon, color) {
            return input.colorPixelsNear(viewer, viewer.map.fromCoordinate(QtPositioning.coordinate(lat, lon), false), color)
        }
        function test_loadPanReplaceAndClear() {
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            viewer.map.center = QtPositioning.coordinate(39.904, 116.4075)
            viewer.map.zoomLevel = 16
            for (let n = 0; n < 2; ++n) {
                viewer.track = input.readGpx(Qt.resolvedUrl("track-speeds.gpx"))
                verify(!viewer.track.error)
                verify(summary.visible && summary.hasSpeed)
                for (let step = 0; step < 3; ++step) {
                    tryVerify(function() { return pixels(39.905, 116.405, "#e53c3c") > 15 }, 10000)
                    tryVerify(function() { return pixels(39.905, 116.410, "#245be6") > 15 })
                    tryVerify(function() { return pixels(39.903, 116.4075, "#8a929e") > 15 })
                    compare(pixels(39.905, 116.4075, "#e53c3c"), 0, "Do not connect GPX segment gaps")
                    viewer.map.pan(15, 8)
                    waitForRendering(viewer)
                }
                const replacement = Object.assign({}, viewer.track)
                const data = JSON.parse(replacement.geoJson)
                for (const feature of data.features) feature.properties.color = "#8a929e"
                replacement.geoJson = JSON.stringify(data)
                replacement.recordedEdgeCount = 0
                replacement.derivedEdgeCount = 0
                viewer.track = replacement
                verify(!summary.hasSpeed)
                tryVerify(function() { return pixels(39.905, 116.405, "#8a929e") > 15 })
                tryVerify(function() { return pixels(39.905, 116.410, "#8a929e") > 15 })
                viewer.track = null
                verify(!summary.visible)
                tryVerify(function() { return pixels(39.905, 116.405, "#8a929e") === 0 })
            }
        }
    }
}
