pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property int artistId: -1
    property string name: ""
    property string artPath: ""
    property QtObject library: null
    property QtObject player: null
    readonly property int currentId: page.player !== null && page.player.current !== undefined && page.player.current !== null
        && page.player.current.id !== undefined && page.player.current.id !== null ? Number(page.player.current.id) : -1
    property bool animated: true
    property bool screenOff: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property int focusRow: 0
    property int ringIndex: 0
    property var albumRows: []
    property var songRows: []
    property var sheetRow: null

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property var pivots: ["albums", "songs"]
    readonly property var ringGlyphs: ["play", "plus", "shuffle"]
    readonly property var panes: [albumsPane, songsPane]
    readonly property FocusList activeList: page.panes[page.pivotIndex]
    readonly property var currentRows: page.pivotIndex === 0 ? page.albumRows : page.songRows
    readonly property int rowIndex: page.activeList.focusIndex
    readonly property var focusedRow: page.currentRows[page.rowIndex] === undefined ? null : page.currentRows[page.rowIndex]
    readonly property bool switching: pivotAnim.running
    readonly property real listX: Ui.px(Ui.detailListX)
    readonly property real listY: Ui.px(Ui.detailRowsTop)
    readonly property real labelX: Ui.px(Ui.detailListX + Ui.hubEdge)

    property alias header: header
    property alias strip: strip
    property alias nameItem: nameText
    property alias labelItem: labelText
    property alias ringRepeater: ringRepeater
    property alias albumsPane: albumsPane
    property alias songsPane: songsPane
    property alias focusBar: focusBar
    property alias sheet: sheet
    property alias backdrop: backdrop
    property alias surface: surface

    signal openAlbum(int id)
    signal openQueue()
    signal sheetAction(string action, string kind, int id)
    signal listPlayed(int count)

    list: page.activeList
    pageId: page.artistId

    function callLibrary(method: string, arg: var): var {
        if (page.library === null || typeof page.library[method] !== "function")
            return undefined
        return page.library[method](arg)
    }

    function str(value: var): string {
        return value === undefined || value === null ? "" : String(value)
    }

    function load() {
        const info = page.callLibrary("artistInfo", page.artistId)
        if (info !== undefined && info !== null) {
            if (info.name !== undefined)
                page.name = page.str(info.name)
            if (info.artPath !== undefined)
                page.artPath = page.str(info.artPath)
        }
        const albums = []
        const albumRows = page.callLibrary("artistAlbumRows", page.artistId)
        if (albumRows !== undefined && albumRows !== null) {
            for (let i = 0; i < albumRows.length; i++) {
                const a = albumRows[i]
                if (a === undefined || a === null || a.title === undefined)
                    continue
                const year = a.year === undefined || a.year === null ? 0 : Number(a.year)
                albums.push({ kind: "album", id: Number(a.id), title: page.str(a.title), subtitle: year > 0 ? String(year) : "", artPath: page.str(a.artPath) })
            }
        }
        const songs = []
        const tracks = page.callLibrary("artistTrackRows", page.artistId)
        if (tracks !== undefined && tracks !== null) {
            for (let i = 0; i < tracks.length; i++) {
                const t = tracks[i]
                const id = t.id === undefined ? -1 : Number(t.id)
                songs.push({ kind: "song", id: id, title: page.str(t.title), subtitle: page.str(t.artist), artPath: "", number: String(i + 1) })
            }
        }
        page.albumRows = albums
        page.songRows = songs
        for (const pane of page.panes) {
            pane.forceLayout()
            pane.snapTo(0)
        }
        page.syncFocus()
        if (d.ready && page.live && !page.arriving)
            Qt.callLater(page.runRefresh)
    }

    function runRefresh() {
        if (!page.live)
            return
        page.panes[page.shownPivot].refresh()
    }

    function playSource(kind: string, id: int, mode: string, startId: int): int {
        if (page.player !== null && typeof page.player.playSource === "function")
            return Number(page.player.playSource(kind, id, mode, startId))
        console.info("microdap: " + mode + " " + kind + " " + id)
        return 0
    }

    function playArtist(mode: string): int {
        return page.playSource("artist", page.artistId, mode, -1)
    }

    function addToQueue() {
        page.sheetAction("add to queue", "artist", page.artistId)
    }

    function playAlbum(albumId: int): void {
        page.playSource("album", albumId, "play", -1)
    }

    function playSong(row: var): int {
        return page.playSource("artist", page.artistId, "play", row.id)
    }

    function ring(index: int): Item {
        return ringRepeater.itemAt(index)
    }

    function ringFocused(index: int): bool {
        return page.focusRow === 0 && page.ringIndex === index
    }

    function activateRing(index: int): void {
        if (index === 1) {
            page.addToQueue()
        } else {
            page.listPlayed(page.playArtist(index === 0 ? "play" : "shuffle"))
        }
        const item = page.ring(index)
        if (item !== null)
            item.toggled()
    }

    function activate() {
        if (page.focusRow === 0) {
            page.activateRing(page.ringIndex)
            return
        }
        const t = page.focusedRow
        if (t === null)
            return
        if (t.kind === "album") {
            page.openAlbum(t.id)
            return
        }
        page.listPlayed(page.playSong(t))
    }

    function sheetTarget(): var {
        if (page.focusRow === 0)
            return { kind: "artist", id: page.artistId, title: page.name }
        return page.focusedRow
    }

    function pinLabel(kind: string, id: int): string {
        const pinned = page.library !== null && typeof page.library.isPinned === "function"
            && page.library.isPinned(kind, id) === true
        return pinned ? "unpin from quickplay" : "pin to quickplay"
    }

    function sheetItemsFor(kind: string, id: int): var {
        if (kind === "song")
            return ["play", "play next", "add to queue", page.pinLabel(kind, id)]
        return ["play", "play next", "add to queue", "shuffle", page.pinLabel(kind, id)]
    }

    function queueAction(): void {
        if (page.focusRow === 0) {
            page.playArtist("shuffle")
            return
        }
        const t = page.focusedRow
        if (t !== null)
            page.sheetAction("add to queue", t.kind, t.id)
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        return (kind === "ring" || kind === "row") && page.openSheet()
    }

    function openSheet(): bool {
        const t = page.sheetTarget()
        if (t === null)
            return false
        page.sheetRow = t
        sheet.title = t.title
        sheet.items = page.sheetItemsFor(t.kind, t.id)
        sheet.open()
        return true
    }

    function runSheet(index: int): void {
        const t = page.sheetRow
        if (t === null)
            return
        const action = sheet.items[index]
        if (action !== "play") {
            page.sheetAction(action, t.kind, t.id)
            return
        }
        if (t.kind === "artist")
            page.playArtist("play")
        else if (t.kind === "album")
            page.playAlbum(t.id)
        else
            page.playSong(t)
    }

    function syncFocus() {
        page.focusIndex = page.activeList.focusIndex
        if (page.focusRow === 1 && page.currentRows.length === 0)
            page.focusRow = 0
    }

    function moveRing(delta: int): bool {
        if (page.focusRow !== 0)
            return false
        const target = Math.max(0, Math.min(Ui.detailRingCount - 1, page.ringIndex + delta))
        if (target === page.ringIndex)
            return false
        page.ringIndex = target
        return true
    }

    function moveRow(delta: int): bool {
        if (page.focusRow === 0) {
            if (delta < 0 || page.currentRows.length === 0)
                return false
            page.focusRow = 1
            page.activeList.setFocus(page.activeList.focusIndex, Motion.scroll)
            page.syncFocus()
            return true
        }
        if (delta < 0 && page.activeList.focusIndex === 0) {
            page.focusRow = 0
            return true
        }
        const target = Math.max(0, Math.min(page.currentRows.length - 1, page.activeList.focusIndex + delta))
        if (target === page.activeList.focusIndex)
            return false
        page.activeList.setFocus(target, Motion.scroll)
        page.syncFocus()
        return true
    }

    function pageRow(dir: int): bool {
        if (page.focusRow !== 1)
            return false
        const moved = page.activeList.pageFocus(dir)
        page.syncFocus()
        return moved
    }

    function landPivot(): void {
        pivotAnim.stop()
        for (const pane of page.panes) {
            pane.shift = 0
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

    function completeAll() {
        page.landPivot()
        for (const pane of page.panes)
            pane.completeScroll()
        sheet.complete()
        for (let i = 0; i < ringRepeater.count; i++) {
            const item = ringRepeater.itemAt(i)
            if (item !== null && item.swapping)
                item.toggled()
        }
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        page.stack.pop()
        return true
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        switch (kind) {
        case "ring":
            page.focusRow = 0
            page.ringIndex = index
            return "a"
        case "row":
            page.focusRow = 1
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
        case "left":
            return page.moveRing(-1)
        case "right":
            return page.moveRing(1)
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
            if (!repeat)
                page.activate()
            return true
        case "x":
            if (!repeat)
                page.openSheet()
            return true
        case "y":
            if (!repeat)
                page.queueAction()
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

    onPivotIndexChanged: {
        if (!d.internal)
            page.landPivot()
        page.syncFocus()
    }

    onEntered: (first, kind) => {
        if (page.arriving)
            page.panes[page.shownPivot].feather(1)
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    onArtistIdChanged: {
        if (d.ready)
            page.load()
    }

    onLibraryChanged: {
        if (d.ready)
            page.load()
    }

    Component.onCompleted: {
        page.load()
        page.shownPivot = page.pivotIndex
        page.focusRow = 0
        page.ringIndex = 0
        d.ready = true
    }

    QtObject {
        id: d

        property bool internal: false
        property bool ready: false
    }

    component Pane: FocusList {
        id: pane

        property int pivot: 0
        property real shift: 0

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

        x: page.listX + pane.shift
        y: page.listY
        width: page.width - page.listX
        height: page.height - pane.y
        visible: page.shownPivot === pane.pivot
        onTouchFocused: page.syncFocus()
    }

    component AlbumRow: Item {
        id: row

        required property int index
        required property var modelData
        readonly property var pane: row.ListView.view
        readonly property bool isFocused: page.focusRow === 1 && page.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property Item tileItem: tile
        readonly property Item ringItem: playRing
        readonly property Item titleItem: titleText
        readonly property Item subItem: subText
        readonly property string touchKind: "row"
        readonly property int touchIndex: row.index
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        width: row.pane.width
        height: row.pane.rowPitch
        transform: Translate { x: Ui.px(row.shift) * (1 - row.enter) }
        opacity: row.enter

        ListView.onPooled: {
            enterAnim.stop()
            row.enter = 1
        }

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

        Tile {
            id: tile

            x: Ui.px(Ui.hubEdge)
            y: 0
            size: Ui.tileL
            captioned: false
            source: row.modelData.artPath !== "" ? "file://" + row.modelData.artPath : ""
            pressed: row.isFocused && page.pressed
            animated: page.live
        }

        Ring {
            id: playRing

            x: tile.x + tile.width - playRing.width - Ui.px(Ui.detailRingInset)
            y: tile.y + Ui.px(Ui.detailRingInset)
            size: Ui.detailRing
            glyph: "play"
            focused: row.isFocused
            animated: page.live
        }

        Text {
            id: titleText

            x: Ui.px(Ui.detailTextX)
            y: Ui.px(Ui.detailTitleDrop) - titleText.baselineOffset
            width: row.width - titleText.x
            text: row.modelData.title
            color: Ui.white(row.isFocused ? Ui.detailTitle.alpha : Ui.alphaHigh)
            font: Ui.font(Ui.detailTitle)
            wrapMode: Text.Wrap
            maximumLineCount: Ui.detailTitleLines
            lineHeightMode: Text.FixedHeight
            lineHeight: Ui.px(Ui.detailTitlePitch)
            clip: true

            Behavior on color {
                enabled: page.live
                ColorAnimation {
                    duration: Motion.focusAlpha
                }
            }
        }

        Text {
            id: subText

            x: Ui.px(Ui.detailTextX)
            y: Ui.px(Ui.detailTitleDrop + (titleText.lineCount - 1) * Ui.detailTitlePitch + Ui.detailSubPitch) - subText.baselineOffset
            width: row.width - Ui.px(Ui.hubEdge) - subText.x
            text: row.modelData.subtitle
            color: Ui.white(Ui.detailSub.alpha)
            font: Ui.font(Ui.detailSub)
            clip: true
        }
    }

    component SongRow: Item {
        id: row

        required property int index
        required property var modelData
        readonly property var pane: row.ListView.view
        readonly property bool isFocused: page.focusRow === 1 && page.pivotIndex === row.pane.pivot && row.pane.focusIndex === row.index
        readonly property Item rowItem: listRow
        readonly property string touchKind: "row"
        readonly property int touchIndex: row.index
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : 0
        property int stagger: 0
        property int enterDuration: 0
        property var enterCurve: Motion.strip

        width: row.pane.width
        height: row.pane.rowPitch
        transform: Translate { x: Ui.px(row.shift) * (1 - row.enter) }
        opacity: row.enter

        ListView.onPooled: {
            enterAnim.stop()
            row.enter = 1
        }

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
            number: row.modelData.number
            primary: row.modelData.title
            secondary: row.modelData.subtitle
            playing: page.currentId >= 0 && Number(row.modelData.id) === page.currentId
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

    Pane {
        id: albumsPane

        pivot: 0
        rowPitch: Ui.px(Ui.detailRow)
        model: page.albumRows
        delegate: AlbumRow {}
    }

    Pane {
        id: songsPane

        pivot: 1
        model: page.songRows
        delegate: SongRow {}
    }

    FocusBar {
        id: focusBar

        visible: page.focusRow === 1 && page.shownPivot === page.pivotIndex && page.focusedRow !== null
        x: page.activeList.x + Ui.px(Ui.hubEdge - Ui.focusBarOffset)
        rowY: page.activeList.y + page.activeList.focusY
        rowHeight: page.activeList.rowPitch
        opacity: page.activeList.opacity * page.focusOpacity
        animated: page.live
    }

    Text {
        id: nameText

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(Ui.detailNameBaseline) - nameText.baselineOffset
        width: Ui.px((Ui.landscape ? Ui.detailColumn : Ui.canvasW) - Ui.hubEdge)
        text: page.name
        color: Ui.white(Ui.detailName.alpha)
        font: Ui.font(Ui.detailName)
        clip: true
    }

    Repeater {
        id: ringRepeater

        model: Ui.detailRingCount

        Ring {
            id: ringItem

            required property int index
            readonly property bool isFocused: page.ringFocused(ringItem.index)

            x: Ui.px(Ui.detailRingX(ringItem.index))
            y: Ui.px(Ui.detailRingTop)
            size: Ui.detailRing
            glyph: page.ringGlyphs[ringItem.index]
            focused: ringItem.isFocused
            pressed: ringItem.isFocused && page.pressed
            animated: page.live

            Item {
                id: ringHit

                readonly property string touchKind: "ring"
                readonly property int touchIndex: ringItem.index

                anchors.centerIn: parent
                width: ringItem.width + Ui.px(Ui.detailRingGap)
                height: ringHit.width
            }
        }
    }

    Text {
        id: labelText

        x: page.labelX
        y: Ui.px(Ui.detailLabelBaseline) - labelText.baselineOffset
        text: "in collection"
        color: Ui.white(Ui.sectionLabel.alpha)
        font: Ui.font(Ui.sectionLabel)
    }

    CroppedHeader {
        id: header

        text: "artist"
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
                property: "shift"
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
                pivotAnim.outgoing.shift = 0
                pivotAnim.outgoing.opacity = 1
                pivotAnim.incoming.feather(pivotAnim.dir)
            }
        }
        PauseAnimation {
            duration: Motion.pivotRows + Motion.pivotRowCap * Motion.pivotRowStagger
        }
    }
}
