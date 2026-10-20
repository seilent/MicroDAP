pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "browse"
    readonly property var contextBluez: typeof BluezModel === "undefined" ? null : BluezModel
    readonly property bool eqShot: root.fixture.startsWith("bt-eq")
    readonly property bool settingsShot: root.fixture.startsWith("settings")
    readonly property bool pairingPreset: root.contextBluez !== null && root.contextBluez.pairing === true

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: sampleEq

        readonly property string activeOutput: "headphones"
        readonly property var sources: []

        signal profilesChanged()

        function profileMap(output) {
            return { mode: "parametric", key: "custom", name: "", missing: false, preamp: -3.5, saved: false,
                bands: [{ type: "lowshelf", freq: 105, freqIndex: -1, gain: 3.5, q: 0.7 },
                    { type: "peak", freq: 2000, freqIndex: 40, gain: -2, q: 1.4 }] }
        }
    }

    QtObject {
        id: sampleSettings

        property string btCodec: root.fixture === "settings-pending" || root.fixture === "settings-sheet" ? "aptx" : "auto"
        property string btCodecHeadphones: root.fixture === "settings-headphones-pending" ? "ldac"
            : root.fixture === "settings-headphones-long" ? "aptx_hd" : "aac"
    }

    Bluetooth {
        id: screen

        anchors.fill: parent
        animated: false
        bluez: root.contextBluez
        settings: sampleSettings
        btApplied: root.fixture === "settings-headphones-pending" || root.fixture === "settings-headphones-long" ? "aac" : "auto"
        eq: root.eqShot ? sampleEq : null
        pivotIndex: root.settingsShot ? 4 : root.eqShot ? 3 : root.pairingPreset ? 1 : 0
        onToast: (first, second) => toast.show(first, second)

        Component.onCompleted: {
            screen.completeAll()
            if (root.fixture === "pairing-pulse")
                screen.pulseHolder.scale = screen.pulsePeak
            if (root.fixture === "bt-eq-band") {
                screen.eqPage.headphonesPane.openBand = 0
                screen.eqPage.focusIndex = screen.eqPage.indexOfKey("band0:gain")
                Qt.callLater(() => screen.eqPage.activePane.ensureVisible(screen.eqPage.focusIndex, 0))
            }
            if (root.fixture.endsWith("-sheet") && root.settingsShot) {
                screen.openCodec()
                screen.sheet.handleAction("down", false)
                screen.sheet.complete()
            }
            if (root.fixture === "hp-sheet") {
                screen.headphonesList.snapTo(2)
                screen.openHeadsetSheet()
                screen.sheet.complete()
            }
            if (root.fixture === "wrong-role") {
                toast.show("Pixel 8", "use the phone pivot")
                toast.complete()
            }
            if (root.fixture === "guard-toast") {
                screen.guardedBack()
                toast.complete()
            }
        }
    }

    Toast {
        id: toast

        x: 0
        y: 0
        width: root.width
    }
}
