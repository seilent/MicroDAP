import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "JumpGrid"
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
        id: counters

        property int jumps: 0
    }

    Component {
        id: hubComponent

        MusicHub {
            artistsModel: artists
            albumsModel: albums
            songsModel: songs
            genresModel: genres
            onJumpRequested: counters.jumps++
        }
    }

    Component {
        id: blankComponent

        Page {}
    }

    function entries(prefix, letters, per) {
        const out = []
        let id = 1
        for (const letter of letters) {
            for (let i = 0; i < per; i++) {
                out.push({ id: id, title: letter + " " + prefix + " " + id, subtitle: "Artist", artPath: "", section: letter.toUpperCase() })
                id++
            }
        }
        return out
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
        for (const m of [artists, songs, genres, albums])
            m.active = false
        albums.columns = 3
        artists.load(tc.entries("artist", ["a", "c", "h", "m"], 6), "artist")
        albums.load(tc.entries("album", ["#", "b", "k"], 4))
        songs.load(tc.entries("song", ["a", "b", "c", "d", "e", "f", "g", "h"], 5), "song")
        genres.load(tc.entries("genre", ["r"], 1), "genre")
        counters.jumps = 0
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

    function test_tokens_portrait() {
        compare(Ui.jumpCols, 5)
        compare(Ui.jumpGap, 8)
        compare(Ui.jumpCells, 38)
        compare(Ui.jumpExitCells, 2)
        compare(Ui.jumpRows, 8)
        fuzzyCompare(Ui.jumpTile, (480 - 32 - 7 * 8) / 8, 0.01)
        verify(Ui.jumpTile <= (320 - 32 - 32) / 5)
        fuzzyCompare(Ui.jumpTop, 16, 0.01)
        verify(Ui.jumpTop + Ui.jumpGridH <= Ui.canvasH - Ui.edge + 0.01)
    }

    function test_tokens_landscape() {
        tc.setWindow(1920, 1080)
        compare(Ui.jumpCols, 9)
        compare(Ui.jumpRows, 5)
        fuzzyCompare(Ui.jumpTile, Math.min((Ui.canvasW - 32 - 64) / 9, (Ui.canvasH - 32 - 32) / 5), 0.01)
        verify(Ui.jumpTop >= Ui.edge)
        verify(Ui.jumpTop + Ui.jumpGridH <= Ui.canvasH - Ui.edge + 0.01)
    }

    function test_tokens_landscape_960() {
        tc.setWindow(960, 640)
        compare(Ui.jumpCols, 9)
        compare(Ui.jumpRows, 5)
        fuzzyCompare(Ui.jumpTile, (480 - 32 - 64) / 9, 0.01)
        fuzzyCompare(Ui.jumpTop, (320 - (5 * Ui.jumpTile + 4 * 8)) / 2, 0.01)
        verify(Ui.jumpTop + Ui.jumpGridH <= Ui.canvasH - Ui.edge + 0.01)
    }

    function test_open_layout_and_available() {
        const page = open()
        verify(router.dispatch("back", false))
        const grid = page.jumpGrid
        compare(grid.shown, true)
        compare(counters.jumps, 1)
        compare(grid.letters.length, 38)
        compare(grid.letters[0], "#")
        compare(grid.letters[1], "a")
        compare(JSON.stringify(grid.available), JSON.stringify(["a", "c", "h", "m"]))
        compare(grid.exitText.text, "exit")
        compare(grid.exitText.font.pixelSize, Ui.fontPx(7))
        compare(grid.exitText.font.styleName, "Bold")
        compare(grid.exitText.font.capitalization, Font.AllUppercase)
        fuzzyCompare(grid.exitText.x, Ui.px(16), 0.01)
        const hash = grid.cellAt(0)
        verify(hash !== null)
        fuzzyCompare(hash.x, Ui.px(16 + 2 * (Ui.jumpTile + 8)), 0.01)
        fuzzyCompare(hash.y, Ui.px(Ui.jumpTop), 0.01)
        fuzzyCompare(hash.width, Ui.px(Ui.jumpTile), 0.01)
        compare(hash.empty, true)
        fuzzyCompare(hash.letterAlpha, 0.4, 0.001)
        const a = grid.cellAt(1)
        compare(a.empty, false)
        compare(a.focused, true)
        fuzzyCompare(a.letterAlpha, 1.0, 0.001)
        const c = grid.cellAt(3)
        fuzzyCompare(c.x, Ui.px(16), 0.01)
        fuzzyCompare(c.y, Ui.px(Ui.jumpTop + Ui.jumpTile + 8), 0.01)
        compare(grid.cellAt(26).letter, "z")
        fuzzyCompare(grid.cellAt(26).y, Ui.px(Ui.jumpTop + 5 * (Ui.jumpTile + 8)), 0.01)
        compare(grid.letters.slice(27).join(""), "あかさたなはまやら" + "わ漢")
        const ka = grid.cellAt(28)
        compare(ka.letter, "か")
        compare(ka.empty, true)
        fuzzyCompare(ka.x, Ui.px(16), 0.01)
        fuzzyCompare(ka.y, Ui.px(Ui.jumpTop + 6 * (Ui.jumpTile + 8)), 0.01)
        const kanji = grid.cellAt(37)
        compare(kanji.letter, "漢")
        fuzzyCompare(kanji.x, Ui.px(16 + 4 * (Ui.jumpTile + 8)), 0.01)
        fuzzyCompare(kanji.y, Ui.px(Ui.jumpTop + 7 * (Ui.jumpTile + 8)), 0.01)
        verify(kanji.y + kanji.height <= Ui.px(Ui.canvasH - Ui.edge) + 0.5)
        compare(grid.cellAt(38), null)
    }

    function test_kana_letters_jump() {
        artists.load(tc.entries("artist", ["a", "か", "は", "漢"], 2), "artist")
        const page = open()
        compare(page.entryAt(0, 3).kind, "divider")
        compare(page.entryAt(0, 3).letter, "か")
        router.dispatch("back", false)
        const grid = page.jumpGrid
        compare(JSON.stringify(grid.available), JSON.stringify(["a", "か", "は", "漢"]))
        compare(grid.focusLetter, "a")
        verify(router.dispatch("right", false))
        compare(grid.focusLetter, "か")
        verify(router.dispatch("right", false))
        compare(grid.focusLetter, "は")
        verify(router.dispatch("down", false))
        compare(grid.focusLetter, "漢")
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("down", false), false)
        verify(router.dispatch("up", false))
        compare(grid.focusLetter, "は")
        verify(router.dispatch("left", false))
        compare(grid.focusLetter, "か")
        verify(router.dispatch("a", false))
        compare(grid.shown, false)
        compare(page.rowIndex, 3)
        compare(page.jump.letter, "か")
        router.dispatch("back", false)
        compare(grid.focusLetter, "か")
        router.dispatch("right", false)
        router.dispatch("right", false)
        compare(grid.focusLetter, "漢")
        router.dispatch("a", false)
        compare(page.entryAt(0, 9).kind, "divider")
        compare(page.entryAt(0, 9).letter, "漢")
        compare(page.rowIndex, 9)
        compare(page.jump.letter, "漢")
    }

    function test_focus_starts_on_current_letter_and_skips_empty() {
        const page = open()
        for (let i = 0; i < 7; i++)
            router.dispatch("down", false)
        compare(page.rowIndex, 7)
        compare(page.entryAt(0, 7).kind, "divider")
        compare(page.focusLetter(), "c")
        router.dispatch("back", false)
        const grid = page.jumpGrid
        compare(grid.focusLetter, "c")
        verify(router.dispatch("right", false))
        compare(grid.focusLetter, "h")
        verify(router.dispatch("right", false))
        compare(grid.focusLetter, "m")
        compare(router.dispatch("right", false), false)
        compare(grid.focusLetter, "m")
        verify(router.dispatch("left", false))
        compare(grid.focusLetter, "h")
        verify(router.dispatch("up", false))
        compare(grid.focusLetter, "c")
        verify(router.dispatch("up", false))
        compare(grid.exitFocused, true)
        compare(grid.focusLetter, "")
        compare(router.dispatch("up", false), false)
        verify(router.dispatch("down", false))
        compare(grid.focusLetter, "c")
        verify(router.dispatch("down", false))
        compare(grid.focusLetter, "h")
        verify(router.dispatch("down", false))
        compare(grid.focusLetter, "m")
        compare(router.dispatch("down", false), false)
        compare(page.rowIndex, 7)
    }

    function test_exit_focus_and_a() {
        const page = open()
        router.dispatch("down", false)
        const before = page.rowIndex
        compare(before, 1)
        router.dispatch("back", false)
        const grid = page.jumpGrid
        compare(grid.focusLetter, "a")
        compare(grid.exitFocused, false)
        compare(grid.exitBar.opacity, 0)
        fuzzyCompare(grid.exitText.color.a, 0.6, 1 / 255)
        verify(router.dispatch("left", false))
        compare(grid.exitFocused, true)
        compare(grid.focusIndex, grid.exitIndex)
        compare(grid.focusLetter, "")
        compare(grid.cellAt(1).focused, false)
        compare(grid.exitBar.opacity, 1)
        fuzzyCompare(grid.exitBar.x, Ui.px(16 - 8), 0.01)
        fuzzyCompare(grid.exitBar.y, Ui.px(Ui.jumpTop), 0.01)
        fuzzyCompare(grid.exitBar.width, Ui.px(2), 0.01)
        fuzzyCompare(grid.exitBar.height, Ui.px(Ui.jumpTile), 0.01)
        fuzzyCompare(grid.exitText.color.a, 1.0, 1 / 255)
        fuzzyCompare(grid.exitText.x, Ui.px(16), 0.01)
        compare(grid.exitText.font.pixelSize, Ui.fontPx(7))
        compare(router.dispatch("left", false), false)
        compare(router.dispatch("up", false), false)
        compare(grid.exitFocused, true)
        verify(router.dispatch("right", false))
        compare(grid.focusLetter, "a")
        verify(router.dispatch("left", false))
        compare(grid.exitFocused, true)
        verify(router.dispatch("down", false))
        compare(grid.focusLetter, "c")
        verify(router.dispatch("up", false))
        compare(grid.exitFocused, true)
        verify(router.dispatch("a", false))
        compare(grid.shown, false)
        compare(page.rowIndex, before)
        compare(page.jump.shown, false)
        compare(pages.depth, 2)
    }

    function test_exit_from_hash() {
        const page = open({ pivotIndex: 1 })
        router.dispatch("back", false)
        const grid = page.jumpGrid
        compare(grid.focusLetter, "#")
        verify(router.dispatch("left", false))
        compare(grid.exitFocused, true)
        verify(router.dispatch("right", false))
        compare(grid.focusLetter, "#")
        verify(router.dispatch("left", false))
        verify(router.dispatch("a", false))
        compare(grid.shown, false)
        compare(page.rowIndex, 0)
        compare(page.gridCol, 0)
    }

    function test_exit_when_empty() {
        artists.load([], "artist")
        const page = open()
        router.dispatch("back", false)
        const grid = page.jumpGrid
        compare(grid.shown, true)
        compare(JSON.stringify(grid.available), JSON.stringify([]))
        compare(grid.exitFocused, true)
        verify(router.dispatch("a", false))
        compare(grid.shown, false)
    }

    function test_a_jumps_and_shows_letter() {
        const page = open({ animated: true })
        router.dispatch("back", false)
        const grid = page.jumpGrid
        tryCompare(grid, "animating", false, 2000)
        compare(grid.progress, 1)
        compare(grid.visualOpacity, 1)
        fuzzyCompare(page.body.opacity, 0.4, 0.001)
        router.dispatch("right", false)
        compare(grid.focusLetter, "c")
        verify(router.dispatch("a", false))
        compare(grid.shown, false)
        compare(page.entryAt(0, 7).kind, "divider")
        compare(page.entryAt(0, 7).letter, "c")
        compare(page.rowIndex, 7)
        compare(page.artistsPane.contentY, 7 * page.artistsPane.rowPitch)
        compare(page.jump.shown, true)
        compare(page.jump.letter, "c")
        compare(page.jump.hold.running, true)
        compare(page.jump.hold.interval, 350)
        compare(page.jump.fadeAnimation.duration, 380)
        tryCompare(grid, "visible", false, 2000)
        fuzzyCompare(page.body.opacity, 1, 0.001)
    }

    function test_b_closes_focus_unchanged() {
        const page = open()
        router.dispatch("down", false)
        const before = page.rowIndex
        router.dispatch("back", false)
        compare(page.jumpGrid.shown, true)
        router.dispatch("right", false)
        verify(router.dispatch("b", false))
        compare(page.jumpGrid.shown, false)
        compare(page.rowIndex, before)
        compare(pages.depth, 2)
        compare(page.jump.shown, false)
    }

    function test_swivel_motion() {
        const page = open({ animated: true })
        const grid = page.jumpGrid
        compare(grid.openDuration, 350)
        compare(grid.closeDuration, 250)
        router.dispatch("back", false)
        verify(grid.animating)
        compare(grid.opening, true)
        verify(grid.angle <= 0)
        verify(grid.angle >= -30)
        compare(grid.swivel.axis, Qt.XAxis)
        fuzzyCompare(grid.swivel.hinge, grid.height / 2, 0.01)
        tryCompare(grid, "animating", false, 2000)
        compare(grid.angle, 0)
        router.dispatch("b", false)
        compare(grid.opening, false)
        verify(grid.animating)
        compare(grid.visible, true)
        tryCompare(grid, "animating", false, 2000)
        compare(grid.visible, false)
        compare(grid.progress, 0)
    }

    function test_grid_pivot_jump() {
        const page = open({ pivotIndex: 1 })
        router.dispatch("back", false)
        const grid = page.jumpGrid
        compare(JSON.stringify(grid.available), JSON.stringify(["#", "b", "k"]))
        compare(grid.focusLetter, "#")
        router.dispatch("right", false)
        router.dispatch("right", false)
        compare(grid.focusLetter, "k")
        router.dispatch("a", false)
        const cell = albums.cellAt(page.rowIndex, page.gridCol)
        compare(cell.kind, "letter")
        compare(cell.letter, "k")
    }

    function test_not_on_playlists() {
        const page = open({ pivotIndex: 4 })
        compare(router.dispatch("back", false), true)
        compare(page.jumpGrid.shown, false)
        compare(counters.jumps, 0)
    }

    function test_screen_off_lands() {
        const page = open({ animated: true })
        router.dispatch("back", false)
        verify(page.jumpGrid.animating)
        page.screenOff = true
        compare(page.jumpGrid.animating, false)
        compare(page.jumpGrid.visible, false)
        compare(page.jumpGrid.shown, true)
        page.screenOff = false
        compare(page.jumpGrid.visible, true)
        compare(page.jumpGrid.progress, 1)
    }
}
