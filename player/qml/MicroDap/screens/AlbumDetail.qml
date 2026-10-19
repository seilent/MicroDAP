pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property int albumId: -1
    property string title: ""
    property string artist: ""
    property int year: 0
    property string artPath: ""
    property QtObject library: null
    property QtObject player: null
    property QtObject status: null
    property QtObject bluez: null
    property bool animated: true
    property bool screenOff: false
    property var rows: []
    property var trackIds: []
    property var sheetRow: null

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property int count: page.rows.length
    readonly property int statusPlaying: 1
    readonly property int statusPaused: 2
    readonly property var current: page.player !== null && page.player.current !== undefined && page.player.current !== null
        ? page.player.current : ({})
    readonly property int currentId: page.current.id === undefined || page.current.id === null ? -1 : Number(page.current.id)
    readonly property bool playing: page.player !== null && page.player.status === page.statusPlaying
    readonly property bool paused: page.player !== null && page.player.status === page.statusPaused
    readonly property bool phoneConnected: page.bluez !== null && page.bluez.connected === true
    readonly property int battery: page.status !== null && page.status.battery !== undefined ? page.status.battery : -1
    readonly property bool batteryPresent: page.status !== null && page.status.batteryPresent === true
    readonly property string clock: page.status !== null && page.status.clock !== undefined ? page.status.clock : ""
    readonly property bool batteryPercent: page.status !== null && page.status.batteryPercent === true
    readonly property int rowIndex: trackList.focusIndex
    readonly property var focusedRow: page.rows[page.rowIndex] === undefined ? null : page.rows[page.rowIndex]
    readonly property string artUrl: page.artPath === "" ? "" : "file://" + page.artPath
    readonly property bool collapses: !Ui.landscape
    readonly property real offset: page.collapses ? Math.max(0, trackList.contentY + trackList.topMargin) : 0
    readonly property real dockShift: Math.max(0, Math.min(Ui.px(Ui.albumDock), page.offset - Ui.px(Ui.albumCollapse)))
    readonly property real textMove: Ui.px(Ui.albumArtTop - Ui.npAlbumBaseline) - albumMetrics.descent
    readonly property real artX: Ui.px(Ui.albumArtX)
    readonly property real artY: Ui.px(Ui.albumArtTop) - page.dockShift
    readonly property real artSide: Ui.px(Ui.tileL) - Math.min(page.offset, Ui.px(Ui.albumCollapse))
    readonly property real textX: Ui.landscape ? Ui.px(Ui.npColumnRight)
        : Ui.px(Ui.edge) + Ui.px(Ui.albumDockX - Ui.edge) * Math.min(1, page.dockShift / page.textMove)
    readonly property real textWidth: page.width - Ui.px(Ui.edge) - page.textX
    readonly property real artistBaseline: Ui.px(Ui.landscape ? Ui.npArtistBaselineLandscape : Ui.npTextTop)
    readonly property real albumBaseline: Ui.px(Ui.landscape ? Ui.npAlbumBaselineLandscape : Ui.npAlbumBaseline)
    readonly property real listX: Ui.px(Ui.detailListX)
    readonly property real listY: page.collapses ? Ui.px(Ui.albumDockTop + Ui.tileS + Ui.albumRowsGap) : Ui.px(Ui.albumRowsTop)

    property alias backRing: backRing
    property alias statusStrip: statusStrip
    property alias artItem: art
    property alias titleItem: titleText
    property alias artistItem: artistText
    property alias trackList: trackList
    property alias focusBar: focusBar
    property alias sheet: sheet
    property alias backdrop: backdrop
    property alias surface: surface

    signal openQueue()
    signal sheetAction(string action, string kind, int id)
    signal playlistPicked(int index)
    signal listPlayed(int count)

    list: trackList
    pageId: page.albumId

    function callLibrary(method: string, arg: var): var {
        if (page.library === null || typeof page.library[method] !== "function")
            return undefined
        return page.library[method](arg)
    }

    function str(value: var): string {
        return value === undefined || value === null ? "" : String(value)
    }

    function formatTime(ms: real): string {
        const total = Math.max(0, Math.floor(ms / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function load(): void {
        const info = page.callLibrary("albumInfo", page.albumId)
        if (info !== undefined && info !== null && info.title !== undefined) {
            page.title = page.str(info.title)
            page.artist = page.str(info.artist)
            page.year = info.year === undefined || info.year === null ? 0 : Number(info.year)
            page.artPath = page.str(info.artPath)
        }
        const rows = []
        const ids = []
        const tracks = page.callLibrary("albumTracks", page.albumId)
        if (tracks !== undefined && tracks !== null) {
            for (let i = 0; i < tracks.length; i++) {
                const t = tracks[i]
                const id = t.id === undefined ? -1 : Number(t.id)
                const ms = t.durationMs === undefined || t.durationMs === null ? 0 : Number(t.durationMs)
                rows.push({ id: id, title: page.str(t.title), artist: page.str(t.artist), number: String(i + 1), duration: ms > 0 ? page.formatTime(ms) : "" })
                ids.push(id)
            }
        }
        page.rows = rows
        page.trackIds = ids
        trackList.forceLayout()
        trackList.snapTo(0)
        page.focusIndex = trackList.focusIndex
        if (d.ready && page.live && !page.arriving)
            Qt.callLater(page.runRefresh)
    }

    function runRefresh(): void {
        if (page.live)
            trackList.refresh()
    }

    function play(start: int): int {
        const startId = start >= 0 && start < page.trackIds.length ? page.trackIds[start] : -1
        if (page.player !== null && typeof page.player.playSource === "function")
            return Number(page.player.playSource("album", page.albumId, "play", startId))
        console.info("microdap: play album " + page.albumId + " " + start)
        return 0
    }

    function activate(): void {
        if (page.focusedRow === null)
            return
        page.listPlayed(page.play(page.rowIndex))
    }

    function moveRow(delta: int): bool {
        if (page.count === 0)
            return false
        const target = Math.max(0, Math.min(page.count - 1, trackList.focusIndex + delta))
        if (target === trackList.focusIndex)
            return false
        trackList.setFocus(target, Motion.scroll)
        page.focusIndex = trackList.focusIndex
        return true
    }

    function pinLabel(kind: string, id: int): string {
        const pinned = page.library !== null && typeof page.library.isPinned === "function"
            && page.library.isPinned(kind, id) === true
        return pinned ? "unpin from quickplay" : "pin to quickplay"
    }

    function sheetItemsFor(kind: string, id: int): var {
        return ["play", "play next", "add to queue", "add to playlist", page.pinLabel(kind, id), "info"]
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        return kind === "row" && page.openSheet()
    }

    function openSheet(): bool {
        if (page.focusedRow === null)
            return false
        page.sheetRow = { kind: "song", id: page.focusedRow.id, title: page.focusedRow.title, row: page.rowIndex }
        sheet.title = page.focusedRow.title
        sheet.items = page.sheetItemsFor("song", page.focusedRow.id)
        sheet.open()
        return true
    }

    function runSheet(index: int): void {
        const t = page.sheetRow
        if (t === null)
            return
        const action = sheet.items[index]
        if (action === "play")
            page.play(t.row)
        else
            page.sheetAction(action, t.kind, t.id)
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        page.stack.pop()
        return true
    }

    function keepFocusVisible(): void {
        trackList.keepFocusVisible()
    }

    function completeAll(): void {
        trackList.settle()
        trackList.completeScroll()
        sheet.complete()
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
            return page.pageFocus(-1)
        case "rt":
            return page.pageFocus(1)
        case "a":
            if (!repeat)
                page.activate()
            return true
        case "x":
            if (!repeat)
                page.openSheet()
            return true
        case "y":
            if (!repeat)
                page.sheetAction("add to queue", "album", page.albumId)
            return true
        case "back":
            if (!repeat)
                page.openQueue()
            return true
        case "b":
            if (repeat)
                return false
            return page.back()
        default:
            return false
        }
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            trackList.feather(1)
    }

    onAlbumIdChanged: {
        if (d.ready)
            page.load()
    }

    onLibraryChanged: {
        if (d.ready)
            page.load()
    }

    Component.onCompleted: {
        page.load()
        d.ready = true
    }

    QtObject {
        id: d

        property bool ready: false
    }

    component TrackRow: Item {
        id: row

        required property int index
        required property var modelData
        readonly property bool isFocused: trackList.focusIndex === row.index
        readonly property bool isPlaying: page.currentId >= 0 && page.currentId === row.modelData.id
        readonly property Item rowItem: listRow
        readonly property string touchKind: "row"
        readonly property int touchIndex: row.index
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        width: trackList.width
        height: trackList.rowPitch
        transform: Translate { x: Ui.px(row.shift) * (1 - row.enter) }
        opacity: row.enter

        ListView.onPooled: {
            enterAnim.stop()
            row.enter = 1
        }

        Connections {
            target: trackList

            function onEntrance(shift, stagger, cap, duration, stripCurve) {
                enterAnim.stop()
                row.shift = shift
                row.stagger = Math.min(cap, Math.max(0, row.index - trackList.firstVisible())) * stagger
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
            number: row.modelData.number
            primary: row.modelData.title
            trailing: row.modelData.duration
            playing: row.isPlaying
            focused: row.isFocused
            pressed: row.isFocused && page.pressed
            animated: page.live
        }
    }

    Rectangle {
        id: surface

        anchors.fill: parent
        color: Ui.bg
    }

    Backdrop {
        id: backdrop

        anchors.fill: parent
    }

    FocusList {
        id: trackList

        signal entrance(real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(trackList.contentY / trackList.rowPitch))
        }

        function feather(dir: int): void {
            trackList.forceLayout()
            trackList.entrance(dir * Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        function refresh(): void {
            trackList.forceLayout()
            trackList.entrance(Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap, Motion.listChange, false)
        }

        x: page.listX
        y: page.listY
        width: page.width - page.listX
        height: page.height - trackList.y
        topMargin: page.collapses ? Ui.px(Ui.albumCollapse + Ui.albumDock) : 0
        model: page.rows
        delegate: TrackRow {}
    }

    FontMetrics {
        id: albumMetrics

        font: Ui.font(Ui.npAlbum)
    }

    FocusBar {
        id: focusBar

        visible: page.focusedRow !== null
        x: trackList.x + Ui.px(Ui.hubEdge - Ui.focusBarOffset)
        rowY: trackList.y + trackList.focusY
        rowHeight: trackList.rowPitch
        opacity: page.focusOpacity
        animated: page.live
    }

    Item {
        id: backRingClip

        readonly property string touchKind: "back"

        width: Ui.px(Ui.backRingX + Ui.backRing)
        height: Ui.px(Ui.backRingY + Ui.backRing)
        clip: true

        Ring {
            id: backRing

            x: Ui.px(Ui.backRingX)
            y: Ui.px(Ui.backRingY)
            size: Ui.backRing
            focused: true
            glyph: "back"
            animated: page.live
        }
    }

    StatusStrip {
        id: statusStrip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        clock: page.clock
        percent: page.batteryPercent
        battery: page.battery
        batteryPresent: page.batteryPresent
        bluetooth: page.phoneConnected || (page.bluez !== null && page.bluez.headsetName !== undefined && page.bluez.headsetName !== "")
        playing: page.playing
        paused: page.paused
    }

    Text {
        id: artistText

        x: page.textX
        y: page.artistBaseline - artistText.baselineOffset
        width: page.textWidth
        text: page.artist
        color: Ui.white(Ui.npArtist.alpha)
        font: Ui.font(Ui.npArtist)
        clip: true
    }

    Text {
        id: titleText

        x: page.textX
        y: page.albumBaseline - titleText.baselineOffset
        width: page.textWidth
        text: page.title
        color: Ui.white(Ui.npAlbum.alpha)
        font: Ui.font(Ui.npAlbum)
        clip: true
    }

    Tile {
        id: art

        x: page.artX
        y: page.artY
        size: Ui.tileL
        transformOrigin: Item.TopLeft
        scale: page.artSide / Ui.px(Ui.tileL)
        captioned: false
        source: page.artUrl
        animated: false
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: !page.live
        onAccepted: (index) => {
            if (sheet.depth > 0) {
                page.playlistPicked(index)
                sheet.close()
                return
            }
            page.runSheet(index)
            if (sheet.depth === 0)
                sheet.close()
        }
    }
}
