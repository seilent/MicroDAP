pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property url source
    property bool paused: false
    property bool screenOff: false
    readonly property bool hasArt: root.source.toString() !== ""
    readonly property bool holding: root.paused || root.screenOff || !root.visible || !root.hasArt
    readonly property var stops: Ui.backdropStops
    readonly property real amplitude: Ui.px(Motion.backdropPanAmplitude)
    property alias art: art
    property alias shade: shade
    readonly property bool animating: scaleLoop.running && !scaleLoop.paused
        && panLoop.running && !panLoop.paused

    clip: true

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
        visible: root.hasArt
    }

    Image {
        id: art

        property real pan: 0

        x: -root.amplitude + art.pan
        y: 0
        width: root.width + 2 * root.amplitude
        height: root.height
        source: root.source
        visible: root.hasArt
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(Math.ceil(art.width), Math.ceil(art.height))
        asynchronous: true
        cache: true
        mipmap: false
        transformOrigin: Item.Center
        scale: 1.0
    }

    Rectangle {
        id: shade

        anchors.fill: parent
        visible: root.hasArt
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: root.stops[0][0]; color: Ui.bgAlpha(root.stops[0][1]) }
            GradientStop { position: root.stops[1][0]; color: Ui.bgAlpha(root.stops[1][1]) }
            GradientStop { position: root.stops[2][0]; color: Ui.bgAlpha(root.stops[2][1]) }
        }
    }

    SequentialAnimation {
        id: scaleLoop

        running: true
        loops: Animation.Infinite
        paused: root.holding

        NumberAnimation {
            target: art
            property: "scale"
            from: 1.0
            to: Motion.backdropScaleMax
            duration: Motion.backdropScale
            easing.type: Motion.inOutSine
        }
        NumberAnimation {
            target: art
            property: "scale"
            from: Motion.backdropScaleMax
            to: 1.0
            duration: Motion.backdropScale
            easing.type: Motion.inOutSine
        }
    }

    SequentialAnimation {
        id: panLoop

        running: true
        loops: Animation.Infinite
        paused: root.holding

        NumberAnimation {
            target: art
            property: "pan"
            from: -root.amplitude
            to: root.amplitude
            duration: Motion.backdropPan
            easing.type: Motion.inOutSine
        }
        NumberAnimation {
            target: art
            property: "pan"
            from: root.amplitude
            to: -root.amplitude
            duration: Motion.backdropPan
            easing.type: Motion.inOutSine
        }
    }

    function restart() {
        scaleLoop.restart()
        panLoop.restart()
        if (root.holding) {
            scaleLoop.pause()
            panLoop.pause()
        }
    }

    Component.onCompleted: {
        if (root.holding) {
            scaleLoop.pause()
            panLoop.pause()
        }
    }
}
