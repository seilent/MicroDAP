import QtQuick
import QtTest
import Casdap

TestCase {
    id: tc

    name: "EqScreen"
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
        pad: null
    }

    QtObject {
        id: fakeEq

        property string activeOutput: "headphones"
        property var sources: [{ name: "HD 600", path: "/eq/HD 600.txt" }, { name: "Gone", path: "/eq/Gone.txt" }]
        property var keys: ({})
        property var missing: ({})
        property var preamps: ({})
        property var bandSets: ({})
        property var calls: []
        readonly property var cycle: ["off", "/eq/HD 600.txt", "/eq/Gone.txt", "custom"]
        readonly property var types: ["off", "peak", "lowshelf", "highshelf"]

        signal profilesChanged()

        function flat() {
            const out = []
            for (let i = 0; i < 10; i++)
                out.push({ type: "peak", freq: 1000, gain: 0, q: 1.4 })
            return out
        }

        function reset() {
            fakeEq.activeOutput = "headphones"
            fakeEq.keys = ({ headphones: "/eq/HD 600.txt", usb: "off", speaker: "off" })
            fakeEq.missing = ({ headphones: false, usb: false, speaker: false })
            fakeEq.preamps = ({ headphones: -6.5, usb: 0, speaker: 0 })
            const hd = fakeEq.flat()
            hd[0] = { type: "lowshelf", freq: 105, gain: 2.1, q: 0.7 }
            hd[1] = { type: "peak", freq: 2500, gain: -3, q: 1.4 }
            hd[2] = { type: "off", freq: 1000, gain: 0, q: 1.4 }
            fakeEq.bandSets = ({ headphones: hd, usb: fakeEq.flat(), speaker: fakeEq.flat() })
            fakeEq.calls = []
        }

        function profileMap(output) {
            const key = fakeEq.keys[output]
            const name = key === "off" || key === "custom" ? "" : key.substring(key.lastIndexOf("/") + 1).replace(".txt", "")
            return { key: key, name: name, missing: fakeEq.missing[output] === true, preamp: fakeEq.preamps[output], bands: fakeEq.bandSets[output] }
        }

        function makeCustom(output) {
            const keys = Object.assign({}, fakeEq.keys)
            keys[output] = "custom"
            fakeEq.keys = keys
        }

        function cycleProfile(output, delta) {
            fakeEq.calls = fakeEq.calls.concat([["cycleProfile", output, delta]])
            const n = fakeEq.cycle.length
            const at = Math.max(0, fakeEq.cycle.indexOf(fakeEq.keys[output]))
            const next = Object.assign({}, fakeEq.keys)
            next[output] = fakeEq.cycle[((at + delta) % n + n) % n]
            fakeEq.keys = next
            const gone = Object.assign({}, fakeEq.missing)
            gone[output] = false
            fakeEq.missing = gone
            fakeEq.profilesChanged()
        }

        function stepPreamp(output, delta, coarse) {
            fakeEq.calls = fakeEq.calls.concat([["stepPreamp", output, delta, coarse]])
            const next = Object.assign({}, fakeEq.preamps)
            const value = next[output] + delta * (coarse ? 1 : 0.1)
            next[output] = Math.max(-24, Math.min(0, Math.round(value * 10) / 10))
            fakeEq.preamps = next
            fakeEq.makeCustom(output)
            fakeEq.profilesChanged()
        }

        function stepBand(output, band, field, delta, coarse) {
            fakeEq.calls = fakeEq.calls.concat([["stepBand", output, band, field, delta, coarse]])
            const sets = Object.assign({}, fakeEq.bandSets)
            const list = sets[output].slice()
            const b = Object.assign({}, list[band])
            if (field === "gain") {
                b.gain = Math.round((b.gain + delta * (coarse ? 1 : 0.1)) * 10) / 10
            } else if (field === "q") {
                b.q = Math.round((b.q + delta * (coarse ? 1 : 0.1)) * 100) / 100
            } else if (field === "freq") {
                b.freq = Math.round(b.freq * Math.pow(2, delta * (coarse ? 6 : 1) / 6))
            } else {
                const n = fakeEq.types.length
                const at = Math.max(0, fakeEq.types.indexOf(b.type))
                b.type = fakeEq.types[((at + delta) % n + n) % n]
            }
            list[band] = b
            sets[output] = list
            fakeEq.bandSets = sets
            fakeEq.makeCustom(output)
            fakeEq.profilesChanged()
        }
    }

    Component {
        id: eqComponent

        EqScreen {}
    }

    Component {
        id: blankComponent

        Page {}
    }

    function setWindow(w, h) {
        Ui.userScale = 1.0
        Ui.setSurface(w, h)
        pages.width = w
        pages.height = h
        router.width = w
        router.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        pages.clear()
        pages.animate = false
        fakeEq.reset()
    }

    function open(properties) {
        const props = properties === undefined ? {} : properties
        if (props.animated === undefined)
            props.animated = false
        if (props.eq === undefined)
            props.eq = fakeEq
        pages.push(blankComponent, {})
        const page = pages.push(eqComponent, props)
        page.completeAll()
        return page
    }

    function rowItem(pane, index) {
        tryVerify(() => pane.rowItem(index) !== null, 1000)
        return pane.rowItem(index)
    }

    function keys(page) {
        return page.rows.map((r) => r.key)
    }

    function callAt(index) {
        return JSON.stringify(fakeEq.calls[index])
    }

    function edit(page, index) {
        page.setFocus(index)
        verify(router.dispatch("a", false))
        compare(page.editing, true)
        compare(page.editKey, page.rows[index].key)
    }

    function test_layout_and_rows() {
        const page = open()
        compare(page.pageId, 0)
        compare(page.header.text, "eq")
        compare(JSON.stringify(page.pivots), JSON.stringify(["headphones", "usb", "speaker"]))
        compare(page.pivotIndex, 0)
        compare(page.headphonesPane.y, Ui.px(128))
        compare(page.rows.length, 12)
        compare(page.rows[0].title, "profile")
        compare(page.rows[0].value, "HD 600")
        compare(page.rows[1].title, "preamp")
        compare(page.rows[1].value, "-6.5 db")
        compare(page.rows[2].title, "band 1")
        compare(page.rows[2].value, "ls 105 hz +2.1 db q 0.70")
        compare(page.rows[3].value, "pk 2.5 khz -3.0 db q 1.40")
        compare(page.rows[4].value, "off")
        compare(page.rows[11].title, "band 10")
        const first = tc.rowItem(page.headphonesPane, 0)
        compare(first.height, Ui.px(43))
        compare(first.titleItem.x, Ui.px(20))
        compare(first.titleItem.font.pixelSize, Ui.fontPx(17))
        compare(first.titleItem.font.styleName, "Bold")
        compare(first.titleItem.font.capitalization, Font.AllUppercase)
        compare(first.valueItem.font.pixelSize, Ui.fontPx(11))
        compare(first.valueItem.font.styleName, "Light")
        compare(first.valueItem.font.capitalization, Font.AllUppercase)
        const slot = first.valueSlot
        fuzzyCompare(slot.x, first.titleItem.x + first.titleItem.implicitWidth + Ui.px(8), 0.01)
        fuzzyCompare(slot.x + slot.width, page.width - Ui.px(20), 0.01)
        fuzzyCompare(slot.x + first.valueItem.x + first.valueItem.width, page.width - Ui.px(20), 0.01)
        compare(slot.clip, true)
        fuzzyCompare(first.titleItem.color.a, 1.0, 1 / 255)
        fuzzyCompare(first.valueItem.color.a, 0.85, 1 / 255)
        const second = tc.rowItem(page.headphonesPane, 1)
        fuzzyCompare(second.titleItem.color.a, 0.6, 1 / 255)
        fuzzyCompare(second.valueItem.color.a, 0.4, 1 / 255)
        compare(page.focusBar.x, Ui.px(12))
        fuzzyCompare(page.focusBar.y, Ui.px(128), 0.01)
        compare(page.focusBar.rowHeight, Ui.px(43))
        compare(page.editing, false)
        tc.setWindow(1920, 1080)
        compare(page.headphonesPane.y, Ui.px(98))
    }

    function test_initial_pivot_follows_output() {
        fakeEq.activeOutput = "speaker"
        const page = open()
        compare(page.pivotIndex, 2)
        compare(page.shownPivot, 2)
        compare(page.rows.length, 1)
        compare(page.rows[0].value, "off")
        fakeEq.activeOutput = "usb"
        compare(page.pivotIndex, 2)
    }

    function test_profile_cycles() {
        const page = open()
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        compare(fakeEq.calls.length, 0)
        tc.edit(page, 0)
        verify(router.dispatch("right", false))
        compare(tc.callAt(0), JSON.stringify(["cycleProfile", "headphones", 1]))
        compare(page.rows[0].value, "Gone")
        compare(page.rows.length, 12)
        verify(router.dispatch("right", false))
        compare(page.rows[0].value, "custom")
        verify(router.dispatch("right", false))
        compare(page.rows[0].value, "off")
        compare(page.rows.length, 1)
        compare(page.editing, true)
        verify(router.dispatch("left", false))
        compare(tc.callAt(3), JSON.stringify(["cycleProfile", "headphones", -1]))
        compare(page.rows[0].value, "custom")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.rows[0].value, "custom")
        compare(fakeEq.calls.length, 4)
    }

    function test_profile_cancel_restores() {
        const page = open()
        tc.edit(page, 0)
        router.dispatch("right", false)
        router.dispatch("right", false)
        router.dispatch("right", false)
        compare(page.rows[0].value, "off")
        verify(router.dispatch("b", false))
        compare(page.editing, false)
        compare(pages.depth, 2)
        compare(fakeEq.keys.headphones, "/eq/HD 600.txt")
        compare(page.rows[0].value, "HD 600")
        compare(page.rows.length, 12)
        compare(fakeEq.calls.length, 6)
        for (let i = 3; i < 6; i++)
            compare(tc.callAt(i), JSON.stringify(["cycleProfile", "headphones", -1]))
    }

    function test_profile_coarse_inert() {
        const page = open()
        tc.edit(page, 0)
        verify(router.dispatch("rb", false))
        verify(router.dispatch("lb", false))
        compare(fakeEq.calls.length, 0)
        compare(page.pivotIndex, 0)
        compare(page.editing, true)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
    }

    function test_preamp_and_band_rows() {
        const page = open()
        verify(router.dispatch("down", false))
        compare(page.focusIndex, 1)
        compare(router.dispatch("left", false), false)
        compare(fakeEq.calls.length, 0)
        verify(router.dispatch("a", false))
        compare(page.editing, true)
        compare(page.editKey, "preamp")
        verify(router.dispatch("left", false))
        compare(tc.callAt(0), JSON.stringify(["stepPreamp", "headphones", -1, false]))
        compare(page.rows[1].value, "-6.6 db")
        compare(page.rows[0].value, "custom")
        verify(router.dispatch("lb", false))
        compare(tc.callAt(1), JSON.stringify(["stepPreamp", "headphones", -1, true]))
        compare(page.rows[1].value, "-7.6 db")
        verify(router.dispatch("rb", false))
        compare(tc.callAt(2), JSON.stringify(["stepPreamp", "headphones", 1, true]))
        compare(page.rows[1].value, "-6.6 db")
        compare(page.pivotIndex, 0)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.rows[1].value, "-6.6 db")
        verify(router.dispatch("down", false))
        compare(router.dispatch("right", false), false)
        compare(router.dispatch("left", false), false)
        compare(fakeEq.calls.length, 3)
        compare(router.dispatch("x", false), true)
        compare(router.dispatch("y", false), true)
        compare(router.dispatch("back", false), true)
        compare(pages.depth, 2)
        compare(fakeEq.calls.length, 3)
    }

    function test_preamp_cancel_restores() {
        const page = open()
        tc.edit(page, 1)
        router.dispatch("left", false)
        router.dispatch("lb", false)
        compare(page.rows[1].value, "-7.6 db")
        compare(page.rows[0].value, "custom")
        verify(router.dispatch("b", false))
        compare(page.editing, false)
        compare(pages.depth, 2)
        compare(fakeEq.preamps.headphones, -6.5)
        compare(fakeEq.keys.headphones, "/eq/HD 600.txt")
        compare(page.rows[0].value, "HD 600")
        compare(page.rows[1].value, "-6.5 db")
        compare(page.focusIndex, 1)
        compare(fakeEq.calls.length, 6)
        compare(tc.callAt(2), JSON.stringify(["stepPreamp", "headphones", 1, true]))
        compare(tc.callAt(3), JSON.stringify(["stepPreamp", "headphones", 1, false]))
        compare(tc.callAt(4), JSON.stringify(["cycleProfile", "headphones", -1]))
        compare(tc.callAt(5), JSON.stringify(["cycleProfile", "headphones", -1]))
    }

    function test_sub_rows_open_and_close() {
        const page = open()
        page.setFocus(2)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.headphonesPane.openBand, 0)
        compare(JSON.stringify(tc.keys(page).slice(2, 7)), JSON.stringify(["band0", "band0:type", "band0:freq", "band0:gain", "band0:q"]))
        compare(page.rows[3].title, "type")
        compare(page.rows[3].value, "low shelf")
        compare(page.rows[4].title, "frequency")
        compare(page.rows[4].value, "105 hz")
        compare(page.rows[5].title, "gain")
        compare(page.rows[5].value, "+2.1 db")
        compare(page.rows[6].title, "q")
        compare(page.rows[6].value, "0.70")
        compare(page.rows.length, 16)
        compare(page.focusIndex, 2)
        const sub = tc.rowItem(page.headphonesPane, 3)
        compare(sub.sub, true)
        compare(sub.height, Ui.px(43))
        compare(sub.titleItem.x, Ui.px(20))
        compare(sub.titleItem.font.pixelSize, Ui.fontPx(11))
        compare(sub.titleItem.font.styleName, "Light")
        compare(sub.titleItem.font.capitalization, Font.AllUppercase)
        router.dispatch("down", false)
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.focusIndex, 5)
        compare(router.dispatch("right", false), false)
        compare(fakeEq.calls.length, 0)
        verify(router.dispatch("a", false))
        compare(page.editing, true)
        verify(router.dispatch("right", false))
        compare(tc.callAt(0), JSON.stringify(["stepBand", "headphones", 0, "gain", 1, false]))
        compare(page.rows[5].value, "+2.2 db")
        compare(page.rows[2].value, "ls 105 hz +2.2 db q 0.70")
        verify(router.dispatch("rb", false))
        compare(tc.callAt(1), JSON.stringify(["stepBand", "headphones", 0, "gain", 1, true]))
        compare(page.rows[5].value, "+3.2 db")
        compare(page.pivotIndex, 0)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.rows[5].value, "+3.2 db")
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.focusIndex, 7)
        compare(page.rows[7].key, "band1")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.headphonesPane.openBand, 1)
        compare(page.focusIndex, 3)
        compare(page.rows[3].key, "band1")
        compare(page.rows[4].key, "band1:type")
        compare(page.rows.length, 16)
        verify(router.dispatch("a", false))
        compare(page.headphonesPane.openBand, -1)
        compare(page.focusIndex, 3)
        compare(page.rows.length, 12)
    }

    function test_sub_rows_coarse_steps() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        const fields = ["type", "freq", "gain", "q"]
        for (let f = 0; f < fields.length; f++) {
            tc.edit(page, 3 + f)
            verify(router.dispatch("rb", false))
            verify(router.dispatch("lb", false))
            verify(router.dispatch("a", false))
            compare(page.editing, false)
            compare(tc.callAt(f * 2), JSON.stringify(["stepBand", "headphones", 0, fields[f], 1, true]))
            compare(tc.callAt(f * 2 + 1), JSON.stringify(["stepBand", "headphones", 0, fields[f], -1, true]))
        }
        compare(page.pivotIndex, 0)
        compare(page.headphonesPane.openBand, 0)
        tc.edit(page, 4)
        router.dispatch("rb", false)
        compare(page.rows[4].value, "210 hz")
        router.dispatch("right", false)
        compare(page.rows[4].value, "236 hz")
        router.dispatch("a", false)
        tc.edit(page, 6)
        router.dispatch("rb", false)
        compare(page.rows[6].value, "1.70")
        router.dispatch("left", false)
        compare(page.rows[6].value, "1.60")
        router.dispatch("a", false)
    }

    function test_sub_row_cancel_restores() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        tc.edit(page, 3)
        router.dispatch("right", false)
        compare(page.rows[3].value, "high shelf")
        router.dispatch("right", false)
        compare(page.rows[3].value, "off")
        compare(page.rows[2].value, "off")
        verify(router.dispatch("b", false))
        compare(page.editing, false)
        compare(pages.depth, 2)
        compare(page.rows[3].value, "low shelf")
        compare(page.rows[2].value, "ls 105 hz +2.1 db q 0.70")
        compare(fakeEq.keys.headphones, "/eq/HD 600.txt")
        compare(page.headphonesPane.openBand, 0)
        compare(page.focusIndex, 3)
        compare(page.rows.length, 16)
        tc.edit(page, 5)
        router.dispatch("rb", false)
        router.dispatch("rb", false)
        router.dispatch("left", false)
        compare(page.rows[5].value, "+4.0 db")
        verify(router.dispatch("b", false))
        compare(page.rows[5].value, "+2.1 db")
        compare(fakeEq.bandSets.headphones[0].gain, 2.1)
        compare(fakeEq.keys.headphones, "/eq/HD 600.txt")
        compare(page.focusIndex, 5)
    }

    function test_edit_keys_ignored() {
        const page = open()
        tc.edit(page, 1)
        for (const name of ["up", "down", "lt", "rt", "x", "y", "start", "back"]) {
            verify(router.dispatch(name, false))
            compare(page.focusIndex, 1)
            compare(page.editing, true)
        }
        compare(page.pivotIndex, 0)
        compare(fakeEq.calls.length, 0)
        compare(pages.depth, 2)
        verify(router.dispatch("a", true))
        compare(page.editing, true)
        verify(router.dispatch("b", true))
        compare(page.editing, true)
        verify(router.dispatch("b", false))
        compare(page.editing, false)
        compare(pages.depth, 2)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_edit_value_color() {
        const page = open()
        const row = tc.rowItem(page.headphonesPane, 1)
        tc.edit(page, 1)
        compare(row.editing, true)
        verify(Qt.colorEqual(row.valueItem.color, Ui.accent))
        fuzzyCompare(row.titleItem.color.a, 1.0, 1 / 255)
        compare(tc.rowItem(page.headphonesPane, 0).editing, false)
        router.dispatch("a", false)
        compare(row.editing, false)
        fuzzyCompare(row.valueItem.color.a, 0.85, 1 / 255)
        verify(!Qt.colorEqual(row.valueItem.color, Ui.accent))
    }

    function test_edit_confirmed_on_screen_off_and_leave() {
        const page = open()
        tc.edit(page, 1)
        router.dispatch("left", false)
        page.screenOff = true
        compare(page.editing, false)
        compare(fakeEq.preamps.headphones, -6.6)
        page.screenOff = false
        tc.edit(page, 1)
        router.dispatch("left", false)
        pages.push(blankComponent, {})
        compare(page.editing, false)
        compare(fakeEq.preamps.headphones, -6.7)
    }

    function test_pivot_switch() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        compare(page.headphonesPane.openBand, 0)
        compare(router.dispatch("lb", false), true)
        compare(page.pivotIndex, 0)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 1)
        compare(page.shownPivot, 1)
        compare(page.focusIndex, 0)
        compare(page.headphonesPane.openBand, -1)
        compare(page.rows.length, 1)
        compare(page.rows[0].value, "off")
        tc.edit(page, 0)
        verify(router.dispatch("right", false))
        compare(tc.callAt(0), JSON.stringify(["cycleProfile", "usb", 1]))
        compare(page.rows.length, 12)
        router.dispatch("a", false)
        router.dispatch("rb", false)
        compare(page.pivotIndex, 2)
        compare(router.dispatch("rb", false), true)
        compare(page.pivotIndex, 2)
        router.dispatch("lb", false)
        router.dispatch("lb", false)
        compare(page.pivotIndex, 0)
        compare(page.rows.length, 12)
    }

    function test_pivot_motion_and_screen_off() {
        const page = open({ animated: true })
        router.dispatch("rb", false)
        verify(page.switching)
        compare(page.shownPivot, 0)
        tryCompare(page, "shownPivot", 1, 2000)
        const row = tc.rowItem(page.usbPane, 0)
        compare(row.shift, 34)
        compare(row.enterDuration, 380)
        tryCompare(row, "enter", 1, 2000)
        tryCompare(page, "switching", false, 2000)
        compare(page.usbPane.x, 0)
        router.dispatch("lb", false)
        verify(page.switching)
        page.screenOff = true
        compare(page.switching, false)
        compare(page.shownPivot, 0)
        compare(page.headphonesPane.x, 0)
        compare(page.headphonesPane.opacity, 1)
    }

    function test_missing_profile_alone() {
        const gone = Object.assign({}, fakeEq.missing)
        gone.headphones = true
        fakeEq.missing = gone
        const page = open()
        compare(page.rows.length, 1)
        compare(page.rows[0].title, "profile")
        compare(page.rows[0].value, "HD 600 (missing)")
        compare(router.dispatch("down", false), false)
        const back = Object.assign({}, fakeEq.missing)
        back.headphones = false
        fakeEq.missing = back
        fakeEq.profilesChanged()
        compare(page.rows.length, 12)
        compare(page.rows[0].value, "HD 600")
    }

    function test_null_eq() {
        const page = open({ eq: null })
        compare(page.pivotIndex, 0)
        compare(page.rows.length, 1)
        compare(page.rows[0].value, "off")
        compare(router.dispatch("right", false), false)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        verify(router.dispatch("rb", false))
        compare(page.pivotIndex, 1)
        compare(page.rows.length, 1)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_b_pops_with_sub_rows_open() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        compare(page.headphonesPane.openBand, 0)
        verify(router.dispatch("b", false))
        compare(pages.depth, 1)
    }

    function test_start_falls_through() {
        const page = open()
        compare(page.handleAction("start", false), false)
    }

    function test_page_lt_rt() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        verify(router.dispatch("rt", false))
        verify(page.focusIndex > 2)
        page.headphonesPane.completeScroll()
        const top = page.focusIndex * Ui.px(43) - page.headphonesPane.contentY
        verify(top >= 0)
        verify(top + Ui.px(43) <= page.headphonesPane.height + 0.01)
        verify(router.dispatch("lt", false))
        verify(router.dispatch("lt", false))
        compare(page.focusIndex, 0)
        compare(router.dispatch("lt", false), false)
    }

    function test_f7_value_swap() {
        const page = open({ animated: true })
        router.dispatch("down", false)
        router.dispatch("a", false)
        router.dispatch("left", false)
        const preamp = tc.rowItem(page.headphonesPane, 1)
        const profile = tc.rowItem(page.headphonesPane, 0)
        compare(preamp.valueSwapping, true)
        compare(profile.valueSwapping, true)
        compare(preamp.shownValue, "-6.5 db")
        tryCompare(preamp, "shownValue", "-6.6 db", 1000)
        tryCompare(profile, "shownValue", "custom", 1000)
        tryCompare(preamp, "valueSwapping", false, 1000)
        compare(preamp.valueItem.opacity, 1)
    }

    function test_f12_on_sub_rows_and_listing() {
        const page = open({ animated: true })
        tryVerify(function() { return page.headphonesPane.pendingFrom < 0 }, 1000)
        tryCompare(tc.rowItem(page.headphonesPane, 2), "enter", 1, 2000)
        page.setFocus(2)
        router.dispatch("a", false)
        const sub = tc.rowItem(page.headphonesPane, 3)
        tryVerify(function() { return sub.enter < 1 }, 1000)
        compare(sub.enterDuration, 220)
        compare(sub.shift, Motion.listChangeShift)
        compare(sub.stagger, 0)
        compare(tc.rowItem(page.headphonesPane, 4).stagger, 50)
        compare(tc.rowItem(page.headphonesPane, 2).enter, 1)
        tryCompare(sub, "enter", 1, 2000)
        router.dispatch("a", false)
        compare(page.rows.length, 12)
        compare(tc.rowItem(page.headphonesPane, 3).enter, 1)
        page.setFocus(0)
        router.dispatch("a", false)
        compare(page.editing, true)
        router.dispatch("right", false)
        router.dispatch("right", false)
        router.dispatch("right", false)
        compare(page.rows.length, 1)
        router.dispatch("right", false)
        compare(page.rows.length, 12)
        tryVerify(function() {
            const r = page.headphonesPane.rowItem(1)
            return r !== null && r.enter < 1
        }, 1000)
        compare(tc.rowItem(page.headphonesPane, 1).enterDuration, 220)
        tryCompare(tc.rowItem(page.headphonesPane, 1), "enter", 1, 2000)
        router.dispatch("a", false)
        compare(page.editing, false)
    }

    function test_t2a_first_show() {
        pages.animate = true
        pages.push(blankComponent, {})
        const page = pages.push(eqComponent, { animated: true, eq: fakeEq })
        compare(page.arriving, true)
        const first = tc.rowItem(page.headphonesPane, 0)
        compare(first.enter, 0)
        compare(first.shift, Motion.pivotShift)
        fuzzyCompare(first.x, Ui.px(34), 0.01)
        compare(first.opacity, 0)
        tryCompare(pages, "phase", "idle", 3000)
        compare(page.arriving, false)
        compare(first.enterDuration, 380)
        compare(first.stagger, 0)
        compare(tc.rowItem(page.headphonesPane, 3).stagger, 3 * 22)
        tryCompare(first, "enter", 1, 2000)
        tryCompare(tc.rowItem(page.headphonesPane, 3), "enter", 1, 2000)
        fuzzyCompare(first.x, 0, 0.01)
    }

    function test_screen_off_lands_rows() {
        const page = open({ animated: true })
        page.setFocus(2)
        router.dispatch("a", false)
        page.screenOff = true
        compare(tc.rowItem(page.headphonesPane, 3).enter, 1)
    }
}
