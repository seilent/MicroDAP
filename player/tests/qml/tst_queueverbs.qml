import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "QueueVerbs"
    when: windowShown
    visible: true
    width: 320
    height: 240

    QtObject {
        id: fakeLibrary

        property int playlistCount: 0
        property var all: [1, 2, 3, 4, 5]

        function albumInfo(id) {
            return id === 12 ? { id: 12, title: "Stories", artist: "Addison Road", year: 0, artPath: "" } : {}
        }

        function artistInfo(id) {
            return id === 7 ? { id: 7, name: "Addison Road", artPath: "" } : {}
        }

        function playlistInfo(id) {
            return id === 4 ? { id: 4, name: "Road", count: 2 } : {}
        }

        function trackRows(ids) {
            return ids.map((i) => ({ id: i, title: "Song " + i, artist: "Addison Road" }))
        }

        function isPinned(kind, id) {
            return false
        }

        function sizeOf(kind, id) {
            switch (kind) {
            case "album":
                return id === 12 ? 2 : 0
            case "artist":
                return id === 7 ? 3 : 0
            case "genre":
                return id === 3 ? 1 : 0
            case "playlist":
                return id === 4 ? 2 : 0
            case "song":
                return 1
            case "all":
                return fakeLibrary.all.length
            default:
                return 0
            }
        }
    }

    QtObject {
        id: fakeQueue

        property int count: 0
        property int index: -1
        property bool shuffle: false
        property int repeat: 0

        function get(row) {
            return { id: row + 1, title: "Song " + (row + 1) }
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 0
        property var current: ({})
        property QtObject queue: fakeQueue
        property var calls: []

        signal added(int count)

        function playSource(kind, id, mode, startId) {
            fakePlayer.calls = fakePlayer.calls.concat([{ kind: kind, id: id, mode: mode, startId: startId }])
            const n = fakeLibrary.sizeOf(kind, id)
            if (n > 0 && (mode === "add" || mode === "next"))
                fakePlayer.added(n)
            if (n > 0 && mode === "shuffle")
                fakeQueue.shuffle = true
            return n
        }
    }

    Main {
        id: main

        library: fakeLibrary
        player: fakePlayer
        shotWidth: 640
        shotHeight: 960
    }

    readonly property var toast: main.overlayLayer.toast

    function init() {
        tryCompare(main.pageStack, "inputLocked", false, 5000)
        main.pageStack.completeAll()
        fakePlayer.calls = []
        fakeQueue.shuffle = false
        fakeLibrary.all = [1, 2, 3, 4, 5]
        tc.toast.hide()
    }

    function last() {
        return fakePlayer.calls[fakePlayer.calls.length - 1]
    }

    function test_add_to_queue_album_shows_toast() {
        main.sheetAction("add to queue", "album", 12)
        compare(fakePlayer.calls.length, 1)
        compare(tc.last().kind, "album")
        compare(tc.last().id, 12)
        compare(tc.last().mode, "add")
        compare(tc.toast.shown, true)
        compare(tc.toast.artist, "added 2 songs")
        compare(tc.toast.title, "Stories")
        compare(main.addSource, "")
    }

    function test_play_next_song() {
        main.sheetAction("play next", "song", 5)
        compare(tc.last().kind, "song")
        compare(tc.last().id, 5)
        compare(tc.last().mode, "next")
        compare(tc.toast.artist, "added 1 song")
        compare(tc.toast.title, "Song 5")
    }

    function test_add_kinds_resolve() {
        main.sheetAction("add to queue", "artist", 7)
        compare(tc.last().kind, "artist")
        compare(tc.last().mode, "add")
        compare(tc.toast.artist, "added 3 songs")
        compare(tc.toast.title, "Addison Road")
        tc.toast.hide()
        main.sheetAction("add to queue", "genre", 3)
        compare(tc.last().kind, "genre")
        compare(tc.last().id, 3)
        compare(tc.toast.artist, "added 1 song")
        tc.toast.hide()
        main.sheetAction("play next", "playlist", 4)
        compare(tc.last().mode, "next")
        compare(tc.last().kind, "playlist")
        compare(tc.toast.title, "Road")
    }

    function test_nothing_to_add_shows_nothing() {
        main.sheetAction("add to queue", "genre", 99)
        compare(fakePlayer.calls.length, 1)
        compare(tc.toast.shown, false)
        compare(main.addSource, "")
    }

    function test_play_collections() {
        main.sheetAction("play", "album", 12)
        compare(tc.last().kind, "album")
        compare(tc.last().mode, "play")
        compare(tc.last().startId, -1)
        main.sheetAction("play", "artist", 7)
        compare(tc.last().kind, "artist")
        main.sheetAction("play", "genre", 3)
        compare(tc.last().kind, "genre")
        main.sheetAction("play", "playlist", 4)
        compare(tc.last().kind, "playlist")
        compare(fakePlayer.calls.length, 4)
        compare(tc.toast.shown, false)
    }

    function test_shuffle_verb_plays_shuffled() {
        main.sheetAction("shuffle", "artist", 7)
        compare(fakePlayer.calls.length, 1)
        compare(tc.last().kind, "artist")
        compare(tc.last().id, 7)
        compare(tc.last().mode, "shuffle")
        compare(fakeQueue.shuffle, true)
        main.sheetAction("shuffle", "album", 12)
        compare(fakePlayer.calls.length, 2)
        compare(tc.last().mode, "shuffle")
    }

    function test_home_y_shuffles_library() {
        compare(main.pageStack.currentItem.objectName, "home")
        verify(main.router.dispatch("y", false))
        compare(fakePlayer.calls.length, 1)
        compare(tc.last().kind, "all")
        compare(tc.last().mode, "shuffle")
        compare(tc.last().startId, -1)
        compare(fakeQueue.shuffle, true)
    }

    function test_shuffle_all_empty_library() {
        fakeLibrary.all = []
        main.shuffleAll()
        compare(fakePlayer.calls.length, 1)
        compare(fakeQueue.shuffle, false)
        compare(tc.toast.shown, false)
    }

    function test_quickplay_entry_plays_source() {
        main.playEntry({ kind: "album", id: 12 })
        compare(tc.last().kind, "album")
        compare(tc.last().id, 12)
        compare(tc.last().mode, "play")
    }
}
