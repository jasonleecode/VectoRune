import QtQuick
import QtTest
import QtPositioning

Item {
    id: root
    QtObject {
        id: backend
        signal routeReady(var result)
        property int requests: 0
        property bool deferred: false
        property var pending
        function planRoute(start, finish, mode, strategy) {
            requests++
            const distance = start.distanceTo(finish)
            const result = {path: [start, finish], distance: distance, seconds: distance / 10,
                cumulative: [0, distance], startOffset: 0, finishOffset: 0,
                instructions: [{distanceAlong: 0, text: "出发", maneuver: "depart"},
                               {distanceAlong: distance, text: "到达终点", maneuver: "arrive"}]}
            if (deferred) pending = result
            else routeReady(result)
        }
    }
    TestCase {
        name: "Navigation"
        property var controller
        function init() {
            const component = Qt.createComponent(Qt.resolvedUrl("../../mapviewer/map/NavigationController.qml"))
            compare(component.status, Component.Ready, component.errorString())
            controller = component.createObject(root, {backend: backend, available: true})
            controller.setStart(QtPositioning.coordinate(39.9, 116.4))
            controller.setFinish(QtPositioning.coordinate(39.901, 116.4))
            backend.requests = 0
            backend.deferred = false
        }
        function cleanup() { if (controller) { controller.stop(); controller.destroy() } }
        function test_plan_and_simulate() {
            controller.plan()
            compare(backend.requests, 1)
            verify(controller.route !== null)
            controller.beginSimulation()
            verify(controller.active && controller.simulating)
            controller.updatePosition(QtPositioning.coordinate(39.9005, 116.4), 0)
            verify(Math.abs(controller.progress - controller.route.distance / 2) < 1)
            verify(controller.active)
            controller.updatePosition(controller.finish, 0)
            verify(!controller.active)
            compare(controller.status, "已到达终点")
        }
        function test_permission_gate() {
            controller.plan()
            controller.beginLive()
            verify(controller.pendingLive)
            verify(!controller.active)
            controller.permissionGranted = true
            verify(controller.active)
            verify(!controller.simulating)
        }
        function test_off_route_replans() {
            controller.plan()
            controller.permissionGranted = true
            controller.beginLive()
            controller.lastReroute = Date.now() - 16000
            controller.updatePosition(QtPositioning.coordinate(39.9005, 116.402), 5)
            compare(backend.requests, 2)
        }
        function test_map_switch_clears_route() {
            controller.plan()
            controller.beginSimulation()
            controller.reset(false)
            verify(!controller.active && !controller.available)
            compare(controller.route, null)
            verify(!controller.start.isValid && !controller.finish.isValid)
        }
        function test_changed_destination_discards_pending_route() {
            backend.deferred = true
            controller.plan()
            verify(controller.busy)
            controller.setFinish(QtPositioning.coordinate(39.902, 116.4))
            backend.routeReady(backend.pending)
            verify(!controller.busy)
            compare(controller.route, null)
        }
    }
}
