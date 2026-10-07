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

    property var playlistRows: []
    property var focusedRow: null
    property var sheetRow: null

    readonly property bool live: hub.animated && !hub.screenOff
    readonly property bool arriving: !hub.arrived && hub.live && hub.stackAnimates
    readonly property var pivots: ["artists", "albums", "songs", "genres", "playlists"]
    readonly property bool scanning: hub.library !== null && hub.library.scanning === true
    readonly property int queueCount: hub.player !== null && hub.player.queue !== undefined && hub.player.queue !== null
        ? hub.player.queue.count : 0
    readonly property bool alphabetic: hub.pivotIndex !== 4
    readonly property bool grid: hub.pivotIndex === 1
    readonly property real cellPitch: Ui.tileS + Ui.gutter
    readonly property real gridRowPitch: Ui.tileCellH + Ui.gutter
    readonly property int cols: Ui.gridCols
    readonly property var panes: [artistsPane, albumsPane, songsPane, genresPane, playlistsPane]
    readonly property FocusList activeList: hub.panes[hub.pivotIndex]
    readonly property int rowIndex: hub.activeList.focusIndex
    readonly property var focusedCell: hub.grid && hub.focusedRow !== null && hub.focusedRow[hub.gridCol] !== undefined
        ? hub.focusedRow[hub.gridCol] : null
    readonly property bool switching: pivotAnim.running

    property alias header: header
    property alias strip: strip
    property alias focusBar: focusBar
    property alias sheet: sheet
    property alias jump: jump
    property alias jumpGrid: jumpGrid
    property alias body: body
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

    function str(value: var): string {
        return value === undefined || value === null ? "" : String(value)
    }

    function letterOf(title: string): string {
        const key = title.trim().toLowerCase()
        if (key.length === 0)
            return "#"
        const c = key.charAt(0)
        return c >= "a" && c <= "z" ? c : "#"
    }

    function entryOf(e: var, kind: string): var {
        if (e === undefined || e === null)
            return null
        const title = e.title !== undefined && e.title !== null ? String(e.title) : hub.str(e.name)
        const letter = e.letter !== undefined && e.letter !== null && String(e.letter) !== "" ? String(e.letter)
            : e.section !== undefined && e.section !== null && String(e.section) !== "" ? String(e.section).toLowerCase()
            : hub.letterOf(title)
        const row = e.sourceRow !== undefined && e.sourceRow !== null ? Number(e.sourceRow)
            : e.row !== undefined && e.row !== null ? Number(e.row) : -1
        return {
            kind: e.kind !== undefined && e.kind !== null && String(e.kind) !== "" ? String(e.kind) : kind,
            id: e.id === undefined || e.id === null ? -1 : Number(e.id),
            title: title,
            subtitle: hub.str(e.subtitle),
            artPath: hub.str(e.artPath),
            letter: letter,
            row: row,
            number: e.number !== undefined && e.number !== null && String(e.number) !== "" ? String(e.number) : String(row + 1)
        }
    }

    function countLabel(n: int): string {
        return n + (n === 1 ? " song" : " songs")
    }

    function buildPlaylists(): var {
        const rows = [{ kind: "nowplaying", id: -1, title: "now playing", subtitle: hub.countLabel(hub.queueCount), artPath: "", letter: "", row: -1, number: "" }]
        const n = hub.modelCount(hub.playlistsModel)
        for (let i = 0; i < n; i++) {
            const e = hub.entryOf(hub.playlistsModel.get(i), "playlist")
            if (e !== null)
                rows.push(e)
        }
        return rows
    }

    function syncPlaylists(): void {
        const rows = hub.buildPlaylists()
        hub.playlistRows = rows
        playlistsList.clear()
        for (let i = 0; i < rows.length; i++) {
            const r = rows[i]
            playlistsList.append({ kind: r.kind, letter: "", id: r.id, title: r.title, subtitle: r.subtitle, artPath: r.artPath, number: "", sourceRow: i })
        }
    }

    function sourceAt(pivot: int): var {
        switch (pivot) {
        case 0:
            return hub.artistsModel
        case 1:
            return hub.albumsModel
        case 2:
            return hub.songsModel
        case 3:
            return hub.genresModel
        default:
            return playlistsList
        }
    }

    function contentAt(pivot: int): Item {
        return hub.panes[pivot]
    }

    function countAt(pivot: int): int {
        if (pivot === 4)
            return hub.playlistRows.length
        if (!hub.contentAt(pivot).bound)
            return 0
        return hub.modelCount(hub.sourceAt(pivot))
    }

    function cellsOf(value: var, index: int): var {
        if (value !== null && value !== undefined && typeof value === "object" && typeof value.length === "number")
            return value
        const m = hub.albumsModel
        if (m === null || m === undefined || typeof m.cells !== "function")
            return []
        return m.cells(index)
    }

    function gridCells(index: int): var {
        const out = []
        const list = hub.cellsOf(null, index)
        for (let i = 0; i < list.length; i++) {
            const e = hub.entryOf(list[i], "album")
            if (e !== null)
                out.push(e)
        }
        return out
    }

    function rowEntry(pivot: int, index: int): var {
        if (index < 0 || index >= hub.countAt(pivot))
            return null
        if (pivot === 4)
            return hub.playlistRows[index]
        if (pivot === 1)
            return hub.gridCells(index)
        const m = hub.sourceAt(pivot)
        if (m === null || m === undefined || typeof m.get !== "function")
            return null
        return hub.entryOf(m.get(index), "")
    }

    function entryAt(pivot: int, index: int): var {
        return hub.rowEntry(pivot, index)
    }

    function focusableAt(pivot: int, index: int): bool {
        if (index < 0 || index >= hub.countAt(pivot))
            return false
        if (pivot === 1 || pivot === 4)
            return true
        const m = hub.sourceAt(pivot)
        if (m === null || m === undefined)
            return false
        if (typeof m.isFocusable === "function")
            return m.isFocusable(index) === true
        return true
    }

    function nearestFocusable(pivot: int, from: int, dir: int): int {
        const m = hub.sourceAt(pivot)
        if (pivot !== 1 && pivot !== 4 && hub.contentAt(pivot).bound && m !== null && m !== undefined
                && typeof m.nearestFocusable === "function")
            return m.nearestFocusable(from, dir)
        const n = hub.countAt(pivot)
        const step = dir >= 0 ? 1 : -1
        for (let i = from; i >= 0 && i < n; i += step) {
            if (hub.focusableAt(pivot, i))
                return i
        }
        for (let i = from - step; i >= 0 && i < n; i -= step) {
            if (hub.focusableAt(pivot, i))
                return i
        }
        return -1
    }

    function bindPane(pivot: int): void {
        const pane = hub.contentAt(pivot)
        if (pane.bound)
            return
        const m = hub.sourceAt(pivot)
        if (m !== null && m !== undefined && m.active !== undefined)
            m.active = true
        pane.bound = true
        hub.fixFocus(pivot)
    }

    function rebind(pivot: int): void {
        const pane = hub.contentAt(pivot)
        if (!pane.bound)
            return
        const m = hub.sourceAt(pivot)
        if (m !== null && m !== undefined && m.active !== undefined)
            m.active = true
        hub.fixFocus(pivot)
    }

    function fixFocus(pivot: int): void {
        d.fixing = true
        hub.placeFocus(pivot)
        d.fixing = false
    }

    function placeFocus(pivot: int): void {
        const pane = hub.contentAt(pivot)
        if (pane.bound)
            pane.forceLayout()
        const n = hub.countAt(pivot)
        if (n === 0) {
            pane.focusIndex = 0
            hub.syncFocus()
            return
        }
        const grid = hub.albumsModel
        if (pivot === 1 && d.focusAlbum >= 0 && grid !== null && grid !== undefined && typeof grid.cellOfAlbum === "function") {
            const pos = grid.cellOfAlbum(d.focusAlbum)
            if (pos !== undefined && pos !== null && Number(pos.row) >= 0) {
                pane.snapTo(Number(pos.row))
                if (hub.pivotIndex === 1)
                    hub.gridCol = Number(pos.col)
                hub.syncFocus()
                return
            }
        }
        const target = hub.nearestFocusable(pivot, Math.min(pane.focusIndex, n - 1), 1)
        pane.snapTo(target < 0 ? 0 : target)
        hub.syncFocus()
    }

    function clampCol(): void {
        if (!hub.grid || hub.focusedRow === null)
            return
        hub.gridCol = Math.max(0, Math.min(hub.focusedRow.length - 1, hub.gridCol))
    }

    function syncFocus(): void {
        hub.focusIndex = hub.activeList.focusIndex
        hub.focusedRow = hub.rowEntry(hub.pivotIndex, hub.activeList.focusIndex)
        hub.clampCol()
    }

    function moveRow(delta: int): bool {
        const list = hub.activeList
        const target = hub.nearestFocusable(hub.pivotIndex, list.focusIndex + delta, delta)
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
        const fixed = hub.nearestFocusable(hub.pivotIndex, list.focusIndex, dir)
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

    function showJump(): void {
        if (!hub.alphabetic)
            return
        const letter = hub.focusLetter()
        if (letter !== "")
            jump.show(letter)
    }

    function focusedEntry(): var {
        return hub.grid ? hub.focusedCell : hub.focusedRow
    }

    function availableLetters(): var {
        const out = []
        const m = hub.sourceAt(hub.pivotIndex)
        if (!hub.alphabetic || m === null || m === undefined || typeof m.letters !== "function")
            return out
        const list = m.letters()
        for (let i = 0; i < list.length; i++)
            out.push(String(list[i]))
        return out
    }

    function openJumpGrid(): bool {
        if (!hub.alphabetic)
            return false
        hub.jumpRequested()
        jumpGrid.open(hub.availableLetters(), hub.focusLetter())
        return true
    }

    function jumpTo(letter: string): void {
        const list = hub.activeList
        const m = hub.sourceAt(hub.pivotIndex)
        if (m === null || m === undefined)
            return
        let top = -1
        let target = -1
        let col = 0
        if (hub.grid) {
            if (typeof m.cellOfLetter !== "function")
                return
            const pos = m.cellOfLetter(letter)
            top = pos === undefined || pos === null ? -1 : Number(pos.row)
            col = pos === undefined || pos === null ? 0 : Number(pos.col)
            target = top
        } else {
            if (typeof m.rowOfLetter !== "function" || typeof m.firstRowOfLetter !== "function")
                return
            top = m.rowOfLetter(letter)
            target = m.firstRowOfLetter(letter)
        }
        if (top < 0 || target < 0)
            return
        list.completeScroll()
        list.focusIndex = target
        list.contentY = Math.max(0, Math.min(list.maxContentY, top * list.rowPitch))
        if (hub.grid)
            hub.gridCol = col
        hub.syncFocus()
        jump.show(letter)
    }

    function canPlaySource(): bool {
        return hub.player !== null && typeof hub.player.playSource === "function"
    }

    function playSong(row: var): void {
        if (hub.canPlaySource()) {
            hub.player.playSource("all", 0, "play", row.id)
            return
        }
        hub.play("song", row.id)
    }

    function playCollection(kind: string, id: int): void {
        if (hub.canPlaySource()) {
            hub.player.playSource(kind, id, "play", -1)
            return
        }
        hub.play(kind, id)
    }

    function activate(): void {
        const t = hub.focusedEntry()
        if (t === null || t === undefined)
            return
        switch (t.kind) {
        case "letter":
            hub.openJumpGrid()
            break
        case "song":
            hub.playSong(t)
            break
        case "genre":
        case "playlist":
            hub.playCollection(t.kind, t.id)
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
        sheet.items = hub.sheetItemsFor(t.kind, t.id)
        sheet.open()
        return true
    }

    function pinLabel(kind: string, id: int): string {
        const pinned = hub.library !== null && typeof hub.library.isPinned === "function"
            && hub.library.isPinned(kind, id) === true
        return pinned ? "unpin from quickplay" : "pin to quickplay"
    }

    function sheetItemsFor(kind: string, id: int): var {
        if (kind === "genre")
            return ["play", "add to queue"]
        if (kind === "song")
            return ["play", "play next", "add to queue", hub.pinLabel(kind, id)]
        return ["play", "play next", "add to queue", "shuffle", hub.pinLabel(kind, id)]
    }

    function runSheet(index: int): void {
        const t = hub.sheetRow
        if (t === null)
            return
        const action = sheet.items[index]
        if (action === "play" && t.kind === "song")
            hub.playSong(t)
        else if (action === "play" && (t.kind === "genre" || t.kind === "playlist"))
            hub.playCollection(t.kind, t.id)
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

    function landPivot(): void {
        pivotAnim.stop()
        for (const pane of hub.panes) {
            pane.x = 0
            pane.opacity = 1
            pane.land()
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
        hub.bindPane(target)
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

    function runRefresh(): void {
        if (!hub.live)
            return
        hub.contentAt(hub.shownPivot).refresh()
    }

    function scheduleRefresh(): void {
        Qt.callLater(hub.runRefresh)
    }

    function scanTick(): void {
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

    function afterRebuild(): void {
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

    function rowsUpdated(pivot: int): void {
        if (!d.ready)
            return
        hub.fixFocus(pivot)
        hub.afterRebuild()
    }

    function completeAll(): void {
        hub.landPivot()
        for (const pane of hub.panes)
            pane.completeScroll()
        jump.complete()
        jumpGrid.complete()
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
        if (jumpGrid.shown)
            return jumpGrid.handleAction(name, repeat)
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
            return hub.openJumpGrid()
        default:
            return false
        }
    }

    onPivotIndexChanged: {
        if (!d.ready)
            return
        if (!d.internal) {
            hub.landPivot()
            hub.bindPane(hub.pivotIndex)
        }
        hub.syncFocus()
    }

    onFocusedCellChanged: {
        if (!d.fixing && hub.focusedCell !== null && hub.focusedCell.kind === "album")
            d.focusAlbum = hub.focusedCell.id
    }

    onEntered: (first, kind) => {
        if (hub.arriving)
            hub.contentAt(hub.shownPivot).feather(1)
    }

    onLiveChanged: {
        if (!hub.live)
            hub.completeAll()
    }

    onArtistsModelChanged: {
        if (d.ready)
            hub.rebind(0)
    }
    onAlbumsModelChanged: {
        if (d.ready)
            hub.rebind(1)
    }
    onSongsModelChanged: {
        if (d.ready)
            hub.rebind(2)
    }
    onGenresModelChanged: {
        if (d.ready)
            hub.rebind(3)
    }
    onPlaylistsModelChanged: {
        if (!d.ready)
            return
        hub.syncPlaylists()
        hub.rowsUpdated(4)
    }
    onQueueCountChanged: {
        if (!d.ready || hub.playlistRows.length === 0)
            return
        const rows = hub.playlistRows.slice()
        rows[0] = Object.assign({}, rows[0], { subtitle: hub.countLabel(hub.queueCount) })
        hub.playlistRows = rows
        playlistsList.setProperty(0, "subtitle", rows[0].subtitle)
        if (hub.pivotIndex === 4)
            hub.syncFocus()
    }
    onColsChanged: {
        if (d.ready)
            hub.fixFocus(1)
    }

    Component.onCompleted: {
        hub.syncPlaylists()
        hub.fixFocus(4)
        hub.bindPane(hub.pivotIndex)
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
        property bool fixing: false
        property int focusAlbum: -1
    }

    ListModel {
        id: playlistsList
    }

    Binding {
        target: hub.albumsModel
        property: "columns"
        value: hub.cols
        when: hub.albumsModel !== null && hub.albumsModel !== undefined && hub.albumsModel.columns !== undefined
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
                hub.rowsUpdated(watch.pivot)
            }

            function onRowsInserted() {
                hub.rowsUpdated(watch.pivot)
            }

            function onRowsRemoved() {
                hub.rowsUpdated(watch.pivot)
            }

            function onRowsMoved() {
                hub.rowsUpdated(watch.pivot)
            }

            function onDataChanged() {
                if (d.ready && watch.pivot === hub.pivotIndex)
                    hub.syncFocus()
            }
        }
    }

    ModelWatch {
        model: artistsPane.bound ? hub.artistsModel : null
        pivot: 0
    }

    ModelWatch {
        model: albumsPane.bound ? hub.albumsModel : null
        pivot: 1
    }

    ModelWatch {
        model: songsPane.bound ? hub.songsModel : null
        pivot: 2
    }

    ModelWatch {
        model: genresPane.bound ? hub.genresModel : null
        pivot: 3
    }

    Connections {
        target: hub.playlistsModel
        ignoreUnknownSignals: true

        function onModelReset() {
            hub.syncPlaylists()
            hub.rowsUpdated(4)
        }

        function onRowsInserted() {
            hub.syncPlaylists()
            hub.rowsUpdated(4)
        }

        function onRowsRemoved() {
            hub.syncPlaylists()
            hub.rowsUpdated(4)
        }

        function onDataChanged() {
            hub.syncPlaylists()
            hub.rowsUpdated(4)
        }
    }

    component Pane: FocusList {
        id: pane

        property int pivot: 0
        property bool bound: false
        property var source: null
        property var entranceArgs: null
        readonly property bool entering: entranceClock.running

        signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(pane.contentY / pane.rowPitch))
        }

        function startEntrance(shift: real, stagger: int, cap: int, duration: int, stripCurve: bool): void {
            pane.forceLayout()
            pane.entranceArgs = { shift: shift, stagger: stagger, cap: cap, duration: duration, stripCurve: stripCurve }
            entranceClock.interval = Math.max(1, duration + cap * stagger)
            entranceClock.restart()
            pane.entrance(shift, stagger, cap, duration, stripCurve)
        }

        function feather(dir: int): void {
            pane.startEntrance(dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        function refresh(): void {
            pane.startEntrance(Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false)
        }

        function land(): void {
            entranceClock.stop()
            pane.settle()
        }

        x: 0
        y: Ui.px(Ui.contentTop)
        width: hub.width
        height: hub.height - pane.y
        visible: hub.shownPivot === pane.pivot
        model: pane.bound ? pane.source : null
        onFocusIndexChanged: {
            if (d.ready && hub.activeList === pane)
                hub.syncFocus()
        }

        Timer {
            id: entranceClock

            repeat: false
        }
    }

    component Entering: Item {
        id: entering

        required property int index
        readonly property var pane: entering.ListView.view
        property real enter: hub.arriving ? 0 : 1
        property real shift: hub.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        function runEntrance(args: var): void {
            enterAnim.stop()
            entering.shift = args.shift
            entering.stagger = Math.min(args.cap, Math.max(0, entering.index - entering.pane.firstVisible())) * args.stagger
            entering.enterDuration = args.duration
            entering.enterCurve = args.stripCurve ? Motion.strip : Motion.decel
            entering.enter = 0
            enterAnim.start()
        }

        function landEntrance(): void {
            enterAnim.stop()
            entering.enter = 1
        }

        width: entering.pane === null ? 0 : entering.pane.width
        height: entering.pane === null ? 0 : entering.pane.rowPitch
        transform: Translate { x: Ui.px(entering.shift) * (1 - entering.enter) }
        opacity: entering.enter

        ListView.onPooled: entering.landEntrance()
        ListView.onReused: {
            if (entering.pane !== null && entering.pane.entering && entering.pane.entranceArgs !== null)
                entering.runEntrance(entering.pane.entranceArgs)
        }

        Connections {
            target: entering.pane

            function onEntrance(shift, stagger, cap, duration, stripCurve) {
                entering.runEntrance({ shift: shift, stagger: stagger, cap: cap, duration: duration, stripCurve: stripCurve })
            }

            function onSettle() {
                entering.landEntrance()
            }
        }

        SequentialAnimation {
            id: enterAnim

            PauseAnimation {
                duration: entering.stagger
            }

            NumberAnimation {
                target: entering
                property: "enter"
                from: 0
                to: 1
                duration: entering.enterDuration
                easing.type: Motion.bezier
                easing.bezierCurve: entering.enterCurve
            }
        }
    }

    component HubRow: Entering {
        id: row

        required property string kind
        required property string letter
        required property string title
        required property string subtitle
        required property string number
        readonly property bool divider: row.kind === "divider"
        readonly property bool isFocused: !row.divider && row.pane !== null && hub.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property Item rowItem: listRow
        readonly property Item dividerItem: dividerText

        ListRow {
            id: listRow

            visible: !row.divider
            width: row.width
            number: row.kind === "song" ? row.number : ""
            bold: row.kind === "genre" || row.kind === "playlist" || row.kind === "nowplaying"
            countLine: row.kind === "artist"
            ring: row.kind === "playlist" || row.kind === "nowplaying"
            ringGlyph: "play"
            primary: row.divider ? "" : row.title
            secondary: row.divider ? "" : row.subtitle
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
            text: row.divider ? row.letter : ""
            color: Ui.white(Ui.letterGlyph.alpha)
            font: Ui.font(Ui.letterGlyph)
        }
    }

    component GridRow: Entering {
        id: row

        required property var cells
        readonly property var cellList: hub.cellsOf(row.cells, row.index)
        readonly property bool isFocusedRow: row.pane !== null && hub.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property int cellCount: cellRepeater.count

        function cellAt(col: int): Item {
            return cellRepeater.itemAt(col)
        }

        Repeater {
            id: cellRepeater

            model: row.cellList

            delegate: Item {
                id: cell

                required property int index
                required property var modelData
                readonly property bool letter: cell.modelData.kind === "letter"
                readonly property bool isFocused: row.isFocusedRow && hub.gridCol === cell.index
                readonly property Item tileItem: cell.letter ? null : cellLoader.item
                readonly property Item letterItem: cell.letter ? cellLoader.item : null

                x: Ui.px(Ui.hubEdge + cell.index * hub.cellPitch)
                y: 0
                width: Ui.px(Ui.tileS)
                height: Ui.px(cell.letter ? Ui.tileS : Ui.tileCellH)

                Loader {
                    id: cellLoader

                    sourceComponent: cell.letter ? letterComponent : tileComponent
                }

                Component {
                    id: tileComponent

                    Tile {
                        source: cell.modelData.artPath !== undefined && cell.modelData.artPath !== "" ? "file://" + cell.modelData.artPath : ""
                        title: cell.modelData.title
                        subtitle: cell.modelData.subtitle
                        focused: cell.isFocused
                        pressed: cell.isFocused && hub.pressed
                        animated: hub.live
                    }
                }

                Component {
                    id: letterComponent

                    LetterTile {
                        letter: cell.modelData.letter
                        focused: cell.isFocused
                        pressed: cell.isFocused && hub.pressed
                        animated: hub.live
                    }
                }
            }
        }
    }

    Item {
        id: body

        anchors.fill: parent
        opacity: 1 - (1 - Motion.swivelDim) * jumpGrid.visualOpacity

        PageSurface {
            anchors.fill: parent
        }

        Pane {
            id: artistsPane

            pivot: 0
            source: hub.artistsModel
            delegate: HubRow {}
        }

        Pane {
            id: albumsPane

            pivot: 1
            rowPitch: Ui.px(hub.gridRowPitch)
            source: hub.albumsModel
            delegate: GridRow {}
        }

        Pane {
            id: songsPane

            pivot: 2
            source: hub.songsModel
            delegate: HubRow {}
        }

        Pane {
            id: genresPane

            pivot: 3
            source: hub.genresModel
            delegate: HubRow {}
        }

        Pane {
            id: playlistsPane

            pivot: 4
            bound: true
            source: playlistsList
            delegate: HubRow {}
        }

        FocusBar {
            id: focusBar

            visible: !hub.grid && hub.shownPivot === hub.pivotIndex && hub.focusedRow !== null
            x: Ui.px(Ui.hubEdge - Ui.focusBarOffset) + hub.activeList.x
            rowY: hub.activeList.y + hub.activeList.focusY
            rowHeight: hub.activeList.rowPitch
            opacity: hub.activeList.opacity * hub.focusOpacity
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
    }

    JumpGrid {
        id: jumpGrid

        anchors.fill: parent
        screenOff: !hub.live
        pressed: hub.pressed
        onJumpTo: (letter) => hub.jumpTo(letter)
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
