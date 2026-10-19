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

        readonly property int battery: root.fixture === "percent-low" ? 5 : 73
        readonly property bool batteryPresent: true
        readonly property string clock: root.fixture === "clock-12h" ? "7:30" : root.fixture.startsWith("clock") || root.fixture.startsWith("percent") ? "19:30" : ""
        readonly property bool batteryPercent: root.fixture.startsWith("percent")
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
        readonly property var current: Object.assign({ rating: 0 }, sampleQueue.rows[0], root.fixture === "outside-library" ? { id: 0 } : {})
        readonly property QtObject queue: sampleQueue
    }

    NowPlaying {
        id: screen

        anchors.fill: parent
        animated: root.fixture === "track-info"
        player: samplePlayer
        bluez: root.contextBluez
        status: sampleStatus

        Component.onCompleted: {
            screen.completeAll()
            screen.pressed = root.fixture === "pressed"
            if (root.fixture === "seek")
                screen.seekAt(Ui.px(Ui.npSeekMargin) + screen.progressWidth * 0.6)
            if (root.fixture === "track-info")
                Qt.callLater(root.infoFixture)
        }
    }

    function infoFixture(): void {
        const parts = ["Music", "Addison Road", "Addison Road", "01 Sticking With You.flac"]
        const width = screen.width - 2 * Ui.px(Ui.edge)
        const path = Env.fitPath(parts, screen.sheet.plainFont, width)
        screen.sheet.showLines(screen.titleItem.text, ["flac", "44100 hz", "16 bit", "912 kbps", "27.4 mb", path], 5)
        screen.completeAll()
        screen.sheet.complete()
    }
}
