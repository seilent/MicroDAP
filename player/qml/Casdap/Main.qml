pragma ComponentBehavior: Bound
import QtQuick

Window {
    id: root

    property bool screenshot: false
    property string shotScreen: ""
    property string shotPivot: "albums"
    property real shotAngle: 40
    property int shotWidth: Screen.width
    property int shotHeight: Screen.height

    readonly property var bluez: typeof BluezModel === "undefined" ? null : BluezModel
    readonly property var historyRecent: typeof HistoryRecent === "undefined" ? null : HistoryRecent
    readonly property var historyNew: typeof HistoryNew === "undefined" ? null : HistoryNew
    readonly property var artists: typeof Artists === "undefined" ? null : Artists
    readonly property var albums: typeof Albums === "undefined" ? null : Albums
    readonly property var songs: typeof Songs === "undefined" ? null : Songs
    readonly property var genres: typeof Genres === "undefined" ? null : Genres
    readonly property var library: root.screenshot || typeof Library === "undefined" ? null : Library

    property var power: root.screenshot || typeof PowerBridge === "undefined" ? null : PowerBridge
    property var player: root.screenshot || typeof Player === "undefined" ? null : Player
    readonly property bool powerScreenOff: root.power !== null && root.power.screenOff === true
    readonly property var bridge: root.screenshot || typeof OverlayBridge === "undefined" ? null : OverlayBridge
    readonly property string topScreen: pages.currentItem !== null ? pages.currentItem.objectName : ""
    readonly property int statusPlaying: 1
    readonly property bool playing: root.player !== null && root.player.status === root.statusPlaying

    property bool overlay: root.bridge !== null
    property bool overlayMapped: root.bridge !== null ? root.bridge.mapped : true
    property real overlayFade: root.bridge !== null ? root.bridge.opacity : 1
    readonly property bool hidden: root.overlay && !root.overlayMapped
    readonly property bool off: root.powerScreenOff || root.hidden
    property alias pageStack: pages
    property alias overlayLayer: overlays
    property alias background: bg
    property alias switchPrompt: switchPrompt
    property alias router: router

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
        switchPrompt.complete()
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
        if (name === "music") {
            pages.push(hubComponent, {})
            return
        }
        console.info("casdap: navigate " + name)
    }

    function closeSheets() {
        const top = pages.currentItem
        if (top === null || top.sheet === undefined || top.sheet === null)
            return
        if (top.sheet.shown)
            top.sheet.close()
    }

    function showSwitchPrompt() {
        if (root.off) {
            console.info("casdap: ShowSwitchPrompt ignored, screen off")
            return
        }
        if (switchPrompt.shown) {
            switchPrompt.close()
            return
        }
        root.closeSheets()
        switchPrompt.show(root.playing)
    }

    function switchFailed() {
        if (switchPrompt.shown)
            switchPrompt.close()
        overlays.toast.show("", "switch failed")
    }

    Connections {
        target: root.power
        ignoreUnknownSignals: true

        function onShowSwitchPrompt() {
            root.showSwitchPrompt()
        }

        function onSwitchFailed() {
            root.switchFailed()
        }
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
        modal: switchPrompt
        pad: typeof Pad === "undefined" ? null : Pad
        player: typeof Player === "undefined" ? null : Player
        screenOff: root.off
        pocketControls: !root.overlay && (typeof Settings === "undefined" ? false : Settings.pocketControls)
        onNowPlaying: console.info("casdap: navigate nowplaying")
    }

    SwitchPrompt {
        id: switchPrompt

        anchors.fill: parent
        screenOff: root.off
        onClosePlayer: {
            if (root.power !== null)
                root.power.closeAndSwitch()
        }
        onKeepInBackground: {
            if (root.power !== null)
                root.power.keepInBackground()
        }
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

    Component {
        id: hubComponent

        MusicHub {
            objectName: "music"
            animated: !root.hidden
            screenOff: root.off
            artistsModel: root.artists
            albumsModel: root.albums
            songsModel: root.songs
            genresModel: root.genres
            player: root.player
            library: root.library
            onOpen: (kind, id) => console.info("casdap: navigate " + kind + " " + id)
            onNowPlaying: console.info("casdap: navigate nowplaying")
            onJumpRequested: console.info("casdap: navigate jump")
            onPlay: (kind, id) => console.info("casdap: play " + kind + " " + id)
            onSheetAction: (action, kind, id) => console.info("casdap: " + action + " " + kind + " " + id)
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
            : root.shotScreen === "prompt" ? promptShotComponent
            : root.shotScreen === "hub" ? hubShotComponent
            : null
    }

    Component {
        id: hubShotComponent
        HubShot {
            pivot: root.shotPivot
        }
    }

    Component {
        id: promptShotComponent
        PromptShot {}
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
