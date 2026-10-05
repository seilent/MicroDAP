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

    readonly property bool powerScreenOff: typeof PowerBridge === "undefined" ? false : PowerBridge.screenOff
    readonly property var power: root.screenshot || typeof PowerBridge === "undefined" ? null : PowerBridge
    readonly property var player: root.screenshot || typeof Player === "undefined" ? null : Player
    readonly property var bridge: root.screenshot || typeof OverlayBridge === "undefined" ? null : OverlayBridge
    readonly property string topScreen: pages.currentItem !== null ? pages.currentItem.objectName : ""

    property bool overlay: root.bridge !== null
    property bool overlayMapped: root.bridge !== null ? root.bridge.mapped : true
    property real overlayFade: root.bridge !== null ? root.bridge.opacity : 1
    readonly property bool hidden: root.overlay && !root.overlayMapped
    readonly property bool off: root.powerScreenOff || root.hidden
    property alias pageStack: pages
    property alias overlayLayer: overlays
    property alias background: bg

    width: shotWidth
    height: shotHeight
    visible: !root.overlay
    visibility: screenshot ? Window.Windowed : root.overlay ? Window.AutomaticVisibility : Window.FullScreen
    color: Ui.bg

    onWidthChanged: Ui.windowW = root.width
    onHeightChanged: Ui.windowH = root.height
    onHiddenChanged: {
        if (!root.hidden)
            return
        pages.completeAll()
        overlays.completeAll()
    }
    Component.onCompleted: {
        Ui.windowW = root.width
        Ui.windowH = root.height
        if (!root.screenshot)
            pages.push(homeComponent)
    }

    function navigate(name: string) {
        if (name === "bluetooth") {
            pages.push(bluetoothComponent, {})
            return
        }
        console.info("casdap: navigate " + name)
    }

    Binding {
        target: root.contentItem
        property: "opacity"
        value: root.overlayFade
        when: root.overlay
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        color: Ui.bg
    }

    PageStack {
        id: pages

        anchors.fill: parent
        visible: !root.screenshot && !root.hidden
    }

    PadRouter {
        id: router

        anchors.fill: parent
        stack: pages
        pad: typeof Pad === "undefined" ? null : Pad
        player: typeof Player === "undefined" ? null : Player
        screenOff: root.off
        pocketControls: !root.overlay && (typeof Settings === "undefined" ? false : Settings.pocketControls)
        onNowPlaying: console.info("casdap: navigate nowplaying")
    }

    OverlayLayer {
        id: overlays

        anchors.fill: parent
        visible: !root.screenshot && !root.hidden
        power: root.power
        bluez: root.screenshot ? null : root.bluez
        player: root.player
        screenOff: root.off
        topScreen: root.topScreen
    }

    Component {
        id: homeComponent

        Home {
            objectName: "home"
            animated: !root.hidden
            bluez: root.bluez
            historyModel: root.historyRecent
            newModel: root.historyNew
            onNavigate: (name) => root.navigate(name)
            onNowPlayingBluetooth: console.info("casdap: navigate nowplaying-bluetooth")
            onPlayRequested: (entry) => console.info("casdap: play " + entry.section + " " + entry.id)
            onPinRequested: (entry, pin) => console.info("casdap: " + (pin ? "pin " : "unpin ") + entry.section + " " + entry.id)
            onShuffleAll: console.info("casdap: shuffle all")
        }
    }

    Component {
        id: bluetoothComponent

        Bluetooth {
            objectName: "bluetooth"
            animated: !root.hidden
            bluez: root.bluez
            screenOff: root.off
            onOpenNowPlaying: console.info("casdap: navigate nowplaying-bluetooth")
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
            : root.shotScreen === "bluetooth" ? bluetoothShotComponent
            : null
    }

    Component {
        id: bluetoothShotComponent
        BluetoothShot {}
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
