pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool screenOff: false
    property int level: 0
    property bool playing: false
    property string artist: ""
    property string title: ""
    readonly property bool shown: root.opacity > 0 || hideTimer.running
    readonly property bool animating: showAnim.running || popAnim.running || fadeAnim.running
    readonly property int showDuration: Motion.volumeShow
    readonly property int popDuration: Motion.volumePop
    readonly property int hideDelay: Motion.volumeHide
    readonly property int fadeDuration: Motion.volumeFade
    readonly property color layerColor: Ui.scrimOverlay
    readonly property real edge: Ui.px(Ui.edge)
    readonly property real centerX: root.width / 2
    readonly property real centerY: root.height / 2
    readonly property real glyphSize: Ui.px(Ui.overlayGlyph)
    readonly property real glyphOffset: Ui.px(Ui.overlayGlyphOffset)
    readonly property real ringSize: Ui.px(Ui.overlayRing)
    property alias background: background
    property alias exitLabel: exitLabel
    property alias plus: plus
    property alias number: number
    property alias prev: prev
    property alias ring: ring
    property alias next: next
    property alias minus: minus
    property alias volumeLabel: volumeLabel
    property alias artistText: artistText
    property alias titleText: titleText
    property alias hide: hideTimer
    property alias showAnimation: showAnim
    property alias popAnimation: popAnim
    property alias fadeAnimation: fadeAnim

    opacity: 0
    visible: root.opacity > 0 && !root.screenOff

    Rectangle {
        id: background

        anchors.fill: parent
        color: root.layerColor
    }

    Glyph {
        id: plus

        x: root.centerX - plus.width / 2
        y: Ui.px(Ui.overlayPlusY) - plus.height / 2
        size: Ui.overlayGlyph
        name: "plus"
        color: Ui.white(Ui.overlayLit)
    }

    Text {
        id: number

        x: root.edge
        y: Ui.px(Ui.overlayNumberBaseline) - number.baselineOffset
        text: root.level
        color: Ui.white(Ui.overlayLit)
        font: Ui.font(Ui.overlayNumber)
        transformOrigin: Item.Center
    }

    Item {
        id: controls

        anchors.fill: parent
        opacity: Ui.overlayDim

        Text {
            id: exitLabel

            x: root.edge
            y: root.edge
            text: "exit"
            color: Ui.textFull
            font: Ui.font(Ui.overlayLabel)
        }

        Glyph {
            id: prev

            x: root.centerX - root.glyphOffset - prev.width / 2
            y: root.centerY - prev.height / 2
            size: Ui.overlayGlyph
            name: "previous"
            color: Ui.textFull
        }

        Ring {
            id: ring

            x: root.centerX - ring.width / 2
            y: root.centerY - ring.height / 2
            size: Ui.overlayRing
            glyph: root.playing ? "pause" : "play"
            focused: true
            animated: !root.screenOff
        }

        Glyph {
            id: next

            x: root.centerX + root.glyphOffset - next.width / 2
            y: root.centerY - next.height / 2
            size: Ui.overlayGlyph
            name: "next"
            color: Ui.textFull
        }

        Text {
            id: volumeLabel

            x: root.centerX - volumeLabel.width / 2
            y: minus.y + minus.height
            text: "volume"
            color: Ui.textFull
            font: Ui.font(Ui.overlayLabel)
        }

        Text {
            id: titleText

            x: root.edge
            y: root.height - Ui.px(Ui.overlayBottomMargin) - titleText.height
            width: root.width - 2 * root.edge
            clip: true
            text: root.title
            color: Ui.white(Ui.toastTitle.alpha)
            font: Ui.font(Ui.toastTitle)
        }

        Text {
            id: artistText

            x: root.edge
            y: titleText.y + titleText.baselineOffset - Ui.px(Ui.overlayTextPitch) - artistText.baselineOffset
            width: root.width - 2 * root.edge
            clip: true
            text: root.artist
            color: Ui.white(Ui.toastArtist.alpha)
            font: Ui.font(Ui.toastArtist)
        }
    }

    Glyph {
        id: minus

        x: root.centerX - minus.width / 2
        y: root.centerY + Ui.px(Ui.overlayMinusOffset) - minus.height / 2
        size: Ui.overlayGlyph
        name: "minus"
        color: Ui.white(Ui.overlayLit)
    }

    NumberAnimation {
        id: showAnim

        target: root
        property: "opacity"
        to: 1
        duration: Motion.volumeShow
        easing.type: Motion.linear
    }

    NumberAnimation {
        id: popAnim

        target: number
        property: "scale"
        from: Motion.toggleScale
        to: 1
        duration: Motion.volumePop
        easing.type: Motion.bezierPop
    }

    Timer {
        id: hideTimer

        interval: Motion.volumeHide
        repeat: false
        onTriggered: fadeAnim.start()
    }

    NumberAnimation {
        id: fadeAnim

        target: root
        property: "opacity"
        to: 0
        duration: Motion.volumeFade
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    function show(level: int) {
        root.level = level
        if (root.screenOff)
            return
        fadeAnim.stop()
        if (root.opacity < 1 && !showAnim.running)
            showAnim.start()
        popAnim.restart()
        hideTimer.restart()
    }

    function dismiss() {
        showAnim.stop()
        popAnim.stop()
        fadeAnim.stop()
        hideTimer.stop()
        number.scale = 1
        root.opacity = 0
    }

    function complete() {
        if (showAnim.running)
            showAnim.complete()
        if (popAnim.running)
            popAnim.complete()
        if (fadeAnim.running)
            fadeAnim.complete()
    }

    onScreenOffChanged: {
        if (root.screenOff)
            root.dismiss()
    }
}
