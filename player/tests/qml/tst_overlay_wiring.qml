import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "OverlayWiring"
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

    OverlayLayer {
        id: layer

        width: 640
        height: 960
        power: fakePower
        bluez: fakeBluez
        player: fakePlayer
        screenOff: fakePower.screenOff
        topScreen: pages.currentItem !== null ? pages.currentItem.objectName : ""
    }

    QtObject {
        id: fakePower

        property bool screenOff: false

        signal volumeChanged(int volume)
    }

    QtObject {
        id: fakeBluez

        property bool connected: false
        property string deviceName: ""
        property bool transportActive: false
        property string title: ""
        property string artist: ""
        property string status: "stopped"

        signal volumeChanged(int volume)
        signal phoneVolumeChanged(int volume)

        function reset() {
            fakeBluez.transportActive = false
            fakeBluez.connected = false
            fakeBluez.deviceName = ""
            fakeBluez.title = ""
            fakeBluez.artist = ""
            fakeBluez.status = "stopped"
        }
    }

    QtObject {
        id: fakePlayer

        property int status: 0
        property var current: ({})
    }

    Component {
        id: pageComponent

        Item {
            id: page

            property var actions: []

            function handleAction(name, repeat) {
                page.actions = page.actions.concat([name])
                return true
            }
        }
    }

    function init() {
        Ui.userScale = 1.0
        Ui.setSurface(640, 960)
        pages.clear()
        pages.animate = false
        fakePower.screenOff = false
        fakeBluez.reset()
        fakePlayer.status = 0
        fakePlayer.current = ({})
        layer.lastTrackId = null
        layer.volume.dismiss()
        layer.toast.hide()
    }

    function open(name) {
        return pages.push(pageComponent, { objectName: name })
    }

    function test_volume_shows_and_hides() {
        open("home")
        compare(layer.volume.shown, false)
        compare(layer.volume.visible, false)
        fakePower.volumeChanged(35)
        compare(layer.volume.level, 35)
        compare(layer.volume.number.text, "35")
        compare(layer.volume.shown, true)
        compare(layer.volume.showAnimation.running, true)
        compare(layer.volume.hide.running, true)
        compare(layer.volume.hide.interval, Motion.volumeHide)
        compare(layer.volume.popAnimation.running, true)
        tryCompare(layer.volume, "opacity", 1, 2000)
        compare(layer.volume.visible, true)
        tryCompare(layer.volume, "visible", false, Motion.volumeHide + Motion.volumeFade + 2000)
        compare(layer.volume.shown, false)
        compare(layer.volume.level, 35)
    }

    function test_volume_from_bluez() {
        open("home")
        fakeBluez.volumeChanged(40)
        compare(layer.volume.shown, false)
        fakeBluez.phoneVolumeChanged(50)
        compare(layer.volume.level, 50)
        compare(layer.volume.shown, true)
        compare(layer.volume.hide.running, true)
        fakePower.volumeChanged(55)
        compare(layer.volume.level, 55)
        compare(layer.volume.showAnimation.running, true)
        compare(layer.volume.popAnimation.running, true)
    }

    function test_volume_ring_and_text_follow_source() {
        open("home")
        fakePlayer.current = ({ id: 3, title: "Local Song", artist: "Local Artist" })
        fakePlayer.status = 1
        compare(layer.volume.playing, true)
        compare(layer.volume.artist, "Local Artist")
        compare(layer.volume.title, "Local Song")
        fakePlayer.status = 2
        compare(layer.volume.playing, false)
        fakeBluez.deviceName = "Pixel 8"
        fakeBluez.title = "Track Four"
        fakeBluez.status = "playing"
        fakeBluez.transportActive = true
        compare(layer.volume.playing, true)
        compare(layer.volume.artist, "from Pixel 8")
        compare(layer.volume.title, "Track Four")
    }

    function test_track_toast() {
        open("home")
        compare(layer.toast.phase, "hidden")
        fakePlayer.current = ({ id: 7, title: "Song title", artist: "artist" })
        compare(layer.toast.phase, "in")
        compare(layer.toast.artist, "artist")
        compare(layer.toast.title, "Song title")
        compare(layer.lastTrackId, 7)
        compare(layer.toast.inAnimation.duration, Motion.toastIn)
        tryCompare(layer.toast, "phase", "hold", 2000)
        compare(layer.toast.hold.interval, Motion.toastHold)
        layer.toast.hide()
        fakePlayer.current = ({ id: 7, title: "Song title", artist: "artist" })
        compare(layer.toast.phase, "hidden")
        fakePlayer.current = ({})
        compare(layer.lastTrackId, null)
        compare(layer.toast.phase, "hidden")
        fakePlayer.current = ({ id: 7, title: "Song title", artist: "artist" })
        compare(layer.toast.phase, "in")
    }

    function test_track_toast_skips_now_playing() {
        open("nowplaying")
        fakePlayer.current = ({ id: 8, title: "Song", artist: "artist" })
        compare(layer.lastTrackId, 8)
        compare(layer.toast.phase, "hidden")
    }

    function test_phone_toast() {
        open("home")
        fakeBluez.deviceName = "Pixel 8"
        fakeBluez.title = "Track Four"
        fakeBluez.transportActive = true
        compare(layer.toast.phase, "in")
        compare(layer.toast.artist, "from Pixel 8")
        compare(layer.toast.title, "Track Four")
        compare(layer.toast.artistText.font.capitalization, Font.AllUppercase)
        layer.toast.complete()
        compare(layer.toast.phase, "hold")
        fakeBluez.title = "Track Five"
        tryCompare(layer.toast, "title", "Track Five", 2000)
        compare(layer.toast.hold.running, true)
        fakeBluez.transportActive = false
        layer.toast.hide()
        fakeBluez.title = "Track Six"
        compare(layer.toast.phase, "hidden")
    }

    function test_phone_toast_skips_bluetooth_screen() {
        open("home")
        open("bluetooth")
        compare(layer.topScreen, "bluetooth")
        fakeBluez.deviceName = "Pixel 8"
        fakeBluez.title = "Track Four"
        fakeBluez.transportActive = true
        compare(layer.toast.phase, "hidden")
        fakeBluez.title = "Track Five"
        compare(layer.toast.phase, "hidden")
        pages.pop()
        compare(layer.topScreen, "home")
        fakeBluez.title = "Track Six"
        compare(layer.toast.phase, "in")
        compare(layer.toast.title, "Track Six")
    }

    function test_screen_off_hides_and_blocks() {
        open("home")
        fakePower.volumeChanged(35)
        fakePlayer.current = ({ id: 1, title: "Song", artist: "artist" })
        compare(layer.volume.shown, true)
        compare(layer.toast.phase, "in")
        fakePower.screenOff = true
        compare(layer.screenOff, true)
        compare(layer.volume.shown, false)
        compare(layer.volume.visible, false)
        compare(layer.volume.animating, false)
        compare(layer.volume.hide.running, false)
        compare(layer.toast.phase, "hidden")
        compare(layer.toast.visible, false)
        fakePower.volumeChanged(20)
        compare(layer.volume.level, 20)
        compare(layer.volume.shown, false)
        compare(layer.volume.visible, false)
        fakeBluez.phoneVolumeChanged(60)
        compare(layer.volume.level, 60)
        compare(layer.volume.shown, false)
        fakePlayer.current = ({ id: 2, title: "Later", artist: "other" })
        compare(layer.toast.phase, "hidden")
        compare(layer.toast.title, "Later")
        fakePower.screenOff = false
        compare(layer.volume.shown, false)
        compare(layer.toast.phase, "hidden")
        fakePower.volumeChanged(25)
        compare(layer.volume.shown, true)
        compare(layer.volume.level, 25)
    }

    function test_input_reaches_page_below() {
        const page = open("home")
        fakePower.volumeChanged(35)
        compare(layer.volume.shown, true)
        fakePlayer.current = ({ id: 4, title: "Song", artist: "artist" })
        compare(layer.toast.phase, "in")
        verify(router.dispatch("a", false))
        verify(router.dispatch("down", false))
        compare(page.actions, ["a", "down"])
        compare(layer.volume.shown, true)
        compare(layer.toast.phase, "in")
        compare(layer.activeFocus, false)
        compare(layer.volume.activeFocus, false)
        compare(layer.toast.activeFocus, false)
    }

    function test_null_sources() {
        layer.power = null
        layer.bluez = null
        layer.player = null
        compare(layer.volume.artist, "")
        compare(layer.volume.title, "")
        compare(layer.volume.playing, false)
        compare(layer.phoneActive, false)
        fakePower.volumeChanged(35)
        compare(layer.volume.shown, false)
        layer.power = fakePower
        layer.bluez = fakeBluez
        layer.player = fakePlayer
        fakePower.volumeChanged(35)
        compare(layer.volume.shown, true)
    }
}
