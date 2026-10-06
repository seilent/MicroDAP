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
    readonly property var pins: typeof Pins === "undefined" ? null : Pins
    readonly property var playlists: typeof Playlists === "undefined" ? null : Playlists
    readonly property var library: root.screenshot || typeof Library === "undefined" ? null : Library
    readonly property var settings: root.screenshot || typeof Settings === "undefined" ? null : Settings
    readonly property var audioInfo: root.screenshot || typeof AudioInfo === "undefined" ? null : AudioInfo
    readonly property int playlistCount: root.library !== null && root.library.playlistCount !== undefined ? root.library.playlistCount : -1

    property var power: root.screenshot || typeof PowerBridge === "undefined" ? null : PowerBridge
    property var player: root.screenshot || typeof Player === "undefined" ? null : Player
    readonly property var status: root.screenshot || typeof StatusModel === "undefined" ? null : StatusModel
    readonly property bool powerScreenOff: root.power !== null && root.power.screenOff === true
    readonly property var bridge: root.screenshot || typeof OverlayBridge === "undefined" ? null : OverlayBridge
    readonly property string topScreen: pages.currentItem !== null ? pages.currentItem.objectName : ""
    readonly property int statusPlaying: 1
    readonly property bool playing: root.player !== null && root.player.status === root.statusPlaying
    readonly property bool hasTrack: root.player !== null && root.player.current !== undefined && root.player.current !== null
        && root.player.current.id !== undefined
    readonly property bool phoneActive: root.bluez !== null && root.bluez.transportActive === true

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
        root.applySettings()
        if (!root.screenshot)
            pages.push(homeComponent)
    }

    function applySettings() {
        if (root.settings === null)
            return
        if (root.settings.scale !== undefined)
            Ui.userScale = Math.max(Ui.sMin, Math.min(Ui.sMax, Number(root.settings.scale)))
        if (root.settings.accent !== undefined && String(root.settings.accent) !== "")
            Ui.accent = root.settings.accent
    }

    function navigate(name: string) {
        switch (name) {
        case "bluetooth":
            pages.push(bluetoothComponent, {})
            break
        case "music":
            pages.push(hubComponent, {})
            break
        case "playlists":
            pages.push(playlistsComponent, {})
            break
        case "history":
            pages.push(historyComponent, {})
            break
        case "settings":
            pages.push(settingsComponent, {})
            break
        default:
            console.info("casdap: navigate " + name)
            break
        }
    }

    function openPairing() {
        pages.push(bluetoothComponent, { pivotIndex: 1 })
    }

    function openNowPlaying() {
        if (root.topScreen === "nowplaying")
            return
        if (!root.hasTrack && !root.phoneActive) {
            console.info("casdap: now playing empty")
            return
        }
        pages.push(nowPlayingComponent, {}, "fade")
    }

    function openQueue() {
        if (root.topScreen === "queue")
            return
        pages.push(queueComponent, {})
    }

    function openArtist(id: int): void {
        pages.push(artistComponent, { artistId: id })
    }

    function openAlbum(id: int): void {
        pages.push(albumComponent, { albumId: id })
    }

    function openEntry(kind: string, id: int): void {
        if (kind === "album")
            root.openAlbum(id)
        else if (kind === "artist")
            root.openArtist(id)
        else
            console.info("casdap: navigate " + kind + " " + id)
    }

    function setPinned(kind: string, id: int, pin: bool): void {
        const verb = pin ? "pin " : "unpin "
        if (root.library === null) {
            console.info("casdap: " + verb + kind + " " + id)
            return
        }
        const ok = pin ? root.library.pin(kind, id) : root.library.unpin(kind, id)
        if (!ok)
            console.info("casdap: " + verb + kind + " " + id + " failed")
    }

    function sheetAction(action: string, kind: string, id: int): void {
        if (action === "pin to quickplay" || action === "unpin from quickplay") {
            root.setPinned(kind, id, action === "pin to quickplay")
            return
        }
        console.info("casdap: " + action + " " + kind + " " + id)
    }

    function playEntry(entry: var): void {
        const kind = entry.kind === undefined || entry.kind === null ? "" : String(entry.kind)
        const id = Number(entry.id)
        let ids = []
        if (kind === "song") {
            ids = [id]
        } else if (kind === "album" && root.library !== null) {
            for (const row of root.library.albumTracks(id))
                ids.push(Number(row.id))
        } else if (kind === "artist" && root.library !== null) {
            ids = root.library.artistTracks(id)
        }
        if (root.player === null || ids.length === 0) {
            console.info("casdap: play " + kind + " " + id)
            return
        }
        root.player.playTracks(ids, 0)
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
        dialogFade: switchPrompt.scrim.opacity
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
        onNowPlaying: root.openNowPlaying()
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
            pinsModel: root.pins
            playlistCount: root.playlistCount
            onNavigate: (name) => root.navigate(name)
            onNowPlayingBluetooth: root.openNowPlaying()
            onPlayRequested: (entry) => root.playEntry(entry)
            onPinRequested: (entry, pin) => root.setPinned(entry.kind === undefined ? "" : String(entry.kind), Number(entry.id), pin)
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
            onOpenNowPlaying: root.openNowPlaying()
        }
    }

    Component {
        id: nowPlayingComponent

        NowPlaying {
            objectName: "nowplaying"
            animated: !root.hidden
            screenOff: root.off
            player: root.player
            bluez: root.bluez
            status: root.status
            onOpenQueue: root.openQueue()
        }
    }

    Component {
        id: queueComponent

        QueueScreen {
            objectName: "queue"
            animated: !root.hidden
            screenOff: root.off
            player: root.player
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
            onOpen: (kind, id) => root.openEntry(kind, id)
            onNowPlaying: root.openNowPlaying()
            onPlay: (kind, id) => console.info("casdap: play " + kind + " " + id)
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
        }
    }

    Component {
        id: playlistsComponent

        PlaylistsScreen {
            objectName: "playlists"
            animated: !root.hidden
            screenOff: root.off
            playlistsModel: root.playlists
            player: root.player
            library: root.library
            onNowPlaying: root.openNowPlaying()
            onPlay: (kind, id) => console.info("casdap: play " + kind + " " + id)
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
        }
    }

    Component {
        id: historyComponent

        HistoryScreen {
            objectName: "history"
            animated: !root.hidden
            screenOff: root.off
            historyModel: root.historyRecent
            newModel: root.historyNew
            player: root.player
            library: root.library
            onPlay: (kind, id) => console.info("casdap: play " + kind + " " + id)
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
        }
    }

    Component {
        id: settingsComponent

        SettingsScreen {
            objectName: "settings"
            animated: !root.hidden
            screenOff: root.off
            settings: root.settings
            power: root.power
            player: root.player
            audioInfo: root.audioInfo
            onPairPhone: root.openPairing()
        }
    }

    Component {
        id: artistComponent

        ArtistDetail {
            objectName: "artist"
            animated: !root.hidden
            screenOff: root.off
            library: root.library
            player: root.player
            onOpenAlbum: (id) => root.openAlbum(id)
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
        }
    }

    Component {
        id: albumComponent

        AlbumDetail {
            objectName: "album"
            animated: !root.hidden
            screenOff: root.off
            library: root.library
            player: root.player
            status: root.status
            bluez: root.bluez
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
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
            : root.shotScreen === "nowplaying" ? nowPlayingShotComponent
            : root.shotScreen === "queue" ? queueShotComponent
            : root.shotScreen === "artist" ? artistShotComponent
            : root.shotScreen === "album" ? albumShotComponent
            : root.shotScreen === "settings" ? settingsShotComponent
            : root.shotScreen === "jump" ? jumpShotComponent
            : root.shotScreen === "playlists" ? playlistsShotComponent
            : root.shotScreen === "history" ? historyShotComponent
            : null
    }

    Component {
        id: settingsShotComponent
        SettingsShot {}
    }

    Component {
        id: jumpShotComponent
        HubShot {
            pivot: root.shotPivot
            jump: true
        }
    }

    Component {
        id: playlistsShotComponent
        PlaylistsShot {}
    }

    Component {
        id: historyShotComponent
        HistoryShot {}
    }

    Component {
        id: artistShotComponent
        DetailShot {
            kind: "artist"
        }
    }

    Component {
        id: albumShotComponent
        DetailShot {
            kind: "album"
        }
    }

    Component {
        id: queueShotComponent
        QueueShot {}
    }

    Component {
        id: nowPlayingShotComponent
        NowPlayingShot {}
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
