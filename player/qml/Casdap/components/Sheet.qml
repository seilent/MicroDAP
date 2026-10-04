pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool shown: false
    property bool screenOff: false
    property string title: ""
    property var items: []
    property int focusIndex: 0
    property real progress: 0
    readonly property bool animating: progressAnim.running
    readonly property int duration: Motion.sheet
    readonly property real scrimAlpha: Motion.sheetScrim
    readonly property color scrimColor: Ui.scrimSheet
    readonly property real travel: panel.height
    readonly property real pitch: Ui.px(Ui.settingsPitch)
    readonly property real edge: Ui.px(Ui.edge)
    property alias scrim: scrim
    property alias panel: panel
    property alias bar: bar

    signal accepted(int index)
    signal rejected()

    visible: (root.shown || root.progress > 0) && !root.screenOff

    Rectangle {
        id: scrim

        anchors.fill: parent
        color: root.scrimColor
        opacity: root.progress
    }

    Rectangle {
        id: panel

        x: 0
        y: root.height - panel.height + (1 - root.progress) * panel.height
        width: root.width
        height: root.edge + titleText.height + root.items.length * root.pitch + root.edge
        color: Ui.bgElevated
        opacity: root.progress

        Text {
            id: titleText

            x: root.edge
            y: root.edge
            visible: root.title !== ""
            height: visible ? implicitHeight : 0
            text: root.title
            color: Ui.white(Ui.dialogTitle.alpha)
            font: Ui.font(Ui.dialogTitle)
        }

        Column {
            id: list

            x: 0
            y: root.edge + titleText.height
            width: panel.width

            Repeater {
                model: root.items

                delegate: Item {
                    id: row

                    required property int index
                    required property var modelData
                    readonly property bool isFocused: row.index === root.focusIndex

                    width: list.width
                    height: root.pitch

                    Text {
                        x: root.edge
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.modelData
                        color: Ui.white(row.isFocused ? Ui.dialogItem.alpha : Ui.dialogItem.alphaDim)
                        font: Ui.font(Ui.dialogItem)

                        Behavior on color {
                            enabled: !root.screenOff
                            ColorAnimation {
                                duration: Motion.focusAlpha
                            }
                        }
                    }
                }
            }
        }

        FocusBar {
            id: bar

            x: root.edge - Ui.px(Ui.focusBarOffset)
            rowY: list.y + root.focusIndex * root.pitch
            rowHeight: root.pitch
            visible: root.items.length > 0
            animated: !root.screenOff
        }
    }

    NumberAnimation {
        id: progressAnim

        target: root
        property: "progress"
        duration: Motion.sheet
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    function animateTo(value: real) {
        progressAnim.stop()
        if (root.screenOff || value === root.progress) {
            root.progress = value
            return
        }
        progressAnim.from = root.progress
        progressAnim.to = value
        progressAnim.start()
    }

    function open() {
        root.focusIndex = 0
        root.shown = true
        root.animateTo(1)
    }

    function close() {
        root.shown = false
        root.animateTo(0)
    }

    function complete() {
        if (progressAnim.running)
            progressAnim.complete()
    }

    function moveFocus(delta: int): bool {
        if (root.items.length === 0)
            return false
        root.focusIndex = Math.max(0, Math.min(root.items.length - 1, root.focusIndex + delta))
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (!root.shown)
            return false
        switch (name) {
        case "up":
            return root.moveFocus(-1)
        case "down":
            return root.moveFocus(1)
        case "a":
            if (!repeat)
                root.accepted(root.focusIndex)
            return true
        case "b":
        case "start":
        case "back":
            if (!repeat) {
                root.rejected()
                root.close()
            }
            return true
        default:
            return true
        }
    }

    onScreenOffChanged: {
        if (root.screenOff)
            root.complete()
    }
}
