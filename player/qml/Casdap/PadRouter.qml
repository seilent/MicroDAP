pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: router

    property PageStack stack: null
    property QtObject pad: null
    property QtObject player: null
    property bool screenOff: false
    property bool pocketControls: false
    property bool keyboard: true
    readonly property bool locked: router.stack !== null && router.stack.inputLocked
    readonly property var keyMap: ({
        [Qt.Key_Up]: "up",
        [Qt.Key_Down]: "down",
        [Qt.Key_Left]: "left",
        [Qt.Key_Right]: "right",
        [Qt.Key_Return]: "a",
        [Qt.Key_Enter]: "a",
        [Qt.Key_Escape]: "b",
        [Qt.Key_Backspace]: "b",
        [Qt.Key_X]: "x",
        [Qt.Key_Y]: "y",
        [Qt.Key_Q]: "lb",
        [Qt.Key_E]: "rb",
        [Qt.Key_PageUp]: "lt",
        [Qt.Key_PageDown]: "rt",
        [Qt.Key_Space]: "start",
        [Qt.Key_Tab]: "back"
    })
    readonly property var directions: ["up", "down", "left", "right"]

    signal routed(string name, bool repeat, bool handled)
    signal dropped(string name, bool repeat)
    signal nowPlaying()
    signal view(string name)
    signal unhandled(string name, bool repeat)

    focus: true
    activeFocusOnTab: false

    function keyName(key: int): string {
        const name = router.keyMap[key]
        return name === undefined ? "" : name
    }

    function page(): Item {
        if (router.stack === null)
            return null
        return router.stack.currentItem
    }

    function pocket(name: string): bool {
        if (!router.screenOff)
            return false
        if (!router.pocketControls || router.player === null)
            return true
        if (name === "lb")
            router.player.previous()
        else if (name === "rb")
            router.player.next()
        else if (name === "start")
            router.player.playPause()
        return true
    }

    function dispatch(name: string, repeat: bool): bool {
        if (name === "")
            return false
        if (router.pocket(name)) {
            router.dropped(name, repeat)
            return false
        }
        if (router.locked) {
            router.dropped(name, repeat)
            return false
        }
        const target = router.page()
        let handled = false
        if (target !== null && typeof target.handleAction === "function")
            handled = target.handleAction(name, repeat)
        if (!handled && !repeat) {
            if (name === "start") {
                router.nowPlaying()
                handled = true
            } else if (name === "back") {
                router.view(name)
                handled = true
            }
        }
        if (!handled)
            router.unhandled(name, repeat)
        router.routed(name, repeat, handled)
        return handled
    }

    function press(name: string) {
        if (name === "" || router.screenOff || router.locked)
            return
        const target = router.page()
        if (target !== null && typeof target.press === "function")
            target.press(name)
    }

    function release(name: string) {
        if (name === "")
            return
        const target = router.page()
        if (target !== null && typeof target.release === "function")
            target.release(name)
    }

    Connections {
        target: router.pad
        ignoreUnknownSignals: true

        function onAction(action, repeat) {
            router.dispatch(router.pad.actionName(action), repeat)
        }

        function onPressed(action) {
            router.press(router.pad.actionName(action))
        }

        function onReleased(action) {
            router.release(router.pad.actionName(action))
        }
    }

    Keys.onPressed: (event) => {
        if (!router.keyboard)
            return
        const name = router.keyName(event.key)
        if (name === "")
            return
        event.accepted = true
        if (event.isAutoRepeat && router.directions.indexOf(name) < 0)
            return
        if (!event.isAutoRepeat)
            router.press(name)
        router.dispatch(name, event.isAutoRepeat)
    }

    Keys.onReleased: (event) => {
        if (!router.keyboard)
            return
        const name = router.keyName(event.key)
        if (name === "")
            return
        event.accepted = true
        if (event.isAutoRepeat)
            return
        router.release(name)
    }
}
