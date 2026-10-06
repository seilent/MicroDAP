pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: sampleEq

        readonly property string activeOutput: "headphones"
        readonly property var sources: [{ name: "SENNHEISER HD 600 PARAMETRICEQ", path: "/home/armada/.config/casdap/eq/Sennheiser HD 600 ParametricEQ.txt" }]
        readonly property var headphones: ({
            key: "/home/armada/.config/casdap/eq/Sennheiser HD 600 ParametricEQ.txt",
            name: "Sennheiser HD 600 ParametricEQ",
            missing: false,
            preamp: -6.5,
            bands: [
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
        })

        signal profilesChanged()

        function profileMap(output) {
            if (output === "headphones")
                return sampleEq.headphones
            if (output === "usb")
                return { key: "/run/media/armada/SD/Music/EQ/Apple Dongle.txt", name: "Apple Dongle", missing: true, preamp: 0, bands: [] }
            return { key: "off", name: "", missing: false, preamp: 0, bands: [] }
        }
    }

    EqScreen {
        id: screen

        anchors.fill: parent
        animated: false
        eq: sampleEq

        Component.onCompleted: {
            screen.completeAll()
            screen.headphonesPane.openBand = 1
            screen.focusIndex = screen.indexOfKey("band1:freq")
            screen.activePane.ensureVisible(screen.focusIndex, 0)
        }
    }
}
