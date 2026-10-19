import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "PlaylistsHistory"
    when: windowShown
    visible: true
    width: 640
    height: 960

    PageStack {
        id: pages

        width: 640
        height: 960
    }

    PadRouter {
        id: router

        width: 640
        height: 960
        stack: pages
        pad: null
    }

    TouchLayer {
        id: layer

        width: 640
        height: 960
        router: router
    }

    ListModel {
        id: playlists
    }

    ListModel {
        id: recent
    }

    ListModel {
        id: fresh
    }

    QtObject {
        id: fakePlayer

        property var calls: []
        property QtObject queue: QtObject {
            property int count: 12
        }

        function playSource(kind, id, mode, startId) {
            fakePlayer.calls = fakePlayer.calls.concat([{ kind: kind, id: id, mode: mode, startId: startId }])
            return 1
        }
    }

    QtObject {
        id: fakeLibrary

        property var pinned: []
        property int playlistCount: 2

        function isPinned(kind, id) {
            return fakeLibrary.pinned.indexOf(kind + ":" + id) >= 0
        }

        function editable(id) {
            return id === 1
        }
    }

    QtObject {
        id: counters

        property int nowPlaying: 0
        property var played: []
        property var sheetActions: []
        property var navigated: []
    }

    Component {
        id: playlistsComponent

        PlaylistsScreen {
            playlistsModel: playlists
            onNowPlaying: counters.nowPlaying++
            onPlay: (kind, id) => counters.played = counters.played.concat([{ kind: kind, id: id }])
            onSheetAction: (action, kind, id) => counters.sheetActions = counters.sheetActions.concat([{ action: action, kind: kind, id: id }])
        }
    }

    Component {
        id: historyComponent

        HistoryScreen {
            historyModel: recent
            newModel: fresh
            onPlay: (kind, id) => counters.played = counters.played.concat([{ kind: kind, id: id }])
            onSheetAction: (action, kind, id) => counters.sheetActions = counters.sheetActions.concat([{ action: action, kind: kind, id: id }])
        }
    }

    Component {
        id: homeComponent

        Home {
            animated: false
            onNavigate: (name) => counters.navigated = counters.navigated.concat([name])
        }
    }

    Component {
        id: blankComponent

        Page {}
    }

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.setSurface(w, h)
        pages.width = w
        pages.height = h
        router.width = w
        router.height = h
        layer.width = w
        layer.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        pages.clear()
        pages.animate = false
        playlists.clear()
        playlists.append({ id: 1, title: "Morning", subtitle: "3 songs", kind: "playlist" })
        playlists.append({ id: 2, title: "Night Drive", subtitle: "2 songs", kind: "playlist" })
        recent.clear()
        for (let i = 0; i < 7; i++)
            recent.append({ id: 100 + i, title: "Song " + i, subtitle: "Artist " + i, artPath: "", kind: "song" })
        fresh.clear()
        for (let i = 0; i < 3; i++)
            fresh.append({ id: 200 + i, title: "Album " + i, subtitle: "Artist " + i, artPath: "", kind: "album" })
        fakePlayer.calls = []
        fakePlayer.queue.count = 12
        fakeLibrary.pinned = []
        counters.nowPlaying = 0
        counters.played = []
        counters.sheetActions = []
        counters.navigated = []
    }

    function open(component, properties) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        pages.push(blankComponent, {})
        const page = pages.push(component, props)
        page.completeAll()
        return page
    }

    function rowAt(view, index) {
        tryVerify(() => view.itemAtIndex(index) !== null, 1000)
        return view.itemAtIndex(index)
    }

    function test_playlists_rows() {
        const page = open(playlistsComponent, { player: fakePlayer, library: fakeLibrary })
        compare(page.header.text, "playlists")
        compare(page.rows.length, 3)
        compare(page.rows[0].kind, "nowplaying")
        compare(page.rows[0].title, "now playing")
        compare(page.rows[0].subtitle, "12 songs")
        compare(page.rows[1].title, "Morning")
        compare(page.rows[1].subtitle, "3 songs")
        compare(page.countText.text, "2 playlists")
        compare(page.countText.x, Ui.px(20))
        fuzzyCompare(page.countText.y + page.countText.baselineOffset, Ui.px(95), 0.5)
        compare(page.countText.font.pixelSize, Ui.fontPx(11))
        compare(page.countText.font.capitalization, Font.AllUppercase)
        compare(page.listView.y, Ui.px(128))
        compare(page.listView.rowPitch, Ui.px(40))
        const first = tc.rowAt(page.listView, 0)
        compare(first.ring, true)
        compare(first.bold, true)
        compare(first.focused, true)
        compare(first.primaryRole.name, "listPlaylist")
        compare(first.primaryItem.font.styleName, "Bold")
        compare(first.primaryItem.font.capitalization, Font.AllUppercase)
        compare(first.secondaryItem.font.capitalization, Font.AllUppercase)
        const second = tc.rowAt(page.listView, 1)
        compare(second.bold, true)
        compare(second.focused, false)
        compare(page.focusBar.x, Ui.px(12))
        fuzzyCompare(page.focusBar.y, Ui.px(128), 0.01)
        fakePlayer.queue.count = 1
        compare(page.rows[0].subtitle, "1 song")
    }

    function test_playlists_play() {
        const page = open(playlistsComponent, { player: fakePlayer, library: fakeLibrary })
        verify(router.dispatch("a", false))
        compare(counters.nowPlaying, 1)
        compare(fakePlayer.calls.length, 0)
        verify(router.dispatch("down", false))
        compare(page.focusIndex, 1)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "playlist")
        compare(fakePlayer.calls[0].id, 1)
        compare(fakePlayer.calls[0].mode, "play")
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls[1].id, 2)
        compare(router.dispatch("down", false), false)
    }

    function test_playlists_play_without_player() {
        const page = open(playlistsComponent, {})
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.played.length, 1)
        compare(counters.played[0].kind, "playlist")
        compare(counters.played[0].id, 1)
    }

    function test_playlists_sheet() {
        fakeLibrary.pinned = ["playlist:2"]
        const page = open(playlistsComponent, { player: fakePlayer, library: fakeLibrary })
        compare(router.dispatch("x", false), true)
        compare(page.sheet.shown, false)
        router.dispatch("down", false)
        router.dispatch("x", false)
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "Morning")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "shuffle", "pin to quickplay", "delete playlist"]))
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(page.sheet.shown, false)
        compare(counters.sheetActions[0].action, "pin to quickplay")
        compare(counters.sheetActions[0].kind, "playlist")
        compare(counters.sheetActions[0].id, 1)
        router.dispatch("down", false)
        router.dispatch("x", false)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "shuffle", "unpin from quickplay"]))
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        router.dispatch("y", false)
        compare(counters.sheetActions[1].action, "add to queue")
        compare(counters.sheetActions[1].id, 2)
    }

    function test_playlists_delete_confirm() {
        const page = open(playlistsComponent, { player: fakePlayer, library: fakeLibrary })
        router.dispatch("down", false)
        router.dispatch("x", false)
        const at = page.sheet.items.indexOf("delete playlist")
        compare(at, page.sheet.items.length - 1)
        page.sheet.focusIndex = at
        verify(router.dispatch("a", false))
        compare(page.sheet.shown, false)
        page.deleteDialog.complete()
        compare(page.deleteDialog.shown, true)
        compare(page.deleteDialog.title, "delete playlist?")
        compare(JSON.stringify(page.deleteDialog.items), JSON.stringify(["delete playlist", "keep playlist"]))
        compare(page.deleteDialog.focusIndex, 1)
        verify(router.dispatch("a", false))
        compare(page.deleteDialog.shown, false)
        compare(counters.sheetActions.length, 0)
        router.dispatch("x", false)
        page.sheet.focusIndex = at
        router.dispatch("a", false)
        router.dispatch("up", false)
        router.dispatch("a", false)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "delete playlist")
        compare(counters.sheetActions[0].id, 1)
        router.dispatch("down", false)
        router.dispatch("x", false)
        compare(page.sheet.items.indexOf("delete playlist"), -1)
        router.dispatch("b", false)
        compare(page.sheet.shown, false)
    }

    function test_playlists_model_changes_and_b() {
        const page = open(playlistsComponent, { player: fakePlayer })
        playlists.append({ id: 3, title: "Harmonics", subtitle: "9 songs", kind: "playlist" })
        compare(page.rows.length, 4)
        compare(page.countText.text, "3 playlists")
        playlists.clear()
        compare(page.rows.length, 1)
        compare(page.countText.text, "0 playlists")
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_history_layout() {
        const page = open(historyComponent, { player: fakePlayer, library: fakeLibrary })
        compare(page.header.text, "history")
        compare(JSON.stringify(page.pivots), JSON.stringify(["recent", "new"]))
        compare(page.pivotIndex, 0)
        compare(page.recentPane.count, 7)
        compare(page.newPane.count, 3)
        compare(page.recentPane.model, recent)
        compare(page.newPane.model, fresh)
        compare(page.entryAt(0, 0).kind, "song")
        compare(page.entryAt(0, 0).title, "Song 0")
        compare(page.entryAt(0, 0).subtitle, "Artist 0")
        compare(page.entryAt(1, 0).kind, "album")
        compare(page.recentPane.y, Ui.px(128))
        compare(page.recentPane.rowPitch, Ui.px(40))
        const row = tc.rowAt(page.recentPane, 0)
        const item = row.rowItem
        compare(item.number, "")
        compare(item.ring, false)
        compare(item.bold, false)
        compare(item.primaryItem.x, Ui.px(20))
        compare(item.secondaryItem.x, Ui.px(20))
        compare(item.primaryItem.text, "Song 0")
        compare(item.secondaryItem.text, "Artist 0")
        compare(item.primaryItem.font.pixelSize, Ui.fontPx(14))
        compare(item.secondaryItem.font.pixelSize, Ui.fontPx(11))
        compare(item.focused, true)
        compare(tc.rowAt(page.recentPane, 1).rowItem.focused, false)
        compare(page.focusBar.x, Ui.px(12))
        fuzzyCompare(page.focusBar.y, Ui.px(128), 0.01)
        compare(page.focusBar.rowHeight, Ui.px(40))
        tc.setWindow(1920, 1080)
        compare(page.recentPane.y, Ui.px(98))
    }

    function test_history_navigation_and_play() {
        const page = open(historyComponent, { player: fakePlayer, library: fakeLibrary })
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("back", false), true)
        compare(pages.depth, 2)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 1)
        for (let i = 0; i < 5; i++)
            router.dispatch("down", false)
        compare(page.rowIndex, 6)
        verify(router.dispatch("a", false))
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "song")
        compare(fakePlayer.calls[0].id, 106)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 1)
        compare(page.rowIndex, 0)
        router.dispatch("a", false)
        compare(fakePlayer.calls[1].kind, "album")
        compare(fakePlayer.calls[1].id, 200)
        compare(router.dispatch("rb", false), true)
        compare(page.pivotIndex, 1)
        router.dispatch("lb", false)
        compare(page.pivotIndex, 0)
        compare(page.rowIndex, 6)
    }

    function test_history_play_without_player() {
        const page = open(historyComponent, {})
        router.dispatch("a", false)
        compare(counters.played[0].kind, "song")
        compare(counters.played[0].id, 100)
        router.dispatch("rb", false)
        router.dispatch("a", false)
        compare(counters.played[1].kind, "album")
        compare(counters.played[1].id, 200)
    }

    function test_history_sheet_and_models() {
        fakeLibrary.pinned = ["song:100"]
        const page = open(historyComponent, { player: fakePlayer, library: fakeLibrary })
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "Song 0")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "add to playlist", "unpin from quickplay"]))
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "song")
        compare(counters.sheetActions[0].id, 100)
        router.dispatch("y", false)
        compare(counters.sheetActions[1].action, "add to queue")
        recent.clear()
        compare(page.recentPane.count, 0)
        compare(page.focusedEntry, null)
        compare(router.dispatch("a", false), true)
        compare(fakePlayer.calls.length, 0)
        recent.append({ id: 300, title: "Late", subtitle: "", artPath: "", kind: "song" })
        compare(page.recentPane.count, 1)
        compare(page.focusedEntry.id, 300)
    }

    function test_history_pivot_motion_and_screen_off() {
        const page = open(historyComponent, { animated: true, player: fakePlayer })
        router.dispatch("rb", false)
        verify(page.switching)
        compare(page.shownPivot, 0)
        tryCompare(page, "shownPivot", 1, 2000)
        tryCompare(page, "switching", false, 2000)
        compare(page.newPane.x, 0)
        compare(page.newPane.opacity, 1)
        router.dispatch("lb", false)
        verify(page.switching)
        page.screenOff = true
        compare(page.switching, false)
        compare(page.shownPivot, 0)
        compare(page.recentPane.x, 0)
        compare(page.recentPane.opacity, 1)
    }

    function test_home_navigates_and_counts() {
        pages.push(homeComponent, { playlistCount: 2 })
        const home = pages.currentItem
        compare(home.playlistsSubtitle, "2 playlists")
        home.playlistCount = -1
        compare(home.playlistsSubtitle, "now playing saved")
        home.setFocus(2)
        router.dispatch("a", false)
        home.setFocus(3)
        router.dispatch("a", false)
        home.setFocus(4)
        router.dispatch("a", false)
        compare(JSON.stringify(counters.navigated), JSON.stringify(["playlists", "history", "settings"]))
    }

    function test_playlists_t2a_first_show() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(playlistsComponent, { animated: true, player: fakePlayer })
        compare(page.arriving, true)
        const first = tc.rowAt(page.listView, 0)
        compare(first.enter, 0)
        compare(first.shift, Motion.pivotShift)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, Ui.px(34), 0.01)
        compare(first.opacity, 0)
        tryCompare(pages, "phase", "idle", 3000)
        compare(page.arriving, false)
        compare(first.enterDuration, 380)
        compare(first.stagger, 0)
        compare(tc.rowAt(page.listView, 2).stagger, 2 * 22)
        tryCompare(first, "enter", 1, 2000)
        tryCompare(tc.rowAt(page.listView, 2), "enter", 1, 2000)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, 0, 0.01)
        compare(first.opacity, 1)
    }

    function test_playlists_t2a_rest_without_animation() {
        const page = open(playlistsComponent, { animated: true, player: fakePlayer })
        compare(page.arriving, false)
        const first = tc.rowAt(page.listView, 0)
        compare(first.enter, 1)
        compare(first.x, 0)
        compare(first.opacity, 1)
    }

    function test_playlists_f12_and_f7() {
        const page = open(playlistsComponent, { animated: true, player: fakePlayer })
        compare(page.countText.text, "2 playlists")
        playlists.append({ id: 3, title: "Harmonics", subtitle: "9 songs", kind: "playlist" })
        compare(page.countLine, "3 playlists")
        compare(page.countSwapping, true)
        compare(page.countText.text, "2 playlists")
        tryCompare(page, "shownCount", "3 playlists", 1000)
        verify(page.countText.opacity < 1)
        tryCompare(page, "countSwapping", false, 1000)
        compare(page.countText.opacity, 1)
        tryVerify(function() {
            const r = page.listView.itemAtIndex(3)
            return r !== null && r.stagger === 3 * 50
        }, 1000)
        const added = tc.rowAt(page.listView, 3)
        compare(added.enterDuration, 220)
        compare(added.shift, Motion.listChangeShift)
        compare(added.stagger, 3 * 50)
        tryCompare(added, "enter", 1, 2000)
        fakePlayer.queue.count = 3
        wait(20)
        compare(tc.rowAt(page.listView, 0).enter, 1)
        compare(page.rows[0].subtitle, "3 songs")
    }

    function test_history_t2a_first_show() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(historyComponent, { animated: true, player: fakePlayer })
        compare(page.arriving, true)
        const first = tc.rowAt(page.recentPane, 0)
        compare(first.enter, 0)
        compare(first.shift, Motion.pivotShift)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, Ui.px(34), 0.01)
        compare(first.opacity, 0)
        tryCompare(pages, "phase", "idle", 3000)
        compare(first.enterDuration, 380)
        compare(first.stagger, 0)
        compare(tc.rowAt(page.recentPane, 2).stagger, 2 * 22)
        tryCompare(first, "enter", 1, 2000)
        tryCompare(tc.rowAt(page.recentPane, 3), "enter", 1, 2000)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, 0, 0.01)
    }

    function test_history_t5_row_stagger() {
        const page = open(historyComponent, { animated: true, player: fakePlayer })
        router.dispatch("rb", false)
        verify(page.switching)
        tryCompare(page, "shownPivot", 1, 2000)
        const pane = page.newPane
        compare(pane.x, 0)
        for (let i = 0; i < 2; i++) {
            const row = tc.rowAt(pane, i)
            compare(row.stagger, Math.min(i, 10) * 22)
            compare(row.enterDuration, 380)
            compare(row.shift, 34)
            verify(row.enter < 1)
        }
        tryCompare(tc.rowAt(pane, 0), "enter", 1, 2000)
        tryCompare(tc.rowAt(pane, 1), "enter", 1, 2000)
        fuzzyCompare(tc.rowAt(pane, 1).x, 0, 0.01)
        compare(tc.rowAt(pane, 1).opacity, 1)
        tryCompare(page, "switching", false, 2000)
        router.dispatch("lb", false)
        tryCompare(page, "shownPivot", 0, 2000)
        compare(tc.rowAt(page.recentPane, 0).shift, -34)
        tryCompare(tc.rowAt(page.recentPane, 0), "enter", 1, 2000)
    }

    function test_history_f12_on_model_change() {
        const page = open(historyComponent, { animated: true, player: fakePlayer })
        recent.append({ id: 300, title: "Late", subtitle: "", artPath: "", kind: "song" })
        tryVerify(function() {
            const r = page.recentPane.itemAtIndex(7)
            return r !== null && r.enter < 1
        }, 1000)
        const added = tc.rowAt(page.recentPane, 7)
        compare(added.enterDuration, 220)
        compare(added.shift, Motion.listChangeShift)
        compare(added.stagger, 0)
        compare(tc.rowAt(page.recentPane, 0).enter, 1)
        tryCompare(added, "enter", 1, 2000)
        recent.move(6, 0, 1)
        tryVerify(function() {
            const r = page.recentPane.itemAtIndex(0)
            return r !== null && r.enter < 1
        }, 1000)
        compare(tc.rowAt(page.recentPane, 0).rowItem.primary, "Song 6")
        compare(tc.rowAt(page.recentPane, 1).stagger, 50)
        tryCompare(tc.rowAt(page.recentPane, 0), "enter", 1, 2000)
        tryCompare(tc.rowAt(page.recentPane, 1), "enter", 1, 2000)
        fresh.append({ id: 300, title: "Late", subtitle: "", artPath: "", kind: "album" })
        wait(20)
        compare(tc.rowAt(page.recentPane, 0).enter, 1)
    }

    function test_history_f12_reset_from_focused_row() {
        const page = open(historyComponent, { animated: true, player: fakePlayer })
        for (let i = 0; i < 3; i++)
            verify(router.dispatch("down", false))
        compare(page.rowIndex, 3)
        recent.modelReset()
        tryVerify(function() {
            const r = page.recentPane.itemAtIndex(3)
            return r !== null && r.enter < 1
        }, 1000)
        compare(tc.rowAt(page.recentPane, 3).stagger, 0)
        compare(tc.rowAt(page.recentPane, 4).stagger, 50)
        compare(tc.rowAt(page.recentPane, 0).enter, 1)
        compare(tc.rowAt(page.recentPane, 2).enter, 1)
        compare(page.rowIndex, 3)
        tryCompare(tc.rowAt(page.recentPane, 3), "enter", 1, 2000)
    }

    function tapAt(item, x, y) {
        mouseClick(item, x, y)
        wait(0)
        wait(0)
    }

    function dragUp(item, x, y, distance) {
        mousePress(item, x, y)
        const steps = Math.round(distance / Ui.px(1))
        for (let i = 1; i <= steps; i++)
            mouseMove(item, x, y - distance * i / steps, -1, Qt.LeftButton)
        wait(150)
        mouseRelease(item, x, y - distance)
        wait(0)
        wait(0)
    }

    function tapHeaderWord(page) {
        tc.tapAt(page.header, page.header.textX + Ui.px(4), page.header.textBaseline - Ui.px(4))
    }

    function test_touch_playlists_row_plays() {
        const page = open(playlistsComponent, { player: fakePlayer, library: fakeLibrary })
        tc.tapAt(tc.rowAt(page.listView, 2))
        compare(page.focusIndex, 2)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "playlist")
        compare(fakePlayer.calls[0].id, 2)
    }

    function test_touch_playlists_drag_then_a() {
        for (let i = 3; i < 33; i++)
            playlists.append({ id: i, title: "List " + i, subtitle: "1 song", kind: "playlist" })
        const page = open(playlistsComponent, { player: fakePlayer, library: fakeLibrary })
        const l = page.listView
        tc.dragUp(page, page.width / 2, page.height - Ui.px(20), l.height + Ui.px(40))
        const first = Math.ceil(l.contentY / l.rowPitch - 0.001)
        verify(first > 0)
        compare(l.focusIndex, first)
        compare(page.focusIndex, first)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].id, page.rows[first].id)
    }

    function test_touch_history_tap_plays_entry() {
        const page = open(historyComponent, { player: fakePlayer, library: fakeLibrary })
        tc.tapAt(tc.rowAt(page.recentPane, 3))
        compare(page.rowIndex, 3)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "song")
        compare(fakePlayer.calls[0].id, 103)
    }

    function test_touch_history_drag_then_a() {
        for (let i = 7; i < 40; i++)
            recent.append({ id: 100 + i, title: "Song " + i, subtitle: "Artist " + i, artPath: "", kind: "song" })
        const page = open(historyComponent, { player: fakePlayer, library: fakeLibrary })
        const l = page.recentPane
        tc.dragUp(page, page.width / 2, page.height - Ui.px(20), l.height + Ui.px(40))
        const first = Math.ceil(l.contentY / l.rowPitch - 0.001)
        verify(first > 0)
        compare(l.focusIndex, first)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].id, 100 + first)
    }

    function test_touch_history_pivot_and_header() {
        const page = open(historyComponent, { player: fakePlayer, library: fakeLibrary })
        const label = page.strip.labelItem(1)
        tc.tapAt(label, Ui.px(4), label.height / 2)
        compare(page.pivotIndex, 1)
        tc.tapHeaderWord(page)
        compare(pages.depth, 1)
    }
}
