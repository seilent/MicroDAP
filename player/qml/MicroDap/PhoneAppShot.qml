pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "app-idle"
    readonly property var contextBluez: typeof BluezModel === "undefined" ? null : BluezModel

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: shotLink
    }

    PhoneAppPairing {
        id: screen

        anchors.fill: parent
        animated: false
        autoOpen: false
        bluez: root.contextBluez
        link: shotLink
        onToast: (first, second) => toast.show(first, second)

        Component.onCompleted: {
            if (root.fixture === "app-paired" || root.fixture === "app-bound")
                screen.paired = true
            if (root.fixture === "app-connected")
                screen.connected = true
            screen.completeAll()
            if (root.fixture === "app-refused") {
                screen.toast("phone app", "pairing refused")
                toast.complete()
            }
            if (root.fixture === "app-bound") {
                screen.toast("phone app", "Pixel 8")
                toast.complete()
            }
        }
    }

    Toast {
        id: toast

        x: 0
        y: 0
        width: root.width
    }
}
