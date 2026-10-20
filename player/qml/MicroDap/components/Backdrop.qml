pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property url source
    property real focusX: 0.5
    property real focusY: 0.5
    readonly property bool hasArt: root.source.toString() !== ""
    readonly property real side: Math.max(root.width, root.height)
    readonly property var stops: Ui.backdropStops
    property alias art: art
    property alias shade: shade

    clip: true

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
        visible: root.hasArt
    }

    Image {
        id: art

        x: Math.min(0, Math.max(root.width - root.side, root.width / 2 - root.focusX * root.side))
        y: Math.min(0, Math.max(root.height - root.side, root.height / 2 - root.focusY * root.side))
        width: root.side
        height: root.side
        source: root.source
        visible: root.hasArt
        fillMode: Image.PreserveAspectCrop
        sourceSize: Qt.size(Math.ceil(art.width), Math.ceil(art.height))
        asynchronous: true
        cache: true
        mipmap: false
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
}
