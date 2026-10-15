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
    readonly property real gap: Ui.px(Ui.pivotGap)
    readonly property real leadGap: Ui.px(Ui.pivotLeadGap)
    property var widths: []
    readonly property var positions: root.layout()
    readonly property bool laidOut: root.knownWidths() === root.count
    readonly property real activeX: root.positionOf(root.activeIndex)
    readonly property real travel: row.x - root.restX
    readonly property real headerShift: Motion.pivotHeaderParallax * root.travel / Ui.u
    readonly property real activeLabelX: row.x + root.activeX
    readonly property bool moving: rowShift.running

    function labelItem(index: int): Item {
        return repeater.itemAt(index)
    }

    function widthOf(index: int): real {
        const w = root.widths[index]
        return w === undefined ? 0 : w
    }

    function knownWidths(): int {
        let n = 0
        for (let i = 0; i < root.count; i++) {
            if (root.widthOf(i) > 0)
                n++
        }
        return n
    }

    function layout() {
        const out = []
        let x = 0
        for (let i = 0; i < root.count; i++) {
            out.push(x)
            x += root.widthOf(i) + (i === root.activeIndex - 1 ? root.leadGap : root.gap)
        }
        return out
    }

    function positionOf(index: int): real {
        const x = root.positions[index]
        return x === undefined ? 0 : x
    }

    function setWidth(index: int, width: real) {
        if (root.widthOf(index) === width)
            return
        const next = root.widths.slice()
        while (next.length <= index)
            next.push(0)
        next[index] = width
        root.widths = next
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

    Item {
        id: row

        x: root.restX - root.activeX

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
                readonly property string touchKind: "pivot"
                readonly property int touchIndex: label.index

                x: root.positionOf(label.index)
                y: root.baselineY - label.baselineOffset
                text: label.modelData
                font: Ui.font(label.role)
                color: Ui.white(label.role.alpha)

                onWidthChanged: root.setWidth(label.index, label.width)

                Component.onCompleted: root.setWidth(label.index, label.width)

                Behavior on x {
                    enabled: root.animated && root.laidOut
                    NumberAnimation {
                        duration: Motion.pivotStrip
                        easing.type: Motion.bezier
                        easing.bezierCurve: Motion.strip
                    }
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
