pragma ComponentBehavior: Bound
import QtQuick
import "GlyphPaths.js" as GlyphPaths

Page {
    id: np

    property QtObject player: null
    property QtObject bluez: null
    property QtObject status: null
    property QtObject lyrics: null
    property QtObject photos: null
    property QtObject link: null
    property bool photoOn: true
    property var artItems: []
    property var artNames: []
    property var artTarget: ({})
    property bool animated: true
    property bool screenOff: false
    property int focusRow: 0
    property int ringIndex: 0
    property bool idle: false
    property bool shownVariant: false
    property real trackShift: 0
    property real trackOpacity: 1
    property real artOpacity: 1
    property real artScale: 1
    property real contentOpacity: 1
    property real variantScale: 1
    property real localLayerOpacity: 1
    property bool lyricsMode: false
    property bool lyricsView: false
    property real lyricsShade: 0
    property var shown: ({ id: null, title: "", artist: "", album: "", artPath: "", rating: 0, artists: [] })
    property var pending: null
    property var nextTitles: []
    property real shownPosition: 0
    property real seekTo: -1
    property string elapsedText: "0:00"
    property string remainingText: ""

    readonly property bool splitFade: true
    readonly property bool live: np.animated && !np.screenOff
    readonly property bool variant: np.bluez !== null && np.bluez.transportActive === true
    readonly property int statusPlaying: 1
    readonly property int statusPaused: 2
    readonly property var current: np.player !== null && np.player.current !== undefined && np.player.current !== null ? np.player.current : ({})
    readonly property var queue: np.player !== null && np.player.queue !== undefined && np.player.queue !== null ? np.player.queue : null
    readonly property bool localPlaying: np.player !== null && np.player.status === np.statusPlaying
    readonly property bool localPaused: np.player !== null && np.player.status === np.statusPaused
    readonly property string phoneStatus: np.bluez !== null && np.bluez.status !== undefined ? np.bluez.status : ""
    readonly property bool phonePlaying: np.phoneStatus === "playing"
    readonly property bool playing: np.shownVariant ? np.phonePlaying : np.localPlaying
    readonly property bool paused: np.shownVariant ? np.phoneStatus === "paused" : np.localPaused
    readonly property bool phoneConnected: np.bluez !== null && np.bluez.connected === true
    readonly property string phoneName: np.bluez !== null && np.bluez.deviceName !== undefined ? np.bluez.deviceName : ""
    readonly property string phoneArt: np.bluez !== null && np.bluez.artUrl !== undefined && np.bluez.artUrl !== null ? np.bluez.artUrl : ""
    readonly property bool phoneArtShown: np.shownVariant && np.phoneArt !== "" && artImage.status !== Image.Error
    readonly property string fromLabelText: "from " + np.phoneName
    readonly property string artistText: np.shownVariant ? np.phoneField("artist") : np.shown.artist
    readonly property string albumText: np.shownVariant ? np.phoneField("album") : np.shown.album
    readonly property string titleText: np.shownVariant ? np.phoneField("title") : np.shown.title
    readonly property int trackNumber: np.bluez !== null && np.bluez.trackNumber !== undefined ? np.bluez.trackNumber : 0
    readonly property int numberOfTracks: np.bluez !== null && np.bluez.numberOfTracks !== undefined ? np.bluez.numberOfTracks : 0
    readonly property string countText: np.trackNumber > 0 && np.numberOfTracks > 0 ? np.trackNumber + " of " + np.numberOfTracks : ""
    readonly property real position: np.shownVariant
        ? (np.bluez.positionMs !== undefined ? np.bluez.positionMs : 0)
        : np.seekTo >= 0 ? np.seekTo : np.shownPosition
    readonly property real duration: np.shownVariant
        ? (np.bluez.durationMs !== undefined ? np.bluez.durationMs : 0)
        : (np.player !== null && np.player.duration !== undefined ? np.player.duration : 0)
    readonly property bool progressShown: !np.shownVariant || np.duration > 0
    readonly property real fraction: np.duration > 0 ? Math.max(0, Math.min(1, np.position / np.duration)) : 0
    readonly property bool shuffleOn: np.queue !== null && np.queue.shuffle === true
    readonly property int repeatMode: np.queue !== null && np.queue.repeat !== undefined ? np.queue.repeat : 0
    readonly property bool rated: np.shown.rating !== undefined && np.shown.rating > 0
    readonly property bool rateShown: np.shownVariant || np.shown.id === null || np.shown.id > 0
    readonly property int ringLast: np.rateShown ? Ui.npRingX.length - 1 : Ui.npRingX.length - 2
    readonly property string phoneShuffle: np.bluez !== null && np.bluez.shuffle !== undefined ? np.bluez.shuffle : "off"
    readonly property string phoneRepeat: np.bluez !== null && np.bluez.repeat !== undefined ? np.bluez.repeat : "off"
    readonly property var ringGlyphs: np.shownVariant
        ? [np.phonePlaying ? "pause" : "play", "shuffle", np.phoneRepeat === "singletrack" ? "repeatOne" : "repeat"]
        : ["shuffle", np.repeatMode === 2 ? "repeatOne" : "repeat", "heart"]
    readonly property var ringOn: np.shownVariant
        ? [false, np.phoneShuffle !== "off", np.phoneRepeat !== "off"]
        : [np.shuffleOn, np.repeatMode !== 0, np.rated]
    readonly property var ringToggle: np.shownVariant ? [false, true, true] : [true, true, true]
    readonly property int battery: np.status !== null && np.status.battery !== undefined ? np.status.battery : -1
    readonly property bool batteryPresent: np.status !== null && np.status.batteryPresent === true
    readonly property string clock: np.status !== null && np.status.clock !== undefined ? np.status.clock : ""
    readonly property bool batteryPercent: np.status !== null && np.status.batteryPercent === true
    readonly property real idleAlpha: np.idle ? Motion.idleAlpha : 1
    readonly property real edgeX: Ui.px(Ui.edge)
    readonly property real artSide: Ui.px(Ui.npArt)
    readonly property real artX: np.edgeX
    readonly property real artY: Ui.px(Ui.landscape ? Ui.npArtTopLandscape : Ui.npArtTop)
    readonly property real textX: Ui.px(Ui.landscape ? Ui.npColumnRight : Ui.edge)
    readonly property real artistBaseline: Ui.px(Ui.landscape ? Ui.npArtistBaselineLandscape : Ui.npTextTop)
    readonly property real albumBaseline: Ui.px(Ui.landscape ? Ui.npAlbumBaselineLandscape : Ui.npAlbumBaseline)
    readonly property real fromBaseline: Ui.px(Ui.landscape ? Ui.npFromBaselineLandscape : Ui.npFromBaseline)
    readonly property real titleBaseline: Ui.px(Ui.landscape ? Ui.npTitleBaselineLandscape : Ui.npTitleY)
    readonly property real nextBaseline: Ui.px(Ui.landscape ? Ui.npNextBaselineLandscape : Ui.npTitleY + Ui.npNextPitch)
    readonly property real progressY: Ui.px(Ui.landscape
        ? Ui.npArtTopLandscape + Ui.npArt + Ui.npProgressGapLandscape
        : Ui.npProgressY)
    readonly property real progressWidth: np.artSide
    readonly property real timeBaseline: np.progressY + Ui.px(Ui.npTimeDrop)
    readonly property real ringY: Ui.px(Ui.npRingY)
    readonly property real titleBottom: np.nextBaseline + Ui.px(Ui.npNextPitch) * (Ui.npNextCount - 1)
    readonly property Item holder: np.parent
    readonly property bool fadingPage: np.holder !== null && np.holder.kind === "fade" && np.stack !== null
        && np.stack.phase !== "idle" && (np.stack.incoming === np.holder || np.stack.outgoing === np.holder)
    readonly property real pageProgress: np.holder !== null && np.holder.progress !== undefined ? np.holder.progress : 1
    readonly property real backdropFade: np.holder !== null && np.holder.backdropFade !== undefined ? np.holder.backdropFade : 1
    readonly property real contentFade: np.holder !== null && np.holder.contentFade !== undefined ? np.holder.contentFade : 1
    readonly property real contentScale: !np.fadingPage ? np.variantScale
        : Motion.lerp(Motion.nowPlayingScale, 1, np.contentFade)
    readonly property bool trackAnimating: trackAnim.running
    readonly property bool lyricsShown: np.lyricsView && !np.shownVariant
    readonly property bool lyricsMatch: np.lyrics !== null && !np.shownVariant && np.shown.id !== null
        && np.shown.id === np.trackId(np.current)
    readonly property bool lyricsPending: np.lyricsMatch && np.lyrics.pending === true
    readonly property bool hasLyrics: np.lyricsMatch && !np.lyricsPending && (d.lines.length > 0 || d.plain.length > 0)
    readonly property bool lyricsToggling: lyricsFade.running
    readonly property bool markShown: !np.shownVariant && !np.lyricsShown && np.hasLyrics
    readonly property real markBox: Ui.capHeight * Ui.npNext.size * GlyphPaths.box
        / (GlyphPaths.micInk.bottom - GlyphPaths.micInk.top)
    readonly property real markRight: Ui.px(Ui.canvasW - Ui.edge)
    readonly property real markLeft: np.markRight
        - Ui.px(np.markBox) * (GlyphPaths.micInk.right - GlyphPaths.micInk.left) / GlyphPaths.box
    readonly property real lyricsTop: Ui.landscape ? Ui.npArtistBaselineLandscape : Ui.npTextTop
    readonly property real lyricsBottom: (Ui.landscape ? Ui.npNextBaselineLandscape : Ui.npTitleY + Ui.npNextPitch)
        + Ui.npNextPitch * (Ui.npNextCount - 1)
    readonly property bool variantAnimating: variantIn.running || variantOut.running

    property alias surface: surface
    property alias backdrop: backdrop
    property alias localLayer: localLayer
    property alias content: content
    property alias backRing: backRing
    property alias statusStrip: statusStrip
    property alias artItem: artItem
    property alias btGlyph: btGlyph
    property alias artTile: artTile
    property alias artImage: artImage
    property alias fromLabel: fromLabel
    property alias artistItem: artistItem
    property alias albumItem: albumItem
    property alias progressItem: progressItem
    property alias progressFill: progressFill
    property alias seekBand: seekBand
    property alias elapsedItem: elapsedItem
    property alias remainingItem: remainingItem
    property alias titleItem: titleItem
    property alias countItem: countItem
    property alias nextRepeater: nextRepeater
    property alias ringRepeater: ringRepeater
    property alias focusBar: focusBar
    property alias idleTimer: idleTimer
    property alias trackAnimation: trackAnim
    property alias variantInAnimation: variantIn
    property alias variantOutAnimation: variantOut
    property alias sheet: sheet
    property alias lyricsColumn: lyricsColumn
    property alias lyricsShadeItem: shade
    property alias lyricsFadeAnimation: lyricsFade
    property alias lyricsSlideAnimation: slideAnim
    property alias lyricsDueTimer: dueTimer
    property alias lyricsMark: lyricsMark

    signal openQueue()
    signal trackInfo(int id)

    focusCount: 0

    function artistsFor(variant: bool): var {
        if (!variant)
            return np.shown.artists !== undefined ? np.shown.artists : []
        const text = np.phoneField("artist")
        if (text === "")
            return []
        return np.photos !== null && typeof np.photos.splitArtists === "function" ? np.photos.splitArtists(text) : [text]
    }

    function photoUrlFor(variant: bool): string {
        if (!np.photoOn || np.photos === null)
            return ""
        const name = np.photos.photoArtist(np.artistsFor(variant))
        return name === "" ? "" : String(np.photos.photoFor([name]))
    }

    function phoneField(name: string): string {
        if (np.bluez === null || np.bluez[name] === undefined || np.bluez[name] === null)
            return ""
        return String(np.bluez[name])
    }

    function formatTime(ms: real): string {
        const total = Math.max(0, Math.floor(ms / 1000))
        const minutes = Math.floor(total / 60)
        const seconds = total % 60
        return minutes + ":" + (seconds < 10 ? "0" : "") + seconds
    }

    function ring(index: int): Item {
        return ringRepeater.itemAt(index)
    }

    function ringFocused(index: int): bool {
        return np.focusRow === 0 && np.ringIndex === index
    }

    function trackId(map: var): var {
        if (map === undefined || map === null || map.id === undefined)
            return null
        return map.id
    }

    function snapshot(map: var): var {
        return {
            id: np.trackId(map),
            title: map.title === undefined || map.title === null ? "" : String(map.title),
            artist: map.artist === undefined || map.artist === null ? "" : String(map.artist),
            album: map.album === undefined || map.album === null ? "" : String(map.album),
            artPath: map.artPath === undefined || map.artPath === null ? "" : String(map.artPath),
            rating: map.rating === undefined || map.rating === null ? 0 : Number(map.rating),
            artists: map.artists !== undefined && map.artists !== null && map.artists.length > 0 ? map.artists
                : map.artist === undefined || map.artist === null || String(map.artist) === "" ? [] : [String(map.artist)]
        }
    }

    readonly property int photoRevision: np.photos !== null && np.photos.revision !== undefined ? np.photos.revision : 0
    readonly property var photoArtists: np.artistsFor(np.shownVariant)
    property real backdropSwap: 1
    property bool backdropSame: true
    readonly property string photoName: np.photoOn && np.photos !== null && np.photoRevision >= 0
        ? np.photos.photoArtist(np.photoArtists) : ""
    property string artLead: ""
    readonly property url photoUrl: np.photoName !== "" && np.photoRevision >= 0 ? np.photos.photoFor([np.photoName]) : ""
    readonly property point photoFocus: np.photoName !== "" && np.photoRevision >= 0 && typeof np.photos.photoFocus === "function"
        ? np.photos.photoFocus([np.photoName]) : Qt.point(0.5, 0.5)
    readonly property bool linked: np.link !== null && np.link.connected === true

    function artSheetItems(info: var): var {
        if (!np.photoOn || !np.linked)
            return []
        const out = []
        if (np.photoName !== "" && info.name !== undefined && String(info.name) !== "" && Number(info.crc) !== 0
            && np.link.canAsk("block")) {
            out.push("block image")
            if (info.wrong === true)
                out.push("wrong artist")
        }
        if (np.link.canAsk("pick") && (np.photoName !== "" || np.photoArtists.length > 0))
            out.push("pick image")
        return out
    }

    function openArtSheet(): bool {
        if (np.idle)
            return false
        const info = np.photoName !== "" && np.photos !== null ? np.photos.photoInfo(np.photoName) : ({})
        const items = np.artSheetItems(info)
        if (items.length === 0)
            return false
        np.artTarget = {
            stored: info.name === undefined ? "" : String(info.name),
            tag: np.photoName !== "" ? np.photoName : (np.photoArtists.length > 0 ? String(np.photoArtists[0]) : ""),
            crc: info.crc === undefined ? 0 : Number(info.crc),
            mbid: info.mbid === undefined ? "" : String(info.mbid),
            artists: np.photoArtists.slice(),
            photo: np.photoName !== ""
        }
        np.artNames = []
        np.artItems = items
        const source = info.source === undefined ? "" : String(info.source)
        const credit = info.credit === undefined ? "" : String(info.credit)
        np.artLead = !np.artTarget.photo || source === "" ? "" : credit === "" ? source : source + ", " + credit
        sheet.title = np.artTarget.photo ? np.artTarget.stored : np.artistText
        sheet.items = items
        sheet.open()
        return true
    }

    function artChosen(index: int): void {
        const t = np.artTarget
        if (sheet.stack.length > 0 && np.artNames.length > 0) {
            const name = np.artNames[index]
            np.artNames = []
            np.artItems = []
            sheet.close()
            if (name !== undefined)
                np.link.artPick(name, "")
            return
        }
        const item = np.artItems[index]
        if (item === "pick image" && !t.photo && t.artists.length > 1) {
            np.artNames = t.artists.slice()
            sheet.push(sheet.title, np.artNames)
            return
        }
        np.artItems = []
        sheet.close()
        if (item === "block image")
            np.link.artBlock(false, t.stored, t.mbid, t.crc)
        else if (item === "wrong artist")
            np.link.artBlock(true, t.stored, t.mbid, t.crc)
        else if (item === "pick image")
            np.link.artPick(t.tag, "")
    }


    function artUrl(path: string): string {
        return path === "" ? "" : "file://" + path
    }

    function buildNext(): var {
        const titles = []
        if (np.queue === null || typeof np.queue.get !== "function")
            return titles
        if (typeof np.queue.upcoming === "function") {
            for (const row of np.queue.upcoming(Ui.npNextCount)) {
                const e = np.queue.get(Number(row))
                if (e === undefined || e === null)
                    break
                titles.push(e.title === undefined || e.title === null ? "" : String(e.title))
            }
            return titles
        }
        const count = typeof np.queue.count === "number" ? np.queue.count : 0
        const start = np.queue.index === undefined ? -1 : np.queue.index
        for (let i = start + 1; i < count && titles.length < Ui.npNextCount; i++) {
            const e = np.queue.get(i)
            if (e === undefined || e === null)
                break
            titles.push(e.title === undefined || e.title === null ? "" : String(e.title))
        }
        return titles
    }

    function refreshNext() {
        if (trackAnim.running || np.pending !== null)
            return
        np.nextTitles = np.buildNext()
    }

    function applyShown(map: var) {
        np.shown = map
        np.nextTitles = np.buildNext()
        np.stepLyrics(np.livePosition(), false)
    }

    function trackChanged() {
        const map = np.snapshot(np.current)
        if (map.id === np.shown.id) {
            np.shown = map
            np.nextTitles = np.buildNext()
            return
        }
        if (!np.live || np.shown.id === null || np.shownVariant) {
            if (trackAnim.running)
                trackAnim.complete()
            np.applyShown(map)
            return
        }
        if (trackAnim.running && np.pending !== null) {
            np.pending = map
            return
        }
        np.pending = map
        trackAnim.restart()
    }

    function syncPosition() {
        if (np.player !== null && np.player.position !== undefined)
            np.shownPosition = np.player.position
        np.updateTimes()
    }

    function updateTimes() {
        np.elapsedText = np.formatTime(np.position)
        np.remainingText = np.duration > 0 ? "-" + np.formatTime(np.duration - np.position) : ""
    }

    function switchVariant() {
        if (lyricsFade.running)
            lyricsFade.complete()
        if (np.variant)
            np.lyricsMode = false
        if (variantIn.running)
            variantIn.complete()
        if (variantOut.running)
            variantOut.complete()
        if (np.variant === np.shownVariant)
            return
        if (!np.live) {
            np.landVariant()
            return
        }
        np.backdropSame = np.photoUrlFor(np.variant) === np.photoUrlFor(!np.variant)
        if (np.variant)
            variantIn.start()
        else
            variantOut.start()
    }

    function landVariant() {
        np.backdropSwap = 1
        np.shownVariant = np.variant
        if (np.variant) {
            np.lyricsView = false
            np.lyricsShade = 0
            np.enterLyrics()
        }
        np.contentOpacity = 1
        np.variantScale = 1
        np.localLayerOpacity = np.variant ? 0 : 1
        if (!np.variant)
            np.trackChanged()
    }

    function wake() {
        np.idle = false
        idleTimer.restart()
    }

    function moveRing(delta: int): bool {
        if (np.focusRow !== 0)
            return false
        const target = Math.max(0, Math.min(np.ringLast, np.ringIndex + delta))
        if (target === np.ringIndex)
            return false
        np.ringIndex = target
        return true
    }

    function callPlayer(method: string, arg: var): bool {
        if (np.player === null || typeof np.player[method] !== "function")
            return false
        if (arg === undefined)
            np.player[method]()
        else
            np.player[method](arg)
        return true
    }

    function callPhone(method: string): bool {
        if (np.bluez === null || typeof np.bluez[method] !== "function")
            return false
        np.bluez[method]()
        return true
    }

    function playPause() {
        if (np.shownVariant) {
            np.callPhone(np.phonePlaying ? "pause" : "play")
            return
        }
        if (!np.callPlayer("playPause"))
            console.info("microdap: nowplaying playPause")
    }

    function skip(delta: int) {
        if (np.shownVariant) {
            np.callPhone(delta < 0 ? "previous" : "next")
            return
        }
        if (!np.callPlayer(delta < 0 ? "previous" : "next"))
            console.info("microdap: nowplaying " + (delta < 0 ? "previous" : "next"))
    }

    function seekTarget(from: real, delta: real): real {
        const target = Math.max(0, from + delta)
        if (delta <= 0 || np.duration <= 0)
            return target
        return Math.max(from, Math.min(np.duration - Ui.seekEndMarginMs, target))
    }

    function seek(delta: int) {
        if (np.shownVariant) {
            np.callPhone(delta < 0 ? "rewind" : "fastForward")
            return
        }
        if (!np.callPlayer("seek", np.seekTarget(np.shownPosition, delta * Ui.seekStepMs)))
            console.info("microdap: nowplaying seek " + delta)
    }

    function startSeek(delta: int): void {
        seekTickTimer.stop()
        d.seekDir = delta
        d.seekHeld = false
        d.seekPos = np.shownPosition
        seekHoldTimer.restart()
    }

    function seekTick(): void {
        if (d.seekDir === 0)
            return
        d.seekHeld = true
        d.seekPos = np.seekTarget(d.seekPos, d.seekDir * Ui.seekTickStepMs)
        if (!np.callPlayer("seek", d.seekPos))
            console.info("microdap: nowplaying scan " + d.seekDir)
    }

    function cancelSeek(): void {
        seekHoldTimer.stop()
        seekTickTimer.stop()
        d.seekDir = 0
        d.seekHeld = false
    }

    function endSeek(): void {
        const dir = d.seekDir
        const held = d.seekHeld
        np.cancelSeek()
        if (dir !== 0 && !held)
            np.seek(dir)
    }

    function toggleRing(index: int) {
        if (np.shownVariant) {
            if (index === 0)
                np.playPause()
            else if (index === 1 && np.bluez !== null && np.bluez.shuffle !== undefined)
                np.bluez.shuffle = np.phoneShuffle === "off" ? "alltracks" : "off"
            else if (index === 2 && np.bluez !== null && np.bluez.repeat !== undefined)
                np.bluez.repeat = np.phoneRepeat === "off" ? "alltracks" : "off"
        } else if (index === 0) {
            if (!np.callPlayer("toggleShuffle"))
                console.info("microdap: nowplaying shuffle")
        } else if (index === 1) {
            if (!np.callPlayer("cycleRepeat"))
                console.info("microdap: nowplaying repeat")
        } else if (!np.callPlayer("rate")) {
            console.info("microdap: nowplaying rate")
        }
        const item = np.ring(index)
        if (item !== null)
            item.toggled()
    }

    function openInfo(): bool {
        if (np.shownVariant || np.shown.id === null || np.shown.id === undefined)
            return false
        np.trackInfo(Number(np.shown.id))
        return true
    }

    function readLyrics(): void {
        const ready = np.lyrics !== null && np.lyrics.pending !== true
        d.lines = ready && np.lyrics.lines !== undefined ? np.lyrics.lines : []
        d.plain = ready && np.lyrics.plain !== undefined ? np.lyrics.plain : []
        d.lyricsGen++
        slideAnim.stop()
        d.lineTrack = null
        np.stepLyrics(np.livePosition(), false)
    }

    function livePosition(): real {
        return np.player !== null && np.player.position !== undefined ? Number(np.player.position) : 0
    }

    function lyricIndex(pos: real): int {
        if (!np.lyricsMatch)
            return lyricsColumn.none
        if (np.lyricsPending)
            return -1
        if (d.lines.length === 0)
            return lyricsColumn.none
        let at = -1
        for (let i = 0; i < d.lines.length && Number(d.lines[i].ms) <= pos; i++)
            at = i
        return at
    }

    function stamp(line: int): real {
        return line >= 0 && line < d.lines.length ? Number(d.lines[line].ms) : -1
    }

    function stepLyrics(pos: real, due: bool): void {
        dueTimer.stop()
        if (!np.lyricsShown)
            return
        const fresh = np.shown.id !== d.lineTrack
        if (!fresh && trackAnim.running)
            return
        let line = np.lyricIndex(pos)
        if (!due && line === d.lineShown)
            d.dueLine = lyricsColumn.none
        if (!fresh && !due && d.dueLine === d.lineShown && line === d.lineShown - 1
                && pos >= np.stamp(d.lineShown) - Motion.progress)
            line = d.lineShown
        if (fresh || line !== d.lineShown) {
            slideAnim.stop()
            let slide = false
            const drawn = !fresh && d.drawnGen === d.lyricsGen && np.lyricsMatch && !np.lyricsPending
            if (drawn && np.live && line >= 0 && line === d.lineShown + 1) {
                const at = np.stamp(line)
                const late = Math.max(0, pos - at)
                let span = Motion.lyricSlide
                if (line + 1 < d.lines.length)
                    span = Math.min(span, np.stamp(line + 1) - at)
                if (span - late >= Motion.scroll) {
                    slide = true
                    d.slideFrom = d.lineShown
                    slideAnim.from = late / span
                    slideAnim.duration = span - late
                }
            }
            d.lineTrack = np.shown.id
            d.lineShown = line
            d.dueLine = due ? line : lyricsColumn.none
            d.drawnGen = line !== lyricsColumn.none && np.lyricsMatch && !np.lyricsPending ? d.lyricsGen : -1
            if (slide)
                slideAnim.restart()
        }
        np.armDue(pos)
    }

    function armDue(pos: real): void {
        if (!np.lyricsShown || !np.localPlaying || !np.live || d.lineShown < -1)
            return
        const next = d.lineShown + 1
        if (next >= d.lines.length)
            return
        dueTimer.interval = Math.max(1, np.stamp(next) - pos)
        dueTimer.restart()
    }

    function toggleLyrics(): void {
        if (lyricsFade.running || np.shownVariant)
            return
        if (!np.lyricsMode && !np.hasLyrics)
            return
        np.lyricsMode = !np.lyricsMode
        np.focusRow = 0
        if (!np.live) {
            np.landLyrics()
            return
        }
        lyricsFade.restart()
    }

    function landLyrics(): void {
        np.lyricsView = np.lyricsMode
        np.lyricsShade = np.lyricsMode ? 1 : 0
        np.contentOpacity = 1
        np.enterLyrics()
    }

    function enterLyrics(): void {
        d.lineTrack = null
        slideAnim.stop()
        if (np.lyricsView)
            np.stepLyrics(np.livePosition(), false)
        else
            dueTimer.stop()
    }

    function touchContext(kind: string, index: int, hit: Item): bool {
        if (kind === "backdrop")
            return np.openArtSheet()
        return kind === "title" && np.openInfo()
    }

    function touchTap(kind: string, index: int, hit: Item): void {
        if (kind === "art" || kind === "lyrics") {
            np.toggleLyrics()
            return
        }
        if (kind !== "title" || !d.titleTap)
            return
        d.titleTap = false
        np.openQueue()
    }

    function activate() {
        if (np.focusRow === 1) {
            if (!np.openInfo())
                np.openQueue()
            return
        }
        np.toggleRing(np.ringIndex)
    }

    function back(): bool {
        if (np.stack === null || np.stack.depth < 2)
            return false
        np.stack.pop()
        return true
    }

    function touchPress(kind: string, index: int, x: real, hit: Item): string {
        d.titleTap = false
        if (np.idle)
            return "a"
        switch (kind) {
        case "ring":
            np.focusRow = 0
            np.ringIndex = index
            return "a"
        case "title":
            np.focusRow = 1
            d.titleTap = true
            return ""
        case "back":
            return "b"
        case "art":
        case "lyrics":
            return ""
        case "seek":
            if (np.shownVariant || np.duration <= 0)
                return ""
            d.seekX = x
            np.seekAt(x)
            if (np.live)
                idleTimer.restart()
            return ""
        default:
            return ""
        }
    }

    function seekAt(x: real): void {
        const f = Math.max(0, Math.min(1, (x - Ui.px(Ui.npSeekMargin)) / np.progressWidth))
        np.seekTo = Math.max(0, Math.min(np.duration - Ui.seekEndMarginMs, f * np.duration))
        np.updateTimes()
    }

    function touchMove(kind: string, index: int, dx: real, dy: real, hit: Item): void {
        if (kind !== "seek" || np.seekTo < 0)
            return
        np.seekAt(d.seekX + dx)
        if (np.live)
            idleTimer.restart()
    }

    function touchLift(kind: string, index: int, dx: real, dy: real, hit: Item): void {
        if (kind !== "seek" || np.seekTo < 0)
            return
        np.seekAt(d.seekX + dx)
        const target = np.seekTo
        np.callPlayer("seek", target)
        np.shownPosition = target
        np.seekTo = -1
        np.updateTimes()
        if (np.live)
            idleTimer.restart()
    }

    function touchCancel(kind: string, index: int, hit: Item): void {
        np.seekTo = -1
        np.updateTimes()
    }

    function completeAll() {
        if (trackAnim.running)
            trackAnim.complete()
        if (lyricsFade.running)
            lyricsFade.complete()
        if (slideAnim.running)
            slideAnim.complete()
        if (variantIn.running)
            variantIn.complete()
        if (variantOut.running)
            variantOut.complete()
        for (let i = 0; i < ringRepeater.count; i++) {
            const item = ringRepeater.itemAt(i)
            if (item !== null && item.swapping)
                item.toggled()
        }
    }

    function handleAction(name: string, repeat: bool): bool {
        if (np.idle) {
            np.wake()
            return true
        }
        if (np.live)
            idleTimer.restart()
        if (sheet.shown)
            return sheet.handleAction(name, repeat)
        switch (name) {
        case "up":
            if (np.focusRow !== 0 || np.lyricsMode)
                return false
            np.focusRow = 1
            return true
        case "down":
            if (np.focusRow !== 1)
                return false
            np.focusRow = 0
            return true
        case "left":
            return np.moveRing(-1)
        case "right":
            return np.moveRing(1)
        case "a":
            if (!repeat)
                np.activate()
            return true
        case "b":
            if (repeat)
                return false
            if (np.lyricsMode) {
                np.toggleLyrics()
                return true
            }
            return np.back()
        case "x":
            if (!repeat)
                np.playPause()
            return true
        case "y":
            if (repeat)
                return true
            if (np.lyricsMode || np.hasLyrics)
                np.toggleLyrics()
            else
                np.openQueue()
            return true
        case "back":
            if (!repeat)
                np.openQueue()
            return true
        case "lb":
            if (!repeat)
                np.skip(-1)
            return true
        case "rb":
            if (!repeat)
                np.skip(1)
            return true
        case "lt":
        case "rt":
            if (repeat)
                return true
            if (np.shownVariant)
                np.seek(name === "lt" ? -1 : 1)
            else
                np.startSeek(name === "lt" ? -1 : 1)
            return true
        case "start":
            if (repeat)
                return true
            if (np.shownVariant) {
                np.playPause()
                return true
            }
            return np.back()
        default:
            return false
        }
    }

    function release(name: string) {
        if (name === "a")
            np.pressed = false
        if (np.shownVariant && (name === "lt" || name === "rt"))
            np.callPhone("play")
        else if (name === "lt" || name === "rt")
            np.endSeek()
    }

    onVariantChanged: {
        if (d.ready)
            np.switchVariant()
    }

    onLiveChanged: {
        if (np.live) {
            idleTimer.restart()
            np.syncPosition()
            return
        }
        np.completeAll()
        np.cancelSeek()
        dueTimer.stop()
        idleTimer.stop()
        np.idle = false
    }

    onDurationChanged: np.updateTimes()
    onLyricsChanged: np.readLyrics()
    onRingLastChanged: np.ringIndex = Math.min(np.ringIndex, np.ringLast)
    onShownVariantChanged: np.updateTimes()

    Component.onCompleted: {
        np.focusRow = 0
        np.ringIndex = 0
        np.shownVariant = np.variant
        np.localLayerOpacity = np.variant ? 0 : 1
        np.applyShown(np.snapshot(np.current))
        np.syncPosition()
        np.readLyrics()
        d.ready = true
        if (np.live)
            idleTimer.restart()
    }

    QtObject {
        id: d

        property bool ready: false
        property int seekDir: 0
        property real seekX: 0
        property bool seekHeld: false
        property real seekPos: 0
        property bool titleTap: false
        property var lines: []
        property var plain: []
        property int lyricsGen: 0
        property int drawnGen: -1
        property var lineTrack: null
        property int lineShown: -2
        property int slideFrom: -2
        property int dueLine: -2
        property real slideT: 1
    }

    Timer {
        id: dueTimer

        repeat: false
        onTriggered: {
            const next = d.lineShown + 1
            if (next < d.lines.length)
                np.stepLyrics(np.stamp(next), true)
        }
    }

    NumberAnimation {
        id: slideAnim

        target: d
        property: "slideT"
        to: 1
        easing.type: Motion.linear
    }

    Connections {
        target: np.lyrics
        ignoreUnknownSignals: true

        function onChanged() {
            np.readLyrics()
        }
    }

    Timer {
        id: seekHoldTimer

        interval: Ui.seekHoldMs
        repeat: false
        onTriggered: {
            np.seekTick()
            seekTickTimer.start()
        }
    }

    Timer {
        id: seekTickTimer

        interval: Ui.seekTickMs
        repeat: true
        onTriggered: np.seekTick()
    }

    Connections {
        target: np.player
        ignoreUnknownSignals: true

        function onCurrentChanged() {
            np.trackChanged()
        }

        function onPositionChanged() {
            if (!np.live)
                np.syncPosition()
            if (np.lyricsShown)
                np.stepLyrics(np.livePosition(), false)
        }

        function onStatusChanged() {
            if (np.lyricsShown)
                np.stepLyrics(np.livePosition(), false)
        }
    }

    Connections {
        target: np.queue
        ignoreUnknownSignals: true

        function onModelReset() {
            Qt.callLater(np.refreshNext)
        }

        function onCurrentSlotChanged() {
            Qt.callLater(np.refreshNext)
        }

        function onCountChanged() {
            Qt.callLater(np.refreshNext)
        }

        function onRowsMoved() {
            Qt.callLater(np.refreshNext)
        }

        function onOrderChanged() {
            Qt.callLater(np.refreshNext)
        }

        function onRepeatChanged() {
            Qt.callLater(np.refreshNext)
        }
    }

    Connections {
        target: np.bluez
        ignoreUnknownSignals: true

        function onPositionMsChanged() {
            if (np.shownVariant)
                np.updateTimes()
        }
    }

    Timer {
        id: progressTimer

        interval: Motion.progress
        repeat: true
        running: np.live && !np.shownVariant
        onTriggered: {
            if (np.player !== null && np.player.position !== undefined)
                np.shownPosition = np.player.position
        }
    }

    Timer {
        id: elapsedTimer

        interval: Motion.elapsedTick
        repeat: true
        running: np.live
        onTriggered: np.updateTimes()
    }

    Timer {
        id: idleTimer

        interval: Motion.idle
        repeat: false
        running: false
        onTriggered: {
            if (np.live)
                np.idle = true
        }
    }

    Rectangle {
        id: surface

        anchors.fill: parent
        color: Ui.bg
        opacity: np.backdropFade
    }

    Backdrop {
        id: backdrop

        readonly property string touchKind: "backdrop"

        anchors.fill: parent
        opacity: np.backdropFade * np.backdropSwap
        source: np.photoUrl
        focusX: np.photoFocus.x
        focusY: np.photoFocus.y
    }

    Item {
        id: localLayer

        anchors.fill: parent
        opacity: np.localLayerOpacity * np.backdropFade
        visible: np.localLayerOpacity > 0

        Rectangle {
            id: shade

            anchors.fill: parent
            color: Ui.scrimSheet
            opacity: np.lyricsShade
            visible: np.lyricsShade > 0
        }
    }

    Item {
        id: content

        anchors.fill: parent
        opacity: np.contentOpacity * np.contentFade
        transform: Scale {
            origin.x: content.width * Motion.nowPlayingOriginX
            origin.y: content.height * Motion.nowPlayingOriginY
            xScale: np.contentScale
            yScale: np.contentScale
        }

        Item {
            id: backRingClip

            readonly property string touchKind: "back"

            width: Ui.px(Ui.backRingX + Ui.backRing)
            height: Ui.px(Ui.backRingY + Ui.backRing)
            clip: true

            Ring {
                id: backRing

                x: Ui.px(Ui.backRingX)
                y: Ui.px(Ui.backRingY)
                size: Ui.backRing
                focused: true
                glyph: "back"
                animated: np.live
            }
        }

        StatusStrip {
            id: statusStrip

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            clock: np.clock
            percent: np.batteryPercent
            battery: np.battery
            batteryPresent: np.batteryPresent
            bluetooth: np.phoneConnected || (np.bluez !== null && np.bluez.headsetName !== undefined && np.bluez.headsetName !== "")
            playing: np.playing
            paused: np.paused
            opacity: np.idleAlpha

            Behavior on opacity {
                enabled: np.live
                NumberAnimation {
                    duration: Motion.idleFade
                    easing.type: Motion.bezier
                    easing.bezierCurve: Motion.decel
                }
            }
        }

        Text {
            id: fromLabel

            x: np.textX
            y: np.fromBaseline - fromLabel.baselineOffset
            visible: np.shownVariant
            text: np.fromLabelText
            color: Ui.white(Ui.sectionLabel.alpha)
            font: Ui.font(Ui.sectionLabel)
        }

        Text {
            id: artistItem

            x: np.textX
            y: np.artistBaseline - artistItem.baselineOffset
            visible: !np.lyricsShown
            text: np.artistText
            color: Ui.white(Ui.npArtist.alpha)
            font: Ui.font(Ui.npArtist)
        }

        Text {
            id: albumItem

            x: np.textX
            y: np.albumBaseline - albumItem.baselineOffset
            visible: !np.lyricsShown
            text: np.albumText
            color: Ui.white(Ui.npAlbum.alpha)
            font: Ui.font(Ui.npAlbum)
        }

        Item {
            id: artItem

            readonly property string touchKind: np.hasLyrics && !np.lyricsShown ? "art" : ""

            x: np.artX
            y: np.artY
            width: np.artSide
            height: np.artSide
            visible: !np.lyricsShown
            opacity: np.artOpacity
            transform: Scale {
                origin.x: artItem.width * Motion.trackArtOriginX
                origin.y: artItem.height * Motion.trackArtOriginY
                xScale: np.artScale
                yScale: np.artScale
            }

            Rectangle {
                id: artTile

                anchors.fill: parent
                visible: !np.shownVariant || np.phoneArtShown
                color: Ui.tile

                Image {
                    id: artImage

                    anchors.fill: parent
                    source: np.shownVariant ? np.phoneArt : np.artUrl(np.shown.artPath)
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    mipmap: false
                    sourceSize: Qt.size(Math.ceil(artItem.width), Math.ceil(artItem.height))
                }
            }

            Glyph {
                id: btGlyph

                anchors.centerIn: parent
                visible: np.shownVariant && !np.phoneArtShown
                name: "bluetooth"
                size: Ui.npBtGlyph
                color: Ui.textFull
            }
        }

        Item {
            id: trackGroup

            x: Ui.px(np.trackShift)
            y: 0
            width: content.width
            height: content.height
            opacity: np.trackOpacity

            Rectangle {
                id: progressItem

                x: np.artX
                y: np.progressY
                width: np.progressWidth
                height: Ui.px(Ui.npProgressThick)
                visible: np.progressShown && !np.lyricsShown
                color: Ui.textLow

                Rectangle {
                    id: progressFill

                    x: 0
                    y: 0
                    width: progressItem.width * np.fraction
                    height: progressItem.height
                    color: Ui.accent
                }
            }

            Item {
                id: seekBand

                readonly property string touchKind: np.shownVariant || !np.progressShown || np.lyricsShown ? "" : "seek"
                readonly property bool touchTrack: true

                x: np.artX - Ui.px(Ui.npSeekMargin)
                y: np.progressY
                width: np.progressWidth + 2 * Ui.px(Ui.npSeekMargin)
                height: Ui.px(Ui.npSeekBandH)
            }

            Text {
                id: elapsedItem

                x: np.artX
                y: np.timeBaseline - elapsedItem.baselineOffset
                visible: np.progressShown && !np.lyricsShown
                text: np.elapsedText
                color: Ui.white(Ui.time.alpha)
                font: Ui.font(Ui.time)
            }

            Text {
                id: remainingItem

                x: np.artX + np.progressWidth - remainingItem.width
                y: np.timeBaseline - remainingItem.baselineOffset
                visible: np.progressShown && !np.lyricsShown
                text: np.remainingText
                color: Ui.white(Ui.time.alpha)
                font: Ui.font(Ui.time)
            }

            FocusBar {
                id: focusBar

                x: np.textX - Ui.px(Ui.focusBarOffset)
                rowY: np.titleBaseline - Ui.px(Ui.capHeight * Ui.npTitle.size)
                rowHeight: Ui.px(Ui.capHeight * Ui.npTitle.size)
                visible: np.focusRow === 1 && !np.lyricsShown
                opacity: np.focusOpacity
                animated: np.live
            }

            Item {
                id: titleBlock

                readonly property string touchKind: np.lyricsShown ? "" : "title"

                x: np.textX
                y: np.titleBaseline - Ui.px(Ui.capHeight * Ui.npTitle.size)
                width: content.width - Ui.px(Ui.edge) - titleBlock.x
                height: np.titleBottom - titleBlock.y
            }

            Text {
                id: titleItem

                x: np.textX
                y: np.titleBaseline - titleItem.baselineOffset
                visible: !np.lyricsShown
                text: np.titleText
                color: Ui.white(Ui.npTitle.alpha)
                font: Ui.font(Ui.npTitle)
            }

            Text {
                id: countItem

                x: np.textX + Ui.px(Ui.npNextIndent)
                y: np.nextBaseline - countItem.baselineOffset
                visible: np.shownVariant
                text: np.countText
                color: Ui.white(Ui.npCount.alpha)
                font: Ui.font(Ui.npCount)
            }

            Repeater {
                id: nextRepeater

                model: np.shownVariant || np.lyricsShown ? [] : np.nextTitles

                Text {
                    id: nextText

                    required property int index
                    required property var modelData

                    readonly property bool marked: nextText.index === 0 && np.markShown

                    x: np.textX + Ui.px(Ui.npNextIndent)
                    y: np.nextBaseline + Ui.px(Ui.npNextPitch) * nextText.index - nextText.baselineOffset
                    width: nextText.marked ? Math.max(0, np.markLeft - Ui.px(Ui.statusGap) - nextText.x) : nextText.implicitWidth
                    clip: nextText.marked
                    text: nextText.modelData
                    color: Ui.white(Ui.npNext.alpha)
                    font: Ui.font(Ui.npNext)
                }
            }

            Glyph {
                id: lyricsMark

                x: np.markRight - Ui.px(np.markBox) * GlyphPaths.micInk.right / GlyphPaths.box
                y: np.nextBaseline - Ui.px(np.markBox) * GlyphPaths.micInk.bottom / GlyphPaths.box
                visible: np.markShown
                name: "mic"
                size: np.markBox
                color: Ui.white(Ui.npNext.alpha)
            }

            LyricsColumn {
                id: lyricsColumn

                anchors.fill: parent
                visible: np.lyricsShown
                lines: np.lyricsMatch ? d.lines : []
                plain: np.lyricsMatch ? d.plain : []
                pending: np.lyricsPending
                title: np.shown.title
                next: np.nextTitles.length > 0 ? np.nextTitles[0] : ""
                from: slideAnim.running ? d.slideFrom : d.lineShown
                to: d.lineShown
                p: slideAnim.running ? Motion.expOut6(d.slideT) : 1
                textLeft: Ui.landscape ? Ui.npColumnRight : Ui.edge
                textTop: np.lyricsTop
                textBottom: np.lyricsBottom
            }
        }

        Repeater {
            id: ringRepeater

            model: Ui.npRingX.length

            Ring {
                id: ringItem

                required property int index
                readonly property bool isFocused: np.ringFocused(ringItem.index)

                x: Ui.px(Ui.npRingCenter(ringItem.index)) - ringItem.width / 2
                y: np.ringY - ringItem.height / 2
                visible: ringItem.index <= np.ringLast
                size: Ui.npRing
                focused: ringItem.isFocused
                on: np.ringOn[ringItem.index]
                toggle: np.ringToggle[ringItem.index]
                glyph: np.ringGlyphs[ringItem.index]
                pressed: ringItem.isFocused && np.pressed
                animated: np.live
                opacity: ringItem.isFocused ? 1 : np.idleAlpha

                Behavior on opacity {
                    enabled: np.live
                    NumberAnimation {
                        duration: Motion.idleFade
                        easing.type: Motion.bezier
                        easing.bezierCurve: Motion.decel
                    }
                }

                Item {
                    id: ringHit

                    readonly property string touchKind: "ring"
                    readonly property int touchIndex: ringItem.index

                    anchors.centerIn: parent
                    width: Ui.px(Ui.npRingCenter(1) - Ui.npRingCenter(0))
                    height: Math.max(0, Math.min(ringHit.width, 2 * (np.ringY - np.titleBottom)))
                }
            }
        }
    }

    SequentialAnimation {
        id: trackAnim

        ParallelAnimation {
            NumberAnimation {
                target: np
                property: "trackShift"
                to: Motion.trackShiftOut
                duration: Motion.trackHide
                easing.type: Motion.easeOut
            }
            NumberAnimation {
                target: np
                property: "trackOpacity"
                to: 0
                duration: Motion.trackHide
                easing.type: Motion.easeOut
            }
            NumberAnimation {
                target: np
                property: "artOpacity"
                to: 0
                duration: Motion.trackHide
                easing.type: Motion.easeOut
            }
        }
        ScriptAction {
            script: {
                if (np.pending !== null)
                    np.applyShown(np.pending)
                np.pending = null
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: np
                property: "artScale"
                from: Motion.trackArtScale
                to: 1
                duration: Motion.trackShow
                easing.type: Motion.easeIn
            }
            NumberAnimation {
                target: np
                property: "artOpacity"
                from: 0
                to: 1
                duration: Motion.trackShow
                easing.type: Motion.easeIn
            }
            NumberAnimation {
                target: np
                property: "trackShift"
                from: Motion.trackShiftIn
                to: 0
                duration: Motion.trackShow
                easing.type: Motion.easeIn
            }
            NumberAnimation {
                target: np
                property: "trackOpacity"
                from: 0
                to: 1
                duration: Motion.trackShow
                easing.type: Motion.easeIn
            }
        }
    }

    SequentialAnimation {
        id: lyricsFade

        ParallelAnimation {
            NumberAnimation {
                target: np
                property: "contentOpacity"
                to: 0
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
            NumberAnimation {
                target: np
                property: "lyricsShade"
                to: np.lyricsMode ? 1 : 0
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
        }
        ScriptAction {
            script: {
                np.lyricsView = np.lyricsMode
                np.enterLyrics()
            }
        }
        NumberAnimation {
            target: np
            property: "contentOpacity"
            to: 1
            duration: Motion.variantFade
            easing.type: Motion.bezier
            easing.bezierCurve: Motion.decel
        }
    }

    SequentialAnimation {
        id: variantIn

        ParallelAnimation {
            NumberAnimation {
                target: np
                property: "contentOpacity"
                from: 1
                to: 0
                duration: Motion.nowPlayingFade
                easing.type: Motion.pop
            }
            NumberAnimation {
                target: np
                property: "localLayerOpacity"
                from: 1
                to: 0
                duration: Motion.nowPlayingFade
                easing.type: Motion.pop
            }
            NumberAnimation {
                target: np
                property: "lyricsShade"
                to: 0
                duration: Motion.nowPlayingFade
                easing.type: Motion.pop
            }
            NumberAnimation {
                target: np
                property: "backdropSwap"
                to: np.backdropSame ? 1 : 0
                duration: Motion.nowPlayingFade
                easing.type: Motion.pop
            }
        }
        ScriptAction {
            script: {
                np.shownVariant = true
                np.lyricsView = false
                np.enterLyrics()
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: np
                property: "variantScale"
                from: Motion.nowPlayingScale
                to: 1
                duration: Motion.nowPlayingIn
                easing.type: Motion.easeIn
            }
            NumberAnimation {
                target: np
                property: "contentOpacity"
                from: 0
                to: 1
                duration: Motion.nowPlayingIn
                easing.type: Motion.easeIn
            }
            NumberAnimation {
                target: np
                property: "backdropSwap"
                to: 1
                duration: Motion.nowPlayingIn
                easing.type: Motion.easeIn
            }
        }
    }

    SequentialAnimation {
        id: variantOut

        ParallelAnimation {
            NumberAnimation {
                target: np
                property: "contentOpacity"
                from: 1
                to: 0
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
            NumberAnimation {
                target: np
                property: "backdropSwap"
                to: np.backdropSame ? 1 : 0
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
        }
        ScriptAction {
            script: {
                np.shownVariant = false
                np.trackChanged()
            }
        }
        ParallelAnimation {
            NumberAnimation {
                target: np
                property: "localLayerOpacity"
                from: 0
                to: 1
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
            NumberAnimation {
                target: np
                property: "backdropSwap"
                to: 1
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
            NumberAnimation {
                target: np
                property: "contentOpacity"
                from: 0
                to: 1
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
        }
    }

    Sheet {
        id: sheet

        anchors.fill: parent
        screenOff: !np.live
        lead: sheet.depth === 0 ? np.artLead : ""
        onAccepted: (index) => {
            if (np.artItems.length > 0 || np.artNames.length > 0)
                np.artChosen(index)
        }
        onRejected: {
            np.artItems = []
            np.artNames = []
        }
        onProgressChanged: {
            if (!sheet.shown && sheet.progress === 0)
                np.artLead = ""
        }
    }

    onPhotoNameChanged: {
        if (np.photoName !== "" && np.photos !== null)
            np.photos.touchName(np.photoName)
    }
}
