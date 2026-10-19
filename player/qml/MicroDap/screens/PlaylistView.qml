pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property QtObject library: null
    property QtObject favorites: null
    property QtObject player: null
    property string kind: "playlist"
    property int playlistId: -1
    property bool animated: true
    property bool screenOff: false
    property var rows: []
    property var sheetRow: null
    property string shownCount: ""

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property bool isFavorites: page.kind === "favorites"
    readonly property string name: page.isFavorites ? "favorites" : page.infoName()
    readonly property bool editable: page.isFavorites
        || (page.library !== null && typeof page.library.editable === "function" && page.library.editable(page.playlistId) === true)
    readonly property var current: page.player !== null && page.player.current !== undefined && page.player.current !== null
        ? page.player.current : ({})
    readonly property int currentId: page.current.id === undefined || page.current.id === null ? -1 : Number(page.current.id)
    readonly property string countLine: page.rows.length === 1 ? "1 song" : page.rows.length + " songs"
    readonly property int rowIndex: rowList.focusIndex
    readonly property var focusedRow: page.rows[page.rowIndex] === undefined ? null : page.rows[page.rowIndex]
    readonly property bool countSwapping: countAnim.running
    property alias header: header
    property alias countItem: countLabel
    property alias listView: rowList
    property alias focusBar: focusBar
    property alias sheet: sheet

    signal sheetAction(string action, string kind, int id)
    signal listPlayed(int count)
    signal refused(string text, string name)

    objectName: page.isFavorites ? "favoritesview" : "playlistview"
    pageId: page.playlistId
    list: rowList

    function infoName(): string {
        if (page.library === null || typeof page.library.playlistInfo !== "function")
            return ""
        const info = page.library.playlistInfo(page.playlistId)
        return info === undefined || info === null || info.name === undefined ? "" : String(info.name)
    }

    function trackIds(): var {
        if (page.isFavorites)
            return page.favorites !== null && typeof page.favorites.trackIds === "function" ? page.favorites.trackIds() : []
        return page.library !== null && typeof page.library.playlistTracks === "function"
            ? page.library.playlistTracks(page.playlistId) : []
    }

    function buildRows(): var {
        const ids = page.trackIds()
        if (ids.length === 0 || page.library === null || typeof page.library.trackRows !== "function")
            return []
        return page.library.trackRows(ids).map((r) => ({
            id: Number(r.id),
            title: r.title === undefined || r.title === null ? "" : String(r.title),
            artist: r.artist === undefined || r.artist === null ? "" : String(r.artist)
        }))
    }

    function reload(): void {
        const focus = rowList.focusIndex
        page.rows = page.buildRows()
        rowList.forceLayout()
        if (page.rows.length > 0)
            rowList.snapTo(Math.max(0, Math.min(focus, page.rows.length - 1)))
        page.focusIndex = rowList.focusIndex
        if (d.ready && page.live)
            Qt.callLater(page.runRefresh)
    }

    function runRefresh(): void {
        if (page.live)
            rowList.refresh()
    }

    function swapCount(): void {
        countAnim.stop()
        if (!page.live || !d.ready || page.shownCount === page.countLine) {
            page.shownCount = page.countLine
            countLabel.opacity = 1
            return
        }
        countAnim.start()
    }

    function play(index: int): int {
        const r = page.rows[index]
        if (r === undefined || page.player === null || typeof page.player.playSource !== "function")
            return 0
        return Number(page.player.playSource(page.isFavorites ? "favorites" : "playlist", page.isFavorites ? 0 : page.playlistId,
            "play", r.id))
    }

    function activate(): void {
        if (page.focusedRow === null)
            return
        page.listPlayed(page.play(page.rowIndex))
    }

    function verbsFor(index: int): var {
        if (!page.editable)
            return ["info"]
        const items = ["remove"]
        if (index > 0)
            items.push("move up")
        if (index < page.rows.length - 1)
            items.push("move down")
        items.push("info")
        return items
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        return kind === "row" && page.openSheet()
    }

    function openSheet(): bool {
        const r = page.focusedRow
        if (r === null)
            return false
        const items = page.verbsFor(page.rowIndex)
        if (items.length === 0)
            return false
        page.sheetRow = { index: page.rowIndex, id: r.id, title: r.title }
        sheet.title = r.title
        sheet.items = items
        sheet.open()
        return true
    }

    function edit(verb: string, index: int): void {
        let error = ""
        if (page.isFavorites) {
            if (page.favorites === null)
                return
            if (verb === "remove") {
                const r = page.rows[index]
                if (r !== undefined && typeof page.favorites.toggle === "function")
                    page.favorites.toggle(r.id)
            } else if (typeof page.favorites.move === "function") {
                error = String(page.favorites.move(index, verb === "move up" ? -1 : 1))
            }
        } else if (page.library !== null && typeof page.library.editPlaylistRow === "function") {
            const r = page.rows[index]
            if (r === undefined)
                return
            error = String(page.library.editPlaylistRow(page.playlistId, index, r.id, verb))
        }
        if (error !== "") {
            page.refused(error, page.name)
            return
        }
        page.reload()
        if (verb === "move up" || verb === "move down") {
            rowList.setFocus(index + (verb === "move up" ? -1 : 1), Motion.scroll)
            page.focusIndex = rowList.focusIndex
        }
    }

    function runSheet(index: int): void {
        const t = page.sheetRow
        const action = sheet.items[index]
        if (t === null || action === undefined)
            return
        if (action === "info")
            page.sheetAction("info", "song", t.id)
        else
            page.edit(action, t.index)
    }

    function moveRow(delta: int): bool {
        if (page.rows.length === 0)
            return false
        const target = Math.max(0, Math.min(page.rows.length - 1, rowList.focusIndex + delta))
        if (target === rowList.focusIndex)
            return false
        rowList.setFocus(target, Motion.scroll)
        page.focusIndex = rowList.focusIndex
        return true
    }

    function pageRow(direction: int): bool {
        const moved = rowList.pageFocus(direction)
        page.focusIndex = rowList.focusIndex
        return moved
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        page.stack.pop()
        return true
    }

    function keepFocusVisible(): void {
        rowList.keepFocusVisible()
    }

    function completeAll(): void {
        rowList.settle()
        rowList.completeScroll()
        countAnim.stop()
        page.shownCount = page.countLine
        countLabel.opacity = 1
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
            return page.pageRow(-1)
        case "rt":
            return page.pageRow(1)
        case "a":
            if (!repeat)
                page.activate()
            return true
        case "x":
            if (!repeat)
                page.openSheet()
            return true
        case "y":
            if (!repeat && page.focusedRow !== null)
                page.sheetAction("add to queue", "song", page.focusedRow.id)
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

    onCountLineChanged: page.swapCount()

    onEntered: (first, kind) => {
        if (page.arriving)
            rowList.feather()
    }

    Component.onCompleted: {
        page.rows = page.buildRows()
        rowList.forceLayout()
        page.focusIndex = rowList.focusIndex
        page.shownCount = page.countLine
        d.ready = true
    }

    QtObject {
        id: d

        property bool ready: false
    }

    Connections {
        target: page.isFavorites ? null : page.library
        ignoreUnknownSignals: true

        function onPlaylistsChanged() {
            page.reload()
        }

        function onTracksChanged() {
            page.reload()
        }
    }

    Connections {
        target: page.isFavorites ? page.favorites : null
        ignoreUnknownSignals: true

        function onChanged() {
            page.reload()
        }
    }

    component PlaylistRow: Item {
        id: item

        required property int index
        required property var modelData
        readonly property bool isFocused: rowList.focusIndex === item.index
        readonly property Item rowItem: listRow
        readonly property string touchKind: "row"
        readonly property int touchIndex: item.index
        property real enter: page.arriving ? 0 : 1
        property real shift: page.arriving ? Motion.pivotShift : Motion.listChangeShift
        property int stagger: 0
        property int enterDuration: Motion.listChange
        property var enterCurve: Motion.decel

        width: rowList.width
        height: rowList.rowPitch
        transform: Translate { x: Ui.px(item.shift) * (1 - item.enter) }
        opacity: item.enter

        ListView.onPooled: {
            enterAnim.stop()
            item.enter = 1
        }

        Connections {
            target: rowList

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
            number: String(item.index + 1)
            primary: item.modelData.title
            secondary: item.modelData.artist
            playing: page.currentId >= 0 && page.currentId === item.modelData.id
            focused: item.isFocused
            pressed: item.isFocused && page.pressed
            animated: page.live
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    FocusList {
        id: rowList

        signal entrance(int from, real shift, int stagger, int cap, int duration, bool stripCurve)
        signal settle()

        function firstVisible(): int {
            return Math.max(0, Math.floor(rowList.contentY / rowList.rowPitch))
        }

        function feather(): void {
            rowList.forceLayout()
            rowList.entrance(rowList.firstVisible(), Motion.pivotShift, Motion.pivotRowStagger, Motion.pivotRowCap, Motion.pivotRows, true)
        }

        function refresh(): void {
            rowList.forceLayout()
            rowList.entrance(rowList.firstVisible(), Motion.listChangeShift, Motion.listChangeStagger, Motion.listChangeCap,
                Motion.listChange, false)
        }

        x: 0
        y: Ui.px(Ui.queueRowsTop)
        width: page.width
        height: page.height - rowList.y
        model: page.rows
        delegate: PlaylistRow {}
        onFocusIndexChanged: page.focusIndex = rowList.focusIndex
    }

    FocusBar {
        id: focusBar

        visible: page.focusedRow !== null
        x: Ui.px(Ui.hubEdge - Ui.focusBarOffset)
        rowY: rowList.y + rowList.focusY
        rowHeight: rowList.rowPitch
        opacity: page.focusOpacity
        animated: page.live
    }

    CroppedHeader {
        id: header

        text: page.name
        animated: page.live
    }

    Text {
        id: countLabel

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(Ui.queueCountBaseline) - countLabel.baselineOffset
        text: page.shownCount
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
            script: page.shownCount = page.countLine
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
        screenOff: !page.live
        onAccepted: (index) => {
            page.runSheet(index)
            if (sheet.depth === 0)
                sheet.close()
        }
    }
}
