pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool overlay: false

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
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
        property string accent: String(Ui.accent).toUpperCase()
        property real scale: 1.0
        property bool pocketControls: false
        property int saverSeconds: 30
        property bool overlayToasts: false
        property int screenOffSeconds: 300
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
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
        folders: ["/home/armada/Music", "/run/media/armada/SD"]

        Component.onCompleted: screen.completeAll()
    }
}
