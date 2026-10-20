pragma ComponentBehavior: Bound
import QtQuick
import "EqFormat.js" as EqFormat

Page {
    id: page

    property var eq: null
    property QtObject dacEq: null
    property Item keyboard: null
    property bool animated: true
    property bool screenOff: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property int revision: 0
    property bool editing: false
    property string editKey: ""
    property string editOutput: ""
    property string editProfileKey: ""

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property bool hasEq: page.eq !== null && page.eq !== undefined
    readonly property bool dacOn: page.dacEq !== null && (page.dacEq.status === "on" || page.dacEq.status === "applying")
    readonly property var dacRanges: page.dacOn && page.dacEq.ranges !== undefined ? page.dacEq.ranges : null
    property bool embedded: false
    property string slot: ""
    readonly property var pivots: page.embedded ? [page.slot] : EqFormat.outputs
    readonly property var panes: page.embedded ? [headphonesPane] : [headphonesPane, usbPane, speakerPane]
    readonly property real slotBaseline: Ui.contentTop + Ui.capHeight * Ui.sectionLabel.size
    readonly property real rowsTop: page.embedded ? page.slotBaseline + Ui.detailLabelDrop : Ui.contentTop
    readonly property var activePane: page.panes[page.pivotIndex]
    readonly property var rows: page.activePane.rows
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
    readonly property real pitch: Ui.px(Ui.settingsPitch)
    readonly property bool switching: pivotAnim.running
    readonly property var subRole: Object.assign({}, Ui.settingsSub, { caps: Font.AllUppercase })
    readonly property var importList: page.hasEq && page.eq.imports !== undefined && page.eq.imports !== null ? page.eq.imports : []
    readonly property string importingPath: page.hasEq && page.eq.importing !== undefined ? String(page.eq.importing) : ""
    property alias header: header
    property alias strip: strip
    property alias focusBar: focusBar
    property alias headphonesPane: headphonesPane
    property alias usbPane: usbPane
    property alias speakerPane: speakerPane
    property alias deleteDialog: deleteDialog
    property alias slotText: slotText
    property alias sheet: sheet

    signal toast(string first, string second)

    pageId: 0
    focusCount: page.rows.length

    function profileOf(output: string, revision: int): var {
        if (!page.hasEq || typeof page.eq.profileMap !== "function")
            return null
        const p = page.eq.profileMap(output)
        return p === undefined ? null : p
    }

    function entry(kind: string, key: string, title: string, value: string, band: int, field: string): var {
        return { kind: kind, key: key, title: title, value: value, band: band, field: field, readOnly: false }
    }

    function buildRows(p: var, openBand: int, ranges: var, importOpen: bool): var {
        const graphic = p !== null && p !== undefined && p.mode === "graphic"
        const out = [page.entry("row", "mode", "mode", graphic ? "graphic" : "parametric", -1, "")]
        if (graphic)
            return page.graphicRows(p, out, ranges)
        out.push(page.entry("row", "profile", "profile", EqFormat.profileText(p), -1, ""))
        const listed = EqFormat.listed(p)
        if (listed && EqFormat.key(p) === "custom")
            out.push(page.entry("row", "savePreset", "save preset", "", -1, ""))
        if (listed && p.saved === true) {
            out.push(page.entry("row", "deletePreset", "delete preset", "", -1, ""))
            out.push(page.entry("row", "renamePreset", "rename preset", "", -1, ""))
        }
        const imports = page.importList
        if (imports.length > 0) {
            out.push(page.entry("row", "import", "import graphiceq", "", -1, ""))
            for (let i = 0; importOpen && i < imports.length; i++) {
                const path = String(imports[i].path)
                const file = page.entry("file", "import:" + path, String(imports[i].name), path === page.importingPath ? "importing" : "", -1, "")
                file.path = path
                out.push(file)
            }
        }
        if (!listed)
            return out
        const preamp = p.preamp === undefined ? 0 : p.preamp
        out.push(page.entry("row", "preamp", "preamp", EqFormat.preampText(preamp) + EqFormat.dacSuffix("preamp", preamp, ranges), -1, ""))
        const bands = p.bands === undefined || p.bands === null ? [] : p.bands
        for (let i = 0; i < bands.length; i++) {
            out.push(page.entry("row", "band" + i, String(i + 1), EqFormat.bandText(bands[i]), i, ""))
            if (i !== openBand)
                continue
            for (let f = 0; f < EqFormat.fields.length; f++) {
                const field = EqFormat.fields[f]
                const inert = EqFormat.fieldInert(bands[i], field)
                const dac = inert || String(bands[i].type) === "off" ? "" : EqFormat.dacSuffix(field, bands[i][field], ranges, String(bands[i].type))
                const sub = page.entry("sub", "band" + i + ":" + field, EqFormat.fieldTitles[f],
                    EqFormat.fieldText(bands[i], field) + dac, i, field)
                sub.readOnly = inert
                out.push(sub)
            }
        }
        if (bands.length < EqFormat.maxBands)
            out.push(page.entry("row", "addBand", "add band", "", -1, ""))
        if (bands.length > 1)
            out.push(page.entry("row", "removeBand", "remove band", String(bands.length), -1, ""))
        return out
    }

    function graphicRows(p: var, out: var, ranges: var): var {
        out.push(page.entry("row", "preset", "preset", EqFormat.profileText(p), -1, ""))
        if (!EqFormat.listed(p))
            return out
        if (EqFormat.key(p) === "custom")
            out.push(page.entry("row", "savePreset", "save preset", "", -1, ""))
        if (p.saved === true) {
            out.push(page.entry("row", "deletePreset", "delete preset", "", -1, ""))
            out.push(page.entry("row", "renamePreset", "rename preset", "", -1, ""))
        }
        const automatic = p.preamp === undefined ? 0 : p.preamp
        const preamp = page.entry("row", "graphicPreamp", "preamp", EqFormat.preampText(automatic) + EqFormat.dacSuffix("preamp", automatic, ranges), -1, "")
        preamp.readOnly = true
        out.push(preamp)
        const bands = p.bands === undefined || p.bands === null ? [] : p.bands
        for (let i = 0; i < bands.length; i++)
            out.push(page.entry("graphic", "gband" + i, EqFormat.freqText(bands[i].freq, bands[i].freqIndex),
                EqFormat.gainText(bands[i].gain) + EqFormat.dacSuffix("gain", bands[i].gain, ranges), i, "gain"))
        return out
    }

    function addBand(): void {
        const pane = page.activePane
        const at = pane.rows.filter((r) => r.kind === "row" && r.band >= 0).length
        if (!page.canCall("addBand") || !page.eq.addBand(pane.output))
            return
        pane.openBand = at
        page.focusIndex = Math.max(0, page.indexOfKey("band" + at))
        pane.ensureVisible(page.focusIndex, Motion.scroll)
        pane.refreshFrom(page.focusIndex)
    }

    function toggleImport(): void {
        const pane = page.activePane
        pane.importOpen = !pane.importOpen
        if (!pane.importOpen)
            return
        pane.ensureVisible(page.focusIndex, Motion.scroll)
        pane.refreshFrom(page.focusIndex + 1)
    }

    function importFile(path: string): void {
        if (!page.canCall("importGraphic"))
            return
        const error = String(page.eq.importGraphic(page.activePane.output, path))
        if (error !== "")
            page.toast(error, "")
    }

    function savePreset(): void {
        const output = page.activePane.output
        if (!page.canCall("savePreset"))
            return
        const error = String(page.eq.savePreset(output))
        if (error !== "")
            page.toast(error, "")
    }

    function confirmDelete(): void {
        d.deleteOutput = page.activePane.output
        d.deleteName = EqFormat.profileText(page.activePane.profile)
        deleteDialog.open()
        deleteDialog.focusIndex = 1
        deleteDialog.barIndex = 1
    }

    function runDelete(): void {
        const output = d.deleteOutput
        d.deleteOutput = ""
        if (output === "" || !page.canCall("deletePreset"))
            return
        if (!page.eq.deletePreset(output))
            page.toast("preset not deleted", d.deleteName)
        page.fixFocus()
    }

    function typable(r: var): bool {
        if (r === null || r === undefined || r.readOnly === true)
            return false
        return r.key === "preamp" || r.kind === "graphic" || (r.kind === "sub" && r.field !== "type")
    }

    function openTypeSheet(): bool {
        const r = page.focusedRow
        if (page.editing || page.keyboard === null || !page.typable(r))
            return false
        sheet.title = r.title
        sheet.items = ["type value"]
        sheet.open()
        return true
    }

    function openType(): void {
        const r = page.focusedRow
        const p = page.activePane.profile
        if (!page.typable(r) || p === null || page.keyboard === null)
            return
        const field = r.key === "preamp" ? "preamp" : r.field
        const value = field === "preamp" ? p.preamp : p.bands[r.band][field]
        d.typeOutput = page.activePane.output
        d.typeBand = r.band
        d.typeField = r.kind === "graphic" ? "graphicGain" : field
        d.kbTarget = "type"
        const request = EqFormat.typedRequest(field, r.title, value)
        request.validator = (text) => page.applyTyped(text)
        d.typeInitial = request.text
        page.keyboard.open(request)
    }

    function applyTyped(text: string): string {
        if (text === d.typeInitial)
            return ""
        const value = Number(text)
        let set = false
        if (d.typeField === "preamp")
            set = page.canCall("setPreamp") && page.eq.setPreamp(d.typeOutput, value) === true
        else if (d.typeField === "graphicGain")
            set = page.canCall("setGraphicGain") && page.eq.setGraphicGain(d.typeOutput, d.typeBand, value) === true
        else
            set = page.canCall("setBandValue") && page.eq.setBandValue(d.typeOutput, d.typeBand, d.typeField, value) === true
        return set ? "" : "not set"
    }

    function openRename(): void {
        if (page.keyboard === null || !page.canCall("renamePreset"))
            return
        const output = page.activePane.output
        d.kbTarget = "rename"
        d.renameOutput = output
        d.renameName = EqFormat.profileText(page.activePane.profile)
        page.keyboard.open({ title: "rename preset", text: String(page.eq.presetName(output)), kind: "text", min: 1,
            validator: (text) => page.eq.renameRefusal(output, text) })
    }

    function renameDone(text: string): void {
        const output = d.renameOutput
        d.renameOutput = ""
        if (output === "")
            return
        if (!page.eq.renamePreset(output, text))
            page.toast("preset not renamed", d.renameName)
    }

    function removeBand(): void {
        const pane = page.activePane
        if (!page.canCall("removeBand") || !page.eq.removeBand(pane.output))
            return
        const count = pane.rows.filter((r) => r.kind === "row" && r.band >= 0).length
        if (pane.openBand >= count)
            pane.openBand = -1
        const remove = page.indexOfKey("removeBand")
        page.focusIndex = Math.max(0, remove >= 0 ? remove : page.indexOfKey("addBand"))
        pane.ensureVisible(page.focusIndex, Motion.scroll)
    }

    function indexOfKey(key: string): int {
        for (let i = 0; i < page.rows.length; i++) {
            if (page.rows[i].key === key)
                return i
        }
        return -1
    }

    function moveRow(delta: int): bool {
        const target = Math.max(0, Math.min(page.rows.length - 1, page.focusIndex + delta))
        if (target === page.focusIndex)
            return false
        page.focusIndex = target
        page.activePane.ensureVisible(target, Motion.scroll)
        return true
    }

    function pageRow(dir: int): bool {
        const pane = page.activePane
        const perPage = Math.max(1, Math.floor(pane.height / page.pitch))
        const target = Math.max(0, Math.min(page.rows.length - 1, page.focusIndex + dir * perPage))
        if (target === page.focusIndex)
            return false
        const base = pane.scrolling ? pane.scrollTarget : pane.contentY
        page.focusIndex = target
        pane.scrollTo(pane.visibleY(target, base + dir * pane.height), Motion.page)
        return true
    }

    function canCall(method: string): bool {
        return page.hasEq && typeof page.eq[method] === "function"
    }

    function snapshot(output: string): string {
        if (!page.canCall("profileMap"))
            return ""
        return JSON.stringify(page.eq.profileMap(output))
    }

    function profileKey(output: string): string {
        if (!page.canCall("profileMap"))
            return ""
        const p = page.eq.profileMap(output)
        return p === undefined || p === null || p.key === undefined ? "" : String(p.key)
    }

    function invoke(s: var): void {
        if (s.method === "cycleProfile")
            page.eq.cycleProfile(s.output, s.delta)
        else if (s.method === "cycleMode")
            page.eq.cycleMode(s.output, s.delta)
        else if (s.method === "cycleGraphic")
            page.eq.cycleGraphic(s.output, s.delta)
        else if (s.method === "stepGraphic")
            page.eq.stepGraphic(s.output, s.band, s.delta, s.coarse)
        else if (s.method === "stepPreamp")
            page.eq.stepPreamp(s.output, s.delta, s.coarse)
        else
            page.eq.stepBand(s.output, s.band, s.field, s.delta, s.coarse)
    }

    function stepFor(r: var, output: string, delta: int, coarse: bool): var {
        if (r.kind === "sub")
            return { method: "stepBand", output: output, band: r.band, field: r.field, delta: delta, coarse: coarse }
        if (r.key === "profile")
            return coarse ? null : { method: "cycleProfile", output: output, delta: delta, coarse: false }
        if (r.key === "mode")
            return coarse ? null : { method: "cycleMode", output: output, delta: delta, coarse: false }
        if (r.key === "preset")
            return coarse ? null : { method: "cycleGraphic", output: output, delta: delta, coarse: false }
        if (r.kind === "graphic")
            return { method: "stepGraphic", output: output, band: r.band, delta: delta, coarse: coarse }
        if (r.key === "preamp")
            return { method: "stepPreamp", output: output, delta: delta, coarse: coarse }
        return null
    }

    function step(delta: int, coarse: bool): bool {
        const r = page.focusedRow
        if (r === null)
            return false
        const s = page.stepFor(r, page.activePane.output, delta, coarse)
        if (s === null || !page.canCall(s.method))
            return false
        const before = page.snapshot(s.output)
        page.invoke(s)
        if (page.editing && page.snapshot(s.output) !== before)
            d.steps = d.steps.concat([s])
        return true
    }

    function editable(r: var): bool {
        return r !== null && r.readOnly !== true && (r.kind === "sub" || r.kind === "graphic" || r.key === "profile"
            || r.key === "preamp" || r.key === "mode" || r.key === "preset")
    }

    function beginEdit(): void {
        const r = page.focusedRow
        if (!page.hasEq || !page.editable(r))
            return
        d.steps = []
        page.editOutput = page.activePane.output
        page.editProfileKey = page.profileKey(page.editOutput)
        d.state = page.canCall("snapshot") ? page.eq.snapshot(page.editOutput) : null
        d.whole = r.key === "mode" || r.key === "preset" || r.kind === "graphic"
        page.editKey = r.key
        page.editing = true
    }

    function confirmEdit(): void {
        page.editing = false
        page.editKey = ""
        d.steps = []
    }

    function restoreProfile(output: string, key: string): void {
        if (key === "" || !page.canCall("cycleProfile") || page.profileKey(output) === key)
            return
        const list = page.eq.sources
        if (list === undefined || list === null || list.length === undefined)
            return
        const n = list.length + 2
        for (let i = 0; i < n && page.profileKey(output) !== key; i++)
            page.eq.cycleProfile(output, -1)
    }

    function cancelEdit(): void {
        if (!page.editing)
            return
        const steps = d.steps
        const output = page.editOutput
        const key = page.editProfileKey
        const state = d.state
        const whole = d.whole
        page.confirmEdit()
        if (whole && state !== null && page.canCall("restore")) {
            page.eq.restore(output, state)
            return
        }
        for (let i = steps.length - 1; i >= 0; i--) {
            const s = Object.assign({}, steps[i])
            s.delta = -s.delta
            if (page.canCall(s.method))
                page.invoke(s)
        }
        page.restoreProfile(output, key)
    }

    function handleEdit(name: string, repeat: bool): bool {
        switch (name) {
        case "left":
            page.step(-1, false)
            return true
        case "right":
            page.step(1, false)
            return true
        case "lb":
            page.step(-1, true)
            return true
        case "rb":
            page.step(1, true)
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

    function toggleBand(band: int): void {
        const pane = page.activePane
        if (pane.openBand === band) {
            pane.openBand = -1
            page.focusIndex = Math.max(0, page.indexOfKey("band" + band))
            return
        }
        pane.openBand = band
        const at = page.indexOfKey("band" + band)
        page.focusIndex = Math.max(0, at)
        pane.ensureVisible(page.focusIndex, Motion.scroll)
        pane.refreshFrom(at + 1)
    }

    function activate(): void {
        const r = page.focusedRow
        if (r === null)
            return
        if (r.kind === "row" && r.band >= 0) {
            page.toggleBand(r.band)
            return
        }
        if (r.key === "savePreset") {
            page.savePreset()
            return
        }
        if (r.key === "import") {
            page.toggleImport()
            return
        }
        if (r.kind === "file") {
            page.importFile(r.path)
            return
        }
        if (r.key === "deletePreset") {
            page.confirmDelete()
            return
        }
        if (r.key === "renamePreset") {
            page.openRename()
            return
        }
        if (r.key === "addBand") {
            page.addBand()
            return
        }
        if (r.key === "removeBand") {
            page.removeBand()
            return
        }
        page.beginEdit()
    }

    function fixFocus(): void {
        page.focusIndex = Math.max(0, Math.min(page.rows.length - 1, page.focusIndex))
    }

    function landPivot(): void {
        pivotAnim.stop()
        for (const pane of page.panes) {
            pane.x = 0
            pane.opacity = 1
            pane.settle()
            if (pane !== page.activePane)
                pane.reset()
        }
        page.shownPivot = page.pivotIndex
    }

    function switchPivot(delta: int): bool {
        const target = Math.max(0, Math.min(page.pivots.length - 1, page.pivotIndex + delta))
        if (target === page.pivotIndex)
            return false
        if (pivotAnim.running)
            page.landPivot()
        const dir = target > page.pivotIndex ? 1 : -1
        const outgoing = page.panes[page.shownPivot]
        page.panes[target].reset()
        d.internal = true
        page.pivotIndex = target
        d.internal = false
        page.focusIndex = 0
        if (!page.live) {
            page.shownPivot = target
            outgoing.reset()
            return true
        }
        pivotAnim.outgoing = outgoing
        pivotAnim.incoming = page.panes[target]
        pivotAnim.dir = dir
        pivotAnim.start()
        return true
    }

    function keepFocusVisible(): void {
        page.activePane.completeScroll()
        page.activePane.contentY = page.activePane.visibleY(page.focusIndex, page.activePane.contentY)
    }

    function completeAll(): void {
        deleteDialog.complete()
        sheet.complete()
        page.landPivot()
        for (const pane of page.panes) {
            pane.settle()
            pane.completeScroll()
        }
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
        case "pivot":
            return "tap"
        case "header":
            return "b"
        default:
            return ""
        }
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        if (page.editing || (kind !== "row" && kind !== "sub"))
            return false
        return page.openTypeSheet()
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (page.editing)
            return
        if (kind === "pivot" && index !== page.pivotIndex)
            page.switchPivot(index - page.pivotIndex)
        else if (kind === "row" || kind === "sub")
            page.activePane.ensureVisible(page.focusIndex, Motion.scroll)
    }

    function handleAction(name: string, repeat: bool): bool {
        if (deleteDialog.shown)
            return deleteDialog.handleAction(name, repeat)
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
        case "lb":
            if (!repeat)
                page.switchPivot(-1)
            return true
        case "rb":
            if (!repeat)
                page.switchPivot(1)
            return true
        case "a":
            if (!repeat)
                page.activate()
            return true
        case "x":
            if (!repeat)
                page.openTypeSheet()
            return true
        case "y":
        case "back":
            return true
        case "b":
            if (repeat)
                return false
            return page.back()
        default:
            return false
        }
    }

    onFocusIndexChanged: {
        const r = page.rows[page.focusIndex]
        d.focusKey = r === undefined || r === null ? "" : r.key
    }

    onRowsChanged: {
        const kept = d.focusKey === "" ? -1 : page.indexOfKey(d.focusKey)
        if (kept >= 0 && kept !== page.focusIndex) {
            page.focusIndex = kept
            page.activePane.ensureVisible(kept, 0)
        }
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

    onSlotChanged: {
        page.cancelEdit()
        if (sheet.shown)
            sheet.close()
        if (deleteDialog.shown)
            deleteDialog.close()
        d.deleteOutput = ""
        if (d.kbTarget !== "" && page.keyboard !== null)
            page.keyboard.dismiss()
        headphonesPane.reset()
        page.focusIndex = 0
    }

    onPivotIndexChanged: {
        if (!d.internal) {
            page.confirmEdit()
            page.landPivot()
        }
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            page.activePane.feather(1)
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        if (page.hasEq) {
            const at = page.pivots.indexOf(String(page.eq.activeOutput))
            if (at >= 0) {
                d.internal = true
                page.pivotIndex = at
                d.internal = false
            }
            if (typeof page.eq.relist === "function")
                page.eq.relist()
        }
        page.shownPivot = page.pivotIndex
    }

    QtObject {
        id: d

        property bool internal: false
        property var steps: []
        property string focusKey: ""
        property string deleteOutput: ""
        property string deleteName: ""
        property string renameOutput: ""
        property string renameName: ""
        property string kbTarget: ""
        property var state: null
        property bool whole: false
        property string typeOutput: ""
        property int typeBand: -1
        property string typeField: ""
        property string typeInitial: ""
    }

    Connections {
        target: page.keyboard
        enabled: d.kbTarget !== ""
        ignoreUnknownSignals: true

        function onDone(text) {
            const target = d.kbTarget
            d.kbTarget = ""
            if (target === "rename")
                page.renameDone(text)
        }

        function onCanceled() {
            d.kbTarget = ""
            d.renameOutput = ""
        }
    }

    Connections {
        target: page.hasEq ? page.eq : null
        ignoreUnknownSignals: true

        function onProfilesChanged() {
            page.revision++
        }

        function onImportsChanged() {
            const list = page.eq.imports
            if (list !== undefined && list !== null && list.length > 0)
                return
            for (const pane of page.panes)
                pane.importOpen = false
        }
    }

    component EqRow: Item {
        id: item

        required property int index
        required property var modelData
        property var owner: null
        readonly property bool isFocused: item.owner !== null && page.pivotIndex === item.owner.pivot && page.focusIndex === item.index
        readonly property bool editing: item.isFocused && page.editing && page.editKey === item.modelData.key
        readonly property bool sub: item.modelData.kind === "sub"
        readonly property bool file: item.modelData.kind === "file"
        readonly property string touchKind: item.sub || item.file ? "sub" : "row"
        readonly property int touchIndex: item.index
        readonly property var titleRole: item.file ? Ui.settingsSub : item.sub ? page.subRole : Ui.settingsItem
        readonly property real titleAlpha: item.isFocused ? Ui.settingsItem.alpha : Ui.settingsItem.alphaDim
        readonly property real valueAlpha: item.isFocused ? Ui.settingsValueFocused : Ui.settingsValueDim
        readonly property Item titleItem: titleText
        readonly property Item valueItem: valueText
        readonly property Item valueSlot: slot
        property string shownValue: item.modelData.value
        readonly property bool valueSwapping: valueAnim.running
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        x: Ui.px(item.shift) * (1 - item.enter)
        y: item.index * page.pitch
        width: item.owner === null ? 0 : item.owner.width
        height: page.pitch
        opacity: item.enter

        Component.onCompleted: {
            if (item.owner === null)
                return
            const old = item.owner.shownValues[item.modelData.key]
            if (old === undefined || old === item.modelData.value || !page.live) {
                item.owner.shownValues[item.modelData.key] = item.modelData.value
                return
            }
            item.shownValue = old
            valueAnim.start()
        }

        Connections {
            target: item.owner

            function onEntrance(shift, stagger, cap, duration, stripCurve, from) {
                enterAnim.stop()
                if (item.index < from) {
                    item.enter = 1
                    return
                }
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
                if (!valueAnim.running)
                    return
                valueAnim.stop()
                item.shownValue = item.modelData.value
                valueText.opacity = 1
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
                    if (item.owner !== null)
                        item.owner.shownValues[item.modelData.key] = item.modelData.value
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
            width: !item.file ? titleText.implicitWidth : valueText.visible ? slot.x - Ui.px(Ui.gutter) - titleText.x
                : item.width - Ui.px(Ui.hubEdge) - titleText.x
            text: item.modelData.title
            color: Ui.white(item.titleAlpha)
            font: Ui.font(item.titleRole)
            clip: item.file

            Behavior on color {
                enabled: page.live
                ColorAnimation {
                    duration: Motion.focusAlpha
                }
            }
        }

        Item {
            id: slot

            x: item.file ? item.width - Ui.px(Ui.hubEdge) - valueText.implicitWidth
                : titleText.x + titleText.implicitWidth + Ui.px(Ui.eqValueGap)
            width: Math.max(0, item.width - Ui.px(Ui.hubEdge) - slot.x)
            height: item.height
            clip: true

            Text {
                id: valueText

                visible: item.shownValue !== ""
                x: slot.width - valueText.width
                y: titleText.y + titleText.baselineOffset - valueText.baselineOffset
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
        }
    }

    component Pane: Flickable {
        id: pane

        property int pivot: 0
        property int openBand: -1
        property bool importOpen: false
        property var shownValues: ({})
        property int pendingFrom: -1
        readonly property string output: pane.pivot < page.pivots.length ? page.pivots[pane.pivot] : ""
        readonly property var profile: page.profileOf(pane.output, page.revision)
        readonly property bool listed: EqFormat.listed(pane.profile)
        readonly property bool graphicMode: pane.profile !== null && pane.profile !== undefined && pane.profile.mode === "graphic"
        readonly property var rows: page.buildRows(pane.profile, pane.listed ? pane.openBand : -1, pane.output === "usb" ? page.dacRanges : null, pane.importOpen)
        readonly property bool scrolling: scrollAnim.running
        readonly property real scrollTarget: scrollAnim.to
        readonly property Repeater repeater: rowRepeater
        readonly property bool touchScroll: true
        readonly property bool touchScrollEnabled: !page.editing
        property alias touch: touchScroller

        signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve, int from)
        signal settle()

        function rowItem(index: int): Item {
            return rowRepeater.itemAt(index)
        }

        function firstVisible(): int {
            return Math.max(0, Math.floor(pane.contentY / page.pitch))
        }

        function feather(dir: int): void {
            pane.entrance(dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true, pane.firstVisible())
        }

        function refreshFrom(index: int): void {
            if (!page.live || page.arriving)
                return
            pane.pendingFrom = pane.pendingFrom < 0 ? index : Math.min(pane.pendingFrom, index)
            Qt.callLater(pane.runRefresh)
        }

        function runRefresh(): void {
            const from = pane.pendingFrom
            pane.pendingFrom = -1
            if (from < 0 || !page.live)
                return
            pane.entrance(Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false,
                Math.max(from, pane.firstVisible()))
        }

        function maxContentY(): real {
            return Math.max(0, pane.contentHeight - pane.height)
        }

        function visibleY(index: int, fromY: real): real {
            const top = index * page.pitch
            let y = fromY
            if (top < y)
                y = top
            else if (top + page.pitch > y + pane.height)
                y = top + page.pitch - pane.height
            return Math.max(0, Math.min(pane.maxContentY(), y))
        }

        function scrollTo(y: real, duration: int): void {
            touchScroller.stop()
            const target = Math.max(0, Math.min(pane.maxContentY(), y))
            scrollAnim.stop()
            if (target === pane.contentY)
                return
            if (!page.live || duration <= 0) {
                pane.contentY = target
                return
            }
            scrollAnim.duration = duration
            scrollAnim.from = pane.contentY
            scrollAnim.to = target
            scrollAnim.start()
        }

        function ensureVisible(index: int, duration: int): void {
            const base = scrollAnim.running ? scrollAnim.to : pane.contentY
            pane.scrollTo(pane.visibleY(index, base), duration)
        }

        function completeScroll(): void {
            if (scrollAnim.running)
                scrollAnim.complete()
        }

        function stopScroll(): void {
            scrollAnim.stop()
        }

        function clampFocusToView(): void {
            if (page.activePane !== pane || pane.rows.length === 0)
                return
            const first = Math.ceil(pane.contentY / page.pitch - 0.001)
            const last = Math.min(pane.rows.length - 1, Math.floor((pane.contentY + pane.height) / page.pitch + 0.001) - 1)
            if (last < first)
                return
            if (page.focusIndex < first)
                page.focusIndex = first
            else if (page.focusIndex > last)
                page.focusIndex = last
        }

        function reset(): void {
            touchScroller.stop()
            scrollAnim.stop()
            pane.pendingFrom = -1
            pane.openBand = -1
            pane.importOpen = false
            pane.contentY = 0
        }

        onGraphicModeChanged: {
            if (pane.graphicMode)
                pane.importOpen = false
        }

        onListedChanged: {
            if (!pane.listed) {
                pane.openBand = -1
                return
            }
            if (page.shownPivot === pane.pivot && page.pivotIndex === pane.pivot)
                pane.refreshFrom(1)
        }

        x: 0
        y: Ui.px(page.rowsTop)
        width: page.width
        height: page.height - pane.y
        contentWidth: pane.width
        contentHeight: pane.rows.length * page.pitch
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        visible: page.shownPivot === pane.pivot

        Repeater {
            id: rowRepeater

            model: pane.rows

            delegate: EqRow {
                owner: pane
            }
        }

        NumberAnimation {
            id: scrollAnim

            target: pane
            property: "contentY"
            easing.type: Motion.easeIn
        }

        TouchScroll {
            id: touchScroller

            flick: pane
            limit: pane.maxContentY
            onMoved: pane.clampFocusToView()
        }
    }

    PageSurface {
        anchors.fill: parent
        visible: !page.embedded
    }

    Text {
        id: slotText

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(page.slotBaseline) - slotText.baselineOffset
        visible: page.embedded
        text: page.slot
        color: Ui.white(Ui.sectionLabel.alpha)
        font: Ui.font(Ui.sectionLabel)
    }

    Pane {
        id: headphonesPane

        pivot: 0
    }

    Pane {
        id: usbPane

        pivot: 1
    }

    Pane {
        id: speakerPane

        pivot: 2
    }

    FocusBar {
        id: focusBar

        visible: page.shownPivot === page.pivotIndex && page.focusedRow !== null
        rowY: page.activePane.y + page.focusIndex * page.pitch - page.activePane.contentY
        rowHeight: page.pitch
        opacity: page.activePane.opacity * page.focusOpacity * (1 - deleteDialog.scrim.opacity)
        animated: page.live
    }

    CroppedHeader {
        id: header

        visible: !page.embedded
        text: "eq"
        shift: strip.headerShift
        animated: page.live
    }

    PivotStrip {
        id: strip

        visible: !page.embedded
        labels: page.pivots
        activeIndex: page.pivotIndex
        animated: page.live
    }

    Dialog {
        id: deleteDialog

        anchors.fill: parent
        screenOff: !page.live
        title: "delete preset?"
        items: ["delete preset", "keep preset"]
        onAccepted: (index) => {
            deleteDialog.close()
            if (index === 0)
                page.runDelete()
            else
                d.deleteOutput = ""
        }
        onRejected: d.deleteOutput = ""
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: !page.live
        onAccepted: (index) => {
            sheet.close()
            if (index === 0)
                page.openType()
        }
    }

    SequentialAnimation {
        id: pivotAnim

        property Item outgoing: null
        property Item incoming: null
        property real dir: 1

        ParallelAnimation {
            NumberAnimation {
                target: pivotAnim.outgoing
                property: "x"
                to: -pivotAnim.dir * Ui.px(Motion.pivotShift)
                duration: Motion.pivotOut
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
            NumberAnimation {
                target: pivotAnim.outgoing
                property: "opacity"
                to: 0
                duration: Motion.pivotOut
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
        }
        ScriptAction {
            script: {
                page.shownPivot = page.pivotIndex
                pivotAnim.outgoing.x = 0
                pivotAnim.outgoing.opacity = 1
                pivotAnim.outgoing.reset()
                pivotAnim.incoming.feather(pivotAnim.dir)
            }
        }
        PauseAnimation {
            duration: Motion.pivotRows + Motion.pivotRowCap * Motion.pivotRowStagger
        }
    }
}
