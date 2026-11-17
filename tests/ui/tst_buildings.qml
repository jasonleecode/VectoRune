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
        buildings3dAvailable: true
        map.plugin: Plugin {
            name: "maplibre"
            PluginParameter { name: "maplibre.map.styles"; value: Qt.resolvedUrl("buildings-style.json") }
        }
    }
    QtObject { id: backend; signal placesReady(var result) }
    MapTools {
        id: toolbar
        anchors.right: parent.right
        mapView: viewer
        backend: backend
    }
    TestCase {
        name: "BuildingView"
        when: windowShown
        function test_toggleAndMapChange() {
            tryVerify(function() { return viewer.map.mapReady })
            viewer.map.activeMapType = viewer.map.supportedMapTypes[0]
            viewer.map.center = QtPositioning.coordinate(39.9042, 116.4074)
            viewer.map.zoomLevel = 17
            const button = findChild(toolbar, "mapToolThreeD")
            verify(button.enabled)
            compare(input.buildingPixels(viewer).count, 0)
            const center = viewer.map.center
            for (let n = 0; n < 3; ++n) {
                mouseClick(button)
                verify(viewer.buildings3dEnabled)
                verify(button.checked)
                verify(viewer.map.tilt > 40)
                compare(viewer.map.center, center)
                tryVerify(function() { return input.buildingPixels(viewer).count > 500 }, 10000)
                const pixels = input.buildingPixels(viewer)
                verify(pixels.height > pixels.width * 1.5, "Building must have tall walls: " + JSON.stringify(pixels))
                mouseClick(button)
                verify(!viewer.buildings3dEnabled)
                verify(!button.checked)
                compare(viewer.map.tilt, 0)
                tryCompare(viewer.buildingParameter.layout, "visibility", "none")
                tryVerify(function() { return input.buildingPixels(viewer).count === 0 })
            }
            toolbar.mapLoading = true
            verify(!button.enabled)
            toolbar.mapLoading = false
            viewer.map.zoomLevel = 12
            mouseClick(button)
            compare(viewer.map.zoomLevel, 16)
            viewer.buildings3dAvailable = false
            verify(!button.enabled)
            verify(!viewer.buildings3dEnabled)
            compare(viewer.map.tilt, 0)
            mouseClick(button)
            verify(!viewer.buildings3dEnabled)
            tryVerify(function() { return input.buildingPixels(viewer).count === 0 })
        }
    }
}
