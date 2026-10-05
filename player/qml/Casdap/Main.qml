pragma ComponentBehavior: Bound
import QtQuick

Window {
    id: root

    property bool screenshot: false
    property string shotScreen: ""
    property real shotAngle: 40
    property int shotWidth: Screen.width
    property int shotHeight: Screen.height

    readonly property var bluez: typeof BluezModel === "undefined" ? null : BluezModel
    readonly property var historyRecent: typeof HistoryRecent === "undefined" ? null : HistoryRecent
    readonly property var historyNew: typeof HistoryNew === "undefined" ? null : HistoryNew

    width: shotWidth
    height: shotHeight
    visible: true
    visibility: screenshot ? Window.Windowed : Window.FullScreen
    color: Ui.bg

    onWidthChanged: Ui.windowW = root.width
    onHeightChanged: Ui.windowH = root.height
    Component.onCompleted: {
        Ui.windowW = root.width
        Ui.windowH = root.height
        if (!root.screenshot)
            pages.push(homeComponent)
    }

    PageStack {
        id: pages

        anchors.fill: parent
        visible: !root.screenshot
    }

    PadRouter {
        id: router

        anchors.fill: parent
        stack: pages
        pad: typeof Pad === "undefined" ? null : Pad
        player: typeof Player === "undefined" ? null : Player
        screenOff: typeof PowerBridge === "undefined" ? false : PowerBridge.screenOff
        pocketControls: typeof Settings === "undefined" ? false : Settings.pocketControls
        onNowPlaying: console.log("casdap: navigate nowplaying")
    }

    Component {
        id: homeComponent

        Home {
            bluez: root.bluez
            historyModel: root.historyRecent
            newModel: root.historyNew
            onNavigate: (name) => console.log("casdap: navigate " + name)
            onNowPlayingBluetooth: console.log("casdap: navigate nowplaying-bluetooth")
            onPlayRequested: (entry) => console.log("casdap: play " + entry.section + " " + entry.id)
            onPinRequested: (entry, pin) => console.log("casdap: " + (pin ? "pin " : "unpin ") + entry.section + " " + entry.id)
            onShuffleAll: console.log("casdap: shuffle all")
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.shotScreen === "specimen" ? specimenComponent
            : root.shotScreen === "motion" ? motionComponent
            : root.shotScreen === "components" ? componentsComponent
            : root.shotScreen === "overlays" ? overlaysComponent
            : root.shotScreen === "volume" ? volumeComponent
            : root.shotScreen === "home" ? homeShotComponent
            : root.shotScreen === "quickplay" ? quickplayShotComponent
            : null
    }

    Component {
        id: componentsComponent
        ComponentsShot {}
    }

    Component {
        id: overlaysComponent
        OverlaysShot {}
    }

    Component {
        id: volumeComponent
        VolumeShot {}
    }

    Component {
        id: specimenComponent
        Specimen {}
    }

    Component {
        id: motionComponent
        MotionShot {
            angle: root.shotAngle
        }
    }

    Component {
        id: homeShotComponent
        HomeShot {}
    }

    Component {
        id: quickplayShotComponent
        HomeShot {
            quickplay: true
        }
    }
}
