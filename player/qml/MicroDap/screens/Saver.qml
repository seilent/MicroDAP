pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: saver

    property bool shown: false
    property bool screenOff: false
    property bool dimmed: false
    property string title: ""
    property string artist: ""
    property string album: ""
    property string artPath: ""
    property bool phone: false
    property string fixedClock: ""
    property real progress: 0
    property real drift: Motion.saverDriftFrom
    property string clockText: saver.fixedClock
    property bool lyricsOn: false
    property var lyrics: null
    property var player: null
    property string lyricText: ""
    property string shownLyric: ""
    property var lyricLines: []
    property string clockMode: "24h"

    readonly property bool live: !saver.screenOff
    readonly property bool animating: showAnim.running || hideAnim.running || dimAnim.running || lyricSwap.running
                                      || lyricRise.running
    readonly property bool drifting: driftLoop.running && !driftLoop.paused
    readonly property string artUrl: saver.artPath === "" ? "" : "file://" + saver.artPath
    readonly property real artY: Ui.px(Ui.saverArtY)
    readonly property real textX: Ui.px(Ui.saverTextX)
    readonly property real titleBaseline: saver.artY + Ui.px(Ui.saverTitleDrop)
    readonly property real artistBaseline: saver.titleBaseline + Ui.px(Ui.saverArtistPitch)
    readonly property real albumBaseline: saver.artistBaseline + Ui.px(Ui.saverAlbumPitch)
    readonly property real lyricBaseline: Ui.px(Ui.saverLyricBaseline)
    property alias clockItem: clockItem
    property alias artItem: artItem
    property alias phoneGlyph: phoneGlyph
    property alias titleItem: titleItem
    property alias artistItem: artistItem
    property alias albumItem: albumItem
    property alias lyricItem: lyricItem
    property alias lyricSwap: lyricSwap
    property alias textGroup: textGroup
    property alias dimLayer: dimLayer
    property alias dimTimer: dimTimer
    property alias clockTimer: clockTimer
    property alias driftLoop: driftLoop
    property alias showAnimation: showAnim
    property alias hideAnimation: hideAnim
    property alias dimAnimation: dimAnim

    visible: saver.progress > 0 && !saver.screenOff
    opacity: saver.progress

    function formatClock(date: var): string {
        if (saver.clockMode === "off")
            return ""
        const h = saver.clockMode === "12h" ? (date.getHours() + 11) % 12 + 1 : date.getHours()
        const m = date.getMinutes()
        return h + ":" + (m < 10 ? "0" : "") + m
    }

    function refreshClock(): void {
        if (saver.fixedClock !== "")
            saver.clockText = saver.fixedClock
        else
            saver.clockText = saver.formatClock(new Date())
    }

    function readLines(): void {
        const lines = saver.lyrics === null || saver.lyrics.pending === true ? null : saver.lyrics.lines
        saver.lyricLines = lines === undefined || lines === null ? [] : lines
    }

    function lyricLine(): string {
        if (!saver.lyricsOn || saver.lyrics === null || saver.player === null || saver.lyrics.pending === true)
            return ""
        const lines = saver.lyricLines
        if (lines.length === 0)
            return ""
        const index = saver.lyrics.lineAt(Number(saver.player.position))
        return index < 0 || index >= lines.length ? "" : String(lines[index].text)
    }

    function refreshLyric(animate: bool): void {
        const text = saver.lyricLine()
        saver.lyricText = text
        const busy = lyricSwap.running || lyricRise.running
        if (!animate || !saver.shown || !saver.live) {
            lyricSwap.stop()
            lyricRise.stop()
            saver.shownLyric = text
            lyricItem.opacity = 1
            return
        }
        if (lyricSwap.running && text === saver.shownLyric && !lyricSwap.swapped) {
            lyricSwap.stop()
            lyricItem.opacity = 1
            return
        }
        if (busy || saver.shownLyric === text)
            return
        if (saver.shownLyric === "") {
            saver.shownLyric = text
            lyricRise.start()
            return
        }
        lyricSwap.swapped = false
        lyricSwap.start()
    }

    function show(): void {
        if (saver.shown)
            return
        saver.refreshLyric(false)
        saver.shown = true
        saver.dimmed = false
        saver.refreshClock()
        hideAnim.stop()
        dimAnim.stop()
        dimLayer.opacity = 0
        if (!saver.live) {
            saver.progress = 1
            return
        }
        showAnim.start()
    }

    function hide(): void {
        if (!saver.shown)
            return
        saver.shown = false
        saver.dimmed = false
        showAnim.stop()
        dimAnim.stop()
        if (!saver.live) {
            saver.progress = 0
            dimLayer.opacity = 0
            return
        }
        hideAnim.start()
    }

    function land(): void {
        showAnim.stop()
        hideAnim.stop()
        dimAnim.stop()
        saver.progress = saver.shown ? 1 : 0
        dimLayer.opacity = saver.shown && saver.dimmed ? Motion.dimAlpha : 0
        saver.refreshLyric(false)
    }

    function completeAll(): void {
        saver.land()
    }

    onScreenOffChanged: {
        if (saver.screenOff)
            saver.land()
    }

    onFixedClockChanged: saver.refreshClock()
    onLyricsOnChanged: saver.refreshLyric(false)
    onLyricsChanged: saver.readLines()
    onClockModeChanged: saver.refreshClock()

    Connections {
        target: saver.shown && saver.live ? saver.player : null
        ignoreUnknownSignals: true

        function onPositionChanged() {
            saver.refreshLyric(true)
        }
    }

    Connections {
        target: saver.lyrics
        ignoreUnknownSignals: true

        function onChanged() {
            saver.readLines()
            saver.refreshLyric(true)
        }
    }

    Component.onCompleted: {
        saver.refreshClock()
        saver.readLines()
    }

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    Text {
        id: clockItem

        x: saver.width - Ui.px(Ui.edge) - clockItem.width
        y: Ui.px(Ui.saverClockBaseline) - clockItem.baselineOffset
        text: saver.clockText
        color: Ui.white(Ui.saverClock.alpha)
        font: Ui.font(Ui.saverClock)
    }

    Rectangle {
        id: artItem

        x: Ui.px(Ui.edge)
        y: saver.artY
        width: Ui.px(Ui.saverArt)
        height: Ui.px(Ui.saverArt)
        color: saver.phone ? "transparent" : Ui.tile

        Glyph {
            id: phoneGlyph

            anchors.centerIn: parent
            visible: saver.phone
            name: "bluetooth"
            size: Ui.npBtGlyph / Ui.npArt * Ui.saverArt
            color: Ui.textFull
        }

        Image {
            anchors.fill: parent
            source: saver.artUrl
            visible: !saver.phone && saver.artUrl !== ""
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            mipmap: false
            sourceSize: Qt.size(Math.ceil(artItem.width), Math.ceil(artItem.height))
        }
    }

    Item {
        id: textGroup

        x: saver.textX
        y: Ui.px(saver.drift)
        width: saver.width - saver.textX - Ui.px(Ui.edge)
        height: saver.height
        clip: true

        Text {
            id: titleItem

            x: 0
            y: saver.titleBaseline - titleItem.baselineOffset
            width: textGroup.width
            text: saver.title
            color: Ui.white(Ui.saverTitle.alpha)
            font: Ui.font(Ui.saverTitle)
            clip: true
        }

        Text {
            id: artistItem

            x: 0
            y: saver.artistBaseline - artistItem.baselineOffset
            width: textGroup.width
            text: saver.artist
            color: Ui.white(Ui.saverArtist.alpha)
            font: Ui.font(Ui.saverArtist)
            clip: true
        }

        Text {
            id: albumItem

            x: 0
            y: saver.albumBaseline - albumItem.baselineOffset
            width: textGroup.width
            text: saver.album
            color: Ui.white(Ui.saverAlbum.alpha)
            font: Ui.font(Ui.saverAlbum)
            clip: true
        }
    }

    Text {
        id: lyricItem

        x: Ui.px(Ui.edge)
        y: Ui.px(saver.drift) + saver.lyricBaseline - lyricItem.baselineOffset
        width: saver.width - 2 * Ui.px(Ui.edge)
        text: saver.shownLyric
        visible: saver.shownLyric !== ""
        color: Ui.white(Ui.npNext.alpha)
        font: Ui.font(Ui.npNext)
        clip: true
    }

    Rectangle {
        id: dimLayer

        anchors.fill: parent
        color: Ui.black
        opacity: 0
    }

    Timer {
        id: clockTimer

        interval: Motion.elapsedTick
        repeat: true
        running: saver.shown && saver.live && saver.fixedClock === ""
        onTriggered: saver.refreshClock()
    }

    Timer {
        id: dimTimer

        interval: Motion.dimDelay
        repeat: false
        running: saver.shown && saver.live && !saver.dimmed && !showAnim.running
        onTriggered: {
            saver.dimmed = true
            dimAnim.start()
        }
    }

    NumberAnimation {
        id: showAnim

        target: saver
        property: "progress"
        to: 1
        duration: Motion.saver
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    NumberAnimation {
        id: hideAnim

        target: saver
        property: "progress"
        to: 0
        duration: Motion.saver
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
        onFinished: dimLayer.opacity = 0
    }

    NumberAnimation {
        id: dimAnim

        target: dimLayer
        property: "opacity"
        from: 0
        to: Motion.dimAlpha
        duration: Motion.dim
        easing.type: Motion.bezier
        easing.bezierCurve: Motion.decel
    }

    SequentialAnimation {
        id: lyricSwap

        property bool swapped: false

        onFinished: saver.refreshLyric(true)

        NumberAnimation {
            target: lyricItem
            property: "opacity"
            to: Motion.valueDip
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
        ScriptAction {
            script: {
                lyricSwap.swapped = true
                saver.shownLyric = saver.lyricText
            }
        }
        NumberAnimation {
            target: lyricItem
            property: "opacity"
            to: 1
            duration: Motion.valueChange
            easing.type: Motion.linear
        }
    }

    NumberAnimation {
        id: lyricRise

        target: lyricItem
        property: "opacity"
        from: Motion.valueDip
        to: 1
        duration: Motion.valueChange
        easing.type: Motion.linear
        onFinished: saver.refreshLyric(true)
    }

    SequentialAnimation {
        id: driftLoop

        running: saver.shown
        loops: Animation.Infinite
        paused: saver.shown && !saver.live

        NumberAnimation {
            target: saver
            property: "drift"
            from: Motion.saverDriftFrom
            to: Motion.saverDriftTo
            duration: Motion.saverDrift
            easing.type: Motion.linear
        }
        NumberAnimation {
            target: saver
            property: "drift"
            from: Motion.saverDriftTo
            to: Motion.saverDriftFrom
            duration: Motion.saverDrift
            easing.type: Motion.linear
        }
    }
}
