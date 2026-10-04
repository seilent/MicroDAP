import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "Overlays"
    when: windowShown
    visible: true
    width: 640
    height: 960

    Backdrop {
        id: backdrop

        width: 640
        height: 960
    }

    StatusStrip {
        id: status

        width: 640
    }

    Sheet {
        id: sheet

        width: 640
        height: 960
        items: ["remove", "play next", "move up", "move down"]
    }

    Dialog {
        id: dialog

        width: 640
        height: 960
        title: "power"
        items: ["screen off", "game mode", "suspend", "power off"]
    }

    Toast {
        id: toast

        width: 640
    }

    VolumeOverlay {
        id: overlay

        width: 640
        height: 960
    }

    JumpLetter {
        id: jump

        width: 640
        height: 960
    }

    SignalSpy {
        id: acceptedSpy

        target: dialog
        signalName: "accepted"
    }

    SignalSpy {
        id: rejectedSpy

        target: dialog
        signalName: "rejected"
    }

    function init() {
        Ui.userScale = 1.0
        Ui.windowW = 640
        Ui.windowH = 960
        status.width = 640
        status.clock = ""
        status.playing = false
        status.paused = false
        status.bluetooth = false
        status.batteryPresent = true
        overlay.width = 640
        overlay.height = 960
        backdrop.screenOff = false
        backdrop.paused = false
        sheet.screenOff = false
        sheet.close()
        sheet.complete()
        dialog.screenOff = false
        dialog.close()
        dialog.complete()
        toast.screenOff = false
        toast.hide()
        overlay.screenOff = false
        overlay.dismiss()
        jump.screenOff = false
        jump.dismiss()
        acceptedSpy.clear()
        rejectedSpy.clear()
    }

    function test_backdrop_stops() {
        compare(backdrop.stops, [[0.0, 0.15], [0.55, 0.35], [1.0, 0.75]])
        const stops = backdrop.shade.gradient.stops
        compare(stops.length, 3)
        fuzzyCompare(stops[0].position, 0.0, 1e-9)
        fuzzyCompare(stops[1].position, 0.55, 1e-9)
        fuzzyCompare(stops[2].position, 1.0, 1e-9)
        fuzzyCompare(stops[0].color.a, 0.15, 0.005)
        fuzzyCompare(stops[1].color.a, 0.35, 0.005)
        fuzzyCompare(stops[2].color.a, 0.75, 0.005)
        fuzzyCompare(stops[2].color.r, 17 / 255, 0.005)
        compare(backdrop.shade.gradient.orientation, Gradient.Vertical)
        compare(backdrop.art.fillMode, Image.PreserveAspectCrop)
        fuzzyCompare(backdrop.amplitude, Ui.px(8), 1e-9)
    }

    function test_backdrop_loop() {
        compare(Motion.backdropScale, 20000)
        compare(Motion.backdropPan, 26000)
        fuzzyCompare(Motion.backdropScaleMax, 1.06, 1e-9)
        compare(backdrop.animating, true)
        wait(60)
        verify(backdrop.art.scale > 1.0)
        backdrop.screenOff = true
        compare(backdrop.animating, false)
        const held = backdrop.art.scale
        const heldPan = backdrop.art.pan
        wait(60)
        fuzzyCompare(backdrop.art.scale, held, 1e-9)
        fuzzyCompare(backdrop.art.pan, heldPan, 1e-9)
        backdrop.screenOff = false
        compare(backdrop.animating, true)
        wait(60)
        verify(backdrop.art.scale > held)
        backdrop.paused = true
        compare(backdrop.animating, false)
    }

    function test_status_geometry() {
        compare(status.glyphSize, Ui.px(12))
        compare(status.row.y, Ui.px(10))
        compare(status.row.spacing, Ui.px(8))
        compare(status.batteryGlyph.height, Ui.px(12))
        compare(status.batteryGlyph.width, Ui.px(12))
        fuzzyCompare(status.row.x + status.row.width, 640 - Ui.px(16), 1e-6)
    }

    function test_status_battery_accent() {
        status.battery = 50
        compare(status.batteryLow, false)
        compare(status.batteryColor, Ui.textFull)
        status.battery = 10
        compare(status.batteryLow, false)
        status.battery = 9
        compare(status.batteryLow, true)
        compare(status.batteryColor, Ui.accent)
        compare(status.batteryGlyph.color, Ui.accent)
        status.battery = 100
    }

    function test_status_glyphs() {
        status.playing = false
        status.paused = false
        status.bluetooth = false
        compare(status.playGlyph.visible, false)
        compare(status.bluetoothGlyph.visible, false)
        status.playing = true
        compare(status.playGlyph.visible, true)
        compare(status.playGlyph.name, "play")
        status.bluetooth = true
        compare(status.bluetoothGlyph.visible, true)
        compare(status.bluetoothGlyph.name, "bluetooth")
        compare(status.batteryGlyph.name, "battery")
    }

    function status_glyph_rects() {
        const items = [status.clockText, status.playGlyph, status.bluetoothGlyph, status.batteryGlyph]
        const rects = []
        for (let i = 0; i < items.length; i++) {
            const it = items[i]
            if (!it.visible)
                continue
            const p = it.mapToItem(status, 0, 0)
            rects.push({ name: it.name === undefined ? "clock" : it.name, x: p.x, y: p.y, w: it.width, h: it.height })
        }
        return rects
    }

    function check_status_row(w, h) {
        Ui.windowW = w
        Ui.windowH = h
        status.width = w
        status.clock = "7:13"
        status.playing = true
        status.bluetooth = true
        status.batteryPresent = true
        waitForRendering(status)
        const rects = status_glyph_rects()
        compare(rects.length, 4)
        for (let i = 0; i < rects.length; i++) {
            verify(rects[i].x >= 0, rects[i].name + " left of window")
            verify(rects[i].x + rects[i].w <= w - Ui.px(Ui.edge) + 1e-6, rects[i].name + " past width - EDGE")
            for (let j = i + 1; j < rects.length; j++) {
                const apart = rects[i].x + rects[i].w <= rects[j].x + 1e-6 || rects[j].x + rects[j].w <= rects[i].x + 1e-6
                verify(apart, rects[i].name + " intersects " + rects[j].name)
            }
        }
        fuzzyCompare(status.playGlyph.width, Ui.px(Ui.statusHeight), 1e-6)
        fuzzyCompare(status.bluetoothGlyph.width, Ui.px(Ui.statusHeight), 1e-6)
        fuzzyCompare(status.batteryGlyph.width, Ui.px(Ui.statusHeight), 1e-6)
        fuzzyCompare(status.playGlyph.k, Ui.px(Ui.statusHeight) / 32, 1e-9)
        verify(status.clockText.x < status.playGlyph.x)
        verify(status.playGlyph.x < status.bluetoothGlyph.x)
        verify(status.bluetoothGlyph.x < status.batteryGlyph.x)
        fuzzyCompare(status.row.x + status.row.width, w - Ui.px(Ui.edge), 1e-6)
    }

    function test_status_row_portrait() {
        check_status_row(640, 960)
    }

    function test_status_row_landscape() {
        check_status_row(1920, 1080)
    }

    function test_sheet_tokens() {
        compare(sheet.duration, 380)
        fuzzyCompare(sheet.scrimAlpha, 0.55, 1e-9)
        fuzzyCompare(sheet.scrim.color.a, 0.55, 0.005)
        fuzzyCompare(sheet.scrim.color.r, 17 / 255, 0.005)
        compare(sheet.pitch, Ui.px(43))
    }

    function test_sheet_travel() {
        compare(sheet.visible, false)
        sheet.open()
        compare(sheet.animating, true)
        compare(sheet.visible, true)
        fuzzyCompare(sheet.panel.y, 960, 1e-6)
        fuzzyCompare(sheet.scrim.opacity, 0, 1e-6)
        verify(sheet.travel > 0)
        tryCompare(sheet, "animating", false, 2000)
        fuzzyCompare(sheet.progress, 1, 1e-9)
        fuzzyCompare(sheet.panel.y, 960 - sheet.travel, 1e-6)
        fuzzyCompare(sheet.panel.opacity, 1, 1e-9)
        fuzzyCompare(sheet.scrim.opacity, 1, 1e-9)
        sheet.close()
        compare(sheet.animating, true)
        tryCompare(sheet, "animating", false, 2000)
        fuzzyCompare(sheet.panel.y, 960, 1e-6)
        compare(sheet.visible, false)
    }

    function test_sheet_focus() {
        sheet.open()
        sheet.complete()
        compare(sheet.focusIndex, 0)
        verify(sheet.handleAction("down", false))
        compare(sheet.focusIndex, 1)
        verify(sheet.handleAction("up", false))
        compare(sheet.focusIndex, 0)
        verify(sheet.handleAction("up", false))
        compare(sheet.focusIndex, 0)
        compare(sheet.bar.rowHeight, sheet.pitch)
        compare(sheet.bar.x, Ui.px(16 - 8))
        verify(sheet.handleAction("b", false))
        compare(sheet.shown, false)
    }

    function test_sheet_screenoff() {
        sheet.open()
        compare(sheet.animating, true)
        sheet.screenOff = true
        compare(sheet.animating, false)
        fuzzyCompare(sheet.progress, 1, 1e-9)
        compare(sheet.visible, false)
        sheet.screenOff = false
        compare(sheet.visible, true)
    }

    function test_dialog_tokens() {
        compare(dialog.duration, 300)
        compare(dialog.hideDuration, 150)
        fuzzyCompare(dialog.fromScale, 0.9, 1e-9)
        fuzzyCompare(dialog.scrim.color.a, 0.55, 0.005)
        compare(dialog.pitch, Ui.px(43))
        compare(dialog.openAnimation.animations[1].duration, 300)
        compare(dialog.openAnimation.animations[1].easing.type, Motion.pop)
        compare(dialog.openAnimation.animations[2].duration, 300)
        compare(dialog.openAnimation.animations[2].easing.type, Motion.bezierPop)
        fuzzyCompare(dialog.openAnimation.animations[2].from, 0.9, 1e-9)
        compare(dialog.closeAnimation.animations[1].duration, 150)
    }

    function test_dialog_pop() {
        compare(dialog.visible, false)
        dialog.open()
        compare(dialog.animating, true)
        compare(dialog.visible, true)
        verify(dialog.body.scale < 1)
        verify(dialog.body.opacity < 1)
        tryCompare(dialog, "animating", false, 2000)
        fuzzyCompare(dialog.body.scale, 1, 1e-9)
        fuzzyCompare(dialog.body.opacity, 1, 1e-9)
        fuzzyCompare(dialog.scrim.opacity, 1, 1e-9)
        dialog.close()
        compare(dialog.animating, true)
        tryCompare(dialog, "animating", false, 2000)
        fuzzyCompare(dialog.body.opacity, 0, 1e-9)
        compare(dialog.visible, false)
    }

    function test_dialog_layout() {
        dialog.open()
        dialog.complete()
        compare(dialog.titleText.x, Ui.px(16))
        fuzzyCompare(dialog.titleText.y + dialog.titleText.baselineOffset, Ui.px(Ui.pivotBaseline), 1e-6)
        compare(dialog.titleText.font.pixelSize, Ui.fontPx(22))
        compare(dialog.bar.rowY, Ui.px(Ui.contentTop))
        compare(dialog.bar.rowHeight, Ui.px(43))
        compare(dialog.bar.x, Ui.px(16 - 8))
        compare(dialog.bar.width, Ui.px(2))
    }

    function test_dialog_focus() {
        dialog.open()
        dialog.complete()
        compare(dialog.focusIndex, 0)
        verify(dialog.handleAction("down", false))
        verify(dialog.handleAction("down", false))
        compare(dialog.focusIndex, 2)
        compare(dialog.bar.rowY, Ui.px(Ui.contentTop) + 2 * dialog.pitch)
        verify(dialog.handleAction("a", false))
        compare(acceptedSpy.count, 1)
        compare(acceptedSpy.signalArguments[0][0], 2)
        verify(dialog.handleAction("b", false))
        compare(rejectedSpy.count, 1)
        compare(dialog.shown, false)
    }

    function test_dialog_screenoff() {
        dialog.open()
        compare(dialog.animating, true)
        dialog.screenOff = true
        compare(dialog.animating, false)
        fuzzyCompare(dialog.body.scale, 1, 1e-9)
        fuzzyCompare(dialog.body.opacity, 1, 1e-9)
        compare(dialog.visible, false)
        dialog.screenOff = false
        compare(dialog.visible, true)
    }

    function test_toast_strip() {
        compare(toast.height, Ui.px(40))
        compare(toast.strip.height, Ui.px(40))
        compare(toast.strip.width, 640)
        fuzzyCompare(toast.strip.color.a, 0.8, 0.005)
        fuzzyCompare(toast.strip.color.r, 0, 1e-9)
        compare(toast.inDuration, 350)
        compare(toast.holdDuration, 3500)
        compare(toast.outDuration, 250)
        compare(toast.inAnimation.duration, 350)
        compare(toast.hold.interval, 3500)
        compare(toast.outAnimation.duration, 250)
        compare(toast.artistText.x, 0)
        fuzzyCompare(toast.artistText.y + toast.artistText.baselineOffset, Ui.px(16), 1e-6)
        fuzzyCompare(toast.titleText.y + toast.titleText.baselineOffset, Ui.px(34), 1e-6)
        compare(toast.artistText.parent.x, Ui.px(16))
        compare(toast.artistText.parent.width, 640 - 2 * Ui.px(16))
        compare(toast.artistText.parent.clip, true)
    }

    function test_toast_show() {
        compare(toast.visible, false)
        toast.show("artist", "Song title")
        compare(toast.phase, "in")
        compare(toast.artist, "artist")
        compare(toast.title, "Song title")
        fuzzyCompare(toast.strip.y, -Ui.px(40), 1e-6)
        fuzzyCompare(toast.strip.opacity, 0, 1e-6)
        tryCompare(toast, "phase", "hold", 2000)
        fuzzyCompare(toast.strip.y, 0, 1e-6)
        fuzzyCompare(toast.strip.opacity, 1, 1e-9)
        compare(toast.hold.running, true)
        toast.show("other", "Next")
        compare(toast.phase, "hold")
        compare(toast.animating, true)
        tryCompare(toast, "title", "Next", 2000)
        tryCompare(toast, "animating", false, 2000)
        compare(toast.hold.running, true)
    }

    function test_toast_out() {
        toast.show("artist", "Song title")
        toast.complete()
        compare(toast.phase, "hold")
        toast.hold.stop()
        toast.hold.triggered()
        compare(toast.phase, "out")
        compare(toast.leaving, true)
        tryCompare(toast, "phase", "hidden", 2000)
        fuzzyCompare(toast.strip.y, -Ui.px(40), 1e-6)
        compare(toast.visible, false)
    }

    function test_toast_show_while_leaving() {
        toast.show("artist", "Song title")
        toast.complete()
        toast.hold.stop()
        toast.hold.triggered()
        compare(toast.phase, "out")
        tryVerify(function() { return toast.progress < 0.9 && toast.progress > 0.3 }, 2000)
        const y = toast.strip.y
        const eased = toast.eased
        verify(y > -toast.travel)
        verify(y < 0)
        toast.show("other", "Next")
        compare(toast.phase, "in")
        compare(toast.leaving, false)
        fuzzyCompare(toast.inAnimation.from, Motion.expOut6Inverse(eased), 1e-9)
        fuzzyCompare(toast.strip.y, y, 1.5)
        tryCompare(toast, "phase", "hold", 2000)
        fuzzyCompare(toast.strip.y, 0, 1e-6)
    }

    function test_curve_inverse() {
        for (let i = 0; i <= 10; i++) {
            const t = i / 10
            fuzzyCompare(Motion.expOut6Inverse(Motion.expOut6(t)), t, 1e-9)
        }
    }

    function test_toast_screenoff() {
        toast.show("artist", "Song title")
        compare(toast.animating, true)
        toast.screenOff = true
        compare(toast.animating, false)
        compare(toast.hold.running, false)
        compare(toast.visible, false)
        compare(toast.phase, "hidden")
        toast.show("later", "Track")
        compare(toast.phase, "hidden")
        compare(toast.title, "Track")
    }

    function test_overlay_anchors() {
        const w = 640
        const h = 960
        fuzzyCompare(overlay.background.color.a, 0.8, 0.005)
        fuzzyCompare(overlay.plus.x + overlay.plus.width / 2, w / 2, 1e-6)
        fuzzyCompare(overlay.plus.y + overlay.plus.height / 2, Ui.px(72), 1e-6)
        compare(overlay.plus.width, Ui.px(20))
        compare(overlay.number.x, Ui.px(16))
        fuzzyCompare(overlay.number.y + overlay.number.baselineOffset, Ui.px(160), 1e-6)
        compare(overlay.number.font.pixelSize, Ui.fontPx(48))
        fuzzyCompare(overlay.ring.x + overlay.ring.width / 2, w / 2, 1e-6)
        fuzzyCompare(overlay.ring.y + overlay.ring.height / 2, h / 2, 1e-6)
        compare(overlay.ring.width, Ui.px(64))
        fuzzyCompare(overlay.prev.x + overlay.prev.width / 2, w / 2 - Ui.px(80), 1e-6)
        fuzzyCompare(overlay.next.x + overlay.next.width / 2, w / 2 + Ui.px(80), 1e-6)
        fuzzyCompare(overlay.prev.y + overlay.prev.height / 2, h / 2, 1e-6)
        fuzzyCompare(overlay.minus.x + overlay.minus.width / 2, w / 2, 1e-6)
        fuzzyCompare(overlay.minus.y + overlay.minus.height / 2, h / 2 + Ui.px(72), 1e-6)
        fuzzyCompare(overlay.volumeLabel.y, overlay.minus.y + overlay.minus.height, 1e-6)
        compare(overlay.volumeLabel.font.pixelSize, Ui.fontPx(7))
        compare(overlay.exitLabel.x, Ui.px(16))
        compare(overlay.titleText.x, Ui.px(16))
        fuzzyCompare(overlay.titleText.y + overlay.titleText.height, h - Ui.px(24), 1e-6)
        compare(overlay.titleText.font.pixelSize, Ui.fontPx(14))
        compare(overlay.artistText.font.pixelSize, Ui.fontPx(10))
        fuzzyCompare(overlay.prev.parent.opacity, 0.4, 1e-9)
        compare(overlay.ring.focused, true)
        fuzzyCompare(overlay.ring.ringAlpha, 1.0, 1e-9)
        fuzzyCompare(overlay.ring.parent.opacity, Ui.overlayDim, 1e-9)
        compare(overlay.number.color, Ui.white(1.0))
        compare(overlay.plus.color, Ui.white(1.0))
        compare(overlay.prev.name, "previous")
        compare(overlay.next.name, "next")
        verify(overlay.prev.known)
        verify(overlay.next.known)
    }

    function check_overlay_row(w, h) {
        Ui.windowW = w
        Ui.windowH = h
        overlay.width = w
        overlay.height = h
        overlay.show(13)
        overlay.complete()
        overlay.hide.stop()
        const prev = overlay.prev
        const next = overlay.next
        const ring = overlay.ring
        fuzzyCompare(prev.width, Ui.px(Ui.overlayGlyph), 1e-6)
        fuzzyCompare(next.width, Ui.px(Ui.overlayGlyph), 1e-6)
        fuzzyCompare(ring.width, Ui.px(Ui.overlayRing), 1e-6)
        verify(prev.x >= 0)
        verify(prev.y >= 0)
        verify(next.x + next.width <= w + 1e-6)
        verify(next.y + next.height <= h + 1e-6)
        verify(prev.x + prev.width <= ring.x + 1e-6)
        verify(next.x >= ring.x + ring.width - 1e-6)
        fuzzyCompare(prev.y + prev.height / 2, ring.y + ring.height / 2, 1e-6)
        fuzzyCompare(next.y + next.height / 2, ring.y + ring.height / 2, 1e-6)
        fuzzyCompare(ring.x + ring.width / 2 - (prev.x + prev.width / 2), next.x + next.width / 2 - (ring.x + ring.width / 2), 1e-6)
    }

    function test_overlay_row_portrait() {
        check_overlay_row(640, 960)
    }

    function test_overlay_row_landscape() {
        check_overlay_row(1920, 1080)
    }

    function test_overlay_timings() {
        compare(overlay.showDuration, 200)
        compare(overlay.popDuration, 400)
        compare(overlay.hideDelay, 1500)
        compare(overlay.fadeDuration, 380)
        compare(overlay.showAnimation.duration, 200)
        compare(overlay.popAnimation.duration, 400)
        compare(overlay.popAnimation.easing.type, Motion.bezierPop)
        fuzzyCompare(overlay.popAnimation.from, 0.7, 1e-9)
        compare(overlay.hide.interval, 1500)
        compare(overlay.fadeAnimation.duration, 380)
        compare(overlay.fadeAnimation.easing.type, Easing.BezierSpline)
        fuzzyCompare(overlay.fadeAnimation.easing.bezierCurve[0], 0.1, 1e-9)
        fuzzyCompare(overlay.fadeAnimation.easing.bezierCurve[1], 0.9, 1e-9)
    }

    function test_overlay_show() {
        compare(overlay.visible, false)
        overlay.show(13)
        compare(overlay.level, 13)
        compare(overlay.number.text, "13")
        compare(overlay.showAnimation.running, true)
        compare(overlay.popAnimation.running, true)
        compare(overlay.hide.running, true)
        verify(overlay.number.scale < 1)
        tryCompare(overlay, "opacity", 1, 2000)
        tryCompare(overlay.popAnimation, "running", false, 2000)
        fuzzyCompare(overlay.number.scale, 1, 1e-9)
        overlay.show(18)
        compare(overlay.level, 18)
        compare(overlay.popAnimation.running, true)
        compare(overlay.showAnimation.running, false)
        compare(overlay.hide.running, true)
        overlay.hide.stop()
        overlay.hide.triggered()
        compare(overlay.fadeAnimation.running, true)
        tryCompare(overlay, "visible", false, 2000)
    }

    function test_overlay_screenoff() {
        overlay.show(35)
        compare(overlay.animating, true)
        overlay.screenOff = true
        compare(overlay.animating, false)
        compare(overlay.hide.running, false)
        compare(overlay.visible, false)
        compare(overlay.level, 35)
        overlay.show(40)
        compare(overlay.level, 40)
        compare(overlay.visible, false)
        compare(overlay.animating, false)
        overlay.screenOff = false
        compare(overlay.visible, false)
    }

    function test_jump_letter() {
        compare(jump.text.font.pixelSize, Ui.fontPx(120))
        compare(jump.text.font.capitalization, Font.AllLowercase)
        compare(jump.holdDuration, 350)
        compare(jump.fadeDuration, 380)
        compare(jump.hold.interval, 350)
        compare(jump.fadeAnimation.duration, 380)
        compare(jump.fadeAnimation.easing.type, Easing.BezierSpline)
        fuzzyCompare(jump.fadeAnimation.easing.bezierCurve[0], 0.1, 1e-9)
        fuzzyCompare(jump.fadeAnimation.easing.bezierCurve[1], 0.9, 1e-9)
        compare(jump.visible, false)
        jump.show("h")
        compare(jump.letter, "h")
        fuzzyCompare(jump.opacity, 1, 1e-9)
        compare(jump.visible, true)
        compare(jump.hold.running, true)
        compare(jump.animating, false)
        fuzzyCompare(jump.text.x + jump.text.width / 2, 320, 0.5)
        fuzzyCompare(jump.text.y + jump.text.height / 2, 480, 0.5)
        jump.hold.stop()
        jump.hold.triggered()
        compare(jump.animating, true)
        tryCompare(jump, "animating", false, 2000)
        fuzzyCompare(jump.opacity, 0, 1e-9)
        compare(jump.visible, false)
    }

    function test_jump_screenoff() {
        jump.show("k")
        jump.hold.stop()
        jump.hold.triggered()
        compare(jump.animating, true)
        jump.screenOff = true
        compare(jump.animating, false)
        compare(jump.hold.running, false)
        compare(jump.visible, false)
        fuzzyCompare(jump.opacity, 0, 1e-9)
        jump.show("m")
        compare(jump.visible, false)
        compare(jump.letter, "m")
    }
}
