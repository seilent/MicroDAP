pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property var historyModel: null
    property var newModel: null
    property QtObject player: null
    readonly property int currentId: page.player !== null && page.player.current !== undefined && page.player.current !== null
        && page.player.current.id !== undefined && page.player.current.id !== null ? Number(page.player.current.id) : -1
    property QtObject library: null
    property bool animated: true
    property bool screenOff: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property var focusedEntry: null
    property var sheetEntry: null

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property var pivots: ["recent", "new"]
    readonly property var kinds: ["song", "album"]
    readonly property var panes: [recentPane, newPane]
    readonly property FocusList activeList: page.panes[page.pivotIndex]
    readonly property int rowIndex: page.activeList.focusIndex
    readonly property bool switching: pivotAnim.running
    property alias header: header
    property alias strip: strip
    property alias recentPane: recentPane
    property alias newPane: newPane
    property alias focusBar: focusBar
    property alias sheet: sheet

    signal play(string kind, int id)
    signal sheetAction(string action, string kind, int id)
    signal listPlayed(int count)

    list: page.activeList

    function modelCount(model: var): int {
        if (model === null || model === undefined)
            return 0
        if (typeof model.count === "number")
            return model.count
        if (typeof model.rowCount === "function")
            return model.rowCount()
        return 0
    }

    function modelAt(pivot: int): var {
        return pivot === 0 ? page.historyModel : page.newModel
    }

    function entryAt(pivot: int, index: int): var {
        const model = page.modelAt(pivot)
        if (index < 0 || index >= page.modelCount(model) || typeof model.get !== "function")
            return null
        const e = model.get(index)
        if (e === undefined || e === null)
            return null
        const kind = e.kind === undefined || e.kind === null || String(e.kind) === "" ? page.kinds[pivot] : String(e.kind)
        return {
            kind: kind,
            id: e.id === undefined || e.id === null ? -1 : Number(e.id),
            title: e.title === undefined || e.title === null ? "" : String(e.title),
            subtitle: e.subtitle === undefined || e.subtitle === null ? "" : String(e.subtitle)
        }
    }

    function syncFocus(): void {
        page.focusIndex = page.activeList.focusIndex
        page.focusedEntry = page.entryAt(page.pivotIndex, page.activeList.focusIndex)
    }

    function fixFocus(pivot: int): void {
        const pane = page.panes[pivot]
        pane.forceLayout()
        const n = page.modelCount(page.modelAt(pivot))
        if (n > 0)
            pane.snapTo(Math.min(pane.focusIndex, n - 1))
        else
            pane.focusIndex = 0
        page.syncFocus()
    }

    function rowsUpdated(pivot: int, from: int): void {
        page.fixFocus(pivot)
        if (d.ready && pivot === page.shownPivot && page.live && !page.arriving && !page.switching)
            page.refreshFrom(from < 0 ? page.panes[pivot].focusIndex : from)
    }

    function refreshFrom(index: int): void {
        d.pendingFrom = d.pendingFrom < 0 ? index : Math.min(d.pendingFrom, index)
        Qt.callLater(page.runRefresh)
    }

    function runRefresh(): void {
        const from = d.pendingFrom
        d.pendingFrom = -1
        if (from < 0 || !page.live)
            return
        const pane = page.panes[page.shownPivot]
        pane.refresh(from)
    }

    function moveRow(delta: int): bool {
        const before = page.activeList.focusIndex
        page.activeList.moveFocus(delta)
        page.syncFocus()
        return page.activeList.focusIndex !== before
    }

    function pageRow(dir: int): bool {
        const moved = page.activeList.pageFocus(dir)
        page.syncFocus()
        return moved
    }

    function playEntry(e: var): int {
        if (page.player !== null && typeof page.player.playSource === "function" && (e.kind === "song" || e.kind === "album"))
            return Number(page.player.playSource(e.kind, e.id, "play", -1))
        page.play(e.kind, e.id)
        return 0
    }

    function pinLabel(kind: string, id: int): string {
        const pinned = page.library !== null && typeof page.library.isPinned === "function"
            && page.library.isPinned(kind, id) === true
        return pinned ? "unpin from quickplay" : "pin to quickplay"
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        return kind === "row" && page.openSheet()
    }

    function openSheet(): bool {
        const e = page.focusedEntry
        if (e === null)
            return false
        page.sheetEntry = e
        sheet.title = e.title
        sheet.items = e.kind === "song"
            ? ["play", "play next", "add to queue", page.pinLabel(e.kind, e.id)]
            : ["play", "play next", "add to queue", "shuffle", page.pinLabel(e.kind, e.id)]
        sheet.open()
        return true
    }

    function runSheet(index: int): void {
        const e = page.sheetEntry
        if (e === null)
            return
        const action = sheet.items[index]
        if (action === "play")
            page.playEntry(e)
        else
            page.sheetAction(action, e.kind, e.id)
    }

    function landPivot(): void {
        pivotAnim.stop()
        for (const pane of page.panes) {
            pane.x = 0
            pane.opacity = 1
            pane.settle()
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
        d.internal = true
        page.pivotIndex = target
        d.internal = false
        page.syncFocus()
        if (!page.live) {
            page.shownPivot = target
            return true
        }
        pivotAnim.outgoing = page.panes[page.shownPivot]
        pivotAnim.incoming = page.panes[target]
        pivotAnim.dir = dir
        pivotAnim.start()
        return true
    }

    function keepFocusVisible(): void {
        for (const pane of page.panes)
            pane.keepFocusVisible()
    }

    function completeAll(): void {
        page.landPivot()
        for (const pane of page.panes)
            pane.completeScroll()
        sheet.complete()
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        page.stack.pop()
        return true
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        switch (kind) {
        case "row":
            page.activeList.touchFocus(index)
            return "a"
        case "pivot":
            return "tap"
        case "header":
            return "b"
        default:
            return ""
        }
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (kind === "pivot" && index !== page.pivotIndex)
            page.switchPivot(index - page.pivotIndex)
        else if (kind === "row")
            page.activeList.ensureVisible(Motion.scroll)
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
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
            if (!repeat && page.focusedEntry !== null) {
                page.listPlayed(page.playEntry(page.focusedEntry))
            }
            return true
        case "x":
            if (!repeat)
                page.openSheet()
            return true
        case "y":
            if (!repeat && page.focusedEntry !== null)
                page.sheetAction("add to queue", page.focusedEntry.kind, page.focusedEntry.id)
            return true
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

    onPivotIndexChanged: {
        if (!d.ready)
            return
        if (!d.internal)
            page.landPivot()
        page.syncFocus()
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            page.panes[page.shownPivot].feather(1)
    }

    onHistoryModelChanged: {
        if (d.ready)
            page.fixFocus(0)
    }
    onNewModelChanged: {
        if (d.ready)
            page.fixFocus(1)
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        page.fixFocus(0)
        page.fixFocus(1)
        page.shownPivot = page.pivotIndex
        page.syncFocus()
        d.ready = true
    }

    QtObject {
        id: d

        property bool internal: false
        property bool ready: false
        property int pendingFrom: -1
    }

    function rowsDataChanged(pivot: int): void {
        if (d.ready && pivot === page.pivotIndex)
            page.syncFocus()
    }

    Connections {
        target: page.historyModel
        ignoreUnknownSignals: true

        function onModelReset() {
            page.rowsUpdated(0, -1)
        }

        function onRowsInserted(parent, first, last) {
            page.rowsUpdated(0, first)
        }

        function onRowsRemoved(parent, first, last) {
            page.rowsUpdated(0, first)
        }

        function onRowsMoved(parent, start, end, destination, row) {
            page.rowsUpdated(0, Math.min(start, row))
        }

        function onDataChanged() {
            page.rowsDataChanged(0)
        }
    }

    Connections {
        target: page.newModel
        ignoreUnknownSignals: true

        function onModelReset() {
            page.rowsUpdated(1, -1)
        }

        function onRowsInserted(parent, first, last) {
            page.rowsUpdated(1, first)
        }

        function onRowsRemoved(parent, first, last) {
            page.rowsUpdated(1, first)
        }

        function onRowsMoved(parent, start, end, destination, row) {
            page.rowsUpdated(1, Math.min(start, row))
        }

        function onDataChanged() {
            page.rowsDataChanged(1)
        }
    }

    component HistoryRow: Item {
        id: row

        required property int index
        required property string title
        required property string subtitle
        required property var model
        readonly property var pane: row.ListView.view
        readonly property bool isFocused: row.pane !== null && page.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property Item rowItem: listRow
        readonly property string touchKind: "row"
        readonly property int touchIndex: row.index
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        width: row.pane === null ? 0 : row.pane.width
        height: row.pane === null ? 0 : row.pane.rowPitch
        transform: Translate { x: Ui.px(row.shift) * (1 - row.enter) }
        opacity: row.enter

        ListView.onPooled: {
            enterAnim.stop()
            row.enter = 1
        }

        Connections {
            target: row.pane

            function onEntrance(from, shift, stagger, cap, duration, stripCurve) {
                if (row.index < from || row.pane === null)
                    return
                enterAnim.stop()
                row.shift = shift
                row.stagger = Math.min(cap, Math.max(0, row.index - Math.max(from, row.pane.firstVisible()))) * stagger
                row.enterDuration = duration
                row.enterCurve = stripCurve ? Motion.strip : Motion.decel
                row.enter = 0
                enterAnim.start()
            }

            function onSettle() {
                enterAnim.stop()
                row.enter = 1
            }
        }

        SequentialAnimation {
            id: enterAnim

            PauseAnimation {
                duration: row.stagger
            }

            NumberAnimation {
                target: row
                property: "enter"
                from: 0
                to: 1
                duration: row.enterDuration
                easing.type: Motion.bezier
                easing.bezierCurve: row.enterCurve
            }
        }

        ListRow {
            id: listRow

            width: row.width
            primary: row.title
            secondary: row.subtitle
            playing: row.model.kind === "song" && page.currentId >= 0 && Number(row.model.id) === page.currentId
            focused: row.isFocused
            pressed: row.isFocused && page.pressed
            animated: page.live
        }
    }

    component Pane: FocusList {
        id: pane

        property int pivot: 0

        signal entrance(int from, real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(pane.contentY / pane.rowPitch))
        }

        function feather(dir: int): void {
            pane.forceLayout()
            pane.entrance(0, dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        function refresh(from: int): void {
            pane.forceLayout()
            pane.entrance(from, Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false)
        }

        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - pane.y
        visible: page.shownPivot === pane.pivot
        onFocusIndexChanged: {
            if (d.ready && page.activeList === pane)
                page.syncFocus()
        }
        onTouchFocused: page.syncFocus()
    }

    PageSurface {
        anchors.fill: parent
    }

    Pane {
        id: recentPane

        pivot: 0
        model: page.historyModel
        delegate: HistoryRow {}
    }

    Pane {
        id: newPane

        pivot: 1
        model: page.newModel
        delegate: HistoryRow {}
    }

    FocusBar {
        id: focusBar

        visible: page.shownPivot === page.pivotIndex && page.focusedEntry !== null
        rowY: page.activeList.y + page.activeList.focusY
        rowHeight: page.activeList.rowPitch
        opacity: page.activeList.opacity * page.focusOpacity
        animated: page.live
    }

    CroppedHeader {
        id: header

        text: "history"
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
        screenOff: !page.live
        onAccepted: (index) => {
            page.runSheet(index)
            sheet.close()
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
                pivotAnim.incoming.feather(pivotAnim.dir)
            }
        }
        PauseAnimation {
            duration: Motion.pivotRows + Motion.pivotRowCap * Motion.pivotRowStagger
        }
    }
}
