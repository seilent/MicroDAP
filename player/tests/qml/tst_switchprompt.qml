import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "SwitchPrompt"
    when: windowShown
    visible: true
    width: 320
    height: 240

    QtObject {
        id: fakePower

        property bool screenOff: false
        property var calls: []

        signal showSwitchPrompt()
        signal switchFailed()
        signal volumeChanged(int volume)

        function keepInBackground() {
            fakePower.calls = fakePower.calls.concat(["keep"])
        }

        function closeAndSwitch() {
            fakePower.calls = fakePower.calls.concat(["close"])
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 0
        property var current: ({})
    }

    Main {
        id: main

        power: fakePower
        player: fakePlayer
        shotWidth: 640
        shotHeight: 960
    }

    readonly property var prompt: main.switchPrompt
    readonly property var home: main.pageStack.currentItem

    function init() {
        tryCompare(main.pageStack, "inputLocked", false, 5000)
        main.pageStack.completeAll()
        fakePower.screenOff = false
        fakePlayer.status = 0
        fakePower.calls = []
        prompt.close()
        prompt.complete()
        main.overlayLayer.toast.hide()
        home.completeAll()
        home.sheet.close()
        home.sheet.complete()
        if (home.quickplayShown)
            home.showMenu()
        home.completeAll()
    }

    function test_defaults() {
        compare(main.overlay, false)
        compare(main.off, false)
        compare(home.objectName, "home")
        compare(prompt.shown, false)
        compare(prompt.visible, false)
        compare(prompt.title, "game mode")
        compare(prompt.items, ["close player", "keep in background"])
        compare(prompt.closeIndex, 0)
        compare(prompt.keepIndex, 1)
        compare(prompt.duration, Motion.dialog)
        compare(prompt.hideDuration, Motion.dialogHide)
        compare(prompt.fromScale, Motion.dialogScale)
        compare(prompt.scrimAlpha, Motion.dialogScrim)
        fuzzyCompare(prompt.scrimAlpha, 0.8, 1e-6)
        compare(Qt.colorEqual(prompt.scrimColor, Ui.scrimOverlay), true)
        fuzzyCompare(prompt.scrim.color.a, 0.8, 1 / 255)
        compare(prompt.scrim.color.r, 0)
        compare(prompt.pitch, Ui.px(Ui.settingsPitch))
        compare(prompt.edge, Ui.px(Ui.edge))
        compare(prompt.titleText.font.pixelSize, Ui.fontPx(Ui.dialogTitle.size))
        compare(prompt.titleText.font.capitalization, Font.AllLowercase)
        compare(main.router.modal, prompt)
        compare(main.router.modalUp, false)
    }

    function test_focus_follows_playback() {
        fakePlayer.status = 1
        fakePower.showSwitchPrompt()
        compare(prompt.shown, true)
        compare(prompt.focusIndex, prompt.keepIndex)
        compare(prompt.openAnimation.running, true)
        compare(prompt.visible, true)
        prompt.complete()
        compare(prompt.body.opacity, 1)
        compare(prompt.body.scale, 1)
        compare(prompt.scrim.opacity, 1)
        prompt.close()
        prompt.complete()

        fakePlayer.status = 2
        fakePower.showSwitchPrompt()
        compare(prompt.focusIndex, prompt.closeIndex)
        prompt.close()
        prompt.complete()

        fakePlayer.status = 0
        fakePower.showSwitchPrompt()
        compare(prompt.focusIndex, prompt.closeIndex)
    }

    function test_b_closes_and_leaves_stack() {
        const depth = main.pageStack.depth
        const focus = home.focusIndex
        fakePower.showSwitchPrompt()
        compare(prompt.shown, true)
        compare(main.router.modalUp, true)
        verify(main.router.dispatch("b", false))
        compare(prompt.shown, false)
        compare(prompt.closeAnimation.running, true)
        compare(prompt.closeAnimation.animations[0].duration, Motion.dialogHide)
        tryCompare(prompt, "visible", false, 2000)
        compare(main.pageStack.depth, depth)
        compare(main.pageStack.currentItem, home)
        compare(home.focusIndex, focus)
        compare(home.quickplayShown, false)
        compare(fakePower.calls, [])
    }

    function test_second_show_closes() {
        fakePower.showSwitchPrompt()
        compare(prompt.shown, true)
        fakePower.showSwitchPrompt()
        compare(prompt.shown, false)
        compare(prompt.closeAnimation.running, true)
        fakePower.showSwitchPrompt()
        compare(prompt.shown, true)
        compare(prompt.openAnimation.running, true)
    }

    function test_screen_off_ignored() {
        fakePower.screenOff = true
        compare(main.off, true)
        fakePower.showSwitchPrompt()
        compare(prompt.shown, false)
        compare(prompt.visible, false)
        fakePower.screenOff = false
        fakePower.showSwitchPrompt()
        compare(prompt.shown, true)
        fakePower.screenOff = true
        compare(prompt.visible, false)
        compare(prompt.animating, false)
        compare(prompt.shown, true)
    }

    function test_closes_sheet_first() {
        home.openPinSheet({ id: 1, title: "Pinned", section: "pins" })
        compare(home.sheet.shown, true)
        fakePower.showSwitchPrompt()
        compare(home.sheet.shown, false)
        compare(prompt.shown, true)
    }

    function test_input_locked_to_prompt() {
        const focus = home.focusIndex
        fakePower.showSwitchPrompt()
        compare(prompt.focusIndex, prompt.closeIndex)
        verify(main.router.dispatch("down", false))
        compare(prompt.focusIndex, prompt.keepIndex)
        verify(main.router.dispatch("down", false))
        compare(prompt.focusIndex, prompt.keepIndex)
        verify(main.router.dispatch("up", false))
        compare(prompt.focusIndex, prompt.closeIndex)
        verify(main.router.dispatch("left", false))
        verify(main.router.dispatch("y", false))
        verify(main.router.dispatch("x", false))
        verify(main.router.dispatch("lb", false))
        compare(home.focusIndex, focus)
        compare(home.quickplayShown, false)
        compare(home.sheet.shown, false)
        compare(main.pageStack.depth, 1)
        main.router.press("a")
        compare(prompt.shown, true)
        compare(fakePower.calls, [])
    }

    function test_a_runs_items() {
        fakePlayer.status = 1
        fakePower.showSwitchPrompt()
        compare(prompt.focusIndex, prompt.keepIndex)
        verify(main.router.dispatch("a", false))
        compare(fakePower.calls, ["keep"])
        compare(prompt.shown, true)
        verify(main.router.dispatch("a", true))
        compare(fakePower.calls, ["keep"])
        verify(main.router.dispatch("up", false))
        verify(main.router.dispatch("a", false))
        compare(fakePower.calls, ["keep", "close"])
        compare(prompt.shown, true)
        compare(main.pageStack.depth, 1)
    }

    function test_page_focus_hides_under_dialog() {
        compare(main.pageStack.dialogFade, 0)
        compare(home.dialogFade, 0)
        compare(home.dialogUp, false)
        fuzzyCompare(home.focusBar.opacity, 1, 1e-6)
        fakePower.showSwitchPrompt()
        prompt.complete()
        compare(home.dialogUp, true)
        compare(prompt.scrim.opacity, 1)
        compare(main.pageStack.dialogFade, 1)
        compare(home.focusOpacity, 0)
        fuzzyCompare(home.focusBar.opacity, 0, 1e-6)
        prompt.close()
        compare(prompt.closeAnimation.running, true)
        verify(home.focusBar.opacity < 1)
        prompt.complete()
        compare(main.pageStack.dialogFade, 0)
        compare(home.dialogUp, false)
        fuzzyCompare(home.focusBar.opacity, 1, 1e-6)
        compare(main.powerMenu.shown, false)
    }

    function test_switch_failed_toasts() {
        fakePower.showSwitchPrompt()
        compare(prompt.shown, true)
        fakePower.switchFailed()
        compare(prompt.shown, false)
        compare(main.overlayLayer.toast.phase, "in")
        compare(main.overlayLayer.toast.artist, "")
        compare(main.overlayLayer.toast.title, "switch failed")
        compare(main.router.modalUp, false)
        main.overlayLayer.toast.hide()
        fakePower.switchFailed()
        compare(prompt.shown, false)
        compare(main.overlayLayer.toast.phase, "in")
    }
}
