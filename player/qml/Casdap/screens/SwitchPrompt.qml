import QtQuick

Dialog {
    id: prompt

    readonly property int closeIndex: 0
    readonly property int keepIndex: 1

    signal closePlayer()
    signal keepInBackground()

    title: "game mode"
    items: ["close player", "keep in background"]

    function show(playing: bool) {
        prompt.open()
        prompt.focusIndex = playing ? prompt.keepIndex : prompt.closeIndex
    }

    onAccepted: (index) => {
        if (index === prompt.keepIndex)
            prompt.keepInBackground()
        else
            prompt.closePlayer()
    }
}
