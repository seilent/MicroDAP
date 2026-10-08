import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "Ui"

    function setWindow(w, h, s) {
        Ui.userScale = s
        Ui.setSurface(w, h)
    }

    function init() {
        Ui.turn = 0
        Ui.orientationActive = false
        Ui.landscapeTurn = 1
        setWindow(640, 960, 1.0)
    }

    function cleanup() {
        Ui.turn = 0
        Ui.orientationActive = false
        Ui.landscapeTurn = Env.landscapeTurn
    }

    function test_turn_surface() {
        Ui.setSurface(640, 960)
        Ui.turn = 1
        compare(Ui.surfaceW, 640)
        compare(Ui.surfaceH, 960)
        compare(Ui.windowW, 960)
        compare(Ui.windowH, 640)
        compare(Ui.landscape, true)
        fuzzyCompare(Ui.u, 2.0, 1e-9)
        fuzzyCompare(Ui.sMax, 1.0, 1e-9)
        Ui.turn = 3
        compare(Ui.windowW, 960)
        compare(Ui.landscape, true)
        Ui.turn = 2
        compare(Ui.windowW, 640)
        compare(Ui.windowH, 960)
        compare(Ui.landscape, false)
        fuzzyCompare(Ui.sMax, 1.1, 1e-9)
    }

    function test_landscape_turn_default() {
        compare(Env.landscapeTurn, 1)
    }

    function test_padDirection_data() {
        return [
            { tag: "L1 portrait", l: 1, t: 0, map: ["right", "down", "left", "up"] },
            { tag: "L1 landscape", l: 1, t: 1, map: ["up", "right", "down", "left"] },
            { tag: "L1 portrait flipped", l: 1, t: 2, map: ["left", "up", "right", "down"] },
            { tag: "L1 landscape flipped", l: 1, t: 3, map: ["down", "left", "up", "right"] },
            { tag: "L3 portrait", l: 3, t: 0, map: ["left", "up", "right", "down"] },
            { tag: "L3 landscape", l: 3, t: 3, map: ["up", "right", "down", "left"] },
            { tag: "L3 portrait flipped", l: 3, t: 2, map: ["right", "down", "left", "up"] },
            { tag: "L3 landscape flipped", l: 3, t: 1, map: ["down", "left", "up", "right"] }
        ]
    }

    function test_padDirection(data) {
        Ui.landscapeTurn = data.l
        Ui.turn = data.t
        Ui.orientationActive = true
        compare(Ui.padTurn, ((data.l - data.t) % 4 + 4) % 4)
        compare(Ui.padDirection("up"), data.map[0])
        compare(Ui.padDirection("right"), data.map[1])
        compare(Ui.padDirection("down"), data.map[2])
        compare(Ui.padDirection("left"), data.map[3])
        compare(Ui.padDirection("a"), "a")
        compare(Ui.padDirection("lb"), "lb")
    }

    function test_padDirection_inactive() {
        Ui.landscapeTurn = 1
        Ui.turn = 2
        Ui.orientationActive = false
        compare(Ui.padTurn, 0)
        compare(Ui.padDirection("up"), "up")
        compare(Ui.padDirection("left"), "left")
    }

    function test_windowW_read_only() {
        let threw = false
        try {
            Ui.windowW = 100
        } catch (e) {
            threw = true
        }
        verify(threw)
        compare(Ui.windowW, 640)
    }

    function test_u_data() {
        return [
            { tag: "640x960", w: 640, h: 960, u: 2.0, landscape: false, canvasW: 320, canvasH: 480 },
            { tag: "1080x1920", w: 1080, h: 1920, u: 3.97, landscape: false, canvasW: 272, canvasH: 483 },
            { tag: "960x640", w: 960, h: 640, u: 2.0, landscape: true, canvasW: 480, canvasH: 320 },
            { tag: "1920x1080", w: 1920, h: 1080, u: 3.97, landscape: true, canvasW: 483, canvasH: 272 },
            { tag: "1280x800", w: 1280, h: 800, u: 2.67, landscape: true, canvasW: 480, canvasH: 300 }
        ]
    }

    function test_u(data) {
        setWindow(data.w, data.h, 1.0)
        fuzzyCompare(Ui.u, data.u, 0.005)
        compare(Ui.landscape, data.landscape)
        fuzzyCompare(Ui.canvasW, data.canvasW, 1)
        fuzzyCompare(Ui.canvasH, data.canvasH, 1)
    }

    function test_sMax_data() {
        return [
            { tag: "640x960", w: 640, h: 960, sMax: 1.1 },
            { tag: "960x640", w: 960, h: 640, sMax: 1.0 },
            { tag: "1080x1920", w: 1080, h: 1920, sMax: 1.0 },
            { tag: "1920x1080", w: 1920, h: 1080, sMax: 1.0 },
            { tag: "1280x800", w: 1280, h: 800, sMax: 1.0 }
        ]
    }

    function test_sMax(data) {
        setWindow(data.w, data.h, 1.0)
        fuzzyCompare(Ui.sMax, data.sMax, 1e-9)
    }

    function test_userScale() {
        setWindow(640, 960, 1.1)
        fuzzyCompare(Ui.u, 2.2, 1e-9)
        fuzzyCompare(Ui.canvasW, 291, 1)
        fuzzyCompare(Ui.canvasH, 436, 1)
    }

    function test_fontPx_floor() {
        setWindow(640, 960, 0.8)
        fuzzyCompare(Ui.u, 1.6, 1e-9)
        compare(Ui.fontPx(7), 12)
        compare(Ui.fontPx(8), 13)
        setWindow(640, 960, 1.0)
        compare(Ui.fontPx(7), 14)
        compare(Ui.font(Ui.overlayLabel).pixelSize, 14)
    }

    function test_px() {
        compare(Ui.px(16), 32)
        compare(Ui.px(Ui.hubEdge), 40)
    }

    function test_tileS() {
        compare(Ui.tileS, 71)
        compare(Ui.gridCols, 3)
        setWindow(1920, 1080, 1.0)
        compare(Ui.tileS, 60)
        compare(Ui.gridCols, 6)
    }

    function test_letterSpacing() {
        for (const role of Ui.roleList) {
            const expected = (role === Ui.hubHeader ? -1.0 : -0.5) * Ui.u
            fuzzyCompare(Ui.letterSpacing(role), expected, 1e-6)
            fuzzyCompare(Ui.font(role).letterSpacing, expected, 1e-6)
        }
        fuzzyCompare(Ui.font(Ui.hubHeader).letterSpacing, -2.0, 1e-6)
        fuzzyCompare(Ui.font(Ui.listPrimary).letterSpacing, -1.0, 1e-6)
    }

    function test_roles() {
        compare(Ui.roleList.length, 38)
        for (const role of Ui.roleList) {
            verify(role.size > 0, role.name)
            verify(["Light", "Regular", "Bold"].indexOf(role.style) >= 0, role.name)
            verify(role.alpha > 0 && role.alpha <= 1, role.name)
            compare(Ui.font(role).family, "Selawik")
            compare(Ui.font(role).styleName, role.style)
            compare(Ui.font(role).capitalization, role.caps)
        }
        compare(Ui.hubHeader.size, 128)
        compare(Ui.listPrimary.size, 14)
        compare(Ui.listSecondary.size, 11)
        compare(Ui.npTitle.size, 26)
        compare(Ui.jumpLetter.size, 120)
        compare(Ui.letterGlyph.style, "Regular")
        compare(Ui.detailName.size, 11)
        compare(Ui.detailName.style, "Bold")
        compare(Ui.detailName.caps, Font.AllUppercase)
        compare(Ui.npCount.size, 13)
        compare(Ui.npCount.caps, Font.AllUppercase)
        compare(Ui.watermark.style, "Light")
        fuzzyCompare(Ui.watermark.alpha, 0.08, 1e-6)
    }

    function test_colors() {
        verify(Qt.colorEqual(Ui.bg, "#111111"))
        verify(Qt.colorEqual(Ui.bgElevated, "#181818"))
        verify(Qt.colorEqual(Ui.tile, "#202020"))
        verify(Qt.colorEqual(Ui.tilePressed, "#2A2A2A"))
        verify(Qt.colorEqual(Ui.border, "#2C2C2C"))
        verify(Qt.colorEqual(Ui.accent, "#FA2A4D"))
        verify(Qt.colorEqual(Ui.accentBright, "#FF4D6B"))
        verify(Qt.colorEqual(Ui.accentForHue(350), "#FA2A4D"))
        verify(Qt.colorEqual(Ui.brightForHue(350), "#FF4D6B"))
        compare(Ui.hueOnGrid(347.6), 350)
        compare(Ui.hueOnGrid(356), 0)
        compare(Ui.hueOnGrid(-10), 350)
        compare(Ui.stepHue(350, 1), 0)
        compare(Ui.stepHue(0, -1), 350)
        for (let h = 0; h < 360; h += 10)
            verify(Ui.luminance(Ui.hsvChannels(h, Ui.accentSaturation(h), 0.98)) >= 0.149)
        compare(Ui.accentSaturation(0), 0.83)
        verify(Ui.accentSaturation(240) < 0.83)
        fuzzyCompare(Ui.textHigh.a, 0.85, 1 / 255)
        fuzzyCompare(Ui.textMid.a, 0.6, 1 / 255)
        fuzzyCompare(Ui.textLow.a, 0.4, 1 / 255)
        fuzzyCompare(Ui.textFaint.a, 0.08, 1 / 255)
        fuzzyCompare(Ui.white(0.6).a, 0.6, 1 / 255)
        fuzzyCompare(Ui.scrimSheet.a, 0.55, 1 / 255)
        fuzzyCompare(Ui.scrimOverlay.a, 0.8, 1 / 255)
    }

    function test_sizes() {
        compare(Ui.edge, 16)
        compare(Ui.hubEdge, 20)
        compare(Ui.headerBand, 70)
        compare(Ui.headerXHeightBottom, 66)
        compare(Ui.pivotTop, 74)
        compare(Ui.pivotBottom, 106)
        compare(Ui.pivotBaseline, 95)
        compare(Ui.pivotGap, 16)
        compare(Ui.pivotLeadGap, 22)
        verify(Ui.hubEdge - Ui.pivotLeadGap < 0)
        compare(Ui.contentTop, 128)
        compare(Ui.rowHeight, 40)
        compare(Ui.gutter, 12)
        compare(Ui.npArt, 168)
        compare(Ui.npProgressY, 324)
        compare(Ui.npTitleY, 362)
        compare(Ui.npRingY, 440)
        compare(Ui.jumpCols, 5)
        fuzzyCompare(Ui.jumpTile, (480 - 32 - 7 * 8) / 8, 1e-6)
        fuzzyCompare(Ui.qpPeekScale, 0.30, 1e-9)
        compare(Ui.qpPeekY, 130)
        fuzzyCompare(Ui.qpPeekW, 64.8, 1e-6)
        compare(Ui.detailNameBaseline, 110)
        compare(Ui.detailRingInset, 8)
        compare(Ui.detailTitleDrop, 28)
        compare(Ui.detailTitlePitch, 15)
        compare(Ui.albumArtX, 16)
        compare(Ui.albumArtTop, 155)
        compare(Ui.albumRowsTop, 283)
        compare(Ui.queueCountBaseline, 95)
        compare(Ui.queueRowsTop, 128)
        compare(Ui.overlayRingY, 216)
        compare(Ui.overlayMinusY, 372)
        compare(Motion.dialogScrim, 0.8)
        setWindow(1920, 1080, 1.0)
        compare(Ui.headerBand, 70)
        compare(Ui.headerXHeightBottom, 66)
        compare(Ui.pivotTop, 58)
        compare(Ui.pivotBottom, 86)
        compare(Ui.pivotBaseline, 79)
        compare(Ui.contentTop, 98)
        compare(Ui.jumpCols, 9)
        fuzzyCompare(Ui.npArt, 166, 0.01)
        compare(Ui.detailNameBaseline, 94)
        compare(Ui.albumArtTop, 98)
        compare(Ui.albumRowsTop, 98)
        compare(Ui.queueCountBaseline, 79)
        compare(Ui.queueRowsTop, 98)
        fuzzyCompare(Ui.overlayRingY, 216 * Ui.canvasH / 480, 1e-6)
        fuzzyCompare(Ui.overlayMinusY, 372 * Ui.canvasH / 480, 1e-6)
        verify(Ui.overlayMinusY < Ui.canvasH)
    }

    function test_fonts() {
        if (!Ui.fontsReady)
            skip("Selawik not found in " + Ui.fontDir)
        compare(Ui.light.font.family, "Selawik")
        compare(Ui.light.font.styleName, "Light")
        compare(Ui.semilight.font.styleName, "Semilight")
        compare(Ui.regular.font.styleName, "Regular")
        compare(Ui.semibold.font.styleName, "Semibold")
        compare(Ui.bold.font.styleName, "Bold")
    }
}
