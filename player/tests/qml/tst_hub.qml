import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "MusicHub"
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
        pad: fakePad
    }

    QtObject {
        id: fakePad

        readonly property var names: ["up", "down", "left", "right", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back"]

        signal action(int code, bool repeat)
        signal pressed(int code)
        signal released(int code)

        function actionName(code) {
            return fakePad.names[code]
        }
    }

    ListModel {
        id: artists
    }

    ListModel {
        id: albums
    }

    ListModel {
        id: songs
    }

    ListModel {
        id: genres
    }

    QtObject {
        id: fakePlayer

        property var calls: []
        property QtObject queue: QtObject {
            property int count: 12
        }

        function playTracks(ids, start) {
            fakePlayer.calls = fakePlayer.calls.concat([{ ids: ids, start: start }])
        }
    }

    QtObject {
        id: fakeLibrary

        property bool scanning: false
    }

    QtObject {
        id: counters

        property var opened: []
        property int nowPlaying: 0
        property int jumps: 0
        property var played: []
        property var sheetActions: []
    }

    Component {
        id: hubComponent

        MusicHub {
            artistsModel: artists
            albumsModel: albums
            songsModel: songs
            genresModel: genres
            onOpen: (kind, id) => counters.opened = counters.opened.concat([{ kind: kind, id: id }])
            onNowPlaying: counters.nowPlaying++
            onJumpRequested: counters.jumps++
            onPlay: (kind, id) => counters.played = counters.played.concat([{ kind: kind, id: id }])
            onSheetAction: (action, kind, id) => counters.sheetActions = counters.sheetActions.concat([{ action: action, kind: kind, id: id }])
        }
    }

    Component {
        id: blankComponent

        Page {}
    }

    function childIndex(parent, item) {
        for (let i = 0; i < parent.children.length; i++) {
            if (parent.children[i] === item)
                return i
        }
        return -1
    }

    function letterAt(i, per) {
        return String.fromCharCode(97 + Math.floor(i / per))
    }

    function rowAt(pane, index) {
        tryVerify(() => pane.itemAtIndex(index) !== null, 1000)
        return pane.itemAtIndex(index)
    }

    function fill(model, prefix, n, per) {
        model.clear()
        for (let i = 0; i < n; i++) {
            const letter = tc.letterAt(i, per)
            model.append({ id: i + 1, title: letter + " " + prefix + " " + (i + 1), subtitle: "Artist " + (i + 1), artPath: "", section: letter.toUpperCase() })
        }
    }

    function fillAlbums(n) {
        albums.clear()
        albums.append({ id: 1000, title: "3 albums", subtitle: "Various", artPath: "", section: "#" })
        for (let i = 0; i < n; i++) {
            const letter = tc.letterAt(i, 4)
            albums.append({ id: i + 1, title: letter + " album " + (i + 1), subtitle: "Artist " + (i + 1), artPath: "", section: letter.toUpperCase() })
        }
    }

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.windowW = w
        Ui.windowH = h
        pages.width = w
        pages.height = h
        router.width = w
        router.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        pages.clear()
        pages.animate = false
        tc.fill(artists, "artist", 12, 3)
        tc.fillAlbums(19)
        tc.fill(songs, "song", 30, 3)
        tc.fill(genres, "genre", 4, 1)
        fakePlayer.calls = []
        fakePlayer.queue.count = 12
        fakeLibrary.scanning = false
        counters.opened = []
        counters.nowPlaying = 0
        counters.jumps = 0
        counters.played = []
        counters.sheetActions = []
    }

    function open(properties) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        pages.push(blankComponent, {})
        const page = pages.push(hubComponent, props)
        page.completeAll()
        for (const pane of page.panes)
            pane.forceLayout()
        return page
    }

    function test_layout_portrait() {
        const page = open()
        compare(Ui.u, 2)
        compare(JSON.stringify(page.pivots), JSON.stringify(["artists", "albums", "songs", "genres", "playlists"]))
        compare(page.pivotIndex, 0)
        compare(page.header.text, "music")
        compare(page.artistsPane.y, Ui.px(128))
        compare(page.artistsPane.rowPitch, Ui.px(40))
        compare(page.albumsPane.rowPitch, Ui.px(71 + 12))
        compare(page.focusBar.x, Ui.px(20 - 8))
        compare(page.focusBar.visible, true)
    }

    function test_grid_cols() {
        const page = open({ pivotIndex: 1 })
        compare(Ui.canvasW, 320)
        compare(Ui.gridCols, 3)
        compare(page.cols, 3)
        compare(page.albumRows[0].length, 3)
        compare(page.albumRows.length, Math.ceil(page.albumCells.length / 3))
        tc.setWindow(1920, 1080)
        fuzzyCompare(Ui.canvasW, 483, 1)
        compare(Ui.gridCols, 6)
        compare(page.cols, 6)
        compare(page.albumRows[0].length, 6)
        compare(page.albumsPane.y, Ui.px(98))
        compare(page.albumsPane.rowPitch, Ui.px(60 + 12))
    }

    function test_grid_letter_tiles() {
        const page = open({ pivotIndex: 1 })
        compare(page.albumCells[0].kind, "letter")
        compare(page.albumCells[0].letter, "#")
        compare(page.albumCells[1].kind, "album")
        compare(page.albumCells[2].kind, "letter")
        compare(page.albumCells[2].letter, "a")
        const row = page.albumsPane.itemAtIndex(0)
        verify(row !== null)
        compare(row.cellCount, 3)
        compare(row.cellAt(0).letterItem.visible, true)
        compare(row.cellAt(0).tileItem.visible, false)
        compare(row.cellAt(1).tileItem.visible, true)
        compare(row.cellAt(0).letterItem.letter, "#")
    }

    function test_caption_clipped_by_next_tile() {
        const page = open({ pivotIndex: 1 })
        const row = page.albumsPane.itemAtIndex(1)
        verify(row !== null)
        for (let c = 0; c < 2; c++) {
            const cell = row.cellAt(c)
            const next = row.cellAt(c + 1)
            compare(cell.x, Ui.px(20 + c * (71 + 12)))
            compare(cell.width, Ui.px(71))
            const caption = cell.tileItem.captionItem
            fuzzyCompare(cell.x + caption.x, Ui.px(20 + c * 83 + 71 + 4), 0.01)
            fuzzyCompare(caption.width, Ui.px(71 + 12 - 4), 0.01)
            verify(cell.x + caption.x + caption.width > next.x)
            fuzzyCompare(cell.x + caption.x + caption.width, next.x + next.width, 0.01)
            compare(caption.clip, true)
            verify(next.z >= cell.z)
            verify(tc.childIndex(row, next) > tc.childIndex(row, cell))
        }
        const last = row.cellAt(2)
        fuzzyCompare(last.x + last.tileItem.captionItem.x, Ui.px(20 + 2 * 83 + 75), 0.01)
        verify(last.x + last.tileItem.captionItem.x < Ui.px(320))
    }

    function test_pivot_travel_no_wrap() {
        const page = open()
        tryCompare(page.strip, "laidOut", true, 2000)
        for (let i = 1; i <= 4; i++) {
            verify(router.dispatch("rb", false))
            compare(page.pivotIndex, i)
            compare(page.shownPivot, i)
            compare(page.strip.activeIndex, i)
            fuzzyCompare(page.strip.activeLabelX, Ui.px(20), 0.01)
            compare(page.strip.labelItem(i).font.styleName, "Bold")
        }
        compare(page.switchPivot(1), false)
        router.dispatch("rb", false)
        compare(page.pivotIndex, 4)
        for (let i = 3; i >= 0; i--) {
            router.dispatch("lb", false)
            compare(page.pivotIndex, i)
            fuzzyCompare(page.strip.activeLabelX, Ui.px(20), 0.01)
        }
        compare(page.switchPivot(-1), false)
        router.dispatch("lb", false)
        compare(page.pivotIndex, 0)
        compare(page.header.shift, 0)
    }

    function test_pivot_header_parallax() {
        const page = open()
        tryCompare(page.strip, "laidOut", true, 2000)
        router.dispatch("rb", false)
        fuzzyCompare(page.header.shift, 0.2 * page.strip.travel / Ui.u, 0.01)
        verify(page.header.shift < 0)
    }

    function test_feather_stagger() {
        const page = open({ animated: true, pivotIndex: 1 })
        router.dispatch("rb", false)
        compare(page.pivotIndex, 2)
        compare(page.shownPivot, 1)
        verify(page.switching)
        tryCompare(page, "shownPivot", 2, 2000)
        const pane = page.songsPane
        const visibleRows = Math.min(9, Math.ceil(pane.height / pane.rowPitch))
        const last = visibleRows - 1
        for (let i = 0; i < visibleRows; i++) {
            const row = tc.rowAt(pane, i)
            verify(row !== null)
            compare(row.stagger, Math.min(i, 10) * 22)
            compare(row.enterDuration, 380)
            compare(row.shift, 34)
            verify(row.enter < 1)
        }
        const deep = pane.itemAtIndex(11)
        if (deep !== null)
            compare(deep.stagger, 220)
        tryCompare(tc.rowAt(pane, 0), "enter", 1, 2000)
        tryCompare(tc.rowAt(pane, last), "enter", 1, 2000)
        fuzzyCompare(pane.itemAtIndex(last).x, 0, 0.01)
        compare(pane.itemAtIndex(last).opacity, 1)
        tryCompare(page, "switching", false, 2000)
    }

    function test_feather_direction_lb() {
        const page = open({ animated: true, pivotIndex: 2 })
        router.dispatch("lb", false)
        tryCompare(page, "shownPivot", 1, 2000)
        const row = tc.rowAt(page.albumsPane, 0)
        verify(row !== null)
        compare(row.shift, -34)
        tryCompare(row, "enter", 1, 2000)
    }

    function test_dividers_skipped() {
        const page = open()
        compare(page.artistRows[0].kind, "divider")
        compare(page.artistRows[0].letter, "a")
        compare(page.artistRows[1].kind, "artist")
        compare(page.rowIndex, 1)
        verify(router.dispatch("up", false) === false)
        compare(page.rowIndex, 1)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 2)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 3)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 5)
        compare(page.artistRows[4].kind, "divider")
        verify(router.dispatch("up", false))
        compare(page.rowIndex, 3)
        const divider = tc.rowAt(page.artistsPane, 0)
        compare(divider.dividerItem.visible, true)
        compare(divider.rowItem.visible, false)
        compare(divider.dividerItem.text, "a")
        compare(divider.dividerItem.x, Ui.px(20))
        fuzzyCompare(divider.dividerItem.y + divider.dividerItem.baselineOffset, Ui.px(40 - 8), 0.5)
        compare(divider.dividerItem.font.styleName, "Regular")
        compare(divider.dividerItem.font.pixelSize, Ui.fontPx(20))
        const entry = tc.rowAt(page.artistsPane, 3)
        compare(entry.rowItem.visible, true)
        compare(entry.rowItem.focused, true)
        compare(entry.rowItem.primary, page.artistRows[3].title)
        compare(entry.rowItem.countLine, true)
        compare(entry.rowItem.secondaryItem.font.capitalization, Font.AllUppercase)
        compare(entry.rowItem.secondaryItem.font.pixelSize, Ui.fontPx(11))
        fuzzyCompare(entry.rowItem.secondaryItem.color.a, 0.6, 1 / 255)
        compare(tc.rowAt(page.artistsPane, 1).rowItem.focused, false)
    }

    function test_songs_rows_and_play() {
        const page = open({ pivotIndex: 2, player: fakePlayer })
        compare(page.songRows[0].kind, "divider")
        compare(page.rowIndex, 1)
        const first = page.songsPane.itemAtIndex(1)
        compare(first.rowItem.number, "1")
        compare(first.rowItem.primary, "a song 1")
        compare(first.rowItem.secondary, "Artist 1")
        compare(first.rowItem.countLine, false)
        compare(first.rowItem.secondaryItem.font.capitalization, Font.MixedCase)
        compare(first.rowItem.textX, Ui.px(20 + 24))
        compare(page.songIds.length, 30)
        verify(router.dispatch("a", false))
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].ids.length, 30)
        compare(fakePlayer.calls[0].ids[0], 1)
        compare(fakePlayer.calls[0].start, 0)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.rowIndex, 5)
        router.dispatch("a", false)
        compare(fakePlayer.calls[1].start, 3)
        compare(page.songsPane.itemAtIndex(5).rowItem.number, "4")
    }

    function test_song_play_without_player() {
        const page = open({ pivotIndex: 2 })
        router.dispatch("a", false)
        compare(counters.played.length, 1)
        compare(counters.played[0].kind, "song")
        compare(counters.played[0].id, 1)
    }

    function test_album_artist_genre_open() {
        const page = open({ pivotIndex: 1 })
        compare(page.gridCol, 0)
        router.dispatch("a", false)
        compare(counters.jumps, 1)
        verify(router.dispatch("right", false))
        compare(page.gridCol, 1)
        router.dispatch("a", false)
        compare(counters.opened.length, 1)
        compare(counters.opened[0].kind, "album")
        compare(counters.opened[0].id, 1000)
        verify(router.dispatch("right", false))
        compare(page.gridCol, 2)
        compare(router.dispatch("right", false), false)
        compare(page.gridCol, 2)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 1)
        compare(page.gridCol, 2)
        router.dispatch("lb", false)
        compare(page.pivotIndex, 0)
        router.dispatch("a", false)
        compare(counters.opened[1].kind, "artist")
        compare(counters.opened[1].id, 1)
        page.switchPivot(3)
        compare(page.pivotIndex, 3)
        compare(page.genresPane.itemAtIndex(1).rowItem.bold, true)
        router.dispatch("a", false)
        compare(counters.opened[2].kind, "genre")
        compare(router.dispatch("left", false), false)
    }

    function test_grid_last_row_col_clamp() {
        const page = open({ pivotIndex: 1 })
        const lastRow = page.albumRows.length - 1
        const lastLen = page.albumRows[lastRow].length
        verify(lastLen < 3)
        page.gridCol = 2
        for (let i = 0; i < 20; i++)
            router.dispatch("down", false)
        compare(page.rowIndex, lastRow)
        compare(page.gridCol, lastLen - 1)
        compare(router.dispatch("down", false), false)
    }

    function test_playlists_now_playing_first() {
        const page = open({ pivotIndex: 4, player: fakePlayer })
        compare(page.alphabetic, false)
        compare(page.playlistRows.length, 1)
        compare(page.playlistRows[0].kind, "nowplaying")
        compare(page.playlistRows[0].title, "now playing")
        compare(page.playlistRows[0].subtitle, "12 songs")
        const row = page.playlistsPane.itemAtIndex(0)
        compare(row.rowItem.ring, true)
        compare(row.rowItem.bold, true)
        compare(row.rowItem.focused, true)
        compare(row.rowItem.primaryRole.name, "listPlaylist")
        fakePlayer.queue.count = 1
        compare(page.playlistRows[0].subtitle, "1 song")
        router.dispatch("a", false)
        compare(counters.nowPlaying, 1)
        compare(router.dispatch("x", false), true)
        compare(page.sheet.shown, false)
        compare(router.dispatch("rt", false), true)
        compare(page.jump.shown, false)
    }

    function test_jump_letter_on_page() {
        const page = open({ animated: true, pivotIndex: 2 })
        compare(page.jump.shown, false)
        verify(router.dispatch("rt", false))
        compare(page.jump.shown, true)
        compare(page.jump.opacity, 1)
        compare(page.songRows[page.rowIndex].kind, "song")
        compare(page.jump.letter, page.songRows[page.rowIndex].letter)
        verify(page.rowIndex > 1)
        compare(page.jump.text.font.pixelSize, Ui.fontPx(120))
        page.songsPane.completeScroll()
        compare(page.jump.hold.running, true)
        tryCompare(page.jump, "shown", false, 2000)
        verify(router.dispatch("lt", false))
        compare(page.jump.shown, true)
        compare(page.rowIndex, 1)
        compare(page.jump.letter, "a")
    }

    function test_jump_letter_grid() {
        const page = open({ animated: true, pivotIndex: 1 })
        page.gridCol = 1
        verify(router.dispatch("rt", false))
        compare(page.jump.shown, true)
        const cell = page.albumRows[page.rowIndex][page.gridCol]
        compare(page.jump.letter, cell.letter)
    }

    function test_back_view_logs_jump() {
        const page = open()
        verify(router.dispatch("back", false))
        compare(counters.jumps, 1)
        page.switchPivot(4)
        router.dispatch("back", false)
        compare(counters.jumps, 1)
    }

    function test_sheet_actions() {
        const page = open({ pivotIndex: 2, player: fakePlayer })
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "a song 1")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "add to queue", "pin to quickplay"]))
        router.dispatch("down", false)
        compare(page.sheet.focusIndex, 1)
        compare(page.rowIndex, 1)
        router.dispatch("a", false)
        compare(page.sheet.shown, false)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "song")
        compare(counters.sheetActions[0].id, 1)
        router.dispatch("x", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].start, 0)
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions[1].action, "pin to quickplay")
        router.dispatch("y", false)
        compare(counters.sheetActions[2].action, "add to queue")
        router.dispatch("x", false)
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(pages.depth, 2)
    }

    function test_b_pops() {
        const page = open()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function entering(pane, index) {
        return function() {
            const row = pane.itemAtIndex(index)
            return row !== null && row.enter < 1
        }
    }

    function test_f12_after_scan() {
        const page = open({ animated: true, pivotIndex: 2, library: fakeLibrary })
        fakeLibrary.scanning = true
        fakeLibrary.scanning = false
        const pane = page.songsPane
        compare(tc.rowAt(pane, 0).enter, 1)
        songs.append({ id: 99, title: "z song", subtitle: "Artist", artPath: "", section: "Z" })
        tryVerify(tc.entering(pane, 0), 1000)
        const visibleRows = Math.min(9, Math.ceil(pane.height / pane.rowPitch))
        const last = visibleRows - 1
        for (let i = 0; i < visibleRows; i++) {
            const row = tc.rowAt(pane, i)
            verify(row !== null)
            compare(row.stagger, Math.min(i, 10) * 50)
            compare(row.enterDuration, 220)
            compare(row.shift, -5)
            verify(row.enter < 1)
        }
        const deep = pane.itemAtIndex(11)
        if (deep !== null)
            compare(deep.stagger, 500)
        tryCompare(tc.rowAt(pane, 0), "enter", 1, 2000)
        tryCompare(tc.rowAt(pane, last), "enter", 1, 2000)
        songs.append({ id: 100, title: "z song 2", subtitle: "Artist", artPath: "", section: "Z" })
        wait(20)
        compare(tc.rowAt(pane, 0).enter, 1)
    }

    function test_f12_reset_during_scan() {
        const page = open({ animated: true, pivotIndex: 0, library: fakeLibrary })
        fakeLibrary.scanning = true
        artists.append({ id: 99, title: "z artist", subtitle: "1 album, 1 song", artPath: "", section: "Z" })
        wait(20)
        compare(tc.rowAt(page.artistsPane, 0).enter, 1)
        fakeLibrary.scanning = false
        tryVerify(tc.entering(page.artistsPane, 0), 1000)
        compare(tc.rowAt(page.artistsPane, 1).stagger, 50)
        tryCompare(tc.rowAt(page.artistsPane, 1), "enter", 1, 2000)
    }

    function test_f12_refreshes_shown_pane() {
        const page = open({ animated: true, pivotIndex: 2, library: fakeLibrary })
        fakeLibrary.scanning = true
        fakeLibrary.scanning = false
        artists.append({ id: 99, title: "z artist", subtitle: "", artPath: "", section: "Z" })
        songs.append({ id: 99, title: "z song", subtitle: "Artist", artPath: "", section: "Z" })
        tryVerify(tc.entering(page.songsPane, 0), 1000)
        compare(tc.rowAt(page.artistsPane, 0).enter, 1)
        compare(page.songRows[page.songRows.length - 1].id, 99)
        tryCompare(tc.rowAt(page.songsPane, 0), "enter", 1, 2000)
    }

    function test_f12_not_without_scan() {
        const page = open({ animated: true, pivotIndex: 2, library: fakeLibrary })
        songs.append({ id: 99, title: "z song", subtitle: "Artist", artPath: "", section: "Z" })
        wait(20)
        compare(tc.rowAt(page.songsPane, 0).enter, 1)
    }

    function test_screen_off_completes() {
        const page = open({ animated: true, pivotIndex: 1 })
        router.dispatch("rb", false)
        verify(page.switching)
        page.screenOff = true
        compare(page.switching, false)
        compare(page.shownPivot, 2)
        compare(page.songsPane.x, 0)
        compare(page.songsPane.opacity, 1)
        compare(page.albumsPane.x, 0)
        compare(tc.rowAt(page.songsPane, 0).enter, 1)
        page.screenOff = false
        router.dispatch("rt", false)
        compare(page.jump.shown, true)
        page.screenOff = true
        compare(page.jump.shown, false)
    }

    function test_focus_bar_follows_row() {
        const page = open()
        const bar = page.focusBar
        compare(bar.rowHeight, Ui.px(40))
        fuzzyCompare(bar.y, Ui.px(128) + Ui.px(40), 0.01)
        router.dispatch("down", false)
        fuzzyCompare(bar.y, Ui.px(128) + 2 * Ui.px(40), 0.01)
        page.switchPivot(1)
        compare(bar.visible, false)
        page.switchPivot(-1)
        compare(bar.visible, true)
    }

    function test_page_keeps_focus_in_bounds() {
        const page = open({ pivotIndex: 2 })
        for (let i = 0; i < 40; i++)
            router.dispatch("down", false)
        const list = page.songsPane
        list.completeScroll()
        compare(page.rowIndex, page.songRows.length - 1)
        const top = list.focusIndex * list.rowPitch - list.contentY
        verify(top >= 0)
        verify(top + list.rowPitch <= list.height + 0.01)
        compare(router.dispatch("down", false), false)
    }
}
