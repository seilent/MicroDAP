import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "Home"
    when: windowShown
    visible: true
    width: 960
    height: 960

    readonly property real canvasW: 320
    readonly property real canvasH: 480

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
        pad: fakePad
    }

    TouchLayer {
        id: layer

        width: 960
        height: 960
        router: router
    }

    QtObject {
        id: fakePad

        readonly property var names: ["up", "down", "left", "right", "a", "b", "x", "y", "lb", "rb", "lt", "rt", "start", "back"]

        signal action(int code, bool repeat)
        signal pressed(int code)
        signal released(int code)

        function actionName(code) {
            return fakePad.names[code]
        }
    }

    ListModel {
        id: history
    }

    ListModel {
        id: fresh
    }

    ListModel {
        id: pins
    }

    QtObject {
        id: phone

        property bool connected: true
        property string deviceName: "Pixel 8"
        property bool pairing: false
        property int pairingRemaining: 0
    }

    QtObject {
        id: counters

        property var navigated: []
        property int bluetoothOpens: 0
        property var played: []
        property var pinned: []
        property int shuffles: 0
    }

    Component {
        id: homeComponent

        Home {
            id: home

            historyModel: history
            newModel: fresh
            pinsModel: pins
            onNavigate: (name) => counters.navigated = counters.navigated.concat([name])
            onNowPlayingBluetooth: counters.bluetoothOpens++
            onPlayRequested: (entry) => counters.played = counters.played.concat([entry])
            onPinRequested: (entry, pin) => counters.pinned = counters.pinned.concat([{ entry: entry, pin: pin }])
            onShuffleAll: counters.shuffles++
        }
    }

    Component {
        id: blankComponent

        Page {}
    }

    Component {
        id: shotComponent

        Item {
            property alias home: shotHome

            Home {
                id: shotHome

                anchors.fill: parent
                animated: false
                historyModel: history
                newModel: fresh
                pinsModel: pins

                Component.onCompleted: {
                    shotHome.showQuickplay()
                    shotHome.completeAll()
                }
            }
        }
    }

    function checkSectionTops(qp) {
        const items = [qp.pinsSection, qp.historySection, qp.newSection]
        for (let s = 0; s < 3; s++) {
            const item = items[s]
            tryVerify(function() { return Math.abs(qp.sections.y + item.y - qp.sectionTop(s)) < 0.5 }, 2000)
            fuzzyCompare(qp.sections.y + item.y, qp.sectionTop(s), 0.5)
        }
    }

    function fill(model, prefix, n) {
        model.clear()
        for (let i = 0; i < n; i++)
            model.append({ id: i + 1, title: prefix + " " + (i + 1), subtitle: "Artist " + (i + 1), artPath: "" })
    }

    function init() {
        Ui.userScale = 1.0
        Ui.setSurface(640, 960)
        pages.clear()
        pages.animate = false
        fill(history, "History", 6)
        fill(fresh, "New", 2)
        pins.clear()
        phone.connected = true
        phone.pairing = false
        phone.pairingRemaining = 0
        counters.navigated = []
        counters.bluetoothOpens = 0
        counters.played = []
        counters.pinned = []
        counters.shuffles = 0
    }

    function open(properties) {
        const props = properties === undefined ? { animated: false } : properties
        const page = pages.push(homeComponent, props)
        if (props.animated === false)
            tc.waitForRendering(page)
        return page
    }

    function test_canvas() {
        compare(Ui.u, 2)
        compare(Ui.canvasW, tc.canvasW)
        compare(Ui.canvasH, tc.canvasH)
    }

    function test_item_baselines() {
        const page = open()
        compare(page.menuRepeater.count, 5)
        const names = ["music", "bluetooth", "playlists", "history", "settings"]
        for (let i = 0; i < 5; i++) {
            const item = page.menuItem(i)
            verify(item !== null)
            compare(item.modelData.name, names[i])
            fuzzyCompare(item.labelBaseline, Ui.px(48 + 55 * i), 0.5)
            fuzzyCompare(item.x, Ui.px(70), 0.01)
            compare(item.opacity, 1)
        }
        compare(page.menuItem(0).subtitle, "artists albums songs genres playlists")
        compare(page.menuItem(1).subtitle, "not connected")
        compare(page.menuItem(2).subtitle, "now playing saved")
        compare(page.menuItem(3).subtitle, "recent new")
        compare(page.menuItem(4).subtitle, "output library display power")
        for (let i = 0; i < 5; i++) {
            verify(page.menuItem(i).subtitle !== "")
            compare(page.menuItem(i).subtitleItem.visible, true)
            compare(page.menuItem(i).subtitleItem.font.capitalization, Font.AllUppercase)
        }
        const first = page.menuItem(0)
        fuzzyCompare(first.subtitleBaseline, first.labelBaseline + Ui.px(8 + 0.7 * 8), 0.5)
    }

    function test_bluetooth_subtitle_states() {
        const page = open({ animated: false, bluez: phone })
        compare(page.menuItem(1).subtitle, "Pixel 8")
        phone.connected = false
        compare(page.menuItem(1).subtitle, "not connected")
        phone.pairing = true
        phone.pairingRemaining = 118
        compare(page.menuItem(1).subtitle, "pairing 118")
        phone.pairingRemaining = 7
        compare(page.menuItem(1).subtitle, "pairing 7")
        phone.connected = true
        compare(page.menuItem(1).subtitle, "Pixel 8")
        phone.pairing = false
        phone.pairingRemaining = 0
        compare(page.menuItem(1).subtitle, "Pixel 8")
        compare(page.menuItem(1).subtitleItem.visible, true)
    }

    function test_playlists_subtitle() {
        const page = open({ animated: false, playlistCount: 3 })
        compare(page.menuItem(2).subtitle, "3 playlists")
        page.playlistCount = 1
        compare(page.menuItem(2).subtitle, "1 playlist")
        page.playlistCount = 0
        compare(page.menuItem(2).subtitle, "0 playlists")
        page.playlistCount = -1
        compare(page.menuItem(2).subtitle, "now playing saved")
    }

    function labelAlpha(page, index) {
        return page.menuItem(index).labelItem.color.a
    }

    function checkFocusAlphas(page) {
        for (let i = 0; i < 5; i++)
            fuzzyCompare(tc.labelAlpha(page, i), i === page.focusIndex ? 1.0 : 0.6, 1 / 255)
    }

    function test_focus_treatment() {
        const page = open()
        compare(page.focusIndex, 0)
        compare(page.focusBar.width, Ui.px(2))
        compare(page.focusBar.x, Ui.px(70 - 8))
        verify(page.focusBar.x + page.focusBar.width <= Ui.px(70))
        fuzzyCompare(page.focusBar.y, Ui.px(48) - Ui.px(0.7 * 34), 0.01)
        fuzzyCompare(page.focusBar.height, Ui.px(0.7 * 34 + 8 + 0.7 * 8), 0.01)
        compare(Qt.colorEqual(page.focusBar.color, Ui.white(1.0)), true)
        tc.checkFocusAlphas(page)
        verify(router.dispatch("down", false))
        compare(page.focusIndex, 1)
        fuzzyCompare(page.focusBar.y, Ui.px(48 + 55) - Ui.px(0.7 * 34), 0.01)
        tc.checkFocusAlphas(page)
        for (let i = 0; i < 10; i++)
            router.dispatch("down", false)
        compare(page.focusIndex, 4)
        fuzzyCompare(page.focusBar.y, Ui.px(48 + 55 * 4) - Ui.px(0.7 * 34), 0.01)
        tc.checkFocusAlphas(page)
        router.dispatch("up", false)
        compare(page.focusIndex, 3)
        fuzzyCompare(page.focusBar.y, Ui.px(48 + 55 * 3) - Ui.px(0.7 * 34), 0.01)
        tc.checkFocusAlphas(page)
    }

    function test_no_ring() {
        const page = open()
        compare(page.ring, undefined)
        compare(page.focusBar.x, Ui.px(62))
        verify(router.dispatch("y", false))
        compare(counters.shuffles, 1)
    }

    function test_bar_follows_animated() {
        const page = open({ animated: true })
        page.completeAll()
        const start = page.focusBar.y
        router.dispatch("down", false)
        verify(page.focusBar.y < start + Ui.px(55))
        tryCompare(page.focusBar, "y", start + Ui.px(55), 1000)
    }

    function test_entrance_plays_at_creation() {
        const page = open({ animated: true })
        compare(page.peekEnter, 0)
        compare(page.barEnter, 0)
        for (let i = 0; i < page.menuRepeater.count; i++)
            compare(page.menuRepeater.itemAt(i).enter, 0)
        tryCompare(page, "peekEnter", 1, 3000)
        for (let i = 0; i < page.menuRepeater.count; i++)
            tryCompare(page.menuRepeater.itemAt(i), "enter", 1, 3000)
        compare(page.barEnter, 1)
        const rest = open()
        compare(rest.peekEnter, 1)
        compare(rest.barEnter, 1)
        for (let i = 0; i < rest.menuRepeater.count; i++) {
            compare(rest.menuRepeater.itemAt(i).enter, 1)
            compare(rest.menuRepeater.itemAt(i).opacity, 1)
        }
    }

    function test_quickplay_peek() {
        const page = open()
        compare(page.quickplayPanel.width, Ui.px(216))
        compare(page.quickplayPanel.x, 0)
        fuzzyCompare(page.quickplayPanel.y, Ui.px(130), 0.01)
        fuzzyCompare(page.quickplayPanel.scale, 0.30, 1e-6)
        compare(page.quickplayPanel.transformOrigin, Item.TopLeft)
        fuzzyCompare(page.quickplayPanel.width * page.quickplayPanel.scale, Ui.px(Ui.qpPeekW), 0.01)
        fuzzyCompare(Ui.qpPeekW, 64.8, 0.01)
        fuzzyCompare(page.quickplayPanel.opacity, 0.4, 1e-6)
        fuzzyCompare(page.quickplayPanel.y + page.quickplay.firstHeaderBaseline * page.quickplayPanel.scale, Ui.px(130 + 35 * 0.30), 0.01)
        compare(page.menuPanel.x, 0)
        compare(page.menuPanel.scale, 1)
        compare(page.menuPanel.opacity, 1)
        compare(page.menuPanel.angle, 0)
        compare(page.quickplay.active, false)
    }

    function test_t6_end_state() {
        const page = open()
        verify(router.dispatch("left", false))
        compare(page.quickplayShown, true)
        compare(page.pan, 1)
        fuzzyCompare(page.menuPanel.x, Ui.px(tc.canvasW - 84), 0.01)
        fuzzyCompare(page.menuPanel.y, Ui.px(94), 0.01)
        fuzzyCompare(page.menuPanel.scale, 0.44, 1e-6)
        fuzzyCompare(page.menuPanel.opacity, 0.6, 1e-6)
        fuzzyCompare(page.focusBar.opacity, 1, 1e-6)
        fuzzyCompare(page.menuPanel.angle, -12, 1e-6)
        compare(page.menuPanel.transformOrigin, Item.TopLeft)
        fuzzyCompare(page.menuPanel.hinge, page.menuPanel.width * 0.44, 0.01)
        compare(page.quickplayPanel.x, 0)
        compare(page.quickplayPanel.y, 0)
        fuzzyCompare(page.quickplayPanel.scale, 1, 1e-6)
        fuzzyCompare(page.quickplayPanel.opacity, 1, 1e-6)
        compare(page.quickplay.active, true)
        verify(router.dispatch("b", false))
        compare(page.quickplayShown, false)
        compare(page.pan, 0)
        compare(page.menuPanel.x, 0)
        compare(page.menuPanel.y, 0)
        compare(page.menuPanel.scale, 1)
        compare(page.menuPanel.hinge, page.menuPanel.width)
    }

    function test_t6_animated() {
        const page = open({ animated: true })
        page.completeAll()
        router.dispatch("back", false)
        compare(page.quickplayShown, true)
        verify(page.panning)
        verify(page.pan < 1)
        tryCompare(page, "pan", 1, 1500)
        fuzzyCompare(page.menuPanel.x, Ui.px(tc.canvasW - 84), 0.01)
        fuzzyCompare(page.menuPanel.y, Ui.px(94), 0.01)
        router.dispatch("rb", false)
        compare(page.quickplayShown, false)
        tryCompare(page, "pan", 0, 1500)
        router.dispatch("lb", false)
        compare(page.quickplayShown, true)
        page.completeAll()
        compare(page.pan, 1)
    }

    function test_column_rules() {
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        compare(qp.count, 8)
        compare(qp.focusIndex, 0)
        verify(router.dispatch("left", false))
        compare(qp.focusIndex, 0)
        compare(page.quickplayShown, true)
        verify(router.dispatch("right", false))
        compare(qp.focusIndex, 1)
        compare(page.quickplayShown, true)
        verify(router.dispatch("right", false))
        compare(page.quickplayShown, false)
        router.dispatch("left", false)
        compare(qp.focusIndex, 1)
        verify(router.dispatch("down", false))
        compare(qp.focusIndex, 3)
        verify(router.dispatch("down", false))
        compare(qp.focusIndex, 5)
        verify(router.dispatch("down", false))
        compare(qp.focusIndex, 7)
        verify(router.dispatch("down", false))
        compare(qp.focusIndex, 7)
        verify(router.dispatch("left", false))
        compare(qp.focusIndex, 6)
        verify(router.dispatch("up", false))
        compare(qp.focusIndex, 4)
        verify(router.dispatch("up", false))
        compare(qp.focusIndex, 2)
        verify(router.dispatch("up", false))
        compare(qp.focusIndex, 0)
        verify(router.dispatch("up", false))
        compare(qp.focusIndex, 0)
    }

    function test_tile_geometry() {
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        const first = qp.tileAt(0)
        const second = qp.tileAt(1)
        const third = qp.tileAt(2)
        verify(first !== null)
        compare(first.width, Ui.px(77))
        compare(first.height, Ui.px(77))
        compare(second.x - first.x, Ui.px(77 + 2))
        compare(third.y - first.y, Ui.px(77 + 2))
        fuzzyCompare(first.mapToItem(qp, 0, 0).x, Ui.px(60), 0.01)
        fuzzyCompare(qp.pinsHeader.mapToItem(qp, 0, 0).x, Ui.px(60), 0.01)
        compare(first.focused, true)
        compare(second.focused, false)
        compare(first.captioned, false)
        compare(first.entry.section, "history")
        compare(first.entry.id, 1)
        compare(first.entry.title, "History 1")
        compare(qp.tileAt(6).entry.section, "new")
        compare(qp.pinsHeader.text, "Pins")
        compare(qp.historyHeader.text, "History")
        compare(qp.emptyText.visible, true)
        fuzzyCompare(qp.firstHeaderBaseline, Ui.px(35), 0.5)
        compare(qp.pinsHeader.font.pixelSize, Ui.fontPx(34))
        compare(qp.pinsHeader.font.pixelSize, 68)
        fuzzyCompare(qp.pinsHeader.color.a, 0.6, 1 / 255)
        compare(qp.emptyText.font.pixelSize, Ui.fontPx(11))
        compare(qp.emptyText.font.capitalization, Font.AllUppercase)
        compare(qp.emptyText.width, Ui.px(140))
        compare(qp.emptyText.lineHeight, Ui.px(11))
        fuzzyCompare(qp.hintBaseline, Ui.px(35 + 16), 0.5)
        verify(qp.emptyText.lineCount >= 2)
        const lastHint = 35 + 16 + 11 * (qp.emptyText.lineCount - 1)
        fuzzyCompare(qp.secondHeaderBaseline, Ui.px(lastHint + 70), 0.5)
        fuzzyCompare(first.mapToItem(qp, 0, 0).y, Ui.px(lastHint + 70 + 5), 0.5)
    }

    function test_scroll_keeps_focus_visible() {
        fill(history, "History", 20)
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        compare(qp.flick.contentY, 0)
        for (let i = 0; i < 10; i++)
            router.dispatch("down", false)
        tryVerify(function() { return !qp.scrolling }, 2000)
        const item = qp.tileAt(qp.focusIndex)
        const top = item.mapToItem(qp.flick.contentItem, 0, 0).y - qp.flick.contentY
        verify(top >= -0.5)
        verify(top + item.height <= qp.height + 0.5)
        verify(qp.flick.contentY > 0)
    }

    function test_scroll_back_reveals_headers() {
        fill(history, "History", 20)
        fill(fresh, "New", 6)
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        for (let i = 0; i < 12; i++)
            router.dispatch("down", false)
        tryVerify(function() { return !qp.scrolling }, 2000)
        verify(qp.flick.contentY > 0)
        const newTop = qp.newHeader.mapToItem(qp.flick.contentItem, 0, 0).y
        verify(qp.flick.contentY <= newTop + 0.5)
        for (let i = 0; i < 12; i++)
            router.dispatch("up", false)
        tryVerify(function() { return !qp.scrolling }, 2000)
        compare(qp.focusIndex, 0)
        compare(qp.flick.contentY, 0)
    }

    function test_scroll_up_reveals_pins_header() {
        fill(pins, "Pin", 4)
        fill(history, "History", 20)
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        for (let i = 0; i < 12; i++)
            router.dispatch("down", false)
        tryVerify(function() { return !qp.scrolling }, 2000)
        verify(qp.flick.contentY > 0)
        for (let i = 0; i < 12; i++)
            router.dispatch("up", false)
        tryVerify(function() { return !qp.scrolling }, 2000)
        compare(qp.focusIndex, 0)
        compare(qp.flick.contentY, 0)
        fuzzyCompare(qp.pinsHeader.mapToItem(qp.flick, 0, qp.pinsHeader.baselineOffset).y, Ui.px(Ui.qpFirstBaseline), 0.5)
    }

    function test_quickplay_shown_before_layout() {
        fill(pins, "Pin", 2)
        const wrap = createTemporaryObject(shotComponent, tc)
        verify(wrap !== null)
        wrap.width = 640
        wrap.height = 960
        const qp = wrap.home.quickplay
        tc.waitForRendering(wrap)
        compare(qp.active, true)
        compare(qp.focusIndex, 0)
        compare(qp.tileAt(0).entry.section, "pins")
        tryCompare(qp.flick, "contentY", 0)
        fuzzyCompare(qp.pinsHeader.mapToItem(qp.flick, 0, qp.pinsHeader.baselineOffset).y, Ui.px(Ui.qpFirstBaseline), 0.5)
        checkSectionTops(qp)
        qp.focusIndex = 2
        compare(qp.tileAt(2).entry.section, "history")
        tryVerify(function() { return !qp.scrolling }, 2000)
        const historyTop = qp.historyHeader.mapToItem(qp.flick.contentItem, 0, 0).y
        verify(historyTop >= qp.flick.contentY - 0.5)
    }

    function test_quickplay_section_tops() {
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        compare(qp.pinsCount, 0)
        checkSectionTops(qp)
        fill(pins, "Pin", 2)
        compare(qp.pinsCount, 2)
        checkSectionTops(qp)
        compare(qp.focusIndex, 0)
        tryCompare(qp.flick, "contentY", 0)
        verify(router.dispatch("down", false))
        compare(qp.tileAt(qp.focusIndex).entry.section, "history")
        tryVerify(function() { return !qp.scrolling }, 2000)
        verify(qp.historyHeader.mapToItem(qp.flick.contentItem, 0, 0).y >= qp.flick.contentY - 0.5)
        history.clear()
        checkSectionTops(qp)
        pins.clear()
        checkSectionTops(qp)
    }

    function test_quickplay_section_tops_phone() {
        fill(pins, "Pin", 2)
        const page = open({ animated: false, bluez: phone })
        router.dispatch("left", false)
        const qp = page.quickplay
        compare(qp.pinsCount, 3)
        checkSectionTops(qp)
        tryCompare(qp.flick, "contentY", 0)
    }

    function test_a_navigates() {
        const page = open()
        const names = ["music", "bluetooth", "playlists", "history", "settings"]
        for (let i = 0; i < names.length; i++) {
            if (i > 0)
                verify(router.dispatch("down", false))
            compare(page.focusIndex, i)
            verify(router.dispatch("a", false))
        }
        compare(counters.navigated, names)
        router.dispatch("a", true)
        compare(counters.navigated.length, names.length)
        verify(router.dispatch("y", false))
        compare(counters.shuffles, 1)
        router.dispatch("y", true)
        compare(counters.shuffles, 1)
    }

    function test_pad_signal_a_navigates() {
        const page = open()
        compare(router.pad, fakePad)
        fakePad.pressed(4)
        compare(page.pressed, true)
        fakePad.action(4, false)
        fakePad.released(4)
        compare(page.pressed, false)
        compare(counters.navigated, ["music"])
        fakePad.action(1, false)
        compare(page.focusIndex, 1)
        fakePad.action(4, false)
        compare(counters.navigated, ["music", "bluetooth"])
        fakePad.action(7, false)
        compare(counters.shuffles, 1)
    }

    function test_a_on_tile_plays() {
        const page = open()
        router.dispatch("left", false)
        router.dispatch("right", false)
        router.dispatch("a", false)
        compare(counters.played.length, 1)
        compare(counters.played[0].section, "history")
        compare(counters.played[0].id, 2)
    }

    function test_x_opens_pin_sheet() {
        const page = open()
        router.dispatch("left", false)
        compare(page.sheet.shown, false)
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.items, ["pin to quickplay"])
        compare(page.sheet.title, "History 1")
        verify(router.dispatch("down", false))
        compare(page.quickplay.focusIndex, 0)
        verify(router.dispatch("a", false))
        compare(counters.pinned.length, 1)
        compare(counters.pinned[0].pin, true)
        compare(counters.pinned[0].entry.id, 1)
        compare(page.sheet.shown, false)
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(page.quickplayShown, true)
        compare(counters.pinned.length, 1)
    }

    function test_unpin_sheet_for_pins() {
        fill(pins, "Pin", 1)
        const page = open()
        router.dispatch("left", false)
        compare(page.quickplay.count, 9)
        compare(page.quickplay.tileAt(0).entry.section, "pins")
        compare(page.quickplay.emptyText.visible, false)
        router.dispatch("x", false)
        compare(page.sheet.items, ["unpin"])
        router.dispatch("a", false)
        compare(counters.pinned[0].pin, false)
    }

    function test_entry_kinds() {
        pins.append({ id: 11, kind: "album", title: "Pinned Album", subtitle: "Artist", artPath: "" })
        pins.append({ id: 12, kind: "artist", title: "Pinned Artist", subtitle: "2 albums", artPath: "" })
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        compare(qp.count, 10)
        compare(qp.tileAt(0).entry.section, "pins")
        compare(qp.tileAt(0).entry.kind, "album")
        compare(qp.tileAt(0).entry.id, 11)
        compare(qp.tileAt(0).entry.title, "Pinned Album")
        compare(qp.tileAt(1).entry.kind, "artist")
        compare(qp.tileAt(1).entry.id, 12)
        compare(qp.tileAt(2).entry.section, "history")
        compare(qp.tileAt(2).entry.kind, "song")
        compare(qp.tileAt(8).entry.section, "new")
        compare(qp.tileAt(8).entry.kind, "album")
        verify(router.dispatch("x", false))
        compare(page.sheet.items, ["unpin"])
        verify(router.dispatch("a", false))
        compare(counters.pinned.length, 1)
        compare(counters.pinned[0].pin, false)
        compare(counters.pinned[0].entry.kind, "album")
        compare(counters.pinned[0].entry.id, 11)
        verify(router.dispatch("down", false))
        compare(qp.tileAt(qp.focusIndex).entry.section, "history")
        verify(router.dispatch("x", false))
        compare(page.sheet.items, ["pin to quickplay"])
        verify(router.dispatch("a", false))
        compare(counters.pinned.length, 2)
        compare(counters.pinned[1].pin, true)
        compare(counters.pinned[1].entry.kind, "song")
        compare(counters.pinned[1].entry.id, 1)
        verify(router.dispatch("a", false))
        compare(counters.played.length, 1)
        compare(counters.played[0].kind, "song")
        compare(counters.played[0].id, 1)
    }

    function test_pin_appears_in_pins() {
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        compare(qp.pinsCount, 0)
        compare(qp.emptyText.visible, true)
        pins.append({ id: 1, kind: "song", title: "History 1", subtitle: "Artist 1", artPath: "" })
        compare(qp.pinsCount, 1)
        compare(qp.count, 9)
        compare(qp.emptyText.visible, false)
        compare(qp.tileAt(0).entry.section, "pins")
        compare(qp.tileAt(0).entry.kind, "song")
        compare(qp.tileAt(0).entry.title, "History 1")
        pins.clear()
        compare(qp.pinsCount, 0)
        compare(qp.emptyText.visible, true)
    }

    function test_press_follows_late_pins() {
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        page.pressed = true
        compare(qp.phoneItem.focused, false)
        compare(qp.tileAt(0).entry.section, "history")
        compare(qp.tileAt(0).scale, 0.96)
        pins.append({ id: 1, kind: "album", title: "Pin 1", subtitle: "Artist 1", artPath: "" })
        pins.append({ id: 2, kind: "artist", title: "Pin 2", subtitle: "", artPath: "" })
        compare(qp.phoneItem.focused, false)
        compare(qp.tileAt(0).entry.section, "pins")
        compare(qp.tileAt(0).scale, 0.96)
        compare(qp.tileAt(1).scale, 1)
        compare(qp.tileAt(2).entry.section, "history")
        compare(qp.tileAt(2).focused, false)
        compare(qp.tileAt(2).scale, 1)
        page.pressed = false
        compare(qp.tileAt(0).scale, 1)
    }

    function test_phone_tile_kind() {
        const page = open({ animated: false, bluez: phone })
        router.dispatch("left", false)
        compare(page.quickplay.tileAt(0).entry.kind, "phone")
        compare(page.quickplay.tileAt(0).entry.phone, true)
    }

    function test_watermark() {
        const page = open()
        compare(page.watermark.text, "microdap")
        compare(page.watermark.x, Ui.px(70))
        verify(page.watermark.parent === page.menuPanel)
        fuzzyCompare(page.watermark.y + page.watermark.baselineOffset, page.height - Ui.px(16), 0.5)
        compare(page.watermark.font.pixelSize, Ui.fontPx(14))
        compare(page.watermark.font.pixelSize, 28)
        compare(page.watermark.font.styleName, "Light")
        fuzzyCompare(page.watermark.color.a, 0.08, 1 / 255)
        fuzzyCompare(page.watermark.opacity, 1, 1e-6)
        router.dispatch("left", false)
        fuzzyCompare(page.watermark.opacity, 0, 1e-6)
    }

    function test_phone_connected() {
        const page = open({ animated: false, bluez: phone })
        compare(page.bluetoothSubtitle, "Pixel 8")
        compare(page.menuItem(1).subtitle, "Pixel 8")
        const qp = page.quickplay
        compare(qp.phoneTile, true)
        compare(qp.count, 9)
        compare(qp.emptyText.visible, false)
        router.dispatch("left", false)
        const tile = qp.tileAt(0)
        verify(tile === qp.phoneItem)
        compare(tile.entry.phone, true)
        compare(tile.entry.title, "Pixel 8")
        compare(tile.width, Ui.px(77))
        compare(tile.focused, true)
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, false)
        verify(router.dispatch("a", false))
        compare(counters.bluetoothOpens, 1)
        compare(counters.played.length, 0)
        verify(router.dispatch("down", false))
        compare(qp.focusIndex, 1)
        compare(qp.tileAt(1).entry.section, "history")
        phone.connected = false
        compare(qp.phoneTile, false)
        compare(qp.count, 8)
        compare(page.bluetoothSubtitle, "not connected")
        phone.connected = true
    }

    function test_entrance() {
        const page = open({ animated: true })
        const item = page.menuItem(0)
        verify(item !== null)
        verify(item.opacity < 1)
        verify(item.x > Ui.px(70))
        verify(page.quickplayPanel.opacity < 0.4)
        tryCompare(item, "opacity", 1, 1500)
        fuzzyCompare(item.x, Ui.px(70), 0.01)
        tryCompare(page.menuItem(4), "opacity", 1, 1500)
        tryCompare(page, "peekEnter", 1, 1500)
        fuzzyCompare(page.quickplayPanel.opacity, 0.4, 1e-3)
        fuzzyCompare(page.focusBar.opacity, 1, 1e-3)
    }

    function test_complete_all() {
        const page = open({ animated: true })
        router.dispatch("left", false)
        page.completeAll()
        compare(page.pan, 1)
        compare(page.peekEnter, 1)
        for (let i = 0; i < 5; i++) {
            compare(page.menuItem(i).opacity, 1)
            fuzzyCompare(page.menuItem(i).x, Ui.px(70), 0.01)
        }
        fuzzyCompare(page.menuPanel.x, Ui.px(tc.canvasW - 84), 0.01)
        fuzzyCompare(page.menuPanel.y, Ui.px(94), 0.01)
    }

    function test_start_and_b_on_menu() {
        const page = open()
        let nowPlaying = 0
        const onNowPlaying = function() { nowPlaying++ }
        router.nowPlaying.connect(onNowPlaying)
        verify(router.dispatch("start", false))
        compare(nowPlaying, 1)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
        compare(page.quickplayShown, false)
        router.nowPlaying.disconnect(onNowPlaying)
    }

    function test_landscape() {
        Ui.setSurface(960, 640)
        const page = pages.push(homeComponent, { animated: false })
        compare(Ui.landscape, true)
        compare(page.menuPanel.x, Ui.px(240))
        compare(page.quickplayPanel.x, 0)
        compare(page.quickplayPanel.y, 0)
        compare(page.quickplayPanel.scale, 1)
        fuzzyCompare(page.quickplayPanel.opacity, 0.6, 1e-6)
        fuzzyCompare(page.menuPanel.opacity, 1, 1e-6)
        fuzzyCompare(page.watermark.x, Ui.px(70), 0.01)
        fuzzyCompare(page.watermark.mapToItem(page, 0, 0).x, Ui.px(240 + 70), 0.01)
        fuzzyCompare(page.menuItem(1).labelBaseline, Ui.px(48 + 48), 0.5)
        router.dispatch("left", false)
        compare(page.quickplayShown, true)
        compare(page.pan, 0)
        compare(page.menuPanel.x, Ui.px(240))
        compare(page.menuPanel.scale, 1)
        fuzzyCompare(page.quickplayPanel.opacity, 1, 1e-6)
        fuzzyCompare(page.menuPanel.opacity, 0.6, 1e-6)
        fuzzyCompare(page.watermark.opacity, 0, 1e-6)
        router.dispatch("right", false)
        router.dispatch("right", false)
        compare(page.quickplayShown, false)
        fuzzyCompare(page.menuPanel.opacity, 1, 1e-6)
        fuzzyCompare(page.quickplayPanel.opacity, 0.6, 1e-6)
    }

    function test_t2_exit_on_push() {
        const page = open({ animated: true })
        page.completeAll()
        page.setFocus(1)
        pages.animate = true
        compare(page.pageMotion, "home")
        compare(page.exitDuration, Motion.homeOut + 3 * Motion.homeOutStagger)
        pages.push(blankComponent, {})
        compare(pages.phase, "exit")
        const holder = pages.outgoing
        compare(holder.kind, "home")
        compare(holder.duration, page.exitDuration)
        compare(holder.toAngle, 0)
        compare(holder.opacity, 1)
        const focused = page.menuItem(1)
        const far = page.menuItem(4)
        compare(focused.exitStagger, 0)
        compare(page.menuItem(0).exitStagger, Motion.homeOutStagger)
        compare(far.exitStagger, 3 * Motion.homeOutStagger)
        tryVerify(function() { return focused.exit > 0 && focused.exit < 1 }, 1000)
        const curve = focused.exitCurve
        verify(focused.y < focused.restY)
        verify(focused.opacity < 1)
        fuzzyCompare(focused.y, focused.restY + curve * Motion.homeOutShift * page.height, Ui.px(1))
        fuzzyCompare(focused.opacity, 1 - curve, 0.05)
        verify(page.quickplayPanel.opacity < 0.4)
        verify(page.focusBar.opacity < 1)
        tryCompare(focused, "exit", 1, 2000)
        compare(focused.opacity, 0)
        fuzzyCompare(focused.y, focused.restY + Motion.homeOutShift * page.height, 0.01)
        tryCompare(far, "exit", 1, 2000)
        tryCompare(page, "peekExit", 1, 2000)
        fuzzyCompare(page.quickplayPanel.opacity, 0, 1e-6)
        tryCompare(pages, "phase", "idle", 3000)
        compare(page.parent.visible, false)
    }

    function test_t4_home_return_replays_t1() {
        const page = open({ animated: true })
        page.completeAll()
        pages.animate = true
        pages.push(blankComponent, {})
        pages.completeAll()
        const item = page.menuItem(0)
        tryCompare(item, "exit", 1, 2000)
        compare(item.opacity, 0)
        compare(page.parent.visible, false)
        pages.pop()
        compare(pages.phase, "exit")
        const holder = pages.incoming
        compare(holder.kind, "home")
        compare(holder.duration, 0)
        compare(holder.opacity, 0)
        compare(pages.outgoing.flying, true)
        compare(pages.transitionKind, "turnstile")
        tryCompare(pages, "phase", "idle", 3000)
        compare(holder.opacity, 1)
        compare(item.exit, 0)
        verify(item.enter < 1)
        compare(item.enterStagger, 0)
        compare(page.menuItem(4).enterStagger, 0)
        verify(item.x > Ui.px(70))
        verify(page.peekEnter < 1)
        tryCompare(item, "enter", 1, 2000)
        tryCompare(page.menuItem(4), "enter", 1, 2000)
        tryCompare(page, "peekEnter", 1, 2000)
        fuzzyCompare(item.x, Ui.px(70), 0.01)
        compare(item.opacity, 1)
        fuzzyCompare(page.quickplayPanel.opacity, 0.4, 1e-3)
        fuzzyCompare(page.focusBar.opacity, 1, 1e-3)
    }

    function test_exit_skipped_without_animated() {
        const page = open()
        pages.animate = true
        pages.push(blankComponent, {})
        compare(pages.outgoing.kind, "home")
        compare(page.menuItem(0).exit, 0)
        compare(page.menuItem(0).opacity, 1)
        pages.completeAll()
        pages.pop()
        pages.completeAll()
        compare(page.menuItem(0).enter, 1)
        compare(page.menuItem(0).opacity, 1)
        compare(page.peekEnter, 1)
    }

    function test_tile_tops_match_layout() {
        fill(pins, "Pin", 3)
        fill(history, "History", 8)
        fill(fresh, "New", 8)
        const page = open()
        const qp = page.quickplay
        compare(qp.count, 19)
        for (let i = 0; i < qp.count; i++) {
            const item = qp.tileAt(i)
            verify(item !== null)
            fuzzyCompare(qp.tileTop(i), item.mapToItem(qp.flick.contentItem, 0, 0).y, 0.5)
        }
        fuzzyCompare(qp.headerTop(1), qp.historyHeader.mapToItem(qp.flick.contentItem, 0, 0).y, 0.5)
        fuzzyCompare(qp.headerTop(2), qp.newHeader.mapToItem(qp.flick.contentItem, 0, 0).y, 0.5)
    }

    function test_rows_changed_keeps_focus_visible() {
        fill(history, "History", 8)
        fill(fresh, "New", 8)
        const page = open()
        router.dispatch("left", false)
        const qp = page.quickplay
        for (let i = 0; i < 8; i++)
            router.dispatch("down", false)
        tryVerify(function() { return !qp.scrolling }, 2000)
        verify(qp.flick.contentY > 0)
        history.move(7, 0, 1)
        fresh.remove(7)
        fresh.remove(6)
        fresh.remove(5)
        tryVerify(function() { return !qp.scrolling }, 2000)
        wait(20)
        verify(qp.focusIndex < qp.count)
        const top = qp.tileTop(qp.focusIndex) - qp.flick.contentY
        verify(top >= -0.5)
        verify(top + qp.tilePx <= qp.height + 0.5)
    }

    function tapAt(item, x, y) {
        mouseClick(item, x, y)
        wait(0)
        wait(0)
    }

    function dragUp(item, x, y, distance) {
        mousePress(item, x, y)
        const steps = Math.round(distance / Ui.px(1))
        for (let i = 1; i <= steps; i++)
            mouseMove(item, x, y - distance * i / steps, -1, Qt.LeftButton)
        wait(150)
        mouseRelease(item, x, y - distance)
        wait(0)
        wait(0)
    }

    function test_touch_menu_opens_item() {
        const page = open()
        tc.tapAt(page.menuItem(2))
        compare(page.focusIndex, 2)
        compare(counters.navigated, ["playlists"])
    }

    function test_touch_peek_brings_quickplay() {
        const page = open()
        tc.tapAt(page.quickplayPanel, Ui.px(4), Ui.px(4))
        compare(page.quickplayShown, true)
        compare(counters.navigated, [])
    }

    function test_touch_peek_tile_only_brings_quickplay() {
        const page = open()
        const qp = page.quickplay
        const tile = qp.tileAt(qp.pinsCount)
        compare(qp.active, false)
        const before = qp.focusIndex
        compare(qp.touchPress("tile", qp.pinsCount, 0, tile), "left")
        compare(qp.focusIndex, before)
        tc.tapAt(tile)
        compare(page.quickplayShown, true)
        compare(counters.played, [])
    }

    function test_touch_tile_in_front_plays() {
        const page = open()
        page.showQuickplay()
        page.completeAll()
        const qp = page.quickplay
        tc.tapAt(qp.tileAt(qp.pinsCount + 1))
        compare(qp.focusIndex, qp.pinsCount + 1)
        compare(counters.played.length, 1)
        compare(counters.played[0].title, "History 2")
    }

    function test_touch_scaled_menu_returns() {
        const page = open()
        page.showQuickplay()
        page.completeAll()
        tc.tapAt(page.menuItem(0))
        compare(page.quickplayShown, false)
        compare(counters.navigated, [])
    }

    function test_touch_landscape_tile_focuses_and_plays() {
        Ui.setSurface(960, 640)
        const page = pages.push(homeComponent, { animated: false })
        compare(Ui.landscape, true)
        const qp = page.quickplay
        tc.tapAt(qp.tileAt(qp.pinsCount))
        compare(page.quickplayShown, true)
        compare(counters.played.length, 1)
        compare(counters.played[0].title, "History 1")
    }

    function test_touch_landscape_menu_tap_from_quickplay() {
        Ui.setSurface(960, 640)
        const page = pages.push(homeComponent, { animated: false })
        router.dispatch("left", false)
        compare(page.quickplayShown, true)
        tc.tapAt(page.menuItem(3))
        compare(page.quickplayShown, false)
        compare(page.focusIndex, 3)
        compare(counters.navigated, ["history"])
        compare(counters.played, [])
    }

    function test_touch_landscape_tile_press_keeps_content_y() {
        Ui.setSurface(960, 640)
        fill(history, "History", 8)
        fill(fresh, "New", 8)
        const page = pages.push(homeComponent, { animated: false })
        const qp = page.quickplay
        qp.flick.contentY = qp.headerTop(1) + Ui.px(4)
        const y = qp.flick.contentY
        const k = qp.pinsCount + 2
        verify(qp.tileTop(k) >= y)
        const tile = qp.tileAt(k)
        mousePress(tile, tile.width / 2, tile.height / 2)
        compare(page.quickplayShown, true)
        compare(qp.focusIndex, k)
        compare(qp.flick.contentY, y)
        mouseRelease(tile, tile.width / 2, tile.height / 2)
        wait(0)
        wait(0)
        compare(counters.played.length, 1)
    }

    function test_touch_overlapped_menu_column_does_not_return() {
        Ui.userScale = 1.1
        verify(Ui.canvasW < 300)
        const page = open()
        page.showQuickplay()
        page.completeAll()
        const qp = page.quickplay
        const right = page.quickplayPanel.x + page.quickplayPanel.width * page.quickplayPanel.scale
        const ty = qp.tileTop(qp.pinsCount + 1) - qp.flick.contentY + qp.tilePx / 2
        const x = page.menuPanel.mapToItem(page, Ui.px(2), page.menuPanel.mapFromItem(page, 0, ty).y).x
        verify(x < right)
        verify(page.menuPanel.contains(page.menuPanel.mapFromItem(page, x, ty)))
        tc.tapAt(page, x, ty)
        compare(page.quickplayShown, true)
        compare(counters.navigated, [])
    }

    function test_touch_watermark_does_nothing() {
        const page = open()
        tc.tapAt(page.watermark)
        compare(page.quickplayShown, false)
        compare(counters.navigated, [])
    }

    function test_touch_quickplay_drag_only_in_front() {
        fill(history, "History", 8)
        fill(fresh, "New", 8)
        const page = open()
        const qp = page.quickplay
        const x = Ui.px(4)
        tc.dragUp(page.quickplayPanel, x, page.quickplayPanel.height * 0.5, Ui.px(60))
        compare(qp.flick.contentY, 0)
        compare(page.quickplayShown, false)
        page.showQuickplay()
        page.completeAll()
        tc.dragUp(page, Ui.px(100), page.height - Ui.px(20), Ui.px(100))
        verify(qp.flick.contentY > 0)
        compare(counters.played, [])
    }

    function test_touch_half_tile_press_keeps_content_y() {
        fill(history, "History", 8)
        fill(fresh, "New", 8)
        const page = open()
        page.showQuickplay()
        page.completeAll()
        const qp = page.quickplay
        const k = qp.pinsCount + 2
        qp.flick.contentY = qp.tileTop(k) + qp.tilePx / 2
        const y = qp.flick.contentY
        const tile = qp.tileAt(k)
        mousePress(tile, tile.width / 2, tile.height * 0.75)
        compare(qp.focusIndex, k)
        compare(qp.flick.contentY, y)
        mouseRelease(tile, tile.width / 2, tile.height * 0.75)
        wait(0)
        wait(0)
        compare(counters.played.length, 1)
    }

    function flingQuickplay(page) {
        const qp = page.quickplay
        const x = Ui.px(100)
        const y = page.height - Ui.px(20)
        mousePress(page, x, y)
        for (let i = 1; i <= 20; i++) {
            mouseMove(page, x, y - Ui.px(150) * i / 20, -1, Qt.LeftButton)
            wait(5)
        }
        mouseRelease(page, x, y - Ui.px(150))
        wait(0)
        wait(0)
        return qp
    }

    function test_touch_fling_across_clamp_keeps_moving() {
        fill(history, "History", 8)
        fill(fresh, "New", 8)
        const page = open()
        page.showQuickplay()
        page.completeAll()
        const qp = tc.flingQuickplay(page)
        verify(qp.flick.touch.flinging)
        const atRelease = qp.flick.contentY
        wait(50)
        verify(qp.flick.contentY > atRelease)
        tryVerify(() => !qp.flick.touch.flinging, 5000)
        verify(qp.focusIndex > 0)
        const top = qp.tileTop(qp.focusIndex)
        verify(top >= qp.flick.contentY && top + qp.tilePx <= qp.flick.contentY + qp.flick.height)
    }

    function test_touch_fling_across_settle_stops() {
        fill(history, "History", 8)
        fill(fresh, "New", 8)
        const page = open()
        page.showQuickplay()
        page.completeAll()
        const qp = tc.flingQuickplay(page)
        verify(qp.flick.touch.flinging)
        fresh.remove(fresh.count - 1)
        wait(0)
        wait(0)
        compare(qp.flick.touch.flinging, false)
        verify(qp.flick.contentY <= Math.max(0, qp.flick.contentHeight - qp.flick.height))
    }
}
