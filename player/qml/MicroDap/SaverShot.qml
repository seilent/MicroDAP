pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool dim: false
    property string fixture: "browse"
    readonly property bool phone: root.fixture === "phone-active"

    Saver {
        id: saver

        anchors.fill: parent
        fixedClock: "7:13"
        phone: root.phone
        phoneArtUrl: root.phone && typeof BluezModel !== "undefined" && BluezModel !== null ? BluezModel.artUrl : ""
        title: root.phone ? "Track Four" : "Sticking With You"
        artist: root.phone ? "Phone Artist" : "Addison Road"
        album: root.phone ? "from Pixel 8" : "Addison Road"
        Component.onCompleted: {
            saver.show()
            saver.dimmed = root.dim
            saver.land()
        }
    }
}
