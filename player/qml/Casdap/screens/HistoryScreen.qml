pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property var historyModel: null
    property var newModel: null
    property QtObject player: null
    property QtObject library: null
    property bool animated: true
    property bool screenOff: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property var recentRows: []
    property var newRows: []
    property var sheetEntry: null

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property var pivots: ["recent", "new"]
    readonly property var panes: [recentPane, newPane]
    readonly property FocusList activeList: page.panes[page.pivotIndex]
    readonly property var currentRows: page.pivotIndex === 0 ? page.recentRows : page.newRows
    readonly property int rowIndex: page.activeList.focusIndex
    readonly property var focusedEntry: page.currentRows[page.rowIndex] === undefined ? null : page.currentRows[page.rowIndex]
    readonly property bool switching: pivotAnim.running
    property alias header: header
    property alias strip: strip
    property alias recentPane: recentPane
    property alias newPane: newPane
    property alias focusBar: focusBar
    property alias sheet: sheet

    signal play(string kind, int id)
    signal sheetAction(string action, string kind, int id)

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

    function entries(model: var, defaultKind: string): var {
        const out = []
        const n = page.modelCount(model)
        for (let i = 0; i < n; i++) {
            const e = model.get(i)
            const kind = e.kind === undefined || e.kind === null || String(e.kind) === "" ? defaultKind : String(e.kind)
            out.push({
                kind: kind,
                id: e.id === undefined ? -1 : Number(e.id),
                title: e.title === undefined || e.title === null ? "" : String(e.title),
                subtitle: e.subtitle === undefined || e.subtitle === null ? "" : String(e.subtitle)
            })
        }
        return out
    }

    function rebuild(pivot: int): void {
        if (pivot === 0)
            page.recentRows = page.entries(page.historyModel, "song")
        else
            page.newRows = page.entries(page.newModel, "album")
        const pane = page.panes[pivot]
        pane.forceLayout()
        if (pane.count > 0)
            pane.snapTo(Math.min(pane.focusIndex, pane.count - 1))
        page.focusIndex = page.activeList.focusIndex
        if (d.ready && pivot === page.shownPivot && page.live && !page.arriving && !page.switching)
            Qt.callLater(page.runRefresh)
    }

    function runRefresh(): void {
        if (!page.live)
            return
        page.panes[page.shownPivot].refresh()
    }

    function moveRow(delta: int): bool {
        const before = page.activeList.focusIndex
        page.activeList.moveFocus(delta)
        page.focusIndex = page.activeList.focusIndex
        return page.activeList.focusIndex !== before
    }

    function pageRow(dir: int): bool {
        const moved = page.activeList.pageFocus(dir)
        page.focusIndex = page.activeList.focusIndex
        return moved
    }

    function albumIds(id: int): var {
        if (page.library === null || typeof page.library.albumTracks !== "function")
            return []
        const ids = []
        for (const t of page.library.albumTracks(id))
            ids.push(Number(t.id))
        return ids
    }

    function playEntry(e: var): void {
        const ids = e.kind === "song" ? [e.id] : e.kind === "album" ? page.albumIds(e.id) : []
        if (page.player !== null && typeof page.player.playTracks === "function" && ids.length > 0) {
            page.player.playTracks(ids, 0)
            return
        }
        page.play(e.kind, e.id)
    }

    function pinLabel(kind: string, id: int): string {
        const pinned = page.library !== null && typeof page.library.isPinned === "function"
            && page.library.isPinned(kind, id) === true
        return pinned ? "unpin from quickplay" : "pin to quickplay"
    }

    function openSheet(): bool {
        const e = page.focusedEntry
        if (e === null)
            return false
        page.sheetEntry = e
        sheet.title = e.title
        sheet.items = ["play", "add to queue", page.pinLabel(e.kind, e.id)]
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
        page.focusIndex = page.activeList.focusIndex
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
            if (!repeat && page.focusedEntry !== null)
                page.playEntry(page.focusedEntry)
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
        if (!d.internal)
            page.landPivot()
        page.focusIndex = page.activeList.focusIndex
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            page.panes[page.shownPivot].feather(1)
    }

    onHistoryModelChanged: page.rebuild(0)
    onNewModelChanged: page.rebuild(1)

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        page.rebuild(0)
        page.rebuild(1)
        page.shownPivot = page.pivotIndex
        d.ready = true
    }

    QtObject {
        id: d

        property bool internal: false
        property bool ready: false
    }

    component ModelWatch: QtObject {
        id: watch

        property var model: null
        property int pivot: 0

        readonly property Connections link: Connections {
            target: watch.model
            ignoreUnknownSignals: true

            function onModelReset() {
                page.rebuild(watch.pivot)
            }

            function onRowsInserted() {
                page.rebuild(watch.pivot)
            }

            function onRowsRemoved() {
                page.rebuild(watch.pivot)
            }

            function onDataChanged() {
                page.rebuild(watch.pivot)
            }
        }
    }

    ModelWatch {
        model: page.historyModel
        pivot: 0
    }

    ModelWatch {
        model: page.newModel
        pivot: 1
    }

    component HistoryRow: Item {
        id: row

        required property int index
        required property var modelData
        readonly property var pane: row.ListView.view
        readonly property bool isFocused: page.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property Item rowItem: listRow
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        width: row.pane.width
        height: row.pane.rowPitch
        transform: Translate { x: Ui.px(row.shift) * (1 - row.enter) }
        opacity: row.enter

        Connections {
            target: row.pane

            function onEntrance(shift, stagger, cap, duration, stripCurve) {
                enterAnim.stop()
                row.shift = shift
                row.stagger = Math.min(cap, Math.max(0, row.index - row.pane.firstVisible())) * stagger
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
            primary: row.modelData.title
            secondary: row.modelData.subtitle
            focused: row.isFocused
            pressed: row.isFocused && page.pressed
            animated: page.live
        }
    }

    component Pane: FocusList {
        id: pane

        property int pivot: 0

        signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(pane.contentY / pane.rowPitch))
        }

        function feather(dir: int): void {
            pane.forceLayout()
            pane.entrance(dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        function refresh(): void {
            pane.forceLayout()
            pane.entrance(Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false)
        }

        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - pane.y
        visible: page.shownPivot === pane.pivot
    }

    PageSurface {
        anchors.fill: parent
    }

    Pane {
        id: recentPane

        pivot: 0
        model: page.recentRows
        delegate: HistoryRow {}
    }

    Pane {
        id: newPane

        pivot: 1
        model: page.newRows
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
