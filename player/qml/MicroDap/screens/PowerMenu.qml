import QtQuick

Dialog {
    id: menu

    readonly property var words: ["screen-off", "game-mode", "suspend", "poweroff"]
    readonly property int gameModeIndex: 1

    signal verb(string word)
    signal gameMode()

    title: "power"
    items: ["screen off", "game mode", "suspend", "power off"]

    onAccepted: (index) => {
        menu.close()
        if (index === menu.gameModeIndex) {
            menu.gameMode()
            return
        }
        menu.verb(menu.words[index])
    }
}
