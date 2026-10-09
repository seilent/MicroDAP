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

    TouchLayer {
        id: layer

        width: 640
        height: 960
        router: router
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

    FakeSectioned {
        id: artists
    }

    FakeAlbumGrid {
        id: albums
    }

    FakeSectioned {
        id: songs
    }

    FakeSectioned {
        id: genres
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

        property bool scanning: false
        property var pinned: []

        function isPinned(kind, id) {
            return fakeLibrary.pinned.indexOf(kind + ":" + id) >= 0
        }
    }

    ListModel {
        id: playlists
    }

    QtObject {
        id: counters

        property var opened: []
        property int nowPlaying: 0
        property int jumps: 0
        property string jumpLetter: ""
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
            onJumpRequested: (letter) => {
                counters.jumps++
                counters.jumpLetter = letter
            }
            onPlay: (kind, id) => counters.played = counters.played.concat([{ kind: kind, id: id }])
            onSheetAction: (action, kind, id) => counters.sheetActions = counters.sheetActions.concat([{ action: action, kind: kind, id: id }])
        }
    }

    Component {
        id: blankComponent

        Page {}
    }

    function letterAt(i, per) {
        return String.fromCharCode(97 + Math.floor(i / per))
    }

    function rowAt(pane, index) {
        tryVerify(() => pane.itemAtIndex(index) !== null, 1000)
        return pane.itemAtIndex(index)
    }

    function entries(prefix, n, per) {
        const out = []
        for (let i = 0; i < n; i++) {
            const letter = tc.letterAt(i, per)
            out.push({ id: i + 1, title: letter + " " + prefix + " " + (i + 1), subtitle: "Artist " + (i + 1), artPath: "", section: letter.toUpperCase() })
        }
        return out
    }

    function albumEntries(n) {
        const out = [{ id: 1000, title: "3 albums", subtitle: "Various", artPath: "", section: "#" }]
        for (let i = 0; i < n; i++) {
            const letter = tc.letterAt(i, 4)
            out.push({ id: i + 1, title: letter + " album " + (i + 1), subtitle: "Artist " + (i + 1), artPath: "", section: letter.toUpperCase() })
        }
        return out
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
        for (const m of [artists, songs, genres, albums])
            m.active = false
        albums.columns = 3
        artists.load(tc.entries("artist", 12, 3), "artist")
        albums.load(tc.albumEntries(19))
        songs.load(tc.entries("song", 30, 3), "song")
        genres.load(tc.entries("genre", 4, 1), "genre")
        fakePlayer.calls = []
        fakePlayer.queue.count = 12
        fakeLibrary.scanning = false
        fakeLibrary.pinned = []
        playlists.clear()
        counters.opened = []
        counters.nowPlaying = 0
        counters.jumps = 0
        counters.jumpLetter = ""
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
        compare(page.albumsPane.rowPitch, Ui.px(71 + 24 + 12))
        compare(page.focusBar.x, Ui.px(20 - 8))
        compare(page.focusBar.visible, true)
    }

    function test_lazy_panes() {
        const page = open()
        compare(artists.active, true)
        compare(albums.active, false)
        compare(songs.active, false)
        compare(genres.active, false)
        compare(page.artistsPane.count, artists.count)
        compare(page.albumsPane.count, 0)
        compare(page.songsPane.count, 0)
        verify(router.dispatch("rb", false))
        compare(albums.active, true)
        compare(page.albumsPane.count, albums.count)
        compare(songs.active, false)
        page.pivotIndex = 2
        compare(songs.active, true)
        compare(page.songsPane.count, songs.count)
        compare(genres.active, false)
    }

    function test_grid_cols() {
        const page = open({ pivotIndex: 1 })
        compare(Ui.canvasW, 320)
        compare(Ui.gridCols, 3)
        compare(page.cols, 3)
        compare(albums.columns, 3)
        compare(albums.cellCount(0), 3)
        compare(page.albumsPane.count, Math.ceil(albums.flat.length / 3))
        tc.setWindow(1920, 1080)
        fuzzyCompare(Ui.canvasW, 483, 1)
        compare(Ui.gridCols, 6)
        compare(page.cols, 6)
        compare(albums.columns, 6)
        compare(albums.cellCount(0), 6)
        compare(page.albumsPane.count, Math.ceil(albums.flat.length / 6))
        compare(page.albumsPane.y, Ui.px(98))
        compare(page.albumsPane.rowPitch, Ui.px(60 + 24 + 12))
    }

    function test_grid_letter_tiles() {
        const page = open({ pivotIndex: 1 })
        const first = albums.cells(0)
        compare(first[0].kind, "letter")
        compare(first[0].letter, "#")
        compare(first[1].kind, "album")
        compare(first[2].kind, "letter")
        compare(first[2].letter, "a")
        const row = tc.rowAt(page.albumsPane, 0)
        compare(row.cellCount, 3)
        verify(row.cellAt(0).letterItem !== null)
        compare(row.cellAt(0).tileItem, null)
        verify(row.cellAt(1).tileItem !== null)
        compare(row.cellAt(1).letterItem, null)
        compare(row.cellAt(0).letterItem.letter, "#")
    }

    function test_caption_under_tile() {
        const page = open({ pivotIndex: 1 })
        const row = tc.rowAt(page.albumsPane, 1)
        compare(row.height, Ui.px(71 + 24 + 12))
        for (let c = 0; c < 3; c++) {
            const cell = row.cellAt(c)
            verify(cell !== null)
            compare(cell.letter, false)
            compare(cell.x, Ui.px(20 + c * (71 + 12)))
            compare(cell.width, Ui.px(71))
            compare(cell.height, Ui.px(71 + 24))
            const tile = cell.tileItem
            verify(tile !== null)
            compare(tile.height, Ui.px(71))
            const caption = tile.captionItem
            compare(caption.visible, true)
            fuzzyCompare(caption.x, 0, 0.01)
            fuzzyCompare(caption.y, Ui.px(71), 0.01)
            fuzzyCompare(caption.width, Ui.px(71), 0.01)
            fuzzyCompare(caption.height, Ui.px(24), 0.01)
            compare(caption.clip, true)
            fuzzyCompare(caption.y + tile.titleItem.y + tile.titleItem.baselineOffset, Ui.px(71 + 11), 0.01)
            fuzzyCompare(tile.subtitleItem.y + tile.subtitleItem.baselineOffset, Ui.px(11 + 11), 0.01)
            fuzzyCompare(tile.titleItem.width, caption.width, 0.01)
            fuzzyCompare(tile.subtitleItem.width, caption.width, 0.01)
            compare(tile.titleItem.elide, Text.ElideNone)
            compare(tile.titleItem.wrapMode, Text.NoWrap)
            if (c < 2) {
                const next = row.cellAt(c + 1)
                verify(cell.x + caption.x + caption.width <= next.x)
            }
        }
        const tile = row.cellAt(0).tileItem
        tile.title = "a very long album title that runs past the tile width"
        verify(tile.titleItem.implicitWidth > tile.captionItem.width)
        fuzzyCompare(tile.titleItem.width, tile.captionItem.width, 0.01)
        fuzzyCompare(tile.captionItem.width, Ui.px(71), 0.01)
    }

    function test_letter_cell_no_caption() {
        const page = open({ pivotIndex: 1 })
        const row = tc.rowAt(page.albumsPane, 0)
        const cell = row.cellAt(0)
        compare(cell.letter, true)
        compare(cell.height, Ui.px(71))
        compare(cell.tileItem, null)
        compare(cell.letterItem.height, Ui.px(71))
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

    function test_dividers_focusable() {
        const page = open()
        compare(page.entryAt(0, 0).kind, "divider")
        compare(page.entryAt(0, 0).letter, "a")
        compare(page.entryAt(0, 1).kind, "artist")
        compare(page.rowIndex, 0)
        verify(router.dispatch("up", false) === false)
        compare(page.rowIndex, 0)
        const divider = tc.rowAt(page.artistsPane, 0)
        compare(divider.isFocused, true)
        compare(page.focusBar.visible, true)
        fuzzyCompare(page.focusBar.y, Ui.px(128), 0.01)
        compare(page.focusBar.rowHeight, Ui.px(40))
        fuzzyCompare(divider.dividerItem.color.a, 1.0, 1 / 255)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 1)
        compare(divider.isFocused, false)
        fuzzyCompare(divider.dividerItem.color.a, 1.0, 1 / 255)
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 3)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 4)
        compare(page.entryAt(0, 4).kind, "divider")
        compare(page.entryAt(0, 4).letter, "b")
        compare(tc.rowAt(page.artistsPane, 4).isFocused, true)
        fuzzyCompare(page.focusBar.y, Ui.px(128) + 4 * Ui.px(40), 0.01)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 5)
        verify(router.dispatch("up", false))
        compare(page.rowIndex, 4)
        verify(router.dispatch("up", false))
        compare(page.rowIndex, 3)
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
        compare(entry.rowItem.primary, page.entryAt(0, 3).title)
        compare(entry.rowItem.countLine, true)
        compare(entry.rowItem.ring, false)
        compare(entry.rowItem.ringItem, null)
        compare(entry.rowItem.secondaryItem.font.capitalization, Font.AllUppercase)
        compare(entry.rowItem.secondaryItem.font.pixelSize, Ui.fontPx(11))
        fuzzyCompare(entry.rowItem.secondaryItem.color.a, 0.6, 1 / 255)
        compare(tc.rowAt(page.artistsPane, 1).rowItem.focused, false)
    }

    function test_divider_a_opens_grid() {
        const page = open({ pivotIndex: 2, player: fakePlayer, library: fakeLibrary })
        compare(page.rowIndex, 0)
        compare(page.entryAt(2, 0).kind, "divider")
        compare(router.dispatch("x", false), true)
        compare(page.sheet.shown, false)
        compare(router.dispatch("y", false), true)
        compare(counters.sheetActions.length, 0)
        for (let i = 0; i < 4; i++)
            verify(router.dispatch("down", false))
        compare(page.rowIndex, 4)
        compare(page.entryAt(2, 4).kind, "divider")
        compare(page.entryAt(2, 4).letter, "b")
        verify(router.dispatch("a", false))
        compare(counters.jumps, 1)
        compare(counters.jumpLetter, "b")
        compare(fakePlayer.calls.length, 0)
        compare(counters.played.length, 0)
        compare(page.jumpGrid.shown, true)
        compare(page.jumpGrid.focusLetter, "b")
        verify(router.dispatch("right", false))
        compare(page.jumpGrid.focusLetter, "c")
        verify(router.dispatch("a", false))
        compare(page.jumpGrid.shown, false)
        compare(page.rowIndex, 8)
        compare(page.entryAt(2, 8).kind, "divider")
        compare(page.entryAt(2, 8).letter, "c")
        compare(page.songsPane.contentY, 8 * page.songsPane.rowPitch)
        compare(page.jump.letter, "c")
        verify(router.dispatch("a", false))
        compare(counters.jumps, 2)
        compare(counters.jumpLetter, "c")
        compare(page.jumpGrid.focusLetter, "c")
        verify(router.dispatch("b", false))
        compare(page.jumpGrid.shown, false)
        compare(page.rowIndex, 8)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 9)
        compare(page.entryAt(2, 9).kind, "song")
        verify(router.dispatch("a", false))
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].startId, 7)
    }

    function test_songs_rows_and_play() {
        const page = open({ pivotIndex: 2, player: fakePlayer })
        compare(page.entryAt(2, 0).kind, "divider")
        compare(page.rowIndex, 0)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 1)
        const first = page.songsPane.itemAtIndex(1)
        compare(first.rowItem.number, "1")
        compare(first.rowItem.primary, "a song 1")
        compare(first.rowItem.secondary, "Artist 1")
        compare(first.rowItem.countLine, false)
        compare(first.rowItem.secondaryItem.font.capitalization, Font.MixedCase)
        compare(first.rowItem.textX, Ui.px(20 + 24))
        verify(router.dispatch("a", false))
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "all")
        compare(fakePlayer.calls[0].mode, "play")
        compare(fakePlayer.calls[0].startId, 1)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.rowIndex, 5)
        router.dispatch("a", false)
        compare(fakePlayer.calls[1].kind, "all")
        compare(fakePlayer.calls[1].startId, 4)
        compare(page.songsPane.itemAtIndex(5).rowItem.number, "4")
    }

    function test_song_play_without_player() {
        const page = open({ pivotIndex: 2 })
        router.dispatch("down", false)
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
        compare(page.jumpGrid.shown, true)
        verify(router.dispatch("b", false))
        compare(page.jumpGrid.shown, false)
        compare(pages.depth, 2)
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
        compare(page.rowIndex, 0)
        verify(router.dispatch("down", false))
        router.dispatch("a", false)
        compare(counters.opened[1].kind, "artist")
        compare(counters.opened[1].id, 1)
        page.switchPivot(3)
        compare(page.pivotIndex, 3)
        compare(tc.rowAt(page.genresPane, 1).rowItem.bold, true)
        verify(router.dispatch("down", false))
        router.dispatch("a", false)
        compare(counters.opened.length, 2)
        compare(counters.played.length, 1)
        compare(counters.played[0].kind, "genre")
        compare(counters.played[0].id, 1)
        compare(router.dispatch("left", false), false)
    }

    function test_genre_plays_and_sheet() {
        const page = open({ pivotIndex: 3, player: fakePlayer, library: fakeLibrary })
        verify(router.dispatch("down", false))
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "genre")
        compare(fakePlayer.calls[0].id, 1)
        compare(fakePlayer.calls[0].mode, "play")
        compare(fakePlayer.calls[0].startId, -1)
        verify(router.dispatch("x", false))
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "add to queue"]))
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 2)
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "genre")
        router.dispatch("y", false)
        compare(counters.sheetActions[1].action, "add to queue")
        compare(counters.sheetActions[1].kind, "genre")
    }

    function test_playlist_rows_play() {
        playlists.clear()
        playlists.append({ id: 5, title: "road", subtitle: "2 songs", artPath: "", section: "playlists" })
        const page = open({ pivotIndex: 4, player: fakePlayer, library: fakeLibrary, playlistsModel: playlists })
        compare(page.playlistRows.length, 2)
        compare(page.playlistRows[1].kind, "playlist")
        compare(page.playlistRows[1].title, "road")
        compare(page.playlistRows[1].subtitle, "2 songs")
        compare(page.playlistsPane.count, 2)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "playlist")
        compare(fakePlayer.calls[0].id, 5)
        verify(router.dispatch("x", false))
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "shuffle", "pin to quickplay"]))
    }

    function test_grid_last_row_col_clamp() {
        const page = open({ pivotIndex: 1 })
        const lastRow = page.albumsPane.count - 1
        const lastLen = albums.cellCount(lastRow)
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
        const row = tc.rowAt(page.playlistsPane, 0)
        compare(row.rowItem.ring, true)
        verify(row.rowItem.ringItem !== null)
        compare(row.rowItem.bold, true)
        compare(row.rowItem.focused, true)
        compare(row.rowItem.primaryRole.name, "listPlaylist")
        fakePlayer.queue.count = 1
        compare(page.playlistRows[0].subtitle, "1 song")
        compare(row.rowItem.secondary, "1 song")
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
        compare(page.rowIndex, page.songsPane.pageRows)
        compare(page.rowIndex, 8)
        compare(page.entryAt(2, page.rowIndex).kind, "divider")
        compare(page.jump.letter, page.entryAt(2, page.rowIndex).letter)
        compare(page.jump.letter, "c")
        compare(page.jump.text.font.pixelSize, Ui.fontPx(120))
        page.songsPane.completeScroll()
        compare(page.jump.hold.running, true)
        tryCompare(page.jump, "shown", false, 2000)
        verify(router.dispatch("rt", false))
        compare(page.rowIndex, 16)
        compare(page.entryAt(2, 16).kind, "divider")
        compare(page.jump.letter, "e")
        page.songsPane.completeScroll()
        verify(router.dispatch("lt", false))
        compare(page.rowIndex, 8)
        page.songsPane.completeScroll()
        verify(router.dispatch("lt", false))
        compare(page.jump.shown, true)
        compare(page.rowIndex, 0)
        compare(page.jump.letter, "a")
    }

    function test_jump_letter_grid() {
        const page = open({ animated: true, pivotIndex: 1 })
        page.gridCol = 1
        verify(router.dispatch("rt", false))
        compare(page.jump.shown, true)
        compare(page.jump.letter, albums.cellAt(page.rowIndex, page.gridCol).letter)
    }

    function test_back_view_opens_jump_grid() {
        const page = open()
        verify(router.dispatch("back", false))
        compare(counters.jumps, 1)
        compare(page.jumpGrid.shown, true)
        router.dispatch("b", false)
        compare(page.jumpGrid.shown, false)
        page.switchPivot(4)
        router.dispatch("back", false)
        compare(counters.jumps, 1)
        compare(page.jumpGrid.shown, false)
    }

    function test_sheet_actions() {
        const page = open({ pivotIndex: 2, player: fakePlayer })
        verify(router.dispatch("down", false))
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "a song 1")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "pin to quickplay"]))
        router.dispatch("down", false)
        compare(page.sheet.focusIndex, 1)
        compare(page.rowIndex, 1)
        router.dispatch("down", false)
        compare(page.sheet.focusIndex, 2)
        router.dispatch("a", false)
        compare(page.sheet.shown, false)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "song")
        compare(counters.sheetActions[0].id, 1)
        router.dispatch("x", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "all")
        compare(fakePlayer.calls[0].startId, 1)
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions[1].action, "pin to quickplay")
        router.dispatch("y", false)
        compare(counters.sheetActions[2].action, "add to queue")
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions[3].action, "play next")
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

    function test_sheet_unpin_label() {
        fakeLibrary.pinned = ["song:1"]
        const page = open({ pivotIndex: 2, library: fakeLibrary })
        verify(router.dispatch("down", false))
        verify(router.dispatch("x", false))
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "unpin from quickplay"]))
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "unpin from quickplay")
        compare(counters.sheetActions[0].kind, "song")
        compare(counters.sheetActions[0].id, 1)
        router.dispatch("down", false)
        verify(router.dispatch("x", false))
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "pin to quickplay"]))
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions[1].action, "pin to quickplay")
        compare(counters.sheetActions[1].id, 2)
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
        songs.push({ id: 99, title: "z song", subtitle: "Artist", artPath: "", section: "Z" })
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
        tryCompare(tc.rowAt(pane, 0), "enter", 1, 2000)
        tryCompare(tc.rowAt(pane, last), "enter", 1, 2000)
        songs.push({ id: 100, title: "z song 2", subtitle: "Artist", artPath: "", section: "Z" })
        wait(20)
        compare(tc.rowAt(pane, 0).enter, 1)
    }

    function test_f12_reset_during_scan() {
        const page = open({ animated: true, pivotIndex: 0, library: fakeLibrary })
        fakeLibrary.scanning = true
        artists.push({ id: 99, title: "z artist", subtitle: "1 album, 1 song", artPath: "", section: "Z" })
        wait(20)
        compare(tc.rowAt(page.artistsPane, 0).enter, 1)
        fakeLibrary.scanning = false
        tryVerify(tc.entering(page.artistsPane, 0), 1000)
        compare(tc.rowAt(page.artistsPane, 1).stagger, 50)
        tryCompare(tc.rowAt(page.artistsPane, 1), "enter", 1, 2000)
    }

    function test_f12_refreshes_shown_pane() {
        const page = open({ animated: true, pivotIndex: 0, library: fakeLibrary })
        page.switchPivot(2)
        tryCompare(page, "switching", false, 3000)
        tryCompare(tc.rowAt(page.songsPane, 0), "enter", 1, 2000)
        fakeLibrary.scanning = true
        fakeLibrary.scanning = false
        artists.push({ id: 99, title: "z artist", subtitle: "", artPath: "", section: "Z" })
        tryVerify(tc.entering(page.songsPane, 0), 1000)
        compare(tc.rowAt(page.artistsPane, 0).enter, 1)
        tryCompare(tc.rowAt(page.songsPane, 0), "enter", 1, 2000)
    }

    function test_f12_not_without_scan() {
        const page = open({ animated: true, pivotIndex: 2, library: fakeLibrary })
        songs.push({ id: 99, title: "z song", subtitle: "Artist", artPath: "", section: "Z" })
        wait(20)
        compare(tc.rowAt(page.songsPane, 0).enter, 1)
    }

    function test_unbound_change_ignored() {
        const page = open({ animated: true, pivotIndex: 2, library: fakeLibrary })
        fakeLibrary.scanning = true
        fakeLibrary.scanning = false
        artists.push({ id: 99, title: "z artist", subtitle: "", artPath: "", section: "Z" })
        wait(20)
        compare(tc.rowAt(page.songsPane, 0).enter, 1)
        compare(page.artistsPane.count, 0)
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
        fuzzyCompare(bar.y, Ui.px(128), 0.01)
        router.dispatch("down", false)
        fuzzyCompare(bar.y, Ui.px(128) + Ui.px(40), 0.01)
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
        compare(page.rowIndex, songs.count - 1)
        const top = list.focusIndex * list.rowPitch - list.contentY
        verify(top >= 0)
        verify(top + list.rowPitch <= list.height + 0.01)
        compare(router.dispatch("down", false), false)
    }

    function test_reused_rows_settle() {
        const page = open({ pivotIndex: 2 })
        compare(page.songsPane.reuseItems, true)
        for (let i = 0; i < 40; i++)
            router.dispatch("down", false)
        page.songsPane.completeScroll()
        page.songsPane.forceLayout()
        const row = tc.rowAt(page.songsPane, page.rowIndex)
        compare(row.enter, 1)
        compare(row.rowItem.focused, true)
        compare(row.rowItem.primary, page.entryAt(2, page.rowIndex).title)
    }

    function test_t2a_first_show() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(hubComponent, { animated: true, pivotIndex: 2 })
        compare(page.arriving, true)
        compare(pages.phase, "exit")
        const pane = page.songsPane
        const first = tc.rowAt(pane, 0)
        compare(first.enter, 0)
        compare(first.shift, Motion.pivotShift)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, Ui.px(34), 0.01)
        compare(first.opacity, 0)
        tryCompare(pages, "phase", "idle", 3000)
        compare(page.arrived, true)
        compare(page.arriving, false)
        compare(first.enterDuration, 380)
        compare(first.stagger, 0)
        const third = tc.rowAt(pane, 3)
        compare(third.stagger, 3 * 22)
        compare(third.enterDuration, 380)
        tryCompare(first, "enter", 1, 2000)
        tryCompare(third, "enter", 1, 2000)
        fuzzyCompare(third.mapToItem(third.parent, 0, 0).x - third.x, 0, 0.01)
        compare(third.opacity, 1)
    }

    function test_t2a_rest_without_animation() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(hubComponent, { animated: false, pivotIndex: 2 })
        compare(page.arriving, false)
        const first = tc.rowAt(page.songsPane, 0)
        compare(first.enter, 1)
        compare(first.x, 0)
        compare(first.opacity, 1)
        pages.completeAll()
        compare(first.enter, 1)
        pages.animate = false
        const rest = open({ animated: true })
        compare(rest.arriving, false)
        compare(tc.rowAt(rest.artistsPane, 0).enter, 1)
    }

    function test_t2a_screen_off_lands() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(hubComponent, { animated: true })
        tryCompare(pages, "phase", "idle", 3000)
        const first = tc.rowAt(page.artistsPane, 0)
        verify(first.enter < 1)
        page.screenOff = true
        compare(first.enter, 1)
        compare(first.opacity, 1)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, 0, 0.01)
    }

    function test_t4_header_fly_up() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(hubComponent, { animated: true })
        pages.completeAll()
        tryCompare(page.strip, "laidOut", true, 2000)
        compare(page.header.y, 0)
        compare(page.strip.y, 0)
        verify(router.dispatch("b", false))
        compare(pages.phase, "exit")
        const holder = pages.outgoing
        compare(holder.flying, true)
        verify(holder.content === page)
        tryVerify(function() { return holder.fly > 0 && holder.fly < 1 }, 1000)
        const fly = holder.fly
        verify(page.header.y < 0)
        fuzzyCompare(page.header.y, fly * Motion.headerOutShift * page.height, Ui.px(1))
        fuzzyCompare(page.header.opacity, 1 - fly, 0.05)
        fuzzyCompare(page.strip.y, page.header.y, Ui.px(1))
        fuzzyCompare(page.strip.opacity, page.header.opacity, 0.05)
        compare(holder.toAngle, Motion.turnstileInAngle)
        tryCompare(pages, "phase", "idle", 3000)
        compare(pages.depth, 1)
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

    function test_touch_song_row_plays() {
        const page = open({ pivotIndex: 2 })
        tc.tapAt(tc.rowAt(page.songsPane, 1))
        compare(page.songsPane.focusIndex, 1)
        compare(counters.played.length, 1)
        compare(counters.played[0].kind, "song")
        compare(counters.played[0].id, 1)
    }

    function test_touch_divider_opens_jump_grid() {
        const page = open({ pivotIndex: 2 })
        tc.tapAt(tc.rowAt(page.songsPane, 0))
        compare(page.jumpGrid.shown, true)
        compare(counters.jumps, 1)
    }

    function test_touch_album_cell_opens_album() {
        const page = open({ pivotIndex: 1 })
        const row = tc.rowAt(page.albumsPane, 0)
        tc.tapAt(row.cellAt(1))
        compare(page.gridCol, 1)
        compare(counters.opened.length, 1)
        compare(counters.opened[0].kind, "album")
        compare(counters.opened[0].id, 1000)
    }

    function test_touch_letter_tile_opens_jump_grid() {
        const page = open({ pivotIndex: 1 })
        tc.tapAt(tc.rowAt(page.albumsPane, 0).cellAt(0))
        compare(page.gridCol, 0)
        compare(page.jumpGrid.shown, true)
    }

    function test_touch_label_switches_on_release() {
        const page = open({ animated: true })
        const label = page.strip.labelItem(2)
        mousePress(label, Ui.px(4), label.height / 2)
        compare(page.pivotIndex, 0)
        mouseRelease(label, Ui.px(4), label.height / 2)
        wait(0)
        wait(0)
        compare(page.pivotIndex, 2)
        compare(page.switching, true)
        page.completeAll()
        compare(page.shownPivot, 2)
    }

    function test_touch_label_past_slop_switches_nothing() {
        const page = open({ animated: true })
        const label = page.strip.labelItem(2)
        mousePress(label, Ui.px(4), label.height / 2)
        mouseMove(label, Ui.px(34), label.height / 2, -1, Qt.LeftButton)
        mouseRelease(label, Ui.px(34), label.height / 2)
        wait(0)
        wait(0)
        compare(page.pivotIndex, 0)
        compare(page.switching, false)
    }

    function test_touch_active_label_does_nothing() {
        const page = open({ animated: true })
        const label = page.strip.labelItem(0)
        tc.tapAt(label, Ui.px(4), label.height / 2)
        compare(page.pivotIndex, 0)
        compare(page.switching, false)
    }

    function test_touch_header_word_pops() {
        const page = open()
        compare(pages.depth, 2)
        tc.tapHeaderWord(page)
        compare(pages.depth, 1)
    }

    function test_touch_right_of_header_word_does_nothing() {
        const page = open({ pivotIndex: 4 })
        const end = page.header.childrenRect.x + page.header.childrenRect.width
        verify(end < page.header.width - Ui.px(10))
        tc.tapAt(page.header, page.header.width - Ui.px(5), page.header.textBaseline - Ui.px(4))
        compare(pages.depth, 2)
    }

    function test_touch_drag_clamps_and_pad_acts() {
        const page = open({ pivotIndex: 2 })
        const pane = page.songsPane
        tc.dragUp(page, page.width / 2, page.height - Ui.px(20), pane.height + Ui.px(40))
        verify(pane.contentY > 0)
        const first = Math.ceil(pane.contentY / pane.rowPitch - 0.001)
        compare(pane.focusIndex, first)
        verify(page.focusableAt(2, pane.focusIndex))
        compare(page.focusIndex, pane.focusIndex)
        const entry = page.rowEntry(2, pane.focusIndex)
        compare(entry.kind, "song")
        compare(page.focusedRow.id, entry.id)
        verify(router.dispatch("x", false))
        compare(page.sheet.title, entry.title)
        router.dispatch("b", false)
        page.sheet.complete()
        router.dispatch("a", false)
        compare(counters.played.length, 1)
        compare(counters.played[0].id, entry.id)
    }

    function test_touch_row_after_label_tap_acts_on_new_pivot() {
        const page = open({ animated: true })
        const label = page.strip.labelItem(2)
        tc.tapAt(label, Ui.px(4), label.height / 2)
        compare(page.pivotIndex, 2)
        wait(100)
        const pane = page.songsPane
        tc.tapAt(page, page.width / 2, pane.y + 1.5 * pane.rowPitch - pane.contentY)
        compare(page.shownPivot, 2)
        compare(counters.played.length, 1)
        compare(counters.played[0].id, 1)
    }

    function test_touch_during_feather_acts_on_landed_row() {
        const page = open({ animated: true, pivotIndex: 2 })
        const pane = page.songsPane
        pane.feather(1)
        verify(tc.rowAt(pane, 1).enter < 1)
        tc.tapAt(page, page.width / 2, pane.y + 1.5 * pane.rowPitch - pane.contentY)
        compare(tc.rowAt(pane, 1).enter, 1)
        compare(counters.played.length, 1)
        compare(counters.played[0].id, 1)
    }

    function test_touch_drag_from_grid_gutter_scrolls() {
        const page = open({ pivotIndex: 1 })
        const x = Ui.px(Ui.hubEdge + Ui.tileS + Ui.gutter / 2)
        tc.dragUp(page, x, page.height - Ui.px(20), Ui.px(100))
        verify(page.albumsPane.contentY > 0)
        compare(counters.opened, [])
    }

    function openSongSheet() {
        const page = open({ animated: true, pivotIndex: 2 })
        page.songsPane.snapTo(1)
        page.syncFocus()
        verify(router.dispatch("x", false))
        page.sheet.complete()
        compare(page.sheet.shown, true)
        waitForRendering(page)
        return page
    }

    function scrimY(page) {
        const pane = page.songsPane
        const y = pane.y + 1.5 * pane.rowPitch
        verify(y < page.sheet.panel.y)
        return y
    }

    function test_touch_sheet_scrim_closes() {
        const page = tc.openSongSheet()
        const focus = page.songsPane.focusIndex
        tc.tapAt(page, page.width / 2, tc.scrimY(page))
        compare(page.sheet.shown, false)
        compare(counters.played, [])
        compare(counters.sheetActions, [])
        compare(pages.depth, 2)
        compare(page.pivotIndex, 2)
        compare(page.songsPane.focusIndex, focus)
    }

    function test_touch_two_scrim_taps_keep_depth() {
        const page = tc.openSongSheet()
        const focus = page.songsPane.focusIndex
        const y = tc.scrimY(page)
        tc.tapAt(page, page.width / 2, y)
        compare(page.sheet.shown, false)
        verify(page.sheet.animating)
        tc.tapAt(page, page.width / 2, y)
        compare(pages.depth, 2)
        compare(page.sheet.shown, false)
        compare(counters.played, [])
        compare(page.pivotIndex, 2)
        compare(page.songsPane.focusIndex, focus)
    }

    function test_touch_sheet_item_runs() {
        const page = tc.openSongSheet()
        const sheet = page.sheet
        const n = sheet.items.length
        tc.tapAt(sheet.panel, sheet.panel.width / 2, sheet.panel.height - sheet.edge - (n - 1 - 0.5) * sheet.pitch)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, sheet.items[1])
        compare(counters.sheetActions[0].id, 1)
    }
}
