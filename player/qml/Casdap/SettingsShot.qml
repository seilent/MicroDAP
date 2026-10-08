pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool overlay: false
    property string orientation: "portrait"
    property string fixture: "browse"

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    function applyFixture(): void {
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

        property string output: "speaker"
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
        readonly property int graphRate: 44100
    }

    QtObject {
        id: sampleOutput

        readonly property bool jackPresent: true
        readonly property bool usbPresent: false
        readonly property string activeOutput: "headphones"
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
        rgbControl: sampleRgb
        eq: sampleEq
        folders: ["/home/armada/Music", "/run/media/armada/SD"]

        Component.onCompleted: {
            screen.completeAll()
            Qt.callLater(root.applyFixture)
        }
    }
}
