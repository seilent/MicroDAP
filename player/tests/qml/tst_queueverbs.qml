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
        property var edits: []
        property string refuse: ""

        function sourceTracks(kind, id) {
            return kind === "album" && id === 12 ? [21, 22] : kind === "artist" && id === 7 ? [21, 22, 23] : []
        }

        function editablePlaylists() {
            return [{ id: 4, name: "Road", source: "/home/micro/Music/Playlists/Road.m3u8" }]
        }

        function editable(id) {
            return id === 4
        }

        function addToPlaylist(id, ids) {
            fakeLibrary.edits = fakeLibrary.edits.concat([{ verb: "add", id: id, ids: ids }])
            return fakeLibrary.refuse
        }

        function createPlaylist(ids) {
            fakeLibrary.edits = fakeLibrary.edits.concat([{ verb: "create", ids: ids }])
            return { name: "playlist 1", id: 9, error: fakeLibrary.refuse }
        }

        function deletePlaylist(id) {
            fakeLibrary.edits = fakeLibrary.edits.concat([{ verb: "delete", id: id }])
            return fakeLibrary.refuse
        }

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
        signal trackStarted(int index)

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

    QtObject {
        id: fakeSettings

        property string output: "auto"
        property string btCodec: "auto"
        property var roots: []
        property real scale: 1.0
        property int saverSeconds: 30
        property bool overlayToasts: false
        property int screenOffSeconds: 0
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
        property bool autoNowPlaying: false
    }

    Main {
        id: main

        library: fakeLibrary
        player: fakePlayer
        settings: fakeSettings
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
        fakeLibrary.edits = []
        fakeLibrary.refuse = ""
        tc.toast.hide()
        fakeSettings.autoNowPlaying = false
        fakePlayer.status = 0
        fakePlayer.current = {}
    }

    function home() {
        while (main.pageStack.depth > 1)
            main.pageStack.pop()
        main.pageStack.completeAll()
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

    function test_auto_now_playing_from_a_list() {
        fakePlayer.current = { id: 1, title: "Song 1", artist: "Addison Road" }
        fakePlayer.status = main.statusPlaying
        main.listPlayed(3)
        fakePlayer.trackStarted(0)
        compare(main.topScreen, "home")
        fakeSettings.autoNowPlaying = true
        main.listPlayed(3)
        compare(main.topScreen, "home")
        fakePlayer.trackStarted(0)
        main.pageStack.completeAll()
        compare(main.topScreen, "nowplaying")
        tc.home()
    }

    function test_auto_now_playing_only_after_its_play() {
        fakeSettings.autoNowPlaying = true
        fakePlayer.current = { id: 1, title: "Song 1", artist: "Addison Road" }
        main.listPlayed(0)
        fakePlayer.trackStarted(0)
        compare(main.topScreen, "home")
        main.listPlayed(2)
        fakePlayer.trackStarted(0)
        main.pageStack.completeAll()
        compare(main.topScreen, "nowplaying")
        tc.home()
        fakePlayer.trackStarted(1)
        compare(main.topScreen, "home")
        main.listPlayed(2)
        main.navigate("music")
        main.pageStack.completeAll()
        fakePlayer.trackStarted(0)
        compare(main.topScreen, "music")
        tc.home()
        fakePlayer.trackStarted(0)
        compare(main.topScreen, "home")
    }

    function test_auto_now_playing_not_from_a_sheet() {
        fakeSettings.autoNowPlaying = true
        fakePlayer.current = { id: 1, title: "Song 1", artist: "Addison Road" }
        fakePlayer.status = main.statusPlaying
        main.sheetAction("play", "album", 12)
        main.playEntry({ kind: "album", id: 12 })
        compare(main.topScreen, "home")
    }

    function hubSheet(title, items) {
        if (main.pageStack.currentItem.objectName !== "music")
            main.navigate("music")
        main.pageStack.completeAll()
        const hub = main.pageStack.currentItem
        compare(hub.objectName, "music")
        hub.sheet.title = title
        hub.sheet.items = items
        hub.sheet.open()
        hub.sheet.complete()
        return hub
    }

    function test_add_to_playlist_second_sheet() {
        const hub = tc.hubSheet("Stories", hub_items())
        main.sheetAction("add to playlist", "album", 12)
        compare(hub.sheet.depth, 1)
        compare(hub.sheet.title, "Stories")
        compare(JSON.stringify(hub.sheet.items), JSON.stringify(["new playlist", "Road"]))
        hub.sheet.focusIndex = 1
        verify(main.router.dispatch("a", false))
        compare(hub.sheet.shown, false)
        compare(fakeLibrary.edits.length, 1)
        compare(fakeLibrary.edits[0].verb, "add")
        compare(fakeLibrary.edits[0].id, 4)
        compare(JSON.stringify(fakeLibrary.edits[0].ids), JSON.stringify([21, 22]))
        compare(tc.toast.artist, "added to Road")
        compare(tc.toast.title, "Stories")
        tc.home()
    }

    function hub_items() {
        return ["play", "play next", "add to queue", "shuffle", "add to playlist", "pin to quickplay"]
    }

    function test_add_to_playlist_new_and_back() {
        const hub = tc.hubSheet("Song 5", ["play", "play next", "add to queue", "add to playlist"])
        main.sheetAction("add to playlist", "song", 5)
        compare(hub.sheet.depth, 1)
        verify(main.router.dispatch("b", false))
        compare(hub.sheet.depth, 0)
        compare(hub.sheet.shown, true)
        compare(hub.sheet.title, "Song 5")
        main.sheetAction("add to playlist", "song", 5)
        verify(main.router.dispatch("a", false))
        compare(fakeLibrary.edits[0].verb, "create")
        compare(JSON.stringify(fakeLibrary.edits[0].ids), JSON.stringify([5]))
        compare(tc.toast.artist, "added to playlist 1")
        tc.toast.hide()
        tc.hubSheet("Song 5", ["play"])
        fakeLibrary.refuse = "card not available"
        main.sheetAction("add to playlist", "song", 5)
        hub.sheet.focusIndex = 1
        verify(main.router.dispatch("a", false))
        compare(tc.toast.artist, "")
        compare(tc.toast.title, "card not available")
        tc.toast.hide()
        tc.hubSheet("Song 5", ["play"])
        fakeLibrary.refuse = "playlist not readable"
        main.sheetAction("add to playlist", "song", 5)
        hub.sheet.focusIndex = 1
        verify(main.router.dispatch("a", false))
        compare(tc.toast.artist, "playlist not readable")
        compare(tc.toast.title, "Road")
        tc.home()
    }

    function test_add_to_playlist_items_on_sheets() {
        main.navigate("music")
        main.pageStack.completeAll()
        const hub = main.pageStack.currentItem
        for (const kind of ["song", "album", "artist", "genre", "folder"])
            verify(hub.sheetItemsFor(kind, 1).indexOf("add to playlist") >= 0, kind)
        for (const kind of ["playlist", "favorites"])
            verify(hub.sheetItemsFor(kind, 4).indexOf("add to playlist") < 0, kind)
        compare(hub.sheetItemsFor("album", 1).indexOf("add to playlist"), hub.sheetItemsFor("album", 1).indexOf("shuffle") + 1)
        verify(hub.sheetItemsFor("playlist", 4).indexOf("delete playlist") >= 0)
        verify(hub.sheetItemsFor("playlist", 5).indexOf("delete playlist") < 0)
        tc.home()
    }

    function test_delete_playlist_confirm() {
        main.navigate("music")
        main.pageStack.completeAll()
        const hub = main.pageStack.currentItem
        hub.confirmDelete({ kind: "playlist", id: 4, title: "Road" })
        hub.deleteDialog.complete()
        compare(hub.deleteDialog.shown, true)
        compare(hub.deleteDialog.title, "delete playlist?")
        compare(JSON.stringify(hub.deleteDialog.items), JSON.stringify(["delete playlist", "keep playlist"]))
        compare(hub.deleteDialog.focusIndex, 1)
        verify(main.router.dispatch("a", false))
        compare(hub.deleteDialog.shown, false)
        compare(fakeLibrary.edits.length, 0)
        hub.confirmDelete({ kind: "playlist", id: 4, title: "Road" })
        verify(main.router.dispatch("b", false))
        compare(fakeLibrary.edits.length, 0)
        hub.confirmDelete({ kind: "playlist", id: 4, title: "Road" })
        verify(main.router.dispatch("up", false))
        verify(main.router.dispatch("a", false))
        compare(fakeLibrary.edits.length, 1)
        compare(fakeLibrary.edits[0].verb, "delete")
        compare(fakeLibrary.edits[0].id, 4)
        tc.home()
    }
}
