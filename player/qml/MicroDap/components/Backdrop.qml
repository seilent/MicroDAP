pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property url source
    readonly property bool hasArt: root.source.toString() !== ""
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

        x: 0
        y: 0
        width: root.width
        height: root.height
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
