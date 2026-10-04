pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property var labels: []
    property int activeIndex: 0
    property bool animated: true

    readonly property int count: root.labels.length
    readonly property real baselineY: Ui.px(Ui.pivotBaseline)
    readonly property real restX: Ui.px(Ui.hubEdge)
    property real activeX: 0
    readonly property real travel: row.x - root.restX
    readonly property real headerShift: Motion.pivotHeaderParallax * root.travel / Ui.u
    readonly property real activeLabelX: row.x + root.activeX
    readonly property bool moving: rowShift.running

    function labelItem(index: int): Item {
        return repeater.itemAt(index)
    }

    function previous(): bool {
        if (root.activeIndex <= 0)
            return false
        root.activeIndex--
        return true
    }

    function next(): bool {
        if (root.activeIndex >= root.count - 1)
            return false
        root.activeIndex++
        return true
    }

    width: Ui.windowW
    height: Ui.px(Ui.pivotBottom)

    Row {
        id: row

        x: root.restX - root.activeX
        spacing: Ui.px(Ui.pivotGap)

        Behavior on x {
            enabled: root.animated
            NumberAnimation {
                id: rowShift

                duration: Motion.pivotStrip
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
        }

        Repeater {
            id: repeater

            model: root.labels

            delegate: Text {
                id: label

                required property int index
                required property var modelData
                readonly property bool active: label.index === root.activeIndex
                readonly property var role: label.active ? Ui.pivotActive : Ui.pivotInactive

                y: root.baselineY - label.baselineOffset
                text: label.modelData
                font: Ui.font(label.role)
                color: Ui.white(label.role.alpha)

                onActiveChanged: {
                    if (label.active)
                        root.activeX = label.x
                }

                onXChanged: {
                    if (label.active)
                        root.activeX = label.x
                }

                Component.onCompleted: {
                    if (label.active)
                        root.activeX = label.x
                }

                Behavior on color {
                    enabled: root.animated
                    ColorAnimation {
                        duration: Motion.pivotLabel
                    }
                }
            }
        }
    }
}
