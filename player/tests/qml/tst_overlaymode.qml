import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "OverlayMode"
    when: windowShown
    visible: true
    width: 320
    height: 240

    QtObject {
        id: fakeState

        property var saved: []

        function stack() {
            return fakeState.saved
        }

        function setStack(list) {
            fakeState.saved = list
        }
    }

    QtObject {
        id: fakeLibrary

        property int playlistCount: 0

        function artistInfo(id) {
            return id === 7 ? { id: 7, name: "Addison Road", artPath: "" } : {}
        }

        function albumInfo(id) {
            return id === 12 ? { id: 12, title: "Stories", artist: "Addison Road", year: 0, artPath: "" } : {}
        }

        function artistAlbums(id) {
            return id === 7 ? [12] : []
        }

        function artistTracks(id) {
            return [1, 2]
        }

        function trackRows(ids) {
            return ids.map((i) => ({ id: i, title: "Song " + i, artist: "Addison Road", album: "Stories", albumId: 12, durationMs: 1000 }))
        }

        function albumTracks(id) {
            return id === 12 ? fakeLibrary.trackRows([1, 2]) : []
        }

        function artistAlbumRows(id) {
            return id === 7 ? [fakeLibrary.albumInfo(12)] : []
        }

        function artistTrackRows(id) {
            return fakeLibrary.trackRows([1, 2])
        }

        function isPinned(kind, id) {
            return false
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 1
        property var current: ({ id: 1, title: "Song 1", artist: "Addison Road", album: "Stories", artPath: "" })
        property real position: 0
        property real duration: 1000
        property QtObject queue: QtObject {
            property int count: 2
            property int index: 0
            property bool shuffle: false
            property int repeat: 0

            function get(row) {
                return { id: row + 1, title: "Song " + (row + 1) }
            }
        }
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
        property int screenOffSeconds: 1
        property int suspendMinutes: 10
        property bool wifiOffScreenOff: false
    }

    QtObject {
        id: fakeBridge

        property int closes: 0

        function closePlayer() {
            fakeBridge.closes++
        }
    }

    Main {
        id: overlayMain

        overlay: true
        overlayMapped: false
        overlayFade: 0
        shotWidth: 960
        shotHeight: 640
        library: fakeLibrary
        player: fakePlayer
        settings: fakeSettings
        stateStore: fakeState
    }

    function collect(obj, out) {
        if (obj === null || obj === undefined)
            return
        const res = obj.resources
        if (res !== undefined) {
            for (let i = 0; i < res.length; i++) {
                const r = res[i]
                if (r.interval !== undefined && r.triggeredOnStart !== undefined)
                    out.timers.push(r)
                else if (r.running !== undefined && r.loops !== undefined)
                    out.animations.push(r)
            }
        }
        const kids = obj.children
        if (kids === undefined)
            return
        for (let i = 0; i < kids.length; i++)
            collect(kids[i], out)
    }

    function init() {
        overlayMain.overlayMapped = false
        overlayMain.overlayFade = 0
        fakeSettings.overlayToasts = false
        fakeSettings.saverSeconds = 1
        fakePlayer.status = 1
        fakePlayer.current = { id: 1, title: "Song 1", artist: "Addison Road", album: "Stories", artPath: "" }
        fakeBridge.closes = 0
        if (overlayMain.toastShown)
            overlayMain.endHiddenToast()
        overlayMain.restoreStack([])
        overlayMain.pageStack.completeAll()
        overlayMain.lastToastId = 1
    }

    function names() {
        return overlayMain.pageStack.serialize().map((e) => e.name)
    }

    function test_hidden_state() {
        compare(overlayMain.overlay, true)
        compare(overlayMain.hidden, true)
        compare(overlayMain.off, true)
        compare(overlayMain.visible, false)
        compare(overlayMain.visibility, Window.Hidden)
        verify(Qt.colorEqual(overlayMain.color, Ui.bg))
        compare(overlayMain.pageStack.depth, 1)
        compare(overlayMain.pageStack.visible, false)
        compare(overlayMain.overlayLayer.visible, false)
        compare(overlayMain.overlayLayer.screenOff, true)
        verify(Qt.colorEqual(overlayMain.background.color, Ui.bg))
        compare(overlayMain.background.parent, overlayMain.rotor)
        compare(overlayMain.rotor.parent, overlayMain.contentItem)
        compare(overlayMain.background.width, overlayMain.rotor.width)
        compare(overlayMain.background.height, overlayMain.rotor.height)
        compare(overlayMain.rotor.rotation, 0)
        compare(overlayMain.contentItem.opacity, 0)
        compare(overlayMain.saverTimer.running, false)
        compare(overlayMain.screenOffTimer.running, false)
        compare(overlayMain.saver.shown, false)
        compare(overlayMain.toastWindow.visible, false)
        const found = { timers: [], animations: [] }
        collect(overlayMain.contentItem, found)
        verify(found.timers.length > 0)
        for (const t of found.timers)
            compare(t.running, false)
        for (const a of found.animations)
            verify(!a.running || a.paused)
    }

    function test_show_maps_and_hide_unmaps() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        compare(overlayMain.hidden, false)
        compare(overlayMain.off, false)
        compare(overlayMain.pageStack.visible, true)
        compare(overlayMain.overlayLayer.visible, true)
        compare(overlayMain.overlayLayer.screenOff, false)
        compare(overlayMain.background.visible, true)
        compare(overlayMain.contentItem.opacity, 1)
        overlayMain.overlayFade = 0.5
        compare(overlayMain.contentItem.opacity, 0.5)
        overlayMain.overlayMapped = false
        compare(overlayMain.hidden, true)
        compare(overlayMain.pageStack.visible, false)
    }

    function test_show_keeps_home_rest_state() {
        const home = overlayMain.pageStack.currentItem
        compare(home.objectName, "home")
        compare(home.animated, false)
        compare(home.overlay, true)
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        compare(home.animated, true)
        compare(home.quickplayShown, false)
        compare(home.pan, 0)
        compare(home.peekEnter, 1)
        compare(home.barEnter, 1)
        compare(home.menuRepeater.count, 5)
        for (let i = 0; i < home.menuRepeater.count; i++) {
            const item = home.menuRepeater.itemAt(i)
            compare(item.enter, 1)
            compare(item.opacity, 1)
            compare(item.x, Ui.px(Ui.homeTextX))
        }
        compare(home.watermark.opacity, 1)
        compare(home.focusBar.opacity, 1)
        compare(home.quickplay.active, false)
        compare(home.quickplay.flick.contentY, 0)
        compare(home.quickplay.scrolling, false)
        overlayMain.overlayMapped = false
        compare(home.animated, false)
        overlayMain.overlayMapped = true
        for (let i = 0; i < home.menuRepeater.count; i++)
            compare(home.menuRepeater.itemAt(i).opacity, 1)
        compare(home.focusBar.opacity, 1)
    }

    function test_menu_lists_dap_items() {
        const home = overlayMain.pageStack.currentItem
        const names = ["music", "bluetooth", "playlists", "history", "settings"]
        for (let i = 0; i < 5; i++)
            compare(home.menuItem(i).modelData.name, names[i])
    }

    function test_stack_serialize() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        overlayMain.navigate("music")
        overlayMain.openAlbum(12)
        overlayMain.pageStack.completeAll()
        const list = overlayMain.pageStack.serialize()
        compare(list.length, 3)
        compare(list[0].name, "home")
        compare(list[0].id, -1)
        compare(list[1].name, "music")
        compare(list[2].name, "album")
        compare(list[2].id, 12)
        compare(JSON.stringify(fakeState.saved), JSON.stringify(list))
        overlayMain.pageStack.pop()
        overlayMain.pageStack.completeAll()
        compare(fakeState.saved.length, 2)
    }

    function test_stack_restore() {
        overlayMain.restoreStack([{ name: "home", id: -1 }, { name: "music", id: -1 }, { name: "album", id: 12 }])
        compare(JSON.stringify(tc.names()), JSON.stringify(["home", "music", "album"]))
        compare(overlayMain.pageStack.currentItem.albumId, 12)
        compare(overlayMain.pageStack.inputLocked, false)
        overlayMain.restoreStack([{ name: "home", id: -1 }, { name: "music", id: -1 }, { name: "album", id: 99 }, { name: "queue", id: -1 }])
        compare(JSON.stringify(tc.names()), JSON.stringify(["home", "music"]))
        overlayMain.restoreStack([{ name: "home", id: -1 }, { name: "artist", id: 7 }, { name: "nowplaying", id: -1 }, { name: "queue", id: -1 }])
        compare(JSON.stringify(tc.names()), JSON.stringify(["home", "artist", "nowplaying", "queue"]))
        overlayMain.restoreStack([{ name: "home", id: -1 }, { name: "bogus", id: 1 }, { name: "settings", id: -1 }])
        compare(JSON.stringify(tc.names()), JSON.stringify(["home"]))
        overlayMain.restoreStack([])
        compare(JSON.stringify(tc.names()), JSON.stringify(["home"]))
        overlayMain.restoreStack(undefined)
        compare(JSON.stringify(tc.names()), JSON.stringify(["home"]))
        compare(JSON.stringify(fakeState.saved.map((e) => e.name)), JSON.stringify(["home"]))
    }

    function test_hide_keeps_stack_and_focus() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        overlayMain.navigate("settings")
        overlayMain.pageStack.completeAll()
        const page = overlayMain.pageStack.currentItem
        page.focusKey("scale")
        overlayMain.overlayMapped = false
        compare(overlayMain.pageStack.depth, 2)
        compare(overlayMain.saverTimer.running, false)
        compare(overlayMain.screenOffTimer.running, false)
        overlayMain.overlayMapped = true
        compare(overlayMain.pageStack.currentItem, page)
        compare(page.focusIndex, page.indexOfKey("scale"))
        compare(overlayMain.pageStack.depth, 2)
    }

    function test_b_release_hides_at_root() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        const home = overlayMain.pageStack.currentItem
        let hides = 0
        const onHide = function() { hides++ }
        overlayMain.hideOverlayRequested.connect(onHide)
        overlayMain.router.press("b")
        verify(overlayMain.router.dispatch("b", false))
        compare(hides, 0)
        compare(overlayMain.pageStack.depth, 1)
        overlayMain.router.release("b")
        compare(hides, 1)
        home.showQuickplay()
        home.completeAll()
        overlayMain.router.release("b")
        compare(hides, 1)
        home.showMenu()
        home.completeAll()
        overlayMain.navigate("music")
        overlayMain.pageStack.completeAll()
        overlayMain.router.release("b")
        compare(hides, 1)
        overlayMain.hideOverlayRequested.disconnect(onHide)
    }

    function test_b_pop_to_home_does_not_hide() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        let hides = 0
        const onHide = function() { hides++ }
        overlayMain.hideOverlayRequested.connect(onHide)
        overlayMain.navigate("music")
        overlayMain.pageStack.completeAll()
        compare(overlayMain.pageStack.depth, 2)
        overlayMain.router.press("b")
        verify(overlayMain.router.dispatch("b", false))
        overlayMain.pageStack.completeAll()
        compare(overlayMain.pageStack.depth, 1)
        overlayMain.router.release("b")
        compare(hides, 0)
        const home = overlayMain.pageStack.currentItem
        home.showQuickplay()
        home.completeAll()
        overlayMain.router.press("b")
        verify(overlayMain.router.dispatch("b", false))
        home.completeAll()
        compare(home.quickplayShown, false)
        overlayMain.router.release("b")
        compare(hides, 0)
        overlayMain.router.press("b")
        overlayMain.router.dispatch("b", false)
        overlayMain.router.release("b")
        compare(hides, 1)
        overlayMain.hideOverlayRequested.disconnect(onHide)
    }

    function test_settings_close_player_row() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        overlayMain.navigate("settings")
        overlayMain.pageStack.completeAll()
        const page = overlayMain.pageStack.currentItem
        compare(page.overlay, true)
        compare(page.focusKey("gameMode"), false)
        compare(page.focusKey("keepPhone"), false)
        verify(page.focusKey("toasts"))
        compare(page.focusedRow.title, "toasts in game")
        verify(page.focusKey("pair"))
        compare(page.focusedRow.value, "pair in dap mode")
        verify(page.focusKey("closePlayer"))
        compare(page.focusedRow.title, "close player")
        page.bridge = fakeBridge
        page.focusKey("closePlayer")
        overlayMain.router.dispatch("a", false)
        compare(page.editing, false)
        compare(page.sheet.title, "close player")
        overlayMain.router.dispatch("a", false)
        compare(fakeBridge.closes, 1)
    }

    function test_settings_edit_mode() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        overlayMain.navigate("settings")
        overlayMain.pageStack.completeAll()
        const page = overlayMain.pageStack.currentItem
        page.focusKey("toasts")
        verify(overlayMain.router.dispatch("a", false))
        compare(page.editing, true)
        verify(overlayMain.router.dispatch("right", false))
        compare(fakeSettings.overlayToasts, true)
        verify(overlayMain.router.dispatch("b", false))
        compare(page.editing, false)
        compare(fakeSettings.overlayToasts, false)
        compare(overlayMain.pageStack.depth, 2)
        verify(overlayMain.router.dispatch("a", false))
        verify(overlayMain.router.dispatch("right", false))
        overlayMain.overlayMapped = false
        compare(page.editing, false)
        compare(fakeSettings.overlayToasts, true)
        fakeSettings.overlayToasts = false
    }

    function test_bluetooth_pairing_inert() {
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        overlayMain.openPairing()
        overlayMain.pageStack.completeAll()
        const page = overlayMain.pageStack.currentItem
        compare(page.objectName, "bluetooth")
        compare(page.overlay, true)
        compare(page.pivotIndex, 1)
        compare(page.dapModeText.visible, true)
        compare(page.dapModeText.text, "pair in dap mode")
        compare(page.dapModeText.font.pixelSize, Ui.fontPx(11))
        compare(page.dapModeText.font.capitalization, Font.AllUppercase)
        fuzzyCompare(page.dapModeText.y + page.dapModeText.baselineOffset, Ui.px(Ui.contentTop + 16), 0.5)
        compare(page.discoverableText.visible, false)
        compare(page.pulseHolder.visible, false)
        compare(page.hintText.visible, false)
        verify(overlayMain.router.dispatch("a", false))
        compare(page.pairing, false)
    }

    function test_saver_never_arms() {
        compare(overlayMain.saverArmed, false)
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        compare(overlayMain.playing, true)
        compare(overlayMain.saverSeconds, 1)
        compare(overlayMain.saverArmed, false)
        compare(overlayMain.saverTimer.running, false)
        compare(overlayMain.screenOffArmed, false)
        compare(overlayMain.screenOffTimer.running, false)
        wait(1300)
        compare(overlayMain.saver.shown, false)
    }

    function test_toast_off_maps_nothing() {
        compare(fakeSettings.overlayToasts, false)
        compare(overlayMain.hidden, true)
        fakePlayer.current = { id: 2, title: "Song 2", artist: "Addison Road", album: "Stories", artPath: "" }
        compare(overlayMain.toastShown, false)
        compare(overlayMain.toastWindow.visible, false)
        compare(overlayMain.hiddenToast.shown, false)
    }

    function test_toast_on_maps_window_while_hidden() {
        fakeSettings.overlayToasts = true
        compare(overlayMain.hidden, true)
        fakePlayer.current = { id: 3, title: "Song 3", artist: "Addison Road", album: "Stories", artPath: "" }
        compare(overlayMain.toastShown, true)
        compare(overlayMain.toastWindow.visible, true)
        compare(overlayMain.toastWindow.height, Ui.px(40))
        compare(overlayMain.toastWindow.width, overlayMain.width)
        verify(Qt.colorEqual(overlayMain.toastWindow.color, Ui.black))
        compare(overlayMain.hiddenToast.phase, "in")
        compare(overlayMain.hiddenToast.artist, "Addison Road")
        compare(overlayMain.hiddenToast.title, "Song 3")
        compare(overlayMain.hiddenToast.inDuration, 350)
        compare(overlayMain.hiddenToast.holdDuration, 3500)
        compare(overlayMain.hiddenToast.outDuration, 250)
        compare(overlayMain.hidden, true)
        compare(overlayMain.pageStack.visible, false)
        overlayMain.hiddenToast.hold.interval = 100
        tryCompare(overlayMain.hiddenToast, "phase", "hidden", 3000)
        compare(overlayMain.toastShown, false)
        compare(overlayMain.toastWindow.visible, false)
        overlayMain.hiddenToast.hold.interval = Motion.toastHold
    }

    function test_toast_window_stays_through_the_hold() {
        fakeSettings.overlayToasts = true
        compare(overlayMain.hidden, true)
        fakePlayer.current = { id: 5, title: "Song 5", artist: "Addison Road", album: "Stories", artPath: "" }
        compare(overlayMain.toastShown, true)
        const toast = overlayMain.hiddenToast
        const whole = toast.inDuration + toast.holdDuration - 200
        const start = Date.now()
        while (Date.now() - start < whole) {
            wait(20)
            verify(overlayMain.toastShown && overlayMain.toastWindow.visible,
                   "toast window gone after " + (Date.now() - start) + " ms")
        }
        compare(toast.phase, "hold")
        tryCompare(toast, "phase", "hidden", 3000)
        compare(overlayMain.toastShown, false)
        compare(overlayMain.toastWindow.visible, false)
    }

    function test_toast_shown_uses_layer() {
        fakeSettings.overlayToasts = true
        overlayMain.overlayMapped = true
        overlayMain.overlayFade = 1
        fakePlayer.current = { id: 4, title: "Song 4", artist: "Addison Road", album: "Stories", artPath: "" }
        compare(overlayMain.toastShown, false)
        compare(overlayMain.toastWindow.visible, false)
        compare(overlayMain.overlayLayer.toast.phase, "in")
        compare(overlayMain.overlayLayer.toast.title, "Song 4")
        overlayMain.overlayLayer.toast.hide()
    }
}
