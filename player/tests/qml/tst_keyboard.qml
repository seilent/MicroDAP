import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "Keyboard"
    when: windowShown
    visible: true
    width: 640
    height: 960

    property var kb: null
    property var doneText: []
    property var refusals: []
    property int cancels: 0

    Component {
        id: keyboardComponent

        Keyboard {
            anchors.fill: parent
            animated: false
        }
    }

    function init() {
        Ui.setSurface(640, 960)
        tc.doneText = []
        tc.refusals = []
        tc.cancels = 0
        tc.kb = createTemporaryObject(keyboardComponent, tc)
        tc.kb.done.connect((text) => { tc.doneText = tc.doneText.concat([text]) })
        tc.kb.refused.connect((title, reason) => { tc.refusals = tc.refusals.concat([title + "|" + reason]) })
        tc.kb.canceled.connect(() => { tc.cancels++ })
    }

    function cleanup() {
        Ui.setSurface(640, 960)
    }

    function indexOf(code) {
        const keys = tc.kb.keys
        for (let i = 0; i < keys.length; i++) {
            if (keys[i].code === code)
                return i
        }
        return -1
    }

    function flick(code, dx, dy) {
        const i = tc.indexOf(code)
        tc.kb.touchPress("key", i, 0, null)
        tc.kb.touchMove("key", i, dx, dy, null)
        tc.kb.touchLift("key", i, dx, dy, null)
    }

    function tap(code) {
        tc.flick(code, 0, 0)
    }

    function act(name) {
        return tc.kb.handleAction(name, false)
    }

    function test_type_and_delete() {
        tc.kb.open({ title: "playlist name" })
        const far = Ui.px(Ui.touchSlop) * 2
        tc.tap("2")
        tc.flick("2", -far, 0)
        tc.flick("2", 0, -far)
        tc.flick("2", 0, far)
        compare(tc.kb.entry.text, "abc2")
        tc.tap("delete")
        compare(tc.kb.entry.text, "abc")
        tc.flick("3", 0, -far)
        compare(tc.kb.entry.text, "abcf")
        tc.flick("2", far, 0)
        compare(tc.kb.entry.text, "abcf")
        tc.tap("space")
        compare(tc.kb.entry.text, "abcf ")
    }

    function test_petal_follows_the_finger() {
        tc.kb.open({ title: "name" })
        const far = Ui.px(Ui.touchSlop) * 2
        const i = tc.indexOf("7")
        tc.kb.touchPress("key", i, 0, null)
        compare(tc.kb.petal, "touch")
        compare(tc.kb.cells.length, 5)
        tc.kb.touchMove("key", i, -far, 0, null)
        compare(tc.kb.pick, "left")
        tc.kb.touchMove("key", i, 0, 0, null)
        compare(tc.kb.pick, "tap")
        tc.kb.touchCancel("key", i, null)
        compare(tc.kb.petal, "")
        compare(tc.kb.entry.text, "")
    }

    function test_petal_stays_on_the_grid() {
        tc.kb.open({ title: "name" })
        for (const code of ["1", "3", "7", "9", "0"]) {
            tc.kb.focusCode = code
            tc.act("a")
            const cells = tc.kb.cells
            verify(cells.length > 0)
            for (const c of cells) {
                verify(c.x >= -Ui.px(Ui.edge) - 0.5, code + " left")
                verify(c.x + c.w <= tc.kb.width + Ui.px(Ui.edge) + 0.5, code + " right")
                verify(c.y + tc.kb.panelTop + c.h <= tc.kb.height + Ui.px(Ui.edge) + 0.5, code + " bottom")
            }
            tc.act("b")
        }
    }

    function test_shift() {
        tc.kb.open({ title: "name" })
        tc.tap("shift")
        compare(tc.kb.entry.shift, 1)
        tc.tap("2")
        tc.tap("2")
        compare(tc.kb.entry.text, "Aa")
        tc.tap("shift")
        tc.tap("shift")
        compare(tc.kb.entry.shift, 2)
        tc.tap("2")
        tc.tap("2")
        compare(tc.kb.entry.text, "AaAA")
        tc.tap("shift")
        compare(tc.kb.entry.shift, 0)
        tc.kb.focusCode = "shift"
        tc.act("a")
        tc.act("a")
        compare(tc.kb.entry.shift, 2)
        tc.act("a")
        compare(tc.kb.entry.shift, 0)
    }

    function test_symbols_page() {
        tc.kb.open({ title: "name" })
        tc.tap("symbols")
        compare(tc.kb.entry.labels["symbols"], "abc")
        tc.tap("4")
        tc.tap("9")
        compare(tc.kb.entry.text, "+9")
    }

    function test_max_bytes() {
        tc.kb.open({ title: "name", text: "abcdef", max: 4 })
        compare(tc.kb.entry.text, "abcd")
        tc.tap("2")
        compare(tc.kb.entry.text, "abcd")
    }

    function test_number_filter() {
        tc.kb.open({ title: "gain", kind: "number", extras: "-.", decimals: 1, unit: "db", minValue: -24, maxValue: 12 })
        const codes = tc.kb.keys.map((k) => k.code)
        compare(codes.indexOf("shift"), -1)
        verify(codes.indexOf("minus") >= 0)
        verify(codes.indexOf("point") >= 0)
        tc.tap("3")
        tc.tap("minus")
        compare(tc.kb.entry.text, "3")
        tc.tap("delete")
        tc.tap("minus")
        tc.tap("3")
        tc.tap("point")
        tc.tap("point")
        tc.tap("5")
        tc.tap("5")
        compare(tc.kb.entry.text, "-3.5")
    }

    function test_number_needs_a_number() {
        tc.kb.open({ title: "gain", kind: "number", extras: "-." , decimals: 1 })
        tc.tap("minus")
        compare(tc.kb.entry.text, "-")
        compare(tc.kb.entry.ready, false)
        tc.tap("done")
        compare(tc.doneText.length, 0)
        tc.tap("point")
        compare(tc.kb.entry.ready, false)
        tc.tap("4")
        compare(tc.kb.entry.ready, true)
        tc.kb.open({ title: "frequency", kind: "number", extras: ".", decimals: 0 })
        tc.tap("point")
        compare(tc.kb.entry.text, "")
    }

    function test_title_elides_and_hides_under_a_toast_data() {
        return [{ tag: "portrait", w: 640, h: 960 }, { tag: "landscape", w: 960, h: 640 }]
    }

    function test_title_elides_and_hides_under_a_toast(data) {
        Ui.setSurface(data.w, data.h)
        tc.width = data.w
        tc.height = data.h
        tc.kb.open({ title: "a very long request title that does not fit in the field width at all" })
        const title = tc.kb.panel.children[0]
        compare(title.width, tc.kb.field.width)
        compare(title.elide, Text.ElideRight)
        verify(title.truncated)
        tc.kb.toastShown = true
        compare(title.visible, data.w < data.h)
        tc.kb.toastShown = false
        compare(title.visible, true)
        tc.width = 640
        tc.height = 960
    }

    function test_number_layout_reflows() {
        tc.kb.open({ title: "frequency", kind: "number", unit: "hz" })
        const zero = tc.kb.keys[tc.indexOf("0")]
        compare(zero.cols, 3)
        const done = tc.kb.keys[tc.indexOf("done")]
        compare(done.rows, 2)
        tc.kb.open({ title: "q", kind: "number", extras: ".", decimals: 2 })
        compare(tc.kb.keys[tc.indexOf("0")].cols, 2)
        compare(tc.kb.keys[tc.indexOf("point")].col, 2)
    }

    function test_done_refuses_out_of_range() {
        tc.kb.open({ title: "gain", kind: "number", extras: "-.", decimals: 1, unit: "db", minValue: -24, maxValue: 12 })
        tc.tap("done")
        compare(tc.doneText.length, 0)
        compare(tc.refusals.length, 0)
        tc.tap("minus")
        tc.tap("3")
        tc.tap("0")
        tc.tap("done")
        compare(tc.refusals, ["gain|-24 to 12 db"])
        verify(tc.kb.shown)
        compare(tc.kb.entry.text, "-30")
        tc.tap("delete")
        tc.act("start")
        compare(tc.doneText, ["-3"])
        verify(!tc.kb.shown)
    }

    function test_back_and_discard() {
        tc.kb.open({ title: "name", text: "mix" })
        tc.act("b")
        compare(tc.cancels, 1)
        verify(!tc.kb.shown)
        tc.kb.open({ title: "name", text: "mix" })
        tc.act("x")
        tc.act("b")
        verify(tc.kb.discard.shown)
        compare(tc.kb.discard.focusIndex, 1)
        verify(tc.kb.bare)
        tc.act("a")
        verify(!tc.kb.discard.shown)
        verify(tc.kb.shown)
        tc.tap("cancel")
        verify(tc.kb.discard.shown)
        tc.act("up")
        tc.act("a")
        compare(tc.cancels, 2)
        verify(!tc.kb.shown)
    }

    function test_pad_path_through_every_key() {
        tc.kb.open({ title: "name" })
        const seen = {}
        for (let row = 0; row < 4; row++) {
            for (let col = 0; col < 4; col++) {
                seen[tc.kb.focusCode] = true
                if (col < 3)
                    tc.act("right")
            }
            for (let col = 0; col < 3; col++)
                tc.act("left")
            tc.act("down")
        }
        compare(Object.keys(seen).length, 16)
        tc.act("down")
        compare(tc.kb.focusCode, "shift")
        tc.act("right")
        tc.act("right")
        tc.act("right")
        tc.act("right")
        compare(tc.kb.focusCode, "done")
    }

    function test_pad_types_through_the_petal() {
        tc.kb.open({ title: "name" })
        tc.act("down")
        tc.act("down")
        compare(tc.kb.focusCode, "7")
        tc.act("a")
        compare(tc.kb.petal, "pad")
        tc.act("right")
        compare(tc.kb.pick, "right")
        tc.act("left")
        compare(tc.kb.pick, "tap")
        tc.act("up")
        tc.act("a")
        compare(tc.kb.entry.text, "r")
        tc.act("a")
        tc.act("down")
        tc.act("a")
        compare(tc.kb.entry.text, "r7")
        tc.act("a")
        tc.act("b")
        verify(tc.kb.shown)
        compare(tc.kb.petal, "")
        tc.act("x")
        compare(tc.kb.entry.text, "r")
    }

    function test_pad_number_spanning_keys() {
        tc.kb.open({ title: "frequency", kind: "number", unit: "hz" })
        tc.kb.focusCode = "9"
        tc.act("right")
        compare(tc.kb.focusCode, "done")
        tc.act("down")
        compare(tc.kb.focusCode, "done")
        tc.act("left")
        compare(tc.kb.focusCode, "9")
        tc.act("down")
        compare(tc.kb.focusCode, "0")
        tc.act("right")
        compare(tc.kb.focusCode, "done")
        tc.kb.focusCode = "5"
        tc.act("a")
        compare(tc.kb.entry.text, "5")
    }

    function test_keys_fit_both_orientations_data() {
        return [{ tag: "portrait", w: 640, h: 960 }, { tag: "landscape", w: 960, h: 640 }]
    }

    function test_keys_fit_both_orientations(data) {
        Ui.setSurface(data.w, data.h)
        tc.width = data.w
        tc.height = data.h
        tc.kb.open({ title: "name" })
        compare(tc.kb.key, Ui.px(70.5))
        const last = tc.kb.rectOf(tc.kb.keys[tc.indexOf("done")])
        compare(tc.kb.panelTop + last.y + last.h, data.h - Ui.px(Ui.edge))
        compare(last.x + last.w, data.w - Ui.px(Ui.edge))
        verify(tc.kb.field.y + tc.kb.field.height <= tc.kb.padY || data.w > data.h)
        verify(tc.kb.field.x + tc.kb.field.width <= tc.kb.padX || data.w < data.h)
        tc.width = 640
        tc.height = 960
    }
}
