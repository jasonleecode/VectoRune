import QtQuick
import QtTest
import QtPositioning
import "../../mapviewer/map"

Item {
    id: root
    width: 900; height: 750
    QtObject {
        id: backend
        signal routeReady(var result)
        property int requests: 0
        function planRoute(start, finish, mode, strategy) {
            requests++
            routeReady({path: [start, finish], distance: 2450, seconds: 480, startOffset: 30, finishOffset: 0,
                cumulative: [0, 2450], instructions: [{distanceAlong: 0, text: "从起点出发"},
                    {distanceAlong: 1200, text: "沿道路继续行驶，经过下一个路口后向右转弯"}], limitations: "路线时间根据道路类型估算。"})
        }
    }
    NavigationController { id: controller; backend: backend }
    TestCase {
        name: "NavigationPanelLayout"
        when: windowShown
        property var panel
        function init() {
            controller.reset(true)
            controller.setStart(QtPositioning.coordinate(39.9, 116.4))
            backend.requests = 0
            const component = Qt.createComponent(Qt.resolvedUrl("../../mapviewer/NavigationPanel.qml"))
            compare(component.status, Component.Ready, component.errorString())
            panel = component.createObject(root, {controller: controller, width: 360, maximumHeight: 720})
            verify(panel !== null)
        }
        function cleanup() { controller.reset(false); panel.destroy() }
        function assertHorizontalBounds(names) {
            wait(50)
            for (const name of names) {
                const control = findChild(panel, name)
                verify(control !== null)
                const point = control.mapToItem(panel, 0, 0)
                verify(point.x >= panel.leftPadding - 1, name + " exceeds left edge")
                verify(point.x + control.width <= panel.width - panel.rightPadding + 1, name + " exceeds right edge")
            }
        }
        function test_layout_data() { return [{tag: "normal", width: 360}, {tag: "narrow", width: 260}] }
        function test_layout(data) {
            panel.width = data.width
            const plan = findChild(panel, "routePlanButton")
            verify(!plan.enabled)
            controller.setFinish(QtPositioning.coordinate(39.91, 116.42))
            verify(plan.enabled)
            assertHorizontalBounds(["routeClearButton", "routeMode", "routeStrategy", "routePlanButton"])
            verify(plan.mapToItem(panel, 0, plan.height).y <= panel.height)
            mouseClick(plan)
            compare(backend.requests, 1)
            assertHorizontalBounds(["routeLiveButton", "routeSimulationButton", "routeStopButton"])
            mouseClick(findChild(panel, "routeSimulationButton"))
            verify(controller.simulating)
            mouseClick(findChild(panel, "routeStopButton"))
            verify(!controller.active)
            panel.maximumHeight = 240
            tryCompare(panel, "height", 240)
            const scroll = findChild(panel, "routePanelScroll")
            tryVerify(function() { return scroll.contentHeight > scroll.availableHeight },
                      1000, "Route results must scroll in a short window")
            assertHorizontalBounds(["routePlanButton", "routeLiveButton", "routeSimulationButton", "routeStopButton"])
            scroll.contentItem.contentY = scroll.contentHeight - scroll.availableHeight
            wait(50)
            verify(scroll.contentItem.contentY > 0)
            scroll.contentItem.contentY = 0
            wait(50)
            mouseClick(findChild(panel, "routeClearButton"))
            verify(!panel.visible)
        }
    }
}
