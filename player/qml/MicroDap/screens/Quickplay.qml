pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: qp

    property var pinsModel: null
    property var historyModel: null
    property var newModel: null
    property QtObject bluez: null
    property bool animated: true
    property bool active: false
    property bool pressed: false
    property int focusIndex: 0
    property bool touchFocusing: false

    readonly property bool phoneTile: qp.bluez !== null && qp.bluez.connected === true
    readonly property string phoneName: qp.phoneTile ? qp.bluez.deviceName : ""
    readonly property int pinsCount: (qp.phoneTile ? 1 : 0) + pinsRep.count
    readonly property int historyCount: historyRep.count
    readonly property int newCount: newRep.count
    readonly property int count: qp.pinsCount + qp.historyCount + qp.newCount
    readonly property var sectionCounts: [qp.pinsCount, qp.historyCount, qp.newCount]
    readonly property real tilePx: Ui.px(Ui.qpTile)
    readonly property real gutterPx: Ui.px(Ui.qpGutter)
    readonly property real padPx: Ui.px(Ui.qpTilePad)
    readonly property real rowPitch: qp.tilePx + qp.gutterPx
    readonly property real gridW: 2 * qp.tilePx + qp.gutterPx
    property alias flick: flick
    property alias sections: sections
    property alias pinsSection: pinsSection
    property alias historySection: historySection
    property alias newSection: newSection
    property alias newHeader: newHeader
    property alias phoneItem: phone
    property alias pinsHeader: pinsHeader
    property alias historyHeader: historyHeader
    property alias emptyText: emptyText
    readonly property real firstHeaderBaseline: sections.y + pinsSection.y + pinsHeader.y + pinsHeader.baselineOffset
    readonly property real hintBaseline: sections.y + pinsSection.y + emptyText.y + emptyText.baselineOffset
    readonly property real secondHeaderBaseline: sections.y + historySection.y + historyHeader.y + historyHeader.baselineOffset
    readonly property bool scrolling: scrollAnim.running

    signal panBack()
    signal play(var entry)
    signal openPhone()
    signal pin(var entry)
    signal touchFront()

    width: Ui.px(Ui.qpW)

    component QpTile: Tile {
        id: t

        required property int index
        required property var model
        property int offset: 0
        property string sectionName: ""
        property string defaultKind: ""
        readonly property int flat: t.offset + t.index
        readonly property string touchKind: "tile"
        readonly property int touchIndex: t.flat
        readonly property string kind: t.model.kind === undefined || t.model.kind === null || t.model.kind === ""
            ? t.defaultKind : String(t.model.kind)
        readonly property var entry: ({
            section: t.sectionName,
            kind: t.kind,
            id: t.model.id,
            title: t.model.title,
            subtitle: t.model.subtitle,
            artPath: t.model.artPath,
            phone: false
        })

        size: Ui.qpTile
        source: t.model.artPath ? "file://" + t.model.artPath : ""
        title: t.model.title
        subtitle: t.model.subtitle
        captioned: false
        focused: qp.active && qp.focusIndex === t.flat
        pressed: t.focused && qp.pressed
        animated: qp.animated
    }

    component SectionHeader: Text {
        color: Ui.white(Ui.quickplayHeader.alpha)
        font: Ui.font(Ui.quickplayHeader)
        onBaselineOffsetChanged: qp.scheduleSettle()
    }

    function sectionOf(index: int): var {
        let rest = index
        for (let s = 0; s < 3; s++) {
            const n = qp.sectionCounts[s]
            if (rest < n)
                return { section: s, local: rest }
            rest -= n
        }
        return { section: -1, local: -1 }
    }

    function sectionStart(section: int): int {
        let start = 0
        for (let s = 0; s < section; s++)
            start += qp.sectionCounts[s]
        return start
    }

    function tileAt(index: int): Item {
        const p = qp.sectionOf(index)
        if (p.section < 0)
            return null
        if (p.section === 0) {
            if (qp.phoneTile) {
                if (p.local === 0)
                    return phone
                return pinsRep.itemAt(p.local - 1)
            }
            return pinsRep.itemAt(p.local)
        }
        if (p.section === 1)
            return historyRep.itemAt(p.local)
        return newRep.itemAt(p.local)
    }

    function entryAt(index: int): var {
        const item = qp.tileAt(index)
        return item === null ? null : item.entry
    }

    function clampIndex(index: int): int {
        return Math.max(0, Math.min(qp.count - 1, index))
    }

    function column(index: int): int {
        const p = qp.sectionOf(index)
        return p.section < 0 ? 0 : p.local % 2
    }

    function moveVertical(delta: int): bool {
        if (qp.count <= 0)
            return false
        const p = qp.sectionOf(qp.focusIndex)
        const n = qp.sectionCounts[p.section]
        const row = Math.floor(p.local / 2)
        const col = p.local % 2
        const rows = Math.ceil(n / 2)
        const targetRow = row + delta
        if (targetRow >= 0 && targetRow < rows) {
            qp.focusIndex = qp.sectionStart(p.section) + Math.min(n - 1, targetRow * 2 + col)
            return true
        }
        let s = p.section + delta
        while (s >= 0 && s < 3) {
            const m = qp.sectionCounts[s]
            if (m > 0) {
                const lastRow = Math.ceil(m / 2) - 1
                const local = delta > 0 ? Math.min(m - 1, col) : Math.min(m - 1, lastRow * 2 + col)
                qp.focusIndex = qp.sectionStart(s) + local
                return true
            }
            s += delta
        }
        return false
    }

    function moveHorizontal(delta: int): bool {
        if (qp.count <= 0) {
            if (delta > 0)
                qp.panBack()
            return true
        }
        const p = qp.sectionOf(qp.focusIndex)
        const n = qp.sectionCounts[p.section]
        const col = p.local % 2
        if (delta < 0) {
            if (col === 1)
                qp.focusIndex = qp.focusIndex - 1
            return true
        }
        if (col === 0 && p.local + 1 < n) {
            qp.focusIndex = qp.focusIndex + 1
            return true
        }
        qp.panBack()
        return true
    }

    function touchSetFocus(index: int): void {
        qp.touchFocusing = true
        qp.focusIndex = qp.clampIndex(index)
        qp.touchFocusing = false
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        if (kind !== "tile" || (!qp.active && !Ui.landscape))
            return false
        const entry = qp.entryAt(qp.focusIndex)
        if (entry === null || entry.phone)
            return false
        qp.pin(entry)
        return true
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        if (kind !== "tile")
            return ""
        if (!qp.active && !Ui.landscape)
            return "left"
        qp.touchFocusing = true
        qp.focusIndex = qp.clampIndex(index)
        if (!qp.active)
            qp.touchFront()
        qp.touchFocusing = false
        return "a"
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (kind === "tile")
            qp.ensureVisible(Motion.scroll)
    }

    function handleAction(name: string, repeat: bool): bool {
        switch (name) {
        case "up":
            qp.moveVertical(-1)
            return true
        case "down":
            qp.moveVertical(1)
            return true
        case "left":
            return qp.moveHorizontal(-1)
        case "right":
            return qp.moveHorizontal(1)
        case "lt":
            return qp.pageFocus(-1)
        case "rt":
            return qp.pageFocus(1)
        case "a": {
            if (repeat)
                return true
            const entry = qp.entryAt(qp.focusIndex)
            if (entry === null)
                return true
            if (entry.phone)
                qp.openPhone()
            else
                qp.play(entry)
            return true
        }
        case "x": {
            if (repeat)
                return true
            const entry = qp.entryAt(qp.focusIndex)
            if (entry !== null && !entry.phone)
                qp.pin(entry)
            return true
        }
        case "b":
        case "rb":
            if (!repeat)
                qp.panBack()
            return true
        case "lb":
            return true
        default:
            return false
        }
    }

    function pageFocus(direction: int): bool {
        if (qp.count <= 0)
            return false
        const rowsPerPage = Math.max(1, Math.floor(qp.height / qp.rowPitch))
        let moved = false
        for (let i = 0; i < rowsPerPage; i++) {
            if (!qp.moveVertical(direction))
                break
            moved = true
        }
        if (moved)
            qp.ensureVisible(Motion.page)
        return moved
    }

    function gridHeight(n: int): real {
        const rows = Math.ceil(n / 2)
        return rows <= 0 ? 0 : rows * qp.tilePx + (rows - 1) * qp.gutterPx
    }

    function sectionHeight(section: int): real {
        if (section === 0 && qp.pinsCount === 0)
            return Ui.px(Ui.qpHintDrop + Ui.qpHintPitch * (emptyText.lineCount - 1))
        return Ui.px(Ui.qpContentDrop) + qp.gridHeight(qp.sectionCounts[section])
    }

    function sectionTop(section: int): real {
        let top = Ui.px(Ui.qpFirstBaseline)
        for (let s = 0; s < section; s++)
            top += qp.sectionHeight(s) + Ui.px(Ui.qpSectionGap)
        return top
    }

    function headerTop(section: int): real {
        const header = section === 2 ? newHeader : section === 1 ? historyHeader : pinsHeader
        return qp.sectionTop(section) - header.baselineOffset
    }

    function tileTop(index: int): real {
        const p = qp.sectionOf(index)
        if (p.section < 0)
            return 0
        return qp.sectionTop(p.section) + Ui.px(Ui.qpContentDrop) + Math.floor(p.local / 2) * qp.rowPitch
    }

    function ensureVisible(duration: int): void {
        touchScroller.stop()
        if (qp.count <= 0 || qp.focusIndex < 0 || qp.focusIndex >= qp.count)
            return
        const p = qp.sectionOf(qp.focusIndex)
        let top = qp.tileTop(qp.focusIndex)
        const bottom = top + qp.tilePx
        if (p.local < 2)
            top = p.section === 0 || (qp.pinsCount === 0 && p.section === 1) ? 0 : qp.headerTop(p.section)
        const maxY = Math.max(0, content.height - flick.height)
        let y = scrollAnim.running ? scrollAnim.to : flick.contentY
        if (bottom > y + flick.height)
            y = bottom - flick.height
        if (top < y)
            y = top
        y = Math.max(0, Math.min(maxY, y))
        scrollAnim.stop()
        if (y === flick.contentY)
            return
        if (!qp.animated || duration <= 0) {
            flick.contentY = y
            return
        }
        scrollAnim.duration = duration
        scrollAnim.from = flick.contentY
        scrollAnim.to = y
        scrollAnim.start()
    }

    function settleScroll(): void {
        if (qp.focusIndex >= qp.count)
            qp.focusIndex = Math.max(0, qp.count - 1)
        qp.ensureVisible(0)
    }

    function scheduleSettle(): void {
        Qt.callLater(qp.settleScroll)
    }

    function keepFocusVisible(): void {
        qp.ensureVisible(0)
    }

    function completeAll() {
        if (scrollAnim.running)
            scrollAnim.complete()
    }

    onFocusIndexChanged: {
        if (!qp.touchFocusing)
            qp.ensureVisible(Motion.scroll)
    }
    onActiveChanged: {
        if (qp.touchFocusing)
            return
        if (qp.active)
            qp.ensureVisible(0)
        else
            Qt.callLater(function() { qp.ensureVisible(Motion.scroll) })
    }
    onCountChanged: {
        if (qp.focusIndex >= qp.count)
            qp.focusIndex = Math.max(0, qp.count - 1)
    }

    component RowWatch: Connections {
        ignoreUnknownSignals: true

        function onRowsInserted() {
            qp.scheduleSettle()
        }

        function onRowsMoved() {
            qp.scheduleSettle()
        }

        function onRowsRemoved() {
            qp.scheduleSettle()
        }

        function onModelReset() {
            qp.scheduleSettle()
        }
    }

    RowWatch {
        target: qp.pinsModel
    }

    RowWatch {
        target: qp.historyModel
    }

    RowWatch {
        target: qp.newModel
    }

    Flickable {
        id: flick

        readonly property bool touchScroll: true
        readonly property bool touchScrollEnabled: Ui.landscape || qp.active
        property alias touch: touchScroller

        function stopScroll(): void {
            scrollAnim.stop()
        }

        function clampFocusToView(): void {
            if (qp.count <= 0)
                return
            const top = flick.contentY
            const bottom = flick.contentY + flick.height
            const focusTop = qp.tileTop(qp.focusIndex)
            if (focusTop >= top && focusTop + qp.tilePx <= bottom)
                return
            const col = qp.column(qp.focusIndex)
            let best = -1
            let distance = Infinity
            for (let pass = 0; pass < 2 && best < 0; pass++) {
                for (let i = 0; i < qp.count; i++) {
                    const y = qp.tileTop(i)
                    if ((pass === 0 && qp.column(i) !== col) || y < top || y + qp.tilePx > bottom)
                        continue
                    if (Math.abs(y - focusTop) < distance) {
                        distance = Math.abs(y - focusTop)
                        best = i
                    }
                }
            }
            if (best >= 0)
                qp.touchSetFocus(best)
        }

        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        interactive: false
        onContentHeightChanged: qp.scheduleSettle()
        onHeightChanged: qp.scheduleSettle()
        boundsBehavior: Flickable.StopAtBounds

        TouchScroll {
            id: touchScroller

            flick: qp.flick
            onMoved: flick.clampFocusToView()
        }

        Item {
            id: content

            width: flick.width
            height: qp.sectionTop(2) + qp.sectionHeight(2) + Ui.px(Ui.hubEdge)

            Column {
                id: sections

                x: Ui.px(Ui.qpEdge)
                y: Ui.px(Ui.qpFirstBaseline)
                spacing: Ui.px(Ui.qpSectionGap)

                Item {
                    id: pinsSection

                    width: qp.gridW
                    height: qp.sectionHeight(0)

                    SectionHeader {
                        id: pinsHeader

                        y: -pinsHeader.baselineOffset
                        text: "Pins"
                    }

                    Text {
                        id: emptyText

                        visible: qp.pinsCount === 0
                        y: Ui.px(Ui.qpHintDrop) - emptyText.baselineOffset
                        width: Ui.px(Ui.qpHintW)
                        wrapMode: Text.WordWrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Ui.px(Ui.qpHintPitch)
                        text: "Press X on an item to pin it to Quickplay."
                        color: Ui.white(Ui.quickplayHint.alpha)
                        font: Ui.font(Ui.quickplayHint)
                    }

                    Grid {
                        id: pinsGrid

                        y: Ui.px(Ui.qpContentDrop)
                        columns: 2
                        columnSpacing: qp.gutterPx
                        rowSpacing: qp.gutterPx

                        PressScale {
                            id: phone

                            readonly property bool focused: qp.phoneTile && qp.active && qp.focusIndex === 0
                            readonly property string touchKind: "tile"
                            readonly property int touchIndex: 0
                            readonly property var entry: ({
                                section: "pins",
                                kind: "phone",
                                id: -1,
                                title: qp.phoneName,
                                subtitle: "",
                                artPath: "",
                                phone: true
                            })

                            visible: qp.phoneTile
                            width: qp.tilePx
                            height: qp.tilePx
                            pressed: phone.focused && qp.pressed
                            animated: qp.animated

                            Rectangle {
                                anchors.fill: parent
                                color: Ui.tile
                            }

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -Ui.px(Ui.focusBorder)
                                color: "transparent"
                                border.width: Ui.px(Ui.focusBorder)
                                border.color: Ui.textFull
                                visible: phone.focused
                            }

                            Text {
                                x: qp.padPx
                                y: qp.padPx
                                text: "phone"
                                color: Ui.white(Ui.sectionLabel.alpha)
                                font: Ui.font(Ui.sectionLabel)
                            }

                            Text {
                                x: qp.padPx
                                anchors.bottom: parent.bottom
                                anchors.bottomMargin: qp.padPx
                                width: parent.width - 2 * qp.padPx
                                wrapMode: Text.WordWrap
                                maximumLineCount: 3
                                clip: true
                                text: qp.phoneName
                                color: Ui.white(phone.focused ? Ui.listPlaylist.alpha : Ui.listPlaylist.alphaDim)
                                font: Ui.font(Ui.listPlaylist)
                            }
                        }

                        Repeater {
                            id: pinsRep

                            model: qp.pinsModel

                            delegate: QpTile {
                                offset: qp.phoneTile ? 1 : 0
                                sectionName: "pins"
                            }
                        }
                    }
                }

                Item {
                    id: historySection

                    width: qp.gridW
                    height: qp.sectionHeight(1)

                    SectionHeader {
                        id: historyHeader

                        y: -historyHeader.baselineOffset
                        text: "History"
                    }

                    Grid {
                        id: historyGrid

                        y: Ui.px(Ui.qpContentDrop)
                        columns: 2
                        columnSpacing: qp.gutterPx
                        rowSpacing: qp.gutterPx

                        Repeater {
                            id: historyRep

                            model: qp.historyModel

                            delegate: QpTile {
                                offset: qp.pinsCount
                                sectionName: "history"
                                defaultKind: "song"
                            }
                        }
                    }
                }

                Item {
                    id: newSection

                    width: qp.gridW
                    height: qp.sectionHeight(2)

                    SectionHeader {
                        id: newHeader

                        y: -newHeader.baselineOffset
                        text: "New"
                    }

                    Grid {
                        id: newGrid

                        y: Ui.px(Ui.qpContentDrop)
                        columns: 2
                        columnSpacing: qp.gutterPx
                        rowSpacing: qp.gutterPx

                        Repeater {
                            id: newRep

                            model: qp.newModel

                            delegate: QpTile {
                                offset: qp.pinsCount + qp.historyCount
                                sectionName: "new"
                                defaultKind: "album"
                            }
                        }
                    }
                }
            }
        }
    }

    NumberAnimation {
        id: scrollAnim

        target: flick
        property: "contentY"
        easing.type: Motion.easeIn
    }
}
