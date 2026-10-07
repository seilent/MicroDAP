pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: layer

    property QtObject power: null
    property QtObject bluez: null
    property QtObject player: null
    property bool screenOff: false
    property string topScreen: ""
    property var lastTrackId: null

    readonly property int statusPlaying: 1
    readonly property bool phoneActive: layer.bluez !== null && layer.bluez.transportActive === true
    readonly property string phoneName: layer.bluez !== null && layer.bluez.deviceName !== undefined ? layer.bluez.deviceName : ""
    readonly property string phoneTitle: layer.bluez !== null && layer.bluez.title !== undefined ? layer.bluez.title : ""
    readonly property string phoneLabel: "from " + layer.phoneName
    readonly property var current: layer.player !== null && layer.player.current !== undefined && layer.player.current !== null ? layer.player.current : ({})
    readonly property string localArtist: layer.current.artist !== undefined ? layer.current.artist : ""
    readonly property string localTitle: layer.current.title !== undefined ? layer.current.title : ""
    readonly property bool localPlaying: layer.player !== null && layer.player.status === layer.statusPlaying
    readonly property bool phonePlaying: layer.bluez !== null && layer.bluez.status === "playing"
    readonly property bool onNowPlaying: layer.topScreen === "nowplaying"
    readonly property bool onBluetooth: layer.topScreen === "bluetooth"
    property alias volume: volume
    property alias toast: toast

    function trackId(map: var): var {
        if (map === undefined || map === null || map.id === undefined)
            return null
        return map.id
    }

    function showVolume(level: int) {
        volume.show(level)
    }

    function showLocalToast(map: var) {
        if (layer.onNowPlaying)
            return
        toast.show(map.artist !== undefined ? map.artist : "", map.title !== undefined ? map.title : "")
    }

    function addedLabel(count: int): string {
        return "added " + count + (count === 1 ? " song" : " songs")
    }

    function showAdded(count: int, source: string): void {
        if (count <= 0)
            return
        toast.show(layer.addedLabel(count), source)
    }

    function showPhoneToast() {
        if (layer.onNowPlaying || layer.onBluetooth || layer.bluez.transportActive !== true)
            return
        toast.show("from " + layer.bluez.deviceName, layer.bluez.title)
    }

    function completeAll() {
        volume.complete()
        toast.complete()
    }

    Component.onCompleted: layer.lastTrackId = layer.trackId(layer.current)

    VolumeOverlay {
        id: volume

        anchors.fill: parent
        screenOff: layer.screenOff
        playing: layer.phoneActive ? layer.phonePlaying : layer.localPlaying
        artist: layer.phoneActive ? layer.phoneLabel : layer.localArtist
        title: layer.phoneActive ? layer.phoneTitle : layer.localTitle
    }

    Toast {
        id: toast

        x: 0
        y: 0
        width: layer.width
        screenOff: layer.screenOff
    }

    Connections {
        target: layer.power
        ignoreUnknownSignals: true

        function onVolumeChanged(level) {
            layer.showVolume(level === undefined ? layer.power.volume : level)
        }
    }

    Connections {
        target: layer.bluez
        ignoreUnknownSignals: true

        function onVolumeChanged(level) {
            layer.showVolume(level === undefined ? layer.bluez.volume : level)
        }

        function onTransportActiveChanged() {
            layer.showPhoneToast()
        }

        function onTitleChanged() {
            layer.showPhoneToast()
        }
    }

    Connections {
        target: layer.player
        ignoreUnknownSignals: true

        function onCurrentChanged() {
            const map = layer.player.current
            const id = layer.trackId(map)
            if (id === layer.lastTrackId)
                return
            layer.lastTrackId = id
            if (id === null)
                return
            layer.showLocalToast(map)
        }
    }
}
