import QtQuick
import QtTest
import MicroDap

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

    TouchLayer {
        id: layer

        width: 640
        height: 960
        router: router
    }

    QtObject {
        id: fakeSettings

        property string output: "auto"
        property string hpGain: "normal"
        property string hpAmp: "lohifi"
        property int spkGain: 102
        property int volumeStep: 5
        property string rgb: "system"
        property string btCodec: "auto"
        property var roots: ["/home/micro/Music"]
        property var defaultList: []
        property int accentHue: 350
        property bool accentFromArt: false
        property real scale: 1.0
        property int saverSeconds: 30
        property bool overlayToasts: false
        property int screenOffSeconds: 300
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
        property string orientation: "portrait"
        property bool autoRotate: false
        property bool touch: true
        property string outputLevel: "off"
        property string sampleRate: "auto"
        property string bootSession: "steam"
        property var sessionChoices: ["microdap", "microemu", "steam"]
        property bool available: true

        function bootSessions() {
            return fakeSettings.sessionChoices
        }

        function defaultRoots() {
            return fakeSettings.defaultList
        }
    }

    QtObject {
        id: fakeSensor

        property bool supported: true
        property int turn: -1
        property string raw: ""
        property string settledRaw: ""
        property var wanted: []

        function setWanted(on) {
            fakeSensor.wanted = fakeSensor.wanted.concat([on])
        }
    }

    QtObject {
        id: fakeOutputs

        property bool internal: true
    }

    QtObject {
        id: fakeLibrary

        property bool scanning: false
        property int songCount: 9
        property int rescans: 0

        function rescanRoots() {
            fakeLibrary.rescans++
        }
    }

    QtObject {
        id: fakePower

        property var verbs: []
        property int switches: 0
        property string imageVersion: "testing-38784f1"
        property string updateStatus: "none"
        property string updateVersion: ""
        property int updateReloads: 0

        function reloadUpdate() {
            fakePower.updateReloads++
        }

        function runVerb(verb) {
            fakePower.verbs = fakePower.verbs.concat([verb])
            return true
        }

        function closeAndSwitch() {
            fakePower.switches++
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
        property int hwRate: 44100
        property int refreshes: 0

        function refresh() {
            fakeAudio.refreshes++
        }
    }

    QtObject {
        id: fakeMode

        property string levelStatus: "off"
        property bool effective: false
        property var rates: [44100, 48000, 88200, 96000, 176400, 192000]
        property bool rateReadOnly: false
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
        property int closePlayer: 0
        property int eq: 0
    }

    QtObject {
        id: fakeDacEq

        property string status: "none"
    }

    QtObject {
        id: fakeEq

        property string activeOutput: "headphones"
        property var keys: ({ headphones: "/home/micro/.config/microdap/eq/HD 600.txt", usb: "custom", speaker: "off" })
        property bool missing: false

        signal profilesChanged()

        function profileMap(output) {
            const key = fakeEq.keys[output]
            return { key: key, name: key === "custom" || key === "off" ? "" : "HD 600", missing: fakeEq.missing, preamp: 0, bands: [] }
        }
    }

    Component {
        id: settingsComponent

        SettingsScreen {
            settings: fakeSettings
            power: fakePower
            player: fakePlayer
            audioInfo: fakeAudio
            outputControl: fakeOutput
            outputMode: fakeMode
            rgbControl: fakeRgb
            folders: ["/home/micro/Music", "/run/media/micro/SD"]
            onPairPhone: counters.pair++
            onOpenEq: counters.eq++
            onClosePlayerRequested: counters.closePlayer++
        }
    }

    Component {
        id: mainComponent

        Main {
            settings: fakeSettings
            outputs: fakeOutputs
            shotWidth: 640
            shotHeight: 960
            visibility: Window.Windowed
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    Component {
        id: blankComponent

        Page {}
    }

    readonly property var expectedPivots: ["output", "library", "display", "power", "system"]
    readonly property var expectedKeys: [
        ["output", "hpGain", "hpAmp", "spkGain", "volumeStep", "codec", "level", "rate", "replaygain", "eq", "pair"],
        ["folders", "rescan"],
        ["accent", "accentArt", "orientation", "autoRotate", "touch", "scale", "saver", "rgb", "toasts"],
        ["screenOff", "suspend", "wifi"],
        ["boot", "about"]
    ]
    readonly property var overlayKeys: [
        ["output", "hpGain", "hpAmp", "spkGain", "volumeStep", "codec", "level", "rate", "replaygain", "eq", "pair"],
        ["folders", "rescan"],
        ["accent", "accentArt", "scale", "saver", "toasts"],
        ["screenOff", "suspend", "wifi"],
        ["boot", "closePlayer", "about"]
    ]

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.setSurface(w, h)
        Ui.turn = 0
        Ui.orientationActive = h > w
        pages.width = w
        pages.height = h
        router.width = w
        router.height = h
        layer.width = w
        layer.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        fakeMode.rates = [44100, 48000, 88200, 96000, 176400, 192000]
        pages.clear()
        pages.animate = false
        fakeSettings.output = "auto"
        fakeSettings.hpGain = "normal"
        fakeSettings.hpAmp = "lohifi"
        fakeSettings.spkGain = 102
        fakeSettings.volumeStep = 5
        fakeSettings.rgb = "system"
        fakeSettings.btCodec = "auto"
        fakeSettings.roots = ["/home/micro/Music"]
        fakeSettings.defaultList = []
        fakeSettings.accentHue = 350
        fakeSettings.accentFromArt = false
        fakeSettings.scale = 1.0
        fakeSettings.saverSeconds = 30
        fakeSettings.overlayToasts = false
        fakeSettings.screenOffSeconds = 300
        fakeSettings.suspendMinutes = 10
        fakeSettings.wifiOffScreenOff = false
        fakeSettings.orientation = "portrait"
        fakeSettings.autoRotate = false
        fakeSettings.touch = true
        fakeSettings.outputLevel = "off"
        fakeSettings.sampleRate = "auto"
        fakeSensor.supported = true
        fakeSensor.turn = -1
        fakeSensor.raw = ""
        fakeSensor.settledRaw = ""
        fakeSensor.wanted = []
        fakeOutputs.internal = true
        fakeLibrary.scanning = false
        fakeLibrary.songCount = 9
        fakeLibrary.rescans = 0
        fakeSettings.bootSession = "steam"
        fakeSettings.sessionChoices = ["microdap", "microemu", "steam"]
        fakeSettings.available = true
        fakeAudio.pcmFormat = "44100 S24_LE"
        fakeAudio.hwRate = 44100
        fakeAudio.refreshes = 0
        fakeMode.levelStatus = "off"
        fakeMode.effective = false
        fakeMode.rates = [44100, 48000, 88200, 96000, 176400, 192000]
        fakeMode.rateReadOnly = false
        fakeOutput.jackPresent = true
        fakeOutput.usbPresent = false
        fakeOutput.activeOutput = "headphones"
        fakeRgb.supported = true
        fakePower.verbs = []
        fakePower.switches = 0
        fakePower.imageVersion = "testing-38784f1"
        fakePower.updateStatus = "none"
        fakePower.updateVersion = ""
        fakePower.updateReloads = 0
        fakeBridge.closes = 0
        counters.pair = 0
        counters.closePlayer = 0
        counters.eq = 0
        fakeEq.activeOutput = "headphones"
        fakeEq.keys = ({ headphones: "/home/micro/.config/microdap/eq/HD 600.txt", usb: "custom", speaker: "off" })
        fakeEq.missing = false
        fakeDacEq.status = "none"
    }

    function cleanup() {
        Ui.userScale = 1.0
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
        const pivot = page.pivotOfKey(key)
        if (pivot >= 0 && pivot !== page.pivotIndex) {
            page.switchPivot(pivot - page.pivotIndex)
            page.landPivot()
        }
        const i = page.indexOfKey(key)
        verify(i >= 0)
        return i
    }

    function absent(page, key) {
        for (const list of tc.allKeys(page))
            verify(list.indexOf(key) < 0, key)
    }

    function allKeys(page) {
        const start = page.pivotIndex
        const out = []
        for (let p = 0; p < page.pivots.length; p++) {
            page.switchPivot(p - page.pivotIndex)
            page.landPivot()
            out.push(tc.keys(page))
        }
        page.switchPivot(start - page.pivotIndex)
        page.landPivot()
        return out
    }

    function rowOf(page, key) {
        const i = tc.keyIndex(page, key)
        return page.rows[i]
    }

    function itemOf(page, key) {
        const i = tc.keyIndex(page, key)
        return tc.rowItem(page, i)
    }

    function focusKey(page, key) {
        page.focusIndex = tc.keyIndex(page, key)
        page.ensureVisible(0)
    }

    function edit(page, key) {
        tc.focusKey(page, key)
        verify(router.dispatch("a", false))
        compare(page.editing, true)
        compare(page.editKey, key)
    }

    function confirm(page) {
        verify(router.dispatch("a", false))
        compare(page.editing, false)
    }

    function sheetOpen(page, key) {
        const title = tc.rowOf(page, key).title
        tc.focusKey(page, key)
        verify(router.dispatch("a", false))
        compare(page.valueOpen, true)
        compare(page.editing, false)
        compare(page.sheet.shown, true)
        compare(page.sheet.title, title)
        page.sheet.complete()
    }

    function items(page) {
        return JSON.stringify(page.sheet.items)
    }

    function focused(page) {
        return page.sheet.items[page.sheet.focusIndex]
    }

    function sheetItem(page, index) {
        for (const c of page.sheet.list.children) {
            if (c.touchIndex === index)
                return c
        }
        return null
    }

    function tapItem(page, index) {
        const item = tc.sheetItem(page, index)
        verify(item !== null)
        tc.tapAt(item, item.width / 2, item.height / 2)
    }

    function keys(page) {
        return page.rows.map((r) => r.key)
    }

    function test_groups_and_order() {
        const page = open()
        compare(page.header.text, "settings")
        compare(JSON.stringify(page.pivots), JSON.stringify(tc.expectedPivots))
        compare(JSON.stringify(page.strip.labels), JSON.stringify(tc.expectedPivots))
        compare(page.pivotIndex, 0)
        compare(JSON.stringify(tc.allKeys(page)), JSON.stringify(tc.expectedKeys))
        compare(page.rows[0].title, "output")
        for (const r of page.rows)
            verify(r.kind !== "label")
        page.switchPivot(4)
        compare(page.rows[page.rows.length - 1].title, "about")
        page.switchPivot(-4)
        compare(page.flick.y, Ui.px(128))
        compare(page.focusIndex, 0)
        compare(page.editing, false)
    }

    function test_pivots_switch_and_keep_focus() {
        const page = open()
        compare(router.dispatch("lb", false), true)
        compare(page.pivotIndex, 0)
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(page.focusIndex, 2)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 1)
        compare(page.focusIndex, 0)
        compare(page.focusedRow.key, "folders")
        verify(router.dispatch("down", false))
        compare(router.dispatch("down", false), false)
        compare(page.focusedRow.key, "rescan")
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 0)
        compare(page.focusedRow.key, "hpAmp")
        for (let i = 0; i < 4; i++)
            router.dispatch("rb", false)
        compare(page.pivotIndex, 4)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 4)
        compare(page.focusedRow.key, "boot")
    }

    function test_pivot_animates_t5() {
        const page = open({ animated: true })
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 0)
        verify(page.switching)
        compare(page.focusBar.visible, false)
        tryCompare(page, "shownPivot", 1, 1000)
        compare(page.focusedRow.key, "folders")
        tryCompare(page, "switching", false, 2000)
        compare(page.flick.x, 0)
        compare(page.flick.opacity, 1)
        compare(page.focusBar.visible, true)
    }

    function test_pivot_label_tap_switches() {
        const page = open()
        const label = page.strip.labelItem(1)
        tc.tapAt(label, label.width / 2, label.height / 2)
        compare(page.pivotIndex, 1)
        compare(page.focusedRow.key, "folders")
        compare(page.editing, false)
        compare(pages.depth, 2)
    }

    function test_pivot_label_tap_reverts_edit() {
        const page = open()
        tc.edit(page, "accent")
        verify(router.dispatch("right", false))
        compare(fakeSettings.accentHue, 0)
        const label = page.strip.labelItem(3)
        tc.tapAt(label, label.width / 2, label.height / 2)
        compare(page.editing, false)
        compare(fakeSettings.accentHue, 350)
        compare(page.pivotIndex, 3)
    }

    function test_pivot_label_tap_cancels_value_sheet() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        verify(router.dispatch("down", false))
        compare(fakeSettings.volumeStep, 2)
        const label = page.strip.labelItem(2)
        tc.tapAt(label, label.width / 2, label.height / 2)
        compare(page.valueOpen, false)
        compare(fakeSettings.volumeStep, 5)
        compare(page.pivotIndex, 2)
    }

    function test_pivot_label_tap_closes_sheet() {
        const page = open({ overlay: true, bridge: fakeBridge })
        tc.focusKey(page, "closePlayer")
        verify(router.dispatch("a", false))
        compare(page.sheet.shown, true)
        const label = page.strip.labelItem(4)
        tc.tapAt(label, label.width / 2, label.height / 2)
        compare(page.sheet.shown, false)
        compare(page.pivotIndex, 4)
        compare(fakeBridge.closes, 0)
    }

    function test_focus_key_on_another_pivot_closes_the_sheet() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        verify(router.dispatch("down", false))
        compare(fakeSettings.volumeStep, 2)
        verify(page.focusKey("screenOff"))
        compare(page.valueOpen, false)
        compare(page.sheet.shown, false)
        compare(fakeSettings.volumeStep, 5)
    }

    function test_pivot_hits_stop_at_the_sheet_data() {
        return [
            { tag: "rate portrait", w: 640, h: 960, key: "rate", value: "384000" },
            { tag: "saver landscape", w: 960, h: 640, key: "saver", value: "300" }
        ]
    }

    function test_pivot_hits_stop_at_the_sheet(data) {
        tc.setWindow(data.w, data.h)
        fakeMode.rates = [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000]
        const page = open()
        verify(page.focusKey(data.key))
        verify(router.dispatch("a", false))
        compare(page.valueOpen, true)
        page.sheet.complete()
        const pivot = page.pivotIndex
        verify(page.sheet.height - page.sheet.panel.height < page.strip.y + page.strip.height)
        compare(page.sheet.firstRow, 0)
        page.sheet.list.forceLayout()
        tc.tapItem(page, 0)
        compare(page.valueOpen, false)
        compare(page.pivotIndex, pivot)
        compare(String(data.key === "rate" ? fakeSettings.sampleRate : fakeSettings.saverSeconds), data.value)
    }

    function test_open_from_home_lands_on_output() {
        const m = tc.openInMain()
        m.page.switchPivot(3)
        compare(m.page.pivotIndex, 3)
        m.main.pageStack.pop()
        m.main.navigate("settings")
        const page = m.main.pageStack.currentItem
        compare(page.objectName, "settings")
        page.completeAll()
        compare(page.pivotIndex, 0)
        compare(page.focusedRow.key, "output")
    }

    function test_dap_rows_removed() {
        const page = open()
        tc.absent(page, "keepPhone")
        tc.absent(page, "gameMode")
        tc.absent(page, "closePlayer")
        compare(tc.keyIndex(page, "about"), tc.keyIndex(page, "boot") + 1)
        for (const r of page.rows) {
            verify(r.title !== "keep phone in game mode")
            verify(r.title !== "game mode")
            verify(r.title !== "close player")
        }
        const overlayPage = open({ overlay: true, bridge: fakeBridge })
        verify(tc.allKeys(overlayPage).every((list) => list.indexOf("keepPhone") < 0 && list.indexOf("gameMode") < 0))
        compare(tc.keyIndex(overlayPage, "closePlayer"), tc.keyIndex(overlayPage, "boot") + 1)
    }

    function test_row_geometry() {
        const page = open()
        const row = tc.rowItem(page, 0)
        compare(row.height, Ui.px(43))
        compare(row.y, 0)
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
        fuzzyCompare(page.focusBar.y, Ui.px(128), 0.01)
        compare(page.focusBar.rowHeight, Ui.px(43))
        compare(page.strip.y, 0)
        compare(page.strip.labelItem(0).x + page.strip.labelItem(0).parent.x, Ui.px(20))
    }

    function test_rows_stop_at_pivot_ends() {
        const page = open()
        compare(router.dispatch("up", false), false)
        compare(page.focusIndex, 0)
        for (let i = 0; i < 10; i++)
            verify(router.dispatch("down", false))
        compare(page.focusIndex, tc.keyIndex(page, "pair"))
        compare(router.dispatch("down", false), false)
        compare(page.pivotIndex, 0)
        page.completeScroll()
        const top = page.rowTopOf(page.focusIndex) - page.flick.contentY
        verify(top >= 0)
        verify(top + Ui.px(43) <= page.flick.height + 0.01)
    }

    function test_value_sheet_open_move_confirm() {
        const page = open()
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        compare(page.valueOpen, false)
        verify(router.dispatch("a", false))
        compare(page.valueOpen, true)
        compare(page.editing, false)
        compare(page.sheet.title, "output")
        compare(tc.items(page), JSON.stringify(["auto", "speaker", "headphones"]))
        compare(page.sheet.mark, 0)
        compare(page.sheet.focusIndex, 0)
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "speaker")
        compare(fakeSettings.output, "auto")
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "auto")
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "headphones")
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "speaker")
        compare(page.sheet.mark, 0)
        compare(fakeSettings.output, "auto")
        verify(router.dispatch("a", false))
        compare(page.valueOpen, false)
        compare(page.sheet.shown, false)
        compare(fakeSettings.output, "speaker")
        compare(tc.rowItem(page, 0).valueItem.text, "speaker")
        compare(router.dispatch("left", false), false)
        compare(fakeSettings.output, "speaker")
    }

    function test_value_sheet_hold_applies_nothing() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        verify(router.dispatch("down", true))
        compare(tc.focused(page), "2%")
        compare(fakeSettings.volumeStep, 5)
        verify(router.dispatch("down", false))
        compare(fakeSettings.volumeStep, 10)
    }

    function test_value_sheet_rise_keeps_turn_and_back() {
        const page = open({ animated: true })
        tc.focusKey(page, "volumeStep")
        verify(router.dispatch("a", false))
        compare(page.sheet.animating, true)
        verify(router.dispatch("a", false))
        compare(page.valueOpen, true)
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(fakeSettings.volumeStep, 5)
        page.sheet.complete()
        compare(fakeSettings.volumeStep, 2)
        compare(tc.focused(page), "2%")
        verify(router.dispatch("b", false))
        page.sheet.complete()
        compare(fakeSettings.volumeStep, 5)
        verify(router.dispatch("a", false))
        compare(page.sheet.animating, true)
        verify(router.dispatch("down", false))
        verify(router.dispatch("b", false))
        page.sheet.complete()
        compare(page.valueOpen, false)
        compare(fakeSettings.volumeStep, 5)
    }

    function test_value_sheet_back_restores() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        compare(tc.items(page), JSON.stringify(["10%", "5%", "2%"]))
        compare(tc.focused(page), "5%")
        verify(router.dispatch("down", false))
        compare(fakeSettings.volumeStep, 2)
        compare(tc.rowOf(page, "volumeStep").value, "2%")
        verify(router.dispatch("b", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.volumeStep, 5)
        compare(tc.rowOf(page, "volumeStep").value, "5%")
        compare(pages.depth, 2)
        tc.sheetOpen(page, "spkGain")
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(tc.focused(page), "+12 db")
        compare(fakeSettings.spkGain, 102)
        verify(router.dispatch("b", false))
        compare(fakeSettings.spkGain, 102)
        tc.sheetOpen(page, "saver")
        router.dispatch("up", false)
        compare(fakeSettings.saverSeconds, 60)
        tc.tapAt(page.sheet.scrim, page.width / 2, Ui.px(4))
        compare(page.valueOpen, false)
        compare(fakeSettings.saverSeconds, 30)
        tc.sheetOpen(page, "suspend")
        router.dispatch("b", false)
        compare(fakeSettings.suspendMinutes, 10)
        compare(pages.depth, 2)
    }

    function test_edit_b_does_not_pop() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        verify(router.dispatch("b", false))
        compare(pages.depth, 2)
        compare(page.valueOpen, false)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_value_sheet_keys_ignored() {
        const page = open()
        tc.sheetOpen(page, "codec")
        const at = page.focusIndex
        const contentY = page.flick.contentY
        for (const name of ["left", "right", "lt", "rt", "x", "y", "lb", "rb"]) {
            verify(router.dispatch(name, false))
            compare(page.focusIndex, at)
            compare(page.sheet.focusIndex, 0)
            compare(page.valueOpen, true)
        }
        compare(page.pivotIndex, 0)
        compare(page.flick.contentY, contentY)
        compare(fakeSettings.btCodec, "auto")
        verify(router.dispatch("a", true))
        compare(page.valueOpen, true)
        verify(router.dispatch("b", true))
        compare(page.valueOpen, true)
        compare(pages.depth, 2)
        verify(router.dispatch("a", false))
        compare(page.valueOpen, false)
    }

    function test_value_sheet_mark_in_accent() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        verify(Qt.colorEqual(tc.sheetItem(page, 1).children[0].color, Ui.accent))
        fuzzyCompare(tc.sheetItem(page, 0).children[0].color.a, Ui.dialogItem.alphaDim, 1 / 255)
        verify(router.dispatch("down", false))
        const moved = tc.sheetItem(page, 1).children[0].color
        verify(Qt.colorEqual(Qt.rgba(moved.r, moved.g, moved.b, 1), Ui.accent))
        fuzzyCompare(moved.a, Ui.dialogItem.alphaDim, 1 / 255)
        verify(Qt.colorEqual(tc.sheetItem(page, 2).children[0].color, Ui.white(Ui.dialogItem.alpha)))
        const row = tc.itemOf(page, "volumeStep")
        verify(!Qt.colorEqual(row.valueItem.color, Ui.accent))
        router.dispatch("b", false)
    }

    function test_value_sheet_screen_off_and_leave() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        router.dispatch("down", false)
        page.screenOff = true
        compare(page.valueOpen, false)
        compare(fakeSettings.volumeStep, 2)
        page.screenOff = false
        tc.sheetOpen(page, "output")
        router.dispatch("down", false)
        page.screenOff = true
        compare(page.valueOpen, false)
        compare(fakeSettings.output, "auto")
        page.screenOff = false
        tc.sheetOpen(page, "volumeStep")
        router.dispatch("up", false)
        compare(fakeSettings.volumeStep, 5)
        pages.push(blankComponent, {})
        compare(page.valueOpen, false)
        compare(fakeSettings.volumeStep, 5)
    }

    function test_value_sheet_outside_change_closes() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        router.dispatch("up", false)
        compare(fakeSettings.volumeStep, 10)
        fakeSettings.volumeStep = 2
        compare(page.valueOpen, false)
        compare(fakeSettings.volumeStep, 2)
        tc.sheetOpen(page, "codec")
        router.dispatch("down", false)
        fakeSettings.btCodec = "sbc"
        compare(page.valueOpen, false)
        compare(fakeSettings.btCodec, "sbc")
        tc.edit(page, "accent")
        router.dispatch("right", false)
        compare(fakeSettings.accentHue, 0)
        fakeSettings.accentHue = 120
        compare(page.editing, false)
        compare(fakeSettings.accentHue, 120)
    }

    function test_value_sheet_drafts_apply_only_on_a() {
        const page = open()
        for (const key of ["output", "hpGain", "hpAmp", "spkGain", "codec", "level", "rate", "orientation", "scale", "boot"]) {
            verify(tc.rowOf(page, key).spec.draft, key)
            const before = String(page.currentOf(key))
            tc.sheetOpen(page, key)
            for (let i = 0; i < page.sheet.items.length; i++) {
                router.dispatch("down", false)
                compare(String(page.currentOf(key)), before, key)
            }
            verify(router.dispatch("b", false))
            compare(String(page.currentOf(key)), before, key)
        }
    }

    function test_act_rows_keep_a() {
        const page = open({ eq: fakeEq, library: fakeLibrary })
        tc.focusKey(page, "eq")
        verify(router.dispatch("a", false))
        compare(counters.eq, 1)
        compare(page.editing, false)
        tc.focusKey(page, "pair")
        verify(router.dispatch("a", false))
        compare(counters.pair, 1)
        compare(page.editing, false)
        tc.focusKey(page, "rescan")
        verify(router.dispatch("a", false))
        compare(fakeLibrary.rescans, 1)
        compare(page.editing, false)
        tc.focusKey(page, "folders")
        verify(router.dispatch("a", false))
        compare(page.foldersOpen, true)
        compare(page.editing, false)
        router.dispatch("a", false)
        compare(page.foldersOpen, false)
        tc.focusKey(page, "about")
        verify(router.dispatch("a", false))
        compare(page.aboutOpen, true)
        compare(page.editing, false)
        for (const key of ["replaygain"]) {
            tc.focusKey(page, key)
            verify(router.dispatch("a", false))
            compare(page.editing, false)
        }
    }

    function test_output_no_jack() {
        const page = open()
        fakeSettings.output = "headphones"
        compare(tc.rowOf(page, "output").value, "headphones")
        fakeOutput.jackPresent = false
        compare(tc.rowOf(page, "output").value, "no jack")
        compare(fakeSettings.output, "headphones")
        tc.sheetOpen(page, "output")
        compare(tc.items(page), JSON.stringify(["auto", "speaker", "no jack"]))
        compare(page.sheet.mark, 2)
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.output, "speaker")
        compare(tc.rowOf(page, "output").value, "speaker")
        fakeSettings.output = "headphones"
        fakeOutput.jackPresent = true
        compare(tc.rowOf(page, "output").value, "headphones")
        tc.sheetOpen(page, "output")
        fakeOutput.jackPresent = false
        compare(page.valueOpen, false)
        compare(fakeSettings.output, "headphones")
        fakeOutput.jackPresent = true
        const bare = open({ outputControl: null })
        fakeSettings.output = "headphones"
        compare(tc.rowOf(bare, "output").value, "headphones")
    }

    function test_output_usb_in_cycle() {
        const page = open()
        compare(JSON.stringify(page.outputs), JSON.stringify(["auto", "speaker", "headphones"]))
        fakeOutput.usbPresent = true
        compare(JSON.stringify(page.outputs), JSON.stringify(["auto", "speaker", "headphones", "usb"]))
        tc.sheetOpen(page, "output")
        compare(tc.items(page), JSON.stringify(["auto", "speaker", "headphones", "usb"]))
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "usb")
        compare(fakeSettings.output, "auto")
        verify(router.dispatch("a", false))
        compare(fakeSettings.output, "usb")
        compare(tc.rowOf(page, "output").value, "usb")
        compare(tc.rowItem(page, 0).valueItem.text, "usb")
        fakeOutput.usbPresent = false
        compare(JSON.stringify(page.outputs), JSON.stringify(["auto", "speaker", "headphones"]))
    }

    function test_output_usb_not_present() {
        const page = open()
        fakeSettings.output = "usb"
        compare(tc.rowOf(page, "output").value, "usb (not present)")
        compare(tc.rowItem(page, 0).valueItem.text, "usb (not present)")
        compare(fakeSettings.output, "usb")
        fakeOutput.usbPresent = true
        compare(tc.rowOf(page, "output").value, "usb")
        fakeOutput.usbPresent = false
        tc.sheetOpen(page, "output")
        compare(page.sheet.mark, -1)
        compare(tc.focused(page), "auto")
        verify(router.dispatch("down", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.output, "speaker")
        fakeSettings.output = "usb"
        tc.sheetOpen(page, "output")
        verify(router.dispatch("b", false))
        compare(fakeSettings.output, "usb")
        tc.sheetOpen(page, "output")
        fakeOutput.usbPresent = true
        compare(page.valueOpen, false)
        compare(fakeSettings.output, "usb")
        fakeOutput.usbPresent = false
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
        compare(page.editing, false)
        compare(fakeSettings.hpGain, "normal")
        tc.focusKey(page, "hpAmp")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(fakeSettings.hpAmp, "lohifi")
        fakeOutput.activeOutput = "headphones"
        compare(tc.rowOf(page, "hpGain").value, "normal")
        compare(tc.rowOf(page, "hpGain").readOnly, false)
        compare(tc.rowOf(page, "hpAmp").value, "lohifi")
        tc.sheetOpen(page, "hpGain")
        verify(router.dispatch("down", false))
        compare(fakeSettings.hpGain, "normal")
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpGain, "high")
        fakeOutput.activeOutput = "speaker"
        compare(tc.rowOf(page, "hpGain").value, "high")
        compare(tc.rowOf(page, "hpGain").readOnly, false)
    }

    function test_codec_cycles() {
        const page = open()
        compare(tc.rowOf(page, "codec").title, "bluetooth codec")
        compare(tc.rowOf(page, "codec").value, "auto")
        tc.sheetOpen(page, "codec")
        compare(tc.items(page), JSON.stringify(["auto", "aptx", "aac", "ldac", "sbc"]))
        for (let i = 0; i < 4; i++)
            verify(router.dispatch("down", false))
        compare(tc.focused(page), "sbc")
        verify(router.dispatch("down", true))
        compare(tc.focused(page), "sbc")
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "auto")
        verify(router.dispatch("down", false))
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
        compare(tc.rowOf(page, "rgb").value, "system")
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
        tc.sheetOpen(page, "hpGain")
        compare(tc.items(page), JSON.stringify(["low", "normal", "high"]))
        compare(tc.focused(page), "normal")
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "high")
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "high")
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "low")
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "low")
        compare(fakeSettings.hpGain, "normal")
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpGain, "low")
        compare(tc.rowOf(page, "hpGain").value, "low")
    }

    function test_headphone_amp_cycles() {
        const page = open()
        tc.sheetOpen(page, "hpAmp")
        compare(tc.items(page), JSON.stringify(["lohifi", "ulp", "hifi"]))
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "hifi")
        compare(fakeSettings.hpAmp, "lohifi")
        verify(router.dispatch("a", false))
        compare(fakeSettings.hpAmp, "hifi")
        compare(tc.rowOf(page, "hpAmp").value, "hifi")
        tc.sheetOpen(page, "hpAmp")
        router.dispatch("up", false)
        compare(fakeSettings.hpAmp, "hifi")
        router.dispatch("b", false)
        compare(fakeSettings.hpAmp, "hifi")
    }

    function test_speaker_gain_steps() {
        const page = open()
        compare(page.spkGainText(84), "0 db")
        compare(page.spkGainText(90), "+6 db")
        compare(page.spkGainText(108), "+24 db")
        tc.sheetOpen(page, "spkGain")
        compare(tc.items(page), JSON.stringify(["+24 db", "+21 db", "+18 db", "+15 db", "+12 db", "+6 db", "0 db"]))
        compare(tc.focused(page), "+18 db")
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "+24 db")
        for (let i = 0; i < 7; i++)
            router.dispatch("down", false)
        compare(tc.focused(page), "0 db")
        compare(fakeSettings.spkGain, 102)
        verify(router.dispatch("a", false))
        compare(fakeSettings.spkGain, 84)
        compare(tc.rowOf(page, "spkGain").value, "0 db")
        tc.sheetOpen(page, "spkGain")
        compare(tc.focused(page), "0 db")
        router.dispatch("up", false)
        verify(router.dispatch("b", false))
        compare(fakeSettings.spkGain, 84)
    }

    function test_volume_step_cycles() {
        const page = open()
        tc.sheetOpen(page, "volumeStep")
        verify(router.dispatch("up", false))
        compare(fakeSettings.volumeStep, 10)
        compare(tc.rowOf(page, "volumeStep").value, "10%")
        verify(router.dispatch("up", false))
        compare(fakeSettings.volumeStep, 2)
        verify(router.dispatch("down", true))
        compare(fakeSettings.volumeStep, 2)
        verify(router.dispatch("down", false))
        compare(fakeSettings.volumeStep, 10)
        verify(router.dispatch("a", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.volumeStep, 10)
    }

    function test_rgb_cycles() {
        const page = open()
        compare(tc.rowOf(page, "rgb").readOnly, false)
        tc.sheetOpen(page, "rgb")
        compare(tc.items(page), JSON.stringify(["accent", "off", "system"]))
        compare(tc.focused(page), "system")
        verify(router.dispatch("down", false))
        compare(fakeSettings.rgb, "accent")
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        compare(fakeSettings.rgb, "off")
        verify(router.dispatch("b", false))
        compare(fakeSettings.rgb, "system")
        tc.sheetOpen(page, "rgb")
        router.dispatch("up", false)
        verify(router.dispatch("a", false))
        compare(fakeSettings.rgb, "off")
        compare(tc.rowOf(page, "rgb").value, "off")
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
        compare(page.editing, false)
        compare(fakeSettings.rgb, "system")
        fakeRgb.supported = true
        compare(tc.rowOf(page, "rgb").value, "system")
        tc.sheetOpen(page, "rgb")
        verify(router.dispatch("up", false))
        compare(fakeSettings.rgb, "off")
        verify(router.dispatch("a", false))
        const bare = open({ rgbControl: null })
        tc.focusKey(bare, "rgb")
        compare(tc.rowOf(bare, "rgb").value, "unsupported")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(bare.editing, false)
        compare(fakeSettings.rgb, "off")
    }

    function test_audio_rows_read_only() {
        const page = open()
        compare(tc.rowOf(page, "rate").title, "sample rate")
        compare(tc.rowOf(page, "rate").value, "auto 44100")
        compare(tc.rowOf(page, "rate").readOnly, false)
        fakeAudio.hwRate = 96000
        compare(tc.rowOf(page, "rate").value, "auto 96000")
        fakeAudio.hwRate = 0
        compare(tc.rowOf(page, "rate").value, "auto")
        fakeAudio.hwRate = 44100
        fakeMode.effective = true
        fakeMode.rateReadOnly = true
        compare(tc.rowOf(page, "rate").value, "auto 44100")
        compare(tc.rowOf(page, "rate").readOnly, true)
        tc.focusKey(page, "rate")
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(fakeSettings.sampleRate, "auto")
        compare(tc.rowOf(page, "replaygain").title, "replaygain")
        compare(tc.rowOf(page, "replaygain").value, "off")
        compare(tc.rowOf(page, "replaygain").readOnly, true)
        tc.focusKey(page, "replaygain")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(tc.rowOf(page, "replaygain").value, "off")
        const item = tc.itemOf(page, "replaygain")
        compare(item.valueItem.font.pixelSize, Ui.fontPx(11))
        compare(item.valueItem.font.styleName, "Light")
        compare(item.valueItem.font.capitalization, Font.AllUppercase)
    }

    function useUsb() {
        fakeOutput.usbPresent = true
        fakeOutput.activeOutput = "usb"
    }

    function test_bit_perfect_rows_order() {
        const page = open()
        compare(tc.keyIndex(page, "level"), tc.keyIndex(page, "codec") + 1)
        compare(tc.keyIndex(page, "rate"), tc.keyIndex(page, "level") + 1)
        compare(tc.keyIndex(page, "replaygain"), tc.keyIndex(page, "rate") + 1)
        compare(tc.rowOf(page, "level").title, "bit perfect")
        compare(tc.itemOf(page, "level").titleItem.text, "bit perfect")
        compare(tc.rowOf(page, "level").readOnly, false)
        compare(tc.rowOf(page, "rate").title, "sample rate")
        for (const r of page.rows)
            verify(r.title !== "sample rate follow")
    }

    function test_bit_perfect_status_suffixes() {
        tc.useUsb()
        const page = open()
        compare(tc.rowOf(page, "level").value, "off")
        fakeSettings.outputLevel = "bitperfect"
        fakeMode.levelStatus = "on"
        fakeMode.effective = true
        compare(tc.rowOf(page, "level").value, "on")
        for (const suffix of ["pending", "volume", "resampled", "converted", "mixed"]) {
            fakeMode.levelStatus = "on (" + suffix + ")"
            compare(tc.rowOf(page, "level").value, "on (" + suffix + ")")
        }
        fakeSettings.outputLevel = "exclusive"
        fakeMode.levelStatus = "exclusive"
        compare(tc.rowOf(page, "level").value, "exclusive")
        for (const suffix of ["pending", "volume", "resampled", "converted", "phone", "released", "unavailable", "no volume"]) {
            fakeMode.levelStatus = "exclusive (" + suffix + ")"
            compare(tc.rowOf(page, "level").value, "exclusive (" + suffix + ")")
        }
        fakeMode.levelStatus = "exclusive (unavailable)"
        const item = tc.itemOf(page, "level")
        compare(item.valueItem.text, "exclusive (unavailable)")
        compare(item.valueItem.font.capitalization, Font.AllUppercase)
        verify(item.titleItem.contentWidth <= item.titleItem.width)
        verify(item.titleItem.x + item.titleItem.width <= item.valueItem.x)
        fuzzyCompare(item.valueItem.x + item.valueItem.width, page.width - Ui.px(20), 0.01)
    }

    function test_bit_perfect_g3_outputs() {
        for (const output of ["speaker", "headphones"]) {
            fakeOutput.activeOutput = output
            const page = open()
            fakeSettings.outputLevel = "off"
            fakeMode.levelStatus = "off"
            compare(tc.rowOf(page, "level").value, "off")
            fakeSettings.outputLevel = "bitperfect"
            fakeMode.levelStatus = "usb only"
            compare(tc.rowOf(page, "level").value, "usb only")
            fakeSettings.outputLevel = "exclusive"
            compare(tc.rowOf(page, "level").value, "usb only")
            compare(tc.rowOf(page, "level").readOnly, false)
        }
        tc.useUsb()
        const usb = open()
        fakeSettings.outputLevel = "off"
        fakeMode.levelStatus = "off"
        compare(tc.rowOf(usb, "level").value, "off")
    }

    function test_bit_perfect_overlay() {
        for (const level of ["off", "bitperfect", "exclusive"]) {
            fakeSettings.outputLevel = level
            const page = open({ overlay: true, bridge: fakeBridge, outputMode: null })
            compare(tc.rowOf(page, "level").value, "dap only")
            compare(tc.rowOf(page, "level").readOnly, true)
            compare(tc.rowOf(page, "rate").value, "dap only")
            compare(tc.rowOf(page, "rate").readOnly, true)
            for (const key of ["level", "rate"]) {
                tc.focusKey(page, key)
                verify(router.dispatch("a", false))
                compare(page.editing, false)
                compare(router.dispatch("right", false), false)
            }
            compare(fakeSettings.outputLevel, level)
            compare(fakeSettings.sampleRate, "auto")
        }
    }

    function test_bit_perfect_without_output_mode() {
        tc.useUsb()
        const page = open({ outputMode: null })
        fakeSettings.outputLevel = "bitperfect"
        compare(tc.rowOf(page, "level").value, "on")
        fakeSettings.outputLevel = "exclusive"
        compare(tc.rowOf(page, "level").value, "exclusive")
        compare(tc.rowOf(page, "rate").value, "auto 44100")
        compare(tc.rowOf(page, "rate").readOnly, false)
        fakeSettings.sampleRate = "96000"
        compare(tc.rowOf(page, "rate").value, "96000")
        compare(tc.rowOf(page, "eq").value, "off")
    }

    function test_bit_perfect_draft_edit() {
        tc.useUsb()
        const page = open()
        const spy = spyComponent.createObject(tc, { target: fakeSettings, signalName: "outputLevelChanged" })
        tc.sheetOpen(page, "level")
        compare(tc.items(page), JSON.stringify(["off", "on", "exclusive"]))
        compare(page.sheet.mark, 0)
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "exclusive")
        compare(tc.rowOf(page, "level").value, "off")
        compare(fakeSettings.outputLevel, "off")
        verify(router.dispatch("b", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.outputLevel, "off")
        compare(pages.depth, 2)
        compare(spy.count, 0)
        tc.sheetOpen(page, "level")
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        verify(router.dispatch("up", false))
        compare(spy.count, 0)
        verify(router.dispatch("a", false))
        compare(fakeSettings.outputLevel, "bitperfect")
        compare(spy.count, 1)
        fakeMode.levelStatus = "on (pending)"
        compare(tc.rowOf(page, "level").value, "on (pending)")
        tc.sheetOpen(page, "level")
        compare(page.sheet.mark, 1)
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.outputLevel, "off")
        compare(spy.count, 2)
        spy.destroy()
    }

    function test_bit_perfect_edit_dropped_on_leave() {
        const page = open()
        tc.sheetOpen(page, "level")
        router.dispatch("down", false)
        pages.push(blankComponent, {})
        compare(page.valueOpen, false)
        compare(fakeSettings.outputLevel, "off")
    }

    function test_sample_rate_values() {
        tc.useUsb()
        fakeMode.rates = [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000]
        const page = open()
        compare(tc.rowOf(page, "rate").value, "auto 44100")
        fakeSettings.sampleRate = "352800"
        compare(tc.rowOf(page, "rate").value, "352800")
        fakeMode.rates = [48000]
        compare(tc.rowOf(page, "rate").value, "352800 (not supported)")
        fakeSettings.sampleRate = "48000"
        compare(tc.rowOf(page, "rate").value, "48000")
        fakeSettings.sampleRate = "96000"
        compare(tc.rowOf(page, "rate").value, "96000 (not supported)")
        compare(fakeSettings.sampleRate, "96000")
        fakeSettings.outputLevel = "bitperfect"
        fakeMode.levelStatus = "on"
        fakeMode.effective = true
        fakeMode.rateReadOnly = true
        fakeAudio.hwRate = 48000
        compare(tc.rowOf(page, "rate").value, "auto 48000")
        compare(tc.rowOf(page, "rate").readOnly, true)
        fakeMode.effective = false
        fakeMode.rateReadOnly = false
        compare(tc.rowOf(page, "rate").value, "96000 (not supported)")
        compare(tc.rowOf(page, "rate").readOnly, false)
    }

    function test_sample_rate_draft_edit() {
        tc.useUsb()
        fakeMode.rates = [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000]
        const page = open()
        const spy = spyComponent.createObject(tc, { target: fakeSettings, signalName: "sampleRateChanged" })
        tc.sheetOpen(page, "rate")
        compare(tc.items(page), JSON.stringify(["384000", "352800", "192000", "176400", "96000", "88200", "48000", "44100", "auto"]))
        compare(page.sheet.mark, 8)
        compare(tc.focused(page), "auto")
        const seen = []
        for (let i = 0; i < 9; i++) {
            verify(router.dispatch("up", false))
            seen.push(tc.focused(page))
        }
        compare(JSON.stringify(seen), JSON.stringify(["44100", "48000", "88200", "96000", "176400", "192000", "352800", "384000", "auto"]))
        compare(fakeSettings.sampleRate, "auto")
        compare(tc.rowOf(page, "rate").value, "auto 44100")
        verify(router.dispatch("b", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.sampleRate, "auto")
        compare(spy.count, 0)
        tc.sheetOpen(page, "rate")
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "352800")
        compare(spy.count, 0)
        verify(router.dispatch("a", false))
        compare(fakeSettings.sampleRate, "352800")
        compare(spy.count, 1)
        compare(tc.rowOf(page, "rate").value, "352800")
        tc.sheetOpen(page, "rate")
        compare(tc.focused(page), "352800")
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "auto")
        verify(router.dispatch("a", false))
        compare(fakeSettings.sampleRate, "auto")
        compare(spy.count, 2)
        fakeMode.rates = [48000]
        tc.sheetOpen(page, "rate")
        compare(tc.items(page), JSON.stringify(["48000", "auto"]))
        verify(router.dispatch("b", false))
        compare(fakeSettings.sampleRate, "auto")
        spy.destroy()
    }

    function test_sample_rate_built_in_rates() {
        const page = open({ outputMode: null })
        tc.sheetOpen(page, "rate")
        compare(tc.items(page), JSON.stringify(["192000", "176400", "96000", "88200", "48000", "44100", "auto"]))
        verify(router.dispatch("b", false))
    }

    function test_sample_rate_unsupported_saved_steps_from_nearest() {
        fakeSettings.sampleRate = "352800"
        const page = open()
        compare(tc.rowOf(page, "rate").value, "352800 (not supported)")
        tc.sheetOpen(page, "rate")
        compare(page.sheet.mark, -1)
        compare(tc.focused(page), "192000")
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "176400")
        verify(router.dispatch("a", false))
        compare(fakeSettings.sampleRate, "176400")
    }

    function test_sample_rate_edit_dropped_when_locked() {
        tc.useUsb()
        const page = open()
        const spy = spyComponent.createObject(tc, { target: fakeSettings, signalName: "sampleRateChanged" })
        tc.sheetOpen(page, "rate")
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "44100")
        fakeSettings.outputLevel = "bitperfect"
        fakeMode.levelStatus = "on"
        fakeMode.effective = true
        fakeMode.rateReadOnly = true
        compare(page.valueOpen, false)
        compare(tc.rowOf(page, "rate").value, "auto 44100")
        verify(router.dispatch("a", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.sampleRate, "auto")
        compare(spy.count, 0)
        spy.destroy()
    }

    function test_level_editable_on_speaker() {
        fakeOutput.activeOutput = "speaker"
        const page = open()
        compare(tc.rowOf(page, "level").value, "off")
        compare(tc.rowOf(page, "level").readOnly, false)
        tc.sheetOpen(page, "level")
        verify(router.dispatch("down", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.outputLevel, "bitperfect")
        compare(tc.rowOf(page, "level").value, "usb only")
    }

    function test_touch_level_sheet_item() {
        tc.useUsb()
        const page = open()
        tc.tapRow(page, "level")
        compare(page.valueOpen, true)
        page.sheet.complete()
        compare(fakeSettings.outputLevel, "off")
        tc.tapItem(page, 2)
        compare(page.valueOpen, false)
        compare(fakeSettings.outputLevel, "exclusive")
    }

    function test_touch_rate_scrim_restores() {
        const page = open()
        tc.tapRow(page, "rate")
        compare(page.valueOpen, true)
        page.sheet.complete()
        tc.tapAt(page.sheet.scrim, page.width / 2, Ui.px(4))
        compare(page.valueOpen, false)
        compare(fakeSettings.sampleRate, "auto")
        tc.tapRow(page, "rate")
        page.sheet.complete()
        tc.tapItem(page, page.sheet.items.indexOf("44100"))
        compare(page.valueOpen, false)
        compare(fakeSettings.sampleRate, "44100")
    }

    function test_eq_bypassed_under_level() {
        tc.useUsb()
        const page = open({ eq: fakeEq })
        fakeEq.activeOutput = "usb"
        compare(tc.rowOf(page, "eq").value, "custom")
        fakeSettings.outputLevel = "bitperfect"
        fakeMode.levelStatus = "on"
        fakeMode.effective = true
        compare(tc.rowOf(page, "eq").value, "bypassed")
        compare(tc.rowOf(page, "eq").readOnly, true)
        tc.focusKey(page, "eq")
        verify(router.dispatch("a", false))
        compare(counters.eq, 1)
        compare(page.editing, false)
        fakeMode.effective = false
        compare(tc.rowOf(page, "eq").value, "custom")
    }

    function test_eq_row_dac() {
        tc.useUsb()
        const file = "/home/micro/.config/microdap/eq/HD 600.txt"
        fakeEq.activeOutput = "usb"
        fakeEq.keys = ({ headphones: file, usb: file, speaker: "off" })
        const page = open({ eq: fakeEq, dacEq: fakeDacEq })
        const value = () => tc.rowOf(page, "eq").value
        compare(value(), "HD 600")
        for (const s of ["none", "unsupported", "applying", "on", "flat"]) {
            fakeDacEq.status = s
            compare(value(), "HD 600")
        }
        fakeDacEq.status = "failed"
        compare(value(), "dac eq failed")
        fakeEq.activeOutput = "headphones"
        compare(value(), "HD 600")
        fakeEq.activeOutput = "speaker"
        compare(value(), "off")
        fakeEq.activeOutput = "usb"
        compare(value(), "dac eq failed")
        fakeSettings.outputLevel = "bitperfect"
        fakeMode.levelStatus = "on"
        fakeMode.effective = true
        compare(value(), "dac eq failed")
        fakeDacEq.status = "none"
        compare(value(), "bypassed")
        fakeDacEq.status = "unsupported"
        compare(value(), "bypassed")
        fakeDacEq.status = "applying"
        compare(value(), "HD 600 (dac)")
        fakeDacEq.status = "on"
        compare(value(), "HD 600 (dac)")
        fakeDacEq.status = "flat"
        compare(value(), "HD 600")
        fakeDacEq.status = "on"
        fakeEq.keys = ({ headphones: file, usb: "custom", speaker: "off" })
        fakeEq.profilesChanged()
        compare(value(), "custom (dac)")
        fakeEq.keys = ({ headphones: file, usb: "off", speaker: "off" })
        fakeEq.profilesChanged()
        compare(value(), "off")
        fakeDacEq.status = "flat"
        compare(value(), "off")
        fakeEq.keys = ({ headphones: file, usb: file, speaker: "off" })
        fakeEq.missing = true
        fakeEq.profilesChanged()
        compare(value(), "HD 600 (missing)")
        fakeDacEq.status = "on"
        compare(value(), "HD 600 (missing)")
        fakeEq.missing = false
        fakeEq.profilesChanged()
        compare(value(), "HD 600 (dac)")
        compare(tc.rowOf(page, "eq").readOnly, true)
        tc.focusKey(page, "eq")
        compare(router.dispatch("left", false), false)
        verify(router.dispatch("a", false))
        compare(counters.eq, 1)
        compare(page.editing, false)
        fakeDacEq.status = "failed"
        fakeSettings.outputLevel = "off"
        fakeMode.levelStatus = "off"
        fakeMode.effective = false
        compare(value(), "dac eq failed")
        fakeEq.activeOutput = "headphones"
        compare(value(), "HD 600")
        fakeEq.activeOutput = "usb"
        fakeDacEq.status = "flat"
        compare(value(), "HD 600")
    }

    function test_eq_row_dac_overlay() {
        fakeDacEq.status = "failed"
        const page = open({ overlay: true, bridge: fakeBridge, eq: fakeEq, dacEq: fakeDacEq })
        compare(tc.rowOf(page, "eq").value, "HD 600")
        fakeMode.effective = true
        compare(tc.rowOf(page, "eq").value, "bypassed")
        fakeDacEq.status = "on"
        compare(tc.rowOf(page, "eq").value, "bypassed")
    }

    function test_audio_info_refresh_on_open() {
        open()
        compare(fakeAudio.refreshes, 1)
    }

    function test_eq_row() {
        const page = open({ eq: fakeEq })
        verify(tc.keyIndex(page, "eq") === tc.keyIndex(page, "replaygain") + 1)
        verify(tc.keyIndex(page, "eq") === tc.keyIndex(page, "pair") - 1)
        compare(tc.rowOf(page, "eq").title, "eq")
        compare(tc.rowOf(page, "eq").value, "HD 600")
        compare(tc.itemOf(page, "eq").valueItem.font.capitalization, Font.AllUppercase)
        fakeEq.activeOutput = "usb"
        compare(tc.rowOf(page, "eq").value, "custom")
        fakeEq.activeOutput = "speaker"
        compare(tc.rowOf(page, "eq").value, "off")
        fakeEq.activeOutput = "headphones"
        fakeEq.missing = true
        fakeEq.profilesChanged()
        compare(tc.rowOf(page, "eq").value, "HD 600 (missing)")
        tc.focusKey(page, "eq")
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        verify(router.dispatch("a", false))
        compare(counters.eq, 1)
        compare(page.editing, false)
        compare(tc.rowOf(page, "eq").value, "HD 600 (missing)")
    }

    function test_eq_row_without_eq() {
        const page = open()
        compare(tc.rowOf(page, "eq").value, "off")
        tc.focusKey(page, "eq")
        verify(router.dispatch("a", false))
        compare(counters.eq, 1)
    }

    function test_eq_row_overlay() {
        const page = open({ overlay: true, bridge: fakeBridge, eq: fakeEq })
        compare(tc.rowOf(page, "eq").value, "HD 600")
        tc.focusKey(page, "eq")
        verify(router.dispatch("a", false))
        compare(counters.eq, 1)
    }

    function test_pair_a_phone() {
        const page = open()
        tc.focusKey(page, "pair")
        compare(tc.rowOf(page, "pair").value, "")
        verify(router.dispatch("a", false))
        compare(counters.pair, 1)
        compare(router.dispatch("right", false), false)
    }

    function test_toasts_in_game_row() {
        const page = open()
        compare(tc.rowOf(page, "toasts").title, "toasts in game")
        compare(tc.itemOf(page, "toasts").titleItem.text, "toasts in game")
        compare(tc.rowOf(page, "toasts").value, "off")
        verify(tc.keyIndex(page, "toasts") > tc.keyIndex(page, "saver"))
        compare(tc.rowOf(page, "toasts").spec.pivot, "display")
        tc.focusKey(page, "toasts")
        verify(router.dispatch("a", false))
        compare(page.valueOpen, false)
        compare(page.editing, false)
        compare(fakeSettings.overlayToasts, true)
        compare(tc.rowOf(page, "toasts").value, "on")
        verify(router.dispatch("a", false))
        compare(fakeSettings.overlayToasts, false)
        for (const r of page.rows)
            verify(r.title !== "overlay toasts")
    }

    function test_overlay_mode_rows() {
        const page = open({ overlay: true, bridge: fakeBridge })
        compare(JSON.stringify(tc.allKeys(page)), JSON.stringify(tc.overlayKeys))
        compare(tc.rowOf(page, "hpGain").title, "headphone gain")
        compare(tc.rowOf(page, "closePlayer").title, "close player")
        compare(tc.rowOf(page, "pair").value, "pair in dap mode")
        compare(tc.itemOf(page, "pair").valueItem.visible, true)
        compare(tc.itemOf(page, "pair").valueItem.text, "pair in dap mode")
        tc.focusKey(page, "pair")
        verify(router.dispatch("a", false))
        compare(counters.pair, 0)
        compare(router.dispatch("right", false), false)
        tc.focusKey(page, "closePlayer")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "close player")
        compare(page.sheet.items, ["close player"])
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(pages.depth, 2)
        compare(fakeBridge.closes, 0)
        router.dispatch("a", false)
        router.dispatch("a", false)
        compare(page.sheet.shown, false)
        compare(fakeBridge.closes, 1)
        compare(fakePower.verbs.length, 0)
        compare(fakePower.switches, 0)
        compare(counters.closePlayer, 1)
        compare(tc.rowOf(page, "toasts").title, "toasts in game")
        compare(tc.rowOf(page, "saver").title, "saver")
    }

    function test_library_folders_default() {
        fakeSettings.roots = ["/home/micro/Music", "/run/media/micro/SD"]
        fakeSettings.defaultList = ["/run/media/micro/SD", "/home/micro/Music"]
        const page = open()
        tc.focusKey(page, "folders")
        const folders = tc.keyIndex(page, "folders")
        verify(router.dispatch("a", false))
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.focusIndex, folders + 2)
        verify(router.dispatch("a", false))
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/home/micro/Music"]))
        verify(router.dispatch("a", false))
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify([]))
    }

    function test_library_folders() {
        const page = open()
        tc.focusKey(page, "folders")
        const folders = tc.keyIndex(page, "folders")
        compare(page.rows[folders].value, "1 on")
        verify(router.dispatch("a", false))
        compare(page.foldersOpen, true)
        compare(page.editing, false)
        compare(page.rows[folders + 1].kind, "sub")
        compare(page.rows[folders + 1].title, "/home/micro/Music")
        compare(page.rows[folders + 1].value, "on")
        compare(page.rows[folders + 2].title, "/run/media/micro/SD")
        compare(page.rows[folders + 2].value, "off")
        compare(page.rows[folders + 3].key, "rescan")
        compare(page.rows.length, folders + 4)
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
        compare(page.editing, false)
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/home/micro/Music", "/run/media/micro/SD"]))
        compare(page.rows[folders + 2].value, "on")
        compare(page.rows[folders].value, "2 on")
        router.dispatch("up", false)
        router.dispatch("a", false)
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/run/media/micro/SD"]))
        compare(page.rows[folders + 1].value, "off")
        compare(page.rows[folders + 1].title, "/home/micro/Music")
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/run/media/micro/SD"]))
        compare(page.rows[folders + 2].value, "on")
        router.dispatch("up", false)
        router.dispatch("up", false)
        router.dispatch("a", false)
        compare(page.foldersOpen, false)
        compare(page.rows[folders + 1].key, "rescan")
        compare(page.rows.length, folders + 2)
    }

    function test_accent_hue_swatch() {
        const page = open()
        compare(tc.rowOf(page, "accent").readOnly, false)
        let row = tc.itemOf(page, "accent")
        compare(row.valueItem.visible, false)
        compare(row.swatchItem.visible, true)
        compare(row.swatchItem.width, Ui.px(12))
        compare(row.swatchItem.height, Ui.px(12))
        fuzzyCompare(row.swatchItem.x + row.swatchItem.width, page.width - Ui.px(20), 0.01)
        verify(Qt.colorEqual(row.swatchItem.color, Ui.accentForHue(350)))
        verify(Qt.colorEqual(row.swatchItem.color, "#FA2A4D"))
        tc.focusKey(page, "accent")
        compare(router.dispatch("right", false), false)
        compare(fakeSettings.accentHue, 350)
        tc.edit(page, "accent")
        verify(router.dispatch("right", false))
        compare(fakeSettings.accentHue, 0)
        row = tc.itemOf(page, "accent")
        verify(Qt.colorEqual(row.swatchItem.color, Ui.accentForHue(0)))
        verify(router.dispatch("right", true))
        verify(router.dispatch("right", true))
        compare(fakeSettings.accentHue, 20)
        for (let i = 0; i < 3; i++)
            verify(router.dispatch("left", false))
        compare(fakeSettings.accentHue, 350)
        verify(router.dispatch("left", false))
        compare(fakeSettings.accentHue, 340)
        row = tc.itemOf(page, "accent")
        verify(Qt.colorEqual(row.swatchItem.color, Ui.accentForHue(340)))
        tc.confirm(page)
        compare(fakeSettings.accentHue, 340)
        fakeSettings.accentHue = 240
        row = tc.itemOf(page, "accent")
        verify(Qt.colorEqual(row.swatchItem.color, Ui.accentForHue(240)))
        tc.edit(page, "accent")
        verify(router.dispatch("left", true))
        verify(router.dispatch("left", true))
        compare(fakeSettings.accentHue, 220)
        verify(router.dispatch("b", false))
        compare(fakeSettings.accentHue, 240)
        compare(page.focusIndex, tc.keyIndex(page, "accent"))
        verify(Qt.colorEqual(Ui.accent, "#FA2A4D"))
    }

    function test_accent_swatch_ignores_art() {
        fakeSettings.accentFromArt = true
        fakeSettings.accentHue = 200
        const page = open()
        verify(Qt.colorEqual(tc.itemOf(page, "accent").swatchItem.color, Ui.accentForHue(200)))
    }

    function test_accent_from_art_toggles() {
        const page = open()
        const accent = tc.keyIndex(page, "accent")
        compare(tc.keyIndex(page, "accentArt"), accent + 1)
        compare(tc.rowOf(page, "accentArt").title, "accent from art")
        compare(tc.rowOf(page, "accentArt").value, "off")
        compare(tc.rowOf(page, "accentArt").readOnly, false)
        tc.focusKey(page, "accentArt")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.valueOpen, false)
        compare(fakeSettings.accentFromArt, true)
        compare(tc.rowOf(page, "accentArt").value, "on")
        compare(tc.itemOf(page, "accentArt").valueItem.text, "on")
        compare(router.dispatch("right", false), false)
        compare(fakeSettings.accentFromArt, true)
        verify(router.dispatch("a", false))
        compare(fakeSettings.accentFromArt, false)
        compare(fakeSettings.accentHue, 350)
    }

    function test_accent_rows_in_overlay() {
        const page = open({ overlay: true, bridge: fakeBridge })
        tc.keyIndex(page, "accent")
        tc.keyIndex(page, "accentArt")
        tc.absent(page, "rgb")
        tc.edit(page, "accent")
        verify(router.dispatch("right", false))
        compare(fakeSettings.accentHue, 0)
        tc.confirm(page)
    }

    function test_orientation_edit() {
        const page = open()
        compare(tc.rowOf(page, "orientation").value, "portrait")
        compare(tc.rowOf(page, "orientation").readOnly, false)
        tc.sheetOpen(page, "orientation")
        compare(tc.items(page), JSON.stringify(["portrait", "portrait flipped", "landscape", "landscape flipped"]))
        for (let i = 0; i < 3; i++)
            verify(router.dispatch("down", false))
        compare(tc.focused(page), "landscape flipped")
        compare(fakeSettings.orientation, "portrait")
        compare(Ui.turn, 0)
        verify(router.dispatch("b", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.orientation, "portrait")
        compare(tc.rowOf(page, "orientation").value, "portrait")
        compare(pages.depth, 2)
        tc.sheetOpen(page, "orientation")
        verify(router.dispatch("down", false))
        compare(fakeSettings.orientation, "portrait")
        verify(router.dispatch("a", false))
        compare(fakeSettings.orientation, "portrait-flipped")
        compare(tc.rowOf(page, "orientation").value, "portrait flipped")
        tc.sheetOpen(page, "orientation")
        compare(page.sheet.mark, 1)
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.orientation, "portrait")
    }

    function test_orientation_edit_dropped_on_leave() {
        const page = open()
        tc.sheetOpen(page, "orientation")
        router.dispatch("down", false)
        page.dismissValue()
        compare(page.valueOpen, false)
        compare(fakeSettings.orientation, "portrait")
        compare(tc.rowOf(page, "orientation").value, "portrait")
    }

    function test_orientation_rotation_revert_or_confirm() {
        const page = open()
        tc.sheetOpen(page, "orientation")
        router.dispatch("down", false)
        page.revertOrConfirmForRotation()
        compare(page.valueOpen, false)
        compare(fakeSettings.orientation, "portrait")
        tc.sheetOpen(page, "saver")
        router.dispatch("up", false)
        page.revertOrConfirmForRotation()
        compare(page.valueOpen, false)
        compare(fakeSettings.saverSeconds, 60)
        tc.edit(page, "accent")
        router.dispatch("right", false)
        page.revertOrConfirmForRotation()
        compare(page.editing, false)
        compare(fakeSettings.accentHue, 0)
    }

    function test_orientation_external_surface() {
        tc.setWindow(960, 640)
        const page = open()
        compare(tc.rowOf(page, "orientation").value, "external display")
        compare(tc.rowOf(page, "orientation").readOnly, true)
        tc.focusKey(page, "orientation")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(router.dispatch("right", false), false)
        compare(fakeSettings.orientation, "portrait")
    }

    function test_orientation_absent_in_overlay() {
        const page = open({ overlay: true, bridge: fakeBridge })
        tc.absent(page, "orientation")
    }

    function openInMain(properties) {
        const main = createTemporaryObject(mainComponent, tc, properties === undefined ? {} : properties)
        verify(main !== null)
        main.pageStack.animate = false
        main.restoreStack([])
        main.navigate("settings")
        const page = main.pageStack.currentItem
        compare(page.objectName, "settings")
        page.completeAll()
        return { main: main, page: page }
    }

    function test_orientation_applies_in_main() {
        const m = tc.openInMain()
        const page = m.page
        compare(Ui.orientationActive, true)
        page.focusKey("orientation")
        verify(page.handleAction("a", false))
        compare(page.valueOpen, true)
        page.sheet.complete()
        page.handleAction("down", false)
        page.handleAction("down", false)
        compare(Ui.turn, 0)
        page.handleAction("a", false)
        compare(fakeSettings.orientation, "landscape")
        tryCompare(Ui, "turn", Ui.landscapeTurn, 1000)
        compare(m.main.pageStack.currentItem, page)
        compare(tc.rowOf(page, "orientation").value, "landscape")
        page.focusKey("orientation")
        page.handleAction("a", false)
        page.sheet.complete()
        page.handleAction("up", false)
        page.handleAction("b", false)
        wait(0)
        compare(Ui.turn, Ui.landscapeTurn)
        compare(fakeSettings.orientation, "landscape")
    }

    function test_orientation_external_output_in_main() {
        const m = tc.openInMain()
        fakeOutputs.internal = false
        tryCompare(Ui, "orientationActive", false, 1000)
        compare(tc.rowOf(m.page, "orientation").value, "external display")
        compare(tc.rowOf(m.page, "orientation").readOnly, true)
        m.page.focusKey("orientation")
        m.page.handleAction("a", false)
        compare(m.page.editing, false)
        fakeOutputs.internal = true
        tryCompare(Ui, "orientationActive", true, 1000)
        compare(tc.rowOf(m.page, "orientation").value, "portrait")
    }

    function test_auto_rotate_unsupported() {
        const page = open()
        compare(tc.rowOf(page, "autoRotate").title, "auto rotate")
        compare(tc.rowOf(page, "autoRotate").value, "unsupported")
        compare(tc.rowOf(page, "autoRotate").readOnly, true)
        tc.focusKey(page, "autoRotate")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(router.dispatch("right", false), false)
        compare(fakeSettings.autoRotate, false)
        fakeSensor.supported = false
        const unsupported = open({ sensor: fakeSensor })
        compare(tc.rowOf(unsupported, "autoRotate").value, "unsupported")
        compare(tc.rowOf(unsupported, "autoRotate").readOnly, true)
        tc.focusKey(unsupported, "autoRotate")
        verify(router.dispatch("a", false))
        compare(unsupported.editing, false)
        fakeSensor.supported = true
        compare(tc.rowOf(unsupported, "autoRotate").value, "off")
        compare(tc.rowOf(unsupported, "autoRotate").readOnly, false)
    }

    function test_auto_rotate_edit() {
        const page = open({ sensor: fakeSensor })
        compare(tc.keyIndex(page, "autoRotate"), tc.keyIndex(page, "orientation") + 1)
        compare(tc.rowOf(page, "autoRotate").value, "off")
        compare(tc.rowOf(page, "autoRotate").readOnly, false)
        tc.focusKey(page, "autoRotate")
        verify(router.dispatch("a", false))
        compare(page.valueOpen, false)
        compare(page.editing, false)
        compare(fakeSettings.autoRotate, true)
        compare(tc.rowOf(page, "autoRotate").value, "on")
        verify(router.dispatch("a", false))
        compare(fakeSettings.autoRotate, false)
        compare(tc.rowOf(page, "autoRotate").value, "off")
    }

    function test_auto_rotate_orientation_read_only() {
        fakeSettings.autoRotate = true
        Ui.turn = Ui.landscapeTurn
        const page = open({ sensor: fakeSensor })
        compare(tc.rowOf(page, "orientation").value, "landscape")
        compare(tc.rowOf(page, "orientation").readOnly, true)
        tc.focusKey(page, "orientation")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(fakeSettings.orientation, "portrait")
        Ui.turn = 2
        compare(tc.rowOf(page, "orientation").value, "portrait flipped")
        fakeSensor.supported = false
        compare(tc.rowOf(page, "orientation").value, "portrait")
        compare(tc.rowOf(page, "orientation").readOnly, false)
        fakeSensor.supported = true
        fakeSettings.autoRotate = false
        compare(tc.rowOf(page, "orientation").value, "portrait")
        compare(tc.rowOf(page, "orientation").readOnly, false)
        Ui.turn = 0
    }

    function test_auto_rotate_absent_in_overlay() {
        const page = open({ overlay: true, bridge: fakeBridge, sensor: fakeSensor })
        tc.absent(page, "autoRotate")
    }

    function test_auto_rotate_in_main() {
        const m = tc.openInMain({ sensor: fakeSensor })
        const page = m.page
        compare(tc.rowOf(page, "autoRotate").value, "off")
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], false)
        page.focusKey("autoRotate")
        page.handleAction("a", false)
        compare(page.valueOpen, false)
        compare(fakeSettings.autoRotate, true)
        compare(fakeSensor.wanted[fakeSensor.wanted.length - 1], true)
        fakeSensor.raw = "left-up"
        fakeSensor.turn = Ui.landscapeTurn
        tryCompare(Ui, "turn", Ui.landscapeTurn, 1000)
        compare(fakeSettings.orientation, "portrait")
        compare(tc.rowOf(page, "orientation").value, "landscape")
        compare(tc.rowOf(page, "orientation").readOnly, true)
        compare(m.main.pageStack.currentItem, page)
    }

    function test_chord_reverts_orientation_edit() {
        const m = tc.openInMain()
        const page = m.page
        const spy = createTemporaryObject(spyComponent, tc, { target: m.main, signalName: "orientationApplied" })
        page.focusKey("orientation")
        page.handleAction("a", false)
        page.sheet.complete()
        page.handleAction("down", false)
        page.handleAction("down", false)
        compare(tc.focused(page), "landscape")
        compare(tc.rowOf(page, "orientation").value, "portrait")
        m.main.rotateBy("right")
        compare(page.valueOpen, false)
        compare(fakeSettings.orientation, "portrait-flipped")
        tryCompare(Ui, "turn", 2, 1000)
        wait(0)
        compare(spy.count, 1)
        compare(tc.rowOf(page, "orientation").value, "portrait flipped")
    }

    function test_chord_confirms_saver_edit() {
        const m = tc.openInMain()
        const page = m.page
        page.focusKey("saver")
        page.handleAction("a", false)
        page.sheet.complete()
        page.handleAction("up", false)
        compare(fakeSettings.saverSeconds, 60)
        m.main.rotateBy("up")
        compare(page.valueOpen, false)
        compare(fakeSettings.saverSeconds, 60)
        tryCompare(Ui, "turn", Ui.landscapeTurn, 1000)
        compare(fakeSettings.saverSeconds, 60)
    }

    function test_scale_steps_from_the_table() {
        const page = open()
        const r = page.tableRow("scale")
        fuzzyCompare(r.min, Ui.sMin, 1e-9)
        fuzzyCompare(r.step, Ui.sStep, 1e-9)
        compare(page.scaleSteps[0], Math.round(r.min * 10) / 10)
        compare(page.scaleSteps[page.scaleSteps.length - 1], Math.round(Math.min(r.max, Ui.sMax) * 10) / 10)
    }

    function test_scale_kept_in_landscape() {
        fakeSettings.scale = 1.1
        const m = tc.openInMain()
        const page = m.page
        compare(Ui.userScale, 1.1)
        fakeSettings.orientation = "landscape"
        tryCompare(Ui, "turn", Ui.landscapeTurn, 1000)
        compare(Ui.userScale, 1.0)
        compare(tc.rowOf(page, "scale").value, "1.0")
        page.focusKey("scale")
        page.handleAction("a", false)
        compare(page.valueOpen, true)
        page.sheet.complete()
        compare(tc.focused(page), "1.0")
        page.handleAction("b", false)
        page.sheet.complete()
        compare(fakeSettings.scale, 1.1)
        page.handleAction("a", false)
        page.sheet.complete()
        page.handleAction("up", false)
        page.handleAction("a", false)
        compare(page.valueOpen, false)
        compare(fakeSettings.scale, 1.1)
        compare(Ui.userScale, 1.0)
        fakeSettings.orientation = "portrait"
        tryCompare(Ui, "turn", 0, 1000)
        compare(Ui.userScale, 1.1)
    }

    function test_scale_draft_focus_kept() {
        const m = tc.openInMain()
        const page = m.page
        compare(Ui.sMax, 1.1)
        compare(JSON.stringify(page.scaleSteps), JSON.stringify([0.8, 0.9, 1.0, 1.1]))
        const scale = tc.keyIndex(page, "scale")
        compare(page.rows[scale].value, "1.0")
        page.focusIndex = scale
        verify(page.handleAction("a", false))
        compare(page.valueOpen, true)
        page.sheet.complete()
        compare(tc.items(page), JSON.stringify(["1.1", "1.0", "0.9", "0.8"]))
        compare(tc.focused(page), "1.0")
        verify(page.handleAction("up", false))
        compare(tc.focused(page), "1.1")
        compare(Ui.userScale, 1.0)
        compare(fakeSettings.scale, 1.0)
        verify(page.handleAction("up", false))
        compare(tc.focused(page), "1.1")
        verify(page.handleAction("a", false))
        compare(page.valueOpen, false)
        compare(Ui.userScale, 1.1)
        compare(fakeSettings.scale, 1.1)
        compare(page.focusIndex, scale)
        compare(page.rows[scale].value, "1.1")
        compare(tc.rowItem(page, scale).height, Ui.px(43))
        fuzzyCompare(Ui.u, 2 * 1.1, 0.001)
        const top = page.rowTopOf(scale) - page.flick.contentY
        verify(top >= 0)
        verify(top + Ui.px(43) <= page.flick.height + 0.01)
        verify(page.handleAction("a", false))
        for (let i = 0; i < 4; i++)
            page.handleAction("down", false)
        compare(tc.focused(page), "0.8")
        verify(page.handleAction("b", false))
        compare(page.valueOpen, false)
        compare(Ui.userScale, 1.1)
        compare(fakeSettings.scale, 1.1)
        compare(page.focusIndex, scale)
        compare(m.main.pageStack.depth, 2)
    }

    function test_saver_steps() {
        const page = open()
        compare(tc.rowOf(page, "saver").value, "30 s")
        tc.sheetOpen(page, "saver")
        compare(tc.items(page), JSON.stringify(["5 min", "2 min", "1 min", "30 s", "15 s", "10 s"]))
        router.dispatch("up", false)
        compare(fakeSettings.saverSeconds, 60)
        compare(tc.rowOf(page, "saver").value, "1 min")
        router.dispatch("up", false)
        router.dispatch("up", false)
        compare(fakeSettings.saverSeconds, 300)
        router.dispatch("up", false)
        compare(fakeSettings.saverSeconds, 10)
        router.dispatch("up", true)
        compare(fakeSettings.saverSeconds, 10)
        compare(tc.focused(page), "15 s")
        verify(router.dispatch("a", false))
        compare(fakeSettings.saverSeconds, 15)
        compare(tc.rowOf(page, "saver").value, "15 s")
    }

    function test_power_rows() {
        const page = open()
        compare(tc.rowOf(page, "screenOff").value, "5 min")
        tc.sheetOpen(page, "screenOff")
        compare(tc.items(page), JSON.stringify(["30 min", "10 min", "5 min", "2 min", "1 min", "off"]))
        router.dispatch("up", false)
        compare(fakeSettings.screenOffSeconds, 600)
        router.dispatch("up", false)
        compare(fakeSettings.screenOffSeconds, 1800)
        compare(tc.rowOf(page, "screenOff").value, "30 min")
        router.dispatch("up", false)
        compare(fakeSettings.screenOffSeconds, 0)
        compare(tc.rowOf(page, "screenOff").value, "off")
        router.dispatch("up", false)
        compare(fakeSettings.screenOffSeconds, 60)
        verify(router.dispatch("a", false))
        compare(tc.rowOf(page, "screenOff").value, "1 min")
        compare(tc.rowOf(page, "suspend").value, "10 min")
        tc.sheetOpen(page, "suspend")
        compare(tc.items(page), JSON.stringify(["60 min", "30 min", "10 min", "5 min", "off"]))
        router.dispatch("up", false)
        compare(fakeSettings.suspendMinutes, 30)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(fakeSettings.suspendMinutes, 0)
        verify(router.dispatch("a", false))
        compare(tc.rowOf(page, "suspend").value, "off")
        compare(tc.keys(page).indexOf("pocket"), -1)
        compare(tc.rowOf(page, "wifi").value, "keep")
        tc.focusKey(page, "wifi")
        verify(router.dispatch("a", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.wifiOffScreenOff, true)
        compare(tc.rowOf(page, "wifi").value, "off")
    }

    function test_about_rows() {
        const page = open()
        tc.focusKey(page, "about")
        const about = tc.keyIndex(page, "about")
        compare(page.rows[about].value, Env.version)
        compare(page.rows.length, tc.expectedKeys[4].length)
        verify(router.dispatch("a", false))
        compare(page.aboutOpen, true)
        compare(page.editing, false)
        compare(fakePower.updateReloads, 1)
        const list = tc.keys(page)
        compare(JSON.stringify(list.slice(about + 1)), JSON.stringify(["image", "update", "track", "pcm"]))
        compare(tc.rowOf(page, "image").title, "image")
        compare(tc.rowOf(page, "image").value, "testing-38784f1")
        compare(tc.rowOf(page, "update").title, "system update")
        compare(tc.rowOf(page, "update").kind, "info")
        compare(tc.rowOf(page, "update").value, "up to date")
        compare(tc.rowOf(page, "track").value, "44100 hz 16 bit")
        compare(tc.rowOf(page, "pcm").title, "output")
        compare(tc.rowOf(page, "pcm").value, "44100 S24_LE")
        tc.absent(page, "drift")
        tc.absent(page, "armada")
        const track = tc.itemOf(page, "track")
        compare(track.titleItem.font.pixelSize, Ui.fontPx(11))
        compare(track.titleItem.font.styleName, "Light")
        compare(track.titleItem.font.capitalization, Font.AllUppercase)
        compare(track.valueItem.font.capitalization, Font.AllUppercase)
        const update = tc.itemOf(page, "update")
        compare(update.titleItem.font.pixelSize, Ui.fontPx(11))
        compare(update.titleItem.font.styleName, "Light")
        compare(update.valueItem.font.capitalization, Font.AllUppercase)
        router.dispatch("down", false)
        compare(page.focusIndex, about + 1)
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        fakePlayer.current = ({ id: 4, title: "x", sampleRate: 44100, bitDepth: 0 })
        compare(tc.rowOf(page, "track").value, "44100 hz")
        fakeAudio.pcmFormat = ""
        compare(page.indexOfKey("pcm"), -1)
        tc.focusKey(page, "about")
        router.dispatch("a", false)
        compare(page.rows.length, tc.expectedKeys[4].length)
        compare(fakePower.updateReloads, 1)
    }

    function test_system_update_values() {
        const page = open()
        tc.focusKey(page, "about")
        router.dispatch("a", false)
        compare(tc.rowOf(page, "update").value, "up to date")
        fakePower.updateStatus = "available"
        fakePower.updateVersion = "testing-0628a41"
        compare(tc.rowOf(page, "update").value, "testing-0628a41 available")
        fakePower.updateVersion = ""
        compare(tc.rowOf(page, "update").value, "available")
        fakePower.updateStatus = "offline"
        compare(tc.rowOf(page, "update").value, "offline")
        fakePower.updateStatus = "unchecked"
        compare(tc.rowOf(page, "update").value, "not checked")
        fakePower.updateStatus = ""
        compare(tc.rowOf(page, "update").value, "not checked")
    }

    function test_about_without_image_version() {
        fakePower.imageVersion = ""
        const page = open()
        tc.focusKey(page, "about")
        router.dispatch("a", false)
        const about = tc.keyIndex(page, "about")
        compare(page.indexOfKey("image"), -1)
        compare(page.rows[about + 1].key, "update")
        const bare = open({ power: null })
        tc.focusKey(bare, "about")
        router.dispatch("a", false)
        compare(bare.indexOfKey("image"), -1)
        compare(bare.indexOfKey("update"), -1)
        compare(bare.rows[tc.keyIndex(bare, "about") + 1].key, "track")
    }

    function test_boot_into_row() {
        const page = open()
        compare(tc.rowOf(page, "boot").title, "boot into")
        compare(tc.rowOf(page, "boot").kind, "row")
        compare(tc.rowOf(page, "boot").value, "steam")
        compare(tc.rowOf(page, "boot").readOnly, false)
        const item = tc.itemOf(page, "boot")
        compare(item.titleItem.font.pixelSize, Ui.fontPx(17))
        compare(item.valueItem.font.pixelSize, Ui.fontPx(11))
        compare(item.valueItem.text, "steam")
    }

    function test_boot_into_draft_edit() {
        const page = open()
        tc.sheetOpen(page, "boot")
        compare(tc.items(page), JSON.stringify(["microdap", "microemu", "steam"]))
        compare(page.sheet.mark, 2)
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "microemu")
        compare(fakeSettings.bootSession, "steam")
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        compare(tc.focused(page), "steam")
        verify(router.dispatch("up", false))
        compare(fakeSettings.bootSession, "steam")
        verify(router.dispatch("a", false))
        compare(fakeSettings.bootSession, "microemu")
        compare(tc.rowOf(page, "boot").value, "microemu")
        tc.sheetOpen(page, "boot")
        verify(router.dispatch("down", false))
        compare(tc.focused(page), "steam")
        verify(router.dispatch("b", false))
        compare(page.valueOpen, false)
        compare(fakeSettings.bootSession, "microemu")
        compare(tc.rowOf(page, "boot").value, "microemu")
        compare(pages.depth, 2)
    }

    function test_boot_into_choices_read_at_edit() {
        const page = open()
        fakeSettings.sessionChoices = ["microdap", "steam"]
        tc.sheetOpen(page, "boot")
        compare(tc.items(page), JSON.stringify(["microdap", "steam"]))
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.bootSession, "microdap")
    }

    function test_boot_into_missing_session() {
        fakeSettings.bootSession = "handheld-desktop-session"
        const page = open()
        compare(tc.rowOf(page, "boot").value, "handheld-desktop-session (not available)")
        tc.sheetOpen(page, "boot")
        compare(page.sheet.mark, -1)
        compare(tc.focused(page), "microdap")
        verify(router.dispatch("down", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.bootSession, "microemu")
    }

    function test_boot_into_without_choices() {
        fakeSettings.sessionChoices = []
        const page = open()
        compare(tc.rowOf(page, "boot").value, "steam")
        compare(tc.rowOf(page, "boot").readOnly, true)
        tc.focusKey(page, "boot")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(router.dispatch("right", false), false)
        fakeSettings.sessionChoices = ["microdap", "steam"]
        fakeSettings.available = false
        fakeSettings.available = true
        compare(tc.rowOf(page, "boot").readOnly, false)
        verify(router.dispatch("a", false))
        compare(page.valueOpen, true)
        compare(page.sheet.title, "boot into")
    }

    function test_boot_into_hidden_without_settings_key() {
        const page = open({ settings: null })
        tc.absent(page, "boot")
        compare(JSON.stringify(tc.allKeys(page)[4]), JSON.stringify(["about"]))
    }

    function test_boot_into_overlay() {
        const page = open({ overlay: true, bridge: fakeBridge })
        tc.sheetOpen(page, "boot")
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(fakeSettings.bootSession, "microemu")
    }

    function test_boot_into_edit_dropped_on_screen_off() {
        const page = open()
        tc.sheetOpen(page, "boot")
        router.dispatch("up", false)
        page.screenOff = true
        compare(page.valueOpen, false)
        compare(fakeSettings.bootSession, "steam")
        compare(tc.rowOf(page, "boot").value, "steam")
    }

    function test_touch_boot_into_item() {
        const page = open()
        tc.tapRow(page, "boot")
        compare(page.valueOpen, true)
        page.sheet.complete()
        tc.tapItem(page, 1)
        compare(page.valueOpen, false)
        compare(fakeSettings.bootSession, "microemu")
    }

    function test_value_change_f7() {
        const page = open({ animated: true })
        const row = tc.rowItem(page, 0)
        compare(row.shownValue, "auto")
        router.dispatch("a", false)
        compare(page.valueOpen, true)
        page.sheet.complete()
        router.dispatch("down", false)
        router.dispatch("a", false)
        const next = tc.rowItem(page, 0)
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
        router.dispatch("a", false)
        router.dispatch("down", false)
        router.dispatch("a", false)
        const row = tc.rowItem(page, 0)
        compare(row.valueSwapping, false)
        compare(row.shownValue, "speaker")
        compare(row.valueItem.opacity, 1)
    }

    function test_page_lt_rt() {
        const page = open()
        verify(router.dispatch("rt", false))
        verify(page.focusIndex > 0)
        page.completeScroll()
        const top = page.rowTopOf(page.focusIndex) - page.flick.contentY
        verify(top >= 0)
        verify(top + Ui.px(43) <= page.flick.height + 0.01)
        verify(router.dispatch("lt", false))
        compare(page.focusIndex, 0)
        compare(router.dispatch("lt", false), false)
    }

    function test_b_pops() {
        const page = open()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_screen_off_settles() {
        const page = open({ animated: true, overlay: true, bridge: fakeBridge })
        tc.focusKey(page, "closePlayer")
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
        compare(first.modelData.key, "output")
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

    function test_rescan_row() {
        const page = open({ library: fakeLibrary })
        compare(tc.rowOf(page, "rescan").title, "rescan library")
        compare(tc.rowOf(page, "rescan").value, "9 songs")
        tc.focusKey(page, "rescan")
        compare(router.dispatch("right", false), false)
        compare(fakeLibrary.rescans, 0)
        verify(router.dispatch("a", false))
        compare(fakeLibrary.rescans, 1)
        fakeLibrary.scanning = true
        compare(tc.rowOf(page, "rescan").value, "scanning")
        compare(tc.itemOf(page, "rescan").shownValue, "scanning")
        verify(router.dispatch("a", false))
        compare(fakeLibrary.rescans, 1)
        fakeLibrary.songCount = 1
        fakeLibrary.scanning = false
        compare(tc.rowOf(page, "rescan").value, "1 song")
    }

    function test_rescan_without_library() {
        const page = open()
        compare(tc.rowOf(page, "rescan").value, "0 songs")
        tc.focusKey(page, "rescan")
        verify(router.dispatch("a", false))
    }

    function tapAt(item, x, y) {
        mouseClick(item, x, y)
        wait(0)
        wait(0)
    }

    function dragUp(item, x, y, distance) {
        mousePress(item, x, y)
        const steps = Math.round(distance / Ui.px(1))
        for (let i = 1; i <= steps; i++)
            mouseMove(item, x, y - distance * i / steps, -1, Qt.LeftButton)
        wait(150)
        mouseRelease(item, x, y - distance)
        wait(0)
        wait(0)
    }

    function tapHeaderWord(page) {
        tc.tapAt(page.header, Math.max(page.header.textX, 0) + Ui.px(4), page.header.textBaseline - Ui.px(4))
    }

    function tapRow(page, key, fraction) {
        tc.focusKey(page, key)
        const item = tc.itemOf(page, key)
        tc.tapAt(item, item.width * (fraction === undefined ? 0.25 : fraction), item.height / 2)
    }

    function test_touch_value_row_opens_sheet() {
        const page = open()
        tc.tapRow(page, "output")
        compare(page.valueOpen, true)
        compare(page.sheet.title, "output")
        compare(page.editing, false)
    }

    function test_touch_sheet_item_confirms() {
        const page = open()
        tc.tapRow(page, "output")
        page.sheet.complete()
        tc.tapItem(page, 1)
        compare(page.valueOpen, false)
        compare(fakeSettings.output, "speaker")
    }

    function test_touch_tap_moves_focus_and_edits() {
        const page = open()
        tc.focusKey(page, "output")
        const item = tc.itemOf(page, "hpGain")
        tc.tapAt(item, item.width * 0.25, item.height / 2)
        compare(page.focusIndex, tc.keyIndex(page, "hpGain"))
        compare(page.valueOpen, true)
        compare(page.sheet.title, "headphone gain")
    }

    function test_touch_flip_row() {
        const page = open()
        tc.tapRow(page, "toasts")
        compare(fakeSettings.overlayToasts, true)
        compare(page.valueOpen, false)
        compare(page.editing, false)
        tc.tapRow(page, "toasts")
        compare(fakeSettings.overlayToasts, false)
    }

    function test_touch_header_reverts_edit() {
        const page = open()
        tc.tapRow(page, "accent")
        compare(page.editing, true)
        tc.tapRow(page, "accent", 0.75)
        compare(fakeSettings.accentHue, 0)
        tc.tapHeaderWord(page)
        compare(page.editing, false)
        compare(fakeSettings.accentHue, 350)
        compare(pages.depth, 2)
    }

    function test_touch_orientation_applies_on_confirm() {
        const page = open()
        tc.tapRow(page, "orientation")
        compare(page.valueOpen, true)
        page.sheet.complete()
        compare(fakeSettings.orientation, "portrait")
        tc.tapItem(page, 1)
        compare(page.valueOpen, false)
        compare(fakeSettings.orientation, "portrait-flipped")
    }

    function test_touch_row_listed_after_auto_rotate() {
        const page = open()
        compare(tc.keyIndex(page, "touch"), tc.keyIndex(page, "autoRotate") + 1)
        compare(tc.rowOf(page, "touch").title, "touch")
        compare(tc.rowOf(page, "touch").value, "on")
        pages.clear()
        const overlay = open({ overlay: true })
        tc.absent(overlay, "touch")
    }

    function test_touch_row_flips() {
        const page = open()
        tc.focusKey(page, "touch")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.valueOpen, false)
        compare(fakeSettings.touch, false)
        compare(tc.rowOf(page, "touch").value, "off")
        verify(router.dispatch("a", false))
        compare(fakeSettings.touch, true)
    }

    function test_touch_b_keeps_flipped_value() {
        const page = open()
        tc.focusKey(page, "touch")
        verify(router.dispatch("a", false))
        compare(fakeSettings.touch, false)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
        compare(fakeSettings.touch, false)
    }

    function test_touch_off_by_tap_disables_handler() {
        const r = tc.openInMain()
        const page = r.page
        compare(r.main.touchLayer.enabled, true)
        tc.tapRow(page, "touch")
        compare(fakeSettings.touch, false)
        compare(r.main.touchOn, false)
        compare(r.main.touchLayer.enabled, false)
        compare(page.editing, false)
        wait(Motion.touchReturnDelay + 50)
        compare(page.pressed, false)
    }

    function test_touch_sub_row_toggles_folder() {
        const page = open()
        tc.tapRow(page, "folders")
        compare(page.foldersOpen, true)
        const sub = tc.keyIndex(page, "folders") + 2
        page.focusIndex = sub
        page.ensureVisible(0)
        const item = tc.rowItem(page, sub)
        tc.tapAt(item, item.width / 2, item.height / 2)
        compare(JSON.stringify(fakeSettings.roots), JSON.stringify(["/home/micro/Music", "/run/media/micro/SD"]))
        compare(page.editing, false)
    }

    function test_touch_rescan_starts_scan() {
        const page = open({ library: fakeLibrary })
        tc.tapRow(page, "rescan")
        compare(fakeLibrary.rescans, 1)
    }

    function test_touch_info_row_does_nothing() {
        const page = open()
        tc.tapRow(page, "about")
        compare(page.aboutOpen, true)
        const about = tc.keyIndex(page, "about")
        const info = about + 1
        compare(page.rows[info].kind, "info")
        const item = tc.rowItem(page, info)
        tc.tapAt(item, item.width / 2, item.height / 2)
        compare(page.focusIndex, about)
        compare(page.aboutOpen, true)
        compare(page.editing, false)
    }

    function test_touch_drag_from_row_scrolls() {
        const page = open()
        const first = tc.itemOf(page, "hpGain")
        const y = first.mapToItem(page, 0, first.height / 2).y
        verify(y > page.flick.y && y < page.height)
        tc.dragUp(page, page.width / 2, y, Ui.px(100))
        verify(page.flick.contentY > 0)
        verify(page.isFocusable(page.focusIndex))
        const top = page.rowTopOf(page.focusIndex)
        verify(top >= page.flick.contentY - 0.5)
        verify(top + page.rowHeightOf(page.focusedRow) <= page.flick.contentY + page.flick.height + 0.5)
    }

    function test_touch_drag_in_edit_does_nothing() {
        const page = open()
        tc.tapRow(page, "accent")
        compare(page.editing, true)
        const y = page.height - Ui.px(20)
        tc.dragUp(page, page.width / 2, y, Ui.px(100))
        compare(page.flick.contentY, 0)
        compare(page.editing, true)
        compare(page.editKey, "accent")
        compare(page.focusIndex, tc.keyIndex(page, "accent"))
    }
}
