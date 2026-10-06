pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property QtObject settings: null
    property QtObject power: null
    property QtObject player: null
    property QtObject audioInfo: null
    property QtObject outputControl: null
    property QtObject bridge: null
    property bool overlay: false
    property bool animated: true
    property bool screenOff: false
    property bool foldersOpen: false
    property bool aboutOpen: false
    property var folders: []
    property var shownValues: ({})

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property var outputs: ["auto", "speaker", "headphones"]
    readonly property var codecs: ["auto", "aptx", "aac", "ldac", "sbc"]
    readonly property var saverSteps: [10, 15, 30, 60, 120, 300]
    readonly property var screenOffSteps: [0, 60, 120, 300, 600, 1800]
    readonly property var suspendSteps: [0, 5, 10, 30, 60]
    readonly property var scaleSteps: page.buildScaleSteps()
    readonly property var roots: page.settings !== null && page.settings.roots !== undefined ? page.settings.roots : []
    readonly property string output: page.settings !== null && page.settings.output !== undefined ? String(page.settings.output) : "auto"
    readonly property string codec: page.settings !== null && page.settings.btCodec !== undefined ? String(page.settings.btCodec) : "auto"
    readonly property bool overlayToasts: page.settings !== null && page.settings.overlayToasts === true
    readonly property bool jackKnown: page.outputControl !== null && page.outputControl.jackPresent !== undefined
    readonly property bool jackPresent: !page.jackKnown || page.outputControl.jackPresent === true
    readonly property int graphRate: page.audioInfo !== null && page.audioInfo.graphRate !== undefined ? Number(page.audioInfo.graphRate) : 0
    readonly property string outputValue: page.output === "headphones" && !page.jackPresent ? "no jack" : page.output
    readonly property string rateValue: page.graphRate > 0 ? "on " + page.graphRate : "on"
    readonly property string gameModeTitle: page.overlay ? "close player" : "game mode"
    readonly property string accent: page.settings !== null && page.settings.accent !== undefined ? String(page.settings.accent) : String(Ui.accent)
    readonly property int saverSeconds: page.settings !== null && page.settings.saverSeconds !== undefined ? page.settings.saverSeconds : 30
    readonly property int screenOffSeconds: page.settings !== null && page.settings.screenOffSeconds !== undefined ? page.settings.screenOffSeconds : 300
    readonly property int suspendMinutes: page.settings !== null && page.settings.suspendMinutes !== undefined ? page.settings.suspendMinutes : 10
    readonly property bool pocketControls: page.settings !== null && page.settings.pocketControls === true
    readonly property bool wifiOffScreenOff: page.settings !== null && page.settings.wifiOffScreenOff === true
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
    signal gameModeRequested()
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
        out.push(page.row("codec", "bluetooth codec", page.codec, false))
        out.push(page.row("rate", "sample rate follow", page.rateValue, true))
        out.push(page.row("replaygain", "replaygain", "off", true))
        out.push(page.row("pair", "pair a phone", page.overlay ? "pair in dap mode" : "", true))
        out.push(page.label("library"))
        out.push(page.row("folders", "library folders", page.roots.length + " on", true))
        if (page.foldersOpen) {
            for (const f of page.folders)
                out.push({ kind: "sub", key: "root:" + f, title: f, value: page.roots.indexOf(f) >= 0 ? "on" : "off", readOnly: true, path: f })
        }
        out.push(page.label("display"))
        out.push(page.row("accent", "accent", "", false))
        out.push(page.row("orientation", "orientation", Ui.landscape ? "landscape" : "portrait", true))
        out.push(page.row("scale", "scale", Ui.userScale.toFixed(1), false))
        out.push(page.row("saver", "saver", page.minutes(page.saverSeconds), false))
        out.push(page.row("toasts", "overlay toasts", page.overlayToasts ? "on" : "off", false))
        out.push(page.label("power"))
        out.push(page.row("screenOff", "screen off after", page.offOr(Math.round(page.screenOffSeconds / 60), "min"), false))
        out.push(page.row("suspend", "suspend after", page.offOr(page.suspendMinutes, "min"), false))
        out.push(page.row("pocket", "pocket controls", page.pocketControls ? "on" : "off", false))
        out.push(page.row("wifi", "wi-fi when off", page.wifiOffScreenOff ? "off" : "keep", false))
        out.push(page.row("gameMode", page.gameModeTitle, "", true))
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

    function cycle(list: var, value: var, delta: int, wrap: bool): var {
        let i = list.indexOf(value)
        if (i < 0) {
            let best = 0
            for (let k = 0; k < list.length; k++) {
                if (Math.abs(Number(list[k]) - Number(value)) < Math.abs(Number(list[best]) - Number(value)))
                    best = k
            }
            i = best
        }
        const next = wrap ? (i + delta + list.length) % list.length : Math.max(0, Math.min(list.length - 1, i + delta))
        return list[next]
    }

    function set(key: string, value: var) {
        if (page.settings === null || page.settings[key] === undefined)
            return
        page.settings[key] = value
    }

    function applyScale(s: real) {
        const index = page.focusIndex
        Ui.userScale = s
        page.set("scale", s)
        page.focusIndex = index
        page.completeScroll()
        flick.contentY = page.visibleY(index, flick.contentY)
    }

    function applyAccent(c: string) {
        Ui.accent = c
        page.set("accent", c)
    }

    function step(delta: int, wrap: bool): bool {
        const r = page.focusedRow
        if (r === null)
            return false
        switch (r.key) {
        case "output":
            page.set("output", page.cycle(page.outputs, page.output, delta, wrap))
            return true
        case "codec":
            page.set("btCodec", page.cycle(page.codecs, page.codec, delta, wrap))
            return true
        case "toasts":
            page.set("overlayToasts", !page.overlayToasts)
            return true
        case "accent": {
            const list = Ui.accentOptions
            const i = list.indexOf(page.accent.toUpperCase())
            const base = i < 0 ? 0 : i
            const next = wrap ? (base + delta + list.length) % list.length : Math.max(0, Math.min(list.length - 1, base + delta))
            page.applyAccent(list[next])
            return true
        }
        case "scale":
            page.applyScale(page.cycle(page.scaleSteps, Math.round(Ui.userScale * 10) / 10, delta, wrap))
            return true
        case "saver":
            page.set("saverSeconds", page.cycle(page.saverSteps, page.saverSeconds, delta, wrap))
            return true
        case "screenOff":
            page.set("screenOffSeconds", page.cycle(page.screenOffSteps, page.screenOffSeconds, delta, wrap))
            return true
        case "suspend":
            page.set("suspendMinutes", page.cycle(page.suspendSteps, page.suspendMinutes, delta, wrap))
            return true
        case "pocket":
            page.set("pocketControls", !page.pocketControls)
            return true
        case "wifi":
            page.set("wifiOffScreenOff", !page.wifiOffScreenOff)
            return true
        default:
            return false
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

    function runGameMode() {
        page.gameModeRequested()
        if (page.overlay) {
            if (page.bridge !== null && typeof page.bridge.closePlayer === "function")
                page.bridge.closePlayer()
            else
                console.info("casdap: close player")
            return
        }
        if (page.power === null) {
            console.info("casdap: game mode")
            return
        }
        if (typeof page.power.closeAndSwitch === "function")
            page.power.closeAndSwitch()
        else if (typeof page.power.runVerb === "function")
            page.power.runVerb("game-mode")
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
        switch (r.key) {
        case "pair":
            if (!page.overlay)
                page.pairPhone()
            return true
        case "folders":
            page.foldersOpen = !page.foldersOpen
            page.refreshFrom(page.focusIndex + 1)
            return true
        case "gameMode":
            sheet.title = page.gameModeTitle
            sheet.items = [page.overlay ? "close player" : "switch to game mode"]
            sheet.open()
            return true
        case "about":
            page.aboutOpen = !page.aboutOpen
            page.refreshFrom(page.focusIndex + 1)
            return true
        case "orientation":
        case "rate":
        case "replaygain":
            return true
        default:
            return page.step(1, true)
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

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        switch (name) {
        case "up":
            return page.moveRow(-1)
        case "down":
            return page.moveRow(1)
        case "left":
            return page.step(-1, false)
        case "right":
            return page.step(1, false)
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

    onRowsChanged: page.fixFocus()

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

    component SettingsRow: Item {
        id: item

        required property int index
        required property var modelData
        readonly property bool isFocused: page.focusIndex === item.index
        readonly property bool label: item.modelData.kind === "label"
        readonly property bool sub: item.modelData.kind === "sub"
        readonly property bool info: item.modelData.kind === "info"
        readonly property bool swatches: item.modelData.key === "accent"
        readonly property var titleRole: item.label ? Ui.sectionLabel : item.sub ? Ui.settingsSub : item.info ? Ui.settingsValue : Ui.settingsItem
        readonly property real titleAlpha: item.label ? Ui.sectionLabel.alpha
            : item.isFocused ? Ui.settingsItem.alpha : Ui.settingsItem.alphaDim
        readonly property real valueAlpha: item.isFocused ? Ui.settingsValueFocused : Ui.settingsValueDim
        readonly property Item titleItem: titleText
        readonly property Item valueItem: valueText
        readonly property Item swatchItem: swatchRow
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
            width: swatchRow.visible ? swatchRow.x - Ui.px(Ui.gutter) - titleText.x
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

            visible: item.shownValue !== "" && !item.swatches
            x: item.width - Ui.px(Ui.hubEdge) - valueText.width
            anchors.baseline: titleText.baseline
            text: item.shownValue
            color: Ui.white(item.valueAlpha)
            font: Ui.font(Ui.settingsValue)

            Behavior on color {
                enabled: page.live
                ColorAnimation {
                    duration: Motion.focusAlpha
                }
            }
        }

        Row {
            id: swatchRow

            visible: item.swatches
            x: item.width - Ui.px(Ui.hubEdge) - swatchRow.width
            anchors.verticalCenter: parent.verticalCenter
            spacing: Ui.px(Ui.settingsSwatchGap)
            opacity: item.valueAlpha / Ui.settingsValueFocused

            Repeater {
                model: item.swatches ? Ui.accentOptions : []

                delegate: Rectangle {
                    id: swatch

                    required property var modelData
                    readonly property bool selected: String(swatch.modelData).toUpperCase() === page.accent.toUpperCase()

                    width: Ui.px(Ui.settingsSwatch)
                    height: Ui.px(Ui.settingsSwatch)
                    color: swatch.modelData
                    border.width: swatch.selected ? Ui.px(Ui.focusBorder) : 0
                    border.color: Ui.textFull
                }
            }
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    Flickable {
        id: flick

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
            page.runGameMode()
        }
    }

    NumberAnimation {
        id: scrollAnim

        target: flick
        property: "contentY"
        easing.type: Motion.easeIn
    }
}
