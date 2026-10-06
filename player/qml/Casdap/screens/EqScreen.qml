pragma ComponentBehavior: Bound
import QtQuick
import "EqFormat.js" as EqFormat

Page {
    id: page

    property var eq: null
    property bool animated: true
    property bool screenOff: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property int revision: 0

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property bool hasEq: page.eq !== null && page.eq !== undefined
    readonly property var pivots: EqFormat.outputs
    readonly property var panes: [headphonesPane, usbPane, speakerPane]
    readonly property var activePane: page.panes[page.pivotIndex]
    readonly property var rows: page.activePane.rows
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
    readonly property real pitch: Ui.px(Ui.settingsPitch)
    readonly property bool switching: pivotAnim.running
    readonly property var subRole: Object.assign({}, Ui.settingsSub, { caps: Font.AllUppercase })
    property alias header: header
    property alias strip: strip
    property alias focusBar: focusBar
    property alias headphonesPane: headphonesPane
    property alias usbPane: usbPane
    property alias speakerPane: speakerPane

    pageId: 0
    focusCount: page.rows.length

    function profileOf(output: string, revision: int): var {
        if (!page.hasEq || typeof page.eq.profileMap !== "function")
            return null
        const p = page.eq.profileMap(output)
        return p === undefined ? null : p
    }

    function entry(kind: string, key: string, title: string, value: string, band: int, field: string): var {
        return { kind: kind, key: key, title: title, value: value, band: band, field: field }
    }

    function buildRows(p: var, openBand: int): var {
        const out = [page.entry("row", "profile", "profile", EqFormat.profileText(p), -1, "")]
        if (!EqFormat.listed(p))
            return out
        out.push(page.entry("row", "preamp", "preamp", EqFormat.preampText(p.preamp === undefined ? 0 : p.preamp), -1, ""))
        const bands = p.bands === undefined || p.bands === null ? [] : p.bands
        for (let i = 0; i < bands.length; i++) {
            out.push(page.entry("row", "band" + i, "band " + (i + 1), EqFormat.bandText(bands[i]), i, ""))
            if (i !== openBand)
                continue
            for (let f = 0; f < EqFormat.fields.length; f++) {
                const field = EqFormat.fields[f]
                out.push(page.entry("sub", "band" + i + ":" + field, EqFormat.fieldTitles[f],
                    EqFormat.fieldText(bands[i], field), i, field))
            }
        }
        return out
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

    function step(delta: int): bool {
        const r = page.focusedRow
        if (r === null)
            return false
        const output = page.activePane.output
        if (r.kind === "sub") {
            if (!page.canCall("stepBand"))
                return false
            page.eq.stepBand(output, r.band, r.field, delta)
            return true
        }
        if (r.key === "profile") {
            if (!page.canCall("cycleProfile"))
                return false
            page.eq.cycleProfile(output, delta)
            return true
        }
        if (r.key === "preamp") {
            if (!page.canCall("stepPreamp"))
                return false
            page.eq.stepPreamp(output, delta)
            return true
        }
        return false
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
        page.step(1)
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

    function completeAll(): void {
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

    function handleAction(name: string, repeat: bool): bool {
        switch (name) {
        case "up":
            return page.moveRow(-1)
        case "down":
            return page.moveRow(1)
        case "left":
            return page.step(-1)
        case "right":
            return page.step(1)
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

    onRowsChanged: page.fixFocus()

    onPivotIndexChanged: {
        if (!d.internal)
            page.landPivot()
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
    }

    Connections {
        target: page.hasEq ? page.eq : null
        ignoreUnknownSignals: true

        function onProfilesChanged() {
            page.revision++
        }
    }

    component EqRow: Item {
        id: item

        required property int index
        required property var modelData
        property var owner: null
        readonly property bool isFocused: item.owner !== null && page.pivotIndex === item.owner.pivot && page.focusIndex === item.index
        readonly property bool sub: item.modelData.kind === "sub"
        readonly property var titleRole: item.sub ? page.subRole : Ui.settingsItem
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
            text: item.modelData.title
            color: Ui.white(item.titleAlpha)
            font: Ui.font(item.titleRole)

            Behavior on color {
                enabled: page.live
                ColorAnimation {
                    duration: Motion.focusAlpha
                }
            }
        }

        Item {
            id: slot

            x: titleText.x + titleText.implicitWidth + Ui.px(Ui.eqValueGap)
            width: Math.max(0, item.width - Ui.px(Ui.hubEdge) - slot.x)
            height: item.height
            clip: true

            Text {
                id: valueText

                visible: item.shownValue !== ""
                x: slot.width - valueText.width
                y: titleText.y + titleText.baselineOffset - valueText.baselineOffset
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
        }
    }

    component Pane: Flickable {
        id: pane

        property int pivot: 0
        property int openBand: -1
        property var shownValues: ({})
        property int pendingFrom: -1
        readonly property string output: page.pivots[pane.pivot]
        readonly property var profile: page.profileOf(pane.output, page.revision)
        readonly property bool listed: EqFormat.listed(pane.profile)
        readonly property var rows: page.buildRows(pane.profile, pane.listed ? pane.openBand : -1)
        readonly property bool scrolling: scrollAnim.running
        readonly property real scrollTarget: scrollAnim.to
        readonly property Repeater repeater: rowRepeater

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

        function reset(): void {
            scrollAnim.stop()
            pane.pendingFrom = -1
            pane.openBand = -1
            pane.contentY = 0
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
        y: Ui.px(Ui.contentTop)
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
    }

    PageSurface {
        anchors.fill: parent
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
        opacity: page.activePane.opacity * page.focusOpacity
        animated: page.live
    }

    CroppedHeader {
        id: header

        text: "eq"
        shift: strip.headerShift
        animated: page.live
    }

    PivotStrip {
        id: strip

        labels: page.pivots
        activeIndex: page.pivotIndex
        animated: page.live
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
