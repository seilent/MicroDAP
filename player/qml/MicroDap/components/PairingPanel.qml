import QtQuick

Item {
    id: panel

    property bool overlay: false
    property bool live: true
    property bool pressed: false
    property real arcSweep: 0
    property string hint: ""
    property string title: "discoverable"

    readonly property real nameBaseline: Ui.contentTop + 16
    readonly property real ringCenterY: Ui.contentTop + 90
    readonly property real pulsePeak: Math.max.apply(null, Motion.pulse.map((k) => k[1]))
    readonly property real hintBaseline: panel.ringCenterY + Ui.backRing / 2 * panel.pulsePeak + Ui.qpHintDrop
    readonly property bool pulsing: pulse.running

    property alias discoverableText: discoverableText
    property alias dapModeText: dapModeText
    property alias pulseHolder: pulseHolder
    property alias pairingRing: pairingRing
    property alias hintText: hintText

    function pulseNow(): void {
        if (panel.live)
            pulse.restart()
    }

    function complete(): void {
        if (pulse.running)
            pulse.stop()
        pulseHolder.scale = 1
    }

    Text {
        id: discoverableText

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(panel.nameBaseline) - discoverableText.baselineOffset
        visible: !panel.overlay
        text: panel.title
        color: Ui.white(Ui.npArtist.alpha)
        font: Ui.font(Ui.npArtist)
    }

    Text {
        id: dapModeText

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(panel.nameBaseline) - dapModeText.baselineOffset
        visible: panel.overlay
        text: "pair in dap mode"
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }

    Item {
        id: pulseHolder

        x: Ui.px(Ui.canvasW / 2) - pulseHolder.width / 2
        y: Ui.px(panel.ringCenterY) - pulseHolder.height / 2
        width: pairingRing.width
        height: pairingRing.height
        visible: !panel.overlay
        transformOrigin: Item.Center
        layer.enabled: pulseHolder.scale !== 1
        layer.smooth: true
        layer.textureSize: Qt.size(Math.ceil(pulseHolder.width * panel.pulsePeak), Math.ceil(pulseHolder.height * panel.pulsePeak))

        Ring {
            id: pairingRing

            readonly property string touchKind: "ring"

            size: Ui.backRing
            glyph: "bluetooth"
            focused: true
            arcSweep: panel.arcSweep
            pressed: panel.pressed
            animated: panel.live
        }
    }

    Text {
        id: hintText

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(panel.hintBaseline) - hintText.baselineOffset
        width: panel.width - 2 * Ui.px(Ui.hubEdge)
        visible: !panel.overlay
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        lineHeight: Ui.px(Ui.qpHintPitch)
        lineHeightMode: Text.FixedHeight
        text: panel.hint
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }

    SequentialAnimation {
        id: pulse

        NumberAnimation {
            target: pulseHolder
            property: "scale"
            from: Motion.pulse[0][1]
            to: Motion.pulse[1][1]
            duration: Motion.pulse[1][0] - Motion.pulse[0][0]
            easing.type: Motion.sine
        }
        NumberAnimation {
            target: pulseHolder
            property: "scale"
            to: Motion.pulse[2][1]
            duration: Motion.pulse[2][0] - Motion.pulse[1][0]
            easing.type: Motion.sine
        }
        NumberAnimation {
            target: pulseHolder
            property: "scale"
            to: Motion.pulse[3][1]
            duration: Motion.pulse[3][0] - Motion.pulse[2][0]
            easing.type: Motion.sine
        }
        NumberAnimation {
            target: pulseHolder
            property: "scale"
            to: Motion.pulse[4][1]
            duration: Motion.pulse[4][0] - Motion.pulse[3][0]
            easing.type: Motion.sine
        }
    }
}
