pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: hub

    property var artistsModel: null
    property var albumsModel: null
    property var songsModel: null
    property var genresModel: null
    property var playlistsModel: null
    property QtObject player: null
    property QtObject library: null
    property bool animated: true
    property bool screenOff: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property int gridCol: 0

    property var artistRows: []
    property var albumCells: []
    property var songRows: []
    property var songIds: []
    property var genreRows: []
    property var playlistRows: []
    property var sheetRow: null

    readonly property bool live: hub.animated && !hub.screenOff
    readonly property var pivots: ["artists", "albums", "songs", "genres", "playlists"]
    readonly property var sheetItems: ["play", "add to queue", "pin to quickplay"]
    readonly property bool scanning: hub.library !== null && hub.library.scanning === true
    readonly property int queueCount: hub.player !== null && hub.player.queue !== undefined && hub.player.queue !== null
        ? hub.player.queue.count : 0
    readonly property bool alphabetic: hub.pivotIndex !== 4
    readonly property bool grid: hub.pivotIndex === 1
    readonly property real cellPitch: Ui.tileS + Ui.gutter
    readonly property int cols: Ui.gridCols
    readonly property var albumRows: hub.chunk(hub.albumCells, hub.cols)
    readonly property var panes: [artistsPane, albumsPane, songsPane, genresPane, playlistsPane]
    readonly property FocusList activeList: hub.panes[hub.pivotIndex]
    readonly property var currentRows: hub.rowsFor(hub.pivotIndex)
    readonly property int rowIndex: hub.activeList.focusIndex
    readonly property var focusedRow: hub.currentRows[hub.rowIndex] === undefined ? null : hub.currentRows[hub.rowIndex]
    readonly property var focusedCell: hub.grid && hub.focusedRow !== null && hub.focusedRow[hub.gridCol] !== undefined
        ? hub.focusedRow[hub.gridCol] : null
    readonly property bool switching: pivotAnim.running

    property alias header: header
    property alias strip: strip
    property alias focusBar: focusBar
    property alias sheet: sheet
    property alias jump: jump
    property alias artistsPane: artistsPane
    property alias albumsPane: albumsPane
    property alias songsPane: songsPane
    property alias genresPane: genresPane
    property alias playlistsPane: playlistsPane

    signal open(string kind, int id)
    signal nowPlaying()
    signal jumpRequested()
    signal play(string kind, int id)
    signal sheetAction(string action, string kind, int id)

    list: hub.activeList

    function modelCount(model: var): int {
        if (model === null || model === undefined)
            return 0
        if (typeof model.count === "number")
            return model.count
        if (typeof model.rowCount === "function")
            return model.rowCount()
        return 0
    }

    function letterOf(title: string): string {
        const key = title.trim().toLowerCase()
        if (key.length === 0)
            return "#"
        const c = key.charAt(0)
        return c >= "a" && c <= "z" ? c : "#"
    }

    function entryOf(model: var, row: int, kind: string): var {
        const e = model.get(row)
        const title = e.title !== undefined && e.title !== null ? String(e.title)
            : e.name !== undefined && e.name !== null ? String(e.name) : ""
        const section = e.section !== undefined && e.section !== null && String(e.section) !== ""
            ? String(e.section).toLowerCase() : hub.letterOf(title)
        return {
            kind: kind,
            id: e.id === undefined ? -1 : Number(e.id),
            title: title,
            subtitle: e.subtitle === undefined || e.subtitle === null ? "" : String(e.subtitle),
            artPath: e.artPath === undefined || e.artPath === null ? "" : String(e.artPath),
            letter: section,
            row: row,
            number: String(row + 1)
        }
    }

    function buildList(model: var, kind: string): var {
        const rows = []
        let last = ""
        const n = hub.modelCount(model)
        for (let i = 0; i < n; i++) {
            const e = hub.entryOf(model, i, kind)
            if (e.letter !== last) {
                rows.push({ kind: "divider", letter: e.letter, id: -1, title: e.letter, subtitle: "", artPath: "" })
                last = e.letter
            }
            rows.push(e)
        }
        return rows
    }

    function buildCells(model: var): var {
        const cells = []
        let last = ""
        const n = hub.modelCount(model)
        for (let i = 0; i < n; i++) {
            const e = hub.entryOf(model, i, "album")
            if (e.letter !== last) {
                cells.push({ kind: "letter", letter: e.letter, id: -1, title: e.letter, subtitle: "", artPath: "" })
                last = e.letter
            }
            cells.push(e)
        }
        return cells
    }

    function chunk(cells: var, cols: int): var {
        const rows = []
        const n = Math.max(1, cols)
        for (let i = 0; i < cells.length; i += n)
            rows.push(cells.slice(i, i + n))
        return rows
    }

    function countLabel(n: int): string {
        return n + (n === 1 ? " song" : " songs")
    }

    function buildPlaylists(): var {
        const rows = [{ kind: "nowplaying", id: -1, title: "now playing", subtitle: hub.countLabel(hub.queueCount), artPath: "", letter: "" }]
        const n = hub.modelCount(hub.playlistsModel)
        for (let i = 0; i < n; i++)
            rows.push(hub.entryOf(hub.playlistsModel, i, "playlist"))
        return rows
    }

    function rowsFor(pivot: int): var {
        switch (pivot) {
        case 0:
            return hub.artistRows
        case 1:
            return hub.albumRows
        case 2:
            return hub.songRows
        case 3:
            return hub.genreRows
        default:
            return hub.playlistRows
        }
    }

    function contentAt(pivot: int): Item {
        return hub.panes[pivot]
    }

    function rebuild(pivot: int) {
        switch (pivot) {
        case 0:
            hub.artistRows = hub.buildList(hub.artistsModel, "artist")
            break
        case 1:
            hub.albumCells = hub.buildCells(hub.albumsModel)
            break
        case 2: {
            const rows = hub.buildList(hub.songsModel, "song")
            const ids = []
            for (const r of rows) {
                if (r.kind === "song")
                    ids.push(r.id)
            }
            hub.songRows = rows
            hub.songIds = ids
            break
        }
        case 3:
            hub.genreRows = hub.buildList(hub.genresModel, "genre")
            break
        default:
            hub.playlistRows = hub.buildPlaylists()
            break
        }
        hub.fixFocus(pivot)
        hub.afterRebuild()
    }

    function rebuildAll() {
        for (let p = 0; p < hub.pivots.length; p++)
            hub.rebuild(p)
    }

    function isFocusable(rows: var, index: int): bool {
        const r = rows[index]
        return r !== undefined && r !== null && r.kind !== "divider"
    }

    function nearestFocusable(rows: var, from: int, dir: int): int {
        const step = dir >= 0 ? 1 : -1
        for (let i = from; i >= 0 && i < rows.length; i += step) {
            if (hub.isFocusable(rows, i))
                return i
        }
        for (let i = from - step; i >= 0 && i < rows.length; i -= step) {
            if (hub.isFocusable(rows, i))
                return i
        }
        return -1
    }

    function fixFocus(pivot: int) {
        const pane = hub.contentAt(pivot)
        pane.forceLayout()
        const rows = hub.rowsFor(pivot)
        if (rows.length === 0) {
            pane.focusIndex = 0
            return
        }
        const target = hub.nearestFocusable(rows, Math.min(pane.focusIndex, rows.length - 1), 1)
        pane.snapTo(target < 0 ? 0 : target)
        hub.syncFocus()
    }

    function clampCol() {
        if (!hub.grid || hub.focusedRow === null)
            return
        hub.gridCol = Math.max(0, Math.min(hub.focusedRow.length - 1, hub.gridCol))
    }

    function syncFocus() {
        hub.focusIndex = hub.activeList.focusIndex
        hub.clampCol()
    }

    function moveRow(delta: int): bool {
        const rows = hub.currentRows
        const list = hub.activeList
        const target = hub.nearestFocusable(rows, list.focusIndex + delta, delta)
        if (target < 0 || target === list.focusIndex)
            return false
        if ((delta > 0 && target < list.focusIndex) || (delta < 0 && target > list.focusIndex))
            return false
        list.setFocus(target, Motion.scroll)
        hub.syncFocus()
        return true
    }

    function pageRow(dir: int): bool {
        const list = hub.activeList
        if (!list.pageFocus(dir))
            return false
        const rows = hub.currentRows
        const fixed = hub.nearestFocusable(rows, list.focusIndex, dir)
        if (fixed >= 0 && fixed !== list.focusIndex) {
            list.focusIndex = fixed
            list.ensureVisible(Motion.page)
        }
        hub.syncFocus()
        hub.showJump()
        return true
    }

    function moveCol(delta: int): bool {
        if (!hub.grid || hub.focusedRow === null)
            return false
        const target = Math.max(0, Math.min(hub.focusedRow.length - 1, hub.gridCol + delta))
        if (target === hub.gridCol)
            return false
        hub.gridCol = target
        return true
    }

    function focusLetter(): string {
        if (hub.grid)
            return hub.focusedCell === null ? "" : hub.focusedCell.letter
        return hub.focusedRow === null || hub.focusedRow.letter === undefined ? "" : hub.focusedRow.letter
    }

    function showJump() {
        if (!hub.alphabetic)
            return
        const letter = hub.focusLetter()
        if (letter !== "")
            jump.show(letter)
    }

    function focusedEntry(): var {
        return hub.grid ? hub.focusedCell : hub.focusedRow
    }

    function playSong(row: var) {
        if (hub.player !== null && typeof hub.player.playTracks === "function") {
            hub.player.playTracks(hub.songIds, hub.songIds.indexOf(row.id))
            return
        }
        hub.play("song", row.id)
    }

    function activate() {
        const t = hub.focusedEntry()
        if (t === null || t === undefined)
            return
        switch (t.kind) {
        case "letter":
            hub.jumpRequested()
            break
        case "song":
            hub.playSong(t)
            break
        case "nowplaying":
            hub.nowPlaying()
            break
        case "divider":
            break
        default:
            hub.open(t.kind, t.id)
            break
        }
    }

    function openSheet(): bool {
        const t = hub.focusedEntry()
        if (t === null || t === undefined || t.kind === "letter" || t.kind === "divider" || t.kind === "nowplaying")
            return false
        hub.sheetRow = t
        sheet.title = t.title
        sheet.items = hub.sheetItems
        sheet.open()
        return true
    }

    function runSheet(index: int) {
        const t = hub.sheetRow
        if (t === null)
            return
        const action = hub.sheetItems[index]
        if (action === "play" && t.kind === "song")
            hub.playSong(t)
        else
            hub.sheetAction(action, t.kind, t.id)
    }

    function queueAction(): bool {
        const t = hub.focusedEntry()
        if (t === null || t === undefined || t.kind === "letter" || t.kind === "divider" || t.kind === "nowplaying")
            return false
        hub.sheetAction("add to queue", t.kind, t.id)
        return true
    }

    function landPivot() {
        pivotAnim.stop()
        for (const pane of hub.panes) {
            pane.x = 0
            pane.opacity = 1
            pane.settle()
        }
        hub.shownPivot = hub.pivotIndex
    }

    function switchPivot(delta: int): bool {
        const target = Math.max(0, Math.min(hub.pivots.length - 1, hub.pivotIndex + delta))
        if (target === hub.pivotIndex)
            return false
        if (pivotAnim.running)
            hub.landPivot()
        const dir = target > hub.pivotIndex ? 1 : -1
        d.internal = true
        hub.pivotIndex = target
        d.internal = false
        hub.syncFocus()
        if (!hub.live) {
            hub.shownPivot = target
            return true
        }
        pivotAnim.outgoing = hub.contentAt(hub.shownPivot)
        pivotAnim.incoming = hub.contentAt(target)
        pivotAnim.dir = dir
        pivotAnim.start()
        return true
    }

    function runRefresh() {
        if (!hub.live)
            return
        hub.contentAt(hub.shownPivot).refresh()
    }

    function scheduleRefresh() {
        Qt.callLater(hub.runRefresh)
    }

    function scanTick() {
        if (hub.scanning) {
            d.scanSeen = true
            d.resetDuringScan = false
            d.refreshArmed = false
            return
        }
        if (!d.scanSeen)
            return
        if (d.resetDuringScan) {
            d.scanSeen = false
            d.resetDuringScan = false
            hub.scheduleRefresh()
        } else {
            d.refreshArmed = true
        }
    }

    function afterRebuild() {
        if (!d.ready)
            return
        if (d.scanSeen && hub.scanning) {
            d.resetDuringScan = true
            return
        }
        if (d.refreshArmed) {
            d.refreshArmed = false
            d.scanSeen = false
            hub.scheduleRefresh()
        }
    }

    function completeAll() {
        hub.landPivot()
        for (const pane of hub.panes)
            pane.completeScroll()
        jump.complete()
        sheet.complete()
    }

    function back(): bool {
        if (hub.stack === null || hub.stack.depth < 2)
            return false
        hub.stack.pop()
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        switch (name) {
        case "up":
            return hub.moveRow(-1)
        case "down":
            return hub.moveRow(1)
        case "left":
            return hub.moveCol(-1)
        case "right":
            return hub.moveCol(1)
        case "lt":
            return hub.pageRow(-1)
        case "rt":
            return hub.pageRow(1)
        case "lb":
            if (!repeat)
                hub.switchPivot(-1)
            return true
        case "rb":
            if (!repeat)
                hub.switchPivot(1)
            return true
        case "a":
            if (!repeat)
                hub.activate()
            return true
        case "x":
            if (!repeat)
                hub.openSheet()
            return true
        case "y":
            if (!repeat)
                hub.queueAction()
            return true
        case "b":
            if (repeat)
                return false
            return hub.back()
        case "back":
            if (repeat || !hub.alphabetic)
                return false
            hub.jumpRequested()
            return true
        default:
            return false
        }
    }

    onPivotIndexChanged: {
        if (!d.internal)
            hub.landPivot()
        hub.syncFocus()
    }

    onLiveChanged: {
        if (!hub.live)
            hub.completeAll()
    }

    onArtistsModelChanged: {
        if (d.ready)
            hub.rebuild(0)
    }
    onAlbumsModelChanged: {
        if (d.ready)
            hub.rebuild(1)
    }
    onSongsModelChanged: {
        if (d.ready)
            hub.rebuild(2)
    }
    onGenresModelChanged: {
        if (d.ready)
            hub.rebuild(3)
    }
    onPlaylistsModelChanged: {
        if (d.ready)
            hub.rebuild(4)
    }
    onQueueCountChanged: {
        if (d.ready)
            hub.playlistRows = hub.buildPlaylists()
    }
    onColsChanged: {
        if (d.ready)
            hub.fixFocus(1)
    }

    Component.onCompleted: {
        hub.rebuildAll()
        hub.shownPivot = hub.pivotIndex
        hub.syncFocus()
        d.ready = true
    }

    QtObject {
        id: d

        property bool internal: false
        property bool ready: false
        property bool scanSeen: false
        property bool resetDuringScan: false
        property bool refreshArmed: false
    }

    Connections {
        target: hub.library
        ignoreUnknownSignals: true

        function onScanningChanged() {
            hub.scanTick()
        }
    }

    component ModelWatch: QtObject {
        id: watch

        property var model: null
        property int pivot: 0

        readonly property Connections link: Connections {
            target: watch.model
            ignoreUnknownSignals: true

            function onModelReset() {
                hub.rebuild(watch.pivot)
            }

            function onRowsInserted() {
                hub.rebuild(watch.pivot)
            }

            function onRowsRemoved() {
                hub.rebuild(watch.pivot)
            }

            function onDataChanged() {
                hub.rebuild(watch.pivot)
            }
        }
    }

    ModelWatch {
        model: hub.artistsModel
        pivot: 0
    }

    ModelWatch {
        model: hub.albumsModel
        pivot: 1
    }

    ModelWatch {
        model: hub.songsModel
        pivot: 2
    }

    ModelWatch {
        model: hub.genresModel
        pivot: 3
    }

    ModelWatch {
        model: hub.playlistsModel
        pivot: 4
    }

    component Pane: FocusList {
        id: pane

        property int pivot: 0

        signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(pane.contentY / pane.rowPitch))
        }

        function feather(dir: int) {
            pane.forceLayout()
            pane.entrance(dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        function refresh() {
            pane.forceLayout()
            pane.entrance(Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false)
        }

        x: 0
        y: Ui.px(Ui.contentTop)
        width: hub.width
        height: hub.height - pane.y
        visible: hub.shownPivot === pane.pivot
    }

    component HubRow: Item {
        id: row

        required property int index
        required property var modelData
        readonly property var pane: row.ListView.view
        readonly property bool divider: row.modelData.kind === "divider"
        readonly property bool isFocused: !row.divider && hub.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property Item rowItem: listRow
        readonly property Item dividerItem: dividerText
        property real enter: 1
        property real shift: 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        width: row.pane.width
        height: row.pane.rowPitch
        x: Ui.px(row.shift) * (1 - row.enter)
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

            visible: !row.divider
            width: row.width
            number: row.modelData.kind === "song" ? row.modelData.number : ""
            bold: row.modelData.kind === "genre" || row.modelData.kind === "playlist" || row.modelData.kind === "nowplaying"
            ring: row.modelData.kind === "playlist" || row.modelData.kind === "nowplaying"
            ringGlyph: "play"
            primary: row.modelData.title
            secondary: row.modelData.subtitle
            focused: row.isFocused
            pressed: row.isFocused && hub.pressed
            animated: hub.live
        }

        Text {
            id: dividerText

            visible: row.divider
            x: Ui.px(Ui.hubEdge)
            anchors.baseline: parent.bottom
            anchors.baselineOffset: -Ui.px(Ui.letterInsetBaseline)
            text: row.divider ? row.modelData.letter : ""
            color: Ui.white(Ui.letterGlyph.alpha)
            font: Ui.font(Ui.letterGlyph)
        }
    }

    component GridRow: Item {
        id: row

        required property int index
        required property var modelData
        readonly property var pane: row.ListView.view
        readonly property bool isFocusedRow: hub.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property int cellCount: cells.count
        property real enter: 1
        property real shift: 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        function cellAt(col: int): Item {
            return cells.itemAt(col)
        }

        width: row.pane.width
        height: row.pane.rowPitch
        x: Ui.px(row.shift) * (1 - row.enter)
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

        Repeater {
            id: cells

            model: row.modelData

            delegate: Item {
                id: cell

                required property int index
                required property var modelData
                readonly property bool letter: cell.modelData.kind === "letter"
                readonly property bool isFocused: row.isFocusedRow && hub.gridCol === cell.index
                readonly property Item tileItem: tile
                readonly property Item letterItem: letterTile

                x: Ui.px(Ui.hubEdge + cell.index * hub.cellPitch)
                y: 0
                width: Ui.px(Ui.tileS)
                height: Ui.px(Ui.tileS)

                Tile {
                    id: tile

                    visible: !cell.letter
                    source: cell.modelData.artPath !== "" ? "file://" + cell.modelData.artPath : ""
                    title: cell.modelData.title
                    subtitle: cell.modelData.subtitle
                    focused: cell.isFocused
                    pressed: cell.isFocused && hub.pressed
                    animated: hub.live
                }

                LetterTile {
                    id: letterTile

                    visible: cell.letter
                    letter: cell.letter ? cell.modelData.letter : ""
                    focused: cell.isFocused
                    pressed: cell.isFocused && hub.pressed
                    animated: hub.live
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    Pane {
        id: artistsPane

        pivot: 0
        model: hub.artistRows
        delegate: HubRow {}
    }

    Pane {
        id: albumsPane

        pivot: 1
        rowPitch: Ui.px(hub.cellPitch)
        model: hub.albumRows
        delegate: GridRow {}
    }

    Pane {
        id: songsPane

        pivot: 2
        model: hub.songRows
        delegate: HubRow {}
    }

    Pane {
        id: genresPane

        pivot: 3
        model: hub.genreRows
        delegate: HubRow {}
    }

    Pane {
        id: playlistsPane

        pivot: 4
        model: hub.playlistRows
        delegate: HubRow {}
    }

    FocusBar {
        id: focusBar

        visible: !hub.grid && hub.shownPivot === hub.pivotIndex && hub.focusedRow !== null
        x: Ui.px(Ui.hubEdge - Ui.focusBarOffset) + hub.activeList.x
        rowY: hub.activeList.y + hub.activeList.focusY
        rowHeight: hub.activeList.rowPitch
        opacity: hub.activeList.opacity
        animated: hub.live
    }

    CroppedHeader {
        id: header

        text: "music"
        shift: strip.headerShift
        animated: hub.live
    }

    PivotStrip {
        id: strip

        labels: hub.pivots
        activeIndex: hub.pivotIndex
        animated: hub.live
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: !hub.live
        onAccepted: (index) => {
            hub.runSheet(index)
            sheet.close()
        }
    }

    JumpLetter {
        id: jump

        anchors.fill: parent
        screenOff: !hub.live
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
                hub.shownPivot = hub.pivotIndex
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
