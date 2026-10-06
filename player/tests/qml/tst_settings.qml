import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "SettingsScreen"
    when: windowShown
    visible: true
    width: 640
    height: 960

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
        pad: null
    }

    QtObject {
        id: fakeSettings

        property string output: "auto"
        property string hpGain: "normal"
        property string hpAmp: "lohifi"
        property int spkGain: 102
        property int volumeStep: 5
        property string rgb: "armada"
        property string btCodec: "auto"
        property var roots: ["/home/armada/Music"]
        property string accent: "#FA2A55"
        property real scale: 1.0
        property bool pocketControls: false
        property int saverSeconds: 30
        property bool overlayToasts: false
        property int screenOffSeconds: 300
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
    }

    QtObject {
        id: fakePower

        property var verbs: []
        property int switches: 0
        property var driftPaths: []

        function runVerb(verb) {
            fakePower.verbs = fakePower.verbs.concat([verb])
            return true
        }

        function closeAndSwitch() {
            fakePower.switches++
        }
    }

    QtObject {
        id: legacyPower

        property var verbs: []
        property var driftPaths: []

        function runVerb(verb) {
            legacyPower.verbs = legacyPower.verbs.concat([verb])
        }
    }

    QtObject {
        id: fakeBridge

        property int closes: 0

        function closePlayer() {
            fakeBridge.closes++
        }
    }

    QtObject {
        id: fakePlayer

        property var current: ({ id: 3, title: "Sticking With You", sampleRate: 44100, bitDepth: 16 })
    }

    QtObject {
        id: fakeAudio

        property string pcmFormat: "44100 S24_LE"
        property int graphRate: 44100
    }

    QtObject {
        id: fakeOutput

        property bool jackPresent: true
        property bool usbPresent: false
        property string activeOutput: "headphones"
    }

    QtObject {
        id: fakeRgb

        property bool supported: true
    }

    QtObject {
        id: counters

        property int pair: 0
        property int gameMode: 0
    }

    Component {
        id: settingsComponent

        SettingsScreen {
            settings: fakeSettings
            power: fakePower
            player: fakePlayer
            audioInfo: fakeAudio
            outputControl: fakeOutput
            rgbControl: fakeRgb
            folders: ["/home/armada/Music", "/run/media/armada/SD"]
            onPairPhone: counters.pair++
            onGameModeRequested: counters.gameMode++
        }
    }

    Component {
        id: blankComponent

        Page {}
    }

    readonly property var expectedKeys: [
        "label-output", "output", "hpGain", "hpAmp", "spkGain", "volumeStep", "codec", "rate", "replaygain", "pair",
        "label-library", "folders",
        "label-display", "accent", "orientation", "scale", "saver", "rgb", "toasts",
        "label-power", "screenOff", "suspend", "pocket", "wifi", "gameMode",
        "about"
    ]
    readonly property var overlayKeys: tc.expectedKeys.filter((k) => k !== "rgb")

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.windowW = w
        Ui.windowH = h
        pages.width = w
        pages.height = h
        router.width = w
        router.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        Ui.accent = "#FA2A55"
        pages.clear()
        pages.animate = false
        fakeSettings.output = "auto"
        fakeSettings.hpGain = "normal"
        fakeSettings.hpAmp = "lohifi"
        fakeSettings.spkGain = 102
        fakeSettings.volumeStep = 5
        fakeSettings.rgb = "armada"
        fakeSettings.btCodec = "auto"
        fakeSettings.roots = ["/home/armada/Music"]
        fakeSettings.accent = "#FA2A55"
        fakeSettings.scale = 1.0
        fakeSettings.pocketControls = false
        fakeSettings.saverSeconds = 30
        fakeSettings.overlayToasts = false
        fakeSettings.screenOffSeconds = 300
        fakeSettings.suspendMinutes = 10
        fakeSettings.wifiOffScreenOff = false
        fakeAudio.pcmFormat = "44100 S24_LE"
        fakeAudio.graphRate = 44100
        fakeOutput.jackPresent = true
        fakeOutput.usbPresent = false
        fakeOutput.activeOutput = "headphones"
        fakeRgb.supported = true
        fakePower.verbs = []
        fakePower.switches = 0
        fakePower.driftPaths = []
        legacyPower.verbs = []
        fakeBridge.closes = 0
        counters.pair = 0
        counters.gameMode = 0
    }

    function cleanup() {
        Ui.userScale = 1.0
        Ui.accent = "#FA2A55"
    }

    function open(properties) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        pages.push(blankComponent, {})
        const page = pages.push(settingsComponent, props)
        page.completeAll()
        return page
    }

    function rowItem(page, index) {
        tryVerify(() => page.repeater.itemAt(index) !== null, 1000)
        return page.repeater.itemAt(index)
    }

    function keyIndex(page, key) {
        const i = page.indexOfKey(key)
        verify(i >= 0)
        return i
    }

    function rowOf(page, key) {
        return page.rows[tc.keyIndex(page, key)]
    }

    function itemOf(page, key) {
        return tc.rowItem(page, tc.keyIndex(page, key))
    }

    function focusKey(page, key) {
        page.focusIndex = tc.keyIndex(page, key)
        page.ensureVisible(0)
    }

    function keys(page) {
        return page.rows.map((r) => r.key)
    }

    function test_groups_and_order() {
        const page = open()
        compare(page.header.text, "settings")
        compare(JSON.stringify(tc.keys(page)), JSON.stringify(tc.expectedKeys))
        compare(page.rows[0].title, "output")
        compare(tc.rowOf(page, "label-library").title, "library")
        compare(tc.rowOf(page, "label-display").title, "display")
        compare(tc.rowOf(page, "label-power").title, "power")
        compare(page.rows[page.rows.length - 1].title, "about")
        compare(page.flick.y, Ui.px(128))
        compare(page.focusIndex, 1)
    }

    function test_row_geometry() {
        const page = open()
        const label = tc.rowItem(page, 0)
        compare(label.height, Ui.px(40))
        compare(label.titleItem.font.pixelSize, Ui.fontPx(8))
        compare(label.titleItem.font.styleName, "Regular")
        compare(label.titleItem.font.capitalization, Font.AllUppercase)
        compare(label.titleItem.x, Ui.px(20))
        fuzzyCompare(label.titleItem.y + label.titleItem.baselineOffset, Ui.px(40 - 8), 0.5)
        fuzzyCompare(label.titleItem.color.a, 0.6, 1 / 255)
        compare(label.valueItem.visible, false)
        const row = tc.rowItem(page, 1)
        compare(row.height, Ui.px(43))
        compare(row.y, Ui.px(40))
        compare(row.titleItem.font.pixelSize, Ui.fontPx(17))
        compare(row.titleItem.font.styleName, "Bold")
        compare(row.titleItem.font.capitalization, Font.AllUppercase)
        compare(row.titleItem.x, Ui.px(20))
        compare(row.titleItem.text, "output")
        compare(row.valueItem.visible, true)
        compare(row.valueItem.text, "auto")
        compare(row.valueItem.font.pixelSize, Ui.fontPx(11))
        compare(row.valueItem.font.styleName, "Light")
        compare(row.valueItem.font.capitalization, Font.AllUppercase)
        fuzzyCompare(row.valueItem.x + row.valueItem.width, page.width - Ui.px(20), 0.01)
        fuzzyCompare(row.titleItem.color.a, 1.0, 1 / 255)
        fuzzyCompare(row.valueItem.color.a, 0.85, 1 / 255)
        const pair = tc.itemOf(page, "pair")
        fuzzyCompare(pair.titleItem.color.a, 0.6, 1 / 255)
        compare(pair.valueItem.visible, false)
        compare(page.focusBar.x, Ui.px(12))
        fuzzyCompare(page.focusBar.y, Ui.px(128 + 40), 0.01)
        compare(page.focusBar.rowHeight, Ui.px(43))
    }

    function test_labels_skipped() {
        const page = open()
        compare(router.dispatch("up", false), false)
        compare(page.focusIndex, 1)
        for (let i = 0; i < 8; i++)
            verify(router.dispatch("down", false))
        compare(page.focusIndex, tc.keyIndex(page, "pair"))
        verify(router.dispatch("down", false))
        compare(page.focusIndex, tc.keyIndex(page, "folders"))
        verify(router.dispatch("down", false))
        compare(page.focusIndex, tc.keyIndex(page, "accent"))
        verify(router.dispatch("up", false))
        compare(page.focusIndex, tc.keyIndex(page, "folders"))
        for (let i = 0; i < 25; i++)
            router.dispatch("down", false)
        compare(page.focusIndex, tc.keyIndex(page, "about"))
        compare(router.dispatch("down", false), false)
        page.completeScroll()
        const top = page.rowTopOf(page.focusIndex) - page.flick.contentY
        verify(top >= 0)
        verify(top + Ui.px(43) <= page.flick.height + 0.01)
    }

    function test_output_cycles() {
        const page = open()
        verify(router.dispatch("right", false))
        compare(fakeSettings.output, "speaker")
        verify(router.dispatch("right", false))
        compare(fakeSettings.output, "headphones")
        verify(router.dispatch("right", false))
        compare(fakeSettings.output, "headphones")
        verify(router.dispatch("left", false))
        compare(fakeSettings.output, "speaker")
        compare(tc.rowItem(page, 1).valueItem.text, "speaker")
        verify(router.dispatch("a", false))
        compare(fakeSettings.output, "headphones")
        verify(router.dispatch("a", false))
        compare(fakeSettings.output, "auto")
        verify(router.dispatch("left", false))
        compare(fakeSettings.output, "auto")
    }

    function test_output_no_jack() {
        const page = open()
        fakeSettings.output = "headphones"
        compare(tc.rowOf(page, "output").value, "headphones")
        fakeOutput.jackPresent = false
        compare(tc.rowOf(page, "output").value, "no jack")
        compare(fakeSettings.output, "headphones")
        verify(router.dispatch("left", false))
        compare(fakeSettings.output, "speaker")
        compare(tc.rowOf(page, "output").value, "speaker")
        fakeSettings.output = "headphones"
        fakeOutput.jackPresent = true
        compare(tc.rowOf(page, "output").value, "headphones")
        const bare = open({ outputControl: null })
        fakeSettings.output = "headphones"
        compare(tc.rowOf(bare, "output").value, "headphones")
    }

    function test_output_usb_in_cycle() {
        const page = open()
        compare(JSON.stringify(page.outputs), JSON.stringify(["auto", "speaker", "headphones"]))
        fakeOutput.usbPresent = true
        compare(JSON.stringify(page.outputs), JSON.stringify(["auto", "speaker", "headphones", "usb"]))
        router.dispatch("right", false)
        router.dispatch("right", false)
        verify(router.dispatch("right", false))
        compare(fakeSettings.output, "usb")
        compare(tc.rowOf(page, "output").value, "usb")
        compare(tc.rowItem(page, 1).valueItem.text, "usb")
        verify(router.dispatch("right", false))
        compare(fakeSettings.output, "usb")
        verify(router.dispatch("a", false))
        compare(fakeSettings.output, "auto")
        fakeSettings.output = "headphones"
        verify(router.dispatch("a", false))
        compare(fakeSettings.output, "usb")
        fakeOutput.usbPresent = false
        compare(JSON.stringify(page.outputs), JSON.stringify(["auto", "speaker", "headphones"]))
    }

    function test_output_usb_not_present() {
        const page = open()
        fakeSettings.output = "usb"
        compare(tc.rowOf(page, "output").value, "usb (not present)")
        compare(tc.rowItem(page, 1).valueItem.text, "usb (not present)")
        compare(fakeSettings.output, "usb")
        fakeOutput.usbPresent = true
        compare(tc.rowOf(page, "output").value, "usb")
        fakeOutput.usbPresent = false
        verify(router.dispatch("right", false))
        compare(fakeSettings.output, "speaker")
        fakeSettings.output = "usb"
        verify(router.dispatch("left", false))
        compare(fakeSettings.output, "auto")
        const bare = open({ outputControl: null })
        fakeSettings.output = "usb"
        compare(tc.rowOf(bare, "output").value, "usb (not present)")
    }

    function test_headphone_rows_built_in_only() {
        const page = open()
        fakeOutput.activeOutput = "usb"
        compare(tc.rowOf(page, "hpGain").value, "built in only")
        compare(tc.rowOf(page, "hpGain").readOnly, true)
        compare(tc.rowOf(page, "hpAmp").value, "built in only")
        compare(tc.rowOf(page, "hpAmp").readOnly, true)
        compare(tc.itemOf(page, "hpGain").valueItem.text, "built in only")
        compare(tc.itemOf(page, "hpGain").valueItem.font.capitalization, Font.AllUppercase)
        tc.focusKey(page, "hpGain")
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpGain, "normal")
        tc.focusKey(page, "hpAmp")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpAmp, "lohifi")
        fakeOutput.activeOutput = "headphones"
        compare(tc.rowOf(page, "hpGain").value, "normal")
        compare(tc.rowOf(page, "hpGain").readOnly, false)
        compare(tc.rowOf(page, "hpAmp").value, "lohifi")
        tc.focusKey(page, "hpGain")
        verify(router.dispatch("right", false))
        compare(fakeSettings.hpGain, "high")
        fakeOutput.activeOutput = "speaker"
        compare(tc.rowOf(page, "hpGain").value, "high")
        compare(tc.rowOf(page, "hpGain").readOnly, false)
    }

    function test_codec_cycles() {
        const page = open()
        tc.focusKey(page, "codec")
        compare(tc.rowOf(page, "codec").title, "bluetooth codec")
        compare(tc.rowOf(page, "codec").value, "auto")
        verify(router.dispatch("right", false))
        compare(fakeSettings.btCodec, "aptx")
        verify(router.dispatch("right", false))
        compare(fakeSettings.btCodec, "aac")
        verify(router.dispatch("right", false))
        compare(fakeSettings.btCodec, "ldac")
        verify(router.dispatch("right", false))
        compare(fakeSettings.btCodec, "sbc")
        verify(router.dispatch("right", false))
        compare(fakeSettings.btCodec, "sbc")
        for (let i = 0; i < 5; i++)
            router.dispatch("left", false)
        compare(fakeSettings.btCodec, "auto")
        verify(router.dispatch("a", false))
        compare(fakeSettings.btCodec, "aptx")
        compare(tc.itemOf(page, "codec").valueItem.text, "aptx")
        compare(tc.itemOf(page, "codec").valueItem.font.capitalization, Font.AllUppercase)
    }

    function test_mixer_rows_order() {
        const page = open()
        const output = tc.keyIndex(page, "output")
        compare(JSON.stringify(tc.keys(page).slice(output, output + 6)), JSON.stringify(["output", "hpGain", "hpAmp", "spkGain", "volumeStep", "codec"]))
        compare(tc.rowOf(page, "hpGain").title, "headphone gain")
        compare(tc.rowOf(page, "hpGain").value, "normal")
        compare(tc.rowOf(page, "hpAmp").title, "headphone amp")
        compare(tc.rowOf(page, "hpAmp").value, "lohifi")
        compare(tc.rowOf(page, "spkGain").title, "speaker digital gain")
        compare(tc.rowOf(page, "spkGain").value, "+18 db")
        compare(tc.rowOf(page, "volumeStep").title, "volume step")
        compare(tc.rowOf(page, "volumeStep").value, "5%")
        compare(tc.rowOf(page, "rgb").title, "rgb")
        compare(tc.rowOf(page, "rgb").value, "armada")
        verify(tc.keyIndex(page, "rgb") > tc.keyIndex(page, "saver"))
        verify(tc.keyIndex(page, "rgb") < tc.keyIndex(page, "toasts"))
        const item = tc.itemOf(page, "spkGain")
        compare(item.valueItem.font.pixelSize, Ui.fontPx(11))
        compare(item.valueItem.font.styleName, "Light")
        compare(item.valueItem.font.capitalization, Font.AllUppercase)
        compare(item.titleItem.font.pixelSize, Ui.fontPx(17))
        compare(item.titleItem.font.styleName, "Bold")
    }

    function test_headphone_gain_cycles() {
        const page = open()
        tc.focusKey(page, "hpGain")
        verify(router.dispatch("right", false))
        compare(fakeSettings.hpGain, "high")
        verify(router.dispatch("right", false))
        compare(fakeSettings.hpGain, "high")
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpGain, "low")
        verify(router.dispatch("left", false))
        compare(fakeSettings.hpGain, "low")
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpGain, "normal")
        compare(tc.rowOf(page, "hpGain").value, "normal")
    }

    function test_headphone_amp_cycles() {
        const page = open()
        tc.focusKey(page, "hpAmp")
        verify(router.dispatch("right", false))
        compare(fakeSettings.hpAmp, "ulp")
        verify(router.dispatch("right", false))
        compare(fakeSettings.hpAmp, "hifi")
        verify(router.dispatch("right", false))
        compare(fakeSettings.hpAmp, "hifi")
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpAmp, "lohifi")
        verify(router.dispatch("left", false))
        compare(fakeSettings.hpAmp, "lohifi")
        compare(tc.rowOf(page, "hpAmp").value, "lohifi")
    }

    function test_speaker_gain_steps() {
        const page = open()
        tc.focusKey(page, "spkGain")
        compare(page.spkGainText(84), "0 db")
        compare(page.spkGainText(90), "+6 db")
        compare(page.spkGainText(108), "+24 db")
        verify(router.dispatch("right", false))
        compare(fakeSettings.spkGain, 105)
        compare(tc.rowOf(page, "spkGain").value, "+21 db")
        verify(router.dispatch("right", false))
        compare(fakeSettings.spkGain, 108)
        verify(router.dispatch("right", false))
        compare(fakeSettings.spkGain, 108)
        verify(router.dispatch("a", false))
        compare(fakeSettings.spkGain, 84)
        compare(tc.rowOf(page, "spkGain").value, "0 db")
        verify(router.dispatch("left", false))
        compare(fakeSettings.spkGain, 84)
        for (let i = 0; i < 4; i++)
            router.dispatch("right", false)
        compare(fakeSettings.spkGain, 102)
        compare(tc.rowOf(page, "spkGain").value, "+18 db")
    }

    function test_volume_step_cycles() {
        const page = open()
        tc.focusKey(page, "volumeStep")
        verify(router.dispatch("right", false))
        compare(fakeSettings.volumeStep, 10)
        compare(tc.rowOf(page, "volumeStep").value, "10%")
        verify(router.dispatch("right", false))
        compare(fakeSettings.volumeStep, 10)
        verify(router.dispatch("a", false))
        compare(fakeSettings.volumeStep, 2)
        compare(tc.rowOf(page, "volumeStep").value, "2%")
        verify(router.dispatch("left", false))
        compare(fakeSettings.volumeStep, 2)
        verify(router.dispatch("a", false))
        compare(fakeSettings.volumeStep, 5)
    }

    function test_rgb_cycles() {
        const page = open()
        tc.focusKey(page, "rgb")
        compare(tc.rowOf(page, "rgb").readOnly, false)
        verify(router.dispatch("right", false))
        compare(fakeSettings.rgb, "armada")
        verify(router.dispatch("left", false))
        compare(fakeSettings.rgb, "off")
        verify(router.dispatch("left", false))
        compare(fakeSettings.rgb, "accent")
        verify(router.dispatch("left", false))
        compare(fakeSettings.rgb, "accent")
        verify(router.dispatch("a", false))
        compare(fakeSettings.rgb, "off")
        verify(router.dispatch("a", false))
        compare(fakeSettings.rgb, "armada")
        verify(router.dispatch("a", false))
        compare(fakeSettings.rgb, "accent")
        compare(tc.rowOf(page, "rgb").value, "accent")
    }

    function test_rgb_unsupported() {
        fakeRgb.supported = false
        const page = open()
        tc.focusKey(page, "rgb")
        compare(tc.rowOf(page, "rgb").value, "unsupported")
        compare(tc.rowOf(page, "rgb").readOnly, true)
        compare(tc.itemOf(page, "rgb").valueItem.text, "unsupported")
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        verify(router.dispatch("a", false))
        compare(fakeSettings.rgb, "armada")
        fakeRgb.supported = true
        compare(tc.rowOf(page, "rgb").value, "armada")
        verify(router.dispatch("left", false))
        compare(fakeSettings.rgb, "off")
        const bare = open({ rgbControl: null })
        tc.focusKey(bare, "rgb")
        compare(tc.rowOf(bare, "rgb").value, "unsupported")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(fakeSettings.rgb, "off")
    }

    function test_audio_rows_read_only() {
        const page = open()
        compare(tc.rowOf(page, "rate").title, "sample rate follow")
        compare(tc.rowOf(page, "rate").value, "on 44100")
        compare(tc.rowOf(page, "rate").readOnly, true)
        compare(tc.rowOf(page, "replaygain").title, "replaygain")
        compare(tc.rowOf(page, "replaygain").value, "off")
        compare(tc.rowOf(page, "replaygain").readOnly, true)
        tc.focusKey(page, "rate")
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        verify(router.dispatch("a", false))
        compare(tc.rowOf(page, "rate").value, "on 44100")
        fakeAudio.graphRate = 96000
        compare(tc.rowOf(page, "rate").value, "on 96000")
        fakeAudio.graphRate = 0
        compare(tc.rowOf(page, "rate").value, "on")
        tc.focusKey(page, "replaygain")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(tc.rowOf(page, "replaygain").value, "off")
        const item = tc.itemOf(page, "replaygain")
        compare(item.valueItem.font.pixelSize, Ui.fontPx(11))
        compare(item.valueItem.font.styleName, "Light")
        compare(item.valueItem.font.capitalization, Font.AllUppercase)
    }

    function test_pair_a_phone() {
        const page = open()
        tc.focusKey(page, "pair")
        compare(tc.rowOf(page, "pair").value, "")
        verify(router.dispatch("a", false))
        compare(counters.pair, 1)
        compare(router.dispatch("right", false), false)
    }

    function test_overlay_toasts_row() {
        const page = open()
        tc.focusKey(page, "toasts")
        compare(tc.rowOf(page, "toasts").title, "overlay toasts")
        compare(tc.rowOf(page, "toasts").value, "off")
        verify(tc.keyIndex(page, "toasts") > tc.keyIndex(page, "saver"))
        verify(tc.keyIndex(page, "toasts") < tc.keyIndex(page, "label-power"))
        verify(router.dispatch("right", false))
        compare(fakeSettings.overlayToasts, true)
        compare(tc.rowOf(page, "toasts").value, "on")
        verify(router.dispatch("a", false))
        compare(fakeSettings.overlayToasts, false)
        compare(tc.rowOf(page, "toasts").value, "off")
    }

    function test_overlay_mode_rows() {
        const page = open({ overlay: true, bridge: fakeBridge })
        compare(JSON.stringify(tc.keys(page)), JSON.stringify(tc.overlayKeys))
        compare(page.indexOfKey("rgb"), -1)
        compare(tc.rowOf(page, "hpGain").title, "headphone gain")
        compare(tc.rowOf(page, "gameMode").title, "close player")
        compare(tc.rowOf(page, "pair").value, "pair in dap mode")
        compare(tc.itemOf(page, "pair").valueItem.visible, true)
        compare(tc.itemOf(page, "pair").valueItem.text, "pair in dap mode")
        tc.focusKey(page, "pair")
        verify(router.dispatch("a", false))
        compare(counters.pair, 0)
        compare(router.dispatch("right", false), false)
        tc.focusKey(page, "gameMode")
        verify(router.dispatch("a", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "close player")
        compare(page.sheet.items, ["close player"])
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(fakeBridge.closes, 0)
        router.dispatch("a", false)
        router.dispatch("a", false)
        compare(fakeBridge.closes, 1)
        compare(fakePower.verbs.length, 0)
        compare(fakePower.switches, 0)
        compare(counters.gameMode, 1)
        compare(tc.rowOf(page, "toasts").title, "overlay toasts")
        compare(tc.rowOf(page, "saver").title, "saver")
    }

    function test_library_folders() {
        const page = open()
        tc.focusKey(page, "folders")
        const folders = tc.keyIndex(page, "folders")
        compare(page.rows[folders].value, "1 on")
        verify(router.dispatch("a", false))
        compare(page.foldersOpen, true)
        compare(page.rows[folders + 1].kind, "sub")
        compare(page.rows[folders + 1].title, "/home/armada/Music")
        compare(page.rows[folders + 1].value, "on")
        compare(page.rows[folders + 2].title, "/run/media/armada/SD")
        compare(page.rows[folders + 2].value, "off")
        compare(page.rows[folders + 3].kind, "label")
        compare(page.focusIndex, folders)
        const sub = tc.rowItem(page, folders + 1)
        compare(sub.height, Ui.px(43))
        compare(sub.titleItem.font.pixelSize, Ui.fontPx(11))
        compare(sub.titleItem.font.styleName, "Light")
        compare(sub.titleItem.font.capitalization, Font.MixedCase)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.focusIndex, folders + 2)
        verify(router.dispatch("a", false))
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/home/armada/Music", "/run/media/armada/SD"]))
        compare(page.rows[folders + 2].value, "on")
        compare(page.rows[folders].value, "2 on")
        router.dispatch("up", false)
        router.dispatch("a", false)
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/run/media/armada/SD"]))
        compare(page.rows[folders + 1].value, "off")
        compare(page.rows[folders + 1].title, "/home/armada/Music")
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/run/media/armada/SD"]))
        compare(page.rows[folders + 2].value, "on")
        router.dispatch("up", false)
        router.dispatch("up", false)
        router.dispatch("a", false)
        compare(page.foldersOpen, false)
        compare(page.rows[folders + 1].kind, "label")
    }

    function test_accent_swatches() {
        const page = open()
        tc.focusKey(page, "accent")
        const row = tc.itemOf(page, "accent")
        compare(row.valueItem.visible, false)
        compare(row.swatchItem.visible, true)
        compare(row.swatchItem.children.length >= 5, true)
        let swatches = 0
        let selected = 0
        for (const c of row.swatchItem.children) {
            if (c.width === undefined || c.width !== Ui.px(12))
                continue
            swatches++
            compare(c.height, Ui.px(12))
            if (c.selected) {
                selected++
                compare(c.border.width, Ui.px(2))
                compare(String(c.color).toUpperCase(), "#FA2A55")
            }
        }
        compare(swatches, 5)
        compare(selected, 1)
        fuzzyCompare(row.swatchItem.x + row.swatchItem.width, page.width - Ui.px(20), 0.01)
        fuzzyCompare(row.swatchItem.spacing, Ui.px(4), 0.01)
        verify(router.dispatch("right", false))
        compare(fakeSettings.accent, "#F09609")
        compare(String(Ui.accent).toUpperCase(), "#F09609")
        verify(router.dispatch("left", false))
        compare(fakeSettings.accent, "#FA2A55")
        verify(router.dispatch("left", false))
        compare(fakeSettings.accent, "#FA2A55")
    }

    function test_orientation_read_only() {
        const page = open()
        tc.focusKey(page, "orientation")
        compare(tc.rowOf(page, "orientation").value, "portrait")
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        compare(tc.rowOf(page, "orientation").value, "portrait")
        tc.setWindow(1920, 1080)
        compare(tc.rowOf(page, "orientation").value, "landscape")
        compare(page.flick.y, Ui.px(98))
    }

    function test_scale_live_focus_kept() {
        const page = open()
        compare(Ui.sMax, 1.1)
        compare(JSON.stringify(page.scaleSteps), JSON.stringify([0.8, 0.9, 1.0, 1.1]))
        tc.focusKey(page, "scale")
        const scale = tc.keyIndex(page, "scale")
        compare(page.rows[scale].value, "1.0")
        verify(router.dispatch("right", false))
        compare(Ui.userScale, 1.1)
        compare(fakeSettings.scale, 1.1)
        compare(page.focusIndex, scale)
        compare(page.rows[scale].value, "1.1")
        compare(tc.rowItem(page, scale).height, Ui.px(43))
        fuzzyCompare(Ui.u, 2 * 1.1, 0.001)
        const top = page.rowTopOf(scale) - page.flick.contentY
        verify(top >= 0)
        verify(top + Ui.px(43) <= page.flick.height + 0.01)
        verify(router.dispatch("right", false))
        compare(Ui.userScale, 1.1)
        router.dispatch("left", false)
        router.dispatch("left", false)
        router.dispatch("left", false)
        compare(Ui.userScale, 0.8)
        compare(router.dispatch("left", false), true)
        compare(Ui.userScale, 0.8)
        compare(page.focusIndex, scale)
    }

    function test_saver_steps() {
        const page = open()
        tc.focusKey(page, "saver")
        compare(tc.rowOf(page, "saver").value, "30 s")
        router.dispatch("right", false)
        compare(fakeSettings.saverSeconds, 60)
        compare(tc.rowOf(page, "saver").value, "1 min")
        for (let i = 0; i < 5; i++)
            router.dispatch("right", false)
        compare(fakeSettings.saverSeconds, 300)
        compare(tc.rowOf(page, "saver").value, "5 min")
        for (let i = 0; i < 9; i++)
            router.dispatch("left", false)
        compare(fakeSettings.saverSeconds, 10)
        compare(tc.rowOf(page, "saver").value, "10 s")
    }

    function test_power_rows() {
        const page = open()
        tc.focusKey(page, "screenOff")
        compare(tc.rowOf(page, "screenOff").value, "5 min")
        router.dispatch("right", false)
        compare(fakeSettings.screenOffSeconds, 600)
        router.dispatch("right", false)
        compare(fakeSettings.screenOffSeconds, 1800)
        compare(tc.rowOf(page, "screenOff").value, "30 min")
        for (let i = 0; i < 6; i++)
            router.dispatch("left", false)
        compare(fakeSettings.screenOffSeconds, 0)
        compare(tc.rowOf(page, "screenOff").value, "off")
        router.dispatch("right", false)
        compare(fakeSettings.screenOffSeconds, 60)
        compare(tc.rowOf(page, "screenOff").value, "1 min")
        tc.focusKey(page, "suspend")
        compare(tc.rowOf(page, "suspend").value, "10 min")
        router.dispatch("right", false)
        compare(fakeSettings.suspendMinutes, 30)
        router.dispatch("right", false)
        compare(fakeSettings.suspendMinutes, 60)
        for (let i = 0; i < 5; i++)
            router.dispatch("left", false)
        compare(fakeSettings.suspendMinutes, 0)
        compare(tc.rowOf(page, "suspend").value, "off")
        tc.focusKey(page, "pocket")
        compare(tc.rowOf(page, "pocket").value, "off")
        router.dispatch("right", false)
        compare(fakeSettings.pocketControls, true)
        compare(tc.rowOf(page, "pocket").value, "on")
        router.dispatch("a", false)
        compare(fakeSettings.pocketControls, false)
        tc.focusKey(page, "wifi")
        compare(tc.rowOf(page, "wifi").value, "keep")
        router.dispatch("left", false)
        compare(fakeSettings.wifiOffScreenOff, true)
        compare(tc.rowOf(page, "wifi").value, "off")
    }

    function test_game_mode_confirm() {
        const page = open()
        tc.focusKey(page, "gameMode")
        compare(tc.rowOf(page, "gameMode").title, "game mode")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "game mode")
        compare(page.sheet.items.length, 1)
        compare(page.sheet.duration, 380)
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(fakePower.verbs.length, 0)
        compare(pages.depth, 2)
        router.dispatch("a", false)
        router.dispatch("a", false)
        compare(page.sheet.shown, false)
        compare(fakePower.verbs.length, 0)
        compare(fakePower.switches, 1)
        compare(fakeBridge.closes, 0)
        compare(counters.gameMode, 1)
    }

    function test_game_mode_falls_back_to_verb() {
        const page = open({ power: legacyPower })
        tc.focusKey(page, "gameMode")
        router.dispatch("a", false)
        router.dispatch("a", false)
        compare(JSON.stringify(legacyPower.verbs), JSON.stringify(["game-mode"]))
    }

    function test_about_rows() {
        fakePower.driftPaths = ["/usr/libexec/armada/session-control"]
        const page = open()
        tc.focusKey(page, "about")
        const about = tc.keyIndex(page, "about")
        compare(page.rows[about].value, Env.version)
        compare(page.rows.length, tc.expectedKeys.length)
        verify(router.dispatch("a", false))
        compare(page.aboutOpen, true)
        const list = tc.keys(page)
        verify(list.indexOf("drift") > about)
        verify(list.indexOf("track") > about)
        verify(list.indexOf("pcm") > about)
        compare(tc.rowOf(page, "drift").value, "1 file")
        compare(tc.rowOf(page, "track").value, "44100 hz 16 bit")
        compare(tc.rowOf(page, "pcm").title, "output")
        compare(tc.rowOf(page, "pcm").value, "44100 S24_LE")
        const track = tc.itemOf(page, "track")
        compare(track.titleItem.font.pixelSize, Ui.fontPx(11))
        compare(track.titleItem.font.styleName, "Light")
        compare(track.titleItem.font.capitalization, Font.AllUppercase)
        compare(track.valueItem.font.capitalization, Font.AllUppercase)
        router.dispatch("down", false)
        compare(router.dispatch("right", false), false)
        fakePlayer.current = ({ id: 4, title: "x", sampleRate: 44100, bitDepth: 0 })
        compare(tc.rowOf(page, "track").value, "44100 hz")
        fakeAudio.pcmFormat = ""
        compare(page.indexOfKey("pcm"), -1)
        fakePower.driftPaths = []
        compare(page.indexOfKey("drift"), -1)
        tc.focusKey(page, "about")
        router.dispatch("a", false)
        compare(page.rows.length, tc.expectedKeys.length)
    }

    function test_value_change_f7() {
        const page = open({ animated: true })
        const row = tc.rowItem(page, 1)
        compare(row.shownValue, "auto")
        router.dispatch("right", false)
        const next = tc.rowItem(page, 1)
        compare(next.valueSwapping, true)
        compare(next.shownValue, "auto")
        tryCompare(next, "shownValue", "speaker", 1000)
        verify(next.valueItem.opacity < 1)
        tryCompare(next, "valueSwapping", false, 1000)
        compare(next.valueItem.opacity, 1)
        compare(Motion.valueChange, 200)
        compare(Motion.valueDip, 0.2)
    }

    function test_value_change_settled_without_animation() {
        const page = open()
        router.dispatch("right", false)
        const row = tc.rowItem(page, 1)
        compare(row.valueSwapping, false)
        compare(row.shownValue, "speaker")
        compare(row.valueItem.opacity, 1)
    }

    function test_page_lt_rt() {
        const page = open()
        verify(router.dispatch("rt", false))
        verify(page.focusIndex > 1)
        page.completeScroll()
        const top = page.rowTopOf(page.focusIndex) - page.flick.contentY
        verify(top >= 0)
        verify(top + Ui.px(43) <= page.flick.height + 0.01)
        verify(router.dispatch("lt", false))
        compare(page.focusIndex, 1)
        compare(router.dispatch("lt", false), false)
    }

    function test_b_pops() {
        const page = open()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_screen_off_settles() {
        const page = open({ animated: true })
        tc.focusKey(page, "gameMode")
        router.dispatch("a", false)
        verify(page.sheet.animating)
        page.screenOff = true
        compare(page.sheet.animating, false)
        compare(page.sheet.visible, false)
        compare(page.sheet.shown, true)
    }

    function test_t2a_first_show() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(settingsComponent, { animated: true })
        compare(page.arriving, true)
        const first = tc.rowItem(page, 0)
        const output = tc.rowItem(page, 1)
        compare(first.enter, 0)
        compare(output.enter, 0)
        compare(output.shift, Motion.pivotShift)
        fuzzyCompare(output.x, Ui.px(34), 0.01)
        compare(output.opacity, 0)
        tryCompare(pages, "phase", "idle", 3000)
        compare(page.arriving, false)
        compare(output.enterDuration, 380)
        compare(first.stagger, 0)
        compare(output.stagger, 22)
        compare(tc.rowItem(page, 3).stagger, 3 * 22)
        tryCompare(output, "enter", 1, 2000)
        tryCompare(tc.rowItem(page, 3), "enter", 1, 2000)
        fuzzyCompare(output.x, 0, 0.01)
        compare(output.opacity, 1)
    }

    function test_t2a_rest_without_animation() {
        const page = open({ animated: true })
        compare(page.arriving, false)
        const output = tc.rowItem(page, 1)
        compare(output.enter, 1)
        compare(output.x, 0)
        compare(output.opacity, 1)
    }

    function test_f12_on_folders_and_about() {
        const page = open({ animated: true })
        tc.focusKey(page, "folders")
        router.dispatch("a", false)
        compare(page.foldersOpen, true)
        const sub = tc.rowItem(page, tc.keyIndex(page, "folders") + 1)
        compare(sub.sub, true)
        tryVerify(function() { return sub.enter < 1 }, 1000)
        compare(sub.enterDuration, 220)
        compare(sub.shift, Motion.listChangeShift)
        compare(sub.stagger, 0)
        compare(tc.rowItem(page, tc.keyIndex(page, "folders") + 2).stagger, 50)
        compare(tc.rowItem(page, tc.keyIndex(page, "folders")).enter, 1)
        tryCompare(sub, "enter", 1, 2000)
        tc.focusKey(page, "about")
        router.dispatch("a", false)
        compare(page.aboutOpen, true)
        const info = tc.rowItem(page, tc.keyIndex(page, "about") + 1)
        compare(info.info, true)
        tryVerify(function() { return info.enter < 1 }, 1000)
        compare(info.enterDuration, 220)
        tryCompare(info, "enter", 1, 2000)
    }
}
