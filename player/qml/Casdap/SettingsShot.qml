pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool overlay: false
    property string orientation: "portrait"
    property string fixture: "browse"

    readonly property bool levelShot: root.fixture.startsWith("bit-perfect")
    readonly property bool rateShot: root.fixture.startsWith("sample-rate")
    readonly property bool usbShot: (root.levelShot || root.rateShot) && root.fixture !== "bit-perfect-usb-only"
    readonly property string levelShown: root.fixture === "bit-perfect-phone" ? "exclusive (phone)"
        : root.fixture === "bit-perfect-unavailable" ? "exclusive (unavailable)"
        : root.levelShot || root.fixture === "sample-rate-locked" ? "on" : "off"

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    function applyFixture(): void {
        if (root.levelShot || root.rateShot) {
            screen.focusIndex = screen.indexOfKey(root.levelShot ? "level" : root.fixture === "sample-rate-locked" ? "eq" : "rate")
            if (root.fixture === "bit-perfect-edit" || root.fixture === "sample-rate-edit") {
                screen.activate()
                for (let i = 0; i < (root.levelShot ? 1 : 7); i++)
                    screen.step(1)
            }
            screen.keepFocusVisible()
            return
        }
        if (root.fixture === "touch" || root.fixture === "touch-edit") {
            screen.focusIndex = screen.indexOfKey("touch")
            if (root.fixture === "touch-edit") {
                screen.activate()
                screen.step(1)
            }
            screen.keepFocusVisible()
            return
        }
        if (root.fixture.startsWith("auto-rotate")) {
            screen.focusIndex = screen.indexOfKey(root.fixture === "auto-rotate-below" ? "saver" : "autoRotate")
            if (root.fixture === "auto-rotate-edit") {
                screen.activate()
                screen.step(1)
            }
            screen.keepFocusVisible()
            return
        }
        const index = screen.indexOfKey("orientation")
        if (index < 0 || !root.fixture.startsWith("orientation"))
            return
        screen.focusIndex = index
        if (root.fixture === "orientation-edit" || root.fixture === "orientation-edit-end") {
            screen.activate()
            for (let i = 0; i < (root.fixture === "orientation-edit" ? 1 : 3); i++)
                screen.step(1)
        }
        screen.keepFocusVisible()
    }

    QtObject {
        id: sampleSettings

        property string output: root.usbShot ? "usb" : "speaker"
        property string hpGain: "normal"
        property string hpAmp: "lohifi"
        property int spkGain: 102
        property int volumeStep: 5
        property string rgb: "accent"
        property string btCodec: "auto"
        property var roots: ["/home/armada/Music", "/run/media/armada/SD"]
        property int accentHue: 350
        property bool accentFromArt: false
        property real scale: 1.0
        property int saverSeconds: 30
        property bool overlayToasts: false
        property int screenOffSeconds: 300
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
        property string orientation: root.orientation
        property bool autoRotate: root.fixture === "auto-rotate" || root.fixture === "auto-rotate-below"
        property bool touch: true
        property string outputLevel: root.fixture === "bit-perfect-phone" || root.fixture === "bit-perfect-unavailable" ? "exclusive"
            : root.levelShot || root.fixture === "sample-rate-locked" ? "bitperfect" : "off"
        property string sampleRate: root.fixture === "sample-rate-unsupported" ? "96000" : "auto"
    }

    QtObject {
        id: sampleOutputMode

        readonly property string levelStatus: root.levelShown
        readonly property bool effective: root.usbShot && root.levelShown !== "off"
        readonly property var rates: root.fixture === "sample-rate-unsupported" ? [48000]
            : root.usbShot ? [44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000] : [44100, 48000, 88200, 96000, 176400, 192000]
        readonly property bool rateReadOnly: sampleOutputMode.effective
    }

    QtObject {
        id: sampleSensor

        readonly property bool supported: root.fixture !== "auto-rotate-unsupported"
    }

    QtObject {
        id: samplePower

        readonly property var driftPaths: []
    }

    QtObject {
        id: samplePlayer

        readonly property var current: ({ id: 3, title: "Sticking With You", sampleRate: 44100, bitDepth: 16 })
    }

    QtObject {
        id: sampleAudio

        readonly property string pcmFormat: "44100 S24_LE"
        readonly property int hwRate: root.fixture === "sample-rate-locked" ? 96000 : 44100
    }

    QtObject {
        id: sampleOutput

        readonly property bool jackPresent: true
        readonly property bool usbPresent: root.usbShot
        readonly property string activeOutput: root.usbShot ? "usb" : "headphones"
    }

    QtObject {
        id: sampleRgb

        readonly property bool supported: true
    }

    QtObject {
        id: sampleEq

        readonly property string activeOutput: "headphones"

        signal profilesChanged()

        function profileMap(output) {
            return { key: "/home/armada/.config/casdap/eq/Sennheiser HD 600 ParametricEQ.txt", name: "Sennheiser HD 600 ParametricEQ", missing: false, preamp: -6.5, bands: [] }
        }
    }

    SettingsScreen {
        id: screen

        anchors.fill: parent
        animated: false
        overlay: root.overlay
        settings: sampleSettings
        power: samplePower
        player: samplePlayer
        audioInfo: sampleAudio
        outputControl: sampleOutput
        outputMode: sampleOutputMode
        rgbControl: sampleRgb
        eq: sampleEq
        sensor: sampleSensor
        folders: ["/home/armada/Music", "/run/media/armada/SD"]

        Component.onCompleted: {
            screen.completeAll()
            Qt.callLater(root.applyFixture)
        }
    }
}
