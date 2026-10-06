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

    readonly property bool live: page.animated && !page.screenOff
    readonly property int queueCount: page.player !== null && page.player.queue !== undefined && page.player.queue !== null
        ? page.player.queue.count : 0
    readonly property int savedCount: page.modelCount(page.playlistsModel)
    readonly property string countLine: page.savedCount === 1 ? "1 playlist" : page.savedCount + " playlists"
    readonly property var focusedRow: page.rows[page.focusIndex] === undefined ? null : page.rows[page.focusIndex]
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

    function playlistIds(id: int): var {
        if (page.library === null || typeof page.library.playlistTracks !== "function")
            return []
        const ids = []
        for (const t of page.library.playlistTracks(id))
            ids.push(typeof t === "object" && t !== null && t.id !== undefined ? Number(t.id) : Number(t))
        return ids
    }

    function playRow(r: var) {
        if (r.kind === "nowplaying") {
            page.nowPlaying()
            return
        }
        const ids = page.playlistIds(r.id)
        if (page.player !== null && typeof page.player.playTracks === "function" && ids.length > 0) {
            page.player.playTracks(ids, 0)
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
        sheet.items = ["play", "add to queue", page.pinLabel(r.id)]
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
        listView.completeScroll()
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

    onPlaylistsModelChanged: page.rebuild()
    onQueueCountChanged: page.rebuild()

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: page.rebuild()

    Connections {
        target: page.playlistsModel
        ignoreUnknownSignals: true

        function onModelReset() {
            page.rebuild()
        }

        function onRowsInserted() {
            page.rebuild()
        }

        function onRowsRemoved() {
            page.rebuild()
        }

        function onDataChanged() {
            page.rebuild()
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    FocusList {
        id: listView

        x: 0
        y: Ui.px(Ui.contentTop)
        width: page.width
        height: page.height - listView.y
        model: page.rows

        delegate: ListRow {
            id: row

            required property int index
            required property var modelData

            width: listView.width
            bold: true
            ring: true
            ringGlyph: "play"
            primary: row.modelData.title
            secondary: row.modelData.subtitle
            focused: listView.focusIndex === row.index
            pressed: row.focused && page.pressed
            animated: page.live
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
        text: page.countLine
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
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
