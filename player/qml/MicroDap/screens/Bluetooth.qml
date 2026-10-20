pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property QtObject bluez: null
    property QtObject settings: null
    property string btApplied: "auto"
    property var eq: null
    property QtObject dacEq: null
    property Item keyboard: null
    property bool animated: true
    property bool screenOff: false
    property bool overlay: false
    property int pivotIndex: 0
    property int shownPivot: 0
    property int pairingTimeout: 120
    property int searchTimeout: 120
    property var sheetRow: null
    property string shownState: ""
    property string shownCodec: ""
    property bool ready: false
    property bool companion: false

    readonly property bool live: page.animated && !page.screenOff
    readonly property var pivots: ["phone", "pairing", "headphones", "eq", "settings"]
    readonly property int eqIndex: 3
    readonly property int settingsIndex: 4
    readonly property string role: page.bluez !== null && page.bluez.activeRole !== undefined ? String(page.bluez.activeRole) : "receiver"
    readonly property bool sourceRole: page.role === "headphones"
    readonly property bool roleSwitching: page.bluez !== null && page.bluez.switching === true
    readonly property var codecRow: Env.settingsRows.find((r) => r.key === "codec") ?? null
    readonly property var codecs: page.codecRow === null ? [] : page.sourceRole ? page.codecRow.headphonesValues : page.codecRow.values
    readonly property string codec: page.settings === null ? "auto"
        : page.sourceRole ? (page.settings.btCodecHeadphones !== undefined ? String(page.settings.btCodecHeadphones) : "auto")
        : (page.settings.btCodec !== undefined ? String(page.settings.btCodec) : "auto")
    readonly property bool codecPending: page.sourceRole
        ? page.btApplied !== "auto" && page.codec !== page.btApplied && page.codec !== "sbc"
        : page.codec !== page.btApplied
    readonly property string codecValue: page.codecWord(page.codec) + (page.codecPending ? " (pending)" : "")
    readonly property bool codecOpen: d.codecOpen
    readonly property string eqSlot: page.sourceRole ? "bt"
        : page.eq !== null && page.eq.activeOutput !== undefined && String(page.eq.activeOutput) !== ""
        ? String(page.eq.activeOutput) : "speaker"
    readonly property bool searching: page.bluez !== null && page.bluez.searching === true
    readonly property int searchRemaining: page.bluez !== null && page.bluez.searchRemaining !== undefined ? page.bluez.searchRemaining : 0
    readonly property var headsets: page.bluez !== null && page.bluez.headsets !== undefined ? page.bluez.headsets : []
    readonly property var discovered: page.bluez !== null && page.bluez.discovered !== undefined ? page.bluez.discovered : []
    readonly property string headsetName: page.bluez !== null && page.bluez.headsetName !== undefined ? String(page.bluez.headsetName) : ""
    readonly property string searchLine: page.searching ? "searching " + page.searchRemaining : ""
    readonly property var hpRows: [{ kind: "search", path: "", name: "search", connected: false, state: "" }]
        .concat(page.headsets.map((h) => Object.assign({ kind: "headset" }, h)))
        .concat(page.discovered.map((h) => Object.assign({ kind: "found" }, h)))
    readonly property real searchSweep: page.searching && page.searchTimeout > 0
        ? 360 * Math.max(0, Math.min(page.searchRemaining, page.searchTimeout)) / page.searchTimeout : 0
    readonly property bool connected: page.bluez !== null && page.bluez.connected === true
    readonly property bool transportActive: page.connected && page.bluez.transportActive === true
    readonly property bool pairing: page.bluez !== null && page.bluez.pairing === true
        && String(page.bluez.pairingMode) !== "companion"
    readonly property int pairingRemaining: page.bluez !== null && page.bluez.pairingRemaining !== undefined ? page.bluez.pairingRemaining : 0
    readonly property int battery: page.bluez !== null && page.bluez.battery !== undefined ? page.bluez.battery : -1
    readonly property string deviceName: page.bluez !== null && page.bluez.deviceName !== undefined ? page.bluez.deviceName : ""
    readonly property string lastPhone: page.bluez !== null && page.bluez.lastPhone !== undefined ? String(page.bluez.lastPhone) : ""
    readonly property string shownPhone: page.connected ? page.deviceName : page.lastPhone
    readonly property bool guarded: page.connected || page.headsetName !== ""
    readonly property string status: page.bluez !== null && page.bluez.status !== undefined ? page.bluez.status : ""
    readonly property string trackTitle: page.bluez !== null && page.bluez.title !== undefined ? page.bluez.title : ""
    readonly property string trackArtist: page.bluez !== null && page.bluez.artist !== undefined ? page.bluez.artist : ""
    readonly property string stateLine: page.transportActive ? "audio active" : page.connected ? "connected" : "disconnected"
    readonly property string instructions: (page.pairing ? "pair from your phone, b to stop" : "press a to make this device discoverable")
        + "\n" + (page.companion ? "pair the phone app from settings > system" : "for the phone app, turn it on in settings > system")
    readonly property string pairingTitle: page.overlay ? "pair in dap mode" : "discoverable"
    readonly property real arcSweep: page.pairing && page.pairingTimeout > 0
        ? 360 * Math.max(0, Math.min(1, page.pairingRemaining / page.pairingTimeout))
        : 0
    readonly property real nameBaseline: Ui.contentTop + 16
    readonly property real stateBaseline: Ui.contentTop + 36
    readonly property real nameBlockTop: page.nameBaseline - Ui.capHeight * Ui.npArtist.size
    readonly property real nameBlockHeight: page.stateBaseline - page.nameBlockTop
    readonly property real rowTop: Ui.contentTop + 60
    readonly property real ringCenterY: pairingPage.ringCenterY
    readonly property real pulsePeak: pairingPage.pulsePeak
    readonly property real hintBaseline: pairingPage.hintBaseline
    readonly property bool switching: pivotAnim.running
    readonly property bool stateSwapping: stateSwap.running
    readonly property bool pulsing: pairingPage.pulsing

    property alias header: header
    property alias strip: strip
    property alias phonePage: phonePage
    property alias pairingPage: pairingPage
    property alias nameText: nameText
    property alias stateText: stateText
    property alias batteryGlyph: batteryGlyph
    property alias transportRow: transportRow
    property alias focusBar: focusBar
    property alias discoverableText: pairingPage.discoverableText
    property alias dapModeText: pairingPage.dapModeText
    property alias pairingRing: pairingPage.pairingRing
    property alias pulseHolder: pairingPage.pulseHolder
    property alias hintText: pairingPage.hintText
    property alias eqPage: eqPage
    property alias headphonesPage: headphonesPage
    property alias headphonesList: headphonesList
    property alias hpFocusBar: hpFocusBar
    property alias sheet: sheet
    property alias guardTimer: guardTimer
    property alias settingsPage: settingsPage
    property alias codecItem: codecItem
    property alias settingsFocusBar: settingsFocusBar

    signal openNowPlaying()
    signal toast(string first, string second)

    function call(method: string, arg: var): bool {
        if (page.bluez === null || typeof page.bluez[method] !== "function")
            return false
        if (arg === undefined)
            page.bluez[method]()
        else
            page.bluez[method](arg)
        return true
    }

    function togglePlayPause(): bool {
        return page.call(page.status === "playing" ? "pause" : "play")
    }

    function contentAt(index: int): Item {
        return [phonePage, pairingPage, headphonesPage, eqPage, settingsPage][index]
    }

    function codecWord(value: string): string {
        return value.replace("_", " ")
    }

    function openCodec(): void {
        if (page.settings === null || page.codecs.length === 0)
            return
        const mark = page.codecs.indexOf(page.codec)
        page.sheetRow = null
        d.codecOpen = true
        d.riseKept = ""
        sheet.title = "codec"
        sheet.items = page.codecs.map((v) => page.codecWord(v))
        sheet.open()
        sheet.mark = mark
        sheet.wrap = !page.codecRow.bounded
        sheet.focusIndex = Math.max(0, mark)
    }

    function closeCodec(): void {
        if (!d.codecOpen)
            return
        d.codecOpen = false
        d.riseKept = ""
        if (sheet.shown)
            sheet.close()
    }

    function acceptCodec(index: int): void {
        const value = page.codecs[index]
        const headphones = page.sourceRole
        page.closeCodec()
        if (value === undefined || page.settings === null)
            return
        if (headphones)
            page.settings.btCodecHeadphones = value
        else
            page.settings.btCodec = value
    }

    function revertOrConfirmForRotation(): void {
        page.closeCodec()
    }

    function holdRise(name: string, repeat: bool): bool {
        if (repeat)
            return true
        if (name === "b" || name === "back" || name === "start")
            d.riseKept = name
        else if ((name === "up" || name === "down") && d.riseKept === "")
            d.riseKept = name
        return true
    }

    function resetEq(): void {
        eqPage.completeAll()
        eqPage.confirmEdit()
        eqPage.headphonesPane.reset()
        eqPage.focusIndex = 0
    }

    function landPivot() {
        pivotAnim.stop()
        for (const item of [phonePage, pairingPage, headphonesPage, eqPage, settingsPage]) {
            item.x = 0
            item.opacity = 1
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
        page.closeCodec()
        page.resetEq()
        d.internal = true
        page.pivotIndex = target
        d.internal = false
        if (!page.live) {
            page.shownPivot = target
            return true
        }
        pivotAnim.outgoing = page.contentAt(page.shownPivot)
        pivotAnim.incoming = page.contentAt(target)
        pivotAnim.dir = dir
        pivotAnim.start()
        return true
    }

    function completeAll() {
        page.landPivot()
        eqPage.completeAll()
        if (stateSwap.running)
            stateSwap.stop()
        stateText.opacity = 1
        page.shownState = page.stateLine
        if (codecSwap.running)
            codecSwap.stop()
        codecValueText.opacity = 1
        page.shownCodec = page.codecValue
        pairingPage.complete()
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        if (page.searching)
            page.call("stopSearch")
        if (page.pairing)
            page.call("stopPairing")
        page.stack.pop()
        return true
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        switch (kind) {
        case "row":
        case "ring":
        case "setrow":
            return "a"
        case "hprow":
            headphonesList.touchFocus(index)
            return "a"
        case "pivot":
            return "tap"
        case "header":
            guardTimer.restart()
            return ""
        default:
            return ""
        }
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        if (kind !== "hprow")
            return false
        headphonesList.touchFocus(index)
        return page.openHeadsetSheet()
    }

    function clampHeadphones(): void {
        if (headphonesList.focusIndex >= page.hpRows.length)
            headphonesList.snapTo(page.hpRows.length - 1)
    }

    function hpRow(): var {
        return page.hpRows[headphonesList.focusIndex] === undefined ? null : page.hpRows[headphonesList.focusIndex]
    }

    function activateHeadphones(): void {
        const row = page.hpRow()
        if (row === null)
            return
        if (row.kind === "search") {
            if (!page.searching)
                page.call("startSearch", page.searchTimeout)
        } else if (row.kind === "headset") {
            page.call(row.connected ? "disconnectHeadset" : "connectHeadset", row.path)
        } else if (row.state !== "pairing") {
            page.call("pairDevice", row.path)
        }
    }

    function openHeadsetSheet(): bool {
        const row = page.hpRow()
        if (row === null || row.kind !== "headset" || page.roleSwitching)
            return false
        page.sheetRow = row
        sheet.title = row.name
        sheet.items = [row.connected ? "disconnect" : "connect", "forget"]
        sheet.open()
        return true
    }

    function runSheet(index: int): void {
        const row = page.sheetRow
        page.sheetRow = null
        if (row === null)
            return
        if (index === 0)
            page.call(row.connected ? "disconnectHeadset" : "connectHeadset", row.path)
        else if (index === 1)
            page.call("forgetHeadset", row.path)
    }

    function headphonesAction(name: string, repeat: bool): bool {
        switch (name) {
        case "up":
            headphonesList.moveFocus(-1)
            return true
        case "down":
            headphonesList.moveFocus(1)
            return true
        case "a":
            if (!repeat && !page.roleSwitching)
                page.activateHeadphones()
            return true
        case "x":
            if (!repeat)
                page.openHeadsetSheet()
            return true
        case "b":
            if (repeat)
                return false
            if (page.searching) {
                page.call("stopSearch")
                return true
            }
            return page.guardedBack()
        default:
            return false
        }
    }

    function touchTap(kind: string, index: int, hit: Item) {
        if (kind === "pivot" && index !== page.pivotIndex)
            page.switchPivot(index - page.pivotIndex)
    }

    function eqAction(name: string, repeat: bool): bool {
        const busy = eqPage.editing || eqPage.sheet.shown || eqPage.deleteDialog.shown
        if (!busy && (name === "lb" || name === "rb"))
            return false
        if (!busy && name === "start") {
            if (page.connected)
                page.togglePlayPause()
            return true
        }
        if (eqPage.handleAction(name, repeat))
            return true
        return name === "b" && !repeat ? page.guardedBack() : false
    }

    function guardedBack(): bool {
        if (!page.guarded)
            return page.back()
        page.toast("bluetooth", "hold b 2 s to leave")
        return true
    }

    function leave(): void {
        guardTimer.stop()
        page.back()
    }

    function press(name: string) {
        if (name === "a")
            page.pressed = true
        if (name === "b")
            guardTimer.restart()
    }

    function release(name: string) {
        if (name === "a")
            page.pressed = false
        if (name === "b")
            guardTimer.stop()
    }

    function touchMove(kind: string, index: int, dx: real, dy: real, hit: Item): void {
    }

    function touchLift(kind: string, index: int, dx: real, dy: real, hit: Item): void {
        if (kind !== "header" || !guardTimer.running)
            return
        guardTimer.stop()
        page.handleAction("b", false)
    }

    function touchCancel(kind: string, index: int, hit: Item): void {
        guardTimer.stop()
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown && d.codecOpen && sheet.animating)
            return page.holdRise(name, repeat)
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        if (page.roleSwitching && (name === "a" || name === "x" || name === "start"))
            return true
        if (page.pivotIndex === page.eqIndex && page.eqAction(name, repeat))
            return true
        if (page.pivotIndex === page.settingsIndex && name === "a") {
            if (!repeat)
                page.openCodec()
            return true
        }
        if (page.pivotIndex === 2 && page.headphonesAction(name, repeat))
            return true
        switch (name) {
        case "lb":
            if (!repeat)
                page.switchPivot(-1)
            return true
        case "rb":
            if (!repeat)
                page.switchPivot(1)
            return true
        case "a":
            if (repeat)
                return true
            if (page.pivotIndex === 1) {
                if (!page.pairing && !page.overlay)
                    page.call("startPairing", page.pairingTimeout)
                return true
            }
            if (page.transportActive) {
                page.openNowPlaying()
                return true
            }
            if (page.connected) {
                page.togglePlayPause()
            } else if (page.lastPhone !== "") {
                page.toast("", "connect from your phone")
                if (page.sourceRole)
                    page.call("setRole", "receiver")
            }
            return true
        case "start":
            if (repeat)
                return true
            if (page.pivotIndex === 0 && page.connected) {
                page.togglePlayPause()
                return true
            }
            return false
        case "b":
            if (repeat)
                return false
            if (page.pivotIndex === 1 && page.pairing) {
                page.call("stopPairing")
                return true
            }
            return page.guardedBack()
        default:
            return false
        }
    }

    onPivotIndexChanged: {
        if (!d.internal)
            page.landPivot()
    }

    onStateLineChanged: {
        if (page.ready && page.live)
            stateSwap.restart()
        else
            page.shownState = page.stateLine
    }

    onPairingChanged: {
        if (page.ready && !page.pairing && page.pivotIndex === 1 && page.connected)
            page.switchPivot(-1)
    }

    onEqSlotChanged: {
        eqPage.cancelEdit()
        page.resetEq()
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    function holdScope(held: bool): void {
        if (held === d.scopeHeld)
            return
        d.scopeHeld = held
        page.call("setReceiverScope", held)
    }

    onSourceRoleChanged: {
        page.closeCodec()
        page.holdScope(!page.sourceRole)
    }

    onCodecChanged: page.closeCodec()

    onCodecValueChanged: {
        if (page.ready && page.live)
            codecSwap.restart()
        else
            page.shownCodec = page.codecValue
    }

    onScreenOffChanged: {
        if (page.screenOff)
            page.closeCodec()
    }

    onActiveChanged: {
        if (!page.active)
            page.closeCodec()
    }

    Component.onDestruction: page.holdScope(false)

    Component.onCompleted: {
        page.call("want")
        page.holdScope(!page.sourceRole)
        if (page.sourceRole && page.pivotIndex === 0)
            page.pivotIndex = 2
        page.resetEq()
        page.shownPivot = page.pivotIndex
        page.shownState = page.stateLine
        page.shownCodec = page.codecValue
        page.ready = true
    }

    QtObject {
        id: d

        property bool internal: false
        property bool scopeHeld: false
        property bool codecOpen: false
        property string riseKept: ""
    }

    Timer {
        id: guardTimer

        interval: Ui.modeGuardHoldMs
        repeat: false
        onTriggered: page.leave()
    }

    PageSurface {
        anchors.fill: parent
    }

    Item {
        id: phonePage

        width: page.width
        height: page.height
        visible: page.shownPivot === 0

        Text {
            id: nameText

            x: Ui.px(Ui.hubEdge)
            y: Ui.px(page.nameBaseline) - nameText.baselineOffset
            width: page.width - 2 * Ui.px(Ui.hubEdge)
            text: page.shownPhone
            color: Ui.white(Ui.npArtist.alpha)
            font: Ui.font(Ui.npArtist)
            clip: true
        }

        Text {
            id: stateText

            x: Ui.px(Ui.hubEdge)
            y: Ui.px(page.stateBaseline) - stateText.baselineOffset
            text: page.shownState
            color: Ui.white(Ui.listCount.alpha)
            font: Ui.font(Ui.listCount)
        }

        Glyph {
            id: batteryGlyph

            visible: page.battery >= 0
            x: stateText.x + stateText.width + Ui.px(Ui.statusGap)
            anchors.verticalCenter: stateText.verticalCenter
            name: "battery"
            size: Ui.statusHeight
            level: page.battery / 100
            color: page.battery < Ui.batteryLowPercent ? Ui.accent : Ui.textFull
        }

        Item {
            id: nameBlock

            readonly property string touchKind: "row"

            x: Ui.px(Ui.hubEdge - Ui.focusBarOffset)
            y: Ui.px(page.nameBlockTop)
            width: page.width - Ui.px(Ui.hubEdge) - nameBlock.x
            height: Ui.px(page.nameBlockHeight)
        }

        FocusBar {
            id: focusBar

            visible: page.shownPhone !== ""
            rowY: page.transportActive ? transportRow.y : Ui.px(page.nameBlockTop)
            rowHeight: page.transportActive ? transportRow.height : Ui.px(page.nameBlockHeight)
            opacity: page.focusOpacity
            animated: page.live
        }

        ListRow {
            id: transportRow

            readonly property string touchKind: "row"

            visible: page.transportActive
            y: Ui.px(page.rowTop)
            width: page.width
            ring: true
            ringGlyph: "play"
            primary: page.trackTitle
            secondary: page.trackArtist
            focused: page.pivotIndex === 0
            pressed: page.pressed && page.pivotIndex === 0 && page.transportActive
            animated: page.live
        }
    }

    PairingPanel {
        id: pairingPage

        width: page.width
        height: page.height
        visible: page.shownPivot === 1
        overlay: page.overlay
        live: page.live
        arcSweep: page.arcSweep
        pressed: page.pressed && page.pivotIndex === 1
        hint: page.instructions
    }

    Item {
        id: headphonesPage

        width: page.width
        height: page.height
        visible: page.shownPivot === 2

        FocusList {
            id: headphonesList

            x: 0
            y: Ui.px(Ui.contentTop)
            width: page.width
            height: page.height - headphonesList.y
            model: page.hpRows.length
            delegate: Item {
                id: hpItem

                required property int index
                readonly property var modelData: page.hpRows[hpItem.index] === undefined ? ({}) : page.hpRows[hpItem.index]
                readonly property string touchKind: "hprow"
                readonly property int touchIndex: hpItem.index
                readonly property Item rowItem: hpListRow

                width: headphonesList.width
                height: headphonesList.rowPitch

                ListRow {
                    id: hpListRow

                    width: parent.width
                    ring: true
                    ringGlyph: "bluetooth"
                    ringArc: hpItem.modelData.kind === "search" ? page.searchSweep : 0
                    ringToggle: hpItem.modelData.kind !== "search"
                    ringOn: hpItem.modelData.connected === true
                    bold: true
                    primary: hpItem.modelData.name === undefined ? "" : hpItem.modelData.name
                    secondary: hpItem.modelData.kind === "search" ? page.searchLine
                        : hpItem.modelData.state === undefined ? "" : hpItem.modelData.state
                    focused: page.pivotIndex === 2 && headphonesList.focusIndex === hpItem.index
                    pressed: page.pressed && page.pivotIndex === 2 && headphonesList.focusIndex === hpItem.index
                    animated: page.live
                }
            }
        }

        FocusBar {
            id: hpFocusBar

            x: Ui.px(Ui.hubEdge - Ui.focusBarOffset)
            rowY: headphonesList.y + headphonesList.focusY
            rowHeight: headphonesList.rowPitch
            opacity: page.focusOpacity
            animated: page.live
        }
    }

    EqScreen {
        id: eqPage

        width: page.width
        height: page.height
        visible: page.shownPivot === page.eqIndex
        embedded: true
        slot: page.eqSlot
        eq: page.eq
        dacEq: page.dacEq
        keyboard: page.keyboard
        animated: page.animated
        screenOff: page.screenOff
        onToast: (first, second) => page.toast(first, second)
    }

    Item {
        id: settingsPage

        width: page.width
        height: page.height
        visible: page.shownPivot === page.settingsIndex

        Item {
            id: codecItem

            readonly property string touchKind: "setrow"
            readonly property Item titleItem: codecTitle
            readonly property Item valueItem: codecValueText

            y: Ui.px(Ui.contentTop)
            width: page.width
            height: Ui.px(Ui.settingsPitch)

            Text {
                id: codecTitle

                x: Ui.px(Ui.hubEdge)
                y: (codecItem.height - codecTitle.height) / 2
                width: codecValueText.x - Ui.px(Ui.gutter) - codecTitle.x
                text: "codec"
                color: Ui.white(Ui.settingsItem.alpha)
                font: Ui.font(Ui.settingsItem)
                clip: true
            }

            Text {
                id: codecValueText

                x: codecItem.width - Ui.px(Ui.hubEdge) - codecValueText.width
                anchors.baseline: codecTitle.baseline
                text: page.shownCodec
                color: Ui.white(Ui.settingsValueFocused)
                font: Ui.font(Ui.settingsValue)
            }
        }

        FocusBar {
            id: settingsFocusBar

            rowY: codecItem.y
            rowHeight: codecItem.height
            opacity: page.focusOpacity
            animated: page.live
        }
    }

    CroppedHeader {
        id: header

        trackTouch: true
        text: "bluetooth"
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
        screenOff: page.screenOff
        animated: page.live
        onAccepted: (index) => {
            if (d.codecOpen) {
                page.acceptCodec(index)
                return
            }
            page.runSheet(index)
            sheet.close()
        }
        onRejected: {
            d.codecOpen = false
            d.riseKept = ""
            page.sheetRow = null
        }
        onAnimatingChanged: {
            if (sheet.animating || d.riseKept === "")
                return
            const kept = d.riseKept
            d.riseKept = ""
            if (sheet.shown && d.codecOpen)
                sheet.handleAction(kept, false)
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
                pivotAnim.incoming.x = pivotAnim.dir * Ui.px(Motion.pivotShift)
                pivotAnim.incoming.opacity = 0
                page.shownPivot = page.pivotIndex
                pivotAnim.outgoing.x = 0
                pivotAnim.outgoing.opacity = 1
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: pivotAnim.incoming
                property: "x"
                to: 0
                duration: Motion.pivotRows
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
            NumberAnimation {
                target: pivotAnim.incoming
                property: "opacity"
                to: 1
                duration: Motion.pivotRows
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.strip
            }
        }
    }

    SequentialAnimation {
        id: codecSwap

        NumberAnimation {
            target: codecValueText
            property: "opacity"
            to: Motion.valueDip
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
        ScriptAction {
            script: page.shownCodec = page.codecValue
        }
        NumberAnimation {
            target: codecValueText
            property: "opacity"
            to: 1
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
    }

    SequentialAnimation {
        id: stateSwap

        NumberAnimation {
            target: stateText
            property: "opacity"
            from: 1
            to: Motion.valueDip
            duration: Motion.valueChange
        }
        ScriptAction {
            script: page.shownState = page.stateLine
        }
        NumberAnimation {
            target: stateText
            property: "opacity"
            from: Motion.valueDip
            to: 1
            duration: Motion.valueChange
        }
    }

    Connections {
        target: page.bluez
        ignoreUnknownSignals: true

        function onConnectedChanged() {
            if (page.ready && page.live && page.connected)
                pairingPage.pulseNow()
        }

        function onHeadsetsChanged() {
            Qt.callLater(page.clampHeadphones)
        }

        function onDiscoveredChanged() {
            Qt.callLater(page.clampHeadphones)
        }
    }
}
