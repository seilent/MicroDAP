import QtQuick

Dialog {
    id: menu

    readonly property var entries: [
        { kind: "verb", word: "screen-off", text: "screen off" },
        { kind: "game", word: "game-mode", text: "game mode" },
        { kind: "verb", word: "suspend", text: "suspend" },
        { kind: "verb", word: "poweroff", text: "power off" }
    ]

    signal verb(string word)
    signal gameMode()

    title: "power"
    items: menu.entries.map((e) => e.text)

    onAccepted: (index) => {
        const entry = menu.entries[index]
        menu.close()
        if (entry === undefined)
            return
        if (entry.kind === "game")
            menu.gameMode()
        else
            menu.verb(entry.word)
    }
}
