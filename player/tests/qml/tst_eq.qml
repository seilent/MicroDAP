import QtQuick
import QtTest
import MicroDap

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

    TouchLayer {
        id: layer

        width: 640
        height: 960
        router: router
    }

    QtObject {
        id: fakeEq

        property string activeOutput: "headphones"
        property var sources: [{ name: "HD 600", path: "/eq/HD 600.txt" }, { name: "Gone", path: "/eq/Gone.txt" }]
        property var keys: ({})
        property var missing: ({})
        property var preamps: ({})
        property var bandSets: ({})
        property var savedKeys: []
        property bool failNext: false
        property var calls: []
        readonly property var cycle: ["off", "/eq/HD 600.txt", "/eq/Gone.txt", "custom"]
        readonly property var types: ["off", "peak", "lowshelf", "highshelf", "bandpass", "lowpass", "highpass", "allpass"]

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
            fakeEq.savedKeys = []
            fakeEq.failNext = false
        }

        function savePreset(output) {
            fakeEq.calls = fakeEq.calls.concat([["savePreset", output]])
            if (fakeEq.failNext)
                return "too many presets"
            const keys = Object.assign({}, fakeEq.keys)
            keys[output] = "/eq/presets/Preset 1.txt"
            fakeEq.keys = keys
            fakeEq.savedKeys = fakeEq.savedKeys.concat([keys[output]])
            fakeEq.profilesChanged()
            return ""
        }

        function deletePreset(output) {
            fakeEq.calls = fakeEq.calls.concat([["deletePreset", output]])
            if (fakeEq.failNext)
                return false
            const keys = Object.assign({}, fakeEq.keys)
            keys[output] = "off"
            fakeEq.keys = keys
            fakeEq.profilesChanged()
            return true
        }

        function presetName(output) {
            return "Preset 1"
        }

        function renameRefusal(output, name) {
            return name === "Taken" ? "name taken" : ""
        }

        function renamePreset(output, name) {
            fakeEq.calls = fakeEq.calls.concat([["renamePreset", output, name]])
            if (fakeEq.failNext)
                return false
            const keys = Object.assign({}, fakeEq.keys)
            keys[output] = "/eq/presets/" + name + ".txt"
            fakeEq.keys = keys
            fakeEq.savedKeys = fakeEq.savedKeys.concat([keys[output]])
            fakeEq.profilesChanged()
            return true
        }

        function profileMap(output) {
            const key = fakeEq.keys[output]
            const name = key === "off" || key === "custom" ? "" : key.substring(key.lastIndexOf("/") + 1).replace(".txt", "")
            return { key: key, name: name, missing: fakeEq.missing[output] === true, preamp: fakeEq.preamps[output], bands: fakeEq.bandSets[output],
                saved: fakeEq.savedKeys.indexOf(key) >= 0 }
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

        function setPreamp(output, value) {
            fakeEq.calls = fakeEq.calls.concat([["setPreamp", output, value]])
            const next = Object.assign({}, fakeEq.preamps)
            next[output] = value
            fakeEq.preamps = next
            fakeEq.makeCustom(output)
            fakeEq.profilesChanged()
            return true
        }

        function setBandValue(output, band, field, value) {
            fakeEq.calls = fakeEq.calls.concat([["setBandValue", output, band, field, value]])
            const sets = Object.assign({}, fakeEq.bandSets)
            const list = sets[output].slice()
            const b = Object.assign({}, list[band])
            b[field] = value
            if (field === "freq")
                b.freqIndex = -1
            list[band] = b
            sets[output] = list
            fakeEq.bandSets = sets
            fakeEq.makeCustom(output)
            fakeEq.profilesChanged()
            return true
        }

        function addBand(output) {
            fakeEq.calls = fakeEq.calls.concat([["addBand", output]])
            const sets = Object.assign({}, fakeEq.bandSets)
            if (fakeEq.keys[output] === "off" || sets[output].length >= 10)
                return false
            sets[output] = sets[output].concat([{ type: "peak", freq: 1000, gain: 0, q: 1.4 }])
            fakeEq.bandSets = sets
            fakeEq.makeCustom(output)
            fakeEq.profilesChanged()
            return true
        }

        function removeBand(output) {
            fakeEq.calls = fakeEq.calls.concat([["removeBand", output]])
            const sets = Object.assign({}, fakeEq.bandSets)
            if (fakeEq.keys[output] === "off" || sets[output].length <= 1)
                return false
            sets[output] = sets[output].slice(0, sets[output].length - 1)
            fakeEq.bandSets = sets
            fakeEq.makeCustom(output)
            fakeEq.profilesChanged()
            return true
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

    QtObject {
        id: fakeDacEq

        property string status: "on"
        readonly property var ranges: ({ gain: [-12, 12], q: [0.25, 8], preamp: [0, 0] })
    }

    Keyboard {
        id: kb

        width: 640
        height: 960
        animated: false
    }

    QtObject {
        id: toasts

        property var shown: []
    }

    Component {
        id: eqComponent

        EqScreen {
            keyboard: kb
            onToast: (first, second) => toasts.shown = toasts.shown.concat([[first, second]])
        }
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
        layer.width = w
        layer.height = h
    }

    function init() {
        tc.setWindow(640, 960)
        pages.clear()
        pages.animate = false
        fakeEq.reset()
        fakeDacEq.status = "on"
        toasts.shown = []
        kb.close()
        kb.complete()
        kb.validator = null
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

    function at(page, key) {
        return page.indexOfKey(key)
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

    function test_type_value_preamp() {
        const page = open()
        page.setFocus(tc.at(page, "preamp"))
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "preamp")
        compare(JSON.stringify(page.sheet.items), JSON.stringify(["type value"]))
        page.sheet.complete()
        verify(router.dispatch("a", false))
        page.sheet.complete()
        compare(page.sheet.shown, false)
        compare(kb.shown, true)
        compare(kb.entry.title, "preamp")
        compare(kb.entry.number, true)
        compare(kb.entry.text, "-6.5")
        compare(kb.entry.unit, "db")
        for (let i = 0; i < 4; i++)
            kb.entry.erase()
        kb.entry.type("3")
        kb.finish()
        compare(kb.shown, true)
        compare(fakeEq.calls.length, 0)
        kb.entry.erase()
        for (const ch of "-3.25")
            kb.entry.type(ch)
        compare(kb.entry.text, "-3.2")
        kb.finish()
        compare(kb.shown, false)
        compare(tc.callAt(0), JSON.stringify(["setPreamp", "headphones", -3.2]))
        compare(page.rows[tc.at(page, "preamp")].value, "-3.2 db")
        compare(page.editing, false)
    }

    function test_type_value_band_fields() {
        const page = open()
        page.setFocus(2)
        verify(router.dispatch("a", false))
        page.setFocus(tc.at(page, "band0:type"))
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, false)
        page.setFocus(tc.at(page, "profile"))
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, false)
        page.setFocus(tc.at(page, "band0:freq"))
        verify(router.dispatch("x", false))
        page.sheet.complete()
        verify(router.dispatch("a", false))
        compare(kb.entry.title, "frequency")
        compare(kb.entry.text, "105")
        compare(kb.entry.type("."), false)
        kb.entry.erase()
        kb.entry.erase()
        kb.entry.erase()
        for (const ch of "1234")
            kb.entry.type(ch)
        kb.finish()
        compare(tc.callAt(0), JSON.stringify(["setBandValue", "headphones", 0, "freq", 1234]))
        compare(page.rows[tc.at(page, "band0:freq")].value, "1234 hz")
        compare(page.rows[tc.at(page, "band0")].value, "ls 1234 hz +2.1 db q 0.70")
        page.setFocus(tc.at(page, "band0:q"))
        verify(router.dispatch("x", false))
        page.sheet.complete()
        verify(router.dispatch("a", false))
        compare(kb.entry.text, "0.70")
        compare(kb.entry.type("1"), false)
        kb.back()
        kb.complete()
        compare(kb.shown, false)
        compare(fakeEq.calls.length, 1)
        tc.edit(page, tc.at(page, "band0:gain"))
        verify(router.dispatch("x", false))
        compare(page.sheet.shown, false)
        verify(router.dispatch("a", false))
    }

    function test_type_value_long_press() {
        const page = open()
        const item = tc.rowItem(page.headphonesPane, tc.at(page, "preamp"))
        mousePress(item, item.width / 2, item.height / 2)
        wait(Ui.seekHoldMs + 150)
        mouseRelease(item, item.width / 2, item.height / 2)
        wait(0)
        wait(0)
        compare(page.editing, false)
        compare(page.sheet.shown, true)
        compare(page.sheet.title, "preamp")
        page.sheet.close()
        page.sheet.complete()
        const profile = tc.rowItem(page.headphonesPane, 0)
        mousePress(profile, profile.width / 2, profile.height / 2)
        wait(Ui.seekHoldMs + 150)
        mouseRelease(profile, profile.width / 2, profile.height / 2)
        wait(0)
        wait(0)
        compare(page.sheet.shown, false)
    }

    function test_off_grid_frequency_text() {
        const sets = Object.assign({}, fakeEq.bandSets)
        const list = sets.headphones.slice()
        list[0] = { type: "peak", freq: 1450, freqIndex: -1, gain: 1, q: 1 }
        list[1] = { type: "peak", freq: 2500, freqIndex: -1, gain: 1, q: 1 }
        list[2] = { type: "peak", freq: 1414.2, freqIndex: 37, gain: 1, q: 1 }
        sets.headphones = list
        fakeEq.bandSets = sets
        const page = open()
        compare(page.rows[tc.at(page, "band0")].value, "pk 1450 hz +1.0 db q 1.00")
        compare(page.rows[tc.at(page, "band1")].value, "pk 2.5 khz +1.0 db q 1.00")
        compare(page.rows[tc.at(page, "band2")].value, "pk 1.4 khz +1.0 db q 1.00")
    }

    function test_preset_rows() {
        const page = open()
        compare(tc.keys(page).indexOf("savePreset"), -1)
        compare(tc.keys(page).indexOf("deletePreset"), -1)
        fakeEq.makeCustom("headphones")
        fakeEq.profilesChanged()
        compare(JSON.stringify(tc.keys(page).slice(0, 3)), JSON.stringify(["profile", "savePreset", "preamp"]))
        compare(page.rows[1].title, "save preset")
        page.setFocus(1)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(tc.callAt(0), JSON.stringify(["savePreset", "headphones"]))
        compare(page.rows[0].value, "Preset 1")
        compare(JSON.stringify(tc.keys(page).slice(0, 4)), JSON.stringify(["profile", "deletePreset", "renamePreset", "preamp"]))
        compare(page.rows[1].title, "delete preset")
        compare(page.rows[2].title, "rename preset")
        compare(page.focusedRow.key, "deletePreset")
    }

    function test_save_preset_refused_toast() {
        fakeEq.makeCustom("headphones")
        const page = open()
        fakeEq.failNext = true
        page.setFocus(1)
        verify(router.dispatch("a", false))
        compare(JSON.stringify(toasts.shown), JSON.stringify([["too many presets", ""]]))
        compare(page.rows[1].key, "savePreset")
    }

    function test_delete_preset_confirm() {
        fakeEq.makeCustom("headphones")
        const page = open()
        page.setFocus(1)
        verify(router.dispatch("a", false))
        page.setFocus(page.indexOfKey("deletePreset"))
        verify(router.dispatch("a", false))
        compare(page.deleteDialog.shown, true)
        compare(page.deleteDialog.title, "delete preset?")
        compare(JSON.stringify(page.deleteDialog.items), JSON.stringify(["delete preset", "keep preset"]))
        compare(page.deleteDialog.focusIndex, 1)
        verify(router.dispatch("a", false))
        page.deleteDialog.complete()
        compare(page.deleteDialog.shown, false)
        compare(fakeEq.calls.filter((c) => c[0] === "deletePreset").length, 0)
        verify(router.dispatch("a", false))
        compare(page.deleteDialog.shown, true)
        verify(router.dispatch("up", false))
        verify(router.dispatch("a", false))
        page.deleteDialog.complete()
        compare(tc.callAt(fakeEq.calls.length - 1), JSON.stringify(["deletePreset", "headphones"]))
        compare(page.rows[0].value, "off")
        compare(page.rows.length, 1)
        compare(page.focusIndex, 0)
    }

    function test_rename_preset_keyboard() {
        fakeEq.makeCustom("headphones")
        const page = open()
        page.setFocus(1)
        verify(router.dispatch("a", false))
        page.setFocus(page.indexOfKey("renamePreset"))
        verify(router.dispatch("a", false))
        compare(kb.shown, true)
        compare(kb.entry.title, "rename preset")
        compare(kb.entry.text, "Preset 1")
        for (let i = 0; i < 8; i++)
            kb.entry.erase()
        for (const ch of "Taken")
            kb.entry.type(ch)
        kb.finish()
        compare(kb.shown, true)
        compare(fakeEq.calls.filter((c) => c[0] === "renamePreset").length, 0)
        for (let i = 0; i < 5; i++)
            kb.entry.erase()
        for (const ch of "Desk")
            kb.entry.type(ch)
        kb.finish()
        compare(kb.shown, false)
        compare(tc.callAt(fakeEq.calls.length - 1), JSON.stringify(["renamePreset", "headphones", "Desk"]))
        compare(page.rows[0].value, "Desk")
    }

    function test_rename_preset_cancel_and_failure() {
        fakeEq.makeCustom("headphones")
        const page = open()
        page.setFocus(1)
        verify(router.dispatch("a", false))
        page.setFocus(page.indexOfKey("renamePreset"))
        verify(router.dispatch("a", false))
        kb.back()
        compare(kb.shown, false)
        kb.open({ title: "other", kind: "text" })
        kb.entry.type("x")
        kb.validator = null
        kb.finish()
        compare(fakeEq.calls.filter((c) => c[0] === "renamePreset").length, 0)
        verify(router.dispatch("a", false))
        fakeEq.failNext = true
        kb.entry.type("2")
        kb.finish()
        compare(JSON.stringify(toasts.shown), JSON.stringify([["preset not renamed", "Preset 1"]]))
    }

    function test_band_list_add_remove() {
        const sets = Object.assign({}, fakeEq.bandSets)
        sets.headphones = sets.headphones.slice(0, 3)
        fakeEq.bandSets = sets
        const page = open()
        compare(JSON.stringify(tc.keys(page)), JSON.stringify(["profile", "preamp", "band0", "band1", "band2", "addBand", "removeBand"]))
        compare(page.rows[5].title, "add band")
        compare(page.rows[5].value, "")
        compare(page.rows[6].value, "3")
        page.setFocus(5)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(tc.callAt(0), JSON.stringify(["addBand", "headphones"]))
        compare(page.headphonesPane.openBand, 3)
        compare(page.focusedRow.key, "band3")
        compare(page.rows[page.focusIndex + 1].key, "band3:type")
        compare(page.rows[page.focusIndex].title, "4")
        compare(page.rows[page.rows.length - 1].value, "4")
        page.setFocus(page.indexOfKey("removeBand"))
        verify(router.dispatch("a", false))
        compare(tc.callAt(1), JSON.stringify(["removeBand", "headphones"]))
        compare(page.headphonesPane.openBand, -1)
        compare(page.focusedRow.key, "removeBand")
        compare(page.focusedRow.value, "3")
        verify(router.dispatch("a", false))
        verify(router.dispatch("a", false))
        compare(JSON.stringify(tc.keys(page)), JSON.stringify(["profile", "savePreset", "preamp", "band0", "addBand"]))
        compare(page.focusedRow.key, "addBand")
        for (let i = 0; i < 9; i++) {
            page.setFocus(page.indexOfKey("addBand"))
            verify(router.dispatch("a", false))
        }
        compare(page.rows.filter((r) => r.band >= 0 && r.kind === "row").length, 10)
        compare(page.indexOfKey("addBand"), -1)
        compare(page.indexOfKey("removeBand") >= 0, true)
    }

    function test_band_list_hidden_when_off() {
        const page = open()
        page.switchPivot(1)
        page.completeAll()
        compare(JSON.stringify(tc.keys(page)), JSON.stringify(["profile"]))
    }

    function test_layout_and_rows() {
        const page = open()
        compare(page.pageId, 0)
        compare(page.header.text, "eq")
        compare(JSON.stringify(page.pivots), JSON.stringify(["headphones", "usb", "speaker"]))
        compare(page.pivotIndex, 0)
        compare(page.headphonesPane.y, Ui.px(128))
        compare(page.rows.length, 13)
        compare(page.rows[0].title, "profile")
        compare(page.rows[0].value, "HD 600")
        compare(page.rows[1].title, "preamp")
        compare(page.rows[1].value, "-6.5 db")
        compare(page.rows[2].title, "1")
        compare(page.rows[2].value, "ls 105 hz +2.1 db q 0.70")
        compare(page.rows[3].value, "pk 2.5 khz -3.0 db q 1.40")
        compare(page.rows[4].value, "off")
        compare(page.rows[11].title, "10")
        compare(page.rows[12].key, "removeBand")
        compare(page.rows[12].title, "remove band")
        compare(page.rows[12].value, "10")
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
        compare(page.rows.length, 13)
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
        compare(page.rows.length, 13)
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
        compare(page.rows[tc.at(page, "preamp")].value, "-6.6 db")
        compare(page.rows[0].value, "custom")
        verify(router.dispatch("lb", false))
        compare(tc.callAt(1), JSON.stringify(["stepPreamp", "headphones", -1, true]))
        compare(page.rows[tc.at(page, "preamp")].value, "-7.6 db")
        verify(router.dispatch("rb", false))
        compare(tc.callAt(2), JSON.stringify(["stepPreamp", "headphones", 1, true]))
        compare(page.rows[tc.at(page, "preamp")].value, "-6.6 db")
        compare(page.pivotIndex, 0)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.rows[tc.at(page, "preamp")].value, "-6.6 db")
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

    function test_type_text() {
        const bands = [
            { type: "off", freq: 1000, gain: 3, q: 1.4 },
            { type: "peak", freq: 1000, gain: -3, q: 1.41 },
            { type: "lowshelf", freq: 105, gain: 2.1, q: 0.7 },
            { type: "highshelf", freq: 10000, gain: 12, q: 0.71 },
            { type: "bandpass", freq: 1000, gain: 6, q: 0.71 },
            { type: "lowpass", freq: 105, gain: 6, q: 0.7 },
            { type: "highpass", freq: 30, gain: 0, q: 0.5 },
            { type: "allpass", freq: 20000, gain: -2, q: 0.25 },
            { type: "highshelf", freq: 20000, gain: -24, q: 10 },
            { type: "peak", freq: 2500, gain: 0, q: 1.4 }
        ]
        const sets = Object.assign({}, fakeEq.bandSets)
        sets.headphones = bands
        fakeEq.bandSets = sets
        const page = open()
        const want = ["off", "pk 1 khz -3.0 db q 1.41", "ls 105 hz +2.1 db q 0.70", "hs 10 khz +12.0 db q 0.71",
            "bp 1 khz q 0.71", "lp 105 hz q 0.70", "hp 30 hz q 0.50", "ap 20 khz q 0.25",
            "hs 20 khz -24.0 db q 10.00", "pk 2.5 khz 0.0 db q 1.40"]
        for (let i = 0; i < want.length; i++)
            compare(page.rows[i + 2].value, want[i])
        const long = ["off", "peak", "low shelf", "high shelf", "band pass", "low pass", "high pass", "all pass"]
        for (let i = 0; i < long.length; i++) {
            page.headphonesPane.openBand = i
            tryVerify(() => page.rows[i + 3].key === "band" + i + ":type", 1000)
            compare(page.rows[i + 3].value, long[i])
        }
    }

    function test_type_cycle() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        tc.edit(page, tc.at(page, "band0:type"))
        const long = ["low shelf", "high shelf", "band pass", "low pass", "high pass", "all pass", "off", "peak",
            "low shelf"]
        for (let i = 1; i < long.length; i++) {
            verify(router.dispatch("right", false))
            compare(page.rows[tc.at(page, "band0:type")].value, long[i])
        }
        for (let i = long.length - 2; i >= 0; i--) {
            verify(router.dispatch("lb", false))
            compare(page.rows[tc.at(page, "band0:type")].value, long[i])
        }
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(fakeEq.bandSets.headphones[0].gain, 2.1)
    }

    function test_gain_not_used() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        tc.edit(page, tc.at(page, "band0:type"))
        router.dispatch("right", false)
        router.dispatch("right", false)
        router.dispatch("right", false)
        compare(page.rows[tc.at(page, "band0:type")].value, "low pass")
        router.dispatch("a", false)
        compare(page.rows[tc.at(page, "band0")].value, "lp 105 hz q 0.70")
        compare(page.rows[tc.at(page, "band0:gain")].title, "gain")
        compare(page.rows[tc.at(page, "band0:gain")].value, "not used")
        compare(page.rows[tc.at(page, "band0:gain")].readOnly, true)
        const calls = fakeEq.calls.length
        page.setFocus(tc.at(page, "band0:gain"))
        compare(router.dispatch("a", false), true)
        compare(page.editing, false)
        compare(router.dispatch("left", false), false)
        compare(router.dispatch("right", false), false)
        compare(fakeEq.calls.length, calls)
        const gain = tc.rowItem(page.headphonesPane, tc.at(page, "band0:gain"))
        const q = tc.rowItem(page.headphonesPane, tc.at(page, "band0:q"))
        compare(gain.valueItem.font.pixelSize, q.valueItem.font.pixelSize)
        compare(gain.isFocused, true)
        compare(gain.editing, false)
        compare(gain.valueAlpha, Ui.settingsValueFocused)
        verify(!Qt.colorEqual(gain.valueItem.color, Ui.accent))
        compare(gain.valueSlot.x + gain.valueItem.x + gain.valueItem.width,
            q.valueSlot.x + q.valueItem.x + q.valueItem.width)
        compare(page.touchPress("sub", tc.at(page, "band0:gain"), 10, gain), "a")
        page.activate()
        compare(page.editing, false)
        tc.edit(page, tc.at(page, "band0:type"))
        for (let i = 0; i < 5; i++)
            router.dispatch("right", false)
        compare(page.rows[tc.at(page, "band0:type")].value, "low shelf")
        router.dispatch("a", false)
        compare(page.rows[tc.at(page, "band0:gain")].value, "+2.1 db")
        compare(page.rows[tc.at(page, "band0:gain")].readOnly, false)
    }

    function test_dac_suffix() {
        const usb = fakeEq.flat()
        usb[0] = { type: "peak", freq: 1000, gain: -15, q: 10 }
        usb[1] = { type: "lowshelf", freq: 105, gain: -24, q: 0.1 }
        usb[2] = { type: "lowpass", freq: 105, gain: -15, q: 10 }
        usb[3] = { type: "off", freq: 1000, gain: -15, q: 10 }
        usb[4] = { type: "peak", freq: 1000, gain: -12.04, q: 8.004 }
        usb[5] = { type: "highshelf", freq: 10000, gain: 12, q: 0.25 }
        usb[6] = { type: "peak", freq: 1000, gain: 11.96, q: 0.246 }
        usb[7] = { type: "peak", freq: 1000, gain: -11.96, q: 0.244 }
        const sets = Object.assign({}, fakeEq.bandSets)
        sets.usb = usb
        fakeEq.bandSets = sets
        fakeEq.keys = ({ headphones: "/eq/HD 600.txt", usb: "/eq/HD 600.txt", speaker: "off" })
        fakeEq.preamps = ({ headphones: -6.5, usb: -6.5, speaker: 0 })
        fakeEq.activeOutput = "usb"
        const page = open({ dacEq: fakeDacEq })
        compare(page.pivotIndex, 1)
        compare(page.rows[1].value, "-6.5 db (dac 0.0)")
        compare(page.rows[2].value, "pk 1 khz -15.0 db q 10.00")
        const want = [
            ["peak", "1 khz", "-15.0 db (dac -12.0)", "10.00 (dac 8.00)"],
            ["low shelf", "105 hz", "-24.0 db (dac -12.0)", "0.10 (dac 0.25)"],
            ["low pass", "105 hz", "not used", "10.00 (dac 8.00)"],
            ["off", "1 khz", "-15.0 db", "10.00"],
            ["peak", "1 khz", "-12.0 db", "8.00"],
            ["high shelf", "10 khz", "+12.0 db", "0.25"],
            ["peak", "1 khz", "+12.0 db", "0.25"],
            ["peak", "1 khz", "-12.0 db", "0.24 (dac 0.25)"]
        ]
        for (let i = 0; i < want.length; i++) {
            page.usbPane.openBand = i
            tryVerify(() => page.rows[i + 3].key === "band" + i + ":type", 1000)
            for (let f = 0; f < 4; f++)
                compare(page.rows[i + 3 + f].value, want[i][f])
        }
        page.usbPane.openBand = 1
        tryVerify(() => page.rows[4].key === "band1:type", 1000)
        const gain = tc.rowItem(page.usbPane, tc.at(page, "band1:gain"))
        compare(gain.modelData.value, "-24.0 db (dac -12.0)")
        compare(gain.valueSlot.x + gain.valueItem.x + gain.valueItem.width, gain.width - Ui.px(Ui.hubEdge))
        verify(gain.valueItem.x >= 0)
        tc.edit(page, tc.at(page, "band1:gain"))
        verify(router.dispatch("right", false))
        compare(tc.callAt(0), JSON.stringify(["stepBand", "usb", 1, "gain", 1, false]))
        compare(fakeEq.bandSets.usb[1].gain, -23.9)
        compare(page.rows[tc.at(page, "band1:gain")].value, "-23.9 db (dac -12.0)")
        verify(router.dispatch("a", false))
        fakeDacEq.status = "applying"
        compare(page.rows[tc.at(page, "preamp")].value, "-6.5 db (dac 0.0)")
        for (const s of ["none", "flat", "failed", "unsupported"]) {
            fakeDacEq.status = s
            compare(page.rows[tc.at(page, "preamp")].value, "-6.5 db")
            compare(page.rows[tc.at(page, "band1:gain")].value, "-23.9 db")
        }
        fakeDacEq.status = "on"
        verify(router.dispatch("lb", false))
        compare(page.pivotIndex, 0)
        compare(page.rows[1].value, "-6.5 db")
        fakeEq.preamps = ({ headphones: -6.5, usb: 0, speaker: 0 })
        fakeEq.profilesChanged()
        verify(router.dispatch("rb", false))
        compare(page.rows[tc.at(page, "preamp")].value, "0.0 db")
    }

    function test_dac_suffix_without_dac_eq() {
        fakeEq.keys = ({ headphones: "/eq/HD 600.txt", usb: "/eq/HD 600.txt", speaker: "off" })
        fakeEq.preamps = ({ headphones: -6.5, usb: -6.5, speaker: 0 })
        fakeEq.activeOutput = "usb"
        const page = open()
        compare(page.pivotIndex, 1)
        compare(page.rows[1].value, "-6.5 db")
    }

    function test_preamp_cancel_restores() {
        const page = open()
        tc.edit(page, tc.at(page, "preamp"))
        router.dispatch("left", false)
        router.dispatch("lb", false)
        compare(page.rows[tc.at(page, "preamp")].value, "-7.6 db")
        compare(page.rows[0].value, "custom")
        verify(router.dispatch("b", false))
        compare(page.editing, false)
        compare(pages.depth, 2)
        compare(fakeEq.preamps.headphones, -6.5)
        compare(fakeEq.keys.headphones, "/eq/HD 600.txt")
        compare(page.rows[0].value, "HD 600")
        compare(page.rows[tc.at(page, "preamp")].value, "-6.5 db")
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
        compare(page.rows.length, 17)
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
        compare(page.rows[tc.at(page, "band0:gain")].value, "+2.2 db")
        compare(page.focusedRow.key, "band0:gain")
        compare(page.rows[1].key, "savePreset")
        compare(page.rows[tc.at(page, "band0")].value, "ls 105 hz +2.2 db q 0.70")
        verify(router.dispatch("rb", false))
        compare(tc.callAt(1), JSON.stringify(["stepBand", "headphones", 0, "gain", 1, true]))
        compare(page.rows[tc.at(page, "band0:gain")].value, "+3.2 db")
        compare(page.pivotIndex, 0)
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.rows[tc.at(page, "band0:gain")].value, "+3.2 db")
        router.dispatch("down", false)
        router.dispatch("down", false)
        compare(page.focusedRow.key, "band1")
        verify(router.dispatch("a", false))
        compare(page.editing, false)
        compare(page.headphonesPane.openBand, 1)
        compare(page.focusedRow.key, "band1")
        compare(page.rows[page.focusIndex + 1].key, "band1:type")
        compare(page.rows.length, 18)
        verify(router.dispatch("a", false))
        compare(page.headphonesPane.openBand, -1)
        compare(page.focusedRow.key, "band1")
        compare(page.rows.length, 14)
    }

    function test_sub_rows_coarse_steps() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        const fields = ["type", "freq", "gain", "q"]
        for (let f = 0; f < fields.length; f++) {
            tc.edit(page, tc.at(page, "band0:" + fields[f]))
            verify(router.dispatch("rb", false))
            verify(router.dispatch("lb", false))
            verify(router.dispatch("a", false))
            compare(page.editing, false)
            compare(tc.callAt(f * 2), JSON.stringify(["stepBand", "headphones", 0, fields[f], 1, true]))
            compare(tc.callAt(f * 2 + 1), JSON.stringify(["stepBand", "headphones", 0, fields[f], -1, true]))
        }
        compare(page.pivotIndex, 0)
        compare(page.headphonesPane.openBand, 0)
        tc.edit(page, tc.at(page, "band0:freq"))
        router.dispatch("rb", false)
        compare(page.rows[tc.at(page, "band0:freq")].value, "210 hz")
        router.dispatch("right", false)
        compare(page.rows[tc.at(page, "band0:freq")].value, "236 hz")
        router.dispatch("a", false)
        tc.edit(page, tc.at(page, "band0:q"))
        router.dispatch("rb", false)
        compare(page.rows[tc.at(page, "band0:q")].value, "1.70")
        router.dispatch("left", false)
        compare(page.rows[tc.at(page, "band0:q")].value, "1.60")
        router.dispatch("a", false)
    }

    function test_sub_row_cancel_restores() {
        const page = open()
        page.setFocus(2)
        router.dispatch("a", false)
        tc.edit(page, tc.at(page, "band0:type"))
        router.dispatch("right", false)
        compare(page.rows[tc.at(page, "band0:type")].value, "high shelf")
        router.dispatch("right", false)
        compare(page.rows[tc.at(page, "band0:type")].value, "band pass")
        compare(page.rows[tc.at(page, "band0")].value, "bp 105 hz q 0.70")
        verify(router.dispatch("b", false))
        compare(page.editing, false)
        compare(pages.depth, 2)
        compare(page.rows[tc.at(page, "band0:type")].value, "low shelf")
        compare(page.rows[tc.at(page, "band0")].value, "ls 105 hz +2.1 db q 0.70")
        compare(fakeEq.keys.headphones, "/eq/HD 600.txt")
        compare(page.headphonesPane.openBand, 0)
        compare(page.focusIndex, tc.at(page, "band0:type"))
        compare(page.rows.length, 17)
        tc.edit(page, tc.at(page, "band0:gain"))
        router.dispatch("rb", false)
        router.dispatch("rb", false)
        router.dispatch("left", false)
        compare(page.rows[tc.at(page, "band0:gain")].value, "+4.0 db")
        verify(router.dispatch("b", false))
        compare(page.rows[tc.at(page, "band0:gain")].value, "+2.1 db")
        compare(fakeEq.bandSets.headphones[0].gain, 2.1)
        compare(fakeEq.keys.headphones, "/eq/HD 600.txt")
        compare(page.focusIndex, tc.at(page, "band0:gain"))
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
        tc.edit(page, tc.at(page, "preamp"))
        router.dispatch("left", false)
        page.screenOff = true
        compare(page.editing, false)
        compare(fakeEq.preamps.headphones, -6.6)
        page.screenOff = false
        tc.edit(page, tc.at(page, "preamp"))
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
        compare(page.rows.length, 13)
        router.dispatch("a", false)
        router.dispatch("rb", false)
        compare(page.pivotIndex, 2)
        compare(router.dispatch("rb", false), true)
        compare(page.pivotIndex, 2)
        router.dispatch("lb", false)
        router.dispatch("lb", false)
        compare(page.pivotIndex, 0)
        compare(page.rows.length, 13)
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
        compare(page.rows.length, 13)
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
        const preamp = tc.rowItem(page.headphonesPane, tc.at(page, "preamp"))
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
        compare(page.rows.length, 13)
        compare(tc.rowItem(page.headphonesPane, 3).enter, 1)
        page.setFocus(0)
        router.dispatch("a", false)
        compare(page.editing, true)
        router.dispatch("right", false)
        router.dispatch("right", false)
        router.dispatch("right", false)
        compare(page.rows.length, 1)
        router.dispatch("right", false)
        compare(page.rows.length, 13)
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

    function tapEqRow(page, index, fraction) {
        const item = tc.rowItem(page.headphonesPane, index)
        tc.tapAt(item, item.width * (fraction === undefined ? 0.25 : fraction), item.height / 2)
    }

    function test_touch_row_enters_edit() {
        const page = open()
        tc.tapEqRow(page, 1)
        compare(page.focusIndex, 1)
        compare(page.editing, true)
        compare(page.editKey, "preamp")
    }

    function test_touch_halves_step_fine() {
        const page = open()
        tc.tapEqRow(page, tc.at(page, "preamp"))
        tc.tapEqRow(page, tc.at(page, "preamp"), 0.75)
        compare(tc.callAt(fakeEq.calls.length - 1), JSON.stringify(["stepPreamp", "headphones", 1, false]))
        tc.tapEqRow(page, tc.at(page, "preamp"), 0.25)
        compare(tc.callAt(fakeEq.calls.length - 1), JSON.stringify(["stepPreamp", "headphones", -1, false]))
        compare(page.editing, true)
    }

    function test_touch_band_opens_and_closes() {
        const page = open()
        tc.tapEqRow(page, 2)
        compare(page.headphonesPane.openBand, 0)
        compare(page.editing, false)
        tc.tapEqRow(page, 2)
        compare(page.headphonesPane.openBand, -1)
    }

    function test_touch_header_reverts_then_pops() {
        const page = open()
        tc.tapEqRow(page, 1)
        tc.tapEqRow(page, 1, 0.75)
        const n = fakeEq.calls.length
        tc.tapHeaderWord(page)
        compare(page.editing, false)
        compare(tc.callAt(n), JSON.stringify(["stepPreamp", "headphones", -1, false]))
        compare(pages.depth, 2)
        tc.tapHeaderWord(page)
        compare(pages.depth, 1)
    }

    function test_touch_pivot_by_tap() {
        const page = open()
        const label = page.strip.labelItem(1)
        tc.tapAt(label, Ui.px(4), label.height / 2)
        compare(page.pivotIndex, 1)
    }

    function test_touch_drag_scrolls_outside_edit() {
        const page = open()
        const pane = page.headphonesPane
        tc.dragUp(page, page.width / 2, page.height - Ui.px(20), Ui.px(100))
        verify(pane.contentY > 0)
        compare(page.editing, false)
        const top = page.focusIndex * page.pitch
        verify(top >= pane.contentY - 0.5)
        verify(top + page.pitch <= pane.contentY + pane.height + 0.5)
    }

    function test_touch_drag_in_edit_does_nothing() {
        const page = open()
        tc.tapEqRow(page, 1)
        compare(page.editing, true)
        tc.dragUp(page, page.width / 2, page.height - Ui.px(20), Ui.px(100))
        compare(page.headphonesPane.contentY, 0)
        compare(page.editing, true)
        compare(page.focusIndex, 1)
    }
}
