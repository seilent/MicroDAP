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
    readonly property int rowIndex: trackList.focusIndex
    readonly property var focusedRow: page.rows[page.rowIndex] === undefined ? null : page.rows[page.rowIndex]
    readonly property string artUrl: page.artPath === "" ? "" : "file://" + page.artPath
    readonly property real artX: Ui.px(Ui.albumArtX)
    readonly property real artY: Ui.px(Ui.albumArtTop)
    readonly property real artSide: Ui.px(Ui.tileL)
    readonly property real textX: Ui.px(Ui.landscape ? Ui.npColumnRight : Ui.edge)
    readonly property real textWidth: page.width - Ui.px(Ui.edge) - page.textX
    readonly property real artistBaseline: Ui.px(Ui.landscape ? Ui.npArtistBaselineLandscape : Ui.npTextTop)
    readonly property real albumBaseline: Ui.px(Ui.landscape ? Ui.npAlbumBaselineLandscape : Ui.npAlbumBaseline)
    readonly property real listX: Ui.px(Ui.detailListX)
    readonly property real listY: Ui.px(Ui.albumRowsTop)

    property alias backRing: backRing
    property alias statusStrip: statusStrip
    property alias artItem: art
    property alias titleItem: titleText
    property alias artistItem: artistText
    property alias trackList: trackList
    property alias focusBar: focusBar
    property alias sheet: sheet
    property alias backdrop: backdrop

    signal sheetAction(string action, string kind, int id)

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
    }

    function play(start: int): void {
        if (page.player !== null && typeof page.player.playTracks === "function") {
            page.player.playTracks(page.trackIds, start)
            return
        }
        console.info("casdap: play album " + page.albumId + " " + start)
    }

    function activate(): void {
        if (page.focusedRow === null)
            return
        page.play(page.rowIndex)
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
        return ["play", "add to queue", page.pinLabel(kind, id)]
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

    function completeAll(): void {
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

        width: trackList.width
        height: trackList.rowPitch

        ListRow {
            id: listRow

            width: row.width
            number: row.modelData.number
            primary: row.modelData.title
            trailing: row.modelData.duration
            playingDot: row.isPlaying
            focused: row.isFocused
            pressed: row.isFocused && page.pressed
            animated: page.live
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    Backdrop {
        id: backdrop

        anchors.fill: parent
        source: page.artUrl
        paused: !page.live
        screenOff: page.screenOff
    }

    FocusList {
        id: trackList

        x: page.listX
        y: page.listY
        width: page.width - page.listX
        height: page.height - trackList.y
        model: page.rows
        delegate: TrackRow {}
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

    Ring {
        id: backRing

        x: Ui.px(Ui.backRingInset)
        y: Ui.px(Ui.backRingInset)
        size: Ui.backRing
        focused: true
        glyph: "back"
        animated: page.live
    }

    StatusStrip {
        id: statusStrip

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        clock: page.clock
        battery: page.battery
        batteryPresent: page.batteryPresent
        bluetooth: page.phoneConnected
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
        captioned: false
        source: page.artUrl
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
}
