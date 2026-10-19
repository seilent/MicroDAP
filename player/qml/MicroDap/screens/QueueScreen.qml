pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: screen

    property QtObject player: null
    property bool animated: true
    property bool screenOff: false
    property var sheetItems: []
    property int sheetRow: -1
    property string shownCount: ""
    property var focusedRow: null

    readonly property bool live: screen.animated && !screen.screenOff
    readonly property bool arriving: !screen.arrived && screen.live && screen.stackAnimates
    readonly property var queue: screen.player !== null && screen.player.queue !== undefined && screen.player.queue !== null
        ? screen.player.queue : null
    readonly property var order: screen.queue !== null && screen.queue.playOrder !== undefined && screen.queue.playOrder !== null
        ? screen.queue.playOrder : null
    readonly property int count: screen.order !== null && typeof screen.order.count === "number" ? screen.order.count : 0
    readonly property int currentIndex: screen.queue !== null && typeof screen.queue.index === "number" ? screen.queue.index : -1
    readonly property int currentSlot: screen.queue !== null && typeof screen.queue.currentSlot === "number" ? screen.queue.currentSlot : screen.currentIndex
    readonly property string countText: (screen.count > 0 && screen.currentSlot >= 0 ? screen.currentSlot + 1 : 0) + " of " + screen.count
    readonly property int rowIndex: queueList.focusIndex
    readonly property bool countSwapping: countAnim.running

    property alias header: header
    property alias countItem: countLabel
    property alias focusBar: focusBar
    property alias sheet: sheet

    list: queueList

    function slotEntry(slot: int): var {
        if (screen.order === null || slot < 0 || slot >= screen.count || typeof screen.order.get !== "function")
            return null
        const e = screen.order.get(slot)
        if (e === undefined || e === null)
            return null
        return {
            id: e.id === undefined || e.id === null ? -1 : Number(e.id),
            title: e.title === undefined || e.title === null ? "" : String(e.title),
            subtitle: e.artist === undefined || e.artist === null ? "" : String(e.artist),
            row: e.row === undefined || e.row === null ? slot : Number(e.row)
        }
    }

    function rowOf(slot: int): int {
        const e = screen.slotEntry(slot)
        return e === null ? -1 : e.row
    }

    function syncFocus(): void {
        screen.focusIndex = queueList.focusIndex
        screen.focusedRow = screen.slotEntry(queueList.focusIndex)
    }

    function fixFocus(): void {
        queueList.forceLayout()
        if (screen.count === 0) {
            queueList.focusIndex = 0
            screen.syncFocus()
            return
        }
        queueList.snapTo(Math.min(queueList.focusIndex, screen.count - 1))
        screen.syncFocus()
    }

    function rowsUpdated(from: int): void {
        d.fixPending = true
        Qt.callLater(screen.applyFix)
        screen.refreshFrom(from)
    }

    function applyFix(): void {
        if (!d.fixPending)
            return
        d.fixPending = false
        screen.fixFocus()
    }

    function refreshFrom(index: int): void {
        if (!screen.live || !d.ready)
            return
        d.pendingFrom = d.pendingFrom < 0 ? index : Math.min(d.pendingFrom, index)
        Qt.callLater(screen.runRefresh)
    }

    function runRefresh(): void {
        const from = d.pendingFrom
        d.pendingFrom = -1
        if (from < 0 || !screen.live)
            return
        queueList.forceLayout()
        queueList.entrance(Math.max(from, queueList.firstVisible()), Motion.listChangeShift, Motion.listChangeStagger,
            Motion.listChangeCap, Motion.listChange, false)
    }

    function swapCount(): void {
        countAnim.stop()
        if (!screen.live || !d.ready || screen.shownCount === screen.countText) {
            screen.shownCount = screen.countText
            countLabel.opacity = 1
            return
        }
        countAnim.start()
    }

    function moveRow(delta: int): bool {
        if (screen.count === 0)
            return false
        const target = Math.max(0, Math.min(screen.count - 1, queueList.focusIndex + delta))
        if (target === queueList.focusIndex)
            return false
        queueList.setFocus(target, Motion.scroll)
        screen.syncFocus()
        return true
    }

    function pageRow(direction: int): bool {
        const moved = queueList.pageFocus(direction)
        screen.syncFocus()
        return moved
    }

    function verbsFor(index: int): var {
        const items = ["remove"]
        const cur = screen.currentSlot
        if (index !== cur && index !== cur + 1)
            items.push("play next")
        if (index > 0)
            items.push("move up")
        if (index < screen.count - 1)
            items.push("move down")
        return items
    }

    function removeRow(index: int): bool {
        if (screen.queue === null || typeof screen.queue.remove !== "function" || index < 0 || index >= screen.count)
            return false
        screen.queue.remove(screen.rowOf(index))
        screen.applyFix()
        return true
    }

    function moveEntry(from: int, to: int): bool {
        if (screen.queue === null)
            return false
        if (from < 0 || to < 0 || from >= screen.count || to >= screen.count || from === to)
            return false
        if (typeof screen.queue.moveSlot === "function")
            screen.queue.moveSlot(from, to)
        else if (typeof screen.queue.move === "function")
            screen.queue.move(from, to)
        else
            return false
        screen.applyFix()
        queueList.setFocus(to, Motion.scroll)
        screen.syncFocus()
        return true
    }

    function playNextRow(index: int): bool {
        const cur = screen.currentSlot
        if (cur < 0 || index === cur || index === cur + 1 || index < 0 || index >= screen.count)
            return false
        const to = index < cur ? cur : cur + 1
        if (screen.queue !== null && typeof screen.queue.playNextRow === "function") {
            screen.queue.playNextRow(screen.rowOf(index))
            screen.applyFix()
            queueList.setFocus(to, Motion.scroll)
            screen.syncFocus()
            return true
        }
        return screen.moveEntry(index, to)
    }

    function runVerb(action: string, index: int): bool {
        switch (action) {
        case "remove":
            return screen.removeRow(index)
        case "play next":
            return screen.playNextRow(index)
        case "move up":
            return screen.moveEntry(index, index - 1)
        case "move down":
            return screen.moveEntry(index, index + 1)
        default:
            return false
        }
    }

    function activate(): void {
        if (screen.focusedRow === null)
            return
        if (screen.player !== null && typeof screen.player.playAt === "function") {
            screen.player.playAt(screen.rowOf(screen.rowIndex))
            return
        }
        console.info("microdap: queue play " + screen.rowIndex)
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        return kind === "row" && screen.openSheet()
    }

    function openSheet(): bool {
        if (screen.focusedRow === null)
            return false
        screen.sheetRow = screen.rowIndex
        screen.sheetItems = screen.verbsFor(screen.rowIndex)
        sheet.title = screen.focusedRow.title
        sheet.items = screen.sheetItems
        sheet.open()
        return true
    }

    function runSheet(index: int): void {
        const action = screen.sheetItems[index]
        if (action === undefined || screen.sheetRow < 0)
            return
        screen.runVerb(action, screen.sheetRow)
    }

    function belowIsNowPlaying(): bool {
        if (screen.stack === null || screen.stack.depth < 2)
            return false
        const holder = screen.stack.pages[screen.stack.depth - 2]
        return holder !== undefined && holder.content !== null && holder.content.objectName === "nowplaying"
    }

    function back(): bool {
        if (screen.stack === null || screen.stack.depth < 2)
            return false
        screen.stack.pop()
        return true
    }

    function keepFocusVisible(): void {
        queueList.keepFocusVisible()
    }

    function completeAll(): void {
        queueList.settle()
        queueList.completeScroll()
        countAnim.stop()
        screen.shownCount = screen.countText
        countLabel.opacity = 1
        sheet.complete()
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        switch (name) {
        case "up":
            return screen.moveRow(-1)
        case "down":
            return screen.moveRow(1)
        case "lt":
            return screen.pageRow(-1)
        case "rt":
            return screen.pageRow(1)
        case "a":
            if (!repeat)
                screen.activate()
            return true
        case "x":
            if (!repeat)
                screen.openSheet()
            return true
        case "y":
            if (!repeat)
                screen.removeRow(screen.rowIndex)
            return true
        case "b":
            if (repeat)
                return false
            return screen.back()
        case "start":
            if (repeat)
                return true
            if (!screen.belowIsNowPlaying())
                return false
            return screen.back()
        default:
            return false
        }
    }

    onLiveChanged: {
        if (!screen.live)
            screen.completeAll()
    }

    onCountTextChanged: screen.swapCount()

    onEntered: (first, kind) => {
        if (screen.arriving)
            queueList.feather()
    }

    onOrderChanged: {
        if (d.ready)
            screen.fixFocus()
    }

    Component.onCompleted: {
        queueList.forceLayout()
        if (screen.count > 0)
            queueList.snapTo(Math.max(0, screen.currentSlot))
        screen.syncFocus()
        screen.shownCount = screen.countText
        d.ready = true
    }

    QtObject {
        id: d

        property bool ready: false
        property bool fixPending: false
        property int pendingFrom: -1
    }

    Connections {
        target: screen.order
        ignoreUnknownSignals: true

        function onModelReset() {
            screen.rowsUpdated(queueList.firstVisible())
        }

        function onRowsInserted(parent, first, last) {
            screen.rowsUpdated(first)
        }

        function onRowsRemoved(parent, first, last) {
            screen.rowsUpdated(first)
        }

        function onRowsMoved(parent, start, end, destination, row) {
            screen.rowsUpdated(Math.min(start, row))
        }

        function onDataChanged() {
            if (d.ready)
                screen.syncFocus()
        }
    }

    component QueueRow: Item {
        id: item

        required property int index
        required property int number
        required property string title
        required property string artist
        required property bool isCurrent
        readonly property bool isFocused: queueList.focusIndex === item.index
        readonly property bool isPlaying: item.isCurrent
        readonly property Item rowItem: listRow
        readonly property string touchKind: "row"
        readonly property int touchIndex: item.index
        property real enter: screen.arriving ? 0 : 1
        property real shift: screen.arriving ? Motion.pivotShift : Motion.listChangeShift
        property int stagger: 0
        property int enterDuration: Motion.listChange
        property var enterCurve: Motion.decel

        width: queueList.width
        height: queueList.rowPitch
        transform: Translate { x: Ui.px(item.shift) * (1 - item.enter) }
        opacity: item.enter

        ListView.onPooled: {
            enterAnim.stop()
            item.enter = 1
        }

        Connections {
            target: queueList

            function onEntrance(from, shift, stagger, cap, duration, stripCurve) {
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

        ListRow {
            id: listRow

            width: item.width
            number: String(item.number)
            primary: item.title
            secondary: item.artist
            playing: item.isPlaying
            focused: item.isFocused
            pressed: item.isFocused && screen.pressed
            animated: screen.live
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    FocusList {
        id: queueList

        signal entrance(int from, real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(queueList.contentY / queueList.rowPitch))
        }

        function feather(): void {
            queueList.forceLayout()
            queueList.entrance(queueList.firstVisible(), Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        x: 0
        y: Ui.px(Ui.queueRowsTop)
        width: screen.width
        height: screen.height - queueList.y
        model: screen.order
        delegate: QueueRow {}
        onFocusIndexChanged: {
            if (d.ready)
                screen.syncFocus()
        }
        onTouchFocused: screen.syncFocus()
    }

    FocusBar {
        id: focusBar

        visible: screen.focusedRow !== null
        x: Ui.px(Ui.hubEdge - Ui.focusBarOffset)
        rowY: queueList.y + queueList.focusY
        rowHeight: queueList.rowPitch
        opacity: screen.focusOpacity
        animated: screen.live
    }

    CroppedHeader {
        id: header

        text: "queue"
        animated: screen.live
    }

    Text {
        id: countLabel

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(Ui.queueCountBaseline) - countLabel.baselineOffset
        text: screen.shownCount
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }

    SequentialAnimation {
        id: countAnim

        NumberAnimation {
            target: countLabel
            property: "opacity"
            to: Motion.valueDip
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
        ScriptAction {
            script: screen.shownCount = screen.countText
        }
        NumberAnimation {
            target: countLabel
            property: "opacity"
            to: 1
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: !screen.live
        onAccepted: (index) => {
            screen.runSheet(index)
            sheet.close()
        }
    }
}
