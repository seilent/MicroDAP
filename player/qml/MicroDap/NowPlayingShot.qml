pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "browse"
    readonly property bool lyricsFixture: root.fixture.startsWith("lyrics") || root.fixture.startsWith("lyricslide")
    readonly property bool markFixture: root.fixture.startsWith("mark")
    readonly property var slideParts: root.fixture.startsWith("lyricslide-") ? root.fixture.split("-") : []
    readonly property string lyricsKind: root.slideParts.length === 3 ? root.slideParts[1]
        : root.fixture.startsWith("lyrics-") ? root.fixture.substring(7)
        : root.fixture === "mark-pending" ? "pending" : ""
    readonly property var contextBluez: typeof BluezModel === "undefined" ? null : BluezModel

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: samplePhotos

        readonly property int revision: 0
        readonly property bool fallback: root.fixture === "photo-fallback-sheet" || root.fixture === "photo-artists-sheet"

        function photoArtist(names: var): string {
            return !samplePhotos.fallback && Env.shotPhoto !== "" && names.length > 0 ? String(names[0]) : ""
        }

        function photoFor(names: var): url {
            return samplePhotos.photoArtist(names) === "" ? "" : "file://" + Env.shotPhoto
        }

        function photoFocus(names: var): point {
            return root.fixture === "photo-focus" ? Qt.point(0.25, 0.3) : Qt.point(0.5, 0.5)
        }

        function photoInfo(name: string): var {
            const credit = root.fixture === "photo-sheet" ? "Jane Roe"
                : root.fixture === "photo-long-sheet" ? "Wikimedia Commons user Somebody With A Very Long Handle, CC BY-SA 4.0" : ""
            const source = root.fixture === "photo-source-sheet" ? "deezer" : "theaudiodb"
            return { name: name, mbid: "", source: source, credit: credit, crc: 1, wrong: true, phone: true }
        }

        function touchName(name: string): void {
        }
    }

    QtObject {
        id: sampleLink

        readonly property bool connected: true

        function canAsk(what: string): bool {
            return true
        }
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

        readonly property var all: [
            { id: 1, title: "This Could Be Our Day", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 233000 },
            { id: 2, title: root.fixture === "mark-longnext" ? "All That Matters When The Lights Go Down Over The City" : "All That Matters", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 221000 },
            { id: 3, title: "Sticking With You", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 198000 },
            { id: 4, title: "Hope Now", artist: "Addison Road", album: "Addison Road", artPath: "", durationMs: 245000 },
            { id: 5, title: "Casimir Pulaski Day", artist: "Sufjan Stevens", album: "Illinois", artPath: "", durationMs: 356000 }
        ]
        readonly property var rows: root.fixture === "mark-nonext" ? sampleQueue.all.slice(0, 1) : sampleQueue.all
        readonly property int count: sampleQueue.rows.length
        readonly property int index: 0
        readonly property bool shuffle: false
        readonly property int repeat: 0

        function get(row: int): var {
            return sampleQueue.rows[row]
        }
    }

    QtObject {
        id: sampleLyrics

        readonly property bool pending: root.lyricsKind === "pending"
        readonly property var short: [
            { ms: 0, text: "Under the paper lanterns" },
            { ms: 4000, text: "We counted every light" },
            { ms: 9000, text: "The river kept on moving" },
            { ms: 13000, text: "" },
            { ms: 15000, text: "And carried half the night" },
            { ms: 19000, text: "Somebody called your name out" },
            { ms: 23000, text: "Across the empty square" },
            { ms: 27000, text: "I turned around to answer" },
            { ms: 31000, text: "But no one else was there" }
        ]
        readonly property var long: [
            { ms: 0, text: "Under the paper lanterns" },
            { ms: 4000, text: "We counted every light that hung above the water like a promise" },
            { ms: 9000, text: "The river kept on moving through the town and past the station where the last train waits" },
            { ms: 13000, text: "And carried half the night" },
            { ms: 17000, text: "Somebody called your name out" },
            { ms: 21000, text: "Across the empty square" }
        ]
        readonly property var cjk: [
            { ms: 0, text: "夜空に浮かぶ星を数えて" },
            { ms: 4000, text: "君の名前を呼んだ夏の終わりに静かな風が吹いていた" },
            { ms: 9000, text: "川は流れ続ける" },
            { ms: 13000, text: "遠い街の灯りまで" },
            { ms: 17000, text: "誰もいない広場で" }
        ]
        readonly property var before: [
            { ms: 8000, text: "Under the paper lanterns" },
            { ms: 12000, text: "We counted every light" },
            { ms: 16000, text: "The river kept on moving" },
            { ms: 20000, text: "And carried half the night" }
        ]
        readonly property var plainText: [
            "Under the paper lanterns", "We counted every light", "The river kept on moving",
            "And carried half the night", "", "Somebody called your name out across the empty square",
            "I turned around to answer", "But no one else was there", "", "Under the paper lanterns",
            "We counted every light", "The river kept on moving", "And carried half the night"
        ]
        readonly property var lines: root.lyricsKind === "long" ? sampleLyrics.long
            : root.lyricsKind === "cjk" ? sampleLyrics.cjk
            : root.lyricsKind === "before" ? sampleLyrics.before
            : root.lyricsKind === "plain" || root.lyricsKind === "none" || root.lyricsKind === "pending" ? []
            : sampleLyrics.short
        readonly property var plain: root.lyricsKind === "plain" ? sampleLyrics.plainText : []

        signal changed()
    }

    QtObject {
        id: samplePlayer

        readonly property int status: 1
        readonly property real position: 5000
        readonly property real duration: 233000
        readonly property var current: Object.assign({ rating: 0 }, sampleQueue.rows[0], root.fixture === "outside-library" ? { id: 0 } : {},
            root.fixture === "photo-artists-sheet" ? { artist: "Addison Road, Sufjan Stevens", artists: ["Addison Road", "Sufjan Stevens"] } : {})
        readonly property QtObject queue: sampleQueue
    }

    NowPlaying {
        id: screen

        anchors.fill: parent
        animated: root.fixture === "track-info" || root.fixture.endsWith("-sheet")
        player: samplePlayer
        bluez: root.contextBluez
        status: sampleStatus
        lyrics: root.lyricsFixture || root.markFixture ? sampleLyrics : null
        photos: root.fixture.startsWith("photo") ? samplePhotos : null
        link: root.fixture.startsWith("photo") ? sampleLink : null

        Component.onCompleted: {
            screen.completeAll()
            screen.pressed = root.fixture === "pressed"
            if (root.fixture === "seek")
                screen.seekAt(Ui.px(Ui.npSeekMargin) + screen.progressWidth * 0.6)
            if (root.fixture === "track-info")
                Qt.callLater(root.infoFixture)
            if (root.lyricsFixture)
                root.lyricsShot()
            if (root.fixture.endsWith("-sheet")) {
                screen.openArtSheet()
                screen.sheet.complete()
            }
            if (root.fixture === "photo-artists-sheet") {
                screen.artChosen(0)
                screen.sheet.complete()
            }
            if (root.fixture === "photo-toast") {
                shotToast.show("image blocked", "Addison Road")
                shotToast.complete()
            }
        }
    }

    function lyricsShot(): void {
        screen.lyricsMode = true
        screen.landLyrics()
        if (root.slideParts.length !== 3)
            return
        screen.lyricsColumn.from = 1
        screen.lyricsColumn.to = 2
        screen.lyricsColumn.p = Motion.expOut6(Number(root.slideParts[2]) / 100)
    }

    function infoFixture(): void {
        const parts = ["Music", "Addison Road", "Addison Road", "01 Sticking With You.flac"]
        const width = screen.width - 2 * Ui.px(Ui.edge)
        const path = Env.fitPath(parts, screen.sheet.plainFont, width)
        screen.sheet.showLines(screen.titleItem.text, ["flac", "44100 hz", "16 bit", "912 kbps", "27.4 mb", path], 5)
        screen.completeAll()
        screen.sheet.complete()
    }

    Toast {
        id: shotToast

        x: 0
        y: 0
        width: root.width
    }
}
