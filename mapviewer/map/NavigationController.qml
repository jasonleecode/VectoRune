import QtQuick
import QtPositioning

Item {
    id: controller
    visible: false
    property var mapView
    property var backend: mapDocument
    property bool available: false
    property bool permissionGranted: false
    property var start: QtPositioning.coordinate()
    property var finish: QtPositioning.coordinate()
    property var route: null
    property string mode: "car"
    property string strategy: "fastest"
    property bool busy: false
    property bool active: false
    property bool simulating: false
    property bool pendingLive: false
    property var position: QtPositioning.coordinate()
    property int currentIndex: 0
    property real progress: 0
    property real offRoute: 0
    property real lastReroute: 0
    property string status: "右键地图设置起点和终点"
    property var nextInstruction: null
    property real simulatedDistance: 0
    property string plannedKey: ""
    signal message(string title, string body)
    signal permissionRequested()

    function requestKey() {
        return [available, start.latitude, start.longitude, finish.latitude,
                finish.longitude, mode, strategy].join("|")
    }

    function stop() {
        active = false
        simulating = false
        pendingLive = false
        if (mapView) mapView.followme = false
    }
    function reset(ready) {
        stop()
        available = ready
        route = null
        start = QtPositioning.coordinate()
        finish = QtPositioning.coordinate()
        position = QtPositioning.coordinate()
        progress = 0
        status = ready ? "右键地图设置起点和终点" : "路线规划需要佳明路网地图"
    }
    function setStart(coordinate) {
        stop(); start = coordinate; route = null
        status = "已设置起点，请设置终点或规划路线"
    }
    function setFinish(coordinate) {
        stop(); finish = coordinate; route = null
        status = "已设置终点，可开始规划路线"
    }
    function plan() {
        if (busy) return
        if (!available) { message("路线规划", "请先加载含路网的佳明 IMG 地图。"); return }
        if (!start.isValid || !finish.isValid) { message("路线规划", "请先在地图右键菜单中设置起点和终点。"); return }
        busy = true
        plannedKey = requestKey()
        status = "正在计算离线路线…"
        backend.planRoute(start, finish, mode, strategy)
    }
    function beginSimulation() {
        if (!route || busy) return
        stop()
        simulating = true
        active = true
        currentIndex = 0
        progress = 0
        simulatedDistance = 0
        lastReroute = Date.now()
        updatePosition(route.path[0], 0)
    }
    function beginLive() {
        if (!route || busy) return
        if (!permissionGranted) { pendingLive = true; permissionRequested(); return }
        stop()
        active = true
        lastReroute = Date.now()
        status = "等待定位…"
        if (mapView) mapView.followme = true
    }
    onPermissionGrantedChanged: if (permissionGranted && pendingLive) beginLive()

    function updatePosition(coordinate, accuracy) {
        if (!active || !route || !coordinate.isValid) return
        position = coordinate
        let closest = currentIndex
        let nearest = Infinity
        let distanceAlong = progress
        const begin = Math.max(0, currentIndex - 8)
        const end = Math.min(route.path.length - 1, currentIndex + 350)
        const longitudeScale = Math.cos(coordinate.latitude * Math.PI / 180)
        for (let i = begin; i < end; ++i) {
            const a = route.path[i], b = route.path[i+1]
            const x = (a.longitude - coordinate.longitude) * longitudeScale
            const y = a.latitude - coordinate.latitude
            const dx = (b.longitude - a.longitude) * longitudeScale
            const dy = b.latitude - a.latitude
            const denominator = dx * dx + dy * dy
            const fraction = denominator > 0 ? Math.max(0, Math.min(1, -(x * dx + y * dy) / denominator)) : 0
            const segmentLength = route.cumulative[i+1] - route.cumulative[i]
            const projected = a.atDistanceAndAzimuth(fraction * segmentLength, a.azimuthTo(b))
            const distance = coordinate.distanceTo(projected)
            if (distance < nearest) {
                closest = i; nearest = distance
                distanceAlong = route.cumulative[i] + fraction * segmentLength
            }
        }
        currentIndex = closest
        progress = distanceAlong
        offRoute = nearest
        nextInstruction = route.instructions[route.instructions.length - 1]
        for (let i = 0; i < route.instructions.length; ++i) {
            if (route.instructions[i].distanceAlong > progress + 5) {
                nextInstruction = route.instructions[i]
                break
            }
        }
        if (mapView && simulating) mapView.map.center = coordinate
        if (route.distance - progress < 25 && nearest < 40) {
            status = route.finishOffset > 50
                ? "已到达匹配道路终点，原目标另距 " + route.finishOffset.toFixed(0) + " 米"
                : "已到达终点"
            stop()
            return
        }
        const tolerance = Math.max(60, isFinite(accuracy) ? accuracy * 2 : 60)
        if (!simulating && nearest > tolerance && !busy && Date.now() - lastReroute > 15000) {
            lastReroute = Date.now()
            start = coordinate
            plan()
            status = "偏离路线，正在重新规划…"
        } else {
            status = (simulating ? "模拟导航 · " : "导航中 · ")
                + (nextInstruction ? nextInstruction.text : "沿路线前进")
        }
    }

    Connections {
        target: controller.backend
        function onRouteReady(result) {
            if (!controller.busy) return
            controller.busy = false
            // A user may change maps or endpoints while the worker is running.
            if (controller.plannedKey !== controller.requestKey()) return
            if (result.error) {
                controller.status = result.error
                if (!controller.active) controller.message("路线规划失败", result.error)
                return
            }
            controller.route = result
            controller.currentIndex = 0
            controller.progress = 0
            controller.nextInstruction = result.instructions[0]
            controller.status = "路线已就绪"
            if (!controller.active && controller.mapView) {
                controller.mapView.followme = false
                controller.mapView.map.bearing = 0
                controller.mapView.map.tilt = 0
                controller.mapView.map.visibleRegion = result.bounds
            }
        }
    }

    Timer {
        interval: 500
        running: controller.active && controller.simulating && !controller.busy
        repeat: true
        onTriggered: {
            const route = controller.route
            controller.simulatedDistance = Math.min(route.distance,
                controller.simulatedDistance + (controller.mode === "walk" ? 1.5 : 10))
            let index = controller.currentIndex
            while (index + 1 < route.path.length && route.cumulative[index+1] < controller.simulatedDistance) ++index
            const remaining = controller.simulatedDistance - route.cumulative[index]
            let coordinate = route.path[index]
            if (index + 1 < route.path.length)
                coordinate = coordinate.atDistanceAndAzimuth(remaining, coordinate.azimuthTo(route.path[index+1]))
            controller.updatePosition(coordinate, 0)
        }
    }
}
