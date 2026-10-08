import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "Orientation"
    when: windowShown
    visible: true
    width: 320
    height: 240

    QtObject {
        id: fakePad

        readonly property var names: ["up", "down", "left", "right", "a", "b"]

        signal action(int action, bool repeat)
        signal pressed(int action)
        signal released(int action)

        function actionName(action) {
            return action >= 0 && action < fakePad.names.length ? fakePad.names[action] : ""
        }

        function code(name) {
            return fakePad.names.indexOf(name)
        }
    }

    QtObject {
        id: fakeGuide

        property bool give: true
        property int takes: 0

        function take() {
            fakeGuide.takes++
            return fakeGuide.give
        }
    }

    QtObject {
        id: fakeOutputs

        property bool internal: true
        property int refreshes: 0

        function refresh() {
            fakeOutputs.refreshes++
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
    }

    QtObject {
        id: fakeSensor

        property bool supported: true
        property int turn: -1
        property string raw: ""
        property string settledRaw: ""
        property var wanted: []
        property var kept: []

        function setWanted(on, keepTurn) {
            fakeSensor.wanted = fakeSensor.wanted.concat([on])
            fakeSensor.kept = fakeSensor.kept.concat([keepTurn === true])
            if (!on)
                fakeSensor.turn = -1
        }

        function arrive(raw) {
            fakeSensor.raw = raw
        }

        function report(raw, turn) {
            fakeSensor.raw = raw
            fakeSensor.settledRaw = raw
            fakeSensor.turn = turn
        }
    }

    QtObject {
        id: fakePower

        property bool screenOff: false
        property bool switching: false

        signal showSwitchPrompt()
        signal showPowerMenu()
        signal switchFailed()
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
            property int keeps: 0
            property var pressedNames: []
            property var releasedNames: []

            list: focusList

            function press(name) {
                lp.pressedNames = lp.pressedNames.concat([name])
            }

            function release(name) {
                lp.releasedNames = lp.releasedNames.concat([name])
            }

            function keepFocusVisible() {
                lp.keeps++
                focusList.keepFocusVisible()
            }

            FocusList {
                id: focusList

                anchors.fill: parent
                model: rowsModel
                delegate: Item {
                    width: focusList.width
                    height: focusList.rowPitch
                }
            }
        }
    }

    Main {
        id: main

        power: fakePower
        settings: fakeSettings
        outputs: fakeOutputs
        shotWidth: 640
        shotHeight: 960
        visibility: Window.Windowed
    }

    SignalSpy {
        id: appliedSpy

        target: main
        signalName: "orientationApplied"
    }

    SignalSpy {
        id: rotateSpy

        target: main.router
        signalName: "rotate"
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

    SignalSpy {
        id: routedSpy

        target: main.router
        signalName: "routed"
    }

    function settle() {
        wait(0)
        wait(0)
    }

    function setOrientation(name) {
        fakeSettings.orientation = name
        tc.settle()
    }

    function init() {
        main.router.pad = fakePad
        main.router.guide = null
        main.router.chordHeld = ({})
        fakeGuide.give = true
        fakeGuide.takes = 0
        fakeOutputs.internal = true
        main.outputs = fakeOutputs
        fakePower.screenOff = false
        fakeSettings.scale = 1.0
        fakeSettings.autoRotate = false
        main.sensor = null
        fakeSensor.supported = true
        fakeSensor.turn = -1
        fakeSensor.raw = ""
        fakeSensor.settledRaw = ""
        fakeSensor.wanted = []
        fakeSensor.kept = []
        main.shotWidth = 640
        main.shotHeight = 960
        fakeSettings.orientation = "portrait"
        tc.settle()
        tryCompare(main.pageStack, "inputLocked", false, 5000)
        main.pageStack.animate = false
        main.restoreStack([])
        main.pageStack.completeAll()
        main.saver.hide()
        main.saver.land()
        main.router.held = ({})
        main.router.heldCount = ({})
        appliedSpy.clear()
        routedSpy.clear()
        rotateSpy.clear()
        compare(Ui.turn, 0)
        compare(Ui.orientationActive, true)
    }

    function cleanup() {
        main.pageStack.animate = true
        if (main.saver.shown) {
            main.saver.hide()
            main.saver.land()
        }
        if (main.switchPrompt.shown) {
            main.switchPrompt.close()
            main.switchPrompt.complete()
        }
    }

    function push(name, repeat) {
        fakePad.pressed(fakePad.code(name))
        fakePad.action(fakePad.code(name), repeat === true)
    }

    function lastRouted() {
        return routedSpy.signalArguments[routedSpy.count - 1][0]
    }

    function test_window_size() {
        compare(main.width, 640)
        compare(main.height, 960)
        compare(main.rotor.width, 640)
        compare(main.rotor.height, 960)
        compare(main.rotor.rotation, 0)
    }

    function test_turns_per_setting_data() {
        return [
            { tag: "portrait", value: "portrait", turn: 0 },
            { tag: "portrait-flipped", value: "portrait-flipped", turn: 2 },
            { tag: "landscape", value: "landscape", turn: Ui.landscapeTurn },
            { tag: "landscape-flipped", value: "landscape-flipped", turn: (Ui.landscapeTurn + 2) % 4 }
        ]
    }

    function test_turns_per_setting(data) {
        tc.setOrientation(data.value)
        compare(Ui.turn, data.turn)
        compare(main.rotor.rotation, data.turn * 90)
        compare(main.rotor.width, data.turn % 2 === 1 ? 960 : 640)
        compare(main.rotor.height, data.turn % 2 === 1 ? 640 : 960)
        compare(main.rotor.x, data.turn % 2 === 1 ? -160 : 0)
        compare(main.rotor.y, data.turn % 2 === 1 ? 160 : 0)
        compare(Ui.landscape, data.turn % 2 === 1)
        compare(appliedSpy.count, data.turn === 0 ? 0 : 1)
    }

    function test_turnFor() {
        compare(main.turnFor("portrait"), 0)
        compare(main.turnFor("portrait-flipped"), 2)
        compare(main.turnFor("landscape"), Ui.landscapeTurn)
        compare(main.turnFor("landscape-flipped"), (Ui.landscapeTurn + 2) % 4)
        compare(main.turnFor("sideways"), 0)
    }

    function test_remap_data() {
        return [
            { tag: "portrait", value: "portrait", map: ["right", "down", "left", "up"] },
            { tag: "landscape", value: "landscape", map: ["up", "right", "down", "left"] },
            { tag: "portrait-flipped", value: "portrait-flipped", map: ["left", "up", "right", "down"] },
            { tag: "landscape-flipped", value: "landscape-flipped", map: ["down", "left", "up", "right"] }
        ]
    }

    function test_remap(data) {
        if (Ui.landscapeTurn !== 1)
            skip("table for L 1")
        tc.setOrientation(data.value)
        const raws = ["up", "right", "down", "left"]
        for (let i = 0; i < 4; i++) {
            tc.push(raws[i])
            compare(tc.lastRouted(), data.map[i])
            fakePad.released(fakePad.code(raws[i]))
        }
        tc.push("a")
        compare(tc.lastRouted(), "a")
        fakePad.released(fakePad.code("a"))
    }

    function test_remap_off_when_inactive() {
        fakeOutputs.internal = false
        tc.settle()
        compare(Ui.orientationActive, false)
        tc.push("up")
        compare(tc.lastRouted(), "up")
        fakePad.released(fakePad.code("up"))
    }

    function test_repeat_and_release_keep_pressed_name() {
        tc.push("up")
        const first = tc.lastRouted()
        compare(first, Ui.landscapeTurn === 1 ? "right" : "left")
        tc.setOrientation("landscape")
        fakePad.action(fakePad.code("up"), true)
        compare(tc.lastRouted(), first)
        compare(main.router.heldName("up"), first)
        fakePad.released(fakePad.code("up"))
        compare(main.router.held["up"], undefined)
        tc.push("up")
        compare(tc.lastRouted(), "up")
        fakePad.released(fakePad.code("up"))
    }

    function test_two_raws_one_name_release_once() {
        if (Ui.landscapeTurn !== 1)
            skip("table for L 1")
        const page = main.pageStack.push(listPage, {})
        fakePad.pressed(fakePad.code("up"))
        compare(page.pressedNames, ["right"])
        tc.setOrientation("landscape")
        fakePad.pressed(fakePad.code("right"))
        compare(page.pressedNames, ["right"])
        compare(main.router.heldCount["right"], 2)
        fakePad.released(fakePad.code("up"))
        compare(page.releasedNames, [])
        fakePad.released(fakePad.code("right"))
        compare(page.releasedNames, ["right"])
        compare(main.router.heldCount["right"], undefined)
        compare(main.router.held["up"], undefined)
        compare(main.router.held["right"], undefined)
        fakePad.pressed(fakePad.code("right"))
        fakePad.released(fakePad.code("right"))
        compare(page.pressedNames, ["right", "right"])
        compare(page.releasedNames, ["right", "right"])
    }

    function test_chord_rotates_once() {
        main.router.guide = fakeGuide
        const page = main.pageStack.push(listPage, {})
        tc.push("right")
        compare(fakeGuide.takes, 1)
        compare(rotateSpy.count, 1)
        compare(rotateSpy.signalArguments[0][0], "right")
        compare(routedSpy.count, 0)
        compare(page.pressedNames, [])
        fakePad.action(fakePad.code("right"), true)
        fakePad.action(fakePad.code("right"), true)
        compare(routedSpy.count, 0)
        tc.settle()
        compare(main.turnFor(fakeSettings.orientation), 1)
        compare(Ui.turn, 1)
        fakePad.released(fakePad.code("right"))
        compare(page.releasedNames, [])
        compare(main.router.chordHeld["right"], undefined)
        compare(fakeGuide.takes, 1)
    }

    function test_chord_second_direction_dropped() {
        main.router.guide = fakeGuide
        tc.push("right")
        compare(rotateSpy.count, 1)
        tc.push("down")
        compare(rotateSpy.count, 1)
        compare(routedSpy.count, 0)
        compare(fakeGuide.takes, 1)
        fakePad.released(fakePad.code("down"))
        fakePad.released(fakePad.code("right"))
        compare(Object.keys(main.router.chordHeld).length, 0)
        tc.settle()
        compare(Ui.turn, 1)
        fakeGuide.give = false
        tc.push("down")
        compare(rotateSpy.count, 1)
        compare(routedSpy.count, 1)
        fakePad.released(fakePad.code("down"))
    }

    function test_chord_take_false_dispatches() {
        main.router.guide = fakeGuide
        fakeGuide.give = false
        tc.push("right")
        compare(fakeGuide.takes, 1)
        compare(rotateSpy.count, 0)
        compare(routedSpy.count, 1)
        compare(tc.lastRouted(), Ui.padDirection("right"))
        fakePad.released(fakePad.code("right"))
        compare(fakeSettings.orientation, "portrait")
    }

    function test_chord_ignores_non_directions() {
        main.router.guide = fakeGuide
        tc.push("a")
        compare(fakeGuide.takes, 0)
        compare(rotateSpy.count, 0)
        fakePad.released(fakePad.code("a"))
    }

    function test_no_chord_without_guide() {
        tc.push("right")
        compare(rotateSpy.count, 0)
        compare(routedSpy.count, 1)
        fakePad.released(fakePad.code("right"))
    }

    function test_no_chord_screen_off() {
        main.router.guide = fakeGuide
        fakePower.screenOff = true
        compare(main.router.rotating, false)
        tc.push("right")
        compare(fakeGuide.takes, 0)
        compare(rotateSpy.count, 0)
        fakePad.released(fakePad.code("right"))
        fakePower.screenOff = false
    }

    function test_no_chord_external_output() {
        main.router.guide = fakeGuide
        fakeOutputs.internal = false
        tc.settle()
        compare(main.router.rotating, false)
        tc.push("right")
        compare(fakeGuide.takes, 0)
        compare(rotateSpy.count, 0)
        compare(tc.lastRouted(), "right")
        fakePad.released(fakePad.code("right"))
    }

    function test_no_chord_overlay() {
        const overlay = createTemporaryObject(overlayMainComponent, tc)
        verify(overlay !== null)
        compare(Ui.orientationActive, false)
        overlay.router.pad = fakePad
        overlay.router.guide = fakeGuide
        compare(overlay.router.rotating, false)
        tc.push("right")
        compare(fakeGuide.takes, 0)
        fakePad.released(fakePad.code("right"))
        overlay.destroy()
        wait(0)
        main.syncSurface()
        compare(Ui.orientationActive, true)
    }

    function test_rotateBy_data() {
        return [
            { tag: "portrait right", from: "portrait", raw: "right", to: "landscape", turn: 1 },
            { tag: "portrait left", from: "portrait", raw: "left", to: "landscape-flipped", turn: 3 },
            { tag: "portrait down", from: "portrait", raw: "down", to: "portrait-flipped", turn: 2 },
            { tag: "portrait up", from: "portrait", raw: "up", to: "portrait", turn: 0 },
            { tag: "portrait-flipped right", from: "portrait-flipped", raw: "right", to: "landscape-flipped", turn: 3 },
            { tag: "portrait-flipped left", from: "portrait-flipped", raw: "left", to: "landscape", turn: 1 },
            { tag: "portrait-flipped down", from: "portrait-flipped", raw: "down", to: "portrait", turn: 0 },
            { tag: "portrait-flipped up", from: "portrait-flipped", raw: "up", to: "portrait-flipped", turn: 2 },
            { tag: "landscape right", from: "landscape", raw: "right", to: "portrait-flipped", turn: 2 },
            { tag: "landscape left", from: "landscape", raw: "left", to: "portrait", turn: 0 },
            { tag: "landscape down", from: "landscape", raw: "down", to: "landscape-flipped", turn: 3 },
            { tag: "landscape up", from: "landscape", raw: "up", to: "landscape", turn: 1 },
            { tag: "landscape-flipped right", from: "landscape-flipped", raw: "right", to: "portrait", turn: 0 },
            { tag: "landscape-flipped left", from: "landscape-flipped", raw: "left", to: "portrait-flipped", turn: 2 },
            { tag: "landscape-flipped down", from: "landscape-flipped", raw: "down", to: "landscape", turn: 1 },
            { tag: "landscape-flipped up", from: "landscape-flipped", raw: "up", to: "landscape-flipped", turn: 3 }
        ]
    }

    function test_rotateBy(data) {
        if (Ui.landscapeTurn !== 1)
            skip("table for L 1")
        tc.setOrientation(data.from)
        main.rotateBy(data.raw)
        tc.settle()
        compare(fakeSettings.orientation, data.to)
        compare(Ui.turn, data.turn)
    }

    function test_rotateBy_twice_before_sync() {
        tc.setOrientation("portrait")
        main.rotateBy("right")
        main.rotateBy("right")
        tc.settle()
        compare(fakeSettings.orientation, "portrait-flipped")
        compare(Ui.turn, 2)
    }

    function test_chord_keeps_saver_sheet_prompt() {
        main.router.guide = fakeGuide
        main.navigate("settings")
        const page = main.pageStack.currentItem
        page.completeAll()
        page.sheet.title = "close player"
        page.sheet.items = ["close player"]
        page.sheet.open()
        page.sheet.complete()
        main.switchPrompt.show(false)
        main.switchPrompt.complete()
        main.saver.show()
        main.saver.land()
        tc.push("right")
        compare(rotateSpy.count, 1)
        tc.settle()
        compare(Ui.turn, 1)
        compare(main.saver.shown, true)
        compare(main.switchPrompt.shown, true)
        compare(page.sheet.shown, true)
        fakePad.released(fakePad.code("right"))
        main.saver.hide()
        main.saver.land()
        main.switchPrompt.close()
        main.switchPrompt.complete()
        page.sheet.close()
        page.sheet.complete()
    }

    function test_hub_pivot_pan_lands() {
        main.pageStack.animate = false
        main.navigate("music")
        const hub = main.pageStack.currentItem
        compare(hub.objectName, "music")
        hub.completeAll()
        verify(hub.switchPivot(1))
        compare(hub.switching, true)
        tc.setOrientation("landscape")
        compare(appliedSpy.count, 1)
        compare(hub.switching, false)
        compare(main.pageStack.depth, 2)
    }

    function test_page_turn_completes() {
        main.pageStack.animate = true
        main.navigate("settings")
        compare(main.pageStack.inputLocked, true)
        fakeSettings.orientation = "landscape"
        tryCompare(appliedSpy, "count", 1, 1000)
        compare(main.pageStack.inputLocked, false)
        compare(main.pageStack.depth, 2)
        compare(main.topScreen, "settings")
    }

    function test_stack_and_focus_kept() {
        main.navigate("settings")
        const page = main.pageStack.currentItem
        page.completeAll()
        const about = page.indexOfKey("about")
        page.focusIndex = about
        page.keepFocusVisible()
        tc.setOrientation("landscape")
        compare(main.pageStack.depth, 2)
        compare(main.pageStack.currentItem, page)
        compare(page.focusIndex, about)
        verify(page.focusY >= 0)
        verify(page.focusY + page.focusHeight <= page.flick.height + 0.5)
    }

    function test_focus_shown_below_after_pop() {
        main.navigate("settings")
        const page = main.pageStack.currentItem
        page.completeAll()
        page.focusIndex = page.indexOfKey("about")
        page.keepFocusVisible()
        const bottom = page.flick.contentY
        verify(bottom > 0)
        main.navigate("eq")
        compare(main.topScreen, "eq")
        tc.setOrientation("landscape")
        main.pageStack.pop()
        compare(main.pageStack.currentItem, page)
        verify(page.focusY >= 0)
        verify(page.focusY + page.focusHeight <= page.flick.height + 0.5)
        verify(page.flick.contentY > bottom)
    }

    function test_flip_snaps_lists_without_height_change() {
        const page = main.pageStack.push(listPage, {})
        tryCompare(page.focusList, "count", 60, 1000)
        page.focusList.snapTo(40)
        const shown = page.focusList.contentY
        verify(shown > 0)
        page.focusList.contentY = 0
        const height = page.focusList.height
        tc.setOrientation("portrait-flipped")
        compare(Ui.turn, 2)
        compare(page.focusList.height, height)
        compare(page.keeps, 1)
        compare(page.focusList.focusIndex, 40)
        compare(page.focusList.contentY, shown)
    }

    function test_square_resize_runs_once() {
        main.shotWidth = 960
        main.shotHeight = 960
        main.shotWidth = 960
        main.shotHeight = 640
        tc.settle()
        compare(appliedSpy.count, 1)
        compare(Ui.orientationActive, false)
        compare(Ui.turn, 0)
        compare(Ui.windowW, 960)
        main.shotWidth = 640
        main.shotHeight = 960
        tc.settle()
        compare(appliedSpy.count, 2)
        compare(Ui.orientationActive, true)
    }

    function test_resize_and_output_in_one_turn_runs_once() {
        fakeSettings.orientation = "landscape"
        tc.settle()
        appliedSpy.clear()
        main.shotWidth = 960
        main.shotHeight = 640
        fakeOutputs.internal = false
        tc.settle()
        compare(appliedSpy.count, 1)
        compare(Ui.turn, 0)
        compare(Ui.orientationActive, false)
        compare(main.rotor.rotation, 0)
    }

    function test_external_output_keeps_saved_value() {
        fakeSettings.orientation = "landscape"
        tc.settle()
        fakeOutputs.internal = false
        tc.settle()
        compare(Ui.turn, 0)
        compare(Ui.padTurn, 0)
        compare(fakeSettings.orientation, "landscape")
        fakeOutputs.internal = true
        tc.settle()
        compare(Ui.turn, Ui.landscapeTurn)
    }

    function test_null_outputs_internal() {
        main.outputs = null
        tc.settle()
        compare(main.internalOutput, true)
        compare(Ui.orientationActive, true)
        tc.setOrientation("landscape")
        compare(Ui.turn, Ui.landscapeTurn)
    }

    function test_saver_stays_shown() {
        main.saver.show()
        main.saver.land()
        compare(main.saver.shown, true)
        tc.setOrientation("landscape")
        compare(appliedSpy.count, 1)
        compare(main.saver.shown, true)
    }

    function test_scale_clamp() {
        fakeSettings.scale = 1.1
        compare(Ui.userScale, 1.1)
        tc.setOrientation("landscape")
        compare(Ui.userScale, 1.0)
        compare(fakeSettings.scale, 1.1)
        tc.setOrientation("portrait")
        compare(Ui.userScale, 1.1)
        compare(fakeSettings.scale, 1.1)
    }

    function autoOn(raw, turn) {
        main.sensor = fakeSensor
        fakeSettings.autoRotate = true
        tc.settle()
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], true)
        fakeSensor.report(raw, turn)
        tc.settle()
    }

    function test_auto_follows_sensor() {
        tc.autoOn("left-up", Ui.landscapeTurn)
        compare(main.autoFollow, true)
        compare(Ui.turn, Ui.landscapeTurn)
        compare(appliedSpy.count, 1)
        compare(fakeSettings.orientation, "portrait")
        fakeSensor.report("bottom-up", 2)
        tc.settle()
        compare(Ui.turn, 2)
        compare(fakeSettings.orientation, "portrait")
    }

    function test_auto_undefined_at_claim_uses_saved() {
        fakeSettings.orientation = "portrait-flipped"
        tc.settle()
        tc.autoOn("undefined", -1)
        compare(Ui.turn, 2)
        compare(main.sensorTurn, -1)
    }

    function test_auto_chord_pins() {
        main.router.guide = fakeGuide
        tc.autoOn("left-up", Ui.landscapeTurn)
        tc.push("down")
        fakePad.released(fakePad.code("down"))
        tc.settle()
        compare(rotateSpy.count, 1)
        compare(Ui.turn, (Ui.landscapeTurn + 2) % 4)
        compare(main.pinnedTurn, (Ui.landscapeTurn + 2) % 4)
        compare(main.pinnedRaw, "left-up")
        compare(fakeSettings.orientation, "portrait")
        fakeSensor.report("left-up", -1)
        fakeSensor.report("left-up", Ui.landscapeTurn)
        tc.settle()
        compare(Ui.turn, (Ui.landscapeTurn + 2) % 4)
        compare(main.pinnedTurn, (Ui.landscapeTurn + 2) % 4)
        fakeSensor.report("normal", 0)
        tc.settle()
        compare(main.pinnedTurn, -1)
        compare(Ui.turn, 0)
        compare(fakeSettings.orientation, "portrait")
    }

    function test_auto_off_chord_writes_orientation() {
        main.router.guide = fakeGuide
        main.sensor = fakeSensor
        tc.settle()
        compare(main.autoFollow, false)
        tc.push("right")
        fakePad.released(fakePad.code("right"))
        tc.settle()
        compare(main.pinnedTurn, -1)
        compare(fakeSettings.orientation, "landscape")
        compare(Ui.turn, Ui.landscapeTurn)
    }

    function test_auto_pin_kept_on_unsettled_value_and_edit_close() {
        main.router.guide = fakeGuide
        tc.autoOn("left-up", Ui.landscapeTurn)
        tc.push("down")
        fakePad.released(fakePad.code("down"))
        tc.settle()
        const pinned = (Ui.landscapeTurn + 2) % 4
        compare(main.pinnedTurn, pinned)
        fakeSensor.arrive("undefined")
        main.navigate("settings")
        const page = main.pageStack.currentItem
        page.completeAll()
        page.focusIndex = page.indexOfKey("saver")
        page.handleAction("a", false)
        page.handleAction("right", false)
        page.handleAction("a", false)
        compare(page.editing, false)
        tc.settle()
        compare(main.pinnedTurn, pinned)
        compare(Ui.turn, pinned)
        fakeSettings.saverSeconds = 30
    }

    function test_auto_chord_while_pending_pins_settled_raw() {
        main.router.guide = fakeGuide
        tc.autoOn("left-up", Ui.landscapeTurn)
        fakeSensor.arrive("normal")
        tc.push("down")
        fakePad.released(fakePad.code("down"))
        tc.settle()
        compare(main.pinnedRaw, "left-up")
        compare(main.pinnedTurn, (Ui.landscapeTurn + 2) % 4)
        fakeSensor.report("normal", 0)
        tc.settle()
        compare(main.pinnedTurn, -1)
        compare(Ui.turn, 0)
    }

    function test_auto_keeps_turn_only_while_on() {
        tc.autoOn("left-up", Ui.landscapeTurn)
        fakePower.screenOff = true
        tc.settle()
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], false)
        compare(fakeSensor.kept[fakeSensor.kept.length - 1], true)
        fakePower.screenOff = false
        tc.settle()
        fakeSettings.autoRotate = false
        tc.settle()
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], false)
        compare(fakeSensor.kept[fakeSensor.kept.length - 1], false)
    }

    function test_auto_screen_off_keeps_turn() {
        tc.autoOn("left-up", Ui.landscapeTurn)
        compare(Ui.turn, Ui.landscapeTurn)
        appliedSpy.clear()
        fakePower.screenOff = true
        tc.settle()
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], false)
        compare(fakeSensor.turn, -1)
        compare(Ui.turn, Ui.landscapeTurn)
        fakePower.screenOff = false
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], true)
        compare(Ui.turn, Ui.landscapeTurn)
        tc.settle()
        compare(Ui.turn, Ui.landscapeTurn)
        compare(appliedSpy.count, 0)
        fakeSensor.report("left-up", Ui.landscapeTurn)
        tc.settle()
        compare(appliedSpy.count, 0)
        fakeSensor.report("normal", 0)
        tc.settle()
        compare(Ui.turn, 0)
        compare(appliedSpy.count, 1)
    }

    function test_auto_waits_for_settings_edit() {
        tc.autoOn("normal", 0)
        main.navigate("settings")
        const page = main.pageStack.currentItem
        page.completeAll()
        page.focusIndex = page.indexOfKey("saver")
        page.handleAction("a", false)
        compare(page.editing, true)
        fakeSensor.report("left-up", Ui.landscapeTurn)
        tc.settle()
        compare(Ui.turn, 0)
        compare(appliedSpy.count, 0)
        page.handleAction("right", false)
        page.handleAction("a", false)
        compare(page.editing, false)
        compare(fakeSettings.saverSeconds, 60)
        tc.settle()
        compare(Ui.turn, Ui.landscapeTurn)
        compare(appliedSpy.count, 1)
        fakeSettings.saverSeconds = 30
    }

    function test_auto_off_uses_saved() {
        tc.autoOn("left-up", Ui.landscapeTurn)
        compare(Ui.turn, Ui.landscapeTurn)
        fakeSettings.autoRotate = false
        tc.settle()
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], false)
        compare(Ui.turn, 0)
        compare(main.sensorTurn, -1)
    }

    function test_auto_unsupported_uses_saved() {
        fakeSettings.orientation = "portrait-flipped"
        tc.settle()
        tc.autoOn("left-up", Ui.landscapeTurn)
        compare(Ui.turn, Ui.landscapeTurn)
        fakeSensor.supported = false
        tc.settle()
        compare(Ui.turn, 2)
    }

    function test_wake_refreshes_outputs() {
        fakeOutputs.refreshes = 0
        fakePower.screenOff = true
        compare(fakeOutputs.refreshes, 0)
        fakePower.screenOff = false
        compare(fakeOutputs.refreshes, 1)
        tc.settle()
    }
}
