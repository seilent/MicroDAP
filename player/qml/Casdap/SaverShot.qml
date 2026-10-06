pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool dim: false

    Saver {
        id: saver

        anchors.fill: parent
        fixedClock: "7:13"
        title: "Sticking With You"
        artist: "Addison Road"
        album: "Addison Road"
        Component.onCompleted: {
            saver.show()
            saver.dimmed = root.dim
            saver.land()
        }
    }
}
