pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "browse"
    readonly property var gain: ({ title: "gain", kind: "number", extras: "-.", decimals: 1, unit: "db", minValue: -24, maxValue: 12 })

    HomeShot {
        anchors.fill: parent
    }

    Keyboard {
        id: keyboard

        anchors.fill: parent
        animated: false
        toastShown: toast.shown
        onRefused: (title, reason) => {
            toast.show(title, reason)
            toast.complete()
        }
    }

    Toast {
        id: toast

        x: 0
        y: 0
        width: root.width
    }

    function typeAll(text: string): void {
        for (const ch of text)
            keyboard.entry.type(ch)
    }

    function applyFixture(): void {
        switch (root.fixture) {
        case "kb-number-freq":
            keyboard.open({ title: "frequency", kind: "number", unit: "hz", text: "2400", minValue: 20, maxValue: 20000 })
            return
        case "kb-number-q":
            keyboard.open({ title: "q", kind: "number", extras: ".", decimals: 2, text: "0.71", minValue: 0.1, maxValue: 10 })
            return
        case "kb-number-gain":
            keyboard.open(root.gain)
            root.typeAll("-3.5")
            keyboard.focusCode = "done"
            return
        case "kb-refused":
            keyboard.open(root.gain)
            root.typeAll("-30")
            keyboard.focusCode = "done"
            keyboard.finish()
            return
        }
        keyboard.open({ title: "playlist name", hint: root.fixture === "kb-empty" ? "Playlist 3" : "" })
        switch (root.fixture) {
        case "kb-empty":
            return
        case "kb-long":
            root.typeAll("Late night drive through the city lights")
            return
        case "kb-petal":
            root.typeAll("Late n")
            keyboard.focusCode = "7"
            keyboard.activate()
            keyboard.stepPick("left")
            return
        case "kb-petal-corner":
            keyboard.focusCode = "1"
            keyboard.activate()
            keyboard.stepPick("up")
            return
        case "kb-shift":
            keyboard.focusCode = "shift"
            keyboard.entry.stepShift()
            keyboard.entry.stepShift()
            return
        case "kb-symbols":
            root.typeAll("Mix ")
            keyboard.focusCode = "symbols"
            keyboard.entry.toggleSymbols()
            return
        case "kb-discard":
            root.typeAll("Late night")
            keyboard.back()
            keyboard.complete()
            return
        default:
            root.typeAll("Late night")
            keyboard.focusCode = "done"
        }
    }

    Component.onCompleted: root.applyFixture()
}
