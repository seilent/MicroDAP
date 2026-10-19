pragma ComponentBehavior: Bound
import QtQuick
import "EqFormat.js" as EqFormat

Page {
    id: page

    property QtObject settings: null
    property QtObject power: null
    property QtObject player: null
    property QtObject audioInfo: null
    property QtObject outputControl: null
    property QtObject library: null
    property QtObject rgbControl: null
    property QtObject bridge: null
    property QtObject sensor: null
    property QtObject outputMode: null
    property QtObject dacEq: null
    property var eq: null
    property bool overlay: false
    property bool animated: true
    property bool screenOff: false
    property bool foldersOpen: false
    property bool aboutOpen: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property bool editing: false
    property string editKey: ""
    property var editOriginal: null
    property var bootChoices: []
    property int eqRevision: 0
    property var folders: []
    property var shownValues: ({})

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property bool usbPresent: page.outputControl !== null && page.outputControl.usbPresent === true
    readonly property bool usbActive: page.outputControl !== null && page.outputControl.activeOutput === "usb"
    readonly property var table: Env.settingsRows
    readonly property var pivots: page.buildPivots()
    readonly property string shownName: page.pivots[page.shownPivot] === undefined ? "" : page.pivots[page.shownPivot]
    readonly property var outputs: page.usbPresent ? page.valuesOf("output") : page.valuesOf("output").filter((v) => v !== "usb")
    readonly property var codecs: page.valuesOf("codec")
    readonly property var hpGains: page.valuesOf("hpGain")
    readonly property var hpAmps: page.valuesOf("hpAmp")
    readonly property var spkGainSteps: page.valuesOf("spkGain")
    readonly property var volumeSteps: page.valuesOf("volumeStep")
    readonly property var rgbModes: page.valuesOf("rgb")
    readonly property var saverSteps: page.valuesOf("saver")
    readonly property var screenOffSteps: page.valuesOf("screenOff")
    readonly property var suspendSteps: page.valuesOf("suspend")
    readonly property var scaleSteps: page.buildScaleSteps()
    readonly property var orientations: page.valuesOf("orientation")
    readonly property var levels: page.valuesOf("level")
    readonly property var builtInRates: page.valuesOf("rate").slice(1).map((r) => Number(r))
    readonly property var roots: page.settings !== null && page.settings.roots !== undefined ? page.settings.roots : []
    readonly property string output: page.settings !== null && page.settings.output !== undefined ? String(page.settings.output) : "auto"
    readonly property string codec: page.settings !== null && page.settings.btCodec !== undefined ? String(page.settings.btCodec) : "auto"
    readonly property string hpGain: page.settings !== null && page.settings.hpGain !== undefined ? String(page.settings.hpGain) : "normal"
    readonly property string hpAmp: page.settings !== null && page.settings.hpAmp !== undefined ? String(page.settings.hpAmp) : "lohifi"
    readonly property int spkGain: page.settings !== null && page.settings.spkGain !== undefined ? Number(page.settings.spkGain) : 102
    readonly property int volumeStep: page.settings !== null && page.settings.volumeStep !== undefined ? Number(page.settings.volumeStep) : 5
    readonly property int maxVolume: page.settings !== null && page.settings.maxVolume !== undefined ? Number(page.settings.maxVolume) : 100
    readonly property int powerOnVolume: page.settings !== null && page.settings.powerOnVolume !== undefined ? Number(page.settings.powerOnVolume) : 0
    readonly property string outputSlot: page.settings !== null && page.settings.outputSlot !== undefined ? String(page.settings.outputSlot) : ""
    readonly property bool crossfeedSlot: page.outputSlot === "headphones" || page.outputSlot === "usb"
    readonly property int balance: page.settings !== null && page.settings.balance !== undefined ? Number(page.settings.balance) : 0
    readonly property string crossfeed: page.settings !== null && page.settings.crossfeed !== undefined ? String(page.settings.crossfeed) : "off"
    readonly property string rgb: page.settings !== null && page.settings.rgb !== undefined ? String(page.settings.rgb) : "system"
    readonly property bool rgbSupported: !page.overlay && page.rgbControl !== null && page.rgbControl.supported === true
    readonly property string rgbValue: page.rgbSupported ? page.rgb : "unsupported"
    readonly property bool overlayToasts: page.settings !== null && page.settings.overlayToasts === true
    readonly property string orientation: page.settings !== null && page.orientations.indexOf(String(page.settings.orientation)) >= 0
        ? String(page.settings.orientation) : "portrait"
    readonly property bool autoRotate: page.settings !== null && page.settings.autoRotate === true
    readonly property bool touchEnabled: page.settings === null || page.settings.touch !== false
    readonly property string touchValue: page.touchEnabled ? "on" : "off"
    readonly property bool sensorSupported: !page.overlay && page.sensor !== null && page.sensor.supported === true
    readonly property bool autoFollowing: page.autoRotate && page.sensorSupported && Ui.orientationActive
    readonly property string effectiveOrientation: page.orientations[[0, 2, Ui.landscapeTurn, (Ui.landscapeTurn + 2) % 4].indexOf(Ui.turn)]
    readonly property string orientationValue: !Ui.orientationActive ? "external display"
        : (page.autoFollowing ? page.effectiveOrientation
        : page.orientation).replace("-", " ")
    readonly property string autoRotateValue: !page.sensorSupported ? "unsupported"
        : page.autoRotate ? "on" : "off"
    readonly property real savedScale: page.settings !== null && page.settings.scale !== undefined ? Number(page.settings.scale) : 1.0
    readonly property bool jackKnown: page.outputControl !== null && page.outputControl.jackPresent !== undefined
    readonly property bool jackPresent: !page.jackKnown || page.outputControl.jackPresent === true
    readonly property int hwRate: page.audioInfo !== null && page.audioInfo.hwRate !== undefined ? Number(page.audioInfo.hwRate) : 0
    readonly property string outputValue: page.output === "headphones" && !page.jackPresent ? "no jack"
        : page.output === "usb" && !page.usbPresent ? "usb (not present)" : page.output
    readonly property string hpGainValue: page.usbActive ? "built in only" : page.hpGain
    readonly property string hpAmpValue: page.usbActive ? "built in only" : page.hpAmp
    readonly property string outputLevel: page.settings !== null && page.levels.indexOf(String(page.settings.outputLevel)) >= 0
        ? String(page.settings.outputLevel) : "off"
    readonly property string sampleRate: page.settings !== null && page.settings.sampleRate !== undefined ? String(page.settings.sampleRate) : "auto"
    readonly property bool levelEffective: page.outputMode !== null && page.outputMode.effective === true
    readonly property string chainNote: page.levelEffective ? " (not on bit perfect)" : ""
    readonly property string levelStatus: page.outputMode !== null && page.outputMode.levelStatus !== undefined ? String(page.outputMode.levelStatus) : ""
    readonly property string levelValue: page.overlay ? "dap only"
        : page.outputLevel === "off" ? "off"
        : !page.usbActive ? "usb only"
        : page.levelStatus !== "" ? page.levelStatus : page.levelWord(page.outputLevel)
    readonly property var rates: page.outputMode !== null && page.outputMode.rates !== undefined && page.outputMode.rates.length > 0
        ? page.outputMode.rates : page.builtInRates
    readonly property var rateChoices: ["auto"].concat(page.rates.map((r) => String(r)))
    readonly property bool rateReadOnly: page.overlay || (page.outputMode !== null && page.outputMode.rateReadOnly === true)
    readonly property string rateValue: page.overlay ? "dap only"
        : page.rateReadOnly ? page.autoText(true)
        : page.rateText(page.sampleRate)
    readonly property bool hasEq: page.eq !== null && page.eq !== undefined
    readonly property string eqOutput: page.hasEq && page.eq.activeOutput !== undefined ? String(page.eq.activeOutput) : ""
    readonly property string dacStatus: page.overlay || page.dacEq === null ? "" : String(page.dacEq.status)
    readonly property string eqValue: page.dacStatus === "failed" && page.eqOutput === "usb" ? "dac eq failed"
        : !page.levelEffective ? page.eqText(page.eqOutput, page.eqRevision)
        : page.dacStatus === "" || page.dacStatus === "none" || page.dacStatus === "unsupported" ? "bypassed"
        : page.dacEqText(page.eqOutput, page.eqRevision)
    readonly property int accentHue: page.settings !== null && page.settings.accentHue !== undefined ? Ui.hueOnGrid(Number(page.settings.accentHue)) : Ui.accentHueDefault
    readonly property bool accentFromArt: page.settings !== null && page.settings.accentFromArt === true
    readonly property string playThrough: page.settings !== null && page.settings.playThrough !== undefined ? String(page.settings.playThrough) : "off"
    readonly property string resume: page.settings !== null && page.settings.resume !== undefined ? String(page.settings.resume) : "position"
    readonly property bool autoNowPlaying: page.settings !== null && page.settings.autoNowPlaying === true
    readonly property string clockMode: page.settings !== null && page.settings.clock !== undefined ? String(page.settings.clock) : "24h"
    readonly property bool batteryPercent: page.settings !== null && page.settings.batteryPercent === true
    readonly property bool quickplay: page.settings === null || page.settings.quickplay !== false
    readonly property bool saverLyrics: page.settings !== null && page.settings.saverLyrics === true
    readonly property int saverSeconds: page.settings !== null && page.settings.saverSeconds !== undefined ? page.settings.saverSeconds : 30
    readonly property int screenOffSeconds: page.settings !== null && page.settings.screenOffSeconds !== undefined ? page.settings.screenOffSeconds : 300
    readonly property int suspendMinutes: page.settings !== null && page.settings.suspendMinutes !== undefined ? page.settings.suspendMinutes : 10
    readonly property bool wifiOffScreenOff: page.settings !== null && page.settings.wifiOffScreenOff === true
    readonly property bool hasBoot: page.settings !== null && page.settings.bootSession !== undefined
    readonly property string bootSession: page.hasBoot ? String(page.settings.bootSession) : ""
    readonly property bool bootReadOnly: page.bootChoices.length === 0
    readonly property string bootValue: page.bootText(page.bootSession)
    readonly property bool scanning: page.library !== null && page.library.scanning === true
    readonly property int songCount: page.library !== null && page.library.songCount !== undefined ? Number(page.library.songCount) : 0
    readonly property string rescanValue: page.scanning ? "scanning" : page.songCount + (page.songCount === 1 ? " song" : " songs")
    readonly property var current: page.player !== null && page.player.current !== undefined && page.player.current !== null ? page.player.current : null
    readonly property string pcmFormat: page.audioInfo !== null && page.audioInfo.pcmFormat !== undefined ? String(page.audioInfo.pcmFormat) : ""
    readonly property string imageVersion: page.power !== null && page.power.imageVersion !== undefined ? String(page.power.imageVersion) : ""
    readonly property bool hasUpdate: page.power !== null && page.power.updateStatus !== undefined
    readonly property string updateValue: page.hasUpdate ? page.updateText(String(page.power.updateStatus), String(page.power.updateVersion)) : ""
    readonly property string version: Env.version
    readonly property var rows: page.buildRows()
    readonly property var rowTops: page.buildTops(page.rows)
    readonly property real contentHeight: page.rows.length === 0 ? 0 : page.rowTops[page.rows.length - 1] + page.rowHeightOf(page.rows[page.rows.length - 1])
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
    readonly property real focusY: page.rowTopOf(page.focusIndex) - flick.contentY
    readonly property real focusHeight: page.focusedRow === null ? 0 : page.rowHeightOf(page.focusedRow)
    readonly property bool scrolling: scrollAnim.running
    readonly property bool switching: pivotAnim.running
    readonly property bool valueOpen: d.valueKey !== ""
    readonly property var watchedValue: page.watchOf(page.valueOpen, d.valueKey, page.editing, page.editKey)
    readonly property string valueItems: page.valueOpen ? JSON.stringify(page.valueWords(d.valueKey, page.sheetValues(d.valueKey))) : ""
    property alias header: header
    property alias strip: strip
    property alias flick: flick
    property alias focusBar: focusBar
    property alias sheet: sheet
    property alias repeater: repeater

    signal pairPhone()
    signal openEq()
    signal closePlayerRequested()
    signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve, int from)
    signal settle()

    function firstVisible(): int {
        return page.rowAtY(flick.contentY)
    }

    function feather(dir: int): void {
        page.entrance(dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true, page.firstVisible())
    }

    function refreshFrom(index: int): void {
        if (!page.live)
            return
        d.pendingFrom = d.pendingFrom < 0 ? index : Math.min(d.pendingFrom, index)
        Qt.callLater(page.runRefresh)
    }

    function runRefresh(): void {
        const from = d.pendingFrom
        d.pendingFrom = -1
        if (from < 0 || !page.live)
            return
        page.entrance(Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false,
            Math.max(from, page.firstVisible()))
    }

    function buildScaleSteps(): var {
        const r = page.tableRow("scale")
        const out = []
        for (let s = r.min; s <= Math.min(r.max, Ui.sMax) + 1e-6; s += r.step)
            out.push(Math.round(s * 10) / 10)
        return out
    }

    function minutes(seconds: int): string {
        return seconds < 60 ? seconds + " s" : Math.round(seconds / 60) + " min"
    }

    function offOr(value: int, unit: string): string {
        return value <= 0 ? "off" : value + " " + unit
    }

    function spkGainText(step: int): string {
        const db = step - 84
        return (db > 0 ? "+" + db : String(db)) + " db"
    }

    function updateText(status: string, version: string): string {
        switch (status) {
        case "available":
            return version !== "" ? version + " available" : "available"
        case "none":
            return "up to date"
        case "offline":
            return "offline"
        default:
            return "not checked"
        }
    }

    function bootText(name: string): string {
        if (page.bootChoices.length === 0 || page.bootChoices.indexOf(name) >= 0)
            return name
        return name + " (not available)"
    }

    function refreshBootChoices(): void {
        page.bootChoices = page.hasBoot && typeof page.settings.bootSessions === "function" ? page.settings.bootSessions() : []
    }

    function trackLine(): string {
        if (page.current === null || page.current.sampleRate === undefined || Number(page.current.sampleRate) <= 0)
            return ""
        const depth = page.current.bitDepth === undefined ? 0 : Number(page.current.bitDepth)
        return Number(page.current.sampleRate) + " hz" + (depth > 0 ? " " + depth + " bit" : "")
    }

    function eqText(output: string, revision: int): string {
        if (!page.hasEq || output === "" || typeof page.eq.profileMap !== "function")
            return "off"
        return EqFormat.profileText(page.eq.profileMap(output))
    }

    function dacEqText(output: string, revision: int): string {
        const text = page.eqText(output, revision)
        if (page.dacStatus !== "on" && page.dacStatus !== "applying" || !page.hasEq || typeof page.eq.profileMap !== "function")
            return text
        return EqFormat.listed(page.eq.profileMap(output)) ? text + " (dac)" : text
    }

    function powerOnText(value: int): string {
        return value <= 0 ? "last" : String(value)
    }

    function balanceText(value: int): string {
        return value === 0 ? "center" : (value < 0 ? "l " : "r ") + Math.abs(value) / 2 + " db"
    }

    function crossfeedWord(mode: string): string {
        switch (mode) {
        case "chuMoy":
            return "chu moy"
        case "janMeier":
            return "jan meier"
        default:
            return mode
        }
    }

    function levelWord(level: string): string {
        return level === "bitperfect" ? "on" : level
    }

    function autoText(live: bool): string {
        return live && page.hwRate > 0 ? "auto " + page.hwRate : "auto"
    }

    function rateText(rate: string): string {
        if (rate === "auto")
            return page.autoText(page.sampleRate === "auto")
        return page.rateChoices.indexOf(rate) >= 0 ? rate : rate + " (not supported)"
    }

    function tableRow(key: string): var {
        for (const r of page.table) {
            if (r.key === key)
                return r
        }
        return null
    }

    function valuesOf(key: string): var {
        const r = page.tableRow(key)
        return r === null ? [] : r.values
    }

    function buildPivots(): var {
        const out = []
        for (const r of page.table) {
            if (out.indexOf(r.pivot) < 0)
                out.push(r.pivot)
        }
        return out
    }

    function ruleOn(rule: string): bool {
        switch (rule) {
        case "always":
            return true
        case "usbActive":
            return page.usbActive
        case "overlay":
            return page.overlay
        case "rate":
            return page.rateReadOnly
        case "orientation":
            return !Ui.orientationActive || page.autoFollowing
        case "sensor":
            return !page.sensorSupported
        case "rgb":
            return !page.rgbSupported
        case "boot":
            return page.bootReadOnly
        case "slot":
            return page.outputSlot === ""
        case "crossfeed":
            return !page.crossfeedSlot
        default:
            return false
        }
    }

    function shownOn(rule: string): bool {
        switch (rule) {
        case "dap":
            return !page.overlay
        case "overlay":
            return page.overlay
        case "boot":
            return page.hasBoot
        default:
            return true
        }
    }

    function valueOf(key: string): string {
        switch (key) {
        case "output":
            return page.outputValue
        case "hpGain":
            return page.hpGainValue
        case "hpAmp":
            return page.hpAmpValue
        case "spkGain":
            return page.spkGainText(page.spkGain)
        case "volumeStep":
            return page.volumeStep + "%"
        case "maxVolume":
            return String(page.maxVolume)
        case "powerOnVolume":
            return page.powerOnText(page.powerOnVolume)
        case "balance":
            return page.balanceText(page.balance) + (page.outputSlot === "" ? "" : page.chainNote)
        case "crossfeed":
            return !page.crossfeedSlot ? "headphones only" : page.crossfeedWord(page.crossfeed) + page.chainNote
        case "codec":
            return page.codec
        case "level":
            return page.levelValue
        case "rate":
            return page.rateValue
        case "replaygain":
            return "off"
        case "eq":
            return page.eqValue
        case "pair":
            return page.overlay ? "pair in dap mode" : ""
        case "folders":
            return page.roots.length + " on"
        case "rescan":
            return page.rescanValue
        case "accentArt":
            return page.accentFromArt ? "on" : "off"
        case "playThrough":
            return page.playThrough
        case "resume":
            return page.resume
        case "autoNowPlaying":
            return page.autoNowPlaying ? "on" : "off"
        case "orientation":
            return page.orientationValue
        case "autoRotate":
            return page.autoRotateValue
        case "touch":
            return page.touchValue
        case "scale":
            return Ui.userScale.toFixed(1)
        case "saver":
            return page.minutes(page.saverSeconds)
        case "saverLyrics":
            return page.saverLyrics ? "on" : "off"
        case "clock":
            return page.clockMode
        case "batteryPercent":
            return page.batteryPercent ? "on" : "off"
        case "quickplay":
            return page.quickplay ? "on" : "off"
        case "rgb":
            return page.rgbValue
        case "toasts":
            return page.overlayToasts ? "on" : "off"
        case "screenOff":
            return page.offOr(Math.round(page.screenOffSeconds / 60), "min")
        case "suspend":
            return page.offOr(page.suspendMinutes, "min")
        case "wifi":
            return page.wifiOffScreenOff ? "off" : "keep"
        case "boot":
            return page.bootValue
        case "about":
            return page.version
        default:
            return ""
        }
    }

    function row(spec: var): var {
        return {
            kind: "row", key: spec.key, title: spec.title, value: page.valueOf(spec.key),
            readOnly: spec.kind === "action" || page.ruleOn(spec.readOnly), spec: spec
        }
    }

    function info(key: string, title: string, value: string): var {
        return { kind: "info", key: key, title: title, value: value, readOnly: true }
    }

    function buildRows(): var {
        const out = []
        for (const spec of page.table) {
            if (spec.pivot !== page.shownName || !page.shownOn(spec.shown))
                continue
            out.push(page.row(spec))
            if (spec.key === "folders" && page.foldersOpen) {
                for (const f of page.folders)
                    out.push({ kind: "sub", key: "root:" + f, title: f, value: page.roots.indexOf(f) >= 0 ? "on" : "off", readOnly: true, path: f })
            }
            if (spec.key === "about" && page.aboutOpen) {
                if (page.imageVersion !== "")
                    out.push(page.info("image", "image", page.imageVersion))
                if (page.hasUpdate)
                    out.push(page.info("update", "system update", page.updateValue))
                const track = page.trackLine()
                if (track !== "")
                    out.push(page.info("track", "track", track))
                if (page.pcmFormat !== "")
                    out.push(page.info("pcm", "output", page.pcmFormat))
            }
        }
        return out
    }

    function rowHeightOf(r: var): real {
        return Ui.px(Ui.settingsPitch)
    }

    function buildTops(list: var): var {
        const tops = []
        let y = 0
        for (const r of list) {
            tops.push(y)
            y += page.rowHeightOf(r)
        }
        return tops
    }

    function rowTopOf(index: int): real {
        const t = page.rowTops[index]
        return t === undefined ? 0 : t
    }

    function indexOfKey(key: string): int {
        for (let i = 0; i < page.rows.length; i++) {
            if (page.rows[i].key === key)
                return i
        }
        return -1
    }

    function isFocusable(index: int): bool {
        const r = page.rows[index]
        return r !== undefined && r !== null
    }

    function pivotOfKey(key: string): int {
        const r = page.tableRow(key)
        if (r !== null)
            return page.pivots.indexOf(r.pivot)
        if (key.startsWith("root:"))
            return page.pivots.indexOf(page.tableRow("folders").pivot)
        if (["image", "update", "track", "pcm"].indexOf(key) >= 0)
            return page.pivots.indexOf(page.tableRow("about").pivot)
        return -1
    }

    function focusKey(key: string): bool {
        const pivot = page.pivotOfKey(key)
        if (pivot >= 0 && pivot !== page.pivotIndex) {
            page.cancelEdit()
            page.closeSheet()
            d.internal = true
            page.pivotIndex = pivot
            d.internal = false
            page.landPivot()
        }
        const index = page.indexOfKey(key)
        if (index < 0)
            return false
        page.focusIndex = index
        page.keepFocusVisible()
        return true
    }

    function savePivot(): void {
        const r = page.focusedRow
        d.spots[page.shownPivot] = { key: r === null ? "" : r.key, index: page.focusIndex, y: scrollAnim.running ? scrollAnim.to : flick.contentY }
    }

    function restorePivot(): void {
        scrollAnim.stop()
        touchScroller.stop()
        const spot = d.spots[page.shownPivot]
        if (spot === undefined) {
            page.focusIndex = Math.max(0, page.nearestFocusable(0, 1))
            flick.contentY = 0
            return
        }
        const at = page.indexOfKey(spot.key)
        page.focusIndex = at >= 0 ? at : Math.max(0, Math.min(spot.index, page.rows.length - 1))
        page.fixFocus()
        flick.contentY = page.visibleY(page.focusIndex, spot.y)
    }

    function landPivot(): void {
        pivotAnim.stop()
        flick.x = 0
        flick.opacity = 1
        page.settle()
        if (page.shownPivot === page.pivotIndex)
            return
        page.savePivot()
        page.shownPivot = page.pivotIndex
        page.restorePivot()
    }

    function switchPivot(delta: int): bool {
        const target = Math.max(0, Math.min(page.pivots.length - 1, page.pivotIndex + delta))
        if (target === page.pivotIndex)
            return false
        page.landPivot()
        page.cancelEdit()
        page.closeSheet()
        const dir = target > page.pivotIndex ? 1 : -1
        d.internal = true
        page.pivotIndex = target
        d.internal = false
        if (!page.live) {
            page.landPivot()
            return true
        }
        pivotAnim.dir = dir
        pivotAnim.start()
        return true
    }

    function nearestFocusable(from: int, dir: int): int {
        const step = dir >= 0 ? 1 : -1
        for (let i = from; i >= 0 && i < page.rows.length; i += step) {
            if (page.isFocusable(i))
                return i
        }
        return -1
    }

    function maxContentY(): real {
        return Math.max(0, page.contentHeight - flick.height)
    }

    function visibleY(index: int, fromY: real): real {
        const top = page.rowTopOf(index)
        const h = page.rows[index] === undefined ? 0 : page.rowHeightOf(page.rows[index])
        let y = fromY
        if (top < y)
            y = top
        else if (top + h > y + flick.height)
            y = top + h - flick.height
        return Math.max(0, Math.min(page.maxContentY(), y))
    }

    function scrollTo(y: real, duration: int) {
        touchScroller.stop()
        const target = Math.max(0, Math.min(page.maxContentY(), y))
        scrollAnim.stop()
        if (target === flick.contentY)
            return
        if (!page.live || duration <= 0) {
            flick.contentY = target
            return
        }
        scrollAnim.duration = duration
        scrollAnim.from = flick.contentY
        scrollAnim.to = target
        scrollAnim.start()
    }

    function ensureVisible(duration: int) {
        const base = scrollAnim.running ? scrollAnim.to : flick.contentY
        page.scrollTo(page.visibleY(page.focusIndex, base), duration)
    }

    function completeScroll() {
        if (scrollAnim.running)
            scrollAnim.complete()
    }

    function moveRow(delta: int): bool {
        const target = page.nearestFocusable(page.focusIndex + delta, delta)
        if (target < 0 || target === page.focusIndex)
            return false
        page.focusIndex = target
        page.ensureVisible(Motion.scroll)
        return true
    }

    function rowAtY(y: real): int {
        let index = 0
        for (let i = 0; i < page.rows.length; i++) {
            if (page.rowTopOf(i) <= y)
                index = i
        }
        return index
    }

    function pageRow(dir: int): bool {
        if (page.rows.length === 0)
            return false
        const base = scrollAnim.running ? scrollAnim.to : flick.contentY
        const wanted = page.rowTopOf(page.focusIndex) + dir * flick.height
        const index = wanted <= 0 ? 0 : wanted >= page.contentHeight ? page.rows.length - 1 : page.rowAtY(wanted)
        const fixed = page.nearestFocusable(index, dir)
        const target = fixed < 0 ? page.nearestFocusable(index, -dir) : fixed
        if (target < 0 || target === page.focusIndex)
            return false
        page.focusIndex = target
        page.scrollTo(page.visibleY(target, base + dir * flick.height), Motion.page)
        return true
    }

    function cycle(list: var, value: var, delta: int): var {
        let i = list.indexOf(value)
        if (i < 0) {
            let best = -1
            for (let k = 0; k < list.length; k++) {
                if (isNaN(Number(list[k])))
                    continue
                if (best < 0 || Math.abs(Number(list[k]) - Number(value)) < Math.abs(Number(list[best]) - Number(value)))
                    best = k
            }
            i = Math.max(0, best)
        }
        return list[Math.max(0, Math.min(list.length - 1, i + delta))]
    }

    function set(key: string, value: var) {
        if (page.settings === null || page.settings[key] === undefined)
            return
        page.settings[key] = value
    }

    function applyScale(s: real) {
        if (s === Math.round(Ui.userScale * 10) / 10)
            return
        const index = page.focusIndex
        page.set("scale", s)
        page.focusIndex = index
        page.completeScroll()
        flick.contentY = page.visibleY(index, flick.contentY)
    }

    function step(delta: int): bool {
        const r = page.focusedRow
        if (r === null)
            return false
        if (r.key === "balance") {
            const spec = r.spec
            const next = Math.max(spec.min, Math.min(spec.max, page.balance + delta * spec.step))
            if (next !== page.balance)
                page.write("balance", next)
            return true
        }
        if (r.key !== "accent")
            return false
        page.write("accentHue", Ui.stepHue(page.accentHue, delta))
        return true
    }

    function write(setting: string, value: var): void {
        d.ours = d.ours.concat([value])
        if (setting === "scale")
            page.applyScale(Number(value))
        else
            page.set(setting, value)
    }

    function valueList(key: string): var {
        switch (key) {
        case "output":
            return page.outputs
        case "rate":
            return page.rateChoices
        case "scale":
            return page.scaleSteps
        case "boot":
            return page.bootChoices
        default:
            return page.valuesOf(key)
        }
    }

    function sheetValues(key: string): var {
        const list = page.valueList(key)
        const numbers = list.filter((v) => v !== "auto" && !isNaN(Number(v)))
        const words = list.filter((v) => v === "auto")
        if (numbers.length === 0 || numbers.length + words.length !== list.length)
            return list.slice()
        return numbers.slice().sort((a, b) => Number(b) - Number(a)).concat(words)
    }

    function wordOf(key: string, value: var): string {
        switch (key) {
        case "output":
            return value === "headphones" && !page.jackPresent ? "no jack" : String(value)
        case "spkGain":
            return page.spkGainText(Number(value))
        case "volumeStep":
            return value + "%"
        case "level":
            return page.levelWord(String(value))
        case "crossfeed":
            return page.crossfeedWord(String(value))
        case "powerOnVolume":
            return page.powerOnText(Number(value))
        case "orientation":
            return String(value).replace("-", " ")
        case "scale":
            return Number(value).toFixed(1)
        case "saver":
            return page.minutes(Number(value))
        case "screenOff":
            return page.offOr(Math.round(Number(value) / 60), "min")
        case "suspend":
            return page.offOr(Number(value), "min")
        default:
            return String(value)
        }
    }

    function valueWords(key: string, values: var): var {
        return values.map((v) => page.wordOf(key, v))
    }

    function liveOf(key: string): var {
        return key === "scale" ? Math.round(Ui.userScale * 10) / 10 : page.currentOf(key)
    }

    function openValue(key: string): void {
        const spec = page.tableRow(key)
        if (key === "boot")
            page.refreshBootChoices()
        const values = page.sheetValues(key)
        if (spec === null || values.length === 0)
            return
        const live = page.liveOf(key)
        const mark = values.findIndex((v) => String(v) === String(live))
        const near = mark >= 0 ? mark : values.findIndex((v) => String(v) === String(page.cycle(values, live, 0)))
        d.valueValues = values
        d.valueDraft = spec.draft
        d.valueOriginal = page.currentOf(key)
        d.ours = [d.valueOriginal]
        d.valueItems = JSON.stringify(page.valueWords(key, values))
        d.valueKey = key
        d.riseKept = ""
        sheet.title = spec.title
        sheet.items = page.valueWords(key, values)
        sheet.open()
        sheet.mark = mark
        sheet.wrap = !spec.bounded
        sheet.focusIndex = Math.max(0, near)
    }

    function endValue(): void {
        d.valueKey = ""
        d.riseKept = ""
        d.valueValues = []
        d.ours = []
        if (sheet.shown)
            sheet.close()
    }

    function previewValue(index: int): void {
        if (!page.valueOpen || d.valueDraft || index < 0 || index >= d.valueValues.length)
            return
        page.write(page.settingOf(d.valueKey), d.valueValues[index])
    }

    function acceptValue(index: int): void {
        const key = d.valueKey
        const value = d.valueValues[index]
        const draft = d.valueDraft
        page.endValue()
        if (value === undefined)
            return
        if (draft || String(page.liveOf(key)) !== String(value))
            page.write(page.settingOf(key), value)
        d.ours = []
    }

    function cancelValue(): void {
        if (!page.valueOpen)
            return
        const key = d.valueKey
        const original = d.valueOriginal
        const draft = d.valueDraft
        page.endValue()
        if (!draft && String(page.currentOf(key)) !== String(original))
            page.write(page.settingOf(key), original)
        d.ours = []
    }

    function dismissValue(): void {
        if (!page.valueOpen)
            return
        if (d.valueDraft)
            page.cancelValue()
        else
            page.endValue()
    }

    function closeSheet(): void {
        if (page.valueOpen)
            page.cancelValue()
        else if (sheet.shown)
            sheet.close()
    }

    function watchOf(open: bool, valueKey: string, editing: bool, editKey: string): var {
        return open ? page.currentOf(valueKey) : editing ? page.currentOf(editKey) : undefined
    }

    function flip(key: string): void {
        page.set(page.settingOf(key), !page.currentOf(key))
    }

    function settingOf(key: string): string {
        const r = page.tableRow(key)
        return r === null ? "" : r.setting
    }

    function currentOf(key: string): var {
        switch (key) {
        case "output":
            return page.output
        case "hpGain":
            return page.hpGain
        case "hpAmp":
            return page.hpAmp
        case "spkGain":
            return page.spkGain
        case "volumeStep":
            return page.volumeStep
        case "maxVolume":
            return page.maxVolume
        case "powerOnVolume":
            return page.powerOnVolume
        case "balance":
            return page.balance
        case "crossfeed":
            return page.crossfeed
        case "codec":
            return page.codec
        case "level":
            return page.outputLevel
        case "rate":
            return page.sampleRate
        case "accent":
            return page.accentHue
        case "accentArt":
            return page.accentFromArt
        case "playThrough":
            return page.playThrough
        case "resume":
            return page.resume
        case "autoNowPlaying":
            return page.autoNowPlaying
        case "scale":
            return page.savedScale
        case "saver":
            return page.saverSeconds
        case "saverLyrics":
            return page.saverLyrics
        case "clock":
            return page.clockMode
        case "batteryPercent":
            return page.batteryPercent
        case "quickplay":
            return page.quickplay
        case "rgb":
            return page.rgb
        case "toasts":
            return page.overlayToasts
        case "screenOff":
            return page.screenOffSeconds
        case "suspend":
            return page.suspendMinutes
        case "wifi":
            return page.wifiOffScreenOff
        case "orientation":
            return page.orientation
        case "autoRotate":
            return page.autoRotate
        case "touch":
            return page.touchEnabled
        case "boot":
            return page.bootSession
        default:
            return undefined
        }
    }

    function beginEdit(key: string): void {
        page.editOriginal = page.currentOf(key)
        d.ours = [page.editOriginal]
        page.editKey = key
        page.editing = true
    }

    function confirmEdit(): void {
        page.editing = false
        page.editKey = ""
        page.editOriginal = null
        d.ours = []
    }

    function cancelEdit(): void {
        if (!page.editing)
            return
        const key = page.editKey
        const original = page.editOriginal
        page.confirmEdit()
        if (String(page.currentOf(key)) !== String(original))
            page.set(page.settingOf(key), original)
    }

    function revertOrConfirmForRotation(): void {
        page.dismissValue()
        page.confirmEdit()
    }

    function handleEdit(name: string, repeat: bool): bool {
        switch (name) {
        case "left":
            page.step(-1)
            return true
        case "right":
            page.step(1)
            return true
        case "a":
            if (!repeat)
                page.confirmEdit()
            return true
        case "b":
            if (!repeat)
                page.cancelEdit()
            return true
        default:
            return true
        }
    }

    function toggleRoot(path: string) {
        const next = page.roots.slice()
        const at = next.indexOf(path)
        if (at >= 0) {
            if (next.length === 1)
                return
            next.splice(at, 1)
        } else {
            next.push(path)
        }
        const defaults = page.settings !== null && typeof page.settings.defaultRoots === "function" ? page.settings.defaultRoots() : []
        const same = defaults.length === next.length && defaults.slice().sort().join("\n") === next.slice().sort().join("\n")
        page.set("roots", same ? [] : next)
    }

    function closePlayer(): void {
        page.closePlayerRequested()
        if (page.bridge !== null && typeof page.bridge.closePlayer === "function")
            page.bridge.closePlayer()
        else
            console.info("microdap: close player")
    }

    function activate(): bool {
        const r = page.focusedRow
        if (r === null)
            return false
        switch (r.kind) {
        case "sub":
            page.toggleRoot(r.path)
            return true
        case "info":
            return true
        default:
            break
        }
        switch (r.spec === undefined ? "" : r.spec.kind) {
        case "sheet":
            if (!r.readOnly)
                page.openValue(r.key)
            return true
        case "flip":
            if (!r.readOnly)
                page.flip(r.key)
            return true
        case "edit":
            if (!r.readOnly)
                page.beginEdit(r.key)
            return true
        default:
            break
        }
        switch (r.key) {
        case "pair":
            if (!page.overlay)
                page.pairPhone()
            return true
        case "eq":
            page.openEq()
            return true
        case "folders":
            page.foldersOpen = !page.foldersOpen
            page.refreshFrom(page.focusIndex + 1)
            return true
        case "closePlayer":
            page.endValue()
            sheet.title = "close player"
            sheet.items = ["close player"]
            sheet.open()
            return true
        case "about":
            page.aboutOpen = !page.aboutOpen
            if (page.aboutOpen && page.power !== null && typeof page.power.reloadUpdate === "function")
                page.power.reloadUpdate()
            page.refreshFrom(page.focusIndex + 1)
            return true
        case "rescan":
            if (!page.scanning && page.library !== null && typeof page.library.rescanRoots === "function")
                page.library.rescanRoots()
            return true
        default:
            return true
        }
    }

    function fixFocus() {
        if (page.rows.length === 0) {
            page.focusIndex = 0
            return
        }
        if (page.isFocusable(page.focusIndex))
            return
        const target = page.nearestFocusable(Math.min(page.focusIndex, page.rows.length - 1), 1)
        page.focusIndex = target < 0 ? page.nearestFocusable(page.rows.length - 1, -1) : target
    }

    function keepFocusVisible(): void {
        page.completeScroll()
        flick.contentY = page.visibleY(page.focusIndex, flick.contentY)
    }

    function completeAll() {
        page.landPivot()
        page.settle()
        page.completeScroll()
        sheet.complete()
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        page.stack.pop()
        return true
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (kind === "pivot")
            return "tap"
        if (page.editing) {
            if (kind === "header")
                return "b"
            if ((kind === "row" || kind === "sub") && index === page.focusIndex)
                return x < hit.width / 2 ? "left" : "right"
            return "a"
        }
        switch (kind) {
        case "row":
        case "sub":
            page.focusIndex = index
            return "a"
        case "header":
            return "b"
        default:
            return ""
        }
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (kind === "pivot") {
            page.cancelEdit()
            page.closeSheet()
            if (index !== page.pivotIndex)
                page.switchPivot(index - page.pivotIndex)
            return
        }
        if (!page.editing && (kind === "row" || kind === "sub"))
            page.ensureVisible(Motion.scroll)
    }

    function holdRise(name: string, repeat: bool): bool {
        if (repeat)
            return true
        if (name === "b" || name === "back" || name === "start")
            d.riseKept = name
        else if ((name === "up" || name === "down") && d.riseKept === "")
            d.riseKept = name
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown && page.valueOpen && sheet.animating)
            return page.holdRise(name, repeat)
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        if (name === "lb" || name === "rb") {
            if (!repeat)
                page.switchPivot(name === "lb" ? -1 : 1)
            return true
        }
        if (page.switching)
            page.landPivot()
        if (page.editing)
            return page.handleEdit(name, repeat)
        switch (name) {
        case "up":
            return page.moveRow(-1)
        case "down":
            return page.moveRow(1)
        case "lt":
            return page.pageRow(-1)
        case "rt":
            return page.pageRow(1)
        case "a":
            if (!repeat)
                page.activate()
            return true
        case "b":
            if (repeat)
                return false
            return page.back()
        default:
            return false
        }
    }

    onRowsChanged: {
        page.fixFocus()
        const r = page.rows[page.focusIndex]
        if (page.editing && (r === undefined || r === null || r.key !== page.editKey))
            page.confirmEdit()
        if (page.valueOpen) {
            const at = page.indexOfKey(d.valueKey)
            if (at < 0 || page.rows[at].readOnly)
                page.cancelValue()
        }
    }

    onWatchedValueChanged: {
        const value = page.watchedValue
        if (d.ours.some((v) => String(v) === String(value)))
            return
        if (page.valueOpen)
            page.endValue()
        else if (page.editing)
            page.confirmEdit()
    }

    onValueItemsChanged: {
        if (page.valueOpen && page.valueItems !== d.valueItems)
            page.cancelValue()
    }

    onActiveChanged: {
        if (!page.active) {
            page.dismissValue()
            page.confirmEdit()
        }
    }

    onScreenOffChanged: {
        if (page.screenOff) {
            page.dismissValue()
            page.confirmEdit()
        }
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            page.feather(1)
    }

    onPivotIndexChanged: {
        if (d.ready && !d.internal)
            page.landPivot()
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        if (page.audioInfo !== null && typeof page.audioInfo.refresh === "function")
            page.audioInfo.refresh()
        const list = page.roots.slice()
        for (const f of page.folders) {
            if (list.indexOf(f) < 0)
                list.push(f)
        }
        page.folders = list
        page.refreshBootChoices()
        page.shownPivot = page.pivotIndex
        page.focusIndex = Math.max(0, page.nearestFocusable(0, 1))
        d.ready = true
    }

    QtObject {
        id: d

        property int pendingFrom: -1
        property string valueKey: ""
        property string riseKept: ""
        property var valueValues: []
        property var valueOriginal: null
        property bool valueDraft: false
        property string valueItems: ""
        property var ours: []
        property bool internal: false
        property bool ready: false
        property var spots: ({})
    }

    Connections {
        target: page.settings
        ignoreUnknownSignals: true

        function onAvailableChanged() {
            page.refreshBootChoices()
        }
    }

    Connections {
        target: page.hasEq ? page.eq : null
        ignoreUnknownSignals: true

        function onProfilesChanged() {
            page.eqRevision++
        }
    }

    component SettingsRow: Item {
        id: item

        required property int index
        required property var modelData
        readonly property bool isFocused: page.focusIndex === item.index
        readonly property bool editing: page.editing && page.editKey === item.modelData.key
        readonly property bool sub: item.modelData.kind === "sub"
        readonly property bool info: item.modelData.kind === "info"
        readonly property bool hasSwatch: item.modelData.key === "accent"
        readonly property string touchKind: item.info ? "" : item.sub ? "sub" : "row"
        readonly property int touchIndex: item.index
        readonly property var titleRole: item.sub ? Ui.settingsSub : item.info ? Ui.settingsValue : Ui.settingsItem
        readonly property real titleAlpha: item.isFocused ? Ui.settingsItem.alpha : Ui.settingsItem.alphaDim
        readonly property real valueAlpha: item.isFocused ? Ui.settingsValueFocused : Ui.settingsValueDim
        readonly property Item titleItem: titleText
        readonly property Item valueItem: valueText
        readonly property Item swatchItem: swatch
        property string shownValue: item.modelData.value
        readonly property bool valueSwapping: valueAnim.running
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        x: Ui.px(item.shift) * (1 - item.enter)
        y: page.rowTopOf(item.index)
        width: flick.width
        height: page.rowHeightOf(item.modelData)
        opacity: item.enter

        Component.onCompleted: {
            const old = page.shownValues[item.modelData.key]
            page.shownValues[item.modelData.key] = item.modelData.value
            if (old === undefined || old === item.modelData.value || !page.live)
                return
            item.shownValue = old
            valueAnim.start()
        }

        Connections {
            target: page

            function onEntrance(shift, stagger, cap, duration, stripCurve, from) {
                if (item.index < from)
                    return
                enterAnim.stop()
                item.shift = shift
                item.stagger = Math.min(cap, item.index - from) * stagger
                item.enterDuration = duration
                item.enterCurve = stripCurve ? Motion.strip : Motion.decel
                item.enter = 0
                enterAnim.start()
            }

            function onSettle() {
                enterAnim.stop()
                item.enter = 1
            }
        }

        SequentialAnimation {
            id: enterAnim

            PauseAnimation {
                duration: item.stagger
            }

            NumberAnimation {
                target: item
                property: "enter"
                from: 0
                to: 1
                duration: item.enterDuration
                easing.type: Motion.bezier
                easing.bezierCurve: item.enterCurve
            }
        }

        SequentialAnimation {
            id: valueAnim

            NumberAnimation {
                target: valueText
                property: "opacity"
                to: Motion.valueDip
                duration: Motion.valueChange
                easing.type: Motion.linear
            }
            ScriptAction {
                script: {
                    item.shownValue = item.modelData.value
                    page.shownValues[item.modelData.key] = item.modelData.value
                }
            }
            NumberAnimation {
                target: valueText
                property: "opacity"
                to: 1
                duration: Motion.valueChange
                easing.type: Motion.linear
            }
        }

        Text {
            id: titleText

            x: Ui.px(Ui.hubEdge)
            y: (item.height - titleText.height) / 2
            width: swatch.visible ? swatch.x - Ui.px(Ui.gutter) - titleText.x
                : valueText.visible ? valueText.x - Ui.px(Ui.gutter) - titleText.x : item.width - Ui.px(Ui.hubEdge) - titleText.x
            text: item.modelData.title
            color: Ui.white(item.titleAlpha)
            font: Ui.font(item.titleRole)
            clip: true

            Behavior on color {
                enabled: page.live
                ColorAnimation {
                    duration: Motion.focusAlpha
                }
            }
        }

        Text {
            id: valueText

            visible: item.shownValue !== "" && !item.hasSwatch
            x: item.width - Ui.px(Ui.hubEdge) - valueText.width
            anchors.baseline: titleText.baseline
            text: item.shownValue
            color: item.editing ? Ui.accent : Ui.white(item.valueAlpha)
            font: Ui.font(Ui.settingsValue)

            Behavior on color {
                enabled: page.live
                ColorAnimation {
                    duration: Motion.focusAlpha
                }
            }
        }

        Rectangle {
            id: swatch

            visible: item.hasSwatch
            x: item.width - Ui.px(Ui.hubEdge) - swatch.width
            anchors.verticalCenter: parent.verticalCenter
            width: Ui.px(Ui.settingsSwatch)
            height: Ui.px(Ui.settingsSwatch)
            color: Ui.accentForHue(page.accentHue)
            opacity: item.valueAlpha / Ui.settingsValueFocused
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    Flickable {
        id: flick

        readonly property bool touchScroll: true
        readonly property bool touchScrollEnabled: !page.editing
        property alias touch: touchScroller

        function stopScroll(): void {
            scrollAnim.stop()
        }

        function clampFocusToView(): void {
            const r = page.focusedRow
            if (r === null)
                return
            const top = flick.contentY
            const bottom = flick.contentY + flick.height
            const rowTop = page.rowTopOf(page.focusIndex)
            if (rowTop >= top - 0.5 && rowTop + page.rowHeightOf(r) <= bottom + 0.5)
                return
            let target = -1
            if (rowTop < top) {
                let i = page.rowAtY(top)
                if (page.rowTopOf(i) < top - 0.5)
                    i++
                target = page.nearestFocusable(i, 1)
            } else {
                let i = page.rowAtY(bottom)
                if (page.rowTopOf(i) + page.rowHeightOf(page.rows[i]) > bottom + 0.5)
                    i--
                target = page.nearestFocusable(i, -1)
            }
            if (target >= 0)
                page.focusIndex = target
        }

        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - flick.y
        contentWidth: flick.width
        contentHeight: page.contentHeight
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Repeater {
            id: repeater

            model: page.rows

            delegate: SettingsRow {}
        }

        TouchScroll {
            id: touchScroller

            flick: page.flick
            limit: page.maxContentY
            onMoved: flick.clampFocusToView()
        }
    }

    FocusBar {
        id: focusBar

        visible: page.focusedRow !== null && page.shownPivot === page.pivotIndex
        rowY: flick.y + page.focusY
        rowHeight: page.focusHeight
        opacity: flick.opacity * page.focusOpacity
        animated: page.live
    }

    CroppedHeader {
        id: header

        text: "settings"
        shift: strip.headerShift
        animated: page.live
    }

    PivotStrip {
        id: strip

        labels: page.pivots
        activeIndex: page.pivotIndex
        animated: page.live
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: page.screenOff
        animated: page.live
        onAccepted: (index) => {
            if (page.valueOpen) {
                page.acceptValue(index)
                return
            }
            sheet.close()
            page.closePlayer()
        }
        onRejected: page.cancelValue()
        onFocusMoved: (index) => page.previewValue(index)
        onAnimatingChanged: {
            if (sheet.animating || d.riseKept === "")
                return
            const kept = d.riseKept
            d.riseKept = ""
            if (sheet.shown && page.valueOpen)
                sheet.handleAction(kept, false)
        }
    }

    Repeater {
        model: sheet.shown ? strip.count : 0

        delegate: Item {
            id: pivotHit

            required property int index
            readonly property Item label: strip.labelItem(pivotHit.index)
            readonly property string touchKind: "pivot"
            readonly property int touchIndex: pivotHit.index

            x: strip.x + strip.restX - strip.activeX + strip.positionOf(pivotHit.index)
            y: strip.y + (pivotHit.label === null ? 0 : pivotHit.label.y)
            width: strip.widthOf(pivotHit.index)
            height: pivotHit.label === null ? 0 : Math.max(0, Math.min(pivotHit.label.height, sheet.height - sheet.panel.height - pivotHit.y))
        }
    }

    NumberAnimation {
        id: scrollAnim

        target: flick
        property: "contentY"
        easing.type: Motion.easeIn
    }

    SequentialAnimation {
        id: pivotAnim

        property real dir: 1

        ParallelAnimation {
            NumberAnimation {
                target: flick
                property: "x"
                to: -pivotAnim.dir * Ui.px(Motion.pivotShift)
                duration: Motion.pivotOut
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
            NumberAnimation {
                target: flick
                property: "opacity"
                to: 0
                duration: Motion.pivotOut
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
        }
        ScriptAction {
            script: {
                page.savePivot()
                page.shownPivot = page.pivotIndex
                page.restorePivot()
                flick.x = 0
                flick.opacity = 1
                page.feather(pivotAnim.dir)
            }
        }
        PauseAnimation {
            duration: Motion.pivotRows + Motion.pivotRowCap * Motion.pivotRowStagger
        }
    }
}
