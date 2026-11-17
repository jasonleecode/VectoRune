import QtQuick
import QtLocation
import QtPositioning
import Qt.labs.animation
import MapLibre.Location as ML

// Keep every zoom gesture in map-local logical pixels. In particular, never
// align an invalid coordinate or let a wheel event overshoot the zoom limits.
Item {
    id: root
    property alias map: map
    readonly property real minimumZoomLevel: map.minimumZoomLevel
    readonly property real maximumZoomLevel: map.maximumZoomLevel
    property bool buildings3dAvailable: false
    property bool buildings3dEnabled: false
    property var buildingParameter: null
    property bool styleReady: false

    function syncBuildings() {
        if (!styleReady) return
        if (buildings3dAvailable && !buildingParameter) {
            buildingParameter = buildingLayer.createObject(buildingStyle)
            buildingStyle.addParameter(buildingParameter)
        }
        if (!buildings3dAvailable) buildings3dEnabled = false
    }
    onBuildings3dAvailableChanged: syncBuildings()
    onBuildings3dEnabledChanged: {
        if (buildings3dEnabled && !buildings3dAvailable) {
            buildings3dEnabled = false
            return
        }
        if (buildings3dEnabled) {
            map.zoomLevel = Math.min(maximumZoomLevel, Math.max(map.zoomLevel, 16))
            map.tilt = Math.min(map.maximumTilt, 55)
        } else map.tilt = 0
    }
    Component.onCompleted: { styleReady = true; syncBuildings() }
    Component {
        id: buildingLayer
        ML.LayerParameter {
            styleId: "buildings-3d"
            type: "fill-extrusion"
            layout: ({"visibility": root.buildings3dEnabled && root.buildings3dAvailable ? "visible" : "none"})
        }
    }

    function zoomAt(point, requestedZoom) {
        if (!map.mapReady || !isFinite(requestedZoom))
            return
        const zoom = Math.max(minimumZoomLevel, Math.min(maximumZoomLevel, requestedZoom))
        if (Math.abs(zoom - map.zoomLevel) < 0.000001)
            return
        const anchor = map.toCoordinate(point, false)
        if (!anchor.isValid)
            return
        map.zoomLevel = zoom
        map.alignCoordinateToPoint(anchor, point)
    }

    Map {
        id: map
        anchors.fill: parent
        ML.MapLibre.style: ML.Style { id: buildingStyle }

        WheelHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: (event) => {
                // Read the position from this event in map-local coordinates.
                const point = Qt.point(event.x, event.y)
                if (event.modifiers === Qt.NoModifier) {
                    const delta = event.pixelDelta.y !== 0
                                ? event.pixelDelta.y / 240 : event.angleDelta.y / 120
                    root.zoomAt(point, map.zoomLevel + delta)
                } else if (event.modifiers === Qt.ShiftModifier
                           || event.modifiers === Qt.ControlModifier) {
                    const anchor = map.toCoordinate(point, false)
                    if (anchor.isValid) {
                        if (event.modifiers === Qt.ShiftModifier)
                            map.bearing += event.angleDelta.y / 15
                        else
                            map.tilt = Math.max(map.minimumTilt,
                                Math.min(map.maximumTilt, map.tilt + event.angleDelta.y / 15))
                        map.alignCoordinateToPoint(anchor, point)
                    }
                }
                event.accepted = true
            }
        }

        PinchHandler {
            id: pinch
            target: null
            property var anchor
            onActiveChanged: {
                if (active)
                    flickAnimation.stop()
                anchor = active ? map.toCoordinate(centroid.position, false) : null
            }
            onTranslationChanged: {
                if (active && anchor && anchor.isValid)
                    map.alignCoordinateToPoint(anchor, centroid.position)
            }
            onScaleChanged: (delta) => {
                if (!active || !anchor || !anchor.isValid || delta <= 0)
                    return
                map.zoomLevel = Math.max(root.minimumZoomLevel,
                    Math.min(root.maximumZoomLevel, map.zoomLevel + Math.log2(delta)))
                map.alignCoordinateToPoint(anchor, centroid.position)
            }
            onRotationChanged: (delta) => {
                if (!active || !anchor || !anchor.isValid)
                    return
                map.bearing -= delta
                map.alignCoordinateToPoint(anchor, centroid.position)
            }
            grabPermissions: PointerHandler.TakeOverForbidden
        }

        DragHandler {
            target: null
            maximumPointCount: 1
            onTranslationChanged: (delta) => map.pan(-delta.x, -delta.y)
            onActiveChanged: {
                if (active)
                    flickAnimation.stop()
                else
                    flickAnimation.restart(centroid.velocity)
            }
        }

        property vector3d animDest
        onAnimDestChanged: {
            if (flickAnimation.running) {
                const delta = Qt.vector2d(animDest.x - flickAnimation.last.x,
                                         animDest.y - flickAnimation.last.y)
                map.pan(-delta.x, -delta.y)
                flickAnimation.last = animDest
            }
        }
        Vector3dAnimation on animDest {
            id: flickAnimation
            property vector3d last
            from: Qt.vector3d(0, 0, 0)
            duration: 500
            easing.type: Easing.OutQuad
            function restart(velocity) {
                stop()
                map.animDest = Qt.vector3d(0, 0, 0)
                last = map.animDest
                to = Qt.vector3d(velocity.x / duration * 100, velocity.y / duration * 100, 0)
                start()
            }
        }
        DragHandler {
            target: null
            minimumPointCount: 2
            maximumPointCount: 2
            xAxis.enabled: false
            grabPermissions: PointerHandler.TakeOverForbidden
            onTranslationChanged: (delta) => {
                map.tilt = Math.max(map.minimumTilt,
                    Math.min(map.maximumTilt, map.tilt - delta.y / 5))
            }
            onActiveChanged: if (active) flickAnimation.stop()
        }
    }
}
