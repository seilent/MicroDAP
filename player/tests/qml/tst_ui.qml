import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "Ui"

    function setWindow(w, h, s) {
        Ui.userScale = s
        Ui.windowW = w
        Ui.windowH = h
    }

    function init() {
        setWindow(640, 960, 1.0)
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
        compare(Ui.roleList.length, 35)
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
    }

    function test_colors() {
        verify(Qt.colorEqual(Ui.bg, "#111111"))
        verify(Qt.colorEqual(Ui.bgElevated, "#181818"))
        verify(Qt.colorEqual(Ui.tile, "#202020"))
        verify(Qt.colorEqual(Ui.tilePressed, "#2A2A2A"))
        verify(Qt.colorEqual(Ui.border, "#2C2C2C"))
        verify(Qt.colorEqual(Ui.accent, "#FA2A55"))
        verify(Qt.colorEqual(Ui.accentBright, "#FF4D79"))
        compare(Ui.accentOptions.length, 5)
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
        compare(Ui.npTitleY, 352)
        compare(Ui.npRingY, 440)
        compare(Ui.jumpCols, 5)
        fuzzyCompare(Ui.jumpTile, (320 - 32 - 32) / 5, 1e-6)
        setWindow(1920, 1080, 1.0)
        compare(Ui.headerBand, 70)
        compare(Ui.headerXHeightBottom, 66)
        compare(Ui.pivotTop, 58)
        compare(Ui.pivotBottom, 86)
        compare(Ui.pivotBaseline, 79)
        compare(Ui.contentTop, 98)
        compare(Ui.jumpCols, 9)
        fuzzyCompare(Ui.npArt, 166, 0.01)
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
