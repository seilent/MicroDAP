pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "browse"

    readonly property var openShots: ({
        "eq-open-hs": { band: 3, field: "type", edit: false },
        "eq-type-edit": { band: 4, field: "type", edit: true },
        "eq-gain-unused": { band: 5, field: "gain", edit: false },
        "eq-open-hp": { band: 6, field: "type", edit: false },
        "eq-open-ap": { band: 7, field: "type", edit: false }
    })
    readonly property var openShot: root.openShots[root.fixture] === undefined ? null : root.openShots[root.fixture]
    readonly property bool typesShot: root.fixture === "eq-types" || root.fixture === "eq-types-end" || root.openShot !== null
    readonly property var bandShots: ({ "eq-bands": 3, "eq-bands-one": 1, "eq-bands-full": 10 })
    readonly property int bandShot: root.bandShots[root.fixture] === undefined ? 0 : root.bandShots[root.fixture]
    readonly property bool dacShot: root.fixture === "eq-dac-range" || root.fixture === "eq-dac-preamp" || root.fixture === "eq-dac-long"

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: sampleEq

        readonly property string activeOutput: root.dacShot ? "usb" : "headphones"
        readonly property var sources: [{ name: "SENNHEISER HD 600 PARAMETRICEQ", path: "/home/micro/.config/microdap/eq/Sennheiser HD 600 ParametricEQ.txt" }]
        readonly property var headphones: ({
            key: "/home/micro/.config/microdap/eq/Sennheiser HD 600 ParametricEQ.txt",
            name: "Sennheiser HD 600 ParametricEQ",
            missing: false,
            preamp: -6.5,
            bands: root.typesShot ? sampleEq.types : root.bandShot > 0 ? sampleEq.autoEq.slice(0, root.bandShot) : sampleEq.autoEq
        })
        readonly property var types: [
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
        readonly property var autoEq: [
            { type: "lowshelf", freq: 105, gain: 2.1, q: 0.7 },
            { type: "peak", freq: 183.4, gain: -1.8, q: 1.12 },
            { type: "peak", freq: 1450, gain: 2.5, q: 2.1 },
            { type: "peak", freq: 3200, gain: -3.4, q: 3.4 },
            { type: "peak", freq: 5600, gain: 4, q: 4.5 },
            { type: "peak", freq: 8100, gain: -2.6, q: 5 },
            { type: "highshelf", freq: 10000, gain: 3, q: 0.7 },
            { type: "peak", freq: 2500, gain: 0, q: 1.4 },
            { type: "off", freq: 16000, gain: 0, q: 1.4 },
            { type: "highshelf", freq: 20000, gain: -24, q: 10 }
        ]

        readonly property var dacLow: ({
            key: "/home/micro/.config/microdap/eq/DQ LOW.txt",
            name: "DQ LOW",
            missing: false,
            preamp: -6.5,
            bands: [
                { type: "peak", freq: 1000, gain: -15, q: 10 },
                { type: "lowshelf", freq: 105, gain: -24, q: 0.1 }
            ].concat(sampleEq.autoEq.slice(2))
        })

        signal profilesChanged()

        function profileMap(output) {
            if (output === "headphones")
                return sampleEq.headphones
            if (output === "usb" && root.dacShot)
                return sampleEq.dacLow
            if (output === "usb")
                return { key: "/run/media/micro/SD/Music/EQ/Apple Dongle.txt", name: "Apple Dongle", missing: true, preamp: 0, bands: [] }
            return { key: "off", name: "", missing: false, preamp: 0, bands: [] }
        }
    }

    QtObject {
        id: sampleDacEq

        readonly property string status: "on"
        readonly property var ranges: ({ gain: [-12, 12], q: [0.25, 8], preamp: [0, 0] })
    }

    EqScreen {
        id: screen

        anchors.fill: parent
        animated: false
        eq: sampleEq
        dacEq: root.dacShot ? sampleDacEq : null

        Component.onCompleted: {
            screen.completeAll()
            if (root.dacShot) {
                const band = root.fixture === "eq-dac-range" ? 0 : 1
                screen.usbPane.openBand = band
                screen.focusIndex = screen.indexOfKey(root.fixture === "eq-dac-preamp" ? "preamp" : "band" + band + ":gain")
                Qt.callLater(() => {
                    screen.activePane.ensureVisible(screen.indexOfKey("band" + band + ":q"), 0)
                    screen.activePane.ensureVisible(screen.focusIndex, 0)
                })
                return
            }
            if (root.openShot !== null) {
                const band = "band" + root.openShot.band
                screen.headphonesPane.openBand = root.openShot.band
                screen.focusIndex = screen.indexOfKey(band + ":" + root.openShot.field)
                if (root.openShot.edit)
                    screen.activate()
                Qt.callLater(() => screen.activePane.ensureVisible(screen.indexOfKey(band + ":q"), 0))
                return
            }
            if (root.bandShot > 0) {
                screen.focusIndex = screen.indexOfKey(root.bandShot === 10 ? "removeBand" : "addBand")
                Qt.callLater(() => screen.activePane.ensureVisible(screen.focusIndex, 0))
                return
            }
            if (root.typesShot) {
                screen.focusIndex = screen.indexOfKey(root.fixture === "eq-types" ? "band4" : "band9")
                Qt.callLater(() => screen.activePane.ensureVisible(screen.focusIndex, 0))
                return
            }
            screen.headphonesPane.openBand = 1
            screen.focusIndex = screen.indexOfKey("band1:freq")
            Qt.callLater(() => screen.activePane.ensureVisible(screen.focusIndex, 0))
        }
    }
}
