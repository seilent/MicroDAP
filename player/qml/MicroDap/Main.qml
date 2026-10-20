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
    property string shotOrientation: "portrait"
    property string shotState: "browse"

    property var bluez: typeof BluezModel === "undefined" ? null : BluezModel
    readonly property var historyRecent: typeof HistoryRecent === "undefined" ? null : HistoryRecent
    readonly property var historyNew: typeof HistoryNew === "undefined" ? null : HistoryNew
    readonly property var quickplayRecent: typeof QuickplayRecent === "undefined" ? null : QuickplayRecent
    readonly property var quickplayNew: typeof QuickplayNew === "undefined" ? null : QuickplayNew
    readonly property var artistsHub: typeof ArtistsHub === "undefined" ? null : ArtistsHub
    readonly property var albumsGrid: typeof AlbumsGrid === "undefined" ? null : AlbumsGrid
    readonly property var songsHub: typeof SongsHub === "undefined" ? null : SongsHub
    readonly property var genresHub: typeof GenresHub === "undefined" ? null : GenresHub
    readonly property var foldersHub: typeof FoldersHub === "undefined" ? null : FoldersHub
    readonly property var genres: typeof Genres === "undefined" ? null : Genres
    readonly property var pins: typeof Pins === "undefined" ? null : Pins
    readonly property var playlists: typeof Playlists === "undefined" ? null : Playlists
    readonly property var favorites: typeof Favorites === "undefined" ? null : Favorites
    property var library: root.screenshot || typeof Library === "undefined" ? null : Library
    property var settings: root.screenshot || typeof Settings === "undefined" ? null : Settings
    property var audioInfo: root.screenshot || typeof AudioInfo === "undefined" ? null : AudioInfo
    property var outputControl: root.screenshot || typeof OutputControl === "undefined" ? null : OutputControl
    property var outputMode: root.screenshot || typeof OutputMode === "undefined" ? null : OutputMode
    property var dacEq: root.screenshot || typeof DacEq === "undefined" ? null : DacEq
    property var link: root.screenshot || typeof Link === "undefined" ? null : Link
    property var btOffer: root.screenshot || typeof BtOffer === "undefined" ? null : BtOffer
    property var photos: root.screenshot || typeof ArtistPhotos === "undefined" ? null : ArtistPhotos
    readonly property bool photoOn: root.settings === null || root.settings.artistPhoto !== false
    property var rgbControl: root.screenshot || typeof RgbControl === "undefined" ? null : RgbControl
    property var accentControl: root.screenshot || typeof Accent === "undefined" ? null : Accent
    property var outputs: root.shotState === "orientation-external" ? externalShotOutputs
        : typeof Outputs === "undefined" ? null : Outputs
    readonly property bool internalOutput: root.outputs === null || root.outputs.internal !== false
    property var sensor: typeof Sensor === "undefined" ? null : Sensor
    readonly property bool autoRotate: root.settings !== null && root.settings.autoRotate === true
    readonly property bool autoFollow: root.autoRotate && root.sensor !== null && root.sensor.supported === true
    readonly property bool sensorWanted: root.autoRotate && Ui.orientationActive && !root.off
    readonly property bool settingsEditing: pages.currentItem !== null && pages.currentItem.objectName === "settings"
        && (pages.currentItem.editing === true || pages.currentItem.valueOpen === true)
    property int sensorTurn: -1
    property int pinnedTurn: -1
    property string pinnedRaw: ""
    readonly property var orientations: ["portrait", "portrait-flipped", "landscape", "landscape-flipped"]
    readonly property string savedOrientation: root.settings !== null && root.orientations.indexOf(String(root.settings.orientation)) >= 0
        ? String(root.settings.orientation) : root.orientations.indexOf(root.shotOrientation) >= 0 ? root.shotOrientation : "portrait"
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
    property var lyrics: root.screenshot || typeof Lyrics === "undefined" ? null : Lyrics
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
    readonly property bool phonePlaying: root.phoneActive && root.bluez.status === "playing"
    readonly property bool overlayOpen: keyboard.shown || root.overlayIn(pages.currentItem)
    readonly property int saverSeconds: root.settings !== null && root.settings.saverSeconds !== undefined ? Number(root.settings.saverSeconds) : 0
    readonly property int screenOffSeconds: root.settings !== null && root.settings.screenOffSeconds !== undefined ? Number(root.settings.screenOffSeconds) : 0
    readonly property bool overlayToasts: root.settings !== null && root.settings.overlayToasts === true
    readonly property bool saverLyrics: root.settings !== null && root.settings.saverLyrics === true

    property bool overlay: root.bridge !== null
    property bool overlayMapped: root.bridge !== null ? root.bridge.mapped : true
    property real overlayFade: root.bridge !== null ? root.bridge.opacity : 1
    property bool restoring: false
    property bool surfaceSynced: false
    property bool firstFrameLogged: false
    property bool blackFrame: false
    property bool pendingHide: false
    property bool rootHidden: false
    property bool toastShown: false
    property bool rotateMotion: Env.rotateMotion
    property bool sweeping: false
    property bool sweepSwitched: false
    property int sweepD: 0
    property int sweepTurn: 0
    property bool sweepActive: false
    property real sweepW: 0
    property real sweepH: 0
    property real sweepOut: 0
    property real sweepIn: 0
    readonly property real spin: !root.sweeping ? 0
        : !root.sweepSwitched ? root.sweepD * 90 * Motion.expIn1(root.sweepOut)
        : -root.sweepD * 90 * (1 - Motion.expOut1(root.sweepIn))
    readonly property real spinOpacity: !root.sweeping ? 1
        : !root.sweepSwitched ? 1 - Motion.sineOut(root.sweepOut)
        : Motion.sineOut(root.sweepIn)
    property var lastToastId: null
    property Item autoNowPage: null
    property string addSource: ""
    property var playlistPick: null
    readonly property bool hidden: root.overlay && !root.overlayMapped
    readonly property bool touchOn: !root.overlay && !root.screenshot && (root.settings === null || root.settings.touch !== false)
    readonly property bool off: root.powerScreenOff || root.hidden
    readonly property bool lyricsHold: root.playing && pages.currentItem !== null
        && pages.currentItem.objectName === "nowplaying" && pages.currentItem.lyricsShown === true
    readonly property bool saverArmed: !root.overlay && !root.off && !root.screenshot && (root.playing || root.phonePlaying)
        && root.saverSeconds > 0 && !saver.shown && !switchPrompt.shown && !powerMenu.shown && !root.lyricsHold
        && !root.overlayOpen
    readonly property bool screenOffArmed: !root.overlay && !root.off && !root.screenshot && root.power !== null
        && root.screenOffSeconds > 0 && !root.switching && !root.lyricsHold
    property alias rotor: rotor
    property alias pageStack: pages
    property alias overlayLayer: overlays
    property alias background: bg
    property alias switchPrompt: switchPrompt
    property alias powerMenu: powerMenu
    property alias keyboard: keyboard
    property alias saver: saver
    property alias router: router
    property alias saverTimer: saverTimer
    property alias screenOffTimer: screenOffTimer
    property alias toastWindow: toastWindow
    property alias hiddenToast: hiddenToast
    property alias touchLayer: touchLayer
    property alias sweep: sweep
    property alias cursorBlank: cursorBlank

    signal hideOverlayRequested()
    signal orientationApplied(int turn)

    width: shotWidth
    height: shotHeight
    visible: !root.overlay
    visibility: screenshot ? Window.Windowed : root.overlay ? Window.AutomaticVisibility : Window.FullScreen
    color: root.blackFrame ? Ui.black : Ui.bg

    onWidthChanged: Qt.callLater(root.syncSurface)
    onHeightChanged: Qt.callLater(root.syncSurface)
    onSavedOrientationChanged: Qt.callLater(root.syncSurface)
    onInternalOutputChanged: Qt.callLater(root.syncSurface)
    onSensorTurnChanged: Qt.callLater(root.syncSurface)
    onPinnedTurnChanged: Qt.callLater(root.syncSurface)
    onAutoFollowChanged: {
        if (!root.autoFollow) {
            root.sensorTurn = -1
            root.pinnedTurn = -1
            root.pinnedRaw = ""
        }
        root.takeSensorTurn()
        Qt.callLater(root.syncSurface)
    }
    onSensorWantedChanged: root.syncSensor()
    onSensorChanged: root.syncSensor()
    onSettingsEditingChanged: {
        if (!root.settingsEditing)
            root.takeSensorTurn()
    }
    onHiddenChanged: {
        if (!root.hidden)
            return
        root.landSweep()
        pages.completeAll()
        overlays.completeAll()
        switchPrompt.complete()
        powerMenu.complete()
        saver.land()
        keyboard.dismiss()
        keyboard.complete()
    }
    onPowerScreenOffChanged: {
        if (root.powerScreenOff) {
            root.landSweep()
            pages.completeAll()
            overlays.completeAll()
            switchPrompt.complete()
            powerMenu.complete()
            saver.hide()
            saver.land()
            keyboard.dismiss()
            keyboard.complete()
            root.pendingHide = true
            root.blackFrame = true
            return
        }
        if (root.outputs !== null && typeof root.outputs.refresh === "function")
            root.outputs.refresh()
        root.pendingHide = false
        root.blackFrame = false
        root.rootHidden = false
    }
    onFrameSwapped: {
        if (!root.firstFrameLogged) {
            root.firstFrameLogged = true
            console.info("microdap: first frame turn " + Ui.turn)
        }
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
        root.syncSurface()
        root.syncSensor()
        root.bindAccent()
        root.lastToastId = root.trackId(root.current)
        if (!root.screenshot)
            root.restoreStack(root.stateStore !== null && typeof root.stateStore.stack === "function" ? root.stateStore.stack() : [])
        root.armSaver()
        root.armScreenOff()
    }

    function turnFor(name: string): int {
        switch (name) {
        case "portrait-flipped":
            return 2
        case "landscape":
            return Ui.landscapeTurn
        case "landscape-flipped":
            return (Ui.landscapeTurn + 2) % 4
        default:
            return 0
        }
    }

    function requestedTurn(): int {
        if (root.autoFollow && root.pinnedTurn >= 0)
            return root.pinnedTurn
        if (root.autoFollow && root.sensorTurn >= 0)
            return root.sensorTurn
        return root.turnFor(root.savedOrientation)
    }

    function syncSensor(): void {
        if (root.sensor !== null && typeof root.sensor.setWanted === "function")
            root.sensor.setWanted(root.sensorWanted, root.autoRotate)
    }

    function takeSensorTurn(): void {
        const turn = root.autoFollow && root.sensor.turn !== undefined ? Number(root.sensor.turn) : -1
        if (turn < 0 || root.settingsEditing)
            return
        if (root.pinnedTurn >= 0 && String(root.sensor.settledRaw) !== root.pinnedRaw) {
            root.pinnedTurn = -1
            root.pinnedRaw = ""
        }
        root.sensorTurn = turn
    }

    function syncSurface(): void {
        const active = !root.overlay && !root.shotOverlay && root.height > root.width && root.internalOutput
        const turn = active ? root.requestedTurn() : 0
        const landed = root.sweeping
        if (landed) {
            if (active === root.sweepActive && root.width === root.sweepW && root.height === root.sweepH)
                return
            root.landSweep()
        }
        if (root.surfaceSynced && (turn !== Ui.turn || active !== Ui.orientationActive)) {
            if (!landed && root.sweepWanted(turn, active))
                root.startSweep(turn, active)
            else
                root.applyOrientation(turn, active)
            return
        }
        Ui.setSurface(root.width, root.height)
        Ui.turn = turn
        Ui.orientationActive = active
        root.surfaceSynced = true
    }

    function rotateBy(raw: string): void {
        root.touch()
        const d = Ui.padDirections.indexOf(raw)
        if (d < 0)
            return
        const turn = (Ui.landscapeTurn + d) % 4
        if (turn === root.requestedTurn())
            return
        const top = pages.currentItem
        if (top !== null && top.objectName === "settings" && typeof top.revertOrConfirmForRotation === "function")
            top.revertOrConfirmForRotation()
        if (root.autoFollow) {
            root.pinnedTurn = turn
            root.pinnedRaw = String(root.sensor.settledRaw)
            return
        }
        const name = root.orientations.find((value) => root.turnFor(value) === turn)
        if (root.settings !== null && root.settings.orientation !== undefined && name !== undefined)
            root.settings.orientation = name
    }

    function sweepWanted(turn: int, active: bool): bool {
        return root.rotateMotion && active && Ui.orientationActive && turn !== Ui.turn
            && root.width === Ui.surfaceW && root.height === Ui.surfaceH
            && root.visible && !root.off && !root.screenshot
    }

    function sweepDelta(from: int, to: int): int {
        const d = ((to - from + 6) % 4) - 2
        return d === -2 ? 2 : d
    }

    function completeMotion(): void {
        pages.completeAll()
        pages.eachPage((page) => {
            if (typeof page.completeAll === "function")
                page.completeAll()
        })
        overlays.completeAll()
        switchPrompt.complete()
        powerMenu.complete()
        saver.land()
    }

    function startSweep(turn: int, active: bool): void {
        touchLayer.cancel()
        root.completeMotion()
        root.sweepD = root.sweepDelta(Ui.turn, turn)
        root.sweepTurn = turn
        root.sweepActive = active
        root.sweepW = root.width
        root.sweepH = root.height
        root.sweepOut = 0
        root.sweepIn = 0
        root.sweepSwitched = false
        root.sweeping = true
        sweep.restart()
    }

    function switchSweep(): void {
        if (!root.sweeping || root.sweepSwitched)
            return
        root.sweepSwitched = true
        root.applyOrientation(root.sweepTurn, root.sweepActive)
    }

    function endSweep(): void {
        root.sweeping = false
        root.sweepSwitched = false
        Qt.callLater(root.syncSurface)
    }

    function landSweep(): void {
        if (!root.sweeping)
            return
        sweep.stop()
        root.switchSweep()
        root.sweeping = false
        root.sweepSwitched = false
        Qt.callLater(root.syncSurface)
    }

    function applyOrientation(turn: int, active: bool): void {
        touchLayer.cancel()
        pages.eachPage((page) => {
            if (typeof page.revertOrConfirmForRotation === "function")
                page.revertOrConfirmForRotation()
        })
        root.completeMotion()
        Ui.turn = turn
        Ui.orientationActive = active
        Ui.setSurface(root.width, root.height)
        pages.eachPage((page) => {
            if (typeof page.keepFocusVisible === "function")
                page.keepFocusVisible()
        })
        root.orientationApplied(turn)
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
        if (root.power !== null && typeof root.power.noteInput === "function")
            root.power.noteInput()
    }

    function requestScreenOff(): void {
        if (root.power === null || typeof root.power.runScreenOff !== "function") {
            console.info("microdap: screen off timer, no power bridge")
            return
        }
        root.power.runScreenOff()
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
            console.info("microdap: navigate " + name)
            break
        }
    }

    function openPairing() {
        pages.push(bluetoothComponent, { pivotIndex: 1 })
    }

    function askWord(what: string, result: string, text: string): string {
        if (result === "busy" || result === "error 3")
            return "phone busy"
        if (result === "closed")
            return "phone app not linked"
        if (what === "pick") {
            if (result === "ok")
                return "pick on phone"
            if (result === "notified")
                return "see phone notification"
            if (result === "error 4" && text.trim() !== "")
                return text.trim()
            return "phone did not answer"
        }
        switch (result) {
        case "ok":
            return what === "wrong" ? "marked wrong artist" : "image blocked"
        case "error 7":
            return "photo changed"
        case "error 10":
            return "photo not on phone"
        case "error 4":
            return "wrong artist not offered"
        default:
            return "phone did not answer"
        }
    }

    function openPhoneApp() {
        pages.push(phoneAppComponent, {})
    }

    function openNowPlaying() {
        if (root.topScreen === "nowplaying")
            return
        if (!root.hasTrack && !root.phoneActive) {
            console.info("microdap: now playing empty")
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

    function openPlaylistView(kind: string, id: int): void {
        pages.push(playlistViewComponent, { kind: kind === "favorites" ? "favorites" : "playlist", playlistId: kind === "favorites" ? 0 : id })
    }

    function openEntry(kind: string, id: int): void {
        if (kind === "album")
            root.openAlbum(id)
        else if (kind === "artist")
            root.openArtist(id)
        else
            console.info("microdap: navigate " + kind + " " + id)
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
        case "playlistview":
            if (!root.hasInfo("playlistInfo", id, "name"))
                return false
            root.openPlaylistView("playlist", id)
            return true
        case "favoritesview":
            if (root.favorites === null)
                return false
            root.openPlaylistView("favorites", 0)
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
            console.info("microdap: " + verb + kind + " " + id)
            return
        }
        const ok = pin ? root.library.pin(kind, id) : root.library.unpin(kind, id)
        if (!ok)
            console.info("microdap: " + verb + kind + " " + id + " failed")
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
        case "add to playlist":
            root.openPlaylistPick(kind, id)
            return
        case "delete playlist":
            root.deletePlaylist(id)
            return
        case "open playlist":
            root.openPlaylistView(kind, id)
            return
        case "info":
            root.openTrackInfo(id)
            return
        default:
            console.info("microdap: " + action + " " + kind + " " + id)
            return
        }
    }

    function trackIdsFor(kind: string, id: int): var {
        if (kind === "song")
            return [id]
        if (kind === "folder")
            return root.foldersHub === null ? [] : root.foldersHub.tracks(id)
        return root.library === null || typeof root.library.sourceTracks !== "function" ? [] : root.library.sourceTracks(kind, id)
    }

    function playlistTargets(): var {
        const out = [{ kind: "new", id: -1, name: "new playlist" }]
        if (root.favorites !== null)
            out.push({ kind: "favorites", id: 0, name: "favorites" })
        const lists = root.library !== null && typeof root.library.editablePlaylists === "function"
            ? root.library.editablePlaylists() : []
        for (const p of lists)
            out.push({ kind: "playlist", id: Number(p.id), name: String(p.name) })
        return out
    }

    function openPlaylistPick(kind: string, id: int): void {
        const page = pages.currentItem
        if (page === null || page.sheet === undefined || page.sheet === null || !page.sheet.shown)
            return
        const targets = root.playlistTargets()
        root.playlistPick = { kind: kind, id: id, title: page.sheet.title, targets: targets }
        page.sheet.push(page.sheet.title, targets.map((t) => t.name))
    }

    function pickPlaylist(index: int): void {
        const pick = root.playlistPick
        root.playlistPick = null
        if (pick === null || index < 0 || index >= pick.targets.length)
            return
        const target = pick.targets[index]
        const ids = root.trackIdsFor(pick.kind, pick.id)
        if (ids.length === 0)
            return
        let error = ""
        let name = target.name
        if (target.kind === "favorites") {
            error = root.favorites === null || typeof root.favorites.add !== "function" ? "" : root.str(root.favorites.add(ids))
        } else if (target.kind === "new") {
            const made = root.library === null || typeof root.library.createPlaylist !== "function" ? null : root.library.createPlaylist(ids)
            if (made === null)
                return
            error = root.str(made.error)
            name = root.str(made.name)
        } else {
            error = root.library === null || typeof root.library.addToPlaylist !== "function" ? "" : root.str(root.library.addToPlaylist(target.id, ids))
        }
        if (error === "playlist not readable")
            overlays.toast.show(error, name)
        else if (error !== "")
            overlays.toast.show("", error)
        else
            overlays.toast.show("added to " + name, pick.title)
    }

    function openTrackInfo(id: int): void {
        const page = pages.currentItem
        if (page === null || page.sheet === undefined || page.sheet === null)
            return
        const info = root.callLibrary("trackInfo", id)
        if (info === undefined || info === null || info.title === undefined)
            return
        const parts = info.parts === undefined ? [] : info.parts
        const width = page.width - 2 * Ui.px(Ui.edge)
        const path = Env.fitPath(parts, page.sheet.plainFont, width)
        const lines = info.lines.concat(path === "" ? [] : [path])
        page.sheet.showLines(String(info.title), lines, path === "" ? -1 : lines.length - 1)
    }

    function deletePlaylist(id: int): void {
        if (root.library === null || typeof root.library.deletePlaylist !== "function")
            return
        const name = root.sourceName("playlist", id)
        const error = root.str(root.library.deletePlaylist(id))
        if (error === "playlist not readable")
            overlays.toast.show(error, name)
        else if (error !== "")
            overlays.toast.show("", error)
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
            console.info("microdap: " + mode + " " + kind + " " + id)
            return 0
        }
        if (kind === "folder") {
            if (root.foldersHub === null || typeof root.player.playIds !== "function")
                return 0
            const played = Number(root.player.playIds(root.foldersHub.tracks(id), mode, startId))
            return isNaN(played) ? 0 : played
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
        case "favorites":
            return "favorites"
        case "genre":
            return root.modelTitle(root.genres, id)
        case "folder":
            return root.foldersHub === null ? "" : root.str(root.foldersHub.name(id))
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
            console.info("microdap: shuffle all, empty library")
    }

    function listPlayed(count: int): void {
        root.autoNowPage = null
        if (count > 0 && root.settings !== null && root.settings.autoNowPlaying === true)
            root.autoNowPage = pages.currentItem
    }

    function playEntry(entry: var): void {
        const kind = entry.kind === undefined || entry.kind === null ? "" : String(entry.kind)
        const id = Number(entry.id)
        if (root.playSource(kind, id, "play", -1) === 0)
            console.info("microdap: play " + kind + " " + id)
    }

    function overlayIn(page: Item): bool {
        if (page === null || page === undefined)
            return false
        for (const name of ["sheet", "deleteDialog"]) {
            const item = page[name]
            if (item !== undefined && item !== null && (item.shown === true || item.animating === true))
                return true
        }
        return page.eqPage !== undefined && page.eqPage !== null && root.overlayIn(page.eqPage)
    }

    function phoneField(name: string): string {
        const v = root.bluez === null ? undefined : root.bluez[name]
        return v === undefined || v === null ? "" : String(v)
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
            console.info("microdap: ShowSwitchPrompt ignored, screen off")
            return
        }
        if (switchPrompt.shown) {
            switchPrompt.close()
            return
        }
        root.closeSheets()
        keyboard.dismiss()
        if (powerMenu.shown)
            powerMenu.close()
        if (saver.shown)
            saver.hide()
        switchPrompt.show(root.playing)
    }

    function showPowerMenu() {
        if (root.off) {
            console.info("microdap: ShowPowerMenu ignored, screen off")
            return
        }
        if (root.overlay) {
            console.info("microdap: ShowPowerMenu ignored in overlay mode")
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
        target: root.link
        ignoreUnknownSignals: true

        function onPhoneBound(name) {
            overlays.toast.show("phone app", name)
        }

        function onAskDone(what, result, name, text) {
            overlays.toast.show(root.askWord(what, result, text), name)
        }
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

        function onVerbFailed(verb) {
            if (verb === "screen-off")
                root.armScreenOff()
        }
    }

    Connections {
        target: typeof OutputMode === "undefined" ? null : OutputMode
        ignoreUnknownSignals: true

        function onNotice(text) {
            overlays.toast.show("", text)
        }
    }

    Connections {
        target: root.favorites
        ignoreUnknownSignals: true

        function onNotice(text) {
            overlays.toast.show("", text)
        }
    }

    Connections {
        target: root.player
        ignoreUnknownSignals: true

        function onNotice(text) {
            overlays.toast.show("", text)
        }

        function onTrackStarted() {
            const page = root.autoNowPage
            root.autoNowPage = null
            if (page !== null && pages.currentItem === page)
                root.openNowPlaying()
        }

        function onCurrentChanged() {
            root.trackChangedWhileHidden()
        }

        function onAdded(count) {
            overlays.showAdded(Number(count), root.addSource)
        }
    }

    Connections {
        target: root.eq
        ignoreUnknownSignals: true

        function onImportNote(first, second) {
            overlays.toast.show(first, second)
        }
    }

    Connections {
        target: root.sensor
        ignoreUnknownSignals: true

        function onTurnChanged() {
            root.takeSensorTurn()
        }
    }

    Connections {
        target: pages

        function onDepthChanged() {
            root.syncStack()
        }

        function onCurrentItemChanged() {
            root.autoNowPage = null
            keyboard.dismiss()
        }
    }

    Connections {
        target: root.bluez
        ignoreUnknownSignals: true

        function onWrongRole(device: string, pivot: string) {
            overlays.toast.show(device, "use the " + pivot + " pivot")
        }
    }

    Connections {
        target: router

        function onRouted() {
            root.touch()
        }

        function onDropped() {
            if (root.power !== null && typeof root.power.noteInput === "function")
                root.power.noteInput()
        }
    }

    Connections {
        target: saver

        function onShownChanged() {
            if (root.rgbControl !== null && typeof root.rgbControl.setSaver === "function")
                root.rgbControl.setSaver(saver.shown)
        }
    }

    Binding {
        target: root.contentItem
        property: "opacity"
        value: root.overlayFade
        when: root.overlay
    }

    Binding {
        target: Ui
        property: "userScale"
        value: root.settings !== null && root.settings.scale !== undefined
            ? Math.max(Ui.sMin, Math.min(Ui.sMax, Number(root.settings.scale))) : Ui.userScale
        when: root.settings !== null && root.settings.scale !== undefined
    }

    Binding {
        target: root.albumsGrid
        property: "columns"
        value: Ui.gridCols
        when: root.albumsGrid !== null
    }

    QtObject {
        id: externalShotOutputs

        readonly property bool internal: false
    }

    SequentialAnimation {
        id: sweep

        NumberAnimation {
            target: root
            property: "sweepOut"
            from: 0
            to: 1
            duration: Motion.rotateOut
            easing.type: Motion.linear
        }
        ScriptAction {
            script: root.switchSweep()
        }
        NumberAnimation {
            target: root
            property: "sweepIn"
            from: 0
            to: 1
            duration: Motion.rotateIn
            easing.type: Motion.linear
        }
        ScriptAction {
            script: root.endSweep()
        }
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

    Item {
        id: rotor

        width: Ui.windowW
        height: Ui.windowH
        x: Math.round((root.width - rotor.width) / 2)
        y: Math.round((root.height - rotor.height) / 2)
        rotation: Ui.turn * 90 + root.spin
        opacity: root.spinOpacity
        layer.enabled: root.sweeping
        layer.smooth: true

        HoverHandler {
            id: cursorBlank

            enabled: !root.overlay && !root.screenshot
            cursorShape: Qt.BlankCursor
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
            clockMode: root.settings !== null && root.settings.clock !== undefined ? String(root.settings.clock) : "24h"
            phone: root.phoneActive
            phoneArtUrl: root.phoneField("artUrl")
            title: root.phoneActive ? root.phoneField("title") : root.field("title")
            artist: root.phoneActive ? root.phoneField("artist") : root.field("artist")
            album: root.phoneActive ? (root.phoneField("deviceName") === "" ? "" : "from " + root.phoneField("deviceName")) : root.field("album")
            artPath: root.phoneActive ? "" : root.field("artPath")
            lyricsOn: root.saverLyrics && !root.phoneActive
            lyrics: root.lyrics
            player: root.player
        }

        PadRouter {
            id: router

            anchors.fill: parent
            stack: pages
            modal: powerMenu.shown ? powerMenu : switchPrompt.shown ? switchPrompt : keyboard.shown ? keyboard : switchPrompt
            saver: saver
            pad: typeof Pad === "undefined" ? null : Pad
            guide: typeof Guide === "undefined" ? null : Guide
            screenOff: root.off
            sweeping: root.sweeping
            onNowPlaying: root.openNowPlaying()
            onRotate: (raw) => root.rotateBy(raw)
        }

        Keyboard {
            id: keyboard

            anchors.fill: parent
            screenOff: root.off
            animated: !root.off && !root.hidden
            toastShown: overlays.toast.shown
            onRefused: (title, reason) => overlays.toast.show(title, reason)
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
            sessions: root.power !== null && root.power.sessions !== undefined ? root.power.sessions : []
            screenOff: root.off
            onVerb: (word) => {
                if (root.power !== null && typeof root.power.runVerb === "function")
                    root.power.runVerb(word)
                else
                    console.info("microdap: power menu " + word)
            }
            onGameMode: root.showSwitchPrompt()
            onSwitchTo: (session) => {
                if (root.power !== null && typeof root.power.switchTo === "function")
                    root.power.switchTo(session)
                else
                    console.info("microdap: power menu switch to " + session)
            }
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
                : root.shotScreen === "phoneapp" ? phoneAppShotComponent
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
                : root.shotScreen === "keyboard" ? keyboardShotComponent
                : null
        }

        TouchLayer {
            id: touchLayer

            anchors.fill: parent
            router: root.router
            dialogs: [switchPrompt, powerMenu, keyboard]
            enabled: root.touchOn && !root.off
            onTouched: root.touch()
        }
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
                if (hiddenToast.phase === "hidden" && hiddenToast.progress <= 0 && root.toastShown)
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
            quickplayOn: root.settings === null || root.settings.quickplay !== false
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
            eq: root.eq
            dacEq: root.dacEq
            keyboard: keyboard
            screenOff: root.off
            companion: root.settings !== null && root.settings.companion === true
            onOpenNowPlaying: root.openNowPlaying()
            onToast: (first, second) => overlays.toast.show(first, second)
        }
    }

    Component {
        id: phoneAppComponent

        PhoneAppPairing {
            objectName: "phoneapp"
            animated: !root.hidden
            bluez: root.bluez
            link: root.link
            screenOff: root.off
            onToast: (first, second) => overlays.toast.show(first, second)
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
            lyrics: root.lyrics
            photos: root.photos
            photoOn: root.photoOn
            link: root.link
            property var infoId: null

            onOpenQueue: root.openQueue()
            onTrackInfo: (id) => {
                infoId = id
                root.openTrackInfo(id)
            }
            onShownChanged: {
                if (infoId !== null && Number(shown.id) !== infoId && sheet.shown && sheet.lines)
                    sheet.close()
            }
        }
    }

    Component {
        id: queueComponent

        QueueScreen {
            objectName: "queue"
            animated: !root.hidden
            screenOff: root.off
            player: root.player
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
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
            favorites: root.favorites
            foldersModel: root.foldersHub
            player: root.player
            library: root.library
            onOpen: (kind, id) => root.openEntry(kind, id)
            onNowPlaying: root.openNowPlaying()
            onPlay: (kind, id) => root.playEntry({ kind: kind, id: id })
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
            onPlaylistPicked: (index) => root.pickPlaylist(index)
            onListPlayed: (count) => root.listPlayed(count)
        }
    }

    Component {
        id: playlistsComponent

        PlaylistsScreen {
            objectName: "playlists"
            animated: !root.hidden
            screenOff: root.off
            playlistsModel: root.playlists
            favorites: root.favorites
            player: root.player
            library: root.library
            onNowPlaying: root.openNowPlaying()
            onPlay: (kind, id) => root.playEntry({ kind: kind, id: id })
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
            onListPlayed: (count) => root.listPlayed(count)
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
            onPlaylistPicked: (index) => root.pickPlaylist(index)
            onListPlayed: (count) => root.listPlayed(count)
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
            btRole: root.bluez !== null && root.bluez.activeRole !== undefined ? String(root.bluez.activeRole) : "receiver"
            btApplied: root.btOffer !== null ? String(root.btOffer.applied) : "auto"
            settings: root.settings
            power: root.power
            player: root.player
            audioInfo: root.audioInfo
            outputControl: root.outputControl
            outputMode: root.outputMode
            dacEq: root.dacEq
            library: root.library
            rgbControl: root.rgbControl
            bridge: root.bridge
            eq: root.eq
            sensor: root.sensor
            onPairPhone: root.openPairing()
            onPairPhoneApp: root.openPhoneApp()
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
            dacEq: root.dacEq
            keyboard: keyboard
            onToast: (first, second) => overlays.toast.show(first, second)
        }
    }

    Component {
        id: artistComponent

        ArtistDetail {
            objectName: "artist"
            animated: !root.hidden
            screenOff: root.off
            library: root.library
            photos: root.photos
            photoOn: root.photoOn
            player: root.player
            onOpenAlbum: (id) => root.openAlbum(id)
            onOpenQueue: root.openQueue()
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
            onPlaylistPicked: (index) => root.pickPlaylist(index)
            onListPlayed: (count) => root.listPlayed(count)
        }
    }

    Component {
        id: albumComponent

        AlbumDetail {
            objectName: "album"
            animated: !root.hidden
            screenOff: root.off
            library: root.library
            photos: root.photos
            photoOn: root.photoOn
            player: root.player
            status: root.status
            bluez: root.bluez
            onOpenQueue: root.openQueue()
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
            onPlaylistPicked: (index) => root.pickPlaylist(index)
            onListPlayed: (count) => root.listPlayed(count)
        }
    }

    Component {
        id: playlistViewComponent

        PlaylistView {
            animated: !root.hidden
            screenOff: root.off
            library: root.library
            favorites: root.favorites
            player: root.player
            onSheetAction: (action, kind, id) => root.sheetAction(action, kind, id)
            onListPlayed: (count) => root.listPlayed(count)
            onRefused: (text, name) => {
                if (text === "playlist not readable")
                    overlays.toast.show(text, name)
                else
                    overlays.toast.show("", text)
            }
        }
    }

    Component {
        id: eqShotComponent
        EqShot {
            fixture: root.shotState
        }
    }

    Component {
        id: settingsShotComponent
        SettingsShot {
            overlay: root.shotOverlay
            orientation: root.savedOrientation
            fixture: root.shotState
        }
    }

    Component {
        id: powerShotComponent
        PowerShot {
            fixture: root.shotState
        }
    }

    Component {
        id: saverShotComponent
        SaverShot {
            fixture: root.shotState
        }
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
        PlaylistsShot {
            fixture: root.shotState
        }
    }

    Component {
        id: historyShotComponent
        HistoryShot {}
    }

    Component {
        id: artistShotComponent
        DetailShot {
            kind: "artist"
            fixture: root.shotState
        }
    }

    Component {
        id: albumShotComponent
        DetailShot {
            kind: "album"
            fixture: root.shotState
        }
    }

    Component {
        id: queueShotComponent
        QueueShot {
            fixture: root.shotState
        }
    }

    Component {
        id: nowPlayingShotComponent
        NowPlayingShot {
            fixture: root.shotState
        }
    }

    Component {
        id: hubShotComponent
        HubShot {
            pivot: root.shotPivot
            fixture: root.shotState
        }
    }

    Component {
        id: keyboardShotComponent
        KeyboardShot {
            fixture: root.shotState
        }
    }

    Component {
        id: promptShotComponent
        PromptShot {}
    }

    Component {
        id: bluetoothShotComponent
        BluetoothShot {
            fixture: root.shotState
        }
    }

    Component {
        id: phoneAppShotComponent
        PhoneAppShot {
            fixture: root.shotState
        }
    }

    Component {
        id: componentsComponent
        ComponentsShot {
            fixture: root.shotState
        }
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
            fixture: root.shotState
        }
    }

    Component {
        id: quickplayShotComponent
        HomeShot {
            quickplay: true
            fixture: root.shotState
        }
    }
}
