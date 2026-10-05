import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "OverlayMode"
    when: windowShown
    visible: true
    width: 320
    height: 240

    Main {
        id: overlayMain

        overlay: true
        overlayMapped: false
        overlayFade: 0
        shotWidth: 960
        shotHeight: 640
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
        compare(overlayMain.background.parent, overlayMain.contentItem)
        compare(overlayMain.background.width, overlayMain.contentItem.width)
        compare(overlayMain.background.height, overlayMain.contentItem.height)
        compare(overlayMain.contentItem.opacity, 0)
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
}
