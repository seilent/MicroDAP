import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "Components"
    when: windowShown
    visible: true
    width: 640
    height: 960

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.windowW = w
        Ui.windowH = h
    }

    function init() {
        setWindow(640, 960)
        strip.animated = false
        strip.activeIndex = 0
        row.focused = false
        row.animated = false
        tile.focused = false
        letter.focused = false
        ring.focused = false
        ring.size = Ui.npRing
        ring.animated = true
        ring.glyph = "shuffle"
    }

    CroppedHeader {
        id: header

        text: "music"
    }

    PivotStrip {
        id: strip

        labels: ["albums", "artists", "songs", "genres", "playlists"]
    }

    ListRow {
        id: row

        y: Ui.px(Ui.contentTop)
        number: "1"
        primary: "Title"
        secondary: "Artist"
        trailing: "3:48"
    }

    Tile {
        id: tile

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(Ui.contentTop + Ui.rowHeight)
        title: "harmonics vol.2 a long album title"
        subtitle: "various artists"
    }

    LetterTile {
        id: letter

        x: Ui.px(Ui.hubEdge + Ui.tileS + Ui.gutter)
        y: Ui.px(Ui.contentTop + Ui.rowHeight)
        letter: "a"
    }

    Rectangle {
        id: ringBoard

        y: Ui.px(300)
        width: ring.width
        height: ring.height
        color: Ui.bg

        Ring {
            id: ring

            glyph: "shuffle"
        }
    }

    Rectangle {
        id: onBoard

        x: Ui.px(Ui.backRing)
        y: Ui.px(300)
        width: ringOn.width
        height: ringOn.height
        color: Ui.bg

        Ring {
            id: ringOn

            glyph: "shuffle"
            on: true
        }
    }

    Glyph {
        id: glyph

        y: Ui.px(360)
        name: "battery"
        level: 0.5
    }

    function test_header_geometry() {
        compare(header.height, Ui.px(Ui.headerBand))
        compare(header.height, 140)
        fuzzyCompare(header.textBaseline, Ui.px(66), 0.5)
        fuzzyCompare(header.textBaseline, 132, 0.5)
        fuzzyCompare(header.textX, Ui.px(Ui.hubEdge), 0.5)
        verify(header.clip)
        setWindow(960, 640)
        compare(Ui.landscape, true)
        compare(header.height, Ui.px(70))
        fuzzyCompare(header.textBaseline, Ui.px(66), 0.5)
        fuzzyCompare(header.textBaseline, 132, 0.5)
        fuzzyCompare(header.textX, Ui.px(Ui.hubEdge), 0.5)
    }

    function test_header_xheight_visible() {
        const em = Ui.fontPx(Ui.hubHeader.size)
        compare(em, 256)
        const xHeightTop = header.textBaseline - 0.5 * em
        verify(xHeightTop >= 0, "x-height top " + xHeightTop)
        fuzzyCompare(xHeightTop, Ui.px(2), 0.5)
        verify(header.textBaseline - 0.74 * em < 0)
        verify(header.textBaseline <= header.height)
    }

    function test_header_shift() {
        header.shift = 10
        tryVerify(function() { return Math.abs(header.textX - Ui.px(Ui.hubEdge + 10)) < 0.5 }, 2000)
        header.shift = 0
        tryVerify(function() { return Math.abs(header.textX - Ui.px(Ui.hubEdge)) < 0.5 }, 2000)
    }

    function test_header_animated_off() {
        header.animated = false
        header.shift = 10
        fuzzyCompare(header.textX, Ui.px(Ui.hubEdge + 10), 0.5)
        header.shift = 0
        fuzzyCompare(header.textX, Ui.px(Ui.hubEdge), 0.5)
        header.animated = true
        verify(tile.animated)
        verify(letter.animated)
    }

    function test_pivot_geometry() {
        const label = strip.labelItem(0)
        verify(label !== null)
        fuzzyCompare(label.y + label.baselineOffset, Ui.px(95), 0.5)
        fuzzyCompare(label.y + label.baselineOffset, 190, 0.5)
        fuzzyCompare(strip.activeLabelX, Ui.px(Ui.hubEdge), 0.5)
        compare(label.font.styleName, "Bold")
        compare(label.font.capitalization, Font.AllUppercase)
        fuzzyCompare(label.color.a, Ui.pivotActive.alpha, 1 / 255)
        const other = strip.labelItem(1)
        compare(other.font.styleName, "Light")
        fuzzyCompare(other.color.a, Ui.pivotInactive.alpha, 1 / 255)
        tryVerify(function() { return Math.abs(other.x - (label.x + label.width) - Ui.px(Ui.pivotGap)) < 0.5 }, 2000)
    }

    function test_pivot_switch() {
        strip.activeIndex = 2
        tryVerify(() => Math.abs(strip.activeLabelX - Ui.px(Ui.hubEdge)) < 0.5, 1000)
        compare(strip.labelItem(2).font.styleName, "Bold")
        compare(strip.labelItem(0).font.styleName, "Light")
        verify(strip.travel < 0)
        fuzzyCompare(strip.headerShift, Motion.pivotHeaderParallax * strip.travel / Ui.u, 1e-6)
        verify(strip.next())
        compare(strip.activeIndex, 3)
        strip.activeIndex = 4
        verify(!strip.next())
        strip.activeIndex = 0
        verify(!strip.previous())
    }

    function test_pivot_animated_travel() {
        strip.animated = true
        strip.activeIndex = 1
        tryVerify(() => Math.abs(strip.activeLabelX - Ui.px(Ui.hubEdge)) < 0.5, 1500)
        strip.animated = false
    }

    function rightEdge(item) {
        return item.mapToItem(strip, item.width, 0).x
    }

    function test_pivot_previous_off_screen() {
        const first = strip.labelItem(0)
        const second = strip.labelItem(1)
        const third = strip.labelItem(2)
        verify(tc.rightEdge(first) > 0)
        strip.activeIndex = 1
        tryVerify(() => Math.abs(strip.activeLabelX - Ui.px(Ui.hubEdge)) < 0.5, 1000)
        verify(tc.rightEdge(first) <= 0, "first label right edge " + tc.rightEdge(first))
        fuzzyCompare(tc.rightEdge(first), Ui.px(Ui.hubEdge - Ui.pivotLeadGap), 0.5)
        fuzzyCompare(second.x - (first.x + first.width), Ui.px(Ui.pivotLeadGap), 0.5)
        fuzzyCompare(third.x - (second.x + second.width), Ui.px(Ui.pivotGap), 0.5)
        strip.activeIndex = 2
        tryVerify(() => Math.abs(strip.activeLabelX - Ui.px(Ui.hubEdge)) < 0.5, 1000)
        verify(tc.rightEdge(second) <= 0, "second label right edge " + tc.rightEdge(second))
        verify(tc.rightEdge(first) <= 0)
        fuzzyCompare(third.x - (second.x + second.width), Ui.px(Ui.pivotLeadGap), 0.5)
        fuzzyCompare(second.x - (first.x + first.width), Ui.px(Ui.pivotGap), 0.5)
        strip.activeIndex = 0
        tryVerify(() => Math.abs(strip.activeLabelX - Ui.px(Ui.hubEdge)) < 0.5, 1000)
        fuzzyCompare(second.x - (first.x + first.width), Ui.px(Ui.pivotGap), 0.5)
    }

    function test_row_geometry() {
        compare(row.height, Ui.px(Ui.rowHeight))
        compare(row.height, 80)
        compare(row.width, 640)
        fuzzyCompare(row.textX, Ui.px(Ui.hubEdge + 24), 0.5)
        row.number = ""
        fuzzyCompare(row.textX, Ui.px(Ui.hubEdge), 0.5)
        row.number = "1"
    }

    function test_row_focus() {
        fuzzyCompare(row.primaryAlpha, Ui.listPrimary.alphaDim, 1e-6)
        fuzzyCompare(row.primaryColor.a, 0.85, 1 / 255)
        row.focused = true
        fuzzyCompare(row.primaryAlpha, Ui.listPrimary.alpha, 1e-6)
        fuzzyCompare(row.primaryColor.a, 1.0, 1 / 255)
        row.playing = true
        verify(Qt.colorEqual(row.primaryColor, Ui.accent))
        row.playing = false
    }

    function test_row_bold() {
        row.bold = true
        compare(row.primaryRole.style, "Bold")
        compare(row.secondaryRole.caps, Font.AllUppercase)
        row.bold = false
        compare(row.primaryRole.style, "Regular")
    }

    function test_tile_geometry() {
        compare(tile.width, Ui.px(Ui.tileS))
        compare(tile.width, 142)
        compare(tile.height, tile.width)
        fuzzyCompare(tile.captionItem.x, Ui.px(Ui.tileS + 4), 0.5)
        fuzzyCompare(tile.captionItem.width, Ui.px(Ui.tileS + Ui.gutter - 4), 0.5)
        fuzzyCompare(tile.captionItem.width, 158, 0.5)
        verify(tile.captionItem.clip)
        compare(tile.borderWidth, Ui.px(Ui.focusBorder))
        compare(tile.borderWidth, 4)
    }

    function test_tile_focus() {
        fuzzyCompare(tile.captionAlpha, Ui.gridCaption.alpha, 1e-6)
        tile.focused = true
        fuzzyCompare(tile.captionAlpha, 1.0, 1e-6)
    }

    function test_letter_tile() {
        compare(letter.width, Ui.px(Ui.tileS))
        compare(letter.borderWidth, Ui.px(Ui.letterTileBorder))
        fuzzyCompare(letter.borderAlpha, Ui.alphaLow, 1e-6)
        letter.focused = true
        compare(letter.borderWidth, Ui.px(Ui.focusBorder))
        fuzzyCompare(letter.borderAlpha, 1.0, 1e-6)
        letter.empty = true
        fuzzyCompare(letter.letterAlpha, Ui.alphaLow, 1e-6)
        letter.empty = false
    }

    function test_ring_geometry() {
        compare(ring.width, Ui.px(32))
        compare(ring.width, 64)
        compare(ring.strokeWidth, Ui.px(3))
        fuzzyCompare(ring.ringAlpha, Ui.alphaMid, 1e-6)
        ring.focused = true
        compare(ring.strokeWidth, Ui.px(4))
        fuzzyCompare(ring.ringAlpha, 1.0, 1e-6)
        ring.size = Ui.backRing
        compare(ring.width, Ui.px(64))
        compare(ring.width, 128)
        compare(ring.strokeWidth, Ui.px(4))
        ring.focused = false
        compare(ring.strokeWidth, Ui.px(3))
    }

    function test_ring_glyph_swap() {
        ring.glyph = "repeat"
        verify(ring.swapping)
        verify(ring.iconItem.opacity < 1 || ring.iconItem.scale < 1)
        tryCompare(ring, "swapping", false, 2000)
        compare(ring.iconItem.opacity, 1)
        compare(ring.iconItem.scale, 1)
    }

    function test_ring_animated_off() {
        ring.animated = false
        ring.glyph = "repeat"
        verify(!ring.swapping)
        compare(ring.iconItem.opacity, 1)
        compare(ring.iconItem.scale, 1)
        ring.animated = true
        ring.glyph = "heart"
        verify(ring.swapping)
        ring.animated = false
        verify(!ring.swapping)
        compare(ring.iconItem.opacity, 1)
        compare(ring.iconItem.scale, 1)
    }

    function ringInk(img, size, radius, pred) {
        const c = size / 2
        let n = 0
        for (let y = 0; y < size; y++) {
            for (let x = 0; x < size; x++) {
                const dx = x + 0.5 - c
                const dy = y + 0.5 - c
                if (dx * dx + dy * dy > radius * radius)
                    continue
                if (pred(img.red(x, y), img.green(x, y), img.blue(x, y)))
                    n++
            }
        }
        return n
    }

    function test_ring_glyph_rendered() {
        tryCompare(ring, "swapping", false, 2000)
        waitForRendering(ringBoard)
        compare(ring.iconItem.opacity, 1)
        compare(ring.iconItem.scale, 1)
        compare(ring.iconItem.name, "shuffle")
        verify(ring.iconItem.known)
        const img = grabImage(ringBoard)
        verify(img !== null)
        compare(img.width, 64)
        compare(img.height, 64)
        const ink = ringInk(img, 64, 20, (r, g, b) => r > 60 && g > 60 && b > 60)
        verify(ink > 0, "glyph pixels inside the ring: " + ink)
        const dark = ringInk(img, 64, 20, (r, g, b) => r < 40 && g < 40 && b < 40)
        verify(dark > 0)
    }

    function test_ring_glyph_on_rendered() {
        tryCompare(ringOn, "swapping", false, 2000)
        waitForRendering(onBoard)
        compare(ringOn.iconItem.opacity, 1)
        compare(ringOn.iconItem.scale, 1)
        const img = grabImage(onBoard)
        verify(img !== null)
        const accent = ringInk(img, 64, 20, (r, g, b) => r > 120 && r - g > 60)
        verify(accent > 0, "accent pixels inside the ring: " + accent)
        const white = ringInk(img, 64, 20, (r, g, b) => r > 120 && g > 120 && b > 120)
        compare(white, 0)
    }

    function test_tile_caption_clip() {
        const right = tile.x + tile.captionItem.x + tile.captionItem.width
        fuzzyCompare(right, 348, 0.5)
        fuzzyCompare(right, letter.x + letter.width, 0.5)
        verify(letter.x < right)
        verify(tile.x + tile.width < letter.x)
    }

    function test_letter_glyph_position() {
        const item = letter.letterItem
        fuzzyCompare(item.x + item.width, letter.width - Ui.px(Ui.letterInsetX), 0.5)
        fuzzyCompare(item.x + item.width, 130, 0.5)
        fuzzyCompare(item.y + item.baselineOffset, letter.height - Ui.px(Ui.letterInsetBaseline), 0.5)
        fuzzyCompare(item.y + item.baselineOffset, 126, 0.5)
    }

    function test_row_text_insets() {
        compare(row.primaryItem.y, Ui.px(Ui.rowTextInset))
        compare(row.primaryItem.y, 8)
        fuzzyCompare(row.secondaryItem.y + row.secondaryItem.height, row.height - Ui.px(Ui.rowTextInset), 0.5)
        fuzzyCompare(row.secondaryItem.y + row.secondaryItem.height, 72, 0.5)
        verify(row.secondaryBaseline > row.primaryBaseline)
        verify(row.secondaryBaseline < row.height)
        compare(row.dotItem.x, Ui.px(Ui.hubEdge - Ui.playingDotOffset))
        compare(row.dotItem.x, 28)
        compare(row.dotItem.width, 8)
        fuzzyCompare(row.dotItem.y, row.primaryBaseline - Ui.px(Ui.playingDot), 0.5)
        compare(row.ringItem.width, Ui.px(Ui.rowRing))
        compare(row.ringItem.width, 40)
    }

    function test_glyph_box() {
        compare(glyph.width, Ui.px(Ui.glyphBox))
        compare(glyph.width, 64)
        compare(glyph.height, glyph.width)
        compare(glyph.strokeWidth, Ui.px(Ui.glyphStroke))
        compare(glyph.strokeWidth, 8)
        verify(glyph.known)
        glyph.size = 16
        compare(glyph.width, Ui.px(16))
        compare(glyph.strokeWidth, Ui.px(2))
        glyph.size = Ui.glyphBox
    }

    function test_glyph_names() {
        const names = ["back", "play", "pause", "next", "previous", "shuffle", "repeat", "repeatOne",
                       "heart", "bluetooth", "battery", "plus", "minus", "search", "power"]
        for (const n of names) {
            glyph.name = n
            verify(glyph.known, n)
        }
        glyph.name = "nothing"
        verify(!glyph.known)
        glyph.name = "battery"
    }

    function test_scale_landscape() {
        setWindow(1920, 1080)
        fuzzyCompare(Ui.u, 3.97, 0.005)
        fuzzyCompare(row.height, 40 * Ui.u, 0.01)
        fuzzyCompare(tile.width, 60 * Ui.u, 0.01)
        fuzzyCompare(ring.width, 32 * Ui.u, 0.01)
        fuzzyCompare(ring.strokeWidth, 3 * Ui.u, 0.01)
        fuzzyCompare(glyph.width, 32 * Ui.u, 0.01)
        fuzzyCompare(glyph.strokeWidth, 4 * Ui.u, 0.01)
        fuzzyCompare(header.height, 70 * Ui.u, 0.01)
        fuzzyCompare(header.textBaseline, Ui.px(66), 0.5)
        fuzzyCompare(header.textBaseline, 66 * Ui.u, 0.5)
        fuzzyCompare(strip.labelItem(0).y + strip.labelItem(0).baselineOffset, Ui.px(Ui.pivotBaseline), 0.5)
        fuzzyCompare(Ui.pivotBaseline, 79, 1e-6)
    }

    Component {
        id: lateRing
        Ring {}
    }

    function test_ring_first_glyph_not_animated() {
        const r = createTemporaryObject(lateRing, tc)
        r.glyph = "pause"
        const icon = r.iconItem
        compare(icon.opacity, 1)
        compare(icon.scale, 1)
        r.glyph = "play"
        verify(icon.opacity < 1 || icon.scale < 1)
    }
}
