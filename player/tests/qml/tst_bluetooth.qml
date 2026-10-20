import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "Bluetooth"
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

    TouchLayer {
        id: layer

        width: 640
        height: 960
        router: router
    }

    QtObject {
        id: fake

        property bool connected: false
        property string deviceName: ""
        property string lastPhone: ""
        property bool transportActive: false
        property string title: ""
        property string artist: ""
        property string album: ""
        property string status: "stopped"
        property int battery: -1
        property bool pairing: false
        property int pairingRemaining: 0
        property string activeRole: "receiver"
        property bool switching: false
        property bool searching: false
        property int searchRemaining: 0
        property var headsets: []
        property var discovered: []
        property string headsetName: ""
        property var calls: []

        function reset() {
            fake.connected = false
            fake.deviceName = ""
            fake.lastPhone = ""
            fake.transportActive = false
            fake.title = ""
            fake.artist = ""
            fake.album = ""
            fake.status = "stopped"
            fake.battery = -1
            fake.pairing = false
            fake.pairingRemaining = 0
            fake.activeRole = "receiver"
            fake.switching = false
            fake.searching = false
            fake.searchRemaining = 0
            fake.headsets = []
            fake.discovered = []
            fake.headsetName = ""
            fake.calls = []
        }

        function startSearch(timeout) {
            fake.calls = fake.calls.concat(["startSearch " + timeout])
        }

        function stopSearch() {
            fake.calls = fake.calls.concat(["stopSearch"])
            fake.searching = false
        }

        function connectHeadset(path) {
            fake.calls = fake.calls.concat(["connectHeadset " + path])
        }

        function disconnectHeadset(path) {
            fake.calls = fake.calls.concat(["disconnectHeadset " + path])
        }

        function forgetHeadset(path) {
            fake.calls = fake.calls.concat(["forgetHeadset " + path])
        }

        function pairDevice(path) {
            fake.calls = fake.calls.concat(["pairDevice " + path])
        }

        function setRole(role) {
            fake.calls = fake.calls.concat(["setRole " + role])
        }

        function connect() {
            fake.deviceName = "Pixel 8"
            fake.battery = 73
            fake.connected = true
        }

        function activate() {
            fake.connect()
            fake.title = "Track Four"
            fake.artist = "Phone Artist"
            fake.status = "playing"
            fake.transportActive = true
        }

        function startPairing(timeout) {
            fake.calls = fake.calls.concat(["startPairing " + timeout])
            fake.pairingRemaining = timeout
            fake.pairing = true
        }

        function stopPairing() {
            fake.calls = fake.calls.concat(["stopPairing"])
            fake.pairing = false
            fake.pairingRemaining = 0
        }

        function play() {
            fake.calls = fake.calls.concat(["play"])
            fake.status = "playing"
        }

        function pause() {
            fake.calls = fake.calls.concat(["pause"])
            fake.status = "paused"
        }
    }

    QtObject {
        id: fakeEq

        property string activeOutput: "speaker"
        property var sources: []
        property var keys: ({})
        property var calls: []

        signal profilesChanged()

        function reset() {
            fakeEq.activeOutput = "speaker"
            fakeEq.keys = ({ headphones: "off", usb: "off", speaker: "off", bt: "off" })
            fakeEq.calls = []
        }

        function profileMap(output) {
            const key = fakeEq.keys[output] === undefined ? "off" : fakeEq.keys[output]
            return { mode: "parametric", key: key, name: "", missing: false, preamp: 0,
                bands: key === "off" ? [] : [{ type: "peak", freq: 1000, freqIndex: 34, gain: 0, q: 1.4 }] }
        }

        function cycleProfile(output, delta) {
            fakeEq.calls = fakeEq.calls.concat([["cycleProfile", output, delta]])
            const k = Object.assign({}, fakeEq.keys)
            k[output] = k[output] === "off" ? "custom" : "off"
            fakeEq.keys = k
            fakeEq.profilesChanged()
        }

        function relist() {
        }
    }

    QtObject {
        id: fakeSettings

        property string btCodec: "auto"
        property string btCodecHeadphones: "auto"

        function reset() {
            fakeSettings.btCodec = "auto"
            fakeSettings.btCodecHeadphones = "auto"
        }
    }

    QtObject {
        id: counters

        property int nowPlayingOpens: 0
        property var toasts: []
    }

    Component {
        id: baseComponent

        Item {}
    }

    Component {
        id: bluetoothComponent

        Bluetooth {
            bluez: fake
            settings: fakeSettings
            onOpenNowPlaying: counters.nowPlayingOpens++
            onToast: (first, second) => counters.toasts = counters.toasts.concat([first + "/" + second])
        }
    }

    function init() {
        Ui.userScale = 1.0
        Ui.setSurface(640, 960)
        pages.clear()
        pages.animate = false
        fake.reset()
        fakeEq.reset()
        fakeSettings.reset()
        counters.nowPlayingOpens = 0
        counters.toasts = []
    }

    function open(properties) {
        pages.push(baseComponent)
        return pages.push(bluetoothComponent, properties === undefined ? { animated: false } : properties)
    }

    function test_hub_frame() {
        const page = open()
        compare(pages.depth, 2)
        compare(page.header.text, "bluetooth")
        compare(page.strip.labels, ["phone", "pairing", "headphones", "eq", "settings"])
        compare(page.strip.activeIndex, 0)
        compare(page.pivotIndex, 0)
        compare(page.shownPivot, 0)
        compare(page.phonePage.visible, true)
        compare(page.pairingPage.visible, false)
        fuzzyCompare(page.header.textBaseline, Ui.px(66), 0.5)
        fuzzyCompare(page.strip.labelItem(0).y + page.strip.labelItem(0).baselineOffset, Ui.px(95), 0.5)
    }

    function test_phone_layout() {
        fake.activate()
        const page = open()
        compare(page.nameText.text, "Pixel 8")
        fuzzyCompare(page.nameText.x, Ui.px(20), 0.01)
        fuzzyCompare(page.nameText.y + page.nameText.baselineOffset, Ui.px(tc.contentTop + 16), 0.5)
        compare(page.nameText.font.pixelSize, Ui.fontPx(20))
        compare(page.nameText.font.pixelSize, 40)
        compare(page.nameText.font.capitalization, Font.AllUppercase)
        fuzzyCompare(page.stateText.x, Ui.px(20), 0.01)
        fuzzyCompare(page.stateText.y + page.stateText.baselineOffset, Ui.px(tc.contentTop + 36), 0.5)
        compare(page.stateText.font.pixelSize, Ui.fontPx(11))
        compare(page.stateText.font.capitalization, Font.AllUppercase)
        fuzzyCompare(page.stateText.color.a, 0.6, 1 / 255)
        compare(page.batteryGlyph.visible, true)
        compare(page.batteryGlyph.name, "battery")
        compare(page.batteryGlyph.width, Ui.px(12))
        fuzzyCompare(page.batteryGlyph.x, page.stateText.x + page.stateText.width + Ui.px(8), 0.01)
        fuzzyCompare(page.batteryGlyph.level, 0.73, 1e-6)
        compare(Qt.colorEqual(page.batteryGlyph.color, Ui.textFull), true)
        compare(page.transportRow.visible, true)
        fuzzyCompare(page.transportRow.y, Ui.px(tc.contentTop + 60), 0.01)
        compare(page.transportRow.height, Ui.px(40))
        compare(page.transportRow.ring, true)
        compare(page.transportRow.ringItem.size, 20)
        compare(page.transportRow.primary, "Track Four")
        compare(page.transportRow.secondary, "Phone Artist")
        compare(page.transportRow.focused, true)
        compare(page.focusBar.visible, true)
        fuzzyCompare(page.focusBar.x, Ui.px(20 - 8), 0.01)
        fuzzyCompare(page.focusBar.y, page.transportRow.y, 0.01)
        compare(page.focusBar.height, page.transportRow.height)
    }

    function test_phone_without_transport() {
        fake.connect()
        const page = open()
        compare(page.transportRow.visible, false)
        compare(page.focusBar.visible, true)
        fuzzyCompare(page.focusBar.x, Ui.px(20 - 8), 0.01)
        fuzzyCompare(page.focusBar.y, Ui.px(128 + 16 - 0.7 * 20), 0.01)
        fuzzyCompare(page.focusBar.y + page.focusBar.height, Ui.px(128 + 36), 0.01)
        fuzzyCompare(page.focusBar.y, page.nameText.y + page.nameText.baselineOffset - Ui.px(0.7 * 20), 0.5)
        fuzzyCompare(page.focusBar.y + page.focusBar.height, page.stateText.y + page.stateText.baselineOffset, 0.5)
        fake.activate()
        fuzzyCompare(page.focusBar.y, page.transportRow.y, 0.01)
        fake.transportActive = false
        fake.connected = false
        compare(page.focusBar.visible, false)
        fake.connected = true
        compare(page.batteryGlyph.visible, true)
        fake.battery = 9
        compare(Qt.colorEqual(page.batteryGlyph.color, Ui.accent), true)
        fake.battery = -1
        compare(page.batteryGlyph.visible, false)
    }

    function test_state_line() {
        const page = open()
        compare(page.stateLine, "disconnected")
        compare(page.stateText.text, "disconnected")
        fake.connect()
        compare(page.stateLine, "connected")
        compare(page.stateText.text, "connected")
        fake.activate()
        compare(page.stateLine, "audio active")
        compare(page.stateText.text, "audio active")
        fake.transportActive = false
        compare(page.stateText.text, "connected")
        fake.connected = false
        compare(page.stateText.text, "disconnected")
        compare(page.stateText.opacity, 1)
    }

    function test_state_line_crossfade() {
        const page = open({ animated: true })
        compare(page.stateText.text, "disconnected")
        compare(page.stateSwapping, false)
        fake.connect()
        compare(page.stateSwapping, true)
        compare(page.stateText.text, "disconnected")
        tryCompare(page, "shownState", "connected", 1000)
        verify(page.stateText.opacity < 1)
        tryCompare(page.stateText, "opacity", 1, 1000)
        compare(page.stateText.text, "connected")
    }

    function test_pivot_switch() {
        const page = open()
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 1)
        compare(page.strip.activeIndex, 1)
        compare(page.phonePage.visible, false)
        compare(page.pairingPage.visible, true)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 2)
        compare(page.headphonesPage.visible, true)
        compare(page.pairingPage.visible, false)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 3)
        compare(page.eqPage.visible, true)
        compare(page.headphonesPage.visible, false)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 4)
        compare(page.settingsPage.visible, true)
        compare(page.eqPage.visible, false)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 4)
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 3)
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 2)
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 1)
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 0)
        compare(page.shownPivot, 0)
        compare(page.phonePage.visible, true)
        compare(page.pairingPage.visible, false)
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 0)
        router.dispatch("rb", true)
        compare(page.pivotIndex, 0)
    }

    function test_eq_pivot() {
        fake.connect()
        fake.status = "paused"
        const page = open({ animated: false, eq: fakeEq })
        page.switchPivot(3)
        compare(page.pivotIndex, 3)
        const eq = page.eqPage
        compare(eq.visible, true)
        compare(eq.embedded, true)
        compare(eq.header.visible, false)
        compare(eq.strip.visible, false)
        compare(eq.slot, "speaker")
        compare(eq.slotText.visible, true)
        compare(eq.slotText.text, "speaker")
        compare(eq.slotText.font.capitalization, Font.AllUppercase)
        fuzzyCompare(eq.slotText.y + eq.slotText.baselineOffset, Ui.px(tc.contentTop + 0.7 * 8), 0.5)
        fuzzyCompare(eq.headphonesPane.y, Ui.px(tc.contentTop + 0.7 * 8 + 12), 0.01)
        compare(JSON.stringify(eq.rows.map((r) => r.key)), JSON.stringify(["mode", "profile"]))
        compare(eq.focusIndex, 0)
        verify(router.dispatch("down", false))
        verify(router.dispatch("a", false))
        compare(eq.editing, true)
        verify(router.dispatch("right", false))
        compare(JSON.stringify(fakeEq.calls[0]), JSON.stringify(["cycleProfile", "speaker", 1]))
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 3)
        verify(router.dispatch("a", false))
        compare(eq.editing, false)
        compare(eq.rows.length, 6)
        verify(router.dispatch("start", false))
        compare(fake.calls, ["play"])
        eq.setFocus(1)
        verify(router.dispatch("a", false))
        compare(eq.editing, true)
        verify(router.dispatch("right", false))
        compare(fakeEq.keys.speaker, "off")
        fakeEq.activeOutput = "headphones"
        compare(fakeEq.keys.speaker, "custom")
        compare(eq.slot, "headphones")
        compare(eq.slotText.text, "headphones")
        compare(eq.editing, false)
        compare(eq.focusIndex, 0)
        compare(JSON.stringify(eq.rows.map((r) => r.key)), JSON.stringify(["mode", "profile"]))
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 2)
        page.switchPivot(1)
        compare(eq.focusIndex, 0)
        verify(router.dispatch("b", false))
        compare(pages.depth, 2)
        compare(JSON.stringify(counters.toasts), JSON.stringify(["bluetooth/hold b 2 s to leave"]))
        fake.connected = false
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_headphones_pivot() {
        const page = open()
        page.switchPivot(2)
        compare(page.pivotIndex, 2)
        const list = page.headphonesList
        compare(list.count, 1)
        compare(list.y, Ui.px(tc.contentTop))
        compare(list.rowPitch, Ui.px(Ui.rowHeight))
        let row = list.itemAtIndex(0).rowItem
        compare(row.primary, "search")
        compare(row.secondary, "")
        compare(row.ring, true)
        compare(row.ringGlyph, "bluetooth")
        compare(row.bold, true)
        compare(row.ringArc, 0)
        compare(row.focused, true)
        compare(page.hpFocusBar.rowY, list.y)
        verify(router.dispatch("a", false))
        compare(fake.calls, ["startSearch 120"])
        fake.searching = true
        fake.searchRemaining = 60
        compare(row.secondary, "searching 60")
        compare(row.ringArc, 180)
        verify(router.dispatch("a", false))
        compare(fake.calls, ["startSearch 120"])
        verify(router.dispatch("b", false))
        compare(fake.calls, ["startSearch 120", "stopSearch"])
        compare(pages.depth, 2)

        fake.headsets = [{ path: "/dev_b", name: "Buds", connected: false, state: "disconnected" },
            { path: "/dev_w", name: "WH", connected: true, state: "audio active" }]
        fake.discovered = [{ path: "/dev_n", name: "New", connected: false, state: "not paired" }]
        fake.headsetName = "WH"
        compare(list.count, 4)
        verify(router.dispatch("down", false))
        row = list.itemAtIndex(1).rowItem
        compare(row.primary, "Buds")
        compare(row.secondary, "disconnected")
        compare(row.ringToggle, true)
        compare(row.ringOn, false)
        compare(list.itemAtIndex(2).rowItem.ringOn, true)
        fake.calls = []
        verify(router.dispatch("a", false))
        compare(fake.calls, ["connectHeadset /dev_b"])
        verify(router.dispatch("down", false))
        verify(router.dispatch("a", false))
        compare(fake.calls, ["connectHeadset /dev_b", "disconnectHeadset /dev_w"])
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "WH")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["disconnect", "forget"]))
        verify(router.dispatch("down", false))
        verify(router.dispatch("a", false))
        compare(page.sheet.shown, false)
        compare(fake.calls[2], "forgetHeadset /dev_w")
        verify(router.dispatch("down", false))
        compare(list.itemAtIndex(3).rowItem.secondary, "not paired")
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, false)
        verify(router.dispatch("a", false))
        compare(fake.calls[3], "pairDevice /dev_n")
        fake.discovered = [{ path: "/dev_n", name: "New", connected: false, state: "pairing" }]
        verify(router.dispatch("a", false))
        compare(fake.calls.length, 4)

        fake.switching = true
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fake.calls.length, 4)
        fake.switching = false

        verify(router.dispatch("b", false))
        compare(pages.depth, 2)
        compare(JSON.stringify(counters.toasts), JSON.stringify(["bluetooth/hold b 2 s to leave"]))
        fake.headsetName = ""
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_headphones_lists_shrink() {
        fake.discovered = [{ path: "/dev_n", name: "New", connected: false, state: "not paired" },
            { path: "/dev_o", name: "Old", connected: false, state: "not paired" }]
        const page = open()
        page.switchPivot(2)
        page.headphonesList.snapTo(2)
        compare(page.headphonesList.focusIndex, 2)
        fake.discovered = []
        tryCompare(page.headphonesList, "focusIndex", 0, 1000)
        fake.searching = true
        fake.searchRemaining = 50
        compare(page.headphonesList.itemAtIndex(0).rowItem.secondary, "searching 50")
        verify(router.dispatch("lb", false))
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
        compare(fake.calls, ["stopSearch"])
    }

    function test_headphones_touch() {
        fake.headsets = [{ path: "/dev_b", name: "Buds", connected: false, state: "disconnected" }]
        const page = open()
        page.switchPivot(2)
        compare(page.touchPress("hprow", 1, 0, null), "a")
        compare(page.headphonesList.focusIndex, 1)
        verify(page.touchContext("hprow", 1, null))
        compare(page.sheet.shown, true)
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(pages.depth, 2)
        verify(!page.touchContext("hprow", 0, null))
    }

    function test_source_role() {
        fake.activeRole = "headphones"
        fake.lastPhone = "Pixel 8"
        const page = open({ animated: false, eq: fakeEq })
        compare(page.pivotIndex, 2)
        compare(page.headphonesPage.visible, true)
        page.switchPivot(1)
        compare(page.eqSlot, "bt")
        compare(page.eqPage.slot, "bt")
        page.switchPivot(-3)
        compare(page.pivotIndex, 0)
        verify(router.dispatch("a", false))
        compare(fake.calls, ["setRole receiver"])
        compare(JSON.stringify(counters.toasts), JSON.stringify(["/connect from your phone"]))
        fake.activeRole = "receiver"
        compare(page.eqSlot, "speaker")
    }

    function test_eq_pivot_header_reverts_edit() {
        const page = open({ animated: false, eq: fakeEq })
        page.switchPivot(3)
        const eq = page.eqPage
        eq.setFocus(1)
        verify(router.dispatch("a", false))
        verify(router.dispatch("right", false))
        compare(fakeEq.keys.speaker, "custom")
        verify(router.dispatch("b", false))
        compare(eq.editing, false)
        compare(fakeEq.keys.speaker, "off")
        compare(pages.depth, 2)
    }

    function test_pivot_switch_animated() {
        const page = open({ animated: true })
        router.dispatch("rb", false)
        compare(page.pivotIndex, 1)
        compare(page.strip.activeIndex, 1)
        compare(page.switching, true)
        compare(page.shownPivot, 0)
        compare(page.phonePage.visible, true)
        tryCompare(page, "shownPivot", 1, 2000)
        compare(page.pairingPage.visible, true)
        compare(page.phonePage.visible, false)
        verify(page.pairingPage.x > 0 || page.pairingPage.opacity < 1)
        tryCompare(page, "switching", false, 2000)
        compare(page.pairingPage.x, 0)
        compare(page.pairingPage.opacity, 1)
        compare(page.phonePage.x, 0)
        compare(page.phonePage.opacity, 1)
        router.dispatch("lb", false)
        compare(page.switching, true)
        page.completeAll()
        compare(page.switching, false)
        compare(page.shownPivot, 0)
        compare(page.phonePage.x, 0)
        compare(page.phonePage.opacity, 1)
    }

    function test_pairing_layout() {
        const page = open({ animated: false, pivotIndex: 1 })
        compare(page.shownPivot, 1)
        compare(page.pairingPage.visible, true)
        compare(page.discoverableText.text, "discoverable")
        fuzzyCompare(page.discoverableText.y + page.discoverableText.baselineOffset, Ui.px(tc.contentTop + 16), 0.5)
        compare(page.discoverableText.font.pixelSize, Ui.fontPx(20))
        compare(page.pairingRing.width, Ui.px(64))
        compare(page.pairingRing.glyph, "bluetooth")
        compare(page.pairingRing.focused, true)
        compare(page.pairingRing.strokeWidth, Ui.px(4))
        const center = page.pairingRing.mapToItem(page, page.pairingRing.width / 2, page.pairingRing.height / 2)
        fuzzyCompare(center.x, Ui.px(tc.canvasW / 2), 0.01)
        fuzzyCompare(center.y, Ui.px(tc.contentTop + 90), 0.01)
        compare(page.pairingRing.arcSweep, 0)
        compare(page.hintText.text, "press a to make this device discoverable\nfor the phone app, turn it on in settings > system")
        compare(page.hintText.font.pixelSize, Ui.fontPx(11))
        compare(page.hintText.font.styleName, "Light")
        compare(page.hintText.font.capitalization, Font.AllUppercase)
        fuzzyCompare(page.hintText.color.a, 0.6, 1 / 255)
        compare(page.hintText.horizontalAlignment, Text.AlignHCenter)
        compare(page.hintText.lineHeight, Ui.px(11))
        fuzzyCompare(page.hintText.y + page.hintText.baselineOffset, Ui.px(tc.contentTop + 90 + 32 * 1.4 + 16), 0.5)
        fuzzyCompare(page.hintText.x, Ui.px(20), 0.01)
        compare(page.hintText.width, Ui.px(tc.canvasW - 40))
        compare(page.dapModeText.visible, false)
        compare(page.overlay, false)
    }

    function test_a_starts_pairing() {
        const page = open()
        router.dispatch("rb", false)
        compare(page.pairingTimeout, 120)
        verify(router.dispatch("a", false))
        compare(fake.calls, ["startPairing 120"])
        compare(fake.pairing, true)
        compare(page.hintText.text, "pair from your phone, b to stop\nfor the phone app, turn it on in settings > system")
        page.companion = true
        compare(page.hintText.text, "pair from your phone, b to stop\npair the phone app from settings > system")
        verify(router.dispatch("a", false))
        compare(fake.calls, ["startPairing 120"])
        router.dispatch("a", true)
        compare(fake.calls, ["startPairing 120"])
    }

    function test_arc_follows_remaining() {
        const page = open({ animated: false, pivotIndex: 1 })
        fake.pairingRemaining = 73
        fake.pairing = true
        fuzzyCompare(page.arcSweep, 360 * 73 / 120, 1e-6)
        fuzzyCompare(page.pairingRing.arcSweep, 360 * 73 / 120, 1e-6)
        fake.pairingRemaining = 120
        fuzzyCompare(page.arcSweep, 360, 1e-6)
        fake.pairingRemaining = 60
        fuzzyCompare(page.arcSweep, 180, 1e-6)
        fake.pairingRemaining = 0
        compare(page.arcSweep, 0)
        fake.pairingRemaining = 30
        fake.pairing = false
        compare(page.arcSweep, 0)
    }

    function test_paired_switches_to_phone() {
        const page = open({ animated: true })
        router.dispatch("rb", false)
        page.completeAll()
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 1)
        router.dispatch("a", false)
        compare(fake.pairing, true)
        fake.connect()
        compare(page.pivotIndex, 1)
        fake.pairing = false
        compare(page.pivotIndex, 0)
        compare(page.strip.activeIndex, 0)
        compare(page.switching, true)
        compare(page.shownPivot, 1)
        tryCompare(page, "shownPivot", 0, 2000)
        tryCompare(page, "switching", false, 2000)
        compare(page.phonePage.visible, true)
        compare(page.pairingPage.visible, false)
        compare(page.phonePage.x, 0)
        compare(page.phonePage.opacity, 1)
    }

    function test_pairing_end_without_phone_stays() {
        const page = open({ animated: false, pivotIndex: 1 })
        router.dispatch("a", false)
        compare(fake.pairing, true)
        fake.pairing = false
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 1)
        compare(page.pairingPage.visible, true)
    }

    function test_b_two_steps() {
        const page = open()
        router.dispatch("rb", false)
        router.dispatch("a", false)
        compare(fake.pairing, true)
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(fake.calls, ["startPairing 120", "stopPairing"])
        compare(fake.pairing, false)
        compare(pages.depth, 2)
        compare(page.pivotIndex, 1)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_guard_only_while_connected() {
        fake.connect()
        const page = open()
        compare(page.guarded, true)
        verify(router.dispatch("b", false))
        compare(pages.depth, 2)
        compare(JSON.stringify(counters.toasts), JSON.stringify(["bluetooth/hold b 2 s to leave"]))
        fake.connected = false
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_guard_hold_leaves_in_every_state() {
        fake.connect()
        let page = open()
        router.press("b")
        verify(router.dispatch("b", false))
        compare(page.guardTimer.running, true)
        compare(page.guardTimer.interval, 2000)
        wait(1000)
        router.release("b")
        compare(page.guardTimer.running, false)
        compare(pages.depth, 2)
        router.press("b")
        tryCompare(pages, "depth", 1, 3000)
        router.release("b")
        pages.clear()
        page = open()
        page.switchPivot(1)
        verify(router.dispatch("a", false))
        compare(fake.pairing, true)
        router.press("b")
        tryCompare(pages, "depth", 1, 3000)
        router.release("b")
        compare(fake.pairing, false)
        fake.connected = false
        pages.clear()
        page = open()
        router.press("b")
        page.guardTimer.triggered()
        router.release("b")
        compare(pages.depth, 1)
    }

    function test_guard_header_touch() {
        fake.connect()
        const page = open()
        const label = page.header.children[0]
        tc.tapAt(label, Ui.px(10), label.height / 2)
        compare(pages.depth, 2)
        compare(JSON.stringify(counters.toasts), JSON.stringify(["bluetooth/hold b 2 s to leave"]))
        const at = label.mapToItem(tc, Ui.px(10), label.height / 2)
        mousePress(label, Ui.px(10), label.height / 2)
        compare(page.guardTimer.running, true)
        tryCompare(pages, "depth", 1, 3000)
        mouseRelease(tc, at.x, at.y)
        wait(0)
    }

    function test_last_phone() {
        fake.lastPhone = "Pixel 8"
        const page = open()
        compare(page.connected, false)
        compare(page.nameText.text, "Pixel 8")
        compare(page.stateText.text, "disconnected")
        compare(page.focusBar.visible, true)
        verify(router.dispatch("a", false))
        compare(fake.calls, [])
        compare(JSON.stringify(counters.toasts), JSON.stringify(["/connect from your phone"]))
        fake.lastPhone = ""
        compare(page.nameText.text, "")
        compare(page.focusBar.visible, false)
        verify(router.dispatch("a", false))
        compare(counters.toasts.length, 1)
    }

    function test_b_on_phone_pops() {
        open()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_a_on_transport_row_opens_now_playing() {
        fake.activate()
        open()
        verify(router.dispatch("a", false))
        compare(counters.nowPlayingOpens, 1)
        compare(fake.calls, [])
        router.dispatch("a", true)
        compare(counters.nowPlayingOpens, 1)
    }

    function test_a_and_start_toggle_phone() {
        fake.connect()
        fake.status = "paused"
        open()
        verify(router.dispatch("a", false))
        compare(fake.calls, ["play"])
        compare(counters.nowPlayingOpens, 0)
        verify(router.dispatch("start", false))
        compare(fake.calls, ["play", "pause"])
        fake.activate()
        verify(router.dispatch("start", false))
        compare(fake.calls, ["play", "pause", "pause"])
        compare(counters.nowPlayingOpens, 0)
    }

    function test_start_disconnected_falls_through() {
        open()
        let nowPlaying = 0
        const onNowPlaying = function() { nowPlaying++ }
        router.nowPlaying.connect(onNowPlaying)
        verify(router.dispatch("start", false))
        compare(nowPlaying, 1)
        compare(fake.calls, [])
        router.dispatch("a", false)
        compare(fake.calls, [])
        router.nowPlaying.disconnect(onNowPlaying)
    }

    function test_pulse_on_connect() {
        const page = open({ animated: true, pivotIndex: 1 })
        compare(page.pulsing, false)
        fake.connect()
        compare(page.pulsing, true)
        tryVerify(function() { return page.pulseHolder.scale > 1.2 }, 1000)
        tryCompare(page, "pulsing", false, 2000)
        compare(page.pulseHolder.scale, 1)
    }

    function test_screen_off_stops_motion() {
        const page = open({ animated: true })
        router.dispatch("rb", false)
        fake.connect()
        compare(page.switching, true)
        page.screenOff = true
        compare(page.live, false)
        compare(page.switching, false)
        compare(page.stateSwapping, false)
        compare(page.pulsing, false)
        compare(page.shownPivot, 1)
        compare(page.pairingPage.x, 0)
        compare(page.pairingPage.opacity, 1)
        compare(page.stateText.opacity, 1)
        compare(page.stateText.text, "connected")
        compare(page.pulseHolder.scale, 1)
        compare(page.header.animated, false)
        compare(page.strip.animated, false)
        compare(page.pairingRing.animated, false)
        compare(page.transportRow.animated, false)
        compare(page.focusBar.animated, false)
        router.dispatch("lb", false)
        compare(page.pivotIndex, 0)
        compare(page.shownPivot, 0)
        compare(page.switching, false)
        fake.connected = false
        compare(page.stateText.text, "disconnected")
        compare(page.stateSwapping, false)
    }

    function test_landscape() {
        Ui.setSurface(1920, 1080)
        fake.activate()
        pages.push(baseComponent)
        const page = pages.push(bluetoothComponent, { animated: false })
        compare(Ui.landscape, true)
        fuzzyCompare(page.nameText.y + page.nameText.baselineOffset, Ui.px(98 + 16), 0.5)
        fuzzyCompare(page.transportRow.y, Ui.px(98 + 60), 0.01)
        compare(page.header.height, Ui.px(70))
        fuzzyCompare(page.header.textBaseline, Ui.px(66), 0.5)
        router.dispatch("rb", false)
        const center = page.pairingRing.mapToItem(page, page.pairingRing.width / 2, page.pairingRing.height / 2)
        fuzzyCompare(center.x, Ui.px(Ui.canvasW / 2), 0.01)
        fuzzyCompare(center.y, Ui.px(98 + 90), 0.01)
    }

    function tapAt(item, x, y) {
        mouseClick(item, x, y)
        wait(0)
        wait(0)
    }

    function test_touch_transport_row_opens_now_playing() {
        fake.activate()
        const page = open()
        tc.tapAt(page.transportRow)
        compare(counters.nowPlayingOpens, 1)
    }

    function test_touch_name_block_toggles() {
        fake.connect()
        const page = open()
        tc.tapAt(page.stateText)
        compare(fake.calls, ["play"])
    }

    function test_touch_pairing_ring_starts_pairing() {
        const page = open({ animated: false, pivotIndex: 1 })
        compare(page.shownPivot, 1)
        tc.tapAt(page.pairingRing)
        compare(fake.calls, ["startPairing 120"])
    }

    function codecSheet(page) {
        verify(router.dispatch("a", false))
        compare(page.codecOpen, true)
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "codec")
        page.sheet.complete()
    }

    function focusedItem(page) {
        return page.sheet.items[page.sheet.focusIndex]
    }

    function test_settings_pivot_row() {
        const page = open({ animated: false, eq: fakeEq })
        page.switchPivot(3)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 4)
        compare(page.shownPivot, 4)
        const row = page.codecItem
        compare(row.y, Ui.px(tc.contentTop))
        compare(row.height, Ui.px(Ui.settingsPitch))
        compare(row.titleItem.text, "codec")
        compare(row.titleItem.x, Ui.px(Ui.hubEdge))
        compare(row.titleItem.font.pixelSize, Ui.font(Ui.settingsItem).pixelSize)
        compare(row.titleItem.font.capitalization, Font.AllUppercase)
        compare(row.valueItem.text, "auto")
        compare(row.valueItem.font.pixelSize, Ui.font(Ui.settingsValue).pixelSize)
        fuzzyCompare(row.valueItem.x + row.valueItem.width, page.width - Ui.px(Ui.hubEdge), 0.5)
        fuzzyCompare(row.valueItem.color.a, Ui.settingsValueFocused, 1 / 255)
        verify(row.titleItem.x + row.titleItem.width <= row.valueItem.x - Ui.px(Ui.gutter) + 0.5)
        compare(page.settingsFocusBar.visible, true)
        fuzzyCompare(page.settingsFocusBar.y, row.y, 0.01)
        compare(page.settingsFocusBar.height, row.height)
    }

    function test_settings_codec_receiver() {
        fakeSettings.btCodec = "aac"
        fakeSettings.btCodecHeadphones = "ldac"
        const page = open({ animated: false, pivotIndex: 4 })
        compare(page.codecItem.valueItem.text, "aac (pending)")
        tc.codecSheet(page)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["auto", "aptx", "aac", "sbc"]))
        compare(page.sheet.mark, 2)
        compare(tc.focusedItem(page), "aac")
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(tc.focusedItem(page), "auto")
        compare(fakeSettings.btCodec, "aac")
        page.btApplied = "aac"
        compare(page.codecOpen, true)
        compare(page.codecItem.valueItem.text, "aac")
        verify(router.dispatch("up", false))
        compare(tc.focusedItem(page), "sbc")
        verify(router.dispatch("a", false))
        compare(page.codecOpen, false)
        compare(fakeSettings.btCodec, "sbc")
        compare(fakeSettings.btCodecHeadphones, "ldac")
        compare(page.codecItem.valueItem.text, "sbc (pending)")
        page.btApplied = "sbc"
        compare(page.codecItem.valueItem.text, "sbc")
        tc.codecSheet(page)
        verify(router.dispatch("down", false))
        verify(router.dispatch("b", false))
        compare(page.codecOpen, false)
        compare(page.sheet.shown, false)
        compare(fakeSettings.btCodec, "sbc")
        compare(pages.depth, 2)
        fakeSettings.btCodec = "auto"
        compare(page.codecItem.valueItem.text, "auto (pending)")
        page.btApplied = "auto"
        compare(page.codecItem.valueItem.text, "auto")
    }

    function test_settings_codec_headphones() {
        fake.activeRole = "headphones"
        fakeSettings.btCodecHeadphones = "ldac"
        const page = open({ animated: false, pivotIndex: 4, btApplied: "aac" })
        compare(page.codecItem.valueItem.text, "ldac (pending)")
        fakeSettings.btCodecHeadphones = "auto"
        compare(page.codecItem.valueItem.text, "auto (pending)")
        fakeSettings.btCodecHeadphones = "aac"
        compare(page.codecItem.valueItem.text, "aac")
        fakeSettings.btCodecHeadphones = "sbc"
        compare(page.codecItem.valueItem.text, "sbc")
        page.btApplied = "auto"
        fakeSettings.btCodecHeadphones = "ldac"
        compare(page.codecItem.valueItem.text, "ldac")
        tc.codecSheet(page)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["auto", "ldac", "aptx hd", "aptx", "aac", "sbc"]))
        for (let i = 0; i < 4; i++)
            verify(router.dispatch("down", false))
        compare(tc.focusedItem(page), "sbc")
        verify(router.dispatch("down", true))
        compare(tc.focusedItem(page), "sbc")
        verify(router.dispatch("down", false))
        compare(tc.focusedItem(page), "auto")
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(fakeSettings.btCodecHeadphones, "ldac")
        verify(router.dispatch("a", false))
        compare(fakeSettings.btCodecHeadphones, "aptx_hd")
        compare(fakeSettings.btCodec, "auto")
        compare(page.codecItem.valueItem.text, "aptx hd")
        compare(page.codecItem.valueItem.font.capitalization, Font.AllUppercase)
    }

    function test_settings_codec_sheet_closes() {
        const page = open({ animated: false, pivotIndex: 4 })
        tc.codecSheet(page)
        verify(router.dispatch("down", false))
        fake.activeRole = "headphones"
        compare(page.codecOpen, false)
        compare(page.sheet.shown, false)
        compare(fakeSettings.btCodec, "auto")
        compare(fakeSettings.btCodecHeadphones, "auto")
        compare(page.codecItem.valueItem.text, "auto")
        tc.codecSheet(page)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["auto", "ldac", "aptx hd", "aptx", "aac", "sbc"]))
        verify(router.dispatch("down", false))
        fakeSettings.btCodecHeadphones = "aac"
        compare(page.codecOpen, false)
        compare(fakeSettings.btCodecHeadphones, "aac")
        tc.codecSheet(page)
        verify(router.dispatch("down", false))
        page.screenOff = true
        compare(page.codecOpen, false)
        compare(fakeSettings.btCodecHeadphones, "aac")
        page.screenOff = false
        tc.codecSheet(page)
        verify(router.dispatch("down", false))
        page.revertOrConfirmForRotation()
        compare(page.codecOpen, false)
        compare(fakeSettings.btCodecHeadphones, "aac")
        tc.codecSheet(page)
        verify(router.dispatch("down", false))
        pages.push(baseComponent)
        compare(page.codecOpen, false)
        compare(fakeSettings.btCodecHeadphones, "aac")
    }

    function test_settings_codec_blocked_while_switching() {
        fake.switching = true
        const page = open({ animated: false, pivotIndex: 4 })
        verify(router.dispatch("a", false))
        compare(page.codecOpen, false)
        compare(page.sheet.shown, false)
        fake.switching = false
        tc.tapAt(page.codecItem, page.codecItem.width / 2, page.codecItem.height / 2)
        compare(page.codecOpen, true)
        verify(router.dispatch("b", false))
        compare(page.codecOpen, false)
    }

    function test_settings_codec_rise_holds() {
        const page = open({ animated: true, pivotIndex: 4 })
        page.completeAll()
        verify(router.dispatch("a", false))
        compare(page.codecOpen, true)
        verify(page.sheet.animating)
        verify(router.dispatch("down", false))
        verify(router.dispatch("a", false))
        compare(page.codecOpen, true)
        compare(page.sheet.focusIndex, 0)
        tryCompare(page.sheet, "animating", false, 2000)
        compare(page.sheet.focusIndex, 1)
        compare(fakeSettings.btCodec, "auto")
        verify(router.dispatch("b", false))
        compare(page.codecOpen, false)
        compare(fakeSettings.btCodec, "auto")
    }

    function test_touch_pivot_by_tap() {
        const page = open()
        const label = page.strip.labelItem(1)
        tc.tapAt(label, Ui.px(4), label.height / 2)
        compare(page.pivotIndex, 1)
    }
}
