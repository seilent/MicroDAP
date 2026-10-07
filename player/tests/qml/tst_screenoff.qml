import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "ScreenOff"
    when: windowShown
    visible: true
    width: 320
    height: 240

    QtObject {
        id: fakePower

        property bool screenOff: false
        property bool switching: false
        property var verbs: []
        property var calls: []

        signal showSwitchPrompt()
        signal showPowerMenu()
        signal switchFailed()
        signal volumeChanged(int volume)

        function runVerb(verb) {
            fakePower.verbs = fakePower.verbs.concat([verb])
            return true
        }

        function keepInBackground() {
            fakePower.calls = fakePower.calls.concat(["keep"])
        }

        function closeAndSwitch() {
            fakePower.calls = fakePower.calls.concat(["close"])
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 1
        property var current: ({ id: 1, title: "Song 1", artist: "Addison Road", album: "Stories", artPath: "" })
        property real position: 0
        property real duration: 1000
        property var calls: []
        property QtObject queue: QtObject {
            property int count: 1
            property int index: 0
            property bool shuffle: false
            property int repeat: 0

            function get(row) {
                return fakePlayer.current
            }
        }

        function playPause() {
            fakePlayer.calls = fakePlayer.calls.concat(["playPause"])
        }

        function next() {
            fakePlayer.calls = fakePlayer.calls.concat(["next"])
        }

        function previous() {
            fakePlayer.calls = fakePlayer.calls.concat(["previous"])
        }
    }

    QtObject {
        id: fakeSettings

        property string output: "auto"
        property string btCodec: "auto"
        property var roots: []
        property string accent: "#FA2A55"
        property real scale: 1.0
        property int saverSeconds: 30
        property bool overlayToasts: false
        property int screenOffSeconds: 0
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
    }

    Main {
        id: main

        power: fakePower
        player: fakePlayer
        settings: fakeSettings
        shotWidth: 640
        shotHeight: 960
    }

    readonly property var home: main.pageStack.currentItem
    readonly property var menu: main.powerMenu

    function init() {
        tryCompare(main.pageStack, "inputLocked", false, 5000)
        main.pageStack.completeAll()
        fakePower.screenOff = false
        fakePower.switching = false
        fakePower.verbs = []
        fakePower.calls = []
        fakePlayer.calls = []
        fakePlayer.status = 1
        fakeSettings.screenOffSeconds = 0
        menu.close()
        menu.complete()
        main.switchPrompt.close()
        main.switchPrompt.complete()
        main.saver.hide()
        main.saver.land()
        while (main.pageStack.depth > 1)
            main.pageStack.pop()
        main.pageStack.completeAll()
        home.completeAll()
        home.setFocus(0)
        tryCompare(main, "rootHidden", false, 2000)
    }

    function test_off_completes_and_hides_root() {
        main.navigate("music")
        compare(main.pageStack.inputLocked, true)
        compare(main.rootHidden, false)
        compare(main.blackFrame, false)
        fakePower.screenOff = true
        compare(main.off, true)
        compare(main.pageStack.inputLocked, false)
        compare(main.pageStack.phase, "idle")
        compare(main.pageStack.currentItem.objectName, "music")
        compare(main.blackFrame, true)
        verify(Qt.colorEqual(main.background.color, Ui.black))
        verify(Qt.colorEqual(main.color, Ui.black))
        compare(main.pendingHide, true)
        tryCompare(main, "rootHidden", true, 2000)
        compare(main.pageStack.visible, false)
        compare(main.overlayLayer.visible, false)
        compare(main.router.screenOff, true)
        compare(main.pageStack.currentItem.screenOff, true)
        compare(main.pageStack.currentItem.animated, true)
        compare(main.saverTimer.running, false)
        compare(main.screenOffTimer.running, false)
        compare(main.saver.visible, false)
        const page = main.pageStack.currentItem
        fakePower.screenOff = false
        compare(main.rootHidden, false)
        compare(main.blackFrame, false)
        verify(Qt.colorEqual(main.background.color, Ui.bg))
        compare(main.pageStack.visible, true)
        compare(main.overlayLayer.visible, true)
        compare(main.pageStack.currentItem, page)
        compare(main.pageStack.depth, 2)
    }

    function test_off_drops_input() {
        fakePower.screenOff = true
        tryCompare(main, "rootHidden", true, 2000)
        const focus = home.focusIndex
        let dropped = 0
        const onDropped = function() { dropped++ }
        main.router.dropped.connect(onDropped)
        compare(main.router.dispatch("down", false), false)
        compare(main.router.dispatch("a", false), false)
        compare(main.router.dispatch("start", false), false)
        compare(main.router.dispatch("lb", false), false)
        compare(dropped, 4)
        compare(home.focusIndex, focus)
        compare(main.pageStack.depth, 1)
        compare(fakePlayer.calls, [])
        main.router.dropped.disconnect(onDropped)
    }

    function test_off_drops_pad_transport() {
        fakePower.screenOff = true
        tryCompare(main, "rootHidden", true, 2000)
        const focus = home.focusIndex
        let dropped = 0
        const onDropped = function() { dropped++ }
        main.router.dropped.connect(onDropped)
        compare(main.router.dispatch("start", false), false)
        compare(main.router.dispatch("rb", false), false)
        compare(main.router.dispatch("lb", false), false)
        compare(main.router.dispatch("down", false), false)
        compare(main.router.dispatch("a", false), false)
        compare(dropped, 5)
        compare(fakePlayer.calls, [])
        compare(home.focusIndex, focus)
        compare(main.pageStack.depth, 1)
        compare(main.rootHidden, true)
        compare(main.overlayLayer.toast.shown, false)
        main.router.dropped.disconnect(onDropped)
    }

    function test_saver_then_off_returns_to_page() {
        main.saver.show()
        main.saver.land()
        compare(main.saver.shown, true)
        fakePower.screenOff = true
        compare(main.saver.shown, false)
        compare(main.saver.progress, 0)
        tryCompare(main, "rootHidden", true, 2000)
        fakePower.screenOff = false
        compare(main.saver.shown, false)
        compare(main.saver.visible, false)
        compare(main.pageStack.currentItem, home)
        compare(main.pageStack.opacity, 1)
    }

    function test_power_menu_items() {
        fakePower.showPowerMenu()
        compare(menu.shown, true)
        compare(menu.title, "power")
        compare(menu.items, ["screen off", "game mode", "suspend", "power off"])
        compare(menu.words, ["screen-off", "game-mode", "suspend", "poweroff"])
        compare(menu.duration, Motion.dialog)
        compare(menu.hideDuration, Motion.dialogHide)
        fuzzyCompare(menu.scrimAlpha, 0.8, 1e-6)
        compare(menu.pitch, Ui.px(Ui.settingsPitch))
        compare(menu.titleText.font.pixelSize, Ui.fontPx(22))
        compare(menu.titleText.font.styleName, "Light")
        compare(menu.titleText.font.capitalization, Font.AllLowercase)
        compare(main.router.modal, menu)
        compare(main.router.modalUp, true)
        menu.complete()
        compare(main.pageStack.dialogFade, 1)
        compare(home.dialogUp, true)
        fuzzyCompare(home.focusBar.opacity, 0, 1e-6)
        verify(main.router.dispatch("b", false))
        compare(menu.shown, false)
        menu.complete()
        compare(home.dialogUp, false)
        fuzzyCompare(home.focusBar.opacity, 1, 1e-6)
        compare(fakePower.verbs, [])
    }

    function test_power_menu_verbs() {
        fakePower.showPowerMenu()
        compare(menu.focusIndex, 0)
        verify(main.router.dispatch("a", false))
        compare(fakePower.verbs, ["screen-off"])
        compare(menu.shown, false)
        menu.complete()

        fakePower.showPowerMenu()
        verify(main.router.dispatch("down", false))
        verify(main.router.dispatch("down", false))
        verify(main.router.dispatch("a", false))
        compare(fakePower.verbs, ["screen-off", "suspend"])
        menu.complete()

        fakePower.showPowerMenu()
        for (let i = 0; i < 4; i++)
            main.router.dispatch("down", false)
        compare(menu.focusIndex, 3)
        verify(main.router.dispatch("a", false))
        compare(fakePower.verbs, ["screen-off", "suspend", "poweroff"])
        menu.complete()
        compare(main.pageStack.depth, 1)
    }

    function test_power_menu_game_mode_opens_prompt() {
        fakePower.showPowerMenu()
        verify(main.router.dispatch("down", false))
        compare(menu.focusIndex, 1)
        verify(main.router.dispatch("a", false))
        compare(menu.shown, false)
        compare(main.switchPrompt.shown, true)
        compare(main.switchPrompt.focusIndex, main.switchPrompt.keepIndex)
        compare(fakePower.verbs, [])
        compare(fakePower.calls, [])
        compare(main.router.modal, main.switchPrompt)
        main.switchPrompt.close()
        main.switchPrompt.complete()
    }

    function test_power_menu_ignored_while_off() {
        fakePower.screenOff = true
        fakePower.showPowerMenu()
        compare(menu.shown, false)
        fakePower.screenOff = false
        fakePower.showPowerMenu()
        compare(menu.shown, true)
        fakePower.screenOff = true
        compare(menu.visible, false)
        compare(menu.animating, false)
    }

    function test_power_menu_closes_prompt_and_saver() {
        fakePower.showSwitchPrompt()
        compare(main.switchPrompt.shown, true)
        fakePower.showPowerMenu()
        compare(main.switchPrompt.shown, false)
        compare(menu.shown, true)
        fakePower.showSwitchPrompt()
        compare(menu.shown, false)
        compare(main.switchPrompt.shown, true)
        main.switchPrompt.close()
        main.switchPrompt.complete()
    }

    function test_screen_off_timer_fires() {
        compare(main.screenOffArmed, false)
        compare(main.screenOffTimer.running, false)
        fakeSettings.screenOffSeconds = 1
        compare(main.screenOffArmed, true)
        compare(main.screenOffTimer.running, true)
        compare(main.screenOffTimer.interval, 1000)
        fakePlayer.status = 2
        compare(main.screenOffTimer.running, true)
        fakePower.showPowerMenu()
        compare(main.screenOffTimer.running, true)
        tryVerify(() => fakePower.verbs.indexOf("screen-off") >= 0, 3000)
        compare(fakePower.verbs, ["screen-off"])
        compare(main.screenOffTimer.running, false)
        menu.close()
        menu.complete()
    }

    function test_screen_off_timer_restarts_on_action() {
        fakeSettings.screenOffSeconds = 1
        compare(main.screenOffTimer.running, true)
        wait(600)
        main.router.dispatch("down", false)
        wait(600)
        compare(fakePower.verbs, [])
        tryVerify(() => fakePower.verbs.indexOf("screen-off") >= 0, 3000)
    }

    function test_screen_off_timer_off_never_arms() {
        fakeSettings.screenOffSeconds = 0
        compare(main.screenOffArmed, false)
        compare(main.screenOffTimer.running, false)
        main.router.dispatch("down", false)
        compare(main.screenOffTimer.running, false)
        wait(200)
        compare(fakePower.verbs, [])
    }

    function test_screen_off_timer_cancelled_by_switch() {
        fakeSettings.screenOffSeconds = 1
        compare(main.screenOffTimer.running, true)
        fakePower.switching = true
        compare(main.screenOffArmed, false)
        compare(main.screenOffTimer.running, false)
        wait(1200)
        compare(fakePower.verbs, [])
        fakePower.switching = false
        compare(main.screenOffTimer.running, true)
    }

    function test_screen_off_timer_restarts_after_screen_on() {
        fakeSettings.screenOffSeconds = 1
        fakePower.screenOff = true
        compare(main.screenOffArmed, false)
        compare(main.screenOffTimer.running, false)
        tryCompare(main, "rootHidden", true, 2000)
        fakePower.screenOff = false
        compare(main.screenOffTimer.running, true)
        tryVerify(() => fakePower.verbs.indexOf("screen-off") >= 0, 3000)
    }
}
