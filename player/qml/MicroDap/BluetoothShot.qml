pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    readonly property var contextBluez: typeof BluezModel === "undefined" ? null : BluezModel
    readonly property bool pairingPreset: root.contextBluez !== null && root.contextBluez.pairing === true

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    Bluetooth {
        id: screen

        anchors.fill: parent
        animated: false
        bluez: root.contextBluez
        pivotIndex: root.pairingPreset ? 1 : 0

        Component.onCompleted: screen.completeAll()
    }
}
