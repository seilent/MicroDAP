pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property var playlistsModel: null
    property QtObject player: null
    property QtObject library: null
    property bool animated: true
    property bool screenOff: false
    property var rows: []
    property var sheetRow: null
    property string shownCount: ""

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property int queueCount: page.player !== null && page.player.queue !== undefined && page.player.queue !== null
        ? page.player.queue.count : 0
    readonly property int savedCount: page.modelCount(page.playlistsModel)
    readonly property string countLine: page.savedCount === 1 ? "1 playlist" : page.savedCount + " playlists"
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
    readonly property bool countSwapping: countAnim.running
    property alias header: header
    property alias countText: countText
    property alias listView: listView
    property alias focusBar: focusBar
    property alias sheet: sheet

    signal nowPlaying()
    signal play(string kind, int id)
    signal sheetAction(string action, string kind, int id)

    list: listView

    function modelCount(model: var): int {
        if (model === null || model === undefined)
            return 0
        if (typeof model.count === "number")
            return model.count
        if (typeof model.rowCount === "function")
            return model.rowCount()
        return 0
    }

    function songsLabel(n: int): string {
        return n + (n === 1 ? " song" : " songs")
    }

    function buildRows(): var {
        const out = [{ kind: "nowplaying", id: -1, title: "now playing", subtitle: page.songsLabel(page.queueCount) }]
        const n = page.modelCount(page.playlistsModel)
        for (let i = 0; i < n; i++) {
            const e = page.playlistsModel.get(i)
            const title = e.title !== undefined && e.title !== null ? String(e.title)
                : e.name !== undefined && e.name !== null ? String(e.name) : ""
            out.push({
                kind: "playlist",
                id: e.id === undefined ? -1 : Number(e.id),
                title: title,
                subtitle: e.subtitle === undefined || e.subtitle === null ? "" : String(e.subtitle)
            })
        }
        return out
    }

    function rebuild() {
        page.rows = page.buildRows()
        listView.forceLayout()
        if (listView.count > 0)
            listView.snapTo(Math.min(listView.focusIndex, listView.count - 1))
        page.focusIndex = listView.focusIndex
    }

    function reload(): void {
        page.rebuild()
        if (d.ready && page.live && !page.arriving)
            Qt.callLater(page.runRefresh)
    }

    function runRefresh(): void {
        if (page.live)
            listView.refresh()
    }

    function swapCount(): void {
        countAnim.stop()
        if (!page.live || !d.ready || page.shownCount === page.countLine) {
            page.shownCount = page.countLine
            countText.opacity = 1
            return
        }
        countAnim.start()
    }

    function playRow(r: var) {
        if (r.kind === "nowplaying") {
            page.nowPlaying()
            return
        }
        if (page.player !== null && typeof page.player.playSource === "function") {
            page.player.playSource("playlist", r.id, "play", -1)
            return
        }
        page.play("playlist", r.id)
    }

    function pinLabel(id: int): string {
        const pinned = page.library !== null && typeof page.library.isPinned === "function"
            && page.library.isPinned("playlist", id) === true
        return pinned ? "unpin from quickplay" : "pin to quickplay"
    }

    function openSheet(): bool {
        const r = page.focusedRow
        if (r === null || r.kind !== "playlist")
            return false
        page.sheetRow = r
        sheet.title = r.title
        sheet.items = ["play", "play next", "add to queue", "shuffle", page.pinLabel(r.id)]
        sheet.open()
        return true
    }

    function runSheet(index: int) {
        const r = page.sheetRow
        if (r === null)
            return
        const action = sheet.items[index]
        if (action === "play")
            page.playRow(r)
        else
            page.sheetAction(action, "playlist", r.id)
    }

    function completeAll() {
        listView.settle()
        listView.completeScroll()
        countAnim.stop()
        page.shownCount = page.countLine
        countText.opacity = 1
        sheet.complete()
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        switch (name) {
        case "a":
            if (!repeat && page.focusedRow !== null)
                page.playRow(page.focusedRow)
            return true
        case "x":
            if (!repeat)
                page.openSheet()
            return true
        case "y":
            if (!repeat && page.focusedRow !== null && page.focusedRow.kind === "playlist")
                page.sheetAction("add to queue", "playlist", page.focusedRow.id)
            return true
        default:
            return page.baseAction(name, repeat)
        }
    }

    function moveRow(delta: int): bool {
        const before = listView.focusIndex
        listView.moveFocus(delta)
        page.focusIndex = listView.focusIndex
        return listView.focusIndex !== before
    }

    function baseAction(name: string, repeat: bool): bool {
        switch (name) {
        case "up":
            return page.moveRow(-1)
        case "down":
            return page.moveRow(1)
        case "lt":
            return page.pageFocus(-1)
        case "rt":
            return page.pageFocus(1)
        case "b":
            if (repeat || page.stack === null || page.stack.depth < 2)
                return false
            page.stack.pop()
            return true
        default:
            return false
        }
    }

    onPlaylistsModelChanged: page.reload()
    onQueueCountChanged: page.rebuild()
    onCountLineChanged: page.swapCount()

    onEntered: (first, kind) => {
        if (page.arriving)
            listView.feather(1)
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        page.rebuild()
        page.shownCount = page.countLine
        d.ready = true
    }

    QtObject {
        id: d

        property bool ready: false
    }

    Connections {
        target: page.playlistsModel
        ignoreUnknownSignals: true

        function onModelReset() {
            page.reload()
        }

        function onRowsInserted() {
            page.reload()
        }

        function onRowsRemoved() {
            page.reload()
        }

        function onDataChanged() {
            page.reload()
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    FocusList {
        id: listView

        signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(listView.contentY / listView.rowPitch))
        }

        function feather(dir: int): void {
            listView.forceLayout()
            listView.entrance(dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        function refresh(): void {
            listView.forceLayout()
            listView.entrance(Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false)
        }

        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - listView.y
        model: page.rows

        delegate: ListRow {
            id: row

            required property int index
            required property var modelData
            property real enter: page.arriving ? 0 : 1
            property real shift: page.arriving ? Motion.pivotShift : 0
            property int stagger: 0
            property int enterDuration: 0
            property var enterCurve: Motion.strip

            transform: Translate { x: Ui.px(row.shift) * (1 - row.enter) }
            opacity: row.enter
            width: listView.width
            bold: true
            ring: true
            ringGlyph: "play"
            primary: row.modelData.title
            secondary: row.modelData.subtitle
            focused: listView.focusIndex === row.index
            pressed: row.focused && page.pressed
            animated: page.live

            ListView.onPooled: {
                enterAnim.stop()
                row.enter = 1
            }

            Connections {
                target: listView

                function onEntrance(shift, stagger, cap, duration, stripCurve) {
                    enterAnim.stop()
                    row.shift = shift
                    row.stagger = Math.min(cap, Math.max(0, row.index - listView.firstVisible())) * stagger
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
        }
    }

    FocusBar {
        id: focusBar

        visible: page.rows.length > 0
        rowY: listView.y + listView.focusY
        rowHeight: listView.rowPitch
        opacity: page.focusOpacity
        animated: page.live
    }

    CroppedHeader {
        id: header

        text: "playlists"
        animated: page.live
    }

    Text {
        id: countText

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(Ui.queueCountBaseline) - countText.baselineOffset
        text: page.shownCount
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }

    SequentialAnimation {
        id: countAnim

        NumberAnimation {
            target: countText
            property: "opacity"
            to: Motion.valueDip
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
        ScriptAction {
            script: page.shownCount = page.countLine
        }
        NumberAnimation {
            target: countText
            property: "opacity"
            to: 1
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
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
}
