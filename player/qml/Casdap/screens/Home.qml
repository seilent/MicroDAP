pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: home

    property QtObject bluez: null
    property var pinsModel: null
    property var historyModel: null
    property var newModel: null
    property bool animated: true
    property bool quickplayShown: false
    property real pan: 0
    property real peekEnter: home.animated ? 0 : 1
    property real barEnter: home.animated ? 0 : 1
    property int playlistCount: -1

    readonly property var menu: [
        { name: "music", subtitle: "artists albums songs genres playlists" },
        { name: "bluetooth", subtitle: "not connected" },
        { name: "playlists", subtitle: "now playing saved" },
        { name: "history", subtitle: "recent new" },
        { name: "settings", subtitle: "output library display power" }
    ]
    readonly property bool phoneConnected: home.bluez !== null && home.bluez.connected === true
    readonly property bool phonePairing: home.bluez !== null && home.bluez.pairing === true
    readonly property string bluetoothSubtitle: home.phoneConnected ? home.bluez.deviceName
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
    readonly property real quickplayX: Ui.landscape ? 0 : -Ui.px(Ui.qpW - Ui.qpPeek) * (1 - home.pan)
    readonly property real panVisual: Ui.landscape ? (home.quickplayShown ? 1 : 0) : home.pan
    readonly property real quickplayAlpha: (Ui.qpPeekAlpha + (1 - Ui.qpPeekAlpha) * home.panVisual) * home.peekEnter
    readonly property bool panning: panAnim.running
    property alias menuPanel: menuPanel
    property alias quickplayPanel: quickplayPanel
    property alias quickplay: quickplay
    property alias focusBar: focusBar
    property alias sheet: sheet
    property alias watermark: watermark
    property alias menuRepeater: menuRepeater

    property var sheetEntry: null

    signal navigate(string name)
    signal nowPlayingBluetooth()
    signal playRequested(var entry)
    signal pinRequested(var entry, bool pin)
    signal shuffleAll()

    focusCount: home.menu.length

    function itemBaseline(index: int): real {
        return Ui.px(Ui.homeFirstBaseline + index * home.pitch)
    }

    function menuItem(index: int): Item {
        return menuRepeater.itemAt(index)
    }

    function showQuickplay() {
        if (home.quickplayShown)
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

    function completeAll() {
        if (panAnim.running)
            panAnim.complete()
        if (peekAnim.running)
            peekAnim.complete()
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

    function openPinSheet(entry) {
        home.sheetEntry = entry
        sheet.title = entry.title
        sheet.items = entry.section === "pins" ? ["unpin"] : ["pin to quickplay"]
        sheet.open()
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

    onAnimatedChanged: {
        if (!home.animated)
            home.completeAll()
    }

    Component.onCompleted: {
        if (home.animated)
            peekAnim.start()
        else
            home.peekEnter = 1
    }

    Item {
        id: quickplayPanel

        x: home.quickplayX
        y: 0
        width: Ui.px(Ui.qpW)
        height: home.height
        opacity: home.quickplayAlpha

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
            onOpenPhone: home.nowPlayingBluetooth()
            onPlay: (entry) => home.playRequested(entry)
            onPin: (entry) => home.openPinSheet(entry)
        }
    }

    Turnstile {
        id: menuPanel

        x: home.menuX
        y: Ui.px(Motion.quickplayMenuY) * home.pan
        width: home.width
        height: home.height
        hinge: menuPanel.width * menuPanel.scale
        angle: Motion.quickplayAngle * home.pan
        transformOrigin: Item.TopLeft
        scale: 1 + (Motion.quickplayScale - 1) * home.pan
        opacity: 1 + (Motion.quickplayAlpha - 1) * home.pan

        FocusBar {
            id: focusBar

            x: Ui.px(Ui.homeBarX)
            rowY: home.itemBaseline(home.focusIndex) - Ui.px(Ui.homeBarTop)
            rowHeight: Ui.px(Ui.homeBarH)
            animated: home.animated
            opacity: home.barEnter
        }

        Repeater {
            id: menuRepeater

            model: home.menu

            delegate: PressScale {
                id: item

                required property int index
                required property var modelData
                readonly property bool isFocused: item.index === home.focusIndex
                readonly property real itemBaseline: home ? home.itemBaseline(item.index) : 0
                readonly property string subtitle: home.subtitleFor(item.modelData.name, item.modelData.subtitle)
                readonly property real labelBaseline: item.y + label.y + label.baselineOffset
                readonly property real subtitleBaseline: item.y + sub.y + sub.baselineOffset
                readonly property Item labelItem: label
                readonly property Item subtitleItem: sub
                property real enter: home.animated ? 0 : 1

                function completeEnter() {
                    enterAnim.complete()
                    item.enter = 1
                }

                x: Ui.px(Ui.homeTextX) + Ui.px(Motion.homeEnterShift) * (1 - item.enter)
                y: item.itemBaseline - label.baselineOffset
                width: Math.max(label.width, sub.x + sub.width)
                height: sub.visible ? sub.y + sub.height : label.height
                opacity: item.enter
                pressed: item.isFocused && home.pressed && !home.quickplayShown

                onEnterChanged: {
                    if (item.index === 0)
                        home.barEnter = item.enter
                }

                Component.onCompleted: {
                    if (home.animated)
                        enterAnim.start()
                    else if (item.index === 0)
                        home.barEnter = 1
                }

                SequentialAnimation {
                    id: enterAnim

                    PauseAnimation {
                        duration: item.index * Motion.homeEnterStagger
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
    }

    Text {
        id: watermark

        opacity: 1 - home.panVisual

        x: Ui.px(Ui.edge)
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Ui.px(Ui.edge)
        text: "casdap"
        color: Ui.white(Ui.watermark.alpha)
        font: Ui.font(Ui.watermark)
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
}
