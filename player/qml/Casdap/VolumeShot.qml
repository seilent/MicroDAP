pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    VolumeOverlay {
        id: volume

        anchors.fill: parent
        playing: true
        artist: "artist"
        title: "Song title"
        Component.onCompleted: {
            volume.show(13)
            volume.complete()
            volume.hide.stop()
        }
    }
}
