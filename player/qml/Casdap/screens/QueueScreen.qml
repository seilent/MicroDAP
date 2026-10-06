pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: screen

    property QtObject player: null
    property bool animated: true
    property bool screenOff: false
    property var rows: []
    property var sheetItems: []
    property int sheetRow: -1

    readonly property bool live: screen.animated && !screen.screenOff
    readonly property var queue: screen.player !== null && screen.player.queue !== undefined && screen.player.queue !== null
        ? screen.player.queue : null
    readonly property int count: screen.rows.length
    readonly property int currentIndex: screen.queue !== null && typeof screen.queue.index === "number" ? screen.queue.index : -1
    readonly property string countText: (screen.count > 0 && screen.currentIndex >= 0 ? screen.currentIndex + 1 : 0) + " of " + screen.count
    readonly property int rowIndex: queueList.focusIndex
    readonly property var focusedRow: screen.rows[screen.rowIndex] === undefined ? null : screen.rows[screen.rowIndex]

    property alias header: header
    property alias countItem: countLabel
    property alias focusBar: focusBar
    property alias sheet: sheet

    list: queueList

    function modelCount(): int {
        if (screen.queue === null)
            return 0
        if (typeof screen.queue.count === "number")
            return screen.queue.count
        if (typeof screen.queue.rowCount === "function")
            return screen.queue.rowCount()
        return 0
    }

    function rebuild() {
        const rows = []
        const n = screen.modelCount()
        for (let i = 0; i < n; i++) {
            const e = screen.queue.get(i)
            if (e === undefined || e === null)
                break
            rows.push({
                id: e.id === undefined ? -1 : Number(e.id),
                title: e.title === undefined || e.title === null ? "" : String(e.title),
                subtitle: e.artist === undefined || e.artist === null ? "" : String(e.artist),
                number: String(i + 1),
                row: i
            })
        }
        screen.rows = rows
        screen.fixFocus()
    }

    function fixFocus() {
        queueList.forceLayout()
        if (screen.rows.length === 0) {
            queueList.focusIndex = 0
            screen.focusIndex = 0
            return
        }
        queueList.snapTo(Math.min(queueList.focusIndex, screen.rows.length - 1))
        screen.focusIndex = queueList.focusIndex
    }

    function refreshFrom(index: int) {
        if (!screen.live)
            return
        d.pendingFrom = d.pendingFrom < 0 ? index : Math.min(d.pendingFrom, index)
        Qt.callLater(screen.runRefresh)
    }

    function runRefresh() {
        const from = d.pendingFrom
        d.pendingFrom = -1
        if (from < 0 || !screen.live)
            return
        queueList.forceLayout()
        queueList.entrance(Math.max(from, queueList.firstVisible()))
    }

    function moveRow(delta: int): bool {
        if (screen.count === 0)
            return false
        const target = Math.max(0, Math.min(screen.count - 1, queueList.focusIndex + delta))
        if (target === queueList.focusIndex)
            return false
        queueList.setFocus(target, Motion.scroll)
        screen.focusIndex = queueList.focusIndex
        return true
    }

    function verbsFor(index: int): var {
        const items = ["remove"]
        const cur = screen.currentIndex
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
        screen.queue.remove(index)
        return true
    }

    function moveEntry(from: int, to: int): bool {
        if (screen.queue === null || typeof screen.queue.move !== "function")
            return false
        if (from < 0 || to < 0 || from >= screen.count || to >= screen.count || from === to)
            return false
        screen.queue.move(from, to)
        queueList.setFocus(to, Motion.scroll)
        screen.focusIndex = queueList.focusIndex
        return true
    }

    function playNextRow(index: int): bool {
        const cur = screen.currentIndex
        if (cur < 0 || index === cur || index === cur + 1)
            return false
        return screen.moveEntry(index, index < cur ? cur : cur + 1)
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

    function activate() {
        if (screen.focusedRow === null)
            return
        if (screen.player !== null && typeof screen.player.playAt === "function") {
            screen.player.playAt(screen.rowIndex)
            return
        }
        console.info("casdap: queue play " + screen.rowIndex)
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

    function runSheet(index: int) {
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

    function completeAll() {
        queueList.settle()
        queueList.completeScroll()
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
            return screen.pageFocus(-1)
        case "rt":
            return screen.pageFocus(1)
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

    onQueueChanged: {
        if (d.ready)
            screen.rebuild()
    }

    Component.onCompleted: {
        screen.rebuild()
        if (screen.count > 0)
            queueList.snapTo(Math.max(0, screen.currentIndex))
        screen.focusIndex = queueList.focusIndex
        d.ready = true
    }

    QtObject {
        id: d

        property bool ready: false
        property int pendingFrom: -1
    }

    Connections {
        target: screen.queue
        ignoreUnknownSignals: true

        function onModelReset() {
            screen.rebuild()
            screen.refreshFrom(0)
        }

        function onRowsInserted(parent, first, last) {
            screen.rebuild()
            screen.refreshFrom(first)
        }

        function onRowsRemoved(parent, first, last) {
            screen.rebuild()
            screen.refreshFrom(first)
        }

        function onRowsMoved(parent, start, end, destination, row) {
            screen.rebuild()
            screen.refreshFrom(Math.min(start, row))
        }
    }

    component QueueRow: Item {
        id: row

        required property int index
        required property var modelData
        readonly property bool isFocused: queueList.focusIndex === row.index
        readonly property bool isPlaying: screen.currentIndex === row.index
        readonly property Item rowItem: listRow
        property real enter: 1
        property int stagger: 0

        width: queueList.width
        height: queueList.rowPitch
        x: Ui.px(Motion.listChangeShift) * (1 - row.enter)
        opacity: row.enter

        Connections {
            target: queueList

            function onEntrance(from) {
                if (row.index < from)
                    return
                enterAnim.stop()
                row.stagger = Math.min(Motion.listChangeCap, row.index - from) * Motion.listChangeStagger
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
                duration: Motion.listChange
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
        }

        ListRow {
            id: listRow

            width: row.width
            number: row.modelData.number
            primary: row.modelData.title
            secondary: row.modelData.subtitle
            playing: row.isPlaying
            focused: row.isFocused
            pressed: row.isFocused && screen.pressed
            animated: screen.live
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    FocusList {
        id: queueList

        signal entrance(int from)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(queueList.contentY / queueList.rowPitch))
        }

        x: 0
        y: Ui.px(Ui.queueRowsTop)
        width: screen.width
        height: screen.height - queueList.y
        model: screen.rows
        delegate: QueueRow {}
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
        text: screen.countText
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
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
