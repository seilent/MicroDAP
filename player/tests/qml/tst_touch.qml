import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "Touch"
    when: windowShown
    visible: true
    width: 640
    height: 960

    property var seq: null
    property var page: null

    Item {
        id: fakeSaver

        property bool shown: false
        property int hides: 0

        function hide() {
            fakeSaver.shown = false
            fakeSaver.hides++
        }
    }

    Item {
        id: fakeDialog

        property bool shown: false
        property bool animating: false
        property var actions: []

        function handleAction(name, repeat) {
            fakeDialog.actions = fakeDialog.actions.concat([name])
            return true
        }
    }

    QtObject {
        id: fakePower

        property bool screenOff: false
        property bool switching: false
        property var verbs: []

        signal showSwitchPrompt()
        signal showPowerMenu()
        signal switchFailed()

        function runVerb(verb) {
            fakePower.verbs = fakePower.verbs.concat([verb])
            return true
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 2
        property var current: ({ id: 1, title: "Song 1", artist: "Addison Road", album: "Stories", artPath: "" })
        property real position: 0
        property real duration: 1000
        property QtObject queue: QtObject {
            property int count: 1
            property int index: 0
            property bool shuffle: false
            property int repeat: 0

            function get(row) {
                return fakePlayer.current
            }
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
        property string orientation: "portrait"
        property bool autoRotate: false
        property bool touch: true
    }

    ListModel {
        id: rowsModel

        Component.onCompleted: {
            for (let i = 0; i < 60; i++)
                rowsModel.append({ title: "row " + i })
        }
    }

    Component {
        id: listPage

        Page {
            id: lp

            property alias focusList: focusList
            property alias band: band
            property var activations: []
            property bool pushOnA: false

            list: focusList
            onActivated: (index) => {
                lp.activations = lp.activations.concat([index])
                if (lp.pushOnA)
                    lp.stack.push(listPage, {})
            }

            Item {
                id: band

                width: lp.width
                height: Ui.px(Ui.contentTop)
            }

            FocusList {
                id: focusList

                y: band.height
                width: lp.width
                height: lp.height - band.height
                model: rowsModel
                delegate: PressScale {
                    id: row

                    required property int index
                    readonly property string touchKind: "row"
                    readonly property int touchIndex: row.index

                    width: focusList.width
                    height: focusList.rowPitch
                    pressed: row.index === focusList.focusIndex && lp.pressed
                }
            }
        }
    }

    Item {
        id: rotor

        width: Ui.windowW
        height: Ui.windowH
        x: Math.round((tc.width - rotor.width) / 2)
        y: Math.round((tc.height - rotor.height) / 2)
        rotation: Ui.turn * 90

        PageStack {
            id: stack

            anchors.fill: parent
        }

        PadRouter {
            id: router

            anchors.fill: parent
            stack: stack
            saver: fakeSaver
            modal: fakeDialog
        }

        TouchLayer {
            id: layer

            anchors.fill: parent
            router: router
            dialogs: [fakeDialog]
        }
    }

    SignalSpy {
        id: routedSpy

        target: router
        signalName: "routed"
    }

    SignalSpy {
        id: touchedSpy

        target: layer
        signalName: "touched"
    }

    Connections {
        id: flingWatch

        property real v0: 0
        property real y0: 0

        target: null
        ignoreUnknownSignals: true

        function onVelocityChanged() {
            if (flingWatch.v0 !== 0 || flingWatch.target.velocity === 0)
                return
            flingWatch.v0 = flingWatch.target.velocity
            flingWatch.y0 = flingWatch.target.flick.contentY
        }
    }

    Main {
        id: main

        power: fakePower
        player: fakePlayer
        settings: fakeSettings
        shotWidth: 640
        shotHeight: 960
        visibility: Window.Windowed
    }

    SignalSpy {
        id: mainRoutedSpy

        target: main.router
        signalName: "routed"
    }

    Component {
        id: overlayMainComponent

        Main {
            overlay: true
            overlayMapped: true
            settings: fakeSettings
            shotWidth: 640
            shotHeight: 960
        }
    }

    Component {
        id: shotMainComponent

        Main {
            screenshot: true
            shotWidth: 640
            shotHeight: 960
        }
    }

    function settle() {
        wait(0)
        wait(0)
    }

    function names(spy) {
        const out = []
        for (let i = 0; i < spy.count; i++)
            out.push(spy.signalArguments[i][0])
        return out
    }

    function count(spy, name) {
        return tc.names(spy).filter((n) => n === name).length
    }

    function list() {
        return tc.page.focusList
    }

    function rowX() {
        return tc.page.width / 2
    }

    function rowY(index) {
        const l = tc.list()
        return l.y + (index + 0.5) * l.rowPitch - l.contentY
    }

    function down(mode, item, x, y) {
        if (mode === "mouse") {
            mousePress(item, x, y, Qt.LeftButton)
            return
        }
        tc.seq = touchEvent(item)
        tc.seq.press(0, item, x, y).commit()
    }

    function moveTo(mode, item, x, y) {
        if (mode === "mouse")
            mouseMove(item, x, y, -1, Qt.LeftButton)
        else
            tc.seq.move(0, item, x, y).commit()
    }

    function up(mode, item, x, y) {
        if (mode === "mouse")
            mouseRelease(item, x, y, Qt.LeftButton)
        else
            tc.seq.release(0, item, x, y).commit()
        tc.settle()
    }

    function tap(mode, item, x, y) {
        tc.down(mode, item, x, y)
        tc.up(mode, item, x, y)
    }

    function slide(mode, item, x, y, dx, dy, steps, pause) {
        for (let i = 1; i <= steps; i++) {
            tc.moveTo(mode, item, x + dx * i / steps, y + dy * i / steps)
            if (pause > 0)
                wait(pause)
        }
        if (mode === "touch")
            waitForRendering(item)
    }

    function bottomY() {
        const l = tc.list()
        return l.y + l.height - Ui.px(20)
    }

    function dragUp(distance, pause) {
        const x = tc.rowX()
        const y = tc.bottomY()
        tc.down("mouse", tc.page, x, y)
        tc.slide("mouse", tc.page, x, y, 0, -distance, Math.round(distance / Ui.px(1)), 0)
        wait(pause)
        tc.up("mouse", tc.page, x, y - distance)
    }

    function fling(distance) {
        const l = tc.list()
        flingWatch.v0 = 0
        flingWatch.target = l.touch
        const x = tc.rowX()
        const fromY = tc.bottomY()
        tc.down("mouse", tc.page, x, fromY)
        tc.slide("mouse", tc.page, x, fromY, 0, -distance, 20, 5)
        tc.up("mouse", tc.page, x, fromY - distance)
    }

    function initTestCase() {
        Ui.setSurface(640, 960)
    }

    function init() {
        Ui.turn = 0
        Ui.setSurface(640, 960)
        main.pageStack.completeAll()
        fakeSaver.shown = false
        fakeSaver.hides = 0
        fakeDialog.shown = false
        fakeDialog.actions = []
        router.screenOff = false
        stack.animate = false
        stack.clear()
        tc.page = stack.push(listPage, {})
        tc.settle()
        routedSpy.clear()
        touchedSpy.clear()
        mainRoutedSpy.clear()
    }

    function cleanup() {
        layer.cancel()
        layer.runReturn()
        tc.seq = null
        stack.clear()
        tc.page = null
        Ui.turn = 0
        fakeSettings.touch = true
        fakeSettings.orientation = "portrait"
        fakeSettings.screenOffSeconds = 0
        fakeSettings.saverSeconds = 30
        fakePlayer.status = 2
        main.powerMenu.close()
        main.powerMenu.complete()
        main.saver.hide()
        main.saver.land()
        main.pageStack.animate = false
        while (main.pageStack.depth > 1)
            main.pageStack.pop()
        main.pageStack.animate = true
        tc.settle()
    }

    function test_tap_turns_data() {
        const out = []
        for (let turn = 0; turn < 4; turn++) {
            out.push({ tag: "mouse turn " + turn, mode: "mouse", turn: turn })
            out.push({ tag: "touch turn " + turn, mode: "touch", turn: turn })
        }
        return out
    }

    function test_tap_turns(data) {
        Ui.turn = data.turn
        tc.settle()
        tc.tap(data.mode, tc.page, tc.rowX(), tc.rowY(4))
        compare(tc.list().focusIndex, 4)
        compare(tc.page.focusIndex, 4)
        compare(tc.count(routedSpy, "a"), 1)
        compare(tc.page.activations, [4])
    }

    function test_drag_turns_data() {
        return [0, 1, 2, 3].map((turn) => ({ tag: "turn " + turn, turn: turn }))
    }

    function test_drag_turns(data) {
        Ui.turn = data.turn
        tc.settle()
        const before = tc.list().contentY
        tc.dragUp(Ui.px(100), 150)
        fuzzyCompare(tc.list().contentY - before, Ui.px(75), Ui.px(2))
        compare(tc.count(routedSpy, "a"), 0)
        compare(tc.page.activations, [])
    }

    function test_drag_x_turns_data() {
        return [0, 1, 2, 3].map((turn) => ({ tag: "turn " + turn, turn: turn }))
    }

    function test_drag_x_turns(data) {
        Ui.turn = data.turn
        tc.settle()
        const x = Ui.px(100)
        const y = tc.rowY(4)
        tc.down("mouse", tc.page, x, y)
        tc.slide("mouse", tc.page, x, y, Ui.px(100), 0, 100, 0)
        tc.up("mouse", tc.page, x + Ui.px(100), y)
        compare(tc.list().contentY, 0)
        compare(routedSpy.count, 0)
        compare(tc.page.activations, [])
    }

    function test_slop_data() {
        const out = []
        for (const mode of ["mouse", "touch"]) {
            out.push({ tag: mode + " 20 taps", mode: mode, dx: 0, dy: 20, taps: true })
            out.push({ tag: mode + " 30 cancels", mode: mode, dx: 0, dy: 30, taps: false })
            out.push({ tag: mode + " 15 15 taps", mode: mode, dx: 15, dy: 15, taps: true })
            out.push({ tag: mode + " 20 20 cancels", mode: mode, dx: 20, dy: 20, taps: false })
        }
        return out
    }

    function test_slop(data) {
        const x = tc.rowX()
        const y = tc.rowY(4)
        const dx = Ui.px(data.dx)
        const dy = Ui.px(data.dy)
        tc.down(data.mode, tc.page, x, y)
        tc.slide(data.mode, tc.page, x, y, dx, dy, 10, 0)
        compare(tc.page.pressed, data.taps)
        tc.up(data.mode, tc.page, x + dx, y + dy)
        compare(tc.count(routedSpy, "a"), data.taps ? 1 : 0)
        compare(tc.page.activations, data.taps ? [4] : [])
    }

    function test_hold_taps() {
        tc.down("mouse", tc.page, tc.rowX(), tc.rowY(3))
        wait(1500)
        tc.up("mouse", tc.page, tc.rowX(), tc.rowY(3))
        compare(tc.page.activations, [3])
    }

    function test_out_and_back_does_nothing() {
        const x = tc.rowX()
        const y = tc.rowY(3)
        tc.down("mouse", tc.page, x, y)
        tc.slide("mouse", tc.page, x, y, Ui.px(30), 0, 10, 0)
        tc.slide("mouse", tc.page, x + Ui.px(30), y, -Ui.px(30), 0, 10, 0)
        tc.up("mouse", tc.page, x, y)
        compare(routedSpy.count, 0)
        compare(tc.page.activations, [])
    }

    function test_press_and_return() {
        const l = tc.list()
        l.contentY = l.rowPitch / 2
        const y = tc.rowY(0) + l.rowPitch / 4
        tc.down("mouse", tc.page, tc.rowX(), y)
        compare(l.focusIndex, 0)
        compare(l.contentY, l.rowPitch / 2)
        compare(tc.page.pressed, true)
        tc.up("mouse", tc.page, tc.rowX(), y)
        compare(tc.count(routedSpy, "a"), 1)
        compare(tc.page.activations, [0])
        compare(tc.page.pressed, true)
        tryCompare(l, "contentY", 0, 1000)
        tryCompare(tc.page, "pressed", false, 1000)
    }

    function test_push_clears_pressed() {
        const first = tc.page
        first.pushOnA = true
        tc.tap("mouse", first, tc.rowX(), tc.rowY(2))
        compare(stack.depth, 2)
        compare(first.activations, [2])
        tryCompare(first, "pressed", false, 1000)
    }

    function test_page_destroyed_before_return() {
        stack.push(listPage, {})
        tc.page = stack.currentItem
        tc.settle()
        tc.tap("mouse", tc.page, tc.rowX(), tc.rowY(2))
        compare(tc.page.activations, [2])
        stack.pop()
        tc.page = stack.currentItem
        wait(Motion.touchReturnDelay + 100)
        compare(layer.returnPage, null)
        compare(tc.page.pressed, false)
    }

    function test_pooled_rows() {
        for (let i = 0; i < 40; i++)
            router.dispatch("down", false)
        tc.list().completeScroll()
        tc.settle()
        const l = tc.list()
        const first = Math.ceil(l.contentY / l.rowPitch - 0.001)
        routedSpy.clear()
        tc.tap("mouse", tc.page, tc.rowX(), tc.rowY(first + 2))
        compare(tc.page.activations, [first + 2])
        compare(tc.count(routedSpy, "a"), 1)
    }

    function test_clip_band_misses_row() {
        const l = tc.list()
        l.snapTo(30)
        tc.settle()
        verify(l.contentY > 0)
        const focus = l.focusIndex
        tc.tap("mouse", tc.page, tc.rowX(), tc.page.band.height / 2)
        compare(routedSpy.count, 0)
        compare(tc.page.activations, [])
        compare(l.focusIndex, focus)
    }

    function test_fling_travel() {
        const l = tc.list()
        tc.fling(Ui.px(200))
        verify(flingWatch.v0 > 0)
        verify(l.touch.flinging)
        const atRelease = l.contentY
        wait(50)
        verify(l.contentY > atRelease)
        tryVerify(() => !l.touch.flinging, 5000)
        const travel = l.contentY - flingWatch.y0
        fuzzyCompare(travel, flingWatch.v0 / l.touch.decay, Ui.px(1))
        verify(l.contentY <= Ui.px(200) + Ui.px(Ui.flingCap) / l.touch.decay + Ui.px(1))
        verify(flingWatch.v0 <= Ui.px(Ui.flingCap))
    }

    function test_pause_then_lift_does_not_fling() {
        tc.dragUp(Ui.px(100), 150)
        compare(tc.list().touch.flinging, false)
    }

    function test_slow_release_does_not_fling() {
        const x = tc.rowX()
        const y = tc.bottomY()
        tc.down("mouse", tc.page, x, y)
        tc.slide("mouse", tc.page, x, y, 0, -Ui.px(30), 30, 0)
        tc.slide("mouse", tc.page, x, y - Ui.px(30), 0, -Ui.px(8), 8, 25)
        tc.up("mouse", tc.page, x, y - Ui.px(38))
        compare(tc.list().touch.flinging, false)
    }

    function test_velocity_anchor() {
        const samples = [{ t: 0, y: 0 }, { t: 50, y: 50 }, { t: 100, y: 100 }, { t: 150, y: 300 }]
        fuzzyCompare(layer.velocity(samples), 2500, 0.001)
        fuzzyCompare(layer.velocity([{ t: 0, y: 0 }, { t: 60, y: 120 }]), 2000, 0.001)
        compare(layer.velocity([{ t: 10, y: 0 }, { t: 10, y: 40 }]), 0)
        compare(layer.velocity([{ t: 10, y: 0 }]), 0)
    }

    function test_fling_stops_at_end() {
        const l = tc.list()
        l.contentY = l.maxContentY - Ui.px(20)
        tc.fling(Ui.px(200))
        tryVerify(() => !l.touch.flinging, 5000)
        compare(l.contentY, l.maxContentY)
    }

    function test_fling_stopped_by_press() {
        const l = tc.list()
        tc.fling(Ui.px(200))
        verify(l.touch.flinging)
        tc.down("mouse", tc.page, tc.rowX(), tc.rowY(3))
        compare(l.touch.flinging, false)
        layer.cancel()
        tc.up("mouse", tc.page, tc.rowX(), tc.rowY(3))
    }

    function test_fling_stopped_by_dispatch_data() {
        return [{ tag: "down", name: "down" }, { tag: "b", name: "b" }, { tag: "lb", name: "lb" }]
    }

    function test_fling_stopped_by_dispatch(data) {
        const l = tc.list()
        tc.fling(Ui.px(200))
        verify(l.touch.flinging)
        router.dispatch(data.name, false)
        compare(l.touch.flinging, false)
        if (data.name !== "down")
            return
        const focus = l.focusIndex
        router.dispatch("down", false)
        compare(l.focusIndex, focus + 1)
    }

    function test_drag_clamps_focus() {
        const l = tc.list()
        compare(l.focusIndex, 0)
        tc.dragUp(l.height + Ui.px(40), 150)
        verify(l.contentY > l.rowPitch)
        const first = Math.ceil(l.contentY / l.rowPitch - 0.001)
        compare(l.focusIndex, first)
        compare(tc.page.focusIndex, first)
        router.dispatch("a", false)
        compare(tc.page.activations, [first])
    }

    function test_cancel_with_release() {
        tc.down("mouse", tc.page, tc.rowX(), tc.rowY(3))
        mouseRelease(tc.page, tc.rowX(), tc.rowY(3), Qt.LeftButton)
        layer.cancel()
        tc.settle()
        compare(routedSpy.count, 0)
        compare(tc.page.activations, [])
        compare(tc.page.pressed, false)
    }

    function test_release_falls_back_to_last_sample() {
        const l = tc.list()
        const x = tc.rowX()
        const y = tc.bottomY()
        tc.down("mouse", tc.page, x, y)
        tc.slide("mouse", tc.page, x, y, 0, -Ui.px(60), 60, 0)
        wait(150)
        const dragged = l.contentY
        const fallback = Qt.point(layer.last.x, layer.last.y + Ui.px(5))
        layer.last = fallback
        layer.lift(false)
        compare(layer.pending.local, fallback)
        tc.settle()
        compare(layer.down, false)
        compare(l.contentY, dragged)
        compare(l.touch.flinging, false)
        tc.up("mouse", tc.page, x, y - Ui.px(60))
        compare(routedSpy.count, 0)
    }

    function test_saver_press_hides_only() {
        fakeSaver.shown = true
        tc.down("mouse", tc.page, tc.rowX(), tc.rowY(5))
        compare(fakeSaver.hides, 1)
        compare(layer.down, true)
        compare(layer.dispatching, false)
        compare(tc.page.pressed, false)
        tc.up("mouse", tc.page, tc.rowX(), tc.rowY(5))
        compare(tc.list().focusIndex, 0)
        compare(tc.page.activations, [])
        compare(tc.count(routedSpy, "a"), 1)
    }

    function test_own_routed_does_not_cancel() {
        tc.tap("mouse", tc.page, tc.rowX(), tc.rowY(2))
        compare(tc.page.activations, [2])
        compare(tc.page.pressed, true)
        compare(layer.returnTimer.running, true)
    }

    function test_screen_off_does_nothing() {
        router.screenOff = true
        tc.tap("mouse", tc.page, tc.rowX(), tc.rowY(4))
        compare(tc.list().focusIndex, 0)
        compare(routedSpy.count, 0)
        compare(touchedSpy.count, 0)
    }

    function test_locked_does_nothing() {
        stack.animate = true
        stack.push(listPage, {})
        compare(router.locked, true)
        const top = stack.currentItem
        tc.tap("mouse", top, top.width / 2, Ui.px(Ui.contentTop) + top.focusList.rowPitch * 4.5)
        compare(top.focusList.focusIndex, 0)
        compare(top.activations, [])
        compare(routedSpy.count, 0)
        stack.completeAll()
    }

    function test_dialog_mid_press_runs_nothing() {
        tc.down("mouse", tc.page, tc.rowX(), tc.rowY(4))
        fakeDialog.shown = true
        compare(tc.page.pressed, false)
        tc.up("mouse", tc.page, tc.rowX(), tc.rowY(4))
        compare(fakeDialog.actions, [])
        compare(tc.page.activations, [])
        fakeDialog.shown = false
    }

    function test_pad_down_mid_press_runs_nothing() {
        tc.down("mouse", tc.page, tc.rowX(), tc.rowY(4))
        router.dispatch("down", false)
        tc.up("mouse", tc.page, tc.rowX(), tc.rowY(4))
        compare(tc.names(routedSpy), ["down"])
        compare(tc.page.activations, [])
        compare(tc.page.pressed, false)
    }

    function test_second_finger_ignored() {
        const s = touchEvent(tc.page)
        s.press(0, tc.page, tc.rowX(), tc.rowY(4)).commit()
        s.press(1, tc.page, tc.rowX(), tc.rowY(9)).commit()
        s.release(1, tc.page, tc.rowX(), tc.rowY(9)).commit()
        s.release(0, tc.page, tc.rowX(), tc.rowY(4)).commit()
        tc.settle()
        compare(tc.page.activations, [4])
        compare(tc.count(routedSpy, "a"), 1)
    }

    function test_count_change_cancels() {
        tc.down("mouse", tc.page, tc.rowX(), tc.rowY(4))
        rowsModel.append({ title: "extra" })
        waitForRendering(tc.page)
        compare(tc.page.pressed, false)
        tc.up("mouse", tc.page, tc.rowX(), tc.rowY(4))
        compare(tc.page.activations, [])
        rowsModel.remove(rowsModel.count - 1)
    }

    function test_outside_snap_stops_drag() {
        const l = tc.list()
        const x = tc.rowX()
        const y = tc.bottomY()
        tc.down("mouse", tc.page, x, y)
        tc.slide("mouse", tc.page, x, y, 0, -Ui.px(60), 60, 0)
        verify(l.contentY > 0)
        l.snapTo(0)
        compare(l.contentY, 0)
        tc.slide("mouse", tc.page, x, y - Ui.px(60), 0, -Ui.px(60), 30, 0)
        compare(l.contentY, 0)
        tc.up("mouse", tc.page, x, y - Ui.px(120))
        compare(l.touch.flinging, false)
    }

    function mainPage() {
        main.pageStack.animate = false
        const p = main.pageStack.push(listPage, {})
        tc.settle()
        mainRoutedSpy.clear()
        return p
    }

    function mainRowY(p, index) {
        return p.focusList.y + (index + 0.5) * p.focusList.rowPitch - p.focusList.contentY
    }

    function test_main_wiring() {
        compare(main.touchOn, true)
        compare(main.touchLayer.enabled, true)
        compare(main.touchLayer.handler.enabled, true)
        compare(main.cursorBlank.enabled, true)
        compare(main.cursorBlank.cursorShape, Qt.BlankCursor)
        const p = tc.mainPage()
        tc.tap("mouse", p, p.width / 2, tc.mainRowY(p, 3))
        compare(p.activations, [3])
        compare(tc.count(mainRoutedSpy, "a"), 1)
    }

    function test_main_touch_off() {
        const p = tc.mainPage()
        fakeSettings.touch = false
        compare(main.touchOn, false)
        compare(main.touchLayer.enabled, false)
        compare(main.touchLayer.handler.enabled, false)
        tc.tap("mouse", p, p.width / 2, tc.mainRowY(p, 3))
        tc.tap("touch", p, p.width / 2, tc.mainRowY(p, 3))
        compare(p.focusList.focusIndex, 0)
        compare(p.activations, [])
        compare(mainRoutedSpy.count, 0)
    }

    function test_main_screen_off_disables() {
        const p = tc.mainPage()
        fakePower.screenOff = true
        compare(main.touchLayer.enabled, false)
        tc.tap("mouse", p, p.width / 2, tc.mainRowY(p, 3))
        compare(p.activations, [])
        fakePower.screenOff = false
        tryCompare(main, "rootHidden", false, 2000)
    }

    function test_main_power_menu_mid_press() {
        const p = tc.mainPage()
        tc.down("mouse", p, p.width / 2, tc.mainRowY(p, 3))
        main.powerMenu.open()
        tc.up("mouse", p, p.width / 2, tc.mainRowY(p, 3))
        compare(p.activations, [])
        compare(fakePower.verbs, [])
        compare(main.powerMenu.shown, true)
    }

    function test_main_volume_overlay_passes() {
        const p = tc.mainPage()
        main.overlayLayer.showVolume(50)
        tc.tap("mouse", p, p.width / 2, tc.mainRowY(p, 3))
        compare(p.activations, [3])
    }

    function test_main_rotation_cancels() {
        const p = tc.mainPage()
        tc.down("mouse", p, p.width / 2, tc.mainRowY(p, 3))
        compare(p.pressed, true)
        fakeSettings.orientation = "landscape"
        tc.settle()
        compare(Ui.turn, Ui.landscapeTurn)
        compare(p.pressed, false)
        mouseRelease(p, p.width / 2, tc.mainRowY(p, 3), Qt.LeftButton)
        tc.settle()
        compare(p.activations, [])
        fakeSettings.orientation = "portrait"
        tc.settle()
        compare(Ui.turn, 0)
    }

    function test_main_tap_restarts_timers() {
        fakeSettings.screenOffSeconds = 2
        fakeSettings.saverSeconds = 2
        fakePlayer.status = 1
        verify(main.screenOffTimer.running)
        verify(main.saverTimer.running)
        wait(1000)
        tc.tap("mouse", main.rotor, main.rotor.width / 2, main.rotor.height / 2)
        wait(1000)
        compare(main.saver.shown, false)
        compare(fakePower.verbs, [])
        tryCompare(main.saver, "shown", true, 4000)
        fakePlayer.status = 2
        fakeSettings.screenOffSeconds = 0
        fakePower.verbs = []
    }

    function test_overlay_main_disabled() {
        const overlay = createTemporaryObject(overlayMainComponent, tc)
        verify(overlay !== null)
        compare(overlay.touchOn, false)
        compare(overlay.touchLayer.enabled, false)
        compare(overlay.cursorBlank.enabled, false)
    }

    function test_screenshot_main_disabled() {
        const shot = createTemporaryObject(shotMainComponent, tc)
        verify(shot !== null)
        compare(shot.touchOn, false)
        compare(shot.touchLayer.enabled, false)
        compare(shot.cursorBlank.enabled, false)
    }
}
