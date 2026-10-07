import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "Focus"
    when: windowShown
    visible: true
    width: 640
    height: 960

    readonly property real rowPx: Ui.px(Ui.rowHeight)
    readonly property real listTop: Ui.px(Ui.contentTop)
    readonly property real listHeight: tc.height - tc.listTop

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

    Component {
        id: listPage

        Page {
            id: pg

            property alias rows: rowList
            property alias bar: focusBar
            property int activatedIndex: -1

            list: rowList
            onActivated: (index) => pg.activatedIndex = index

            FocusList {
                id: rowList

                y: Ui.px(Ui.contentTop)
                width: pg.width
                height: pg.height - Ui.px(Ui.contentTop)
                model: 50

                delegate: PressScale {
                    id: row

                    required property int index
                    readonly property bool isFocused: row.index === rowList.focusIndex

                    width: rowList.width
                    height: rowList.rowPitch
                    pressed: row.isFocused && pg.pressed

                    Text {
                        x: Ui.px(Ui.hubEdge)
                        text: "Row " + row.index
                        color: Ui.white(row.isFocused ? Ui.listPrimary.alpha : Ui.listPrimary.alphaDim)
                        font: Ui.font(Ui.listPrimary)
                    }
                }
            }

            FocusBar {
                id: focusBar

                rowY: rowList.y + rowList.focusY
            }
        }
    }

    Component {
        id: plainPage

        Page {
            focusCount: 3
        }
    }

    Timer {
        id: hold

        property string name: ""

        interval: Ui.stickRepeatFirst
        repeat: true
        onTriggered: {
            router.dispatch(hold.name, true)
            if (hold.interval !== Ui.stickRepeat)
                hold.interval = Ui.stickRepeat
        }
    }

    function init() {
        Ui.userScale = 1.0
        Ui.windowW = 640
        Ui.windowH = 960
        pages.clear()
        pages.animate = true
        router.screenOff = false
        router.keyboard = true
        hold.stop()
        hold.interval = Ui.stickRepeatFirst
    }

    function openList() {
        const page = pages.push(listPage)
        page.rows.forceLayout()
        tryCompare(page.rows, "contentHeight", 50 * tc.rowPx, 1000)
        return page
    }

    function focusedCount(page) {
        page.rows.forceLayout()
        let n = 0
        const children = page.rows.contentItem.children
        for (let i = 0; i < children.length; i++) {
            if (children[i].isFocused === true)
                n++
        }
        return n
    }

    function settle(page) {
        tryVerify(function() { return !page.rows.scrolling }, 2000)
        page.rows.forceLayout()
    }

    function rowTop(page, index) {
        return index * tc.rowPx - page.rows.contentY
    }

    function verifyInside(page, index) {
        const top = rowTop(page, index)
        verify(top >= -0.5, "row " + index + " top " + top)
        verify(top <= tc.listHeight - tc.rowPx + 0.5, "row " + index + " top " + top)
    }

    function test_geometry() {
        const page = openList()
        compare(page.stack, pages)
        compare(page.rows.count, 50)
        compare(page.rows.rowPitch, 80)
        compare(page.rows.height, 704)
        compare(page.rows.pageRows, 8)
        compare(page.rows.pageDistance, 640)
        compare(page.rows.maxContentY, 50 * 80 - 704)
        compare(page.focusIndex, 0)
        compare(page.rows.contentY, 0)
        compare(focusedCount(page), 1)
    }

    function test_down_keeps_focus_visible() {
        const page = openList()
        for (let i = 1; i < 50; i++) {
            verify(router.dispatch("down", false))
            compare(page.focusIndex, i)
            compare(page.rows.focusIndex, i)
            if (page.rows.scrolling) {
                compare(page.rows.scrollDuration, Motion.scroll)
                verify(page.rows.contentY < page.rows.scrollTarget)
            }
            settle(page)
            verifyInside(page, i)
            compare(focusedCount(page), 1)
        }
        compare(page.focusIndex, 49)
        compare(page.rows.contentY, page.rows.maxContentY)
        router.dispatch("down", false)
        compare(page.focusIndex, 49)
    }

    function test_first_scroll_step() {
        const page = openList()
        for (let i = 0; i < 7; i++)
            router.dispatch("down", false)
        compare(page.focusIndex, 7)
        compare(page.rows.contentY, 0)
        verify(!page.rows.scrolling)
        router.dispatch("down", false)
        compare(page.focusIndex, 8)
        verify(page.rows.scrolling)
        compare(page.rows.scrollDuration, 100)
        compare(page.rows.scrollTarget, 8 * 80 - (704 - 80))
        verify(page.rows.contentY < page.rows.scrollTarget)
        tryCompare(page.rows, "contentY", 8 * 80 - (704 - 80), 1000)
        verify(!page.rows.scrolling)
        verifyInside(page, 8)
    }

    function test_up_keeps_focus_visible() {
        const page = openList()
        page.setFocus(49)
        page.rows.forceLayout()
        compare(page.focusIndex, 49)
        compare(page.rows.contentY, page.rows.maxContentY)
        verifyInside(page, 49)
        for (let i = 48; i >= 0; i--) {
            verify(router.dispatch("up", false))
            compare(page.focusIndex, i)
            settle(page)
            verifyInside(page, i)
            compare(focusedCount(page), 1)
        }
        compare(page.rows.contentY, 0)
    }

    function test_page_rt_lt() {
        const page = openList()
        verify(router.dispatch("rt", false))
        compare(page.focusIndex, 8)
        verify(page.rows.scrolling)
        compare(page.rows.scrollDuration, Motion.page)
        compare(page.rows.scrollDuration, 500)
        compare(page.rows.scrollTarget, 640)
        wait(250)
        verify(page.rows.contentY > 0)
        verify(page.rows.contentY < 640)
        tryCompare(page.rows, "contentY", 640, 1500)
        compare(focusedCount(page), 1)
        verify(router.dispatch("rt", false))
        compare(page.focusIndex, 16)
        compare(page.rows.scrollTarget, 1280)
        tryCompare(page.rows, "contentY", 1280, 1500)
        verify(router.dispatch("lt", false))
        compare(page.focusIndex, 8)
        compare(page.rows.scrollTarget, 640)
        tryCompare(page.rows, "contentY", 640, 1500)
        verify(router.dispatch("lt", false))
        compare(page.focusIndex, 0)
        tryCompare(page.rows, "contentY", 0, 1500)
        verify(router.dispatch("lt", false))
        compare(page.focusIndex, 0)
        compare(page.rows.contentY, 0)
    }

    function test_page_rt_clamps_at_end() {
        const page = openList()
        page.setFocus(45)
        router.dispatch("rt", false)
        compare(page.focusIndex, 49)
        tryCompare(page.rows, "contentY", page.rows.maxContentY, 1500)
        verifyInside(page, 49)
    }

    function test_held_down_repeat() {
        const page = openList()
        hold.name = "down"
        router.dispatch("down", false)
        hold.start()
        tryCompare(page, "focusIndex", 20, 2000)
        hold.stop()
        settle(page)
        verifyInside(page, page.focusIndex)
        compare(focusedCount(page), 1)
    }

    function test_one_focused_item() {
        const page = openList()
        compare(focusedCount(page), 1)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(focusedCount(page), 1)
        router.dispatch("rt", false)
        tryCompare(page.rows, "contentY", 640, 1500)
        compare(focusedCount(page), 1)
        page.setFocus(49)
        compare(focusedCount(page), 1)
        verify(page.focusItem() !== null)
        compare(page.focusItem().index, 49)
    }

    function test_focus_bar_follows() {
        const page = openList()
        compare(page.bar.x, Ui.px(Ui.hubEdge - 8))
        compare(page.bar.width, Ui.px(2))
        compare(page.bar.height, 80)
        compare(page.bar.y, tc.listTop)
        router.dispatch("down", false)
        compare(page.bar.rowY, tc.listTop + 80)
        verify(page.bar.y < tc.listTop + 80)
        tryCompare(page.bar, "y", tc.listTop + 80, 1000)
    }

    function test_press_scale() {
        const page = openList()
        const item = page.focusItem()
        verify(item !== null)
        compare(item.scale, 1)
        router.press("a")
        compare(page.pressed, true)
        tryVerify(function() { return item.scale < 1 }, 1000)
        tryCompare(item, "scale", Motion.pressScale, 1000)
        compare(item.scale, 0.96)
        router.dispatch("a", false)
        compare(page.activatedIndex, 0)
        compare(item.scale, 0.96)
        router.release("a")
        compare(page.pressed, false)
        tryCompare(item, "scale", 1, 1000)
        const other = page.rows.itemAtIndex(1)
        verify(other !== null)
        compare(other.scale, 1)
    }

    function test_b_pops() {
        openList()
        openList()
        pages.completeAll()
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
        pages.completeAll()
        verify(!router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_locked_drops_input() {
        const a = openList()
        pages.push(listPage)
        compare(pages.inputLocked, true)
        verify(!router.dispatch("down", false))
        verify(!router.dispatch("b", false))
        compare(pages.depth, 2)
        compare(a.focusIndex, 0)
        pages.completeAll()
        compare(pages.inputLocked, false)
        const b = pages.currentItem
        b.rows.forceLayout()
        verify(router.dispatch("down", false))
        compare(b.focusIndex, 1)
        compare(a.focusIndex, 0)
    }

    function test_global_start_and_back() {
        const page = openList()
        let nowPlayingCount = 0
        let viewCount = 0
        const onNowPlaying = function() { nowPlayingCount++ }
        const onView = function(name) { viewCount++ }
        router.nowPlaying.connect(onNowPlaying)
        router.view.connect(onView)
        verify(router.dispatch("start", false))
        compare(nowPlayingCount, 1)
        verify(router.dispatch("back", false))
        compare(viewCount, 1)
        verify(!router.dispatch("start", true))
        compare(nowPlayingCount, 1)
        verify(!router.dispatch("x", false))
        compare(page.focusIndex, 0)
        router.nowPlaying.disconnect(onNowPlaying)
        router.view.disconnect(onView)
    }

    function test_screen_off_drops_every_action() {
        const page = openList()
        let dropped = 0
        const onDropped = function() { dropped++ }
        router.dropped.connect(onDropped)
        router.screenOff = true
        verify(!router.dispatch("rb", false))
        verify(!router.dispatch("lb", false))
        verify(!router.dispatch("start", false))
        verify(!router.dispatch("down", false))
        compare(dropped, 4)
        compare(page.focusIndex, 0)
        router.press("a")
        compare(page.pressed, false)
        router.screenOff = false
        verify(router.dispatch("down", false))
        compare(page.focusIndex, 1)
        router.dropped.disconnect(onDropped)
    }

    function test_keyboard_path() {
        const page = openList()
        router.forceActiveFocus()
        tryCompare(router, "activeFocus", true, 1000)
        keyClick(Qt.Key_Down)
        compare(page.focusIndex, 1)
        keyClick(Qt.Key_Up)
        compare(page.focusIndex, 0)
        keyClick(Qt.Key_PageDown)
        compare(page.focusIndex, 8)
        tryCompare(page.rows, "contentY", 640, 1500)
        keyClick(Qt.Key_PageUp)
        compare(page.focusIndex, 0)
        tryCompare(page.rows, "contentY", 0, 1500)
        keyPress(Qt.Key_Return)
        compare(page.pressed, true)
        compare(page.activatedIndex, 0)
        keyRelease(Qt.Key_Return)
        compare(page.pressed, false)
        pages.push(listPage)
        pages.completeAll()
        compare(pages.depth, 2)
        keyClick(Qt.Key_Escape)
        compare(pages.depth, 1)
        pages.completeAll()
        compare(router.keyName(Qt.Key_X), "x")
        compare(router.keyName(Qt.Key_Y), "y")
        compare(router.keyName(Qt.Key_Q), "lb")
        compare(router.keyName(Qt.Key_E), "rb")
        compare(router.keyName(Qt.Key_Space), "start")
        compare(router.keyName(Qt.Key_Tab), "back")
        compare(router.keyName(Qt.Key_Backspace), "b")
        compare(router.keyName(Qt.Key_A), "")
    }

    function test_plain_page_focus() {
        const page = pages.push(plainPage)
        compare(page.focusCount, 3)
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        verify(router.dispatch("down", false))
        compare(page.focusIndex, 2)
        verify(router.dispatch("up", false))
        compare(page.focusIndex, 1)
        verify(!router.dispatch("rt", false))
        compare(page.focusItem(), null)
    }
}
