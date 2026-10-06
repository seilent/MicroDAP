import QtQuick
import QtTest
import Casdap

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

    QtObject {
        id: fakeQueue

        property var rows: []
        property int count: 0
        property int index: 0
        property bool shuffle: false
        property int repeat: 0

        function get(row) {
            return fakeQueue.rows[row]
        }

        function fill(n) {
            const rows = []
            for (let i = 0; i < n; i++)
                rows.push({ id: i + 1, title: "Song " + (i + 1), artist: i < 4 ? "Addison Road" : "Other Artist", album: "Album " + (i < 4 ? 1 : 2), artPath: "", durationMs: 240000 })
            fakeQueue.rows = rows
            fakeQueue.count = n
            fakeQueue.index = 0
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
        id: counters

        property int queueOpens: 0
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
            onOpenQueue: counters.queueOpens++
        }
    }

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.windowW = w
        Ui.windowH = h
        pages.width = w
        pages.height = h
        router.width = w
        router.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        pages.clear()
        pages.animate = false
        fakeQueue.fill(8)
        fakePlayer.status = 1
        fakePlayer.position = 5000
        fakePlayer.duration = 240000
        fakePlayer.calls = []
        fakePlayer.select(0)
        fakeBluez.reset()
        fakeStatus.battery = 73
        fakeStatus.batteryPresent = true
        counters.queueOpens = 0
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
        compare(page.backRing.x, Ui.px(4))
        compare(page.backRing.y, Ui.px(4))
        compare(page.backRing.width, Ui.px(64))
        compare(page.backRing.glyph, "back")
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
        compare(page.progressItem.width, 640 - 2 * Ui.px(16))
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
        compare(page.backdropA.source.toString(), "")
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
        compare(counters.queueOpens, 1)
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
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "seek 0")
        verify(router.dispatch("rt", false))
        compare(fakePlayer.calls[fakePlayer.calls.length - 1], "seek 15000")
        verify(router.dispatch("y", false))
        compare(counters.queueOpens, 1)
        verify(router.dispatch("back", false))
        compare(counters.queueOpens, 2)
        compare(pages.depth, 2)
        verify(router.dispatch("start", false))
        compare(pages.depth, 1)
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
        compare(pages.incoming.easingType, Motion.easeIn)
        compare(pages.outgoing.easingType, Motion.pop)
        tryCompare(pages, "phase", "enter", 2000)
        verify(page.contentScale <= 1)
        verify(page.contentScale >= 0.9)
        tryCompare(pages, "phase", "idle", 2000)
        compare(page.contentScale, 1)
        compare(pages.incoming, null)
        verify(router.dispatch("b", false))
        compare(pages.transitionKind, "fade")
        compare(pages.outgoing.duration, 300)
        compare(pages.incoming.easingType, Motion.pop)
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

    function test_track_change_lands_when_not_live() {
        const page = open()
        fakePlayer.select(5)
        compare(page.trackAnimating, false)
        compare(page.shown.id, 6)
        compare(page.artistItem.text, "Other Artist")
        compare(page.nextRepeater.count, 2)
    }

    function test_backdrop_crossfade() {
        fakeQueue.rows[0].artPath = "/tmp/a.png"
        fakeQueue.rows[5].artPath = "/tmp/b.png"
        fakePlayer.select(0)
        const page = open({ animated: true })
        compare(page.backdropA.source.toString(), "file:///tmp/a.png")
        compare(page.frontIsA, true)
        fakePlayer.select(5)
        tryCompare(page, "crossfading", true, 3000)
        compare(page.backdropB.source.toString(), "file:///tmp/b.png")
        compare(page.crossfadeAnimation.duration, 2000)
        verify(page.backdropB.opacity < 1)
        tryCompare(page, "crossfading", false, 4000)
        compare(page.frontIsA, false)
        compare(page.backdropA.source.toString(), "")
        compare(page.backdropB.opacity, 1)
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
        fuzzyCompare(tc.baselineOf(page.countItem), Ui.px(480 - 118 + 18), 0.5)
        compare(page.nextRepeater.count, 0)
        compare(page.progressItem.visible, true)
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
        compare(page.backdropA.animating, false)
        page.screenOff = false
        compare(page.idleTimer.running, true)
    }
}
