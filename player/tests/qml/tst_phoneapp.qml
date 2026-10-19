import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "PhoneApp"
    when: windowShown
    visible: true
    width: 640
    height: 960

    readonly property real canvasW: 320
    readonly property real contentTop: 128

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
    }

    QtObject {
        id: fake

        property bool ready: true
        property bool pairing: false
        property int pairingRemaining: 0
        property string pairingMode: ""
        property bool confirmPending: false
        property int confirmPasskey: 0
        property string confirmName: ""
        property var calls: []

        signal companionPaired(string device)

        function reset() {
            fake.ready = true
            fake.pairing = false
            fake.pairingRemaining = 0
            fake.pairingMode = ""
            fake.confirmPending = false
            fake.confirmPasskey = 0
            fake.confirmName = ""
            fake.calls = []
        }

        function startCompanionPairing(timeout) {
            fake.calls = fake.calls.concat(["startCompanionPairing " + timeout])
            fake.pairingMode = "companion"
            fake.pairingRemaining = timeout
            fake.pairing = true
        }

        function stopPairing() {
            fake.calls = fake.calls.concat(["stopPairing"])
            fake.pairing = false
            fake.pairingRemaining = 0
            fake.pairingMode = ""
        }

        function answerConfirm(passkey, yes) {
            fake.calls = fake.calls.concat(["answerConfirm " + passkey + " " + yes])
            if (passkey === fake.confirmPasskey)
                fake.confirmPending = false
        }

        function confirm(passkey) {
            fake.confirmPasskey = passkey
            fake.confirmName = "Pixel 8"
            fake.confirmPending = true
        }
    }

    QtObject {
        id: fakeLink

        property var calls: []

        signal phoneHello()

        function pairPhoneApp() {
            fakeLink.calls = fakeLink.calls.concat(["pairPhoneApp"])
            fake.startCompanionPairing(120)
        }
    }

    QtObject {
        id: counters

        property var toasts: []
    }

    Component {
        id: baseComponent

        Item {}
    }

    Component {
        id: phoneAppComponent

        PhoneAppPairing {
            bluez: fake
            link: fakeLink
            onToast: (first, second) => counters.toasts = counters.toasts.concat([first + "/" + second])
        }
    }

    function init() {
        Ui.userScale = 1.0
        Ui.setSurface(640, 960)
        pages.clear()
        pages.animate = false
        fake.reset()
        fakeLink.calls = []
        pages.dialogFade = 0
        counters.toasts = []
    }

    function open(properties) {
        pages.push(baseComponent)
        const page = pages.push(phoneAppComponent, properties === undefined ? { animated: false } : properties)
        page.completeAll()
        return page
    }

    function test_entry_opens_the_window() {
        const page = open()
        compare(page.header.text, "phone app")
        compare(fakeLink.calls, ["pairPhoneApp"])
        compare(page.open, true)
        compare(page.countItem.text, "pairing 120")
        fake.pairingRemaining = 119
        compare(page.countItem.text, "pairing 119")
        compare(page.panel.hintText.text, "pair from your phone, b to stop")
        compare(page.panel.discoverableText.text, "discoverable")
        compare(page.panel.pairingRing.arcSweep, 360 * 119 / 120)
        verify(router.dispatch("a", false))
        compare(fakeLink.calls, ["pairPhoneApp"])
    }

    function test_layout() {
        const page = open()
        fuzzyCompare(page.countItem.y + page.countItem.baselineOffset, Ui.px(Ui.queueCountBaseline), 0.5)
        compare(page.countItem.font.pixelSize, Ui.fontPx(Ui.listCount.size))
        fuzzyCompare(page.countItem.x, Ui.px(Ui.hubEdge), 0.01)
        const center = page.panel.pairingRing.mapToItem(page, page.panel.pairingRing.width / 2, page.panel.pairingRing.height / 2)
        fuzzyCompare(center.x, Ui.px(tc.canvasW / 2), 0.01)
        fuzzyCompare(center.y, Ui.px(tc.contentTop + 90), 0.01)
        compare(page.panel.pairingRing.width, Ui.px(64))
        fuzzyCompare(page.panel.hintText.y + page.panel.hintText.baselineOffset, Ui.px(tc.contentTop + 90 + 32 * 1.4 + 16), 0.5)
    }

    function test_b_stops_then_pops() {
        const page = open()
        verify(router.dispatch("b", false))
        compare(fake.calls, ["startCompanionPairing 120", "stopPairing"])
        compare(pages.depth, 2)
        compare(page.countItem.text, "")
        compare(page.panel.hintText.text, "press a to make this device discoverable")
        verify(router.dispatch("a", false))
        compare(page.open, true)
        verify(router.dispatch("b", false))
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_leaving_closes_the_window() {
        open()
        compare(fake.pairing, true)
        pages.pop()
        tryCompare(fake, "pairing", false)
        compare(fake.calls[fake.calls.length - 1], "stopPairing")
    }

    function test_code_dialog() {
        const page = open()
        fake.confirm(4821)
        page.codeDialog.complete()
        compare(page.codeDialog.shown, true)
        compare(page.codeDialog.title, "code 004821")
        compare(page.codeDialog.items, ["pair phone", "keep unpaired"])
        compare(page.codeDialog.focusIndex, page.codeDialog.keepIndex)
        verify(router.dispatch("a", false))
        compare(fake.calls[fake.calls.length - 1], "answerConfirm 4821 false")
        compare(counters.toasts, [])
        compare(page.codeDialog.shown, false)
        compare(pages.depth, 2)
        fake.confirm(123456)
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fake.calls[fake.calls.length - 1], "answerConfirm 123456 true")
        compare(counters.toasts.length, 0)
        fake.confirm(123456)
        verify(router.dispatch("b", false))
        compare(fake.calls[fake.calls.length - 1], "answerConfirm 123456 false")
        compare(pages.depth, 2)
        fake.confirm(111111)
        page.codeDialog.complete()
        verify(router.dispatch("up", false))
        fake.confirm(222222)
        page.codeDialog.complete()
        compare(page.codeDialog.shown, true)
        compare(page.codeDialog.title, "code 222222")
        compare(page.codeDialog.focusIndex, page.codeDialog.keepIndex)
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fake.calls[fake.calls.length - 1], "answerConfirm 222222 true")
    }

    function test_paired_then_connected() {
        const page = open()
        fake.stopPairing()
        fake.companionPaired("/org/bluez/hci0/dev_11_22_33_44_55_66")
        page.completeAll()
        compare(page.countItem.text, "paired")
        compare(page.panel.hintText.text, "open the app on your phone")
        fakeLink.phoneHello()
        page.completeAll()
        compare(page.countItem.text, "connected")
        verify(router.dispatch("a", false))
        page.completeAll()
        compare(page.countItem.text, "pairing 120")
        fake.stopPairing()
        page.completeAll()
        compare(page.countItem.text, "connected")
        compare(page.panel.hintText.text, "press a to make this device discoverable")
    }

    function test_code_refused_under_another_dialog() {
        const page = open()
        pages.dialogFade = 1
        fake.confirm(123456)
        compare(page.codeDialog.shown, false)
        compare(fake.calls[fake.calls.length - 1], "answerConfirm 123456 false")
        compare(counters.toasts, ["phone app/pairing refused"])
    }

    function test_code_waits_while_covered() {
        const page = open()
        pages.push(baseComponent)
        compare(page.active, false)
        fake.confirm(123456)
        compare(page.codeDialog.shown, false)
        pages.pop()
        tryCompare(page.codeDialog, "shown", true)
    }

    function test_opens_when_bluetooth_comes_up() {
        fake.ready = false
        const page = open()
        compare(fakeLink.calls, [])
        fake.ready = true
        compare(fakeLink.calls, ["pairPhoneApp"])
        compare(page.open, true)
    }

    function test_unavailable() {
        fake.ready = false
        const page = open()
        compare(fakeLink.calls, [])
        compare(page.countItem.text, "unavailable")
        compare(page.panel.hintText.text, "bluetooth unavailable")
        verify(router.dispatch("a", false))
        compare(fakeLink.calls, [])
    }
}
