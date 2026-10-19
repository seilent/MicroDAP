pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: page

    property QtObject bluez: null
    property QtObject link: null
    property bool animated: true
    property bool screenOff: false
    property int pairingTimeout: 120
    property bool paired: false
    property bool connected: false
    property bool wanted: false
    property string shownCount: ""
    property bool ready: false
    property bool autoOpen: true
    property int shownPasskey: 0

    readonly property bool live: page.animated && !page.screenOff
    readonly property bool available: page.bluez !== null && page.bluez.ready !== false
    readonly property bool open: page.bluez !== null && page.bluez.pairing === true
        && String(page.bluez.pairingMode) === "companion"
    readonly property int remaining: page.bluez !== null && page.bluez.pairingRemaining !== undefined ? page.bluez.pairingRemaining : 0
    readonly property bool confirming: page.bluez !== null && page.bluez.confirmPending === true
    readonly property int passkey: page.bluez !== null && page.bluez.confirmPasskey !== undefined ? page.bluez.confirmPasskey : 0
    readonly property string countWord: !page.available ? "unavailable"
        : page.open ? "pairing"
        : page.connected ? "connected"
        : page.paired ? "paired"
        : ""
    readonly property string countText: page.countWord === "pairing" ? "pairing " + page.remaining : page.countWord
    readonly property bool settled: (page.stack === null || page.active) && !page.arriving
    readonly property bool arriving: !page.arrived && page.live && page.stackAnimates
    readonly property string hint: !page.available ? "bluetooth unavailable"
        : page.open ? "pair from your phone, b to stop"
        : page.paired && !page.connected ? "open the app on your phone"
        : "press a to make this device discoverable"
    readonly property real arcSweep: page.open && page.pairingTimeout > 0
        ? 360 * Math.max(0, Math.min(1, page.remaining / page.pairingTimeout))
        : 0
    readonly property bool countSwapping: countAnim.running

    property alias header: header
    property alias countItem: countLabel
    property alias panel: panel
    property alias codeDialog: codeDialog

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

    function openWindow(): void {
        if (!page.available) {
            page.wanted = true
            return
        }
        if (page.open)
            return
        page.wanted = false
        if (page.link !== null && typeof page.link.pairPhoneApp === "function")
            page.link.pairPhoneApp()
        else
            page.call("startCompanionPairing", page.pairingTimeout)
    }

    function answer(yes: bool): void {
        if (codeDialog.shown)
            codeDialog.close()
        if (page.bluez !== null && typeof page.bluez.answerConfirm === "function")
            page.bluez.answerConfirm(page.shownPasskey, yes)
    }

    function showCode(): void {
        if (!page.confirming || codeDialog.shown || !page.settled)
            return
        page.shownPasskey = page.passkey
        if (page.dialogUp) {
            page.answer(false)
            page.toast("phone app", "pairing refused")
            return
        }
        codeDialog.title = "code " + String(page.shownPasskey).padStart(6, "0")
        codeDialog.open()
        codeDialog.focusIndex = codeDialog.keepIndex
        codeDialog.barIndex = codeDialog.keepIndex
    }

    function completeAll(): void {
        panel.complete()
        codeDialog.complete()
        if (countAnim.running)
            countAnim.stop()
        countLabel.opacity = 1
        page.shownCount = page.countText
    }

    function back(): bool {
        if (page.stack === null || page.stack.depth < 2)
            return false
        page.stack.pop()
        return true
    }

    function handleAction(name: string, repeat: bool): bool {
        if (codeDialog.shown)
            return codeDialog.handleAction(name, repeat)
        switch (name) {
        case "a":
            if (!repeat)
                page.openWindow()
            return true
        case "b":
            if (repeat)
                return false
            if (page.open) {
                page.call("stopPairing")
                return true
            }
            return page.back()
        default:
            return false
        }
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        switch (kind) {
        case "ring":
            return "a"
        case "header":
            return "b"
        default:
            return ""
        }
    }

    onCountTextChanged: {
        if (page.ready && page.live && page.shownCount.split(" ")[0] !== page.countText.split(" ")[0])
            countAnim.restart()
        else if (!countAnim.running)
            page.shownCount = page.countText
    }

    onConfirmingChanged: {
        if (page.confirming)
            page.showCode()
        else if (codeDialog.shown)
            codeDialog.close()
    }

    onPasskeyChanged: {
        if (!codeDialog.shown || page.passkey === page.shownPasskey)
            return
        codeDialog.close()
        page.showCode()
    }

    onSettledChanged: page.showCode()

    onAvailableChanged: {
        if (page.available && page.wanted)
            page.openWindow()
    }

    onLiveChanged: {
        if (!page.live)
            page.completeAll()
    }

    Component.onCompleted: {
        page.shownCount = page.countText
        page.ready = true
        if (page.autoOpen)
            page.openWindow()
        page.showCode()
    }

    Component.onDestruction: {
        if (page.open)
            page.call("stopPairing")
    }

    Connections {
        target: page.bluez
        ignoreUnknownSignals: true

        function onCompanionPaired() {
            page.paired = true
            page.connected = false
            panel.pulseNow()
        }
    }

    Connections {
        target: page.link
        ignoreUnknownSignals: true

        function onPhoneHello() {
            page.connected = true
        }
    }

    PageSurface {
        anchors.fill: parent
    }

    PairingPanel {
        id: panel

        width: page.width
        height: page.height
        live: page.live
        arcSweep: page.arcSweep
        pressed: page.pressed
        hint: page.hint
    }

    CroppedHeader {
        id: header

        text: "phone app"
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
            script: page.shownCount = page.countText
        }
        NumberAnimation {
            target: countLabel
            property: "opacity"
            to: 1
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
    }

    Dialog {
        id: codeDialog

        readonly property int pairIndex: 0
        readonly property int keepIndex: 1

        anchors.fill: parent
        screenOff: page.screenOff
        items: ["pair phone", "keep unpaired"]
        onAccepted: (index) => page.answer(index === codeDialog.pairIndex)
        onRejected: page.answer(false)
    }
}
