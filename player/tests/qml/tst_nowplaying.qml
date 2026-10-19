import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "NowPlaying"
    when: windowShown
    visible: true
    width: 640
    height: 960

    PageStack {
        id: pages

        width: 640
        height: 960
    }

    PadRouter {
        id: router

        width: 640
        height: 960
        stack: pages
    }

    TouchLayer {
        id: layer

        width: 640
        height: 960
        router: router
    }

    QtObject {
        id: fakeQueue

        property var rows: []
        property int count: 0
        property int index: 0
        property bool shuffle: false
        property int repeat: 0
        property var upcomingRows: null

        signal orderChanged()
        signal modelReset()
        signal currentSlotChanged()

        function get(row) {
            return fakeQueue.rows[row]
        }

        function upcoming(n) {
            if (fakeQueue.upcomingRows !== null)
                return fakeQueue.upcomingRows.slice(0, n)
            const out = []
            for (let i = fakeQueue.index + 1; i < fakeQueue.count && out.length < n; i++)
                out.push(i)
            return out
        }

        function fill(n) {
            const rows = []
            for (let i = 0; i < n; i++)
                rows.push({ id: i + 1, title: "Song " + (i + 1), artist: i < 4 ? "Addison Road" : "Other Artist", album: "Album " + (i < 4 ? 1 : 2), artPath: "", durationMs: 240000 })
            fakeQueue.rows = rows
            fakeQueue.count = n
            fakeQueue.index = 0
            fakeQueue.upcomingRows = null
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 1
        property real position: 5000
        property real duration: 240000
        property var current: ({})
        property QtObject queue: fakeQueue
        property var calls: []

        function note(name) {
            fakePlayer.calls = fakePlayer.calls.concat([name])
        }

        function select(row) {
            fakeQueue.index = row
            fakePlayer.current = Object.assign({ rating: 0 }, fakeQueue.rows[row])
        }

        function playPause() {
            fakePlayer.note("playPause")
            fakePlayer.status = fakePlayer.status === 1 ? 2 : 1
        }

        function pause() {
            fakePlayer.note("pause")
            fakePlayer.status = 2
        }

        function play() {
            fakePlayer.note("play")
            fakePlayer.status = 1
        }

        function next() {
            fakePlayer.note("next")
        }

        function previous() {
            fakePlayer.note("previous")
        }

        function seek(ms) {
            fakePlayer.note("seek " + ms)
        }

        function toggleShuffle() {
            fakePlayer.note("toggleShuffle")
            fakeQueue.shuffle = !fakeQueue.shuffle
        }

        function cycleRepeat() {
            fakePlayer.note("cycleRepeat")
            fakeQueue.repeat = (fakeQueue.repeat + 1) % 3
        }

        function rate() {
            fakePlayer.note("rate")
            const map = Object.assign({}, fakePlayer.current)
            map.rating = map.rating > 0 ? 0 : 1
            fakePlayer.current = map
        }
    }

    QtObject {
        id: fakeBluez

        property bool connected: false
        property string deviceName: ""
        property bool transportActive: false
        property string title: ""
        property string artist: ""
        property string album: ""
        property int trackNumber: 0
        property int numberOfTracks: 0
        property real durationMs: 0
        property real positionMs: 0
        property string status: "stopped"
        property string shuffle: "off"
        property string repeat: "off"
        property int battery: -1
        property bool pairing: false
        property int pairingRemaining: 0
        property var calls: []

        function reset() {
            fakeBluez.connected = false
            fakeBluez.deviceName = ""
            fakeBluez.transportActive = false
            fakeBluez.title = ""
            fakeBluez.artist = ""
            fakeBluez.album = ""
            fakeBluez.trackNumber = 0
            fakeBluez.numberOfTracks = 0
            fakeBluez.durationMs = 0
            fakeBluez.positionMs = 0
            fakeBluez.status = "stopped"
            fakeBluez.shuffle = "off"
            fakeBluez.repeat = "off"
            fakeBluez.battery = -1
            fakeBluez.calls = []
        }

        function activate() {
            fakeBluez.deviceName = "Pixel 8"
            fakeBluez.connected = true
            fakeBluez.title = "Track Four"
            fakeBluez.artist = "Phone Artist"
            fakeBluez.album = "Phone Album"
            fakeBluez.trackNumber = 4
            fakeBluez.numberOfTracks = 11
            fakeBluez.durationMs = 200000
            fakeBluez.positionMs = 50000
            fakeBluez.status = "playing"
            fakeBluez.transportActive = true
            if (fakePlayer.status === 1)
                fakePlayer.pause()
        }

        function play() {
            fakeBluez.calls = fakeBluez.calls.concat(["play"])
            fakeBluez.status = "playing"
        }

        function pause() {
            fakeBluez.calls = fakeBluez.calls.concat(["pause"])
            fakeBluez.status = "paused"
        }

        function next() {
            fakeBluez.calls = fakeBluez.calls.concat(["next"])
        }

        function previous() {
            fakeBluez.calls = fakeBluez.calls.concat(["previous"])
        }

        function rewind() {
            fakeBluez.calls = fakeBluez.calls.concat(["rewind"])
        }

        function fastForward() {
            fakeBluez.calls = fakeBluez.calls.concat(["fastForward"])
        }
    }

    QtObject {
        id: fakeStatus

        property int battery: 73
        property bool batteryPresent: true
        property string clock: ""
    }

    QtObject {
        id: fakeLyrics

        property bool pending: false
        property var lines: []
        property var plain: []

        signal changed()

        function set(lines, plain) {
            fakeLyrics.pending = false
            fakeLyrics.lines = lines
            fakeLyrics.plain = plain === undefined ? [] : plain
            fakeLyrics.changed()
        }
    }

    QtObject {
        id: counters

        property int queueOpens: 0
        property var infos: []
    }

    Component {
        id: blankComponent

        Page {}
    }

    Component {
        id: nowPlayingComponent

        NowPlaying {
            objectName: "nowplaying"
            player: fakePlayer
            bluez: fakeBluez
            status: fakeStatus
            lyrics: fakeLyrics
            onOpenQueue: counters.queueOpens++
            onTrackInfo: (id) => counters.infos = counters.infos.concat([id])
        }
    }

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.setSurface(w, h)
        pages.width = w
        pages.height = h
        router.width = w
        router.height = h
        layer.width = w
        layer.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        pages.clear()
        pages.animate = false
        fakeQueue.fill(8)
        fakeQueue.shuffle = false
        fakeQueue.repeat = 0
        fakePlayer.status = 1
        fakePlayer.position = 5000
        fakePlayer.duration = 240000
        fakePlayer.calls = []
        fakePlayer.select(0)
        fakeBluez.reset()
        fakeStatus.battery = 73
        fakeStatus.batteryPresent = true
        counters.queueOpens = 0
        counters.infos = []
        fakeLyrics.set([], [])
    }

    function sampleLines() {
        return [
            { ms: 0, text: "First line" },
            { ms: 4000, text: "Second line" },
            { ms: 9000, text: "Third line" },
            { ms: 13000, text: "Fourth line" },
            { ms: 17000, text: "Fifth line" }
        ]
    }

    function columnTexts(page) {
        const out = []
        const rep = page.lyricsColumn.drawRepeater
        for (let i = 0; i < rep.count; i++) {
            const d = rep.itemAt(i).modelData
            const b = d.fb !== null ? d.fb : d.fa
            out.push((b.big ? "*" : "") + b.texts.join(" "))
        }
        return out
    }

    function open(properties) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        pages.push(blankComponent, {})
        return pages.push(nowPlayingComponent, props, "fade")
    }

    function baselineOf(item) {
        return item.y + item.baselineOffset
    }

    function ringCenterX(page, i) {
        const ring = page.ring(i)
        return ring.x + ring.width / 2
    }

    function test_layout_portrait() {
        const page = open()
        compare(Ui.u, 2)
        compare(Ui.canvasH, 480)
        compare(page.shownVariant, false)
        compare(page.backRing.x, Ui.px(0))
        compare(page.backRing.y, Ui.px(-15))
        compare(page.backRing.width, Ui.px(64))
        compare(page.backRing.glyph, "back")
        compare(page.backRing.parent.clip, true)
        compare(page.backRing.parent.y, 0)
        compare(page.backRing.parent.height, Ui.px(49))
        verify(page.backRing.y + page.backRing.height < tc.baselineOf(page.artistItem) - Ui.px(0.7 * 20))
        compare(page.artistItem.text, "Addison Road")
        compare(page.artistItem.font.capitalization, Font.AllUppercase)
        compare(page.artistItem.font.pixelSize, Ui.fontPx(20))
        fuzzyCompare(page.artistItem.x, Ui.px(16), 0.01)
        fuzzyCompare(tc.baselineOf(page.artistItem), Ui.px(117), 0.5)
        compare(page.albumItem.text, "Album 1")
        fuzzyCompare(tc.baselineOf(page.albumItem), Ui.px(141), 0.5)
        compare(page.artItem.width, Ui.px(168))
        compare(page.artItem.height, Ui.px(168))
        fuzzyCompare(page.artItem.x, Ui.px(16), 0.01)
        fuzzyCompare(page.artItem.y, Ui.px(155), 0.01)
        compare(page.btGlyph.visible, false)
        compare(page.progressItem.visible, true)
        fuzzyCompare(page.progressItem.y, Ui.px(480 - 156), 0.01)
        fuzzyCompare(page.progressItem.x, Ui.px(16), 0.01)
        compare(page.progressItem.width, page.artItem.width)
        compare(page.progressItem.width, Ui.px(168))
        fuzzyCompare(page.progressItem.x + page.progressItem.width, page.artItem.x + page.artItem.width, 0.01)
        compare(page.progressItem.height, Ui.px(2))
        fuzzyCompare(page.progressFill.width, page.progressItem.width * 5000 / 240000, 0.01)
        compare(page.elapsedText, "0:05")
        compare(page.remainingText, "-3:55")
        fuzzyCompare(tc.baselineOf(page.elapsedItem), page.progressItem.y + Ui.px(14), 0.5)
        fuzzyCompare(page.remainingItem.x + page.remainingItem.width, page.progressItem.x + page.progressItem.width, 0.5)
        compare(page.titleItem.text, "Song 1")
        compare(page.titleItem.font.pixelSize, Ui.fontPx(26))
        fuzzyCompare(tc.baselineOf(page.titleItem), Ui.px(480 - 118), 0.5)
        compare(page.nextRepeater.count, 3)
        for (let i = 0; i < 3; i++) {
            const next = page.nextRepeater.itemAt(i)
            compare(next.text, "Song " + (i + 2))
            fuzzyCompare(next.x, Ui.px(16 + 40), 0.01)
            fuzzyCompare(tc.baselineOf(next), Ui.px(480 - 118 + 18 * (i + 1)), 0.5)
        }
        compare(page.ringRepeater.count, 3)
        fuzzyCompare(tc.ringCenterX(page, 0), Ui.px(320 * 0.13), 0.01)
        fuzzyCompare(tc.ringCenterX(page, 1), Ui.px(320 * 0.5), 0.01)
        fuzzyCompare(tc.ringCenterX(page, 2), Ui.px(320 * 0.87), 0.01)
        for (let i = 0; i < 3; i++) {
            const ring = page.ring(i)
            compare(ring.width, Ui.px(32))
            fuzzyCompare(ring.y + ring.height / 2, Ui.px(480 - 40), 0.01)
        }
        compare(page.ring(0).glyph, "shuffle")
        compare(page.ring(1).glyph, "repeat")
        compare(page.ring(2).glyph, "heart")
        compare(page.ring(0).focused, true)
        compare(page.ring(1).focused, false)
        compare(page.focusBar.visible, false)
        compare(page.statusStrip.playGlyph.visible, true)
        compare(page.statusStrip.playGlyph.name, "play")
        compare(page.statusStrip.bluetoothGlyph.visible, false)
        compare(page.statusStrip.batteryGlyph.visible, true)
        compare(Qt.colorEqual(page.statusStrip.batteryGlyph.color, Ui.textFull), true)
        compare(page.backdrop.source.toString(), "")
        verify(Qt.colorEqual(page.surface.color, Ui.bg))
        compare(page.localLayer.visible, true)
    }

    function test_art_minimum() {
        const page = open()
        Ui.userScale = 960 / 420 / 2
        fuzzyCompare(Ui.canvasH, 420, 0.01)
        fuzzyCompare(page.artItem.width, Ui.px(109), 0.01)
        fuzzyCompare(page.progressItem.y, Ui.px(420 - 156), 0.01)
        fuzzyCompare(tc.baselineOf(page.titleItem), Ui.px(420 - 118), 0.5)
    }

    function test_layout_landscape() {
        tc.setWindow(1920, 1080)
        const page = open()
        compare(Ui.landscape, true)
        fuzzyCompare(Ui.canvasW, 483, 1)
        fuzzyCompare(Ui.canvasH, 272, 1)
        fuzzyCompare(page.artItem.x, Ui.px(16), 0.01)
        fuzzyCompare(page.artItem.y, Ui.px(72), 0.01)
        fuzzyCompare(page.artItem.width, Ui.px(Math.min(168, Ui.canvasH - 106)), 0.01)
        fuzzyCompare(page.progressItem.y, page.artItem.y + page.artItem.height + Ui.px(8), 0.01)
        compare(page.progressItem.width, page.artItem.width)
        fuzzyCompare(page.artistItem.x, Ui.px(216), 0.01)
        fuzzyCompare(tc.baselineOf(page.artistItem), Ui.px(60), 0.5)
        fuzzyCompare(tc.baselineOf(page.albumItem), Ui.px(84), 0.5)
        fuzzyCompare(page.titleItem.x, Ui.px(216), 0.01)
        fuzzyCompare(tc.baselineOf(page.titleItem), Ui.px(116), 0.5)
        fuzzyCompare(tc.baselineOf(page.nextRepeater.itemAt(0)), Ui.px(140), 0.5)
        fuzzyCompare(tc.baselineOf(page.nextRepeater.itemAt(1)), Ui.px(158), 0.5)
        fuzzyCompare(tc.baselineOf(page.nextRepeater.itemAt(2)), Ui.px(176), 0.5)
        const span = Ui.canvasW - 216 - 16
        fuzzyCompare(tc.ringCenterX(page, 0), Ui.px(216 + span * 0.13), 0.01)
        fuzzyCompare(tc.ringCenterX(page, 1), Ui.px(216 + span * 0.5), 0.01)
        fuzzyCompare(tc.ringCenterX(page, 2), Ui.px(216 + span * 0.87), 0.01)
        fuzzyCompare(page.ring(1).y + page.ring(1).height / 2, Ui.px(Ui.canvasH - 40), 0.01)
        fuzzyCompare(page.statusStrip.row.y, Ui.px(10), 0.01)
        fakeBluez.activate()
        compare(page.shownVariant, true)
        compare(page.fromLabel.visible, true)
        fuzzyCompare(page.fromLabel.x, Ui.px(216), 0.01)
        fuzzyCompare(tc.baselineOf(page.fromLabel), Ui.px(39), 0.5)
        compare(Ui.npFromBaselineLandscape, 39)
    }

    function test_battery_low() {
        const page = open()
        fakeStatus.battery = 9
        compare(Qt.colorEqual(page.statusStrip.batteryGlyph.color, Ui.accent), true)
        fakeStatus.battery = 10
        compare(Qt.colorEqual(page.statusStrip.batteryGlyph.color, Ui.textFull), true)
        fakeStatus.batteryPresent = false
        compare(page.statusStrip.batteryGlyph.visible, false)
    }

    function test_focus_rings_and_title() {
        const page = open()
        compare(page.focusRow, 0)
        compare(page.ringIndex, 0)
        verify(!router.dispatch("left", false))
        verify(router.dispatch("right", false))
        compare(page.ringIndex, 1)
        compare(page.ring(1).focused, true)
        verify(router.dispatch("right", false))
        compare(page.ringIndex, 2)
        verify(!router.dispatch("right", false))
        verify(!router.dispatch("down", false))
        verify(router.dispatch("up", false))
        compare(page.focusRow, 1)
        compare(page.focusBar.visible, true)
        fuzzyCompare(page.focusBar.x, Ui.px(16 - 8), 0.01)
        fuzzyCompare(page.focusBar.y, tc.baselineOf(page.titleItem) - Ui.px(0.7 * 26), 0.5)
        compare(page.ring(2).focused, false)
        verify(!router.dispatch("up", false))
        verify(router.dispatch("a", false))
        compare(counters.queueOpens, 0)
        compare(JSON.stringify(counters.infos), JSON.stringify([page.shown.id]))
        verify(router.dispatch("x", false))
        compare(fakePlayer.calls, ["playPause"])
        verify(router.dispatch("down", false))
        compare(page.focusRow, 0)
        compare(page.ringIndex, 2)
        compare(page.ring(2).focused, true)
    }

    function test_ring_toggles() {
        const page = open({ animated: true })
        compare(page.ring(0).on, false)
        verify(router.dispatch("a", false))
        compare(fakePlayer.calls, ["toggleShuffle"])
        compare(page.ring(0).on, true)
        compare(page.ring(0).swapping, true)
        router.dispatch("right", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls, ["toggleShuffle", "cycleRepeat"])
        compare(page.ring(1).on, true)
        compare(page.ring(1).glyph, "repeat")
        router.dispatch("a", false)
        compare(page.ring(1).glyph, "repeatOne")
        router.dispatch("right", false)
        router.dispatch("a", false)
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "rate")
        compare(page.ring(2).on, true)
        compare(page.shown.rating, 1)
    }

    function test_rate_ring_library_only() {
        const page = open()
        compare(page.ring(2).visible, true)
        compare(page.ring(2).glyph, "heart")
        verify(router.dispatch("right", false))
        verify(router.dispatch("right", false))
        compare(page.ringIndex, 2)
        fakePlayer.current = { id: 0, title: "Outside", artist: "", album: "", artPath: "", rating: 0 }
        tryVerify(() => page.shown.id === 0, 1000)
        compare(page.ring(2).visible, false)
        compare(page.ringIndex, 1)
        verify(!router.dispatch("right", false))
        compare(page.ringIndex, 1)
        compare(page.ring(0).visible, true)
        compare(page.ring(1).visible, true)
        fakePlayer.select(2)
        tryVerify(() => page.shown.id === 3, 1000)
        compare(page.ring(2).visible, true)
        verify(router.dispatch("right", false))
        compare(page.ringIndex, 2)
    }

    function test_pad_map() {
        const page = open()
        verify(router.dispatch("lb", false))
        compare(fakePlayer.calls, ["previous"])
        verify(router.dispatch("rb", false))
        compare(fakePlayer.calls, ["previous", "next"])
        verify(router.dispatch("x", false))
        compare(fakePlayer.calls, ["previous", "next", "playPause"])
        compare(fakePlayer.status, 2)
        compare(page.statusStrip.playGlyph.name, "pause")
        verify(router.dispatch("lt", false))
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "playPause")
        router.release("lt")
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "seek 0")
        verify(router.dispatch("rt", false))
        router.release("rt")
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "seek 15000")
        verify(router.dispatch("y", false))
        compare(counters.queueOpens, 1)
        verify(router.dispatch("back", false))
        compare(counters.queueOpens, 2)
        compare(pages.depth, 2)
        verify(router.dispatch("start", false))
        compare(pages.depth, 1)
    }

    function test_next_rows_follow_play_order() {
        const page = open()
        compare(JSON.stringify(page.nextTitles), JSON.stringify(["Song 2", "Song 3", "Song 4"]))
        fakeQueue.upcomingRows = [5, 2, 7]
        fakeQueue.orderChanged()
        tryVerify(() => JSON.stringify(page.nextTitles) === JSON.stringify(["Song 6", "Song 3", "Song 8"]), 1000)
        fakeQueue.upcomingRows = []
        fakeQueue.orderChanged()
        tryVerify(() => page.nextTitles.length === 0, 1000)
        fakeQueue.upcomingRows = [3]
        fakeQueue.modelReset()
        tryVerify(() => JSON.stringify(page.nextTitles) === JSON.stringify(["Song 4"]), 1000)
        fakeQueue.upcomingRows = [1, 2]
        fakeQueue.currentSlotChanged()
        tryVerify(() => JSON.stringify(page.nextTitles) === JSON.stringify(["Song 2", "Song 3"]), 1000)
    }

    function test_seek_hold_scans() {
        open()
        verify(router.dispatch("rt", false))
        compare(fakePlayer.calls.length, 0)
        tryVerify(() => fakePlayer.calls.length >= 2, 2000)
        compare(fakePlayer.calls[0], "seek 6000")
        compare(fakePlayer.calls[1], "seek 7000")
        router.release("rt")
        const n = fakePlayer.calls.length
        wait(Ui.seekTickMs * 2)
        compare(fakePlayer.calls.length, n)
        verify(fakePlayer.calls.indexOf("seek 15000") < 0)
    }

    function test_seek_rt_clamps_at_end() {
        fakePlayer.position = 236000
        const page = open()
        page.syncPosition()
        verify(router.dispatch("rt", false))
        router.release("rt")
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "seek 239000")
        fakePlayer.position = 5000
    }

    function test_back_pops() {
        open()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_fade_transition() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(nowPlayingComponent, { animated: true }, "fade")
        compare(pages.transitionKind, "fade")
        compare(pages.phase, "exit")
        compare(pages.outgoing.kind, "fade")
        compare(pages.outgoing.toAngle, 0)
        compare(pages.incoming.duration, 300)
        compare(pages.incoming.splitFade, true)
        compare(pages.incoming.opacity, 1)
        compare(pages.incoming.backdropFade, 0)
        compare(pages.incoming.contentFade, 0)
        compare(pages.outgoing.splitFade, false)
        compare(pages.outgoing.opacity, 1)
        compare(page.content.opacity, 0)
        compare(page.localLayer.opacity, 0)
        tryCompare(pages, "phase", "enter", 2000)
        tryVerify(function() { return page.backdropFade > 0 && page.backdropFade < 1 }, 1000)
        verify(page.contentFade <= page.backdropFade)
        verify(page.contentScale <= 1)
        verify(page.contentScale >= 0.9)
        tryCompare(pages, "phase", "idle", 2000)
        compare(page.contentScale, 1)
        compare(page.backdropFade, 1)
        compare(page.contentFade, 1)
        compare(page.content.opacity, 1)
        compare(pages.incoming, null)
        verify(router.dispatch("b", false))
        compare(pages.transitionKind, "fade")
        compare(pages.outgoing.duration, 300)
        compare(pages.outgoing.flying, false)
        compare(pages.incoming.splitFade, false)
        tryVerify(function() { return page.contentFade < 1 }, 1000)
        verify(page.backdropFade < 1)
        verify(page.contentScale < 1)
        tryCompare(pages, "phase", "idle", 2000)
        compare(pages.depth, 1)
    }

    function test_idle_fade_eats_first_input() {
        const page = open({ animated: true })
        compare(page.idle, false)
        compare(page.idleTimer.running, true)
        compare(page.idleTimer.interval, 5000)
        tryCompare(page, "idle", true, 7000)
        tryCompare(page.ring(1), "opacity", 0.4, 2000)
        tryCompare(page.statusStrip, "opacity", 0.4, 2000)
        tryCompare(page.ring(2), "opacity", 0.4, 2000)
        compare(page.ring(0).opacity, 1)
        verify(router.dispatch("right", false))
        compare(page.idle, false)
        compare(page.ringIndex, 0)
        tryCompare(page.ring(1), "opacity", 1, 2000)
        tryCompare(page.statusStrip, "opacity", 1, 2000)
        verify(router.dispatch("right", false))
        compare(page.ringIndex, 1)
        compare(page.idleTimer.running, true)
    }

    function test_progress_and_elapsed() {
        const page = open({ animated: true })
        fakePlayer.position = 60000
        tryCompare(page, "shownPosition", 60000, 2000)
        fuzzyCompare(page.progressFill.width, page.progressItem.width * 0.25, 0.01)
        tryCompare(page, "elapsedText", "1:00", 2000)
        compare(page.remainingText, "-3:00")
    }

    function test_track_change_animates() {
        const page = open({ animated: true })
        compare(page.shown.id, 1)
        fakePlayer.select(1)
        compare(page.trackAnimating, true)
        compare(page.shown.id, 1)
        tryCompare(page, "trackAnimating", false, 3000)
        compare(page.shown.id, 2)
        compare(page.titleItem.text, "Song 2")
        compare(page.nextRepeater.count, 3)
        compare(page.nextRepeater.itemAt(0).text, "Song 3")
        compare(page.trackOpacity, 1)
        compare(page.artOpacity, 1)
        compare(page.artScale, 1)
        compare(page.trackShift, 0)
    }

    function test_trackChangeTwiceKeepsAnimation() {
        const page = open({ animated: true })
        compare(page.shown.id, 1)
        fakePlayer.select(1)
        compare(page.trackAnimation.running, true)
        compare(page.pending.id, 2)
        wait(100)
        verify(page.trackOpacity < 1)
        const before = page.trackOpacity
        fakePlayer.currentChanged()
        compare(page.trackAnimation.running, true)
        compare(page.pending.id, 2)
        verify(page.trackOpacity <= before)
        wait(50)
        verify(page.trackOpacity < before)
        tryCompare(page, "trackAnimating", false, Motion.trackHide + Motion.trackShow + 100)
        compare(page.shown.id, 2)
        compare(page.titleItem.text, "Song 2")
        compare(page.trackOpacity, 1)
        compare(page.artOpacity, 1)
    }

    function test_track_change_lands_when_not_live() {
        const page = open()
        fakePlayer.select(5)
        compare(page.trackAnimating, false)
        compare(page.shown.id, 6)
        compare(page.artistItem.text, "Other Artist")
        compare(page.nextRepeater.count, 2)
    }

    function test_art_not_backdrop() {
        fakeQueue.rows[0].artPath = "/tmp/a.png"
        fakeQueue.rows[5].artPath = "/tmp/b.png"
        fakePlayer.select(0)
        const page = open({ animated: true })
        compare(page.backdrop.source.toString(), "")
        fakePlayer.select(5)
        tryCompare(page, "trackAnimating", false, 3000)
        compare(page.shown.artPath, "/tmp/b.png")
        compare(page.backdrop.source.toString(), "")
    }

    function test_track_art_in_one_album() {
        fakeQueue.rows[0].artPath = "/tmp/a.png"
        fakeQueue.rows[1].artPath = "/tmp/b.png"
        fakePlayer.select(0)
        const page = open({ animated: true })
        compare(page.shown.album, "Album 1")
        compare(page.shown.artPath, "/tmp/a.png")
        compare(page.artUrl(page.shown.artPath), "file:///tmp/a.png")
        fakePlayer.select(1)
        tryCompare(page, "trackAnimating", false, 3000)
        compare(page.shown.album, "Album 1")
        compare(page.shown.artPath, "/tmp/b.png")
        compare(page.artUrl(page.shown.artPath), "file:///tmp/b.png")
    }

    function test_ring_states_white() {
        const page = open()
        for (let i = 0; i < 3; i++) {
            compare(page.ring(i).toggle, true)
            compare(page.ring(i).on, false)
            fuzzyCompare(page.ring(i).ringAlpha, Ui.alphaMid, 1e-6)
            verify(Qt.colorEqual(page.ring(i).iconItem.color, Ui.white(Ui.alphaMid)))
            compare(page.ring(i).arcSweep, 0)
        }
        compare(page.ring(0).strokeWidth, Ui.px(4))
        compare(page.ring(1).strokeWidth, Ui.px(3))
        router.dispatch("a", false)
        compare(page.ring(0).on, true)
        fuzzyCompare(page.ring(0).ringAlpha, Ui.alphaFull, 1e-6)
        verify(Qt.colorEqual(page.ring(0).iconItem.color, Ui.white(Ui.alphaFull)))
        verify(!Qt.colorEqual(page.ring(0).iconItem.color, Ui.accent))
        router.dispatch("right", false)
        compare(page.ring(0).strokeWidth, Ui.px(3))
        fuzzyCompare(page.ring(0).ringAlpha, Ui.alphaFull, 1e-6)
        compare(page.ring(1).strokeWidth, Ui.px(4))
        fuzzyCompare(page.ring(1).ringAlpha, Ui.alphaMid, 1e-6)
        fakeBluez.activate()
        compare(page.shownVariant, true)
        compare(page.ring(0).toggle, false)
        compare(page.ring(1).toggle, true)
    }

    function test_variant_from_other_screen() {
        pages.push(blankComponent, {})
        fakeBluez.activate()
        compare(fakePlayer.status, 2)
        const page = pages.push(nowPlayingComponent, { animated: false }, "fade")
        compare(fakePlayer.calls, ["pause"])
        compare(fakePlayer.status, 2)
        compare(page.shownVariant, true)
        compare(page.fromLabel.visible, true)
        compare(page.fromLabel.text, "from Pixel 8")
        compare(page.fromLabel.font.capitalization, Font.AllUppercase)
        fuzzyCompare(tc.baselineOf(page.fromLabel), Ui.px(97), 0.5)
        compare(page.artistItem.text, "Phone Artist")
        compare(page.albumItem.text, "Phone Album")
        compare(page.titleItem.text, "Track Four")
        compare(page.btGlyph.visible, true)
        compare(page.btGlyph.width, Ui.px(64))
        fuzzyCompare(page.btGlyph.x + page.btGlyph.width / 2, page.artItem.width / 2, 0.5)
        compare(page.countItem.visible, true)
        compare(page.countItem.text, "4 of 11")
        compare(page.countItem.font.capitalization, Font.AllUppercase)
        compare(page.countItem.font.pixelSize, Ui.fontPx(13))
        fuzzyCompare(tc.baselineOf(page.countItem), Ui.px(480 - 118 + 18), 0.5)
        compare(page.nextRepeater.count, 0)
        compare(page.progressItem.visible, true)
        compare(page.progressItem.width, page.artItem.width)
        fuzzyCompare(page.progressFill.width, page.progressItem.width * 0.25, 0.01)
        compare(page.ring(0).glyph, "pause")
        compare(page.ring(1).glyph, "shuffle")
        compare(page.ring(2).glyph, "repeat")
        compare(page.statusStrip.bluetoothGlyph.visible, true)
        compare(page.localLayer.visible, false)
        fakeBluez.durationMs = 0
        compare(page.progressItem.visible, false)
    }

    function test_variant_pad() {
        fakeBluez.activate()
        const page = open()
        verify(router.dispatch("a", false))
        compare(fakeBluez.calls, ["pause"])
        compare(page.ring(0).glyph, "play")
        verify(router.dispatch("start", false))
        compare(fakeBluez.calls, ["pause", "play"])
        compare(pages.depth, 2)
        verify(router.dispatch("x", false))
        compare(fakeBluez.calls, ["pause", "play", "pause"])
        verify(router.dispatch("lb", false))
        verify(router.dispatch("rb", false))
        compare(fakeBluez.calls.slice(3), ["previous", "next"])
        verify(router.dispatch("rt", false))
        page.release("rt")
        compare(fakeBluez.calls.slice(5), ["fastForward", "play"])
        router.dispatch("right", false)
        verify(router.dispatch("a", false))
        compare(fakeBluez.shuffle, "alltracks")
        compare(page.ring(1).on, true)
        compare(fakePlayer.calls.indexOf("play"), -1)
    }

    function test_variant_fades_out_and_local_stays_paused() {
        fakeBluez.activate()
        const page = open({ animated: true })
        compare(page.shownVariant, true)
        compare(fakePlayer.status, 2)
        fakeBluez.transportActive = false
        compare(page.variantAnimating, true)
        compare(page.variantOutAnimation.running, true)
        compare(Motion.variantFade, 380)
        compare(page.shownVariant, true)
        tryCompare(page, "shownVariant", false, 2000)
        tryCompare(page, "variantAnimating", false, 2000)
        compare(page.contentOpacity, 1)
        compare(page.localLayerOpacity, 1)
        compare(page.fromLabel.visible, false)
        compare(page.titleItem.text, "Song 1")
        compare(fakePlayer.status, 2)
        compare(fakePlayer.calls.indexOf("play"), -1)
        compare(page.statusStrip.playGlyph.name, "pause")
    }

    function test_variant_enters_while_shown() {
        const page = open({ animated: true })
        compare(page.shownVariant, false)
        fakeBluez.activate()
        compare(page.variantInAnimation.running, true)
        compare(fakePlayer.status, 2)
        tryCompare(page, "shownVariant", true, 2000)
        tryCompare(page, "variantAnimating", false, 2000)
        compare(page.variantScale, 1)
        compare(page.contentOpacity, 1)
        compare(page.titleItem.text, "Track Four")
    }

    function test_screen_off_completes() {
        const page = open({ animated: true })
        fakePlayer.select(1)
        compare(page.trackAnimating, true)
        page.screenOff = true
        compare(page.trackAnimating, false)
        compare(page.shown.id, 2)
        compare(page.idleTimer.running, false)
        page.screenOff = false
        compare(page.idleTimer.running, true)
    }

    function tapAt(item, x, y) {
        mouseClick(item, x, y)
        wait(0)
        wait(0)
    }

    function test_touch_rings_toggle() {
        const page = open()
        tc.tapAt(page.ring(0))
        compare(fakePlayer.calls, ["toggleShuffle"])
        tc.tapAt(page.ring(1))
        compare(page.ringIndex, 1)
        compare(fakePlayer.calls, ["toggleShuffle", "cycleRepeat"])
        tc.tapAt(page.ring(2))
        compare(page.ringIndex, 2)
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "rate")
    }

    function test_touch_phone_ring_toggles() {
        fakeBluez.activate()
        const page = open()
        compare(page.shownVariant, true)
        tc.tapAt(page.ring(0))
        compare(page.ringIndex, 0)
        compare(fakeBluez.calls, ["pause"])
        tc.tapAt(page.ring(1))
        compare(page.ringIndex, 1)
        compare(fakeBluez.shuffle, "alltracks")
    }

    function test_touch_seek_band() {
        const page = open()
        const band = page.seekBand
        const line = page.progressItem
        const m = Ui.px(Ui.npSeekMargin)
        compare(band.x, line.x - m)
        compare(band.width, line.width + 2 * m)
        compare(band.y, line.y)
        fuzzyCompare(band.height, Ui.px(19.8), 0.01)
        const y = band.height / 2
        mousePress(band, m + line.width / 2, y)
        compare(page.seekTo, 120000)
        compare(page.progressFill.width, line.width / 2)
        compare(page.elapsedItem.text, "2:00")
        mouseMove(band, m + line.width * 3 / 4, y)
        compare(page.seekTo, 180000)
        mouseMove(band, m + line.width * 3 / 4, y + Ui.px(200))
        compare(page.seekTo, 180000)
        mouseMove(band, -Ui.px(40), y + Ui.px(200))
        compare(page.seekTo, 0)
        mouseMove(band, m + line.width / 4, y)
        compare(fakePlayer.calls, [])
        mouseRelease(band, m + line.width / 4, y)
        wait(0)
        wait(0)
        compare(fakePlayer.calls, ["seek 60000"])
        compare(page.seekTo, -1)
        mousePress(band, band.width - 1, y)
        compare(page.seekTo, 240000 - Ui.seekEndMarginMs)
        mouseRelease(band, band.width - 1, y)
        wait(0)
        wait(0)
        compare(fakePlayer.calls, ["seek 60000", "seek " + (240000 - Ui.seekEndMarginMs)])
    }

    function test_touch_seek_band_idle_wakes() {
        const page = open({ animated: true })
        page.idle = true
        const band = page.seekBand
        mousePress(band, band.width / 2, band.height / 2)
        mouseRelease(band, band.width / 2, band.height / 2)
        wait(0)
        wait(0)
        wait(Motion.touchReturnDelay + 50)
        compare(page.idle, false)
        compare(page.pressed, false)
        compare(page.seekTo, -1)
        compare(fakePlayer.calls.filter((c) => c.startsWith("seek")), [])
    }

    function test_touch_seek_drag_keeps_awake() {
        const page = open({ animated: true })
        const band = page.seekBand
        const m = Ui.px(Ui.npSeekMargin)
        const y = band.height / 2
        mousePress(band, m, y)
        const steps = 6
        for (let i = 1; i <= steps; i++) {
            wait(Motion.idle / 4)
            mouseMove(band, m + page.progressItem.width * i / (steps * 2), y)
        }
        compare(page.idle, false)
        mouseRelease(band, m + page.progressItem.width / 2, y)
        wait(0)
        wait(0)
        compare(page.idle, false)
        compare(page.idleTimer.running, true)
    }

    function test_touch_title_opens_queue() {
        const page = open()
        tc.tapAt(page.titleItem)
        compare(page.focusRow, 1)
        compare(counters.queueOpens, 1)
    }

    function test_touch_long_press_title_does_not_pause() {
        const page = open()
        const title = page.titleItem
        const routed = []
        const seen = (name) => routed.push(name)
        router.routed.connect(seen)
        mousePress(title, title.width / 2, title.height / 2)
        compare(layer.holdTimer.running, true)
        wait(Ui.seekHoldMs + 150)
        mouseRelease(title, title.width / 2, title.height / 2)
        wait(0)
        wait(0)
        router.routed.disconnect(seen)
        compare(fakePlayer.calls, [])
        compare(routed.indexOf("x"), -1)
        compare(JSON.stringify(counters.infos), JSON.stringify([page.shown.id]))
        compare(counters.queueOpens, 0)
    }

    function test_info_sheet_routes_keys() {
        const page = open()
        page.sheet.showLines("Song", ["flac", "44100 hz", "16 bit", "Music / A / 01.flac"], 3)
        page.sheet.complete()
        compare(page.sheet.shown, true)
        compare(page.sheet.lines, true)
        compare(page.sheet.bar.visible, false)
        verify(router.dispatch("a", false))
        verify(router.dispatch("x", false))
        compare(fakePlayer.calls, [])
        compare(page.sheet.shown, true)
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(pages.depth, 2)
    }

    function test_touch_last_next_track_opens_queue() {
        const page = open()
        compare(page.nextRepeater.count, 3)
        tc.tapAt(page.nextRepeater.itemAt(2))
        compare(counters.queueOpens, 1)
        compare(fakePlayer.calls, [])
    }

    function test_touch_back_ring_returns() {
        const page = open()
        compare(pages.depth, 2)
        tc.tapAt(page.backRing)
        compare(pages.depth, 1)
    }

    function test_touch_first_tap_after_idle_restores() {
        const page = open({ animated: true })
        page.idle = true
        tc.tapAt(page.ring(1))
        compare(page.idle, false)
        compare(page.ringIndex, 0)
        compare(page.focusRow, 0)
        compare(fakePlayer.calls, [])
    }

    function test_lyrics_y_toggles() {
        fakeLyrics.set(tc.sampleLines())
        const page = open()
        compare(page.hasLyrics, true)
        compare(page.lyricsShown, false)
        verify(router.dispatch("y", false))
        compare(page.lyricsMode, true)
        compare(page.lyricsShown, true)
        compare(counters.queueOpens, 0)
        compare(page.artistItem.visible, false)
        compare(page.titleItem.visible, false)
        compare(page.artItem.visible, false)
        compare(page.progressItem.visible, false)
        compare(page.nextRepeater.count, 0)
        compare(page.lyricsShade, 1)
        verify(Qt.colorEqual(page.lyricsShadeItem.color, Ui.scrimSheet))
        compare(page.lyricsColumn.to, 1)
        compare(tc.columnTexts(page), ["First line", "*Second line", "Third line", "Fourth line", "Fifth line"])
        const cur = page.lyricsColumn.drawRepeater.itemAt(1)
        const curText = cur.children[1].children[0]
        compare(curText.font.pixelSize, Ui.fontPx(26))
        compare(curText.font.styleName, "Light")
        fuzzyCompare(cur.y + curText.y + curText.baselineOffset, Ui.px(117 + 18 + (31.2 + 0.7 * 26) / 2), 0.01)
        const up = page.lyricsColumn.drawRepeater.itemAt(2)
        const upText = up.children[1].children[0]
        compare(upText.font.pixelSize, Ui.fontPx(13))
        fuzzyCompare(up.opacity * up.children[1].opacity, 0.6, 1e-6)
        fuzzyCompare(up.y + upText.y + upText.baselineOffset, Ui.px(117 + 18 + 31.2 + (18 + 0.7 * 13) / 2), 0.01)
        fuzzyCompare(page.lyricsColumn.drawRepeater.itemAt(0).children[1].opacity, 0.4, 1e-6)
        verify(router.dispatch("back", false))
        compare(counters.queueOpens, 1)
        verify(router.dispatch("y", false))
        compare(page.lyricsShown, false)
        compare(page.lyricsShade, 0)
        compare(page.titleItem.visible, true)
    }

    function test_lyrics_y_without_lyrics_opens_queue() {
        const page = open()
        compare(page.hasLyrics, false)
        verify(router.dispatch("y", false))
        compare(page.lyricsMode, false)
        compare(counters.queueOpens, 1)
        fakeLyrics.pending = true
        fakeLyrics.lines = tc.sampleLines()
        fakeLyrics.changed()
        compare(page.hasLyrics, false)
        verify(router.dispatch("y", false))
        compare(page.lyricsMode, false)
        compare(counters.queueOpens, 2)
    }

    function test_lyrics_b_leaves_then_closes() {
        fakeLyrics.set(tc.sampleLines())
        const page = open()
        verify(router.dispatch("y", false))
        compare(page.lyricsShown, true)
        verify(router.dispatch("b", false))
        compare(page.lyricsShown, false)
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_lyrics_touch_art_enters_column_leaves() {
        fakeLyrics.set(tc.sampleLines())
        const page = open()
        tc.tapAt(page.artItem)
        compare(page.lyricsShown, true)
        compare(fakePlayer.calls, [])
        tc.tapAt(page.lyricsColumn.drawRepeater.itemAt(1))
        compare(page.lyricsShown, false)
        compare(counters.queueOpens, 0)
        compare(pages.depth, 2)
    }

    function test_lyrics_fade_and_toggle_ignored() {
        fakeLyrics.set(tc.sampleLines())
        const page = open({ animated: true })
        page.completeAll()
        verify(router.dispatch("y", false))
        compare(page.lyricsFadeAnimation.running, true)
        compare(page.lyricsMode, true)
        compare(page.lyricsShown, false)
        verify(router.dispatch("y", false))
        compare(page.lyricsMode, true)
        tryCompare(page, "lyricsShown", true, 1000)
        tryCompare(page, "lyricsToggling", false, 1000)
        compare(page.contentOpacity, 1)
        compare(page.lyricsShade, 1)
    }

    function test_lyrics_slide_duration() {
        const lines = [
            { ms: 0, text: "Zero" },
            { ms: 4000, text: "One" },
            { ms: 4200, text: "Two" },
            { ms: 9000, text: "Three" },
            { ms: 9050, text: "Four" },
            { ms: 20000, text: "Five" }
        ]
        fakeLyrics.set(lines)
        fakePlayer.position = 1000
        const page = open({ animated: true })
        page.completeAll()
        verify(router.dispatch("y", false))
        page.completeAll()
        compare(page.lyricsShown, true)
        compare(page.lyricsColumn.to, 0)
        fakePlayer.position = 4000
        compare(page.lyricsColumn.to, 1)
        compare(page.lyricsSlideAnimation.running, true)
        compare(page.lyricsSlideAnimation.duration, 200)
        compare(page.lyricsColumn.from, 0)
        page.lyricsSlideAnimation.complete()
        fakePlayer.position = 4250
        compare(page.lyricsColumn.to, 2)
        compare(page.lyricsSlideAnimation.running, true)
        compare(page.lyricsSlideAnimation.duration, 300)
        fuzzyCompare(page.lyricsSlideAnimation.from, 50 / 350, 1e-6)
        page.lyricsSlideAnimation.complete()
        fakePlayer.position = 9000
        compare(page.lyricsColumn.to, 3)
        compare(page.lyricsSlideAnimation.running, false)
        fakePlayer.position = 9100
        compare(page.lyricsColumn.to, 4)
        compare(page.lyricsSlideAnimation.running, true)
        compare(page.lyricsSlideAnimation.duration, 350 - 50)
        page.lyricsSlideAnimation.complete()
        fakePlayer.position = 16000
        compare(page.lyricsColumn.to, 4)
        fakePlayer.position = 20300
        compare(page.lyricsColumn.to, 5)
        compare(page.lyricsSlideAnimation.running, false)
    }

    function test_lyrics_due_timer_and_no_step_back() {
        fakeLyrics.set(tc.sampleLines())
        fakePlayer.position = 3900
        const page = open({ animated: true })
        page.completeAll()
        verify(router.dispatch("y", false))
        page.completeAll()
        compare(page.lyricsColumn.to, 0)
        compare(page.lyricsDueTimer.running, true)
        compare(page.lyricsDueTimer.interval, 100)
        tryCompare(page.lyricsColumn, "to", 1, 1000)
        compare(page.lyricsSlideAnimation.running, true)
        compare(page.lyricsSlideAnimation.duration, 350)
        fakePlayer.position = 3990
        compare(page.lyricsColumn.to, 1)
        fakePlayer.position = 1000
        compare(page.lyricsColumn.to, 0)
        fakePlayer.status = 2
        compare(page.lyricsDueTimer.running, false)
    }

    function test_lyrics_slide_frames() {
        fakeLyrics.set(tc.sampleLines())
        fakePlayer.position = 4000
        const page = open()
        verify(router.dispatch("y", false))
        const column = page.lyricsColumn
        column.from = 1
        column.to = 2
        column.p = 0
        const start = column.frame.map((e) => [e.d.fa === null ? null : e.d.fa.line, e.d.fb === null ? null : e.d.fb.line, e.top])
        compare(JSON.stringify(start.filter((e) => e[0] === 2)), JSON.stringify([[2, 2, Ui.npTextTop + 18 + 31.2]]))
        column.p = 1
        const end = column.frame.filter((e) => e.d.fb !== null && e.d.fb.line === 2)
        fuzzyCompare(end[0].top, Ui.npTextTop + 18, 1e-6)
        compare(end[0].an, 1)
        column.p = 0.25
        const mid = column.frame.filter((e) => e.d.fb !== null && e.d.fb.line === 1)[0]
        compare(mid.swap, true)
        fuzzyCompare(mid.ao, 0.5, 1e-6)
        compare(mid.an, 0)
        column.p = 0.75
        const late = column.frame.filter((e) => e.d.fb !== null && e.d.fb.line === 1)[0]
        compare(late.ao, 0)
        fuzzyCompare(late.an, 0.4 * 0.5, 1e-6)
        const zero = column.frame.filter((e) => e.d.fa !== null && e.d.fa.line === 0)[0]
        compare(zero.d.kind, "pair")
        compare(zero.d.fb.alpha, 0)
        fuzzyCompare(zero.top, Motion.lerp(Ui.npTextTop, Ui.npTextTop - 18, 0.75), 1e-6)
        fuzzyCompare(zero.an, 0.4 * 0.25, 1e-6)
        compare(column.frame.filter((e) => e.d.kind === "out").length, 0)
    }

    function test_lyrics_layouts() {
        fakeLyrics.set([{ ms: 8000, text: "Later" }, { ms: 9000, text: "Again" }])
        const page = open()
        verify(router.dispatch("y", false))
        compare(page.lyricsColumn.to, -1)
        compare(tc.columnTexts(page), ["Later", "Again"])
        fakeLyrics.set([], ["Plain one", "", "Plain two"])
        compare(page.lyricsColumn.plainShown, true)
        compare(page.lyricsColumn.plainRepeater.count, 3)
        fuzzyCompare(page.lyricsColumn.plainView.height, Ui.px(Math.floor((416 - 117) / 24) * 24), 0.01)
        const first = page.lyricsColumn.plainRepeater.itemAt(0)
        fuzzyCompare(first.y + first.baselineOffset, Ui.px(12 + 7), 0.6)
        compare(first.font.capitalization, Font.MixedCase)
        compare(first.font.pixelSize, Ui.fontPx(20))
        fakeLyrics.set([], [])
        compare(page.lyricsShown, true)
        compare(tc.columnTexts(page), ["*" + page.shown.title, page.nextTitles[0]])
        fakeLyrics.pending = true
        fakeLyrics.changed()
        compare(page.lyricsColumn.drawRepeater.count, 0)
    }

    function test_lyrics_long_current_takes_rows() {
        const long = "A very long current lyric line that keeps going and going until it needs many rows of text to fit"
        fakeLyrics.set([{ ms: 0, text: "Before" }, { ms: 4000, text: long }, { ms: 9000, text: "After" }])
        const page = open()
        verify(router.dispatch("y", false))
        const blocks = page.lyricsColumn.plan.b
        const cur = blocks.filter((b) => b.big)[0]
        verify(cur.rows >= 2)
        const item = page.lyricsColumn.drawRepeater.itemAt(blocks.indexOf(cur))
        const rows = item.children[1].children
        fuzzyCompare(rows[1].y - rows[0].y, Ui.px(31.2), 0.01)
        verify(cur.rows <= Math.floor((416 - 117) / 31.2))
        const bottom = Math.max.apply(null, blocks.map((b) => b.top + b.rows * b.pitch))
        verify(bottom <= 416 + 1e-6)
    }

    function test_lyrics_landscape_column() {
        tc.setWindow(960, 640)
        fakeLyrics.set(tc.sampleLines())
        const page = open()
        verify(router.dispatch("y", false))
        compare(page.lyricsColumn.textLeft, Ui.npColumnRight)
        compare(page.lyricsColumn.textTop, Ui.npArtistBaselineLandscape)
        compare(page.lyricsColumn.textBottom, Ui.npNextBaselineLandscape + 36)
        const item = page.lyricsColumn.drawRepeater.itemAt(0)
        fuzzyCompare(item.x, Ui.px(Ui.npColumnRight), 0.01)
        compare(item.width, Ui.px(Ui.canvasW - Ui.edge - Ui.npColumnRight))
    }

    function test_lyrics_variant_ends_mode() {
        fakeLyrics.set(tc.sampleLines())
        const page = open()
        verify(router.dispatch("y", false))
        compare(page.lyricsShown, true)
        fakeBluez.activate()
        compare(page.shownVariant, true)
        compare(page.lyricsMode, false)
        compare(page.lyricsShown, false)
        compare(page.lyricsShade, 0)
    }

    function test_lyrics_mark() {
        const page = open()
        compare(page.lyricsMark.visible, false)
        const first = page.nextRepeater.itemAt(0)
        compare(first.clip, false)
        fakeLyrics.set(tc.sampleLines())
        compare(page.markShown, true)
        const mark = page.lyricsMark
        compare(mark.visible, true)
        compare(mark.name, "mic")
        verify(mark.known)
        verify(mark.filled)
        const box = 13 * 0.7 * 32 / 27
        fuzzyCompare(mark.width, Ui.px(box), 1e-6)
        fuzzyCompare(mark.x + mark.width * 24 / 32, Ui.px(320 - 16), 1e-6)
        fuzzyCompare(mark.y + mark.height * 30 / 32, Ui.px(362 + 18), 1e-6)
        fuzzyCompare(mark.y + mark.height * 3 / 32, Ui.px(362 + 18 - 9.1), 1e-6)
        verify(Qt.colorEqual(mark.color, Ui.white(0.6)))
        compare(first.clip, true)
        fuzzyCompare(first.x + first.width, page.markLeft - Ui.px(8), 1e-6)
        fuzzyCompare(page.markLeft, Ui.px(320 - 16) - Ui.px(box) * 16 / 32, 1e-6)
        compare(page.nextRepeater.itemAt(1).clip, false)
        fakeLyrics.set([], ["Plain"])
        compare(mark.visible, true)
        fakeLyrics.pending = true
        fakeLyrics.changed()
        compare(mark.visible, false)
        fakeLyrics.set([], [])
        compare(mark.visible, false)
        fakeLyrics.set(tc.sampleLines())
        verify(router.dispatch("y", false))
        compare(page.lyricsShown, true)
        compare(mark.visible, false)
        verify(router.dispatch("y", false))
        compare(mark.visible, true)
        fakeBluez.activate()
        compare(page.shownVariant, true)
        compare(mark.visible, false)
    }

    function test_lyrics_mark_without_next_track() {
        fakeQueue.fill(1)
        fakePlayer.select(0)
        fakeLyrics.set(tc.sampleLines())
        const page = open()
        compare(page.nextRepeater.count, 0)
        compare(page.lyricsMark.visible, true)
    }

    function test_lyrics_mark_landscape() {
        tc.setWindow(960, 640)
        fakeLyrics.set(tc.sampleLines())
        const page = open()
        const mark = page.lyricsMark
        fuzzyCompare(mark.x + mark.width * 24 / 32, Ui.px(480 - 16), 1e-6)
        fuzzyCompare(mark.y + mark.height * 30 / 32, Ui.px(Ui.npNextBaselineLandscape), 1e-6)
    }

    function test_touch_art_does_nothing() {
        const page = open()
        tc.tapAt(page.artItem)
        compare(fakePlayer.calls, [])
        compare(counters.queueOpens, 0)
        compare(pages.depth, 2)
    }
}
