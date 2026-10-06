import QtQuick
import QtTest
import Casdap

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

    QtObject {
        id: fake

        property bool connected: false
        property string deviceName: ""
        property bool transportActive: false
        property string title: ""
        property string artist: ""
        property string album: ""
        property string status: "stopped"
        property int battery: -1
        property bool pairing: false
        property int pairingRemaining: 0
        property var calls: []

        function reset() {
            fake.connected = false
            fake.deviceName = ""
            fake.transportActive = false
            fake.title = ""
            fake.artist = ""
            fake.album = ""
            fake.status = "stopped"
            fake.battery = -1
            fake.pairing = false
            fake.pairingRemaining = 0
            fake.calls = []
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
        id: counters

        property int nowPlayingOpens: 0
    }

    Component {
        id: baseComponent

        Item {}
    }

    Component {
        id: bluetoothComponent

        Bluetooth {
            bluez: fake
            onOpenNowPlaying: counters.nowPlayingOpens++
        }
    }

    function init() {
        Ui.userScale = 1.0
        Ui.windowW = 640
        Ui.windowH = 960
        pages.clear()
        pages.animate = false
        fake.reset()
        counters.nowPlayingOpens = 0
    }

    function open(properties) {
        pages.push(baseComponent)
        return pages.push(bluetoothComponent, properties === undefined ? { animated: false } : properties)
    }

    function test_hub_frame() {
        const page = open()
        compare(pages.depth, 2)
        compare(page.header.text, "bluetooth")
        compare(page.strip.labels, ["phone", "pairing"])
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
        compare(page.hintText.text, "press a to make this device discoverable")
        compare(page.hintText.font.pixelSize, Ui.fontPx(11))
        compare(page.hintText.font.capitalization, Font.AllUppercase)
        compare(page.hintText.horizontalAlignment, Text.AlignHCenter)
        compare(page.hintText.lineHeight, Ui.px(11))
        fuzzyCompare(page.hintText.y + page.hintText.baselineOffset, Ui.px(tc.contentTop + 90 + 32 + 16), 0.5)
        fuzzyCompare(page.hintText.x, Ui.px(20), 0.01)
        compare(page.hintText.width, Ui.px(tc.canvasW - 40))
    }

    function test_a_starts_pairing() {
        const page = open()
        router.dispatch("rb", false)
        compare(page.pairingTimeout, 120)
        verify(router.dispatch("a", false))
        compare(fake.calls, ["startPairing 120"])
        compare(fake.pairing, true)
        compare(page.hintText.text, "pair from your phone, b to stop")
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
        Ui.windowW = 1920
        Ui.windowH = 1080
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
}
