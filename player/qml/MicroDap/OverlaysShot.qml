pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    readonly property real bandTop: Ui.px(120)
    readonly property real toastBand: Ui.px(Ui.toastHeight + 8)
    readonly property real lowerTop: root.bandTop + root.toastBand
    readonly property real lowerHeight: root.height - root.lowerTop
    readonly property real splitX: Math.round(root.width * 0.6)
    readonly property real dialogHeight: Math.round(root.lowerHeight * 0.6)
    readonly property real volumeW: Math.max(root.splitX, Ui.px(Ui.minCanvasW))
    readonly property real volumeH: Math.max(root.lowerHeight, Ui.px(Ui.minCanvasH))
    property alias volumeBox: volumeBox

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    Backdrop {
        id: backdrop

        x: 0
        y: 0
        width: root.width
        height: root.bandTop

        StatusStrip {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            clock: "7:13"
            battery: 9
            bluetooth: true
            playing: true
        }

        JumpLetter {
            id: jump

            anchors.fill: parent
            Component.onCompleted: {
                jump.show("h")
                jump.hold.stop()
            }
        }
    }

    Toast {
        id: toast

        x: 0
        y: root.bandTop
        width: root.width
        Component.onCompleted: {
            toast.show("artist", "Song title")
            toast.complete()
            toast.hold.stop()
        }
    }

    Item {
        id: volumeBox

        x: 0
        y: root.lowerTop
        width: root.volumeW
        height: root.volumeH
        clip: true

        VolumeOverlay {
            id: volume

            anchors.fill: parent
            artist: "artist"
            title: "Song title"
            Component.onCompleted: {
                volume.show(13)
                volume.complete()
                volume.hide.stop()
            }
        }
    }

    Item {
        x: root.splitX
        y: root.lowerTop
        width: root.width - root.splitX
        height: root.dialogHeight
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Ui.tile
        }

        Dialog {
            id: dialog

            anchors.fill: parent
            title: "power"
            items: ["screen off", "game mode"]
            Component.onCompleted: {
                dialog.open()
                dialog.complete()
            }
        }
    }

    Item {
        x: root.splitX
        y: root.lowerTop + root.dialogHeight
        width: root.width - root.splitX
        height: root.lowerHeight - root.dialogHeight
        clip: true

        Rectangle {
            anchors.fill: parent
            color: Ui.tile
        }

        Sheet {
            id: sheet

            anchors.fill: parent
            items: ["remove", "play next"]
            Component.onCompleted: {
                sheet.open()
                sheet.complete()
            }
        }
    }
}
