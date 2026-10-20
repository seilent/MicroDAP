pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool overlay: false
    property string orientation: "portrait"
    property string fixture: "browse"

    readonly property bool levelShot: root.fixture.startsWith("bit-perfect")
    readonly property bool chainShot: root.fixture.startsWith("balance") || root.fixture.startsWith("crossfeed")
    readonly property bool replaygainShot: root.fixture.startsWith("replaygain") || root.fixture.startsWith("crossfade-")
    readonly property bool chainBitPerfect: root.fixture === "crossfeed-bit-perfect" || root.fixture === "replaygain-bit-perfect"
        || root.fixture === "crossfade-bit-perfect"
    readonly property bool rateShot: root.fixture.startsWith("sample-rate")
    readonly property bool dacShot: root.fixture.startsWith("eq-dac")
    readonly property bool bootShot: root.fixture.startsWith("boot")
    readonly property bool aboutShot: root.fixture.startsWith("about")
    readonly property bool usbShot: (root.levelShot || root.rateShot || root.dacShot || root.chainBitPerfect)
        && root.fixture !== "bit-perfect-usb-only"
    readonly property string levelShown: root.fixture === "bit-perfect-phone" ? "exclusive (phone)"
        : root.fixture === "bit-perfect-unavailable" ? "exclusive (unavailable)"
        : root.levelShot || root.dacShot || root.fixture === "sample-rate-locked" || root.chainBitPerfect ? "on" : "off"

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    function applyFixture(): void {
        if (root.fixture.startsWith("brightness")) {
            screen.focusKey("brightness")
            screen.keepFocusVisible()
            if (root.fixture.endsWith("-sheet"))
                screen.activate()
            if (root.fixture === "brightness-step-sheet")
                screen.sheetAction("up", false)
            return
        }
        if (root.fixture.startsWith("sleep")) {
            screen.focusKey("sleep")
            screen.keepFocusVisible()
            if (root.fixture.endsWith("-sheet"))
                screen.activate()
            return
        }
        if (root.fixture.startsWith("max-volume") || root.fixture.startsWith("power-on")) {
            screen.focusKey(root.fixture.startsWith("max-volume") ? "maxVolume" : "powerOnVolume")
            screen.keepFocusVisible()
            if (root.fixture.endsWith("-sheet")) {
                screen.activate()
                screen.sheet.handleAction("down", false)
            }
            return
        }
        if (root.replaygainShot) {
            screen.focusKey(root.fixture.startsWith("crossfade-") ? "crossfade" : "replaygain")
            screen.keepFocusVisible()
            if (root.fixture.endsWith("-sheet")) {
                screen.activate()
                screen.sheet.handleAction("down", false)
            }
            return
        }
        if (root.chainShot) {
            screen.focusKey(root.fixture.startsWith("balance") ? "balance" : "crossfeed")
            screen.keepFocusVisible()
            if (root.fixture === "balance-edit") {
                screen.activate()
                screen.step(-1)
            } else if (root.fixture === "crossfeed-sheet") {
                screen.activate()
                screen.sheet.handleAction("down", false)
            }
            return
        }
        if (root.bootShot || root.aboutShot) {
            screen.focusKey(root.bootShot ? "boot" : "about")
            if (root.fixture === "boot-edit") {
                screen.activate()
                screen.sheet.handleAction("down", false)
            } else if (root.aboutShot) {
                screen.activate()
                screen.focusKey("update")
            }
            screen.keepFocusVisible()
            return
        }
        if (root.levelShot || root.rateShot || root.dacShot) {
            screen.focusKey(root.levelShot ? "level" : root.dacShot || root.fixture === "sample-rate-locked" ? "eq" : "rate")
            if (root.fixture === "bit-perfect-edit" || root.fixture === "sample-rate-edit") {
                screen.activate()
                for (let i = 0; i < (root.levelShot ? 1 : 7); i++)
                    screen.sheet.handleAction("down", false)
            }
            screen.keepFocusVisible()
            return
        }
        if (root.fixture === "touch" || root.fixture === "touch-edit") {
            screen.focusKey("touch")
            if (root.fixture === "touch-edit")
                screen.activate()
            screen.keepFocusVisible()
            return
        }
        if (root.fixture.startsWith("auto-rotate")) {
            screen.focusKey(root.fixture === "auto-rotate-below" ? "saver" : "autoRotate")
            if (root.fixture === "auto-rotate-edit")
                screen.activate()
            screen.keepFocusVisible()
            return
        }
        if (root.fixture === "accent-edit" || root.fixture === "saver-sheet") {
            screen.focusKey(root.fixture === "accent-edit" ? "accent" : "saver")
            screen.activate()
            if (root.fixture === "accent-edit")
                screen.step(1)
            else
                screen.sheet.handleAction("down", false)
            return
        }
        if (root.fixture === "play-through" || root.fixture === "play-through-sheet" || root.fixture === "resume-sheet") {
            screen.focusKey(root.fixture === "resume-sheet" ? "resume" : root.fixture === "play-through" ? "autoNowPlaying" : "playThrough")
            screen.keepFocusVisible()
            if (root.fixture !== "play-through")
                screen.activate()
            return
        }
        if (root.fixture.startsWith("pivot-")) {
            screen.switchPivot(screen.pivots.indexOf(root.fixture.substring(6)))
            return
        }
        if (!root.fixture.startsWith("orientation") || !screen.focusKey("orientation"))
            return
        if (root.fixture === "orientation-edit" || root.fixture === "orientation-edit-end") {
            screen.activate()
            for (let i = 0; i < (root.fixture === "orientation-edit" ? 1 : 3); i++)
                screen.sheet.handleAction("down", false)
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
        property int maxVolume: root.fixture.startsWith("max-volume") ? 70 : 100
        property int powerOnVolume: root.fixture.startsWith("power-on") ? 40 : 0
        readonly property string outputSlot: root.fixture === "crossfeed-speaker" ? "speaker" : sampleOutput.activeOutput
        property int balance: root.chainShot ? -7 : 0
        property string crossfeed: root.chainShot ? "janMeier" : "off"
        property string rgb: "accent"
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
            : root.levelShot || root.dacShot || root.fixture === "sample-rate-locked" || root.chainBitPerfect ? "bitperfect" : "off"
        property string sampleRate: root.fixture === "sample-rate-unsupported" ? "96000" : "auto"
        property string playThrough: root.fixture === "play-through" ? "albums" : "off"
        property string replaygain: root.replaygainShot ? "album" : "off"
        property int crossfadeMs: root.replaygainShot ? 3000 : 0
        property string resume: "position"
        property bool autoNowPlaying: root.fixture === "play-through"
        property string bootSession: root.fixture === "boot-missing" ? "handheld-desktop-session" : "steam"
        property int brightness: root.fixture === "brightness-floor-sheet" ? 300 : 2442
        readonly property var brightnessSteps: [1000, 1500, 2000, 2500, 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500,
                                                7000, 7500, 8000, 8500, 9000, 9500, 10000]

        function bootSessions() {
            return ["microdap", "microemu", "steam"]
        }

        function brightnessStep(from, delta) {
            const up = sampleSettings.brightnessSteps.find((v) => v > from)
            const down = sampleSettings.brightnessSteps.slice().reverse().find((v) => v < from)
            return delta > 0 ? (up === undefined ? from : up) : (down === undefined ? from : down)
        }

        function previewBrightness(value) {
            sampleSettings.brightness = value
        }

        function confirmBrightness(value) {
            sampleSettings.brightness = value
        }

        function cancelBrightness() {
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
        readonly property int sleepMinutesLeft: root.fixture === "sleep" || root.fixture === "sleep-left-sheet" ? 29 : 0
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
