import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "Detail"
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

    QtObject {
        id: fakePlayer

        property var calls: []
        property int status: 1
        property var current: ({ id: 3, title: "Sticking With You", artist: "Addison Road", album: "Addison Road", artPath: "" })
        property QtObject queue: QtObject {
            property int count: 0
        }

        function playSource(kind, id, mode, startId) {
            fakePlayer.calls = fakePlayer.calls.concat([{ kind: kind, id: id, mode: mode, startId: startId }])
            return 1
        }
    }

    QtObject {
        id: fakeLibrary

        property bool scanning: false
        property bool many: false
        readonly property var tracks: [
            [1, "This Could Be Our Day", 1, 214000],
            [2, "All That Matters", 1, 241000],
            [3, "Sticking With You", 1, 228000],
            [4, "Run", 2, 203000],
            [5, "Fight Another Day", 2, 221000],
            [6, "Where I Find You", 2, 0]
        ]

        function artistInfo(id) {
            return { id: id, name: "Addison Road", artPath: "/tmp/microdap-artist.jpg" }
        }

        function artistAlbums(id) {
            return [1, 2]
        }

        function albumInfo(id) {
            if (id === 1)
                return { id: 1, title: "Addison Road", artist: "Addison Road", year: 2008, artPath: "/tmp/microdap-album.jpg" }
            if (id === 2)
                return { id: 2, title: "Stories", artist: "Addison Road", year: 0, artPath: "" }
            return {}
        }

        function rowOf(t) {
            return { id: t[0], title: t[1], artist: "Addison Road", album: "", albumId: t[2], durationMs: t[3] }
        }

        function artistTracks(id) {
            return fakeLibrary.tracks.map((t) => t[0])
        }

        function artistAlbumRows(id) {
            return [fakeLibrary.albumInfo(1), fakeLibrary.albumInfo(2)]
        }

        function manyTracks() {
            const out = []
            for (let i = 1; i <= 40; i++)
                out.push([i, "Song " + i, 1, 200000])
            return out
        }

        function artistTrackRows(id) {
            return (fakeLibrary.many ? fakeLibrary.manyTracks() : fakeLibrary.tracks).map((t) => fakeLibrary.rowOf(t))
        }

        function trackRows(ids) {
            return fakeLibrary.tracks.filter((t) => ids.indexOf(t[0]) >= 0).map((t) => fakeLibrary.rowOf(t))
        }

        function albumTracks(id) {
            return fakeLibrary.tracks.filter((t) => t[2] === id).map((t) => fakeLibrary.rowOf(t))
        }
    }

    QtObject {
        id: counters

        property var opened: []
        property var sheetActions: []
    }

    Component {
        id: hubComponent

        MusicHub {
            artistsModel: artists
            player: fakePlayer
            library: fakeLibrary
            onOpen: (kind, id) => {
                counters.opened = counters.opened.concat([{ kind: kind, id: id }])
                if (kind === "artist")
                    pages.push(artistComponent, { artistId: id, animated: false })
            }
        }
    }

    Component {
        id: artistComponent

        ArtistDetail {
            library: fakeLibrary
            player: fakePlayer
            onOpenAlbum: (id) => {
                counters.opened = counters.opened.concat([{ kind: "album", id: id }])
                pages.push(albumComponent, { albumId: id, animated: false })
            }
            onSheetAction: (action, kind, id) => counters.sheetActions = counters.sheetActions.concat([{ action: action, kind: kind, id: id }])
        }
    }

    Component {
        id: albumComponent

        AlbumDetail {
            library: fakeLibrary
            player: fakePlayer
            onSheetAction: (action, kind, id) => counters.sheetActions = counters.sheetActions.concat([{ action: action, kind: kind, id: id }])
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
        artists.active = false
        fakeLibrary.many = false
        artists.load([{ id: 7, title: "Addison Road", subtitle: "2 albums, 6 songs", artPath: "", section: "A" }], "artist")
        fakePlayer.calls = []
        fakePlayer.current = { id: 3, title: "Sticking With You", artist: "Addison Road", album: "Addison Road", artPath: "" }
        counters.opened = []
        counters.sheetActions = []
    }

    function rowAt(list, index) {
        tryVerify(() => list.itemAtIndex(index) !== null, 1000)
        return list.itemAtIndex(index)
    }

    function textBaseline(item) {
        return item.y + item.baselineOffset
    }

    function openArtist(properties) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        if (props.artistId === undefined)
            props.artistId = 7
        pages.push(blankComponent, {})
        const page = pages.push(artistComponent, props)
        page.completeAll()
        for (const pane of page.panes)
            pane.forceLayout()
        return page
    }

    function openAlbum(properties) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        if (props.albumId === undefined)
            props.albumId = 1
        pages.push(blankComponent, {})
        const page = pages.push(albumComponent, props)
        page.completeAll()
        page.trackList.forceLayout()
        return page
    }

    function sortedIds(ids) {
        return ids.slice().sort((a, b) => a - b)
    }

    function test_artist_layout_portrait() {
        const page = openArtist()
        compare(Ui.u, 2)
        compare(page.header.text, "artist")
        compare(JSON.stringify(page.pivots), JSON.stringify(["albums", "songs"]))
        compare(page.name, "Addison Road")
        compare(page.nameItem.x, Ui.px(20))
        fuzzyCompare(tc.textBaseline(page.nameItem), Ui.px(95 + 15), 0.01)
        compare(page.nameItem.font.styleName, "Bold")
        compare(page.nameItem.font.pixelSize, Ui.fontPx(11))
        compare(page.nameItem.font.capitalization, Font.AllUppercase)
        fuzzyCompare(page.nameItem.color.a, 1.0, 1 / 255)
        compare(page.ringRepeater.count, 3)
        for (let i = 0; i < 3; i++) {
            const ring = page.ring(i)
            verify(ring !== null)
            compare(ring.x, Ui.px(20 + i * (32 + 24)))
            compare(ring.y, Ui.px(150))
            compare(ring.width, Ui.px(32))
            compare(ring.glyph, ["play", "plus", "shuffle"][i])
        }
        compare(page.labelItem.text, "in collection")
        compare(page.labelItem.x, Ui.px(20))
        fuzzyCompare(tc.textBaseline(page.labelItem), Ui.px(200), 0.01)
        compare(page.albumsPane.x, 0)
        compare(page.albumsPane.y, Ui.px(212))
        compare(page.albumsPane.rowPitch, Ui.px(124))
        compare(page.songsPane.y, Ui.px(212))
        compare(page.songsPane.rowPitch, Ui.px(40))
        compare(page.focusRow, 0)
        compare(page.ringIndex, 0)
        compare(page.ring(0).focused, true)
        compare(page.ring(1).focused, false)
        compare(page.focusBar.visible, false)
    }

    function test_artist_rows() {
        const page = openArtist()
        compare(page.albumRows.length, 2)
        compare(page.albumRows[0].title, "Addison Road")
        compare(page.albumRows[0].subtitle, "2008")
        compare(page.albumRows[1].subtitle, "")
        compare(page.songRows.length, 6)
        compare(page.songRows[0].number, "1")
        const row = tc.rowAt(page.albumsPane, 0)
        compare(row.height, Ui.px(124))
        compare(row.tileItem.x, Ui.px(20))
        compare(row.tileItem.y, 0)
        compare(row.tileItem.width, Ui.px(116))
        compare(row.tileItem.height, Ui.px(116))
        compare(row.tileItem.captioned, false)
        compare(row.ringItem.width, Ui.px(32))
        compare(row.ringItem.glyph, "play")
        fuzzyCompare(row.ringItem.x + row.ringItem.width, Ui.px(20 + 116 - 8), 0.01)
        fuzzyCompare(row.ringItem.y, Ui.px(8), 0.01)
        fuzzyCompare(row.ringItem.y + row.ringItem.height / 2, Ui.px(24), 0.01)
        compare(row.titleItem.x, Ui.px(20 + 116 + 8))
        fuzzyCompare(tc.textBaseline(row.titleItem), Ui.px(28), 0.01)
        compare(row.titleItem.width, row.width - row.titleItem.x)
        compare(row.titleItem.maximumLineCount, 2)
        compare(row.titleItem.lineHeight, Ui.px(15))
        compare(row.titleItem.lineHeightMode, Text.FixedHeight)
        compare(row.titleItem.lineCount, 1)
        compare(row.titleItem.font.styleName, "Bold")
        compare(row.titleItem.font.capitalization, Font.AllUppercase)
        fuzzyCompare(tc.textBaseline(row.subItem), Ui.px(28 + 13), 0.01)
        compare(row.subItem.text, "2008")
    }

    function test_artist_row_title_wraps() {
        const page = openArtist()
        page.albumRows = [{ kind: "album", id: 1, title: "When The World Comes Down And Keeps On Falling Forever", subtitle: "2008", artPath: "" }]
        page.albumsPane.forceLayout()
        const row = tc.rowAt(page.albumsPane, 0)
        tryCompare(row.titleItem, "lineCount", 2, 1000)
        fuzzyCompare(tc.textBaseline(row.titleItem), Ui.px(28), 0.01)
        fuzzyCompare(tc.textBaseline(row.subItem), Ui.px(28 + 15 + 13), 0.01)
        compare(row.subItem.text, "2008")
    }

    function test_artist_focus_flow() {
        const page = openArtist()
        verify(router.dispatch("down", false))
        compare(page.focusRow, 1)
        compare(page.rowIndex, 0)
        compare(page.focusBar.visible, true)
        compare(page.focusBar.x, Ui.px(20 - 8))
        compare(page.focusBar.rowY, Ui.px(212))
        compare(page.focusBar.rowHeight, Ui.px(124))
        compare(page.ring(0).focused, false)
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 1)
        verify(router.dispatch("down", false) === false)
        verify(router.dispatch("up", false))
        compare(page.rowIndex, 0)
        verify(router.dispatch("up", false))
        compare(page.focusRow, 0)
        compare(page.ring(0).focused, true)
        verify(router.dispatch("up", false) === false)
        verify(router.dispatch("right", false))
        compare(page.ringIndex, 1)
        verify(router.dispatch("right", false))
        compare(page.ringIndex, 2)
        verify(router.dispatch("right", false) === false)
        verify(router.dispatch("left", false))
        compare(page.ringIndex, 1)
        verify(router.dispatch("down", false))
        verify(router.dispatch("left", false) === false)
    }

    function test_artist_rings_act() {
        const page = openArtist()
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "artist")
        compare(fakePlayer.calls[0].id, 7)
        compare(fakePlayer.calls[0].mode, "play")
        compare(fakePlayer.calls[0].startId, -1)
        router.dispatch("right", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "artist")
        compare(counters.sheetActions[0].id, 7)
        router.dispatch("right", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 2)
        compare(fakePlayer.calls[1].kind, "artist")
        compare(fakePlayer.calls[1].mode, "shuffle")
        router.dispatch("y", false)
        compare(fakePlayer.calls.length, 3)
        compare(fakePlayer.calls[2].mode, "shuffle")
        compare(fakePlayer.calls[2].id, 7)
    }

    function test_artist_ring_swap() {
        const page = openArtist({ animated: true })
        tryCompare(page.strip, "laidOut", true, 2000)
        router.dispatch("a", false)
        verify(page.ring(0).swapping)
        tryCompare(page.ring(0), "swapping", false, 2000)
    }

    function test_artist_open_album() {
        const page = openArtist()
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.opened.length, 1)
        compare(counters.opened[0].kind, "album")
        compare(counters.opened[0].id, 2)
        compare(pages.depth, 3)
        compare(pages.currentItem.albumId, 2)
        compare(pages.currentItem.title, "Stories")
    }

    function test_artist_songs_pivot() {
        const page = openArtist()
        tryCompare(page.strip, "laidOut", true, 2000)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 1)
        compare(page.strip.activeIndex, 1)
        fuzzyCompare(page.strip.activeLabelX, Ui.px(20), 0.01)
        compare(page.switchPivot(1), false)
        compare(page.focusRow, 0)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "artist")
        compare(fakePlayer.calls[0].id, 7)
        compare(fakePlayer.calls[0].startId, 2)
        const row = tc.rowAt(page.songsPane, 1)
        compare(row.rowItem.number, "2")
        compare(row.rowItem.primary, "All That Matters")
        compare(row.rowItem.secondary, "Addison Road")
        compare(row.rowItem.focused, true)
        router.dispatch("lb", false)
        compare(page.pivotIndex, 0)
        compare(page.switchPivot(-1), false)
        compare(page.header.shift, 0)
    }

    function test_artist_pivot_feather() {
        const page = openArtist({ animated: true })
        tryCompare(page.strip, "laidOut", true, 2000)
        router.dispatch("rb", false)
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 0)
        verify(page.switching)
        tryCompare(page, "shownPivot", 1, 2000)
        const row = tc.rowAt(page.songsPane, 1)
        compare(row.stagger, 22)
        compare(row.enterDuration, 380)
        compare(row.shift, 34)
        tryCompare(row, "enter", 1, 2000)
        fuzzyCompare(row.x, 0, 0.01)
        tryCompare(page, "switching", false, 2000)
        compare(page.albumsPane.x, 0)
        compare(page.albumsPane.opacity, 1)
    }

    function test_artist_sheet() {
        const page = openArtist()
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "Addison Road")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["play", "play next", "add to queue", "shuffle", "pin to quickplay"]))
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "artist")
        compare(fakePlayer.calls[0].mode, "play")
        compare(page.sheet.shown, false)
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "artist")
        compare(counters.sheetActions[0].id, 7)
        router.dispatch("down", false)
        router.dispatch("x", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 2)
        compare(fakePlayer.calls[1].kind, "album")
        compare(fakePlayer.calls[1].id, 1)
        compare(fakePlayer.calls[1].mode, "play")
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions.length, 2)
        compare(counters.sheetActions[1].action, "pin to quickplay")
        compare(counters.sheetActions[1].kind, "album")
        compare(counters.sheetActions[1].id, 1)
        router.dispatch("y", false)
        compare(counters.sheetActions.length, 3)
        compare(counters.sheetActions[2].action, "add to queue")
        compare(counters.sheetActions[2].kind, "album")
        compare(counters.sheetActions[2].id, 1)
        compare(fakePlayer.calls.length, 2)
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions[3].action, "play next")
        compare(counters.sheetActions[3].kind, "album")
    }

    function test_detail_back_opens_queue() {
        const artist = openArtist()
        let opens = 0
        const onOpen = function() { opens++ }
        artist.openQueue.connect(onOpen)
        verify(router.dispatch("back", false))
        compare(opens, 1)
        compare(pages.currentItem, artist)
        artist.openQueue.disconnect(onOpen)
    }

    function test_artist_backdrop() {
        const page = openArtist({ animated: true })
        compare(page.artPath, "/tmp/microdap-artist.jpg")
        compare(page.backdrop.source.toString(), "")
        compare(page.backdrop.hasArt, false)
        compare(page.backdrop.holding, true)
        compare(page.backdrop.animating, false)
        verify(Qt.colorEqual(page.surface.color, Ui.bg))
        compare(page.surface.width, page.width)
        compare(page.surface.height, page.height)
    }

    function test_artist_back_pops() {
        openArtist()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_artist_landscape() {
        const page = openArtist()
        tc.setWindow(1920, 1080)
        fuzzyCompare(Ui.u, 3.97, 0.01)
        compare(page.nameItem.x, Ui.px(20))
        fuzzyCompare(tc.textBaseline(page.nameItem), Ui.px(79 + 15), 0.01)
        compare(page.ring(0).x, Ui.px(20))
        compare(page.ring(0).y, Ui.px(150))
        compare(page.labelItem.x, Ui.px(216))
        fuzzyCompare(tc.textBaseline(page.labelItem), Ui.px(98 + 0.7 * 8), 0.01)
        compare(page.albumsPane.x, Ui.px(216 - 20))
        fuzzyCompare(page.albumsPane.y, Ui.px(98 + 0.7 * 8 + 12), 0.01)
        compare(page.albumsPane.width, pages.width - Ui.px(216 - 20))
        router.dispatch("down", false)
        compare(page.focusBar.x, Ui.px(216 - 8))
    }

    function test_hub_opens_artist() {
        pages.push(hubComponent, { animated: false })
        const hub = pages.currentItem
        hub.completeAll()
        compare(hub.entryAt(0, 1).kind, "artist")
        compare(hub.rowIndex, 0)
        verify(router.dispatch("down", false))
        compare(hub.rowIndex, 1)
        router.dispatch("a", false)
        compare(counters.opened.length, 1)
        compare(counters.opened[0].kind, "artist")
        compare(counters.opened[0].id, 7)
        compare(pages.depth, 2)
        compare(pages.currentItem.artistId, 7)
        compare(pages.currentItem.name, "Addison Road")
        compare(pages.currentItem.header.text, "artist")
    }

    function test_album_layout_portrait() {
        const page = openAlbum()
        compare(page.header, undefined)
        compare(page.labelItem, undefined)
        compare(page.title, "Addison Road")
        compare(page.artist, "Addison Road")
        compare(page.year, 2008)
        compare(page.backRing.x, Ui.px(0))
        compare(page.backRing.y, Ui.px(-15))
        compare(page.backRing.width, Ui.px(64))
        compare(page.backRing.glyph, "back")
        compare(page.backRing.parent.clip, true)
        compare(page.backRing.parent.height, Ui.px(49))
        compare(page.statusStrip.row.y, Ui.px(10))
        compare(page.statusStrip.playing, true)
        compare(page.artistItem.x, Ui.px(16))
        fuzzyCompare(tc.textBaseline(page.artistItem), Ui.px(117), 0.01)
        compare(page.artistItem.font.styleName, "Bold")
        compare(page.artistItem.font.pixelSize, Ui.fontPx(20))
        compare(page.artistItem.font.capitalization, Font.AllUppercase)
        compare(page.titleItem.x, Ui.px(16))
        fuzzyCompare(tc.textBaseline(page.titleItem), Ui.px(141), 0.01)
        compare(page.titleItem.font.styleName, "Light")
        compare(page.titleItem.font.pixelSize, Ui.fontPx(20))
        compare(page.titleItem.font.capitalization, Font.AllUppercase)
        compare(page.artItem.x, Ui.px(16))
        compare(page.artItem.y, Ui.px(155))
        compare(page.artItem.width, Ui.px(116))
        compare(page.artItem.height, Ui.px(116))
        compare(page.artItem.source.toString(), "file:///tmp/microdap-album.jpg")
        compare(page.trackList.x, 0)
        compare(page.trackList.y, Ui.px(155 + 116 + 12))
        compare(page.trackList.y, Ui.px(283))
        compare(page.trackList.rowPitch, Ui.px(40))
        compare(page.rows.length, 3)
        compare(page.rowIndex, 0)
        compare(page.focusBar.visible, true)
        compare(page.focusBar.rowY, Ui.px(283))
        compare(page.backdrop.source.toString(), "")
        verify(Qt.colorEqual(page.surface.color, Ui.bg))
    }

    function test_album_rows() {
        const page = openAlbum()
        const first = tc.rowAt(page.trackList, 0)
        compare(first.rowItem.number, "1")
        compare(first.rowItem.primary, "This Could Be Our Day")
        compare(first.rowItem.trailing, "3:34")
        compare(first.rowItem.trailingItem.visible, true)
        fuzzyCompare(first.rowItem.trailingItem.x + first.rowItem.trailingItem.width, pages.width - Ui.px(20), 0.01)
        compare(first.rowItem.playingDot, false)
        compare(first.rowItem.dotItem.visible, false)
        const third = tc.rowAt(page.trackList, 2)
        compare(third.rowItem.trailing, "3:48")
        compare(third.rowItem.playingDot, true)
        compare(third.rowItem.dotItem.visible, true)
        compare(third.rowItem.dotItem.width, Ui.px(4))
        compare(third.rowItem.dotItem.height, Ui.px(4))
        compare(third.rowItem.dotItem.radius, Ui.px(2))
        compare(third.rowItem.dotItem.color, Ui.accent)
        verify(third.rowItem.dotItem.x + third.rowItem.dotItem.width <= Ui.px(20))
        compare(third.rowItem.playing, false)
        fakePlayer.current = { id: 1 }
        compare(first.rowItem.dotItem.visible, true)
        compare(third.rowItem.dotItem.visible, false)
    }

    function test_album_play_from_row() {
        const page = openAlbum()
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false) === false)
        compare(page.rowIndex, 2)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "album")
        compare(fakePlayer.calls[0].id, 1)
        compare(fakePlayer.calls[0].startId, 3)
        router.dispatch("y", false)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "album")
        compare(counters.sheetActions[0].id, 1)
        router.dispatch("x", false)
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "Sticking With You")
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(counters.sheetActions.length, 2)
        compare(counters.sheetActions[1].kind, "song")
        compare(counters.sheetActions[1].id, 3)
        compare(page.sheet.shown, false)
    }

    function test_album_empty_duration() {
        const page = openAlbum({ albumId: 2 })
        compare(page.title, "Stories")
        compare(page.rows.length, 3)
        compare(page.rows[2].duration, "")
        const row = tc.rowAt(page.trackList, 2)
        compare(row.rowItem.trailingItem.visible, false)
    }

    function test_album_back_pops() {
        openAlbum()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_album_back_opens_queue() {
        const page = openAlbum()
        let opens = 0
        const onOpen = function() { opens++ }
        page.openQueue.connect(onOpen)
        verify(router.dispatch("back", false))
        compare(opens, 1)
        compare(pages.depth, 2)
        page.openQueue.disconnect(onOpen)
    }

    function test_album_landscape() {
        const page = openAlbum()
        tc.setWindow(1920, 1080)
        compare(page.backRing.x, Ui.px(0))
        compare(page.artItem.x, Ui.px(16))
        compare(page.artItem.y, Ui.px(98))
        compare(page.artistItem.x, Ui.px(216))
        fuzzyCompare(tc.textBaseline(page.artistItem), Ui.px(60), 0.01)
        compare(page.titleItem.x, Ui.px(216))
        fuzzyCompare(tc.textBaseline(page.titleItem), Ui.px(84), 0.01)
        fuzzyCompare(page.titleItem.width, pages.width - Ui.px(216 + 16), 0.01)
        compare(page.trackList.x, Ui.px(216 - 20))
        compare(page.trackList.y, Ui.px(98))
        compare(page.focusBar.x, Ui.px(216 - 8))
        const row = tc.rowAt(page.trackList, 0)
        fuzzyCompare(page.trackList.x + row.rowItem.trailingItem.x + row.rowItem.trailingItem.width, pages.width - Ui.px(20), 0.01)
    }

    function test_screen_off_lands() {
        const page = openArtist({ animated: true })
        tryCompare(page.strip, "laidOut", true, 2000)
        router.dispatch("rb", false)
        verify(page.switching)
        page.screenOff = true
        compare(page.switching, false)
        compare(page.shownPivot, 1)
        compare(page.backdrop.holding, true)
        router.dispatch("x", false)
        compare(page.sheet.visible, false)
    }

    function test_t2a_artist_first_show() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(artistComponent, { artistId: 7, animated: true, pivotIndex: 1 })
        compare(page.arriving, true)
        const pane = page.songsPane
        const first = tc.rowAt(pane, 0)
        compare(first.enter, 0)
        compare(first.shift, Motion.pivotShift)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, Ui.px(34), 0.01)
        compare(first.opacity, 0)
        tryCompare(pages, "phase", "idle", 3000)
        compare(page.arriving, false)
        compare(first.enterDuration, 380)
        compare(first.stagger, 0)
        const third = tc.rowAt(pane, 2)
        compare(third.stagger, 2 * 22)
        tryCompare(first, "enter", 1, 2000)
        tryCompare(third, "enter", 1, 2000)
        fuzzyCompare(third.mapToItem(third.parent, 0, 0).x - third.x, 0, 0.01)
        compare(third.opacity, 1)
    }

    function test_t2a_album_first_show() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(albumComponent, { albumId: 1, animated: true })
        compare(page.arriving, true)
        const first = tc.rowAt(page.trackList, 0)
        compare(first.enter, 0)
        compare(first.opacity, 0)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, Ui.px(34), 0.01)
        tryCompare(pages, "phase", "idle", 3000)
        compare(first.enterDuration, 380)
        compare(tc.rowAt(page.trackList, 1).stagger, 22)
        tryCompare(first, "enter", 1, 2000)
        tryCompare(tc.rowAt(page.trackList, 2), "enter", 1, 2000)
        fuzzyCompare(tc.rowAt(page.trackList, 2).x, 0, 0.01)
        compare(tc.rowAt(page.trackList, 2).opacity, 1)
    }

    function test_t2a_rest_without_animation() {
        const album = openAlbum({ animated: true })
        const row = tc.rowAt(album.trackList, 0)
        compare(album.arriving, false)
        compare(row.enter, 1)
        compare(row.x, 0)
        compare(row.opacity, 1)
        pages.clear()
        const artist = openArtist({ animated: false })
        compare(artist.arriving, false)
        compare(tc.rowAt(artist.albumsPane, 0).enter, 1)
    }

    function test_f12_on_album_reload() {
        const page = openAlbum({ animated: true })
        page.albumId = 2
        compare(page.rows.length, 3)
        tryVerify(function() {
            const r = page.trackList.itemAtIndex(0)
            return r !== null && r.enter < 1
        }, 1000)
        const first = tc.rowAt(page.trackList, 0)
        compare(first.enterDuration, 220)
        compare(first.shift, Motion.listChangeShift)
        compare(tc.rowAt(page.trackList, 1).stagger, 50)
        tryCompare(first, "enter", 1, 2000)
    }

    function test_f12_on_artist_reload() {
        const page = openArtist({ animated: true, pivotIndex: 1 })
        page.artistId = 8
        tryVerify(function() {
            const r = page.songsPane.itemAtIndex(0)
            return r !== null && r.enter < 1
        }, 1000)
        const first = tc.rowAt(page.songsPane, 0)
        compare(first.enterDuration, 220)
        compare(first.shift, Motion.listChangeShift)
        tryCompare(first, "enter", 1, 2000)
        compare(tc.rowAt(page.albumsPane, 0).enter, 1)
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

    function test_touch_artist_rings() {
        const page = openArtist()
        tc.tapAt(page.ring(0))
        compare(page.focusRow, 0)
        compare(page.ringIndex, 0)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "artist")
        compare(fakePlayer.calls[0].mode, "play")
        tc.tapAt(page.ring(1))
        compare(page.ringIndex, 1)
        compare(counters.sheetActions.length, 1)
        compare(counters.sheetActions[0].action, "add to queue")
        compare(counters.sheetActions[0].kind, "artist")
        tc.tapAt(page.ring(2))
        compare(page.ringIndex, 2)
        compare(fakePlayer.calls.length, 2)
        compare(fakePlayer.calls[1].mode, "shuffle")
        const ring = page.ring(1)
        tc.tapAt(ring, ring.width + Ui.px(Ui.detailRingGap) / 2 - 1, ring.height / 2)
        compare(page.ringIndex, 1)
        compare(counters.sheetActions.length, 2)
        compare(counters.sheetActions[1].action, "add to queue")
        for (let i = 0; i < 3; i++)
            compare(typeof page.ringRepeater.itemAt(i).swapping, "boolean")
    }

    function test_touch_artist_album_row_opens_album() {
        const page = openArtist()
        tc.tapAt(tc.rowAt(page.albumsPane, 0))
        compare(counters.opened.length, 1)
        compare(counters.opened[0].kind, "album")
        compare(counters.opened[0].id, 1)
        compare(pages.depth, 3)
    }

    function test_touch_artist_songs_drag_then_a() {
        fakeLibrary.many = true
        const page = openArtist({ pivotIndex: 1 })
        compare(page.shownPivot, 1)
        verify(router.dispatch("down", false))
        compare(page.focusRow, 1)
        const pane = page.songsPane
        tc.dragUp(page, page.width / 2, page.height - Ui.px(20), pane.height + Ui.px(40))
        verify(pane.contentY > 0)
        const first = Math.ceil(pane.contentY / pane.rowPitch - 0.001)
        compare(pane.focusIndex, first)
        compare(page.focusRow, 1)
        router.dispatch("a", false)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].startId, page.songRows[first].id)
    }

    function test_touch_album_track_plays_from_row() {
        const page = openAlbum()
        tc.tapAt(tc.rowAt(page.trackList, 1))
        compare(page.trackList.focusIndex, 1)
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0].kind, "album")
        compare(fakePlayer.calls[0].id, 1)
        compare(fakePlayer.calls[0].startId, 2)
    }

    function test_touch_back_ring_pops() {
        const page = openAlbum()
        compare(pages.depth, 2)
        tc.tapAt(page.backRing)
        compare(pages.depth, 1)
    }
}
