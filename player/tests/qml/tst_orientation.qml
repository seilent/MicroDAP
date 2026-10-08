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
        fakeOutputs.internal = true
        main.outputs = fakeOutputs
        fakePower.screenOff = false
        fakeSettings.scale = 1.0
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
        compare(Ui.turn, 0)
        compare(Ui.orientationActive, true)
    }

    function cleanup() {
        main.pageStack.animate = true
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

    function test_wake_refreshes_outputs() {
        fakeOutputs.refreshes = 0
        fakePower.screenOff = true
        compare(fakeOutputs.refreshes, 0)
        fakePower.screenOff = false
        compare(fakeOutputs.refreshes, 1)
        tc.settle()
    }
}
