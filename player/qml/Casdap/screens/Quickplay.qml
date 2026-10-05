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

    width: Ui.px(Ui.qpW)

    component QpTile: Tile {
        id: t

        required property int index
        required property var model
        property int offset: 0
        property string sectionName: ""
        readonly property int flat: t.offset + t.index
        readonly property var entry: ({
            section: t.sectionName,
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

    function layout() {
        sections.forceLayout()
        pinsGrid.forceLayout()
        historyGrid.forceLayout()
        newGrid.forceLayout()
    }

    function ensureVisible(duration: int) {
        const item = qp.tileAt(qp.focusIndex)
        if (item === null)
            return
        qp.layout()
        const top = item.mapToItem(content, 0, 0).y
        const bottom = top + item.height
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        let y = scrollAnim.running ? scrollAnim.to : flick.contentY
        if (top < y)
            y = top
        else if (bottom > y + flick.height)
            y = bottom - flick.height
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

    function completeAll() {
        if (scrollAnim.running)
            scrollAnim.complete()
    }

    onFocusIndexChanged: qp.ensureVisible(Motion.scroll)
    onCountChanged: {
        if (qp.focusIndex >= qp.count)
            qp.focusIndex = Math.max(0, qp.count - 1)
    }

    Flickable {
        id: flick

        anchors.fill: parent
        contentWidth: width
        contentHeight: content.height
        interactive: false
        boundsBehavior: Flickable.StopAtBounds

        Item {
            id: content

            width: flick.width
            height: sections.y + sections.height + Ui.px(Ui.hubEdge)

            Column {
                id: sections

                x: Ui.px(Ui.qpEdge)
                y: Ui.px(Ui.qpFirstBaseline)
                spacing: Ui.px(Ui.qpSectionGap)

                Item {
                    id: pinsSection

                    width: qp.gridW
                    height: qp.pinsCount === 0
                        ? Ui.px(Ui.qpHintDrop + Ui.qpHintPitch * (emptyText.lineCount - 1))
                        : Ui.px(Ui.qpContentDrop) + pinsGrid.height

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

                            readonly property bool focused: qp.active && qp.focusIndex === 0
                            readonly property var entry: ({
                                section: "pins",
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
                                elide: Text.ElideRight
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
                    height: Ui.px(Ui.qpContentDrop) + historyGrid.height

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
                            }
                        }
                    }
                }

                Item {
                    id: newSection

                    width: qp.gridW
                    height: Ui.px(Ui.qpContentDrop) + newGrid.height

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
