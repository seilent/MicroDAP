pragma ComponentBehavior: Bound
import QtQuick

Window {
    id: root

    property bool screenshot: false
    property string shotScreen: ""
    property string shotPivot: "albums"
    property real shotAngle: 40
    property bool shotOverlay: false
    property int shotWidth: Screen.width
    property int shotHeight: Screen.height

    readonly property var bluez: typeof BluezModel === "undefined" ? null : BluezModel
    readonly property var historyRecent: typeof HistoryRecent === "undefined" ? null : HistoryRecent
    readonly property var historyNew: typeof HistoryNew === "undefined" ? null : HistoryNew
    readonly property var quickplayRecent: typeof QuickplayRecent === "undefined" ? null : QuickplayRecent
    readonly property var quickplayNew: typeof QuickplayNew === "undefined" ? null : QuickplayNew
    readonly property var artistsHub: typeof ArtistsHub === "undefined" ? null : ArtistsHub
    readonly property var albumsGrid: typeof AlbumsGrid === "undefined" ? null : AlbumsGrid
    readonly property var songsHub: typeof SongsHub === "undefined" ? null : SongsHub
    readonly property var genresHub: typeof GenresHub === "undefined" ? null : GenresHub
    readonly property var genres: typeof Genres === "undefined" ? null : Genres
    readonly property var pins: typeof Pins === "undefined" ? null : Pins
    readonly property var playlists: typeof Playlists === "undefined" ? null : Playlists
    property var library: root.screenshot || typeof Library === "undefined" ? null : Library
    property var settings: root.screenshot || typeof Settings === "undefined" ? null : Settings
    property var audioInfo: root.screenshot || typeof AudioInfo === "undefined" ? null : AudioInfo
    property var outputControl: root.screenshot || typeof OutputControl === "undefined" ? null : OutputControl
    property var codecFragment: root.screenshot || typeof CodecFragment === "undefined" ? null : CodecFragment
    property var rgbControl: root.screenshot || typeof RgbControl === "undefined" ? null : RgbControl
    property var accentControl: root.screenshot || typeof Accent === "undefined" ? null : Accent
    readonly property int sliderHue: root.settings !== null && root.settings.accentHue !== undefined
        ? Ui.hueOnGrid(Number(root.settings.accentHue)) : Ui.accentHueDefault
    readonly property int accentHue: root.accentControl !== null && root.accentControl.hue !== undefined
        ? Ui.hueOnGrid(Number(root.accentControl.hue)) : root.sliderHue
    readonly property color accentColor: root.accentControl !== null && root.accentControl.color !== undefined
        ? root.accentControl.color : Ui.accentForHue(root.accentHue)
    readonly property color accentBrightColor: root.accentControl !== null && root.accentControl.brightColor !== undefined
        ? root.accentControl.brightColor : Ui.brightForHue(root.accentHue)
    property var eq: root.screenshot || typeof Eq === "undefined" ? null : Eq
    property var stateStore: root.screenshot || typeof PlayerState === "undefined" ? null : PlayerState
    readonly property int playlistCount: root.library !== null && root.library.playlistCount !== undefined ? root.library.playlistCount : -1

    property var power: root.screenshot || typeof PowerBridge === "undefined" ? null : PowerBridge
    property var player: root.screenshot || typeof Player === "undefined" ? null : Player
    readonly property var status: root.screenshot || typeof StatusModel === "undefined" ? null : StatusModel
    readonly property bool powerScreenOff: root.power !== null && root.power.screenOff === true
    readonly property bool switching: root.power !== null && root.power.switching === true
    readonly property var bridge: root.screenshot || typeof OverlayBridge === "undefined" ? null : OverlayBridge
    readonly property string topScreen: pages.currentItem !== null ? pages.currentItem.objectName : ""
    readonly property int statusPlaying: 1
    readonly property bool playing: root.player !== null && root.player.status === root.statusPlaying
    readonly property var current: root.player !== null && root.player.current !== undefined && root.player.current !== null
        ? root.player.current : ({})
    readonly property bool hasTrack: root.current.id !== undefined && root.current.id !== null
    readonly property bool phoneActive: root.bluez !== null && root.bluez.transportActive === true
    readonly property int saverSeconds: root.settings !== null && root.settings.saverSeconds !== undefined ? Number(root.settings.saverSeconds) : 0
    readonly property int screenOffSeconds: root.settings !== null && root.settings.screenOffSeconds !== undefined ? Number(root.settings.screenOffSeconds) : 0
    readonly property bool overlayToasts: root.settings !== null && root.settings.overlayToasts === true

    property bool overlay: root.bridge !== null
    property bool overlayMapped: root.bridge !== null ? root.bridge.mapped : true
    property real overlayFade: root.bridge !== null ? root.bridge.opacity : 1
    property bool restoring: false
    property bool blackFrame: false
    property bool pendingHide: false
    property bool rootHidden: false
    property bool toastShown: false
    property var lastToastId: null
    property string addSource: ""
    readonly property bool hidden: root.overlay && !root.overlayMapped
    readonly property bool off: root.powerScreenOff || root.hidden
    readonly property bool saverArmed: !root.overlay && !root.off && !root.screenshot && root.playing
        && root.saverSeconds > 0 && !saver.shown && !switchPrompt.shown && !powerMenu.shown
    readonly property bool screenOffArmed: !root.overlay && !root.off && !root.screenshot && root.power !== null
        && root.screenOffSeconds > 0 && !root.switching
    property alias pageStack: pages
    property alias overlayLayer: overlays
    property alias background: bg
    property alias switchPrompt: switchPrompt
    property alias powerMenu: powerMenu
    property alias saver: saver
    property alias router: router
    property alias saverTimer: saverTimer
    property alias screenOffTimer: screenOffTimer
    property alias toastWindow: toastWindow
    property alias hiddenToast: hiddenToast

    signal hideOverlayRequested()

    width: shotWidth
    height: shotHeight
    visible: !root.overlay
    visibility: screenshot ? Window.Windowed : root.overlay ? Window.AutomaticVisibility : Window.FullScreen
    color: root.blackFrame ? Ui.black : Ui.bg

    onWidthChanged: Ui.windowW = root.width
    onHeightChanged: Ui.windowH = root.height
    onHiddenChanged: {
        if (!root.hidden)
            return
        pages.completeAll()
        overlays.completeAll()
        switchPrompt.complete()
        powerMenu.complete()
        saver.land()
    }
    onPowerScreenOffChanged: {
        if (root.powerScreenOff) {
            pages.completeAll()
            overlays.completeAll()
            switchPrompt.complete()
            powerMenu.complete()
            saver.hide()
            saver.land()
            root.pendingHide = true
            root.blackFrame = true
            return
        }
        root.pendingHide = false
        root.blackFrame = false
        root.rootHidden = false
    }
    onFrameSwapped: {
        if (!root.pendingHide)
            return
        root.pendingHide = false
        root.rootHidden = true
        if (typeof root.releaseResources === "function")
            root.releaseResources()
    }
    onSaverArmedChanged: root.armSaver()
    onScreenOffArmedChanged: root.armScreenOff()
    Component.onCompleted: {
        Ui.windowW = root.width
        Ui.windowH = root.height
        root.bindAccent()
        root.applySettings()
        root.lastToastId = root.trackId(root.current)
        if (!root.screenshot)
            root.restoreStack(root.stateStore !== null && typeof root.stateStore.stack === "function" ? root.stateStore.stack() : [])
        root.armSaver()
        root.armScreenOff()
    }

    function applySettings() {
        if (root.settings === null)
            return
        if (root.settings.scale !== undefined)
            Ui.userScale = Math.max(Ui.sMin, Math.min(Ui.sMax, Number(root.settings.scale)))
    }

    function bindAccent(): void {
        Ui.accent = Qt.binding(() => root.accentColor)
        Ui.accentBright = Qt.binding(() => root.accentBrightColor)
    }

    function trackId(map: var): var {
        if (map === undefined || map === null || map.id === undefined)
            return null
        return map.id
    }

    function field(name: string): string {
        const v = root.current[name]
        return v === undefined || v === null ? "" : String(v)
    }

    function armSaver(): void {
        if (root.saverArmed)
            saverTimer.restart()
        else
            saverTimer.stop()
    }

    function armScreenOff(): void {
        if (root.screenOffArmed)
            screenOffTimer.restart()
        else
            screenOffTimer.stop()
    }

    function touch(): void {
        if (saverTimer.running)
            saverTimer.restart()
        if (screenOffTimer.running)
            screenOffTimer.restart()
    }

    function requestScreenOff(): void {
        if (root.power === null || typeof root.power.runVerb !== "function") {
            console.info("casdap: screen off timer, no power bridge")
            return
        }
        root.power.runVerb("screen-off")
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
        case "eq":
            pages.push(eqComponent, {})
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

    function hasInfo(method: string, id: int, key: string): bool {
        if (root.library === null || typeof root.library[method] !== "function")
            return false
        const info = root.library[method](id)
        return info !== undefined && info !== null && info[key] !== undefined && info[key] !== null
    }

    function pushNamed(name: string, id: int): bool {
        switch (name) {
        case "music":
        case "bluetooth":
        case "playlists":
        case "history":
        case "settings":
        case "eq":
            root.navigate(name)
            return true
        case "artist":
            if (!root.hasInfo("artistInfo", id, "name"))
                return false
            root.openArtist(id)
            return true
        case "album":
            if (!root.hasInfo("albumInfo", id, "title"))
                return false
            root.openAlbum(id)
            return true
        case "nowplaying":
            if (!root.hasTrack && !root.phoneActive)
                return false
            root.openNowPlaying()
            return true
        case "queue":
            root.openQueue()
            return true
        default:
            return false
        }
    }

    function restoreStack(list: var): void {
        const animate = pages.animate
        pages.animate = false
        root.restoring = true
        pages.clear()
        pages.push(homeComponent)
        const entries = list === undefined || list === null ? [] : list
        for (let i = 0; i < entries.length; i++) {
            const e = entries[i]
            if (e === undefined || e === null)
                break
            const name = String(e.name)
            if (i === 0 && name === "home")
                continue
            if (!root.pushNamed(name, Number(e.id)))
                break
        }
        root.restoring = false
        pages.animate = animate
        root.syncStack()
    }

    function syncStack(): void {
        if (root.restoring || root.stateStore === null || typeof root.stateStore.setStack !== "function")
            return
        root.stateStore.setStack(pages.serialize())
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
        switch (action) {
        case "pin to quickplay":
        case "unpin from quickplay":
            root.setPinned(kind, id, action === "pin to quickplay")
            return
        case "play":
            root.playEntry({ kind: kind, id: id })
            return
        case "shuffle":
            root.playSource(kind, id, "shuffle", -1)
            return
        case "add to queue":
        case "play next":
            root.addTracks(action === "play next", kind, id)
            return
        default:
            console.info("casdap: " + action + " " + kind + " " + id)
            return
        }
    }

    function str(value: var): string {
        return value === undefined || value === null ? "" : String(value)
    }

    function callLibrary(method: string, arg: var): var {
        if (root.library === null || typeof root.library[method] !== "function")
            return undefined
        return root.library[method](arg)
    }

    function playSource(kind: string, id: int, mode: string, startId: int): int {
        if (root.player === null || typeof root.player.playSource !== "function") {
            console.info("casdap: " + mode + " " + kind + " " + id)
            return 0
        }
        const n = Number(root.player.playSource(kind, id, mode, startId))
        return isNaN(n) ? 0 : n
    }

    function modelTitle(model: var, id: int): string {
        if (model === null || model === undefined || typeof model.get !== "function")
            return ""
        if (typeof model.ensureLoaded === "function")
            model.ensureLoaded()
        const n = typeof model.count === "number" ? model.count
            : typeof model.rowCount === "function" ? model.rowCount() : 0
        for (let i = 0; i < n; i++) {
            const e = model.get(i)
            if (e === undefined || e === null || Number(e.id) !== id)
                continue
            return e.title !== undefined && e.title !== null ? String(e.title) : root.str(e.name)
        }
        return ""
    }

    function infoField(method: string, id: int, key: string): string {
        const info = root.callLibrary(method, id)
        return info === undefined || info === null ? "" : root.str(info[key])
    }

    function sourceName(kind: string, id: int): string {
        switch (kind) {
        case "album":
            return root.infoField("albumInfo", id, "title")
        case "artist":
            return root.infoField("artistInfo", id, "name")
        case "playlist":
            return root.infoField("playlistInfo", id, "name")
        case "genre":
            return root.modelTitle(root.genres, id)
        case "song": {
            const rows = root.callLibrary("trackRows", [id])
            return rows === undefined || rows === null || rows.length === 0 ? "" : root.str(rows[0].title)
        }
        default:
            return ""
        }
    }

    function addTracks(next: bool, kind: string, id: int): int {
        root.addSource = root.sourceName(kind, id)
        const added = root.playSource(kind, id, next ? "next" : "add", -1)
        root.addSource = ""
        return added
    }

    function shuffleAll(): void {
        if (root.playSource("all", 0, "shuffle", -1) === 0)
            console.info("casdap: shuffle all, empty library")
    }

    function playEntry(entry: var): void {
        const kind = entry.kind === undefined || entry.kind === null ? "" : String(entry.kind)
        const id = Number(entry.id)
        if (root.playSource(kind, id, "play", -1) === 0)
            console.info("casdap: play " + kind + " " + id)
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
        if (powerMenu.shown)
            powerMenu.close()
        if (saver.shown)
            saver.hide()
        switchPrompt.show(root.playing)
    }

    function showPowerMenu() {
        if (root.off) {
            console.info("casdap: ShowPowerMenu ignored, screen off")
            return
        }
        if (root.overlay) {
            console.info("casdap: ShowPowerMenu ignored in overlay mode")
            return
        }
        root.closeSheets()
        if (switchPrompt.shown)
            switchPrompt.close()
        if (saver.shown)
            saver.hide()
        powerMenu.open()
    }

    function switchFailed() {
        if (switchPrompt.shown)
            switchPrompt.close()
        overlays.toast.show("", "switch failed")
    }

    function hideOverlay(): void {
        root.hideOverlayRequested()
        if (root.bridge !== null && typeof root.bridge.hide === "function")
            root.bridge.hide()
    }

    function showHiddenToast(artist: string, title: string): void {
        if (root.bridge !== null && typeof root.bridge.prepareToast === "function" && !root.bridge.prepareToast())
            return
        root.toastShown = true
        hiddenToast.show(artist, title)
    }

    function endHiddenToast(): void {
        root.toastShown = false
        hiddenToast.hide()
        if (root.bridge !== null && typeof root.bridge.releaseToast === "function")
            root.bridge.releaseToast()
    }

    function trackChangedWhileHidden(): void {
        const id = root.trackId(root.current)
        if (id === root.lastToastId)
            return
        root.lastToastId = id
        if (id === null || !root.hidden || !root.overlayToasts)
            return
        root.showHiddenToast(root.field("artist"), root.field("title"))
    }

    Connections {
        target: root.power
        ignoreUnknownSignals: true

        function onShowSwitchPrompt() {
            root.showSwitchPrompt()
        }

        function onShowPowerMenu() {
            root.showPowerMenu()
        }

        function onSwitchFailed() {
            root.switchFailed()
        }
    }

    Connections {
        target: root.player
        ignoreUnknownSignals: true

        function onCurrentChanged() {
            root.trackChangedWhileHidden()
        }

        function onAdded(count) {
            overlays.showAdded(Number(count), root.addSource)
        }
    }

    Connections {
        target: pages

        function onDepthChanged() {
            root.syncStack()
        }
    }

    Connections {
        target: router

        function onRouted() {
            root.touch()
        }
    }

    Binding {
        target: root.contentItem
        property: "opacity"
        value: root.overlayFade
        when: root.overlay
    }

    Binding {
        target: root.albumsGrid
        property: "columns"
        value: Ui.gridCols
        when: root.albumsGrid !== null
    }

    Timer {
        id: saverTimer

        interval: Math.max(1, root.saverSeconds) * 1000
        repeat: false
        onTriggered: saver.show()
    }

    Timer {
        id: screenOffTimer

        interval: Math.max(1, root.screenOffSeconds) * 1000
        repeat: false
        onTriggered: root.requestScreenOff()
    }

    Rectangle {
        id: bg

        anchors.fill: parent
        color: root.blackFrame ? Ui.black : Ui.bg
    }

    PageStack {
        id: pages

        anchors.fill: parent
        visible: !root.screenshot && !root.hidden && !root.rootHidden
        opacity: 1 - saver.progress
        dialogFade: Math.max(switchPrompt.scrim.opacity, powerMenu.scrim.opacity)
    }

    Saver {
        id: saver

        anchors.fill: parent
        screenOff: root.off
        title: root.field("title")
        artist: root.field("artist")
        album: root.field("album")
        artPath: root.field("artPath")
    }

    PadRouter {
        id: router

        anchors.fill: parent
        stack: pages
        modal: powerMenu.shown ? powerMenu : switchPrompt
        saver: saver
        pad: typeof Pad === "undefined" ? null : Pad
        screenOff: root.off
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

    PowerMenu {
        id: powerMenu

        anchors.fill: parent
        screenOff: root.off
        onVerb: (word) => {
            if (root.power !== null && typeof root.power.runVerb === "function")
                root.power.runVerb(word)
            else
                console.info("casdap: power menu " + word)
        }
        onGameMode: root.showSwitchPrompt()
    }

    OverlayLayer {
        id: overlays

        anchors.fill: parent
        visible: !root.screenshot && !root.hidden && !root.rootHidden
        power: root.power
        bluez: root.screenshot ? null : root.bluez
        player: root.player
        screenOff: root.off
        topScreen: root.topScreen
    }

    Window {
        id: toastWindow

        x: 0
        y: 0
        width: root.width
        height: Ui.px(Ui.toastHeight)
        flags: Qt.FramelessWindowHint
        transientParent: null
        color: Ui.black
        visible: root.toastShown && !root.screenshot

        Toast {
            id: hiddenToast

            x: 0
            y: 0
            width: toastWindow.width
            screenOff: false
            onPhaseChanged: {
                if (hiddenToast.phase === "hidden" && root.toastShown)
                    root.endHiddenToast()
            }
        }
    }

    Component {
        id: homeComponent

        Home {
            objectName: "home"
            animated: !root.hidden
            overlay: root.overlay
            bluez: root.bluez
            historyModel: root.quickplayRecent
            newModel: root.quickplayNew
            pinsModel: root.pins
            playlistCount: root.playlistCount
            onNavigate: (name) => root.navigate(name)
            onNowPlayingBluetooth: root.openNowPlaying()
            onPlayRequested: (entry) => root.playEntry(entry)
            onPinRequested: (entry, pin) => root.setPinned(entry.kind === undefined ? "" : String(entry.kind), Number(entry.id), pin)
            onShuffleAll: root.shuffleAll()
            onHideRequested: root.hideOverlay()
        }
    }

    Component {
        id: bluetoothComponent

        Bluetooth {
            objectName: "bluetooth"
            animated: !root.hidden
            overlay: root.overlay
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
            artistsModel: root.artistsHub
            albumsModel: root.albumsGrid
            songsModel: root.songsHub
            genresModel: root.genresHub
            playlistsModel: root.playlists
            player: root.player
            library: root.library
            onOpen: (kind, id) => root.openEntry(kind, id)
            onNowPlaying: root.openNowPlaying()
            onPlay: (kind, id) => root.playEntry({ kind: kind, id: id })
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
            onPlay: (kind, id) => root.playEntry({ kind: kind, id: id })
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
            onPlay: (kind, id) => root.playEntry({ kind: kind, id: id })
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
        }
    }

    Component {
        id: settingsComponent

        SettingsScreen {
            objectName: "settings"
            animated: !root.hidden
            screenOff: root.off
            folders: root.settings !== null && typeof root.settings.candidateRoots === "function" ? root.settings.candidateRoots() : []
            overlay: root.overlay
            settings: root.settings
            power: root.power
            player: root.player
            audioInfo: root.audioInfo
            outputControl: root.outputControl
            codecFragment: root.codecFragment
            library: root.library
            rgbControl: root.rgbControl
            bridge: root.bridge
            eq: root.eq
            onPairPhone: root.openPairing()
            onOpenEq: root.navigate("eq")
        }
    }

    Component {
        id: eqComponent

        EqScreen {
            objectName: "eq"
            animated: !root.hidden
            screenOff: root.off
            eq: root.eq
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
            onOpenQueue: root.openQueue()
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
            onOpenQueue: root.openQueue()
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
            : root.shotScreen === "power" ? powerShotComponent
            : root.shotScreen === "saver" ? saverShotComponent
            : root.shotScreen === "saver-dim" ? saverDimShotComponent
            : root.shotScreen === "hub" ? hubShotComponent
            : root.shotScreen === "nowplaying" ? nowPlayingShotComponent
            : root.shotScreen === "queue" ? queueShotComponent
            : root.shotScreen === "artist" ? artistShotComponent
            : root.shotScreen === "album" ? albumShotComponent
            : root.shotScreen === "settings" ? settingsShotComponent
            : root.shotScreen === "jump" ? jumpShotComponent
            : root.shotScreen === "playlists" ? playlistsShotComponent
            : root.shotScreen === "history" ? historyShotComponent
            : root.shotScreen === "eq" ? eqShotComponent
            : null
    }

    Component {
        id: eqShotComponent
        EqShot {}
    }

    Component {
        id: settingsShotComponent
        SettingsShot {
            overlay: root.shotOverlay
        }
    }

    Component {
        id: powerShotComponent
        PowerShot {}
    }

    Component {
        id: saverShotComponent
        SaverShot {}
    }

    Component {
        id: saverDimShotComponent
        SaverShot {
            dim: true
        }
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
        HomeShot {
            overlay: root.shotOverlay
        }
    }

    Component {
        id: quickplayShotComponent
        HomeShot {
            quickplay: true
        }
    }
}
