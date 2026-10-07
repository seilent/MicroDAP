pragma ComponentBehavior: Bound
import QtQuick

Page {
    id: np

    property QtObject player: null
    property QtObject bluez: null
    property QtObject status: null
    property bool animated: true
    property bool screenOff: false
    property int focusRow: 0
    property int ringIndex: 0
    property bool idle: false
    property bool shownVariant: false
    property bool frontIsA: true
    property real crossfade: 0
    property real trackShift: 0
    property real trackOpacity: 1
    property real artOpacity: 1
    property real artScale: 1
    property real contentOpacity: 1
    property real variantScale: 1
    property real localLayerOpacity: 1
    property var shown: ({ id: null, title: "", artist: "", album: "", artPath: "", rating: 0 })
    property var pending: null
    property var nextTitles: []
    property real shownPosition: 0
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
    readonly property string fromLabelText: "from " + np.phoneName
    readonly property string artistText: np.shownVariant ? np.phoneField("artist") : np.shown.artist
    readonly property string albumText: np.shownVariant ? np.phoneField("album") : np.shown.album
    readonly property string titleText: np.shownVariant ? np.phoneField("title") : np.shown.title
    readonly property int trackNumber: np.bluez !== null && np.bluez.trackNumber !== undefined ? np.bluez.trackNumber : 0
    readonly property int numberOfTracks: np.bluez !== null && np.bluez.numberOfTracks !== undefined ? np.bluez.numberOfTracks : 0
    readonly property string countText: np.trackNumber > 0 && np.numberOfTracks > 0 ? np.trackNumber + " of " + np.numberOfTracks : ""
    readonly property real position: np.shownVariant
        ? (np.bluez.positionMs !== undefined ? np.bluez.positionMs : 0)
        : np.shownPosition
    readonly property real duration: np.shownVariant
        ? (np.bluez.durationMs !== undefined ? np.bluez.durationMs : 0)
        : (np.player !== null && np.player.duration !== undefined ? np.player.duration : 0)
    readonly property bool progressShown: !np.shownVariant || np.duration > 0
    readonly property real fraction: np.duration > 0 ? Math.max(0, Math.min(1, np.position / np.duration)) : 0
    readonly property bool shuffleOn: np.queue !== null && np.queue.shuffle === true
    readonly property int repeatMode: np.queue !== null && np.queue.repeat !== undefined ? np.queue.repeat : 0
    readonly property bool rated: np.shown.rating !== undefined && np.shown.rating > 0
    readonly property string phoneShuffle: np.bluez !== null && np.bluez.shuffle !== undefined ? np.bluez.shuffle : "off"
    readonly property string phoneRepeat: np.bluez !== null && np.bluez.repeat !== undefined ? np.bluez.repeat : "off"
    readonly property var ringGlyphs: np.shownVariant
        ? [np.phonePlaying ? "pause" : "play", "shuffle", np.phoneRepeat === "singletrack" ? "repeatOne" : "repeat"]
        : ["shuffle", np.repeatMode === 2 ? "repeatOne" : "repeat", "heart"]
    readonly property var ringOn: np.shownVariant
        ? [false, np.phoneShuffle !== "off", np.phoneRepeat !== "off"]
        : [np.shuffleOn, np.repeatMode !== 0, np.rated]
    readonly property int battery: np.status !== null && np.status.battery !== undefined ? np.status.battery : -1
    readonly property bool batteryPresent: np.status !== null && np.status.batteryPresent === true
    readonly property string clock: np.status !== null && np.status.clock !== undefined ? np.status.clock : ""
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
    readonly property Item holder: np.parent
    readonly property bool fadingPage: np.holder !== null && np.holder.kind === "fade" && np.stack !== null
        && np.stack.phase !== "idle" && (np.stack.incoming === np.holder || np.stack.outgoing === np.holder)
    readonly property real pageProgress: np.holder !== null && np.holder.progress !== undefined ? np.holder.progress : 1
    readonly property real backdropFade: np.holder !== null && np.holder.backdropFade !== undefined ? np.holder.backdropFade : 1
    readonly property real contentFade: np.holder !== null && np.holder.contentFade !== undefined ? np.holder.contentFade : 1
    readonly property real contentScale: !np.fadingPage ? np.variantScale
        : Motion.lerp(Motion.nowPlayingScale, 1, np.contentFade)
    readonly property bool trackAnimating: trackAnim.running
    readonly property bool variantAnimating: variantIn.running || variantOut.running
    readonly property bool crossfading: crossfadeAnim.running

    property alias backdropA: backA
    property alias backdropB: backB
    property alias localLayer: localLayer
    property alias content: content
    property alias backRing: backRing
    property alias statusStrip: statusStrip
    property alias artItem: artItem
    property alias btGlyph: btGlyph
    property alias fromLabel: fromLabel
    property alias artistItem: artistItem
    property alias albumItem: albumItem
    property alias progressItem: progressItem
    property alias progressFill: progressFill
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
    property alias crossfadeAnimation: crossfadeAnim

    signal openQueue()

    focusCount: 0

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
            rating: map.rating === undefined || map.rating === null ? 0 : Number(map.rating)
        }
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

    function frontBackdrop(): Item {
        return np.frontIsA ? backA : backB
    }

    function backBackdrop(): Item {
        return np.frontIsA ? backB : backA
    }

    function setBackdrop(source: string, immediate: bool) {
        if (crossfadeAnim.running)
            crossfadeAnim.complete()
        const front = np.frontBackdrop()
        if (String(front.source) === source)
            return
        if (immediate || !np.live) {
            front.source = source
            np.backBackdrop().source = ""
            front.restart()
            return
        }
        const back = np.backBackdrop()
        back.source = source
        back.restart()
        crossfadeAnim.start()
    }

    function finishCrossfade() {
        const old = np.frontBackdrop()
        np.frontIsA = !np.frontIsA
        np.crossfade = 0
        old.source = ""
    }

    function applyShown(map: var) {
        np.shown = map
        np.nextTitles = np.buildNext()
        np.setBackdrop(np.artUrl(map.artPath), !d.ready)
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
        if (np.variant)
            variantIn.start()
        else
            variantOut.start()
    }

    function landVariant() {
        np.shownVariant = np.variant
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
        const target = Math.max(0, Math.min(Ui.npRingX.length - 1, np.ringIndex + delta))
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
            console.info("casdap: nowplaying playPause")
    }

    function skip(delta: int) {
        if (np.shownVariant) {
            np.callPhone(delta < 0 ? "previous" : "next")
            return
        }
        if (!np.callPlayer(delta < 0 ? "previous" : "next"))
            console.info("casdap: nowplaying " + (delta < 0 ? "previous" : "next"))
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
            console.info("casdap: nowplaying seek " + delta)
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
            console.info("casdap: nowplaying scan " + d.seekDir)
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
                console.info("casdap: nowplaying shuffle")
        } else if (index === 1) {
            if (!np.callPlayer("cycleRepeat"))
                console.info("casdap: nowplaying repeat")
        } else if (!np.callPlayer("rate")) {
            console.info("casdap: nowplaying rate")
        }
        const item = np.ring(index)
        if (item !== null)
            item.toggled()
    }

    function activate() {
        if (np.focusRow === 1) {
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

    function completeAll() {
        if (trackAnim.running)
            trackAnim.complete()
        if (variantIn.running)
            variantIn.complete()
        if (variantOut.running)
            variantOut.complete()
        if (crossfadeAnim.running)
            crossfadeAnim.complete()
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
        switch (name) {
        case "up":
            if (np.focusRow !== 0)
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
            return np.back()
        case "x":
            if (!repeat)
                np.playPause()
            return true
        case "y":
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
        idleTimer.stop()
        np.idle = false
    }

    onDurationChanged: np.updateTimes()
    onShownVariantChanged: np.updateTimes()

    Component.onCompleted: {
        np.focusRow = 0
        np.ringIndex = 0
        np.shownVariant = np.variant
        np.localLayerOpacity = np.variant ? 0 : 1
        np.applyShown(np.snapshot(np.current))
        np.syncPosition()
        d.ready = true
        if (np.live)
            idleTimer.restart()
    }

    QtObject {
        id: d

        property bool ready: false
        property int seekDir: 0
        property bool seekHeld: false
        property real seekPos: 0
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

    PageSurface {
        anchors.fill: parent
        opacity: np.backdropFade
    }

    Item {
        id: localLayer

        anchors.fill: parent
        opacity: np.localLayerOpacity * np.backdropFade
        visible: np.localLayerOpacity > 0

        Backdrop {
            id: backA

            anchors.fill: parent
            z: np.frontIsA ? 0 : 1
            opacity: np.frontIsA ? 1 : np.crossfade
            paused: !np.live || np.shownVariant
            screenOff: np.screenOff
        }

        Backdrop {
            id: backB

            anchors.fill: parent
            z: np.frontIsA ? 1 : 0
            opacity: np.frontIsA ? np.crossfade : 1
            paused: !np.live || np.shownVariant
            screenOff: np.screenOff
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

        Ring {
            id: backRing

            x: Ui.px(Ui.backRingInset)
            y: Ui.px(Ui.backRingInset)
            size: Ui.backRing
            focused: true
            glyph: "back"
            animated: np.live
        }

        StatusStrip {
            id: statusStrip

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            clock: np.clock
            battery: np.battery
            batteryPresent: np.batteryPresent
            bluetooth: np.phoneConnected
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
            text: np.artistText
            color: Ui.white(Ui.npArtist.alpha)
            font: Ui.font(Ui.npArtist)
        }

        Text {
            id: albumItem

            x: np.textX
            y: np.albumBaseline - albumItem.baselineOffset
            text: np.albumText
            color: Ui.white(Ui.npAlbum.alpha)
            font: Ui.font(Ui.npAlbum)
        }

        Item {
            id: artItem

            x: np.artX
            y: np.artY
            width: np.artSide
            height: np.artSide
            opacity: np.artOpacity
            transform: Scale {
                origin.x: artItem.width * Motion.trackArtOriginX
                origin.y: artItem.height * Motion.trackArtOriginY
                xScale: np.artScale
                yScale: np.artScale
            }

            Rectangle {
                anchors.fill: parent
                visible: !np.shownVariant
                color: Ui.tile

                Image {
                    anchors.fill: parent
                    source: np.artUrl(np.shown.artPath)
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
                visible: np.shownVariant
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
                visible: np.progressShown
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

            Text {
                id: elapsedItem

                x: np.artX
                y: np.timeBaseline - elapsedItem.baselineOffset
                visible: np.progressShown
                text: np.elapsedText
                color: Ui.white(Ui.time.alpha)
                font: Ui.font(Ui.time)
            }

            Text {
                id: remainingItem

                x: np.artX + np.progressWidth - remainingItem.width
                y: np.timeBaseline - remainingItem.baselineOffset
                visible: np.progressShown
                text: np.remainingText
                color: Ui.white(Ui.time.alpha)
                font: Ui.font(Ui.time)
            }

            FocusBar {
                id: focusBar

                x: np.textX - Ui.px(Ui.focusBarOffset)
                rowY: np.titleBaseline - Ui.px(Ui.capHeight * Ui.npTitle.size)
                rowHeight: Ui.px(Ui.capHeight * Ui.npTitle.size)
                visible: np.focusRow === 1
                opacity: np.focusOpacity
                animated: np.live
            }

            Text {
                id: titleItem

                x: np.textX
                y: np.titleBaseline - titleItem.baselineOffset
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

                model: np.shownVariant ? [] : np.nextTitles

                Text {
                    id: nextText

                    required property int index
                    required property var modelData

                    x: np.textX + Ui.px(Ui.npNextIndent)
                    y: np.nextBaseline + Ui.px(Ui.npNextPitch) * nextText.index - nextText.baselineOffset
                    text: nextText.modelData
                    color: Ui.white(Ui.npNext.alpha)
                    font: Ui.font(Ui.npNext)
                }
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
                size: Ui.npRing
                focused: ringItem.isFocused
                on: np.ringOn[ringItem.index]
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
            }
        }
    }

    NumberAnimation {
        id: crossfadeAnim

        target: np
        property: "crossfade"
        from: 0
        to: 1
        duration: Motion.artistFade
        easing.type: Motion.linear
        onFinished: np.finishCrossfade()
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
        }
        ScriptAction {
            script: np.shownVariant = true
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
        }
    }

    SequentialAnimation {
        id: variantOut

        NumberAnimation {
            target: np
            property: "contentOpacity"
            from: 1
            to: 0
            duration: Motion.variantFade
            easing.type: Motion.bezier
            easing.bezierCurve: Motion.decel
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
                property: "contentOpacity"
                from: 0
                to: 1
                duration: Motion.variantFade
                easing.type: Motion.bezier
                easing.bezierCurve: Motion.decel
            }
        }
    }
}
