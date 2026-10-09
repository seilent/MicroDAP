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
    property QtObject codecFragment: null
    property QtObject library: null
    property QtObject rgbControl: null
    property QtObject bridge: null
    property QtObject sensor: null
    property var eq: null
    property bool overlay: false
    property bool animated: true
    property bool screenOff: false
    property bool foldersOpen: false
    property bool aboutOpen: false
    property bool editing: false
    property string editKey: ""
    property var editOriginal: null
    property string orientationDraft: ""
    property bool autoRotateDraft: false
    property bool touchDraft: true
    property int eqRevision: 0
    property var folders: []
    property var shownValues: ({})

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property bool usbPresent: page.outputControl !== null && page.outputControl.usbPresent === true
    readonly property bool usbActive: page.outputControl !== null && page.outputControl.activeOutput === "usb"
    readonly property var outputs: page.usbPresent ? ["auto", "speaker", "headphones", "usb"] : ["auto", "speaker", "headphones"]
    readonly property var codecs: ["auto", "aptx", "aac", "ldac", "sbc"]
    readonly property var hpGains: ["low", "normal", "high"]
    readonly property var hpAmps: ["lohifi", "ulp", "hifi"]
    readonly property var spkGainSteps: [84, 90, 96, 99, 102, 105, 108]
    readonly property var volumeSteps: [2, 5, 10]
    readonly property var rgbModes: ["accent", "off", "armada"]
    readonly property var saverSteps: [10, 15, 30, 60, 120, 300]
    readonly property var screenOffSteps: [0, 60, 120, 300, 600, 1800]
    readonly property var suspendSteps: [0, 5, 10, 30, 60]
    readonly property var scaleSteps: page.buildScaleSteps()
    readonly property var orientations: ["portrait", "portrait-flipped", "landscape", "landscape-flipped"]
    readonly property var editKeys: ({
        output: "output", hpGain: "hpGain", hpAmp: "hpAmp", spkGain: "spkGain", volumeStep: "volumeStep",
        codec: "btCodec", accent: "accentHue", accentArt: "accentFromArt", scale: "scale", saver: "saverSeconds",
        rgb: "rgb", toasts: "overlayToasts", screenOff: "screenOffSeconds", suspend: "suspendMinutes",
        wifi: "wifiOffScreenOff", orientation: "orientation", autoRotate: "autoRotate", touch: "touch"
    })
    readonly property var roots: page.settings !== null && page.settings.roots !== undefined ? page.settings.roots : []
    readonly property string output: page.settings !== null && page.settings.output !== undefined ? String(page.settings.output) : "auto"
    readonly property string codec: page.settings !== null && page.settings.btCodec !== undefined ? String(page.settings.btCodec) : "auto"
    readonly property string hpGain: page.settings !== null && page.settings.hpGain !== undefined ? String(page.settings.hpGain) : "normal"
    readonly property string hpAmp: page.settings !== null && page.settings.hpAmp !== undefined ? String(page.settings.hpAmp) : "lohifi"
    readonly property int spkGain: page.settings !== null && page.settings.spkGain !== undefined ? Number(page.settings.spkGain) : 102
    readonly property int volumeStep: page.settings !== null && page.settings.volumeStep !== undefined ? Number(page.settings.volumeStep) : 5
    readonly property string rgb: page.settings !== null && page.settings.rgb !== undefined ? String(page.settings.rgb) : "armada"
    readonly property bool rgbSupported: !page.overlay && page.rgbControl !== null && page.rgbControl.supported === true
    readonly property string rgbValue: page.rgbSupported ? page.rgb : "unsupported"
    readonly property bool overlayToasts: page.settings !== null && page.settings.overlayToasts === true
    readonly property string orientation: page.settings !== null && page.orientations.indexOf(String(page.settings.orientation)) >= 0
        ? String(page.settings.orientation) : "portrait"
    readonly property bool autoRotate: page.settings !== null && page.settings.autoRotate === true
    readonly property bool touchEnabled: page.settings === null || page.settings.touch !== false
    readonly property string touchValue: (page.editing && page.editKey === "touch" ? page.touchDraft : page.touchEnabled) ? "on" : "off"
    readonly property bool sensorSupported: !page.overlay && page.sensor !== null && page.sensor.supported === true
    readonly property bool autoFollowing: page.autoRotate && page.sensorSupported && Ui.orientationActive
    readonly property string effectiveOrientation: page.orientations[[0, 2, Ui.landscapeTurn, (Ui.landscapeTurn + 2) % 4].indexOf(Ui.turn)]
    readonly property string orientationValue: !Ui.orientationActive ? "external display"
        : (page.autoFollowing ? page.effectiveOrientation
        : page.editing && page.editKey === "orientation" ? page.orientationDraft : page.orientation).replace("-", " ")
    readonly property string autoRotateValue: !page.sensorSupported ? "unsupported"
        : (page.editing && page.editKey === "autoRotate" ? page.autoRotateDraft : page.autoRotate) ? "on" : "off"
    readonly property real savedScale: page.settings !== null && page.settings.scale !== undefined ? Number(page.settings.scale) : 1.0
    readonly property bool jackKnown: page.outputControl !== null && page.outputControl.jackPresent !== undefined
    readonly property bool jackPresent: !page.jackKnown || page.outputControl.jackPresent === true
    readonly property int graphRate: page.audioInfo !== null && page.audioInfo.graphRate !== undefined ? Number(page.audioInfo.graphRate) : 0
    readonly property string outputValue: page.output === "headphones" && !page.jackPresent ? "no jack"
        : page.output === "usb" && !page.usbPresent ? "usb (not present)" : page.output
    readonly property string hpGainValue: page.usbActive ? "built in only" : page.hpGain
    readonly property string hpAmpValue: page.usbActive ? "built in only" : page.hpAmp
    readonly property string rateValue: page.graphRate > 0 ? "on " + page.graphRate : "on"
    readonly property bool hasEq: page.eq !== null && page.eq !== undefined
    readonly property string eqOutput: page.hasEq && page.eq.activeOutput !== undefined ? String(page.eq.activeOutput) : ""
    readonly property string eqValue: page.eqText(page.eqOutput, page.eqRevision)
    readonly property int accentHue: page.settings !== null && page.settings.accentHue !== undefined ? Ui.hueOnGrid(Number(page.settings.accentHue)) : Ui.accentHueDefault
    readonly property bool accentFromArt: page.settings !== null && page.settings.accentFromArt === true
    readonly property int saverSeconds: page.settings !== null && page.settings.saverSeconds !== undefined ? page.settings.saverSeconds : 30
    readonly property int screenOffSeconds: page.settings !== null && page.settings.screenOffSeconds !== undefined ? page.settings.screenOffSeconds : 300
    readonly property int suspendMinutes: page.settings !== null && page.settings.suspendMinutes !== undefined ? page.settings.suspendMinutes : 10
    readonly property bool wifiOffScreenOff: page.settings !== null && page.settings.wifiOffScreenOff === true
    readonly property bool codecPending: page.codecFragment !== null && page.codecFragment.pending === true
    readonly property string codecValue: page.codecPending ? page.codec + " (pending)" : page.codec
    readonly property bool scanning: page.library !== null && page.library.scanning === true
    readonly property int songCount: page.library !== null && page.library.songCount !== undefined ? Number(page.library.songCount) : 0
    readonly property string rescanValue: page.scanning ? "scanning" : page.songCount + (page.songCount === 1 ? " song" : " songs")
    readonly property var current: page.player !== null && page.player.current !== undefined && page.player.current !== null ? page.player.current : null
    readonly property string pcmFormat: page.audioInfo !== null && page.audioInfo.pcmFormat !== undefined ? String(page.audioInfo.pcmFormat) : ""
    readonly property var driftPaths: page.power !== null && page.power.driftPaths !== undefined ? page.power.driftPaths : []
    readonly property string version: Env.version
    readonly property var manifest: Env.manifest
    readonly property var rows: page.buildRows()
    readonly property var rowTops: page.buildTops(page.rows)
    readonly property real contentHeight: page.rows.length === 0 ? 0 : page.rowTops[page.rows.length - 1] + page.rowHeightOf(page.rows[page.rows.length - 1])
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
    readonly property real focusY: page.rowTopOf(page.focusIndex) - flick.contentY
    readonly property real focusHeight: page.focusedRow === null ? 0 : page.rowHeightOf(page.focusedRow)
    readonly property bool scrolling: scrollAnim.running
    property alias header: header
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

    function feather(): void {
        page.entrance(Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true, page.firstVisible())
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
        const out = []
        for (let s = Ui.sMin; s <= Ui.sMax + 1e-6; s += Ui.sStep)
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

    function digestTail(image: string): string {
        const at = image.indexOf("sha256:")
        const digest = at >= 0 ? image.substring(at + 7) : image
        return digest.length > 12 ? digest.substring(0, 12) : digest
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

    function label(title: string): var {
        return { kind: "label", key: "label-" + title, title: title, value: "", readOnly: true }
    }

    function row(key: string, title: string, value: string, readOnly: bool): var {
        return { kind: "row", key: key, title: title, value: value, readOnly: readOnly }
    }

    function info(key: string, title: string, value: string): var {
        return { kind: "info", key: key, title: title, value: value, readOnly: true }
    }

    function buildRows(): var {
        const out = []
        out.push(page.label("output"))
        out.push(page.row("output", "output", page.outputValue, false))
        out.push(page.row("hpGain", "headphone gain", page.hpGainValue, page.usbActive))
        out.push(page.row("hpAmp", "headphone amp", page.hpAmpValue, page.usbActive))
        out.push(page.row("spkGain", "speaker digital gain", page.spkGainText(page.spkGain), false))
        out.push(page.row("volumeStep", "volume step", page.volumeStep + "%", false))
        out.push(page.row("codec", "bluetooth codec", page.codecValue, false))
        out.push(page.row("rate", "sample rate follow", page.rateValue, true))
        out.push(page.row("replaygain", "replaygain", "off", true))
        out.push(page.row("eq", "eq", page.eqValue, true))
        out.push(page.row("pair", "pair a phone", page.overlay ? "pair in dap mode" : "", true))
        out.push(page.label("library"))
        out.push(page.row("folders", "library folders", page.roots.length + " on", true))
        if (page.foldersOpen) {
            for (const f of page.folders)
                out.push({ kind: "sub", key: "root:" + f, title: f, value: page.roots.indexOf(f) >= 0 ? "on" : "off", readOnly: true, path: f })
        }
        out.push(page.row("rescan", "rescan library", page.rescanValue, true))
        out.push(page.label("display"))
        out.push(page.row("accent", "accent", "", false))
        out.push(page.row("accentArt", "accent from art", page.accentFromArt ? "on" : "off", false))
        if (!page.overlay)
            out.push(page.row("orientation", "orientation", page.orientationValue, !Ui.orientationActive || page.autoFollowing))
        if (!page.overlay)
            out.push(page.row("autoRotate", "auto rotate", page.autoRotateValue, !page.sensorSupported))
        if (!page.overlay)
            out.push(page.row("touch", "touch", page.touchValue, false))
        out.push(page.row("scale", "scale", Ui.userScale.toFixed(1), false))
        out.push(page.row("saver", "saver", page.minutes(page.saverSeconds), false))
        if (!page.overlay)
            out.push(page.row("rgb", "rgb", page.rgbValue, !page.rgbSupported))
        out.push(page.row("toasts", "toasts in game", page.overlayToasts ? "on" : "off", false))
        out.push(page.label("power"))
        out.push(page.row("screenOff", "screen off after", page.offOr(Math.round(page.screenOffSeconds / 60), "min"), false))
        out.push(page.row("suspend", "suspend after", page.offOr(page.suspendMinutes, "min"), false))
        out.push(page.row("wifi", "wi-fi when off", page.wifiOffScreenOff ? "off" : "keep", false))
        if (page.overlay)
            out.push(page.row("closePlayer", "close player", "", true))
        out.push(page.row("about", "about", page.version, true))
        if (page.aboutOpen) {
            const image = page.manifest.IMAGE_VERSION === undefined ? "" : String(page.manifest.IMAGE_VERSION)
            const base = page.manifest.BASE_IMAGE === undefined ? "" : String(page.manifest.BASE_IMAGE)
            if (image !== "")
                out.push(page.info("armada", "armada", image))
            if (base !== "")
                out.push(page.info("image", "image", page.digestTail(base)))
            if (page.driftPaths.length > 0)
                out.push(page.info("drift", "drift", page.driftPaths.length + (page.driftPaths.length === 1 ? " file" : " files")))
            const track = page.trackLine()
            if (track !== "")
                out.push(page.info("track", "track", track))
            if (page.pcmFormat !== "")
                out.push(page.info("pcm", "output", page.pcmFormat))
        }
        return out
    }

    function rowHeightOf(r: var): real {
        return Ui.px(r.kind === "label" ? Ui.rowHeight : Ui.settingsPitch)
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
        return r !== undefined && r !== null && r.kind !== "label"
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
            let best = 0
            for (let k = 0; k < list.length; k++) {
                if (Math.abs(Number(list[k]) - Number(value)) < Math.abs(Number(list[best]) - Number(value)))
                    best = k
            }
            i = best
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
        switch (r.key) {
        case "output":
            page.set("output", page.cycle(page.outputs, page.output, delta))
            return true
        case "hpGain":
            if (page.usbActive)
                return false
            page.set("hpGain", page.cycle(page.hpGains, page.hpGain, delta))
            return true
        case "hpAmp":
            if (page.usbActive)
                return false
            page.set("hpAmp", page.cycle(page.hpAmps, page.hpAmp, delta))
            return true
        case "spkGain":
            page.set("spkGain", page.cycle(page.spkGainSteps, page.spkGain, delta))
            return true
        case "volumeStep":
            page.set("volumeStep", page.cycle(page.volumeSteps, page.volumeStep, delta))
            return true
        case "rgb":
            if (!page.rgbSupported)
                return false
            page.set("rgb", page.cycle(page.rgbModes, page.rgb, delta))
            return true
        case "codec":
            page.set("btCodec", page.cycle(page.codecs, page.codec, delta))
            return true
        case "toasts":
            page.set("overlayToasts", !page.overlayToasts)
            return true
        case "accent":
            page.set("accentHue", Ui.stepHue(page.accentHue, delta))
            return true
        case "accentArt":
            page.set("accentFromArt", !page.accentFromArt)
            return true
        case "scale":
            page.applyScale(page.cycle(page.scaleSteps, Math.round(Ui.userScale * 10) / 10, delta))
            return true
        case "saver":
            page.set("saverSeconds", page.cycle(page.saverSteps, page.saverSeconds, delta))
            return true
        case "screenOff":
            page.set("screenOffSeconds", page.cycle(page.screenOffSteps, page.screenOffSeconds, delta))
            return true
        case "suspend":
            page.set("suspendMinutes", page.cycle(page.suspendSteps, page.suspendMinutes, delta))
            return true
        case "wifi":
            page.set("wifiOffScreenOff", !page.wifiOffScreenOff)
            return true
        case "orientation":
            page.orientationDraft = page.cycle(page.orientations, page.orientationDraft, delta)
            return true
        case "autoRotate":
            page.autoRotateDraft = !page.autoRotateDraft
            return true
        case "touch":
            page.touchDraft = !page.touchDraft
            return true
        default:
            return false
        }
    }

    function settingOf(key: string): string {
        const s = page.editKeys[key]
        return s === undefined ? "" : s
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
        case "codec":
            return page.codec
        case "accent":
            return page.accentHue
        case "accentArt":
            return page.accentFromArt
        case "scale":
            return page.savedScale
        case "saver":
            return page.saverSeconds
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
        default:
            return undefined
        }
    }

    function beginEdit(key: string): void {
        page.editOriginal = page.currentOf(key)
        page.orientationDraft = page.orientation
        page.autoRotateDraft = page.autoRotate
        page.touchDraft = page.touchEnabled
        page.editKey = key
        page.editing = true
    }

    function confirmEdit(): void {
        page.editing = false
        page.editKey = ""
        page.editOriginal = null
    }

    function cancelEdit(): void {
        if (!page.editing)
            return
        const key = page.editKey
        const original = page.editOriginal
        if (key === "scale")
            page.applyScale(Number(original))
        else if (key !== "orientation" && key !== "autoRotate" && key !== "touch")
            page.set(page.settingOf(key), original)
        page.confirmEdit()
    }

    function applyEdit(): void {
        const key = page.editKey
        const draft = page.orientationDraft
        const autoDraft = page.autoRotateDraft
        const touchDraft = page.touchDraft
        page.confirmEdit()
        if (key === "orientation")
            page.set("orientation", draft)
        else if (key === "autoRotate")
            page.set("autoRotate", autoDraft)
        else if (key === "touch")
            page.set("touch", touchDraft)
    }

    function revertOrConfirmForRotation(): void {
        if (!page.editing)
            return
        if (page.editKey === "orientation" || page.editKey === "autoRotate")
            page.cancelEdit()
        else
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
                page.applyEdit()
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
        page.set("roots", next)
    }

    function closePlayer(): void {
        page.closePlayerRequested()
        if (page.bridge !== null && typeof page.bridge.closePlayer === "function")
            page.bridge.closePlayer()
        else
            console.info("casdap: close player")
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
        if (page.settingOf(r.key) !== "") {
            if (!r.readOnly)
                page.beginEdit(r.key)
            return true
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
            sheet.title = "close player"
            sheet.items = ["close player"]
            sheet.open()
            return true
        case "about":
            page.aboutOpen = !page.aboutOpen
            if (page.aboutOpen && page.power !== null && typeof page.power.reloadDrift === "function")
                page.power.reloadDrift()
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
        if (!page.editing && (kind === "row" || kind === "sub"))
            page.ensureVisible(Motion.scroll)
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
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
    }

    onActiveChanged: {
        if (!page.active)
            page.confirmEdit()
    }

    onScreenOffChanged: {
        if (page.screenOff)
            page.confirmEdit()
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            page.feather()
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        if (page.audioInfo !== null && typeof page.audioInfo.refreshGraphRate === "function")
            page.audioInfo.refreshGraphRate()
        const list = page.roots.slice()
        for (const f of page.folders) {
            if (list.indexOf(f) < 0)
                list.push(f)
        }
        page.folders = list
        page.focusIndex = Math.max(0, page.nearestFocusable(0, 1))
    }

    QtObject {
        id: d

        property int pendingFrom: -1
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
        readonly property bool label: item.modelData.kind === "label"
        readonly property bool sub: item.modelData.kind === "sub"
        readonly property bool info: item.modelData.kind === "info"
        readonly property bool hasSwatch: item.modelData.key === "accent"
        readonly property string touchKind: item.label || item.info ? "" : item.sub ? "sub" : "row"
        readonly property int touchIndex: item.index
        readonly property var titleRole: item.label ? Ui.sectionLabel : item.sub ? Ui.settingsSub : item.info ? Ui.settingsValue : Ui.settingsItem
        readonly property real titleAlpha: item.label ? Ui.sectionLabel.alpha
            : item.isFocused ? Ui.settingsItem.alpha : Ui.settingsItem.alphaDim
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
            y: item.label
                ? item.height - Ui.px(Ui.settingsLabelBaselineInset) - titleText.baselineOffset
                : (item.height - titleText.height) / 2
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

        visible: page.focusedRow !== null
        rowY: flick.y + page.focusY
        rowHeight: page.focusHeight
        opacity: page.focusOpacity
        animated: page.live
    }

    CroppedHeader {
        id: header

        text: "settings"
        animated: page.live
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: !page.live
        onAccepted: (index) => {
            sheet.close()
            page.closePlayer()
        }
    }

    NumberAnimation {
        id: scrollAnim

        target: flick
        property: "contentY"
        easing.type: Motion.easeIn
    }
}
