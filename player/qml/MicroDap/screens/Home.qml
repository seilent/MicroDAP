pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: home

    property QtObject bluez: null
    property var pinsModel: null
    property var historyModel: null
    property var newModel: null
    property bool animated: true
    property bool overlay: false
    property bool quickplayShown: false
    property bool quickplayOn: true
    property real pan: 0
    property real peekEnter: 1
    property real barEnter: 1
    property real peekExit: 0
    property real barExit: 0
    property bool exited: false
    property int playlistCount: -1
    property bool hideArmed: false

    readonly property string pageMotion: "home"
    readonly property int exitSpan: Math.max(home.focusIndex, home.menu.length - 1 - home.focusIndex)
    readonly property int exitDuration: Motion.homeOut + Motion.homeOutStagger * home.exitSpan
    readonly property real peekExitCurve: Motion.expOut6(home.peekExit)
    readonly property real barExitCurve: Motion.expOut6(home.barExit)
    readonly property bool exiting: peekExitAnim.running

    readonly property var menu: [
        { name: "music", subtitle: "artists albums songs genres playlists" },
        { name: "bluetooth", subtitle: "not connected" },
        { name: "playlists", subtitle: "now playing saved" },
        { name: "history", subtitle: "recent new" },
        { name: "settings", subtitle: "output library display power" }
    ]
    readonly property bool phoneConnected: home.bluez !== null && home.bluez.connected === true
    readonly property bool phonePairing: home.bluez !== null && home.bluez.pairing === true
        && String(home.bluez.pairingMode) !== "companion"
    readonly property bool sourceRole: home.bluez !== null && home.bluez.activeRole === "headphones"
    readonly property string bluetoothSubtitle: home.sourceRole
        ? (String(home.bluez.headsetName || "") !== "" ? String(home.bluez.headsetName)
           : home.bluez.searching === true ? "searching " + home.bluez.searchRemaining
           : home.menu[1].subtitle)
        : home.phoneConnected ? home.bluez.deviceName
        : home.phonePairing ? "pairing " + home.bluez.pairingRemaining
        : home.menu[1].subtitle
    readonly property string playlistsSubtitle: home.playlistCount < 0 ? home.menu[2].subtitle
        : home.playlistCount === 1 ? "1 playlist"
        : home.playlistCount + " playlists"

    function subtitleFor(name: string, fallback: string): string {
        if (name === "bluetooth")
            return home.bluetoothSubtitle
        if (name === "playlists")
            return home.playlistsSubtitle
        return fallback
    }
    readonly property real pitch: Ui.landscape ? Ui.homePitchLandscape : Ui.homePitch
    readonly property real menuX: Ui.landscape ? Ui.px(Ui.homeMenuXLandscape) : Ui.px(Ui.canvasW - Motion.quickplayMenuInset) * home.pan
    readonly property real panVisual: Ui.landscape ? (home.quickplayShown ? 1 : 0) : home.pan
    readonly property real quickplayScale: Ui.landscape ? 1 : Ui.qpPeekScale + (1 - Ui.qpPeekScale) * home.pan
    readonly property real quickplayY: Ui.landscape ? 0 : Ui.px(Ui.qpPeekY) * (1 - home.pan)
    readonly property real quickplayRestAlpha: Ui.landscape ? Motion.quickplayAlpha : Ui.qpPeekAlpha
    readonly property real quickplayAlpha: (home.quickplayRestAlpha + (1 - home.quickplayRestAlpha) * home.panVisual) * home.peekEnter * (1 - home.peekExitCurve)
    readonly property real menuAlpha: 1 + (Motion.quickplayAlpha - 1) * home.panVisual
    readonly property bool panning: panAnim.running
    property alias menuPanel: menuPanel
    property alias quickplayPanel: quickplayPanel
    property alias quickplay: quickplay
    property alias focusBar: focusBar
    property alias playRing: playRing
    property alias playRingHit: playRingHit
    property alias sheet: sheet
    property alias watermark: watermark
    property alias menuRepeater: menuRepeater

    property var sheetEntry: null

    signal navigate(string name)
    signal nowPlayingBluetooth()
    signal playRequested(var entry)
    signal pinRequested(var entry, bool pin)
    signal shuffleAll()
    signal hideRequested()

    focusCount: home.menu.length

    function press(name: string) {
        if (name === "a")
            home.pressed = true
        if (name === "b")
            home.hideArmed = home.overlay && !home.quickplayShown && !sheet.shown
    }

    function release(name: string) {
        if (name === "a")
            home.pressed = false
        if (name !== "b")
            return
        const armed = home.hideArmed
        home.hideArmed = false
        if (armed && home.overlay && !home.quickplayShown && !sheet.shown)
            home.hideRequested()
    }

    function itemBaseline(index: int): real {
        return Ui.px(Ui.homeFirstBaseline + index * home.pitch)
    }

    function menuItem(index: int): Item {
        return menuRepeater.itemAt(index)
    }

    function showQuickplay() {
        if (home.quickplayShown || !home.quickplayOn)
            return
        home.quickplayShown = true
        home.animatePan(1)
    }

    function showMenu() {
        if (!home.quickplayShown)
            return
        home.quickplayShown = false
        home.animatePan(0)
    }

    function animatePan(target: real) {
        panAnim.stop()
        if (Ui.landscape) {
            home.pan = 0
            return
        }
        if (!home.animated) {
            home.pan = target
            return
        }
        panAnim.from = home.pan
        panAnim.to = target
        panAnim.start()
    }

    function keepFocusVisible(): void {
        quickplay.keepFocusVisible()
    }

    function completeAll() {
        if (panAnim.running)
            panAnim.complete()
        if (peekAnim.running)
            peekAnim.complete()
        if (peekExitAnim.running)
            peekExitAnim.complete()
        for (let i = 0; i < menuRepeater.count; i++) {
            const item = menuRepeater.itemAt(i)
            if (item !== null)
                item.completeEnter()
        }
        home.peekEnter = 1
        home.barEnter = 1
        quickplay.completeAll()
        sheet.complete()
    }

    function runExit() {
        if (!home.animated)
            return
        home.exited = true
        peekExitAnim.stop()
        home.peekExit = 0
        peekExitAnim.start()
        for (let i = 0; i < menuRepeater.count; i++) {
            const item = menuRepeater.itemAt(i)
            if (item !== null)
                item.startExit(Math.abs(i - home.focusIndex) * Motion.homeOutStagger)
        }
    }

    function replayEntrance() {
        home.exited = false
        peekExitAnim.stop()
        home.peekExit = 0
        home.barExit = 0
        for (let i = 0; i < menuRepeater.count; i++) {
            const item = menuRepeater.itemAt(i)
            if (item !== null)
                item.startEnter(0)
        }
        if (!home.animated) {
            home.peekEnter = 1
            return
        }
        peekAnim.stop()
        home.peekEnter = 0
        peekAnim.start()
    }

    function openPinSheet(entry) {
        home.sheetEntry = entry
        sheet.title = entry.title
        sheet.items = entry.section === "pins" ? ["unpin"] : ["pin to quickplay"]
        sheet.open()
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        switch (kind) {
        case "menu":
            home.showMenu()
            home.setFocus(index)
            return "a"
        case "peek":
            return !Ui.landscape && !home.quickplayShown && home.quickplayOn ? "left" : ""
        case "ring":
            playRing.pressed = true
            return ""
        case "menu-peek": {
            if (Ui.landscape || !home.quickplayShown)
                return ""
            const p = hit.mapToItem(home, x, hit.height / 2)
            return p.x < quickplayPanel.x + quickplayPanel.width * quickplayPanel.scale ? "" : "b"
        }
        default:
            return ""
        }
    }

    function touchMove(kind: string, index: int, dx: real, dy: real, hit: Item): void {
        const slop = Ui.px(Ui.touchSlop)
        if (dx * dx + dy * dy > slop * slop)
            playRing.pressed = false
    }

    function touchLift(kind: string, index: int, dx: real, dy: real, hit: Item): void {
        const slop = Ui.px(Ui.touchSlop)
        if (!playRing.pressed || dx * dx + dy * dy > slop * slop) {
            playRing.pressed = false
            return
        }
        ringReturn.restart()
        home.shuffleAll()
    }

    function touchCancel(kind: string, index: int, hit: Item): void {
        ringReturn.stop()
        playRing.pressed = false
    }

    function handleAction(name: string, repeat: bool): bool {
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        if (home.quickplayShown) {
            if (quickplay.handleAction(name, repeat))
                return true
            if (name === "y") {
                if (!repeat)
                    home.shuffleAll()
                return true
            }
            if (name === "back")
                return true
            return false
        }
        switch (name) {
        case "up":
            return home.moveFocus(-1)
        case "down":
            return home.moveFocus(1)
        case "lt":
            home.setFocus(0)
            return true
        case "rt":
            home.setFocus(home.focusCount - 1)
            return true
        case "a":
            if (!repeat)
                home.activated(home.focusIndex)
            return true
        case "left":
        case "back":
        case "lb":
            if (!repeat)
                home.showQuickplay()
            return true
        case "y":
            if (!repeat)
                home.shuffleAll()
            return true
        case "b":
        case "right":
        case "rb":
            return true
        default:
            return false
        }
    }

    onActivated: (index) => home.navigate(home.menu[index].name)

    onQuickplayOnChanged: {
        if (!home.quickplayOn)
            home.showMenu()
    }

    onLeaving: home.runExit()

    onEntered: (first, kind) => {
        if (kind === "home" || home.exited)
            home.replayEntrance()
    }

    onAnimatedChanged: {
        if (!home.animated)
            home.completeAll()
    }

    Component.onCompleted: {
        if (!home.animated)
            return
        home.peekEnter = 0
        peekAnim.start()
    }

    Item {
        id: quickplayPanel

        readonly property string touchKind: Ui.landscape || !home.quickplayOn ? "" : "peek"

        x: 0
        y: home.quickplayY
        width: Ui.px(Ui.qpW)
        height: home.height
        visible: home.quickplayOn
        opacity: home.quickplayAlpha
        transformOrigin: Item.TopLeft
        scale: home.quickplayScale

        Quickplay {
            id: quickplay

            anchors.fill: parent
            pinsModel: home.pinsModel
            historyModel: home.historyModel
            newModel: home.newModel
            bluez: home.bluez
            animated: home.animated
            active: home.quickplayShown
            pressed: home.pressed && home.quickplayShown
            onPanBack: home.showMenu()
            onTouchFront: home.showQuickplay()
            onOpenPhone: home.nowPlayingBluetooth()
            onPlay: (entry) => home.playRequested(entry)
            onPin: (entry) => home.openPinSheet(entry)
        }
    }

    Turnstile {
        id: menuPanel

        readonly property string touchKind: !Ui.landscape && home.quickplayShown ? "menu-peek" : ""

        x: home.menuX
        y: Ui.px(Motion.quickplayMenuY) * home.pan
        width: home.width
        height: home.height
        hinge: menuPanel.width * menuPanel.scale
        angle: Motion.quickplayAngle * home.pan
        transformOrigin: Item.TopLeft
        scale: 1 + (Motion.quickplayScale - 1) * home.pan
        opacity: home.menuAlpha

        FocusBar {
            id: focusBar

            readonly property real musicY: home.itemBaseline(0) - Ui.px(Ui.homeBarTop)

            x: Ui.px(Ui.homeBarX)
            rowY: home.itemBaseline(home.focusIndex) - Ui.px(Ui.homeBarTop)
            rowHeight: Ui.px(Ui.homeBarH)
            animated: home.animated
            opacity: home.barEnter * (1 - home.barExitCurve) * home.focusOpacity
                * Math.min(1, Math.abs(focusBar.y - focusBar.musicY) / Ui.px(home.pitch))
        }

        Ring {
            id: playRing

            x: Ui.px(Ui.homeRingX - Ui.homeRingSize / 2)
            y: Ui.px(Ui.homeRingY - Ui.homeRingSize / 2)
            size: Ui.homeRingSize
            glyph: "play"
            visible: Ui.landscape || !home.quickplayShown
            opacity: home.peekEnter * (1 - home.peekExitCurve)
            animated: home.animated
        }

        Item {
            id: playRingHit

            readonly property string touchKind: playRing.visible ? "ring" : ""
            readonly property bool touchTrack: true

            x: 0
            y: 0
            width: Ui.px(Math.min(Ui.homeBarX, Ui.homeRingX + Ui.homeRingSize / 2))
            height: Ui.px(Ui.homeRingHitBottom)
        }

        Repeater {
            id: menuRepeater

            model: home.menu

            delegate: PressScale {
                id: item

                required property int index
                required property var modelData
                readonly property bool isFocused: item.index === home.focusIndex
                readonly property string touchKind: Ui.landscape || !home.quickplayShown ? "menu" : ""
                readonly property int touchIndex: item.index
                readonly property real itemBaseline: home ? home.itemBaseline(item.index) : 0
                readonly property string subtitle: home.subtitleFor(item.modelData.name, item.modelData.subtitle)
                readonly property real labelBaseline: item.y + label.y + label.baselineOffset
                readonly property real subtitleBaseline: item.y + sub.y + sub.baselineOffset
                readonly property Item labelItem: label
                readonly property Item subtitleItem: sub
                property real enter: 1
                property real exit: 0
                property int enterStagger: item.index * Motion.homeEnterStagger
                property int exitStagger: 0
                readonly property real exitCurve: Motion.expOut6(item.exit)
                readonly property real restY: item.itemBaseline - label.baselineOffset

                function completeEnter() {
                    enterAnim.complete()
                    exitAnim.complete()
                    item.enter = 1
                }

                function startEnter(stagger: int): void {
                    enterAnim.stop()
                    exitAnim.stop()
                    item.exit = 0
                    if (!home.animated) {
                        item.enter = 1
                        return
                    }
                    item.enterStagger = stagger
                    item.enter = 0
                    enterAnim.start()
                }

                function startExit(stagger: int): void {
                    exitAnim.stop()
                    item.exitStagger = stagger
                    item.exit = 0
                    exitAnim.start()
                }

                x: Ui.px(Ui.homeTextX) + Ui.px(Motion.homeEnterShift) * (1 - item.enter)
                y: item.restY + item.exitCurve * Motion.homeOutShift * home.height
                width: Math.max(label.width, sub.x + sub.width)
                height: sub.visible ? sub.y + sub.height : label.height
                opacity: item.enter * (1 - item.exitCurve)
                pressed: item.isFocused && home.pressed && !home.quickplayShown
                animated: home.animated

                onEnterChanged: {
                    if (item.index === 0)
                        home.barEnter = item.enter
                }

                onExitChanged: {
                    if (item.isFocused)
                        home.barExit = item.exit
                }

                Component.onCompleted: {
                    if (!home.animated)
                        return
                    item.enter = 0
                    enterAnim.start()
                }

                SequentialAnimation {
                    id: enterAnim

                    PauseAnimation {
                        duration: item.enterStagger
                    }

                    NumberAnimation {
                        target: item
                        property: "enter"
                        from: 0
                        to: 1
                        duration: Motion.homeEnter
                        easing.type: Motion.bezier
                        easing.bezierCurve: Motion.decel
                    }
                }

                SequentialAnimation {
                    id: exitAnim

                    PauseAnimation {
                        duration: item.exitStagger
                    }

                    NumberAnimation {
                        target: item
                        property: "exit"
                        from: 0
                        to: 1
                        duration: Motion.homeOut
                        easing.type: Motion.linear
                    }
                }

                Text {
                    id: label

                    x: 0
                    y: 0
                    text: item.modelData.name
                    color: Ui.white(item.isFocused ? Ui.homeItem.alpha : Ui.homeItem.alphaDim)
                    font: Ui.font(Ui.homeItem)

                    Behavior on color {
                        enabled: home.animated
                        ColorAnimation {
                            duration: Motion.focusAlpha
                        }
                    }
                }

                Text {
                    id: sub

                    visible: item.subtitle !== ""
                    x: Ui.px(Ui.homeSubtitleX)
                    y: label.y + label.baselineOffset + Ui.px(Ui.homeSubtitleDrop + Ui.capHeight * Ui.homeSubtitle.size) - sub.baselineOffset
                    text: item.subtitle
                    color: Ui.white(Ui.homeSubtitle.alpha)
                    font: Ui.font(Ui.homeSubtitle)
                }
            }
        }

        Text {
            id: watermark

            opacity: 1 - home.panVisual
            x: Ui.px(Ui.homeTextX)
            y: menuPanel.height - Ui.px(Ui.edge) - watermark.baselineOffset
            text: "microdap"
            color: Ui.white(Ui.watermark.alpha)
            font: Ui.font(Ui.watermark)
        }
    }

    Timer {
        id: ringReturn

        interval: Motion.touchReturnDelay
        onTriggered: playRing.pressed = false
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: !home.animated
        onAccepted: (index) => {
            if (home.sheetEntry !== null)
                home.pinRequested(home.sheetEntry, home.sheetEntry.section !== "pins")
            sheet.close()
        }
    }

    NumberAnimation {
        id: panAnim

        target: home
        property: "pan"
        duration: Motion.quickplay
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    NumberAnimation {
        id: peekAnim

        target: home
        property: "peekEnter"
        from: 0
        to: 1
        duration: Motion.homeEnterPeek
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    NumberAnimation {
        id: peekExitAnim

        target: home
        property: "peekExit"
        from: 0
        to: 1
        duration: Motion.homeOut
        easing.type: Motion.linear
    }
}
