pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: sampleSettings

        property string output: "speaker"
        property var roots: ["/home/armada/Music", "/run/media/armada/SD"]
        property string accent: String(Ui.accent).toUpperCase()
        property real scale: 1.0
        property bool pocketControls: false
        property int saverSeconds: 30
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
    }

    SettingsScreen {
        id: screen

        anchors.fill: parent
        animated: false
        settings: sampleSettings
        power: samplePower
        player: samplePlayer
        audioInfo: sampleAudio
        folders: ["/home/armada/Music", "/run/media/armada/SD"]

        Component.onCompleted: screen.completeAll()
    }
}
