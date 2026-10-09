import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "QueueScreen"
    when: windowShown
    visible: true
    width: 640
    height: 960

    component FakeQueue: QtObject {
        id: q

        property var rows: []
        property var order: []
        property int index: -1
        property bool shuffle: false
        property int repeat: 0
        property var calls: []
        readonly property int count: q.rows.length
        readonly property int currentSlot: q.slotOf(q.index)
        property ListModel playOrder: ListModel {}

        onIndexChanged: q.renumber()

        function note(name) {
            q.calls = q.calls.concat([name])
        }

        function rowAt(slot) {
            return q.order.length > 0 ? q.order[slot] : slot
        }

        function slotOf(row) {
            if (row < 0)
                return -1
            return q.order.length > 0 ? q.order.indexOf(row) : row
        }

        function get(row) {
            return q.rows[row]
        }

        function entryFor(slot) {
            const r = q.rowAt(slot)
            const e = q.rows[r]
            return { row: r, slot: slot, number: slot + 1, id: e.id, title: e.title, artist: e.artist, album: e.album, durationMs: e.durationMs, isCurrent: r === q.index }
        }

        function renumber() {
            for (let s = 0; s < q.playOrder.count; s++) {
                const r = q.rowAt(s)
                q.playOrder.setProperty(s, "slot", s)
                q.playOrder.setProperty(s, "number", s + 1)
                q.playOrder.setProperty(s, "row", r)
                q.playOrder.setProperty(s, "isCurrent", r === q.index)
            }
        }

        function fill(n, current, order) {
            const rows = []
            for (let i = 0; i < n; i++)
                rows.push({ id: i + 1, title: "Song " + (i + 1), artist: "Artist " + (i + 1), album: "Album", artPath: "", durationMs: 240000 })
            q.playOrder.clear()
            q.rows = rows
            q.order = order === undefined ? [] : order
            q.index = n > 0 ? current : -1
            for (let s = 0; s < n; s++)
                q.playOrder.append(q.entryFor(s))
            q.calls = []
        }

        function insertAt(row, entry) {
            const rows = q.rows.slice()
            rows.splice(row, 0, entry)
            q.rows = rows
            if (row <= q.index)
                q.index++
            q.playOrder.insert(row, q.entryFor(row))
            q.renumber()
        }

        function remove(row) {
            q.note("remove " + row)
            if (row < 0 || row >= q.rows.length)
                return
            const slot = q.slotOf(row)
            const rows = q.rows.slice()
            rows.splice(row, 1)
            q.rows = rows
            if (q.order.length > 0)
                q.order = q.order.filter((r) => r !== row).map((r) => r > row ? r - 1 : r)
            let next = q.index
            if (row < next)
                next--
            else if (row === next && next >= rows.length)
                next = rows.length - 1
            q.playOrder.remove(slot)
            q.index = next
            q.renumber()
        }

        function shiftSlot(from, to) {
            const n = q.rows.length
            if (from < 0 || from >= n || to < 0 || to >= n || from === to)
                return
            if (q.order.length > 0) {
                const order = q.order.slice()
                const row = order.splice(from, 1)[0]
                order.splice(to, 0, row)
                q.order = order
            } else {
                const rows = q.rows.slice()
                const entry = rows.splice(from, 1)[0]
                rows.splice(to, 0, entry)
                q.rows = rows
                const cur = q.index
                let next = cur
                if (cur === from)
                    next = to
                else if (from < to && cur > from && cur <= to)
                    next = cur - 1
                else if (to < from && cur >= to && cur < from)
                    next = cur + 1
                q.index = next
            }
            q.playOrder.move(from, to, 1)
            q.renumber()
        }

        function moveSlot(from, to) {
            q.note("moveSlot " + from + " " + to)
            q.shiftSlot(from, to)
        }

        function playNextRow(row) {
            q.note("playNextRow " + row)
            const slot = q.slotOf(row)
            const cur = q.currentSlot
            q.shiftSlot(slot, slot < cur ? cur : cur + 1)
        }
    }

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
        onNowPlaying: counters.nowPlaying++
    }

    TouchLayer {
        id: layer

        width: 640
        height: 960
        router: router
    }

    FakeQueue {
        id: fakeQueue
    }

    QtObject {
        id: fakePlayer

        property int status: 1
        property var current: ({})
        property QtObject queue: fakeQueue
        property var calls: []

        function playAt(index) {
            fakePlayer.calls = fakePlayer.calls.concat(["playAt " + index])
            fakeQueue.index = index
        }
    }

    QtObject {
        id: counters

        property int nowPlaying: 0
    }

    FakeQueue {
        id: orderedQueue

        shuffle: true
    }

    QtObject {
        id: orderedPlayer

        property int status: 1
        property var current: ({})
        property QtObject queue: orderedQueue
        property var calls: []

        function playAt(index) {
            orderedPlayer.calls = orderedPlayer.calls.concat(["playAt " + index])
        }
    }

    Component {
        id: queueComponent

        QueueScreen {
            player: fakePlayer
        }
    }

    Component {
        id: blankComponent

        Page {}
    }

    function rowAt(index) {
        tryVerify(() => tc.list().itemAtIndex(index) !== null, 1000)
        return tc.list().itemAtIndex(index)
    }

    function list() {
        return pages.currentItem.list
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
        fakeQueue.fill(20, 2)
        fakePlayer.calls = []
        counters.nowPlaying = 0
    }

    function open(properties, belowName) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        const below = pages.push(blankComponent, {})
        below.objectName = belowName === undefined ? "nowplaying" : belowName
        const page = pages.push(queueComponent, props)
        page.completeAll()
        page.list.forceLayout()
        return page
    }

    function entering(index) {
        return function() {
            const row = tc.list().itemAtIndex(index)
            return row !== null && row.enter < 1
        }
    }

    function test_layout_portrait() {
        const page = open()
        compare(Ui.u, 2)
        compare(page.header.text, "queue")
        compare(page.countItem.text, "3 of 20")
        compare(page.countItem.font.capitalization, Font.AllUppercase)
        compare(page.countItem.font.pixelSize, Ui.fontPx(11))
        compare(page.countItem.x, Ui.px(20))
        fuzzyCompare(page.countItem.y + page.countItem.baselineOffset, Ui.px(95), 0.5)
        fuzzyCompare(page.countItem.y + page.countItem.baselineOffset, Ui.px(Ui.pivotBaseline), 0.5)
        verify(page.countItem.y + page.countItem.baselineOffset - Ui.px(0.7 * 11) > Ui.px(66))
        compare(page.list.y, Ui.px(128))
        compare(page.list.rowPitch, Ui.px(40))
        compare(page.list.count, 20)
        verify(page.list.model === fakeQueue.playOrder)
        compare(page.list.reuseItems, true)
        compare(page.rowIndex, 2)
        compare(page.focusBar.x, Ui.px(20 - 8))
        compare(page.focusBar.visible, true)
        fuzzyCompare(page.focusBar.y, Ui.px(128) + 2 * Ui.px(40), 0.01)
        const first = tc.rowAt(0)
        compare(first.rowItem.number, "1")
        compare(first.rowItem.primary, "Song 1")
        compare(first.rowItem.secondary, "Artist 1")
        compare(first.rowItem.textX, Ui.px(20 + 24))
        compare(first.rowItem.playing, false)
        const playing = tc.rowAt(2)
        compare(playing.rowItem.number, "3")
        compare(playing.rowItem.playing, true)
        compare(playing.rowItem.focused, true)
        compare(playing.rowItem.primaryColor, Ui.accent)
        verify(!Qt.colorEqual(first.rowItem.primaryColor, Ui.accent))
    }

    function test_layout_landscape() {
        tc.setWindow(1920, 1080)
        const page = open()
        compare(Ui.landscape, true)
        fuzzyCompare(page.countItem.y + page.countItem.baselineOffset, Ui.px(79), 0.5)
        compare(page.countItem.x, Ui.px(20))
        compare(page.list.y, Ui.px(98))
        compare(page.list.rowPitch, Ui.px(40))
        compare(page.list.width, 1920)
        compare(page.rowIndex, 2)
    }

    function test_count_follows_index() {
        const page = open()
        fakeQueue.index = 5
        compare(page.countText, "6 of 20")
        compare(tc.rowAt(5).rowItem.playing, true)
        compare(tc.rowAt(2).rowItem.playing, false)
        fakeQueue.fill(20, 0)
        compare(page.countText, "1 of 20")
    }

    function test_navigation_and_play() {
        const page = open()
        verify(router.dispatch("down", false))
        compare(page.rowIndex, 3)
        verify(router.dispatch("a", false))
        compare(fakePlayer.calls.length, 1)
        compare(fakePlayer.calls[0], "playAt 3")
        compare(page.countText, "4 of 20")
        compare(tc.rowAt(3).rowItem.playing, true)
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        verify(router.dispatch("up", false))
        compare(page.rowIndex, 0)
        compare(router.dispatch("up", false), false)
        verify(router.dispatch("rt", false))
        compare(page.rowIndex, page.list.pageRows)
        verify(router.dispatch("lt", false))
        compare(page.rowIndex, 0)
        for (let i = 0; i < 25; i++)
            router.dispatch("down", false)
        compare(page.rowIndex, 19)
        compare(router.dispatch("down", false), false)
    }

    function test_sheet_verbs_per_row() {
        const page = open()
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "Song 3")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["remove", "move up", "move down"]))
        verify(router.dispatch("b", false))
        compare(page.sheet.shown, false)
        compare(pages.depth, 2)
        router.dispatch("down", false)
        router.dispatch("x", false)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["remove", "move up", "move down"]))
        router.dispatch("b", false)
        router.dispatch("down", false)
        router.dispatch("x", false)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["remove", "play next", "move up", "move down"]))
        router.dispatch("b", false)
        page.list.snapTo(0)
        router.dispatch("x", false)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["remove", "play next", "move down"]))
        router.dispatch("b", false)
        page.list.snapTo(19)
        router.dispatch("x", false)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["remove", "play next", "move up"]))
        router.dispatch("b", false)
    }

    function test_sheet_motion() {
        const page = open({ animated: true })
        compare(page.sheet.scrimAlpha, 0.55)
        fuzzyCompare(page.sheet.scrimColor.a, 0.55, 0.01)
        compare(page.sheet.duration, 380)
        router.dispatch("x", false)
        compare(page.sheet.shown, true)
        verify(page.sheet.animating)
        compare(page.sheet.travel, page.sheet.panel.height)
        verify(page.sheet.panel.y > page.height - page.sheet.panel.height)
        tryCompare(page.sheet, "progress", 1, 2000)
        fuzzyCompare(page.sheet.panel.y, page.height - page.sheet.panel.height, 0.01)
        compare(page.sheet.scrim.opacity, 1)
        router.dispatch("b", false)
        tryCompare(page.sheet, "progress", 0, 2000)
        fuzzyCompare(page.sheet.panel.y, page.height, 0.01)
        tryCompare(page.sheet, "visible", false, 1000)
    }

    function test_y_removes_with_f12() {
        const page = open({ animated: true })
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.rowIndex, 4)
        compare(tc.rowAt(3).enter, 1)
        verify(router.dispatch("y", false))
        compare(fakeQueue.calls[0], "remove 4")
        compare(fakeQueue.count, 19)
        compare(page.count, 19)
        compare(page.rowIndex, 4)
        compare(page.countText, "3 of 19")
        tryVerify(tc.entering(4), 1000)
        compare(tc.rowAt(3).enter, 1)
        compare(tc.rowAt(4).rowItem.primary, "Song 6")
        compare(tc.rowAt(4).rowItem.number, "5")
        const visible = Math.min(9, Math.ceil(page.list.height / page.list.rowPitch))
        for (let i = 4; i < visible; i++) {
            const row = tc.rowAt(i)
            compare(row.stagger, Math.min(i - 4, 10) * 50)
            verify(row.enter < 1)
        }
        tryCompare(tc.rowAt(4), "enter", 1, 2000)
        tryCompare(tc.rowAt(visible - 1), "enter", 1, 2000)
        fuzzyCompare(page.list.itemAtIndex(visible - 1).x, 0, 0.01)
        compare(page.list.itemAtIndex(visible - 1).opacity, 1)
    }

    function test_remove_playing_row() {
        const page = open()
        compare(page.rowIndex, 2)
        router.dispatch("y", false)
        compare(fakeQueue.calls[0], "remove 2")
        compare(fakeQueue.index, 2)
        compare(page.countText, "3 of 19")
        compare(tc.rowAt(2).rowItem.primary, "Song 4")
        compare(tc.rowAt(2).rowItem.playing, true)
    }

    function test_remove_above_playing() {
        const page = open()
        page.list.snapTo(0)
        router.dispatch("y", false)
        compare(fakeQueue.index, 1)
        compare(page.countText, "2 of 19")
        compare(tc.rowAt(1).rowItem.primary, "Song 3")
        compare(tc.rowAt(1).rowItem.playing, true)
    }

    function test_remove_last_keeps_focus_in_range() {
        const page = open()
        page.list.snapTo(19)
        router.dispatch("y", false)
        compare(page.count, 19)
        compare(page.rowIndex, 18)
    }

    function test_move_up_via_sheet() {
        const page = open({ animated: true })
        router.dispatch("x", false)
        router.dispatch("down", false)
        compare(page.sheet.items[page.sheet.focusIndex], "move up")
        router.dispatch("a", false)
        compare(page.sheet.shown, false)
        compare(fakeQueue.calls[0], "moveSlot 2 1")
        compare(fakeQueue.index, 1)
        compare(page.rowIndex, 1)
        compare(page.countText, "2 of 20")
        tryVerify(tc.entering(1), 1000)
        compare(tc.rowAt(0).enter, 1)
        compare(tc.rowAt(1).rowItem.primary, "Song 3")
        compare(tc.rowAt(1).rowItem.playing, true)
        compare(tc.rowAt(2).rowItem.primary, "Song 2")
        compare(tc.rowAt(1).stagger, 0)
        compare(tc.rowAt(2).stagger, 50)
        tryCompare(tc.rowAt(1), "enter", 1, 2000)
    }

    function test_move_down_via_sheet() {
        const page = open({ animated: true })
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.sheet.items[page.sheet.focusIndex], "move down")
        router.dispatch("a", false)
        compare(fakeQueue.calls[0], "moveSlot 2 3")
        compare(fakeQueue.index, 3)
        compare(page.rowIndex, 3)
        compare(page.countText, "4 of 20")
        tryVerify(tc.entering(2), 1000)
        compare(tc.rowAt(1).enter, 1)
        compare(tc.rowAt(2).rowItem.primary, "Song 4")
        compare(tc.rowAt(3).rowItem.primary, "Song 3")
        compare(tc.rowAt(3).rowItem.playing, true)
        compare(tc.rowAt(2).stagger, 0)
        compare(tc.rowAt(3).stagger, 50)
    }

    function test_play_next_below_current() {
        const page = open()
        page.list.snapTo(6)
        router.dispatch("x", false)
        router.dispatch("down", false)
        compare(page.sheet.items[page.sheet.focusIndex], "play next")
        router.dispatch("a", false)
        compare(fakeQueue.calls[0], "playNextRow 6")
        compare(fakeQueue.index, 2)
        compare(page.rowIndex, 3)
        compare(tc.rowAt(3).rowItem.primary, "Song 7")
        compare(tc.rowAt(4).rowItem.primary, "Song 4")
    }

    function test_play_next_above_current() {
        const page = open()
        page.list.snapTo(0)
        router.dispatch("x", false)
        router.dispatch("down", false)
        compare(page.sheet.items[page.sheet.focusIndex], "play next")
        router.dispatch("a", false)
        compare(fakeQueue.calls[0], "playNextRow 0")
        compare(fakeQueue.index, 1)
        compare(page.rowIndex, 2)
        compare(tc.rowAt(1).rowItem.primary, "Song 3")
        compare(tc.rowAt(1).rowItem.playing, true)
        compare(tc.rowAt(2).rowItem.primary, "Song 1")
    }

    function test_remove_via_sheet() {
        const page = open()
        router.dispatch("x", false)
        compare(page.sheet.items[0], "remove")
        router.dispatch("a", false)
        compare(fakeQueue.calls[0], "remove 2")
        compare(page.count, 19)
    }

    function test_external_insert_runs_f12() {
        const page = open({ animated: true })
        fakeQueue.insertAt(1, { id: 99, title: "Inserted", artist: "Artist", album: "", artPath: "", durationMs: 1000 })
        compare(page.count, 21)
        tryVerify(tc.entering(1), 1000)
        compare(tc.rowAt(0).enter, 1)
        compare(tc.rowAt(1).rowItem.primary, "Inserted")
        compare(tc.rowAt(3).rowItem.playing, true)
        compare(page.countText, "4 of 21")
        tryCompare(tc.rowAt(1), "enter", 1, 2000)
    }

    function test_play_order_rows_and_verbs() {
        orderedQueue.fill(5, 0, [2, 0, 4, 1, 3])
        const page = open({ player: orderedPlayer })
        compare(page.count, 5)
        compare(page.countText, "2 of 5")
        compare(page.rowIndex, 1)
        compare(tc.rowAt(0).rowItem.primary, "Song 3")
        compare(tc.rowAt(0).rowItem.number, "1")
        compare(tc.rowAt(1).rowItem.primary, "Song 1")
        compare(tc.rowAt(1).rowItem.playing, true)
        compare(tc.rowAt(2).rowItem.primary, "Song 5")
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(orderedPlayer.calls[0], "playAt 4")
        page.list.snapTo(3)
        router.dispatch("x", false)
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["remove", "play next", "move up", "move down"]))
        router.dispatch("down", false)
        router.dispatch("a", false)
        compare(orderedQueue.calls[0], "playNextRow 1")
        compare(page.rowIndex, 2)
        compare(tc.rowAt(2).rowItem.primary, "Song 2")
        compare(tc.rowAt(3).rowItem.primary, "Song 5")
        page.list.snapTo(0)
        router.dispatch("x", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.sheet.items[page.sheet.focusIndex], "move down")
        router.dispatch("a", false)
        compare(orderedQueue.calls[1], "moveSlot 0 1")
        compare(page.rowIndex, 1)
        compare(page.countText, "1 of 5")
        compare(tc.rowAt(0).rowItem.playing, true)
        router.dispatch("y", false)
        compare(orderedQueue.calls[2], "remove 2")
    }

    function test_no_f12_when_not_animated() {
        const page = open()
        router.dispatch("y", false)
        wait(20)
        compare(tc.rowAt(2).enter, 1)
        compare(tc.rowAt(5).enter, 1)
    }

    function test_screen_off_settles() {
        const page = open({ animated: true })
        router.dispatch("y", false)
        tryVerify(tc.entering(2), 1000)
        router.dispatch("x", false)
        page.screenOff = true
        compare(tc.rowAt(2).enter, 1)
        compare(tc.rowAt(6).enter, 1)
        compare(page.sheet.animating, false)
        compare(page.sheet.visible, false)
    }

    function test_empty_queue() {
        fakeQueue.fill(0, 0)
        const page = open()
        compare(page.countText, "0 of 0")
        compare(page.list.count, 0)
        compare(page.focusBar.visible, false)
        compare(router.dispatch("x", false), true)
        compare(page.sheet.shown, false)
        router.dispatch("y", false)
        router.dispatch("a", false)
        compare(fakeQueue.calls.length, 0)
        compare(fakePlayer.calls.length, 0)
        compare(router.dispatch("up", false), false)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_b_pops() {
        open()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_start_pops_to_now_playing() {
        open()
        compare(pages.depth, 2)
        verify(router.dispatch("start", false))
        compare(pages.depth, 1)
        compare(counters.nowPlaying, 0)
    }

    function test_start_elsewhere_opens_now_playing() {
        open({}, "home")
        verify(router.dispatch("start", false))
        compare(pages.depth, 2)
        compare(counters.nowPlaying, 1)
    }

    function test_turnstile_kind() {
        pages.animate = true
        const below = pages.push(blankComponent, {})
        below.objectName = "nowplaying"
        pages.push(queueComponent, { animated: true })
        compare(pages.transitionKind, "turnstile")
        pages.completeAll()
        router.dispatch("b", false)
        compare(pages.transitionKind, "turnstile")
        pages.completeAll()
        compare(pages.depth, 1)
    }

    function test_t2a_first_show() {
        pages.animate = true
        const below = pages.push(blankComponent, {})
        below.objectName = "nowplaying"
        const page = pages.push(queueComponent, { animated: true })
        compare(page.arriving, true)
        const first = tc.rowAt(0)
        compare(first.enter, 0)
        compare(first.shift, Motion.pivotShift)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, Ui.px(34), 0.01)
        compare(first.opacity, 0)
        tryCompare(pages, "phase", "idle", 3000)
        compare(page.arriving, false)
        compare(first.enterDuration, 380)
        compare(first.stagger, 0)
        compare(tc.rowAt(3).stagger, 3 * 22)
        const deep = tc.list().itemAtIndex(12)
        if (deep !== null)
            compare(deep.stagger, 10 * 22)
        tryCompare(first, "enter", 1, 2000)
        tryCompare(tc.rowAt(3), "enter", 1, 2000)
        fuzzyCompare(first.mapToItem(first.parent, 0, 0).x - first.x, 0, 0.01)
        compare(first.opacity, 1)
    }

    function test_t2a_rest_without_animation() {
        const page = open({ animated: true })
        compare(page.arriving, false)
        const first = tc.rowAt(0)
        compare(first.enter, 1)
        compare(first.x, 0)
        compare(first.opacity, 1)
    }

    function test_count_f7() {
        const page = open({ animated: true })
        compare(page.countItem.text, "3 of 20")
        fakeQueue.index = 5
        compare(page.countText, "6 of 20")
        compare(page.countSwapping, true)
        compare(page.countItem.text, "3 of 20")
        tryCompare(page, "shownCount", "6 of 20", 1000)
        verify(page.countItem.opacity < 1)
        tryCompare(page, "countSwapping", false, 1000)
        compare(page.countItem.opacity, 1)
        compare(page.countItem.text, "6 of 20")
        page.screenOff = true
        fakeQueue.index = 7
        compare(page.countSwapping, false)
        compare(page.countItem.text, "8 of 20")
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

    function tapHeaderWord(page) {
        tc.tapAt(page.header, page.header.textX + Ui.px(4), page.header.textBaseline - Ui.px(4))
    }

    function test_touch_row_plays() {
        open()
        tc.tapAt(tc.rowAt(5))
        compare(tc.list().focusIndex, 5)
        compare(fakePlayer.calls, ["playAt 5"])
    }

    function test_touch_drag_then_x_titles_clamped_row() {
        const page = open()
        const l = tc.list()
        tc.dragUp(page, page.width / 2, page.height - Ui.px(20), l.height + Ui.px(40))
        const first = Math.ceil(l.contentY / l.rowPitch - 0.001)
        verify(first > 2)
        compare(l.focusIndex, first)
        compare(page.focusIndex, first)
        verify(router.dispatch("x", false))
        compare(page.sheet.title, page.focusedRow.title)
        compare(page.sheet.title, "Song " + (first + 1))
    }

    function test_touch_header_pops() {
        const page = open()
        tc.tapHeaderWord(page)
        compare(pages.depth, 1)
    }
}
