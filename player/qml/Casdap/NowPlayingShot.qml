pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "browse"
    readonly property var contextBluez: typeof BluezModel === "undefined" ? null : BluezModel

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: sampleStatus

        readonly property int battery: 73
        readonly property bool batteryPresent: true
        readonly property string clock: ""
    }

    QtObject {
        id: sampleQueue

        readonly property var rows: [
            { id: 1, title: "This Could Be Our Day", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 233000 },
            { id: 2, title: "All That Matters", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 221000 },
            { id: 3, title: "Sticking With You", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 198000 },
            { id: 4, title: "Hope Now", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 245000 },
            { id: 5, title: "Casimir Pulaski Day", artist: "Sufjan Stevens", album: "Illinois", artPath: "", durationMs: 356000 }
        ]
        readonly property int count: sampleQueue.rows.length
        readonly property int index: 0
        readonly property bool shuffle: false
        readonly property int repeat: 0

        function get(row: int): var {
            return sampleQueue.rows[row]
        }
    }

    QtObject {
        id: samplePlayer

        readonly property int status: 1
        readonly property real position: 5000
        readonly property real duration: 233000
        readonly property var current: Object.assign({ rating: 0 }, sampleQueue.rows[0])
        readonly property QtObject queue: sampleQueue
    }

    NowPlaying {
        id: screen

        anchors.fill: parent
        animated: false
        player: samplePlayer
        bluez: root.contextBluez
        status: sampleStatus

        Component.onCompleted: {
            screen.completeAll()
            screen.pressed = root.fixture === "pressed"
        }
    }
}
