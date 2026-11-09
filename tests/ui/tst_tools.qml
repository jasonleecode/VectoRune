import QtQuick
import QtTest
import QtPositioning

Item {
    id: root
    width: 900; height: 600
    QtObject {
        id: map
        property real bearing: 120
        property var center: QtPositioning.coordinate(40, 116)
    }
    QtObject {
        id: view
        property var map: map
        property bool followme: false
        property var gpsCoordinate: QtPositioning.coordinate()
        property var gpsTimestamp: new Date()
    }
    QtObject {
        id: backend
        signal placesReady(var result)
        property string requested: ""
        function searchPlaces(query) { requested = query; placesReady({items: [{name: "test", latitude: 40, longitude: 116}]}) }
    }
    TestCase {
        name: "MapTools"
        property var tools
        function init() {
            const component = Qt.createComponent(Qt.resolvedUrl("../../mapviewer/map/MapTools.qml"))
            compare(component.status, Component.Ready, component.errorString())
            tools = component.createObject(root, {mapView: view, backend: backend,
                mapCenter: QtPositioning.coordinate(39.9, 116.4)})
            verify(tools !== null)
            view.gpsCoordinate = QtPositioning.coordinate()
            map.center = QtPositioning.coordinate(40, 116)
        }
        function cleanup() { tools.destroy() }
        function test_north_up() {
            map.bearing = 120
            const center = map.center
            tools.northUp()
            compare(map.bearing, 0)
            compare(map.center, center)
        }
        function test_location_or_map_center() {
            tools.locate()
            verify(tools.locating)
            tools.positionReceived(QtPositioning.coordinate(40.1, 116.5), new Date())
            compare(map.center, QtPositioning.coordinate(40.1, 116.5))
            verify(!tools.locating)
            tools.locate()
            tools.locationFailed("系统暂时无法定位")
            compare(map.center, tools.mapCenter)
            view.gpsCoordinate = QtPositioning.coordinate(40.1, 116.5)
            view.gpsTimestamp = new Date()
            tools.locate()
            compare(map.center, view.gpsCoordinate)
            view.gpsTimestamp = new Date(Date.now() - 180000)
            tools.locate()
            tools.locationFailed("系统定位超时")
            compare(map.center, tools.mapCenter)
            tools.mapCenter = QtPositioning.coordinate(31.2, 121.5)
            tools.locate()
            tools.locationFailed("系统定位服务不可用")
            compare(map.center, tools.mapCenter)
        }
        function test_search() {
            const query = findChild(tools, "placeQuery")
            query.text = "天安门"
            tools.search()
            compare(backend.requested, "天安门")
            verify(!tools.searching)
            compare(tools.results.length, 1)
            tools.resetSearch()
            compare(tools.results.length, 0)
        }
    }
}
