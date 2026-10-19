import QtCore
import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "Saver"
    when: windowShown
    visible: true
    width: 320
    height: 240

    Rectangle {
        id: artSample

        width: 8
        height: 8
        color: "steelblue"
    }

    QtObject {
        id: fakePower

        property bool screenOff: false
        property bool switching: false
        property var verbs: []

        signal showSwitchPrompt()
        signal showPowerMenu()
        signal switchFailed()
        signal volumeChanged(int volume)

        function runVerb(verb) {
            fakePower.verbs = fakePower.verbs.concat([verb])
            return true
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 1
        property var current: ({ id: 1, title: "Sticking With You", artist: "Addison Road", album: "Addison Road", artPath: "" })
        property real position: 0
        property real duration: 1000
        property QtObject queue: QtObject {
            property int count: 1
            property int index: 0
            property bool shuffle: false
            property int repeat: 0

            function get(row) {
                return fakePlayer.current
            }
        }
    }

    QtObject {
        id: fakeBluez

        property bool connected: true
        property bool transportActive: true
        property string deviceName: "Pixel 8"
        property string title: "Phone Song"
        property string artist: "Phone Artist"
        property string album: "Phone Album"
        property string artUrl: ""
        property string status: "playing"
    }

    QtObject {
        id: fakeSettings

        property string output: "auto"
        property string btCodec: "auto"
        property var roots: []
        property string accent: "#FA2A55"
        property real scale: 1.0
        property int saverSeconds: 1
        property bool overlayToasts: false
        property int screenOffSeconds: 0
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
        property bool saverLyrics: false
    }

    QtObject {
        id: fakeLyrics

        property bool pending: false
        property var lines: []
        property var plain: []

        signal changed()

        function lineAt(ms) {
            let found = -1
            for (let i = 0; i < fakeLyrics.lines.length; ++i) {
                if (fakeLyrics.lines[i].ms <= ms)
                    found = i
            }
            return found
        }
    }

    Main {
        id: main

        power: fakePower
        player: fakePlayer
        settings: fakeSettings
        lyrics: fakeLyrics
        shotWidth: 640
        shotHeight: 960
    }

    readonly property var saver: main.saver
    readonly property var home: main.pageStack.currentItem

    function sampleArt(name) {
        const url = String(StandardPaths.writableLocation(StandardPaths.TempLocation)) + "/" + name
        let saved = false
        artSample.grabToImage((result) => { saved = result.saveToFile(url.replace("file://", "")) })
        tryVerify(() => saved, 2000)
        return url
    }

    function init() {
        tryCompare(main.pageStack, "inputLocked", false, 5000)
        main.pageStack.completeAll()
        fakeBluez.artUrl = ""
        fakePower.screenOff = false
        fakePower.verbs = []
        fakePlayer.status = 1
        fakeSettings.saverSeconds = 1
        fakeSettings.saverLyrics = false
        fakeLyrics.pending = false
        fakeLyrics.lines = []
        fakeLyrics.changed()
        fakePlayer.position = 0
        saver.hide()
        saver.land()
        saver.dimTimer.interval = Motion.dimDelay
        home.completeAll()
        main.armSaver()
    }

    function test_layout_and_roles() {
        saver.show()
        saver.land()
        compare(saver.shown, true)
        compare(saver.visible, true)
        compare(saver.opacity, 1)
        compare(saver.title, "Sticking With You")
        compare(saver.artist, "Addison Road")
        compare(saver.album, "Addison Road")
        compare(saver.artItem.x, Ui.px(16))
        fuzzyCompare(saver.artItem.y, Ui.px(480 - 24 - 56), 0.01)
        compare(saver.artItem.width, Ui.px(56))
        compare(saver.artItem.height, Ui.px(56))
        fuzzyCompare(saver.textGroup.x, Ui.px(16 + 56 + 8), 0.01)
        compare(saver.titleItem.font.pixelSize, Ui.fontPx(32))
        compare(saver.titleItem.font.styleName, "Light")
        fuzzyCompare(saver.titleItem.color.a, 1.0, 1 / 255)
        compare(saver.artistItem.font.pixelSize, Ui.fontPx(20))
        fuzzyCompare(saver.artistItem.color.a, 0.85, 1 / 255)
        compare(saver.albumItem.font.pixelSize, Ui.fontPx(16))
        fuzzyCompare(saver.albumItem.color.a, 0.6, 1 / 255)
        fuzzyCompare(saver.titleItem.y + saver.titleItem.baselineOffset, saver.artItem.y + Ui.px(0.7 * 32), 0.5)
        fuzzyCompare(saver.artistItem.y + saver.artistItem.baselineOffset, saver.titleItem.y + saver.titleItem.baselineOffset + Ui.px(20), 0.5)
        fuzzyCompare(saver.albumItem.y + saver.albumItem.baselineOffset, saver.artistItem.y + saver.artistItem.baselineOffset + Ui.px(16), 0.5)
        compare(saver.clockItem.font.pixelSize, Ui.fontPx(50))
        compare(saver.clockItem.font.styleName, "Light")
        fuzzyCompare(saver.clockItem.x + saver.clockItem.width, main.width - Ui.px(16), 0.5)
        fuzzyCompare(saver.clockItem.y + saver.clockItem.baselineOffset, Ui.px(16 + 0.7 * 50), 0.5)
        verify(/^\d{1,2}:\d{2}$/.test(saver.clockText))
        compare(saver.formatClock(new Date(2026, 9, 6, 7, 13)), "7:13")
        compare(saver.formatClock(new Date(2026, 9, 6, 0, 5)), "0:05")
        saver.clockMode = "12h"
        compare(saver.formatClock(new Date(2026, 9, 6, 19, 30)), "7:30")
        compare(saver.formatClock(new Date(2026, 9, 6, 0, 5)), "12:05")
        saver.clockMode = "off"
        compare(saver.formatClock(new Date(2026, 9, 6, 7, 13)), "")
        saver.clockMode = "24h"
        compare(saver.clockTimer.interval, 1000)
        compare(saver.clockTimer.running, true)
        compare(saver.drifting, true)
        compare(Motion.saverDrift, 18000)
        compare(Motion.saverDriftFrom, 8)
        compare(Motion.saverDriftTo, -27)
    }

    function test_lyric_line() {
        fakeLyrics.lines = [{ ms: 1000, text: "First line" }, { ms: 2000, text: "" }, { ms: 3000, text: "Third line" }]
        fakeLyrics.changed()
        fakePlayer.position = 1500
        saver.show()
        saver.land()
        compare(saver.shownLyric, "")
        compare(saver.lyricItem.visible, false)

        fakeSettings.saverLyrics = true
        compare(saver.lyricsOn, true)
        compare(saver.shownLyric, "First line")
        compare(saver.lyricItem.visible, true)
        compare(saver.lyricItem.x, Ui.px(16))
        compare(saver.lyricItem.width, main.width - 2 * Ui.px(16))
        compare(saver.lyricItem.clip, true)
        compare(saver.lyricItem.font.pixelSize, Ui.fontPx(13))
        compare(saver.lyricItem.font.styleName, "Light")
        fuzzyCompare(saver.lyricItem.color.a, 0.6, 1 / 255)
        fuzzyCompare(saver.lyricItem.y + saver.lyricItem.baselineOffset,
                     saver.artItem.y - Ui.px(16) + Ui.px(saver.drift), 0.5)
        verify(saver.lyricItem.y + saver.lyricItem.height - Ui.px(saver.drift) + Ui.px(Motion.saverDriftFrom)
               <= saver.artItem.y + 0.5)

        fakePlayer.position = 1900
        compare(saver.lyricSwap.running, false)
        fakePlayer.position = 3100
        compare(saver.lyricSwap.running, true)
        compare(Motion.valueChange, 200)
        compare(Motion.valueDip, 0.2)
        tryCompare(saver, "shownLyric", "Third line", 1000)
        tryCompare(saver.lyricSwap, "running", false, 1000)
        compare(saver.lyricItem.opacity, 1)

        fakePlayer.position = 2500
        tryCompare(saver, "shownLyric", "", 1000)
        compare(saver.lyricItem.visible, false)
        fakePlayer.position = 500
        compare(saver.lyricText, "")

        fakePlayer.position = 3500
        tryCompare(saver, "shownLyric", "Third line", 1000)
        tryCompare(saver.lyricItem, "opacity", 1, 1000)

        fakePlayer.position = 1500
        compare(saver.lyricSwap.running, true)
        fakePlayer.position = 3500
        compare(saver.lyricSwap.running, false)
        compare(saver.lyricItem.opacity, 1)
        compare(saver.shownLyric, "Third line")

        fakePlayer.position = 2500
        tryCompare(saver, "shownLyric", "", 1000)
        tryCompare(saver.lyricSwap, "running", false, 1000)
        fakePlayer.position = 3500
        compare(saver.shownLyric, "Third line")
        compare(saver.lyricSwap.running, false)
        tryCompare(saver.lyricItem, "opacity", 1, 1000)
        fakeLyrics.lines = []
        fakeLyrics.plain = ["Plain text"]
        fakeLyrics.changed()
        tryCompare(saver, "shownLyric", "", 1000)

        fakeLyrics.lines = [{ ms: 0, text: "Pending" }]
        fakeLyrics.pending = true
        fakeLyrics.changed()
        compare(saver.lyricText, "")
        fakeLyrics.pending = false
        fakeLyrics.changed()
        tryCompare(saver, "shownLyric", "Pending", 1000)

        fakePower.screenOff = true
        fakeLyrics.lines = [{ ms: 0, text: "Off" }]
        fakeLyrics.changed()
        compare(saver.lyricSwap.running, false)
        compare(saver.shownLyric, "Off")
        fakePower.screenOff = false
        fakeSettings.saverLyrics = false
        compare(saver.shownLyric, "")
    }

    function test_track_art_in_one_album() {
        const first = fakePlayer.current
        fakePlayer.current = { id: 1, title: "One", artist: "Addison Road", album: "Addison Road", artPath: "/tmp/one.png" }
        compare(saver.artUrl, "file:///tmp/one.png")
        fakePlayer.current = { id: 2, title: "Two", artist: "Addison Road", album: "Addison Road", artPath: "/tmp/two.png" }
        compare(saver.artUrl, "file:///tmp/two.png")
        fakePlayer.current = first
        compare(saver.artUrl, "")
    }

    function test_timer_shows_after_saver_seconds() {
        compare(main.saverArmed, true)
        compare(main.saverTimer.running, true)
        compare(main.saverTimer.interval, 1000)
        compare(saver.shown, false)
        tryCompare(saver, "shown", true, 3000)
        compare(saver.showAnimation.duration, 380)
        tryCompare(saver, "progress", 1, 2000)
        compare(main.pageStack.opacity, 0)
        compare(main.saverTimer.running, false)
    }

    function test_dim_after_delay() {
        compare(saver.dimTimer.interval, 7000)
        saver.dimTimer.interval = 100
        saver.show()
        saver.land()
        tryCompare(saver.dimTimer, "running", true, 1000)
        compare(saver.dimmed, false)
        compare(saver.dimLayer.opacity, 0)
        tryCompare(saver, "dimmed", true, 2000)
        compare(saver.dimAnimation.running, true)
        compare(saver.dimAnimation.duration, 380)
        compare(saver.dimAnimation.to, 0.6)
        tryCompare(saver.dimLayer, "opacity", 0.6, 2000)
        verify(Qt.colorEqual(saver.dimLayer.color, Ui.black))
        compare(saver.dimTimer.running, false)
    }

    function test_any_action_returns() {
        saver.show()
        saver.land()
        saver.dimmed = true
        saver.land()
        compare(saver.dimLayer.opacity, 0.6)
        const focus = home.focusIndex
        compare(main.router.saverUp, true)
        verify(main.router.dispatch("down", false))
        compare(home.focusIndex, focus)
        compare(saver.shown, false)
        compare(saver.hideAnimation.running, true)
        tryCompare(saver, "progress", 0, 2000)
        tryCompare(saver, "visible", false, 1000)
        compare(saver.dimLayer.opacity, 0)
        compare(main.pageStack.opacity, 1)
        compare(main.saverTimer.running, true)
        verify(main.router.dispatch("down", false))
        compare(home.focusIndex, focus + 1)
    }

    function test_lyrics_mode_holds_saver_while_playing() {
        fakeLyrics.lines = [{ ms: 0, text: "One" }, { ms: 4000, text: "Two" }]
        fakeLyrics.changed()
        main.openNowPlaying()
        main.pageStack.completeAll()
        const np = main.pageStack.currentItem
        compare(np.objectName, "nowplaying")
        compare(np.hasLyrics, true)
        compare(main.saverArmed, true)
        np.toggleLyrics()
        np.completeAll()
        compare(np.lyricsShown, true)
        compare(main.lyricsHold, true)
        compare(main.saverArmed, false)
        compare(main.saverTimer.running, false)
        fakeSettings.screenOffSeconds = 1
        compare(main.screenOffArmed, false)
        fakePlayer.status = 2
        compare(main.lyricsHold, false)
        compare(main.saverTimer.running, false)
        compare(main.screenOffArmed, true)
        fakePlayer.status = 1
        compare(main.saverArmed, false)
        np.toggleLyrics()
        np.completeAll()
        compare(main.lyricsHold, false)
        compare(main.saverArmed, true)
        compare(main.saverTimer.running, true)
        fakeSettings.screenOffSeconds = 0
        main.pageStack.pop()
        main.pageStack.completeAll()
        compare(main.pageStack.currentItem, home)
    }

    function test_keyboard_hosted() {
        const kb = main.keyboard
        compare(main.saverArmed, true)
        kb.open({ title: "name", kind: "text" })
        kb.complete()
        compare(main.router.modal, kb)
        compare(main.router.modalUp, true)
        compare(main.saverArmed, false)
        compare(main.touchLayer.dialogs.indexOf(kb) >= 0, true)
        const focus = home.focusIndex
        verify(main.router.dispatch("right", false))
        compare(kb.focusCode, "2")
        compare(home.focusIndex, focus)
        verify(main.router.dispatch("b", false))
        compare(kb.shown, false)
        compare(main.saverArmed, true)
        let canceled = 0
        const count = () => canceled++
        kb.canceled.connect(count)
        kb.open({ title: "name", kind: "text" })
        kb.entry.type("a")
        main.navigate("music")
        main.pageStack.completeAll()
        compare(kb.shown, false)
        compare(canceled, 1)
        main.pageStack.pop()
        main.pageStack.completeAll()
        kb.open({ title: "name", kind: "text" })
        kb.entry.type("a")
        fakePower.screenOff = true
        compare(kb.shown, false)
        compare(kb.progress, 0)
        compare(canceled, 2)
        fakePower.screenOff = false
        kb.canceled.disconnect(count)
        main.powerMenu.open()
        kb.open({ title: "name", kind: "text" })
        compare(main.router.modal, main.powerMenu)
        main.powerMenu.close()
        main.powerMenu.complete()
        compare(main.router.modal, kb)
        kb.validator = (text) => "no"
        kb.open({ title: "name", kind: "text" })
        compare(kb.validator, null)
        kb.open({ title: "name", kind: "text", validator: (text) => text === "x" ? "taken" : "" })
        kb.entry.type("x")
        kb.finish()
        compare(kb.shown, true)
        kb.entry.erase()
        kb.entry.type("y")
        kb.finish()
        compare(kb.shown, false)
        kb.complete()
        kb.open({ title: "name", kind: "text" })
        fakePower.showSwitchPrompt()
        compare(main.switchPrompt.shown, true)
        compare(kb.shown, false)
        compare(main.router.modal, main.switchPrompt)
        main.switchPrompt.close()
        main.switchPrompt.complete()
        kb.complete()
    }

    function test_saver_waits_for_overlays() {
        compare(main.saverArmed, true)
        main.openNowPlaying()
        main.pageStack.completeAll()
        const np = main.pageStack.currentItem
        np.sheet.showLines("Song", ["flac"], 0)
        np.sheet.complete()
        compare(main.overlayOpen, true)
        compare(main.saverArmed, false)
        compare(main.saverTimer.running, false)
        np.sheet.close()
        np.sheet.complete()
        compare(main.saverArmed, true)
        main.keyboard.open({ title: "name", kind: "text" })
        compare(main.saverArmed, false)
        main.keyboard.close()
        main.keyboard.complete()
        compare(main.saverArmed, true)
        main.pageStack.pop()
        main.pageStack.completeAll()
    }

    function test_saver_phone_source() {
        fakePlayer.status = 2
        compare(main.saverArmed, false)
        fakeSettings.saverLyrics = true
        fakeLyrics.lines = [{ ms: 0, text: "Local line" }]
        fakeLyrics.changed()
        main.bluez = fakeBluez
        compare(main.saverArmed, true)
        saver.show()
        saver.land()
        compare(saver.phone, true)
        compare(saver.title, "Phone Song")
        compare(saver.artist, "Phone Artist")
        compare(saver.album, "from Pixel 8")
        fakeBluez.deviceName = ""
        compare(saver.album, "")
        compare(saver.albumItem.text, "")
        fakeBluez.deviceName = "Pixel 8"
        compare(saver.phoneGlyph.visible, true)
        fuzzyCompare(saver.phoneGlyph.width, Ui.px(64 / 168 * 56), 0.01)
        verify(Qt.colorEqual(saver.artItem.color, "transparent"))
        compare(saver.shownLyric, "")
        compare(saver.artImage.visible, false)
        const art = tc.sampleArt("microdap-saver-art.png")
        fakeBluez.artUrl = art
        tryCompare(saver.artImage, "status", Image.Ready, 2000)
        compare(saver.phoneGlyph.visible, false)
        compare(saver.artImage.visible, true)
        compare(String(saver.artImage.source), art)
        compare(saver.artImage.width, Ui.px(56))
        verify(Qt.colorEqual(saver.artItem.color, Ui.tile))
        fakeBluez.artUrl = ""
        compare(saver.phoneGlyph.visible, true)
        compare(saver.artImage.visible, false)
        fakeBluez.artUrl = art.replace("microdap-saver-art.png", "microdap-saver-missing.png")
        tryCompare(saver.artImage, "status", Image.Error, 2000)
        compare(saver.phoneGlyph.visible, true)
        verify(Qt.colorEqual(saver.artItem.color, "transparent"))
        fakeBluez.artUrl = art
        fakeBluez.status = "paused"
        saver.hide()
        saver.land()
        compare(main.saverArmed, false)
        fakeBluez.status = "playing"
        fakeBluez.transportActive = false
        compare(saver.phone, false)
        compare(saver.title, "Sticking With You")
        compare(saver.phoneGlyph.visible, false)
        verify(String(saver.artImage.source) !== art)
        main.bluez = null
        fakeBluez.transportActive = true
        fakePlayer.status = 1
        fakeSettings.saverLyrics = false
    }

    function test_not_armed_when_paused() {
        fakePlayer.status = 2
        compare(main.saverArmed, false)
        compare(main.saverTimer.running, false)
        fakePlayer.status = 1
        compare(main.saverTimer.running, true)
        fakeSettings.saverSeconds = 30
        compare(main.saverTimer.interval, 30000)
    }

    function test_action_restarts_timer() {
        compare(main.saverTimer.running, true)
        wait(600)
        main.router.dispatch("down", false)
        wait(600)
        compare(saver.shown, false)
        tryCompare(saver, "shown", true, 2000)
    }

    function test_screen_off_lands() {
        saver.show()
        compare(saver.showAnimation.running, true)
        fakePower.screenOff = true
        compare(saver.screenOff, true)
        compare(saver.shown, false)
        compare(saver.progress, 0)
        compare(saver.animating, false)
        compare(saver.clockTimer.running, false)
        compare(saver.dimTimer.running, false)
        compare(saver.drifting, false)
        compare(main.saverTimer.running, false)
        fakePower.screenOff = false
        compare(saver.shown, false)
        compare(main.pageStack.currentItem, home)
        compare(main.saverTimer.running, true)
    }

    function test_dialogs_block_saver() {
        fakePower.showPowerMenu()
        compare(main.powerMenu.shown, true)
        compare(main.saverArmed, false)
        main.powerMenu.close()
        main.powerMenu.complete()
        compare(main.saverArmed, true)
        saver.show()
        saver.land()
        fakePower.showPowerMenu()
        compare(saver.shown, false)
        compare(main.powerMenu.shown, true)
        main.powerMenu.close()
        main.powerMenu.complete()
    }

    function test_fixed_clock() {
        saver.fixedClock = "7:13"
        compare(saver.clockText, "7:13")
        saver.show()
        saver.land()
        compare(saver.clockTimer.running, false)
        compare(saver.clockText, "7:13")
        saver.fixedClock = ""
        compare(saver.clockTimer.running, true)
    }
}
