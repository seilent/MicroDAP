pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool overlay: false
    property string orientation: "portrait"
    property string fixture: "browse"

    readonly property bool levelShot: root.fixture.startsWith("bit-perfect")
    readonly property bool rateShot: root.fixture.startsWith("sample-rate")
    readonly property bool dacShot: root.fixture.startsWith("eq-dac")
    readonly property bool bootShot: root.fixture.startsWith("boot")
    readonly property bool aboutShot: root.fixture.startsWith("about")
    readonly property bool usbShot: (root.levelShot || root.rateShot || root.dacShot) && root.fixture !== "bit-perfect-usb-only"
    readonly property string levelShown: root.fixture === "bit-perfect-phone" ? "exclusive (phone)"
        : root.fixture === "bit-perfect-unavailable" ? "exclusive (unavailable)"
        : root.levelShot || root.dacShot || root.fixture === "sample-rate-locked" ? "on" : "off"

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    function applyFixture(): void {
        if (root.bootShot || root.aboutShot) {
            screen.focusIndex = screen.indexOfKey(root.bootShot ? "boot" : "about")
            if (root.fixture === "boot-edit") {
                screen.activate()
                screen.step(1)
            } else if (root.aboutShot) {
                screen.activate()
                screen.focusIndex = screen.indexOfKey("update")
            }
            screen.keepFocusVisible()
            return
        }
        if (root.levelShot || root.rateShot || root.dacShot) {
            screen.focusIndex = screen.indexOfKey(root.levelShot ? "level" : root.dacShot || root.fixture === "sample-rate-locked" ? "eq" : "rate")
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
        property var roots: ["/home/micro/Music", "/run/media/micro/SD"]
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
            : root.levelShot || root.dacShot || root.fixture === "sample-rate-locked" ? "bitperfect" : "off"
        property string sampleRate: root.fixture === "sample-rate-unsupported" ? "96000" : "auto"
        property string bootSession: root.fixture === "boot-missing" ? "handheld-desktop-session" : "steam"

        function bootSessions() {
            return ["microdap", "microemu", "steam"]
        }
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

        readonly property string imageVersion: root.fixture === "about-missing" ? "" : "testing-38784f1"
        readonly property string updateStatus: root.fixture === "about-available" ? "available"
            : root.fixture === "about-missing" ? "unchecked" : "none"
        readonly property string updateVersion: root.fixture === "about-available" ? "testing-0628a41" : ""
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
        id: sampleDacEq

        readonly property string status: root.fixture === "eq-dac-failed" ? "failed"
            : root.fixture === "eq-dac-unsupported" ? "unsupported" : "on"
    }

    QtObject {
        id: sampleEq

        readonly property string activeOutput: root.dacShot ? "usb" : "headphones"

        signal profilesChanged()

        function profileMap(output) {
            if (root.fixture === "eq-dac-custom")
                return { key: "custom", name: "", missing: false, preamp: 0, bands: [] }
            return { key: "/home/micro/.config/microdap/eq/Sennheiser HD 600 ParametricEQ.txt", name: "Sennheiser HD 600 ParametricEQ", missing: false, preamp: -6.5, bands: [] }
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
        dacEq: root.dacShot ? sampleDacEq : null
        sensor: sampleSensor
        folders: ["/home/micro/Music", "/run/media/micro/SD"]

        Component.onCompleted: {
            screen.completeAll()
            Qt.callLater(root.applyFixture)
        }
    }
}
