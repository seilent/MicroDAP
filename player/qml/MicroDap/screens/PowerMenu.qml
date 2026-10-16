import QtQuick

Dialog {
    id: menu

    property var sessions: []
    readonly property var entries: menu.buildEntries(menu.sessions)

    signal verb(string word)
    signal gameMode()
    signal switchTo(string session)

    title: "power"
    items: menu.entries.map((e) => e.text)

    function buildEntries(list: var): var {
        const out = [{ kind: "verb", word: "screen-off", text: "screen off" }]
        const others = []
        for (const s of list === null || list === undefined ? [] : list) {
            if (s === null || s === undefined || s.name === undefined)
                continue
            const name = String(s.name)
            if (name === "steam") {
                out.push({ kind: "game", word: "game-mode", text: "game mode" })
                continue
            }
            const label = s.label === undefined || String(s.label) === "" ? name : String(s.label)
            others.push({ kind: "session", word: name, text: label })
        }
        return out.concat(others, [
            { kind: "verb", word: "suspend", text: "suspend" },
            { kind: "verb", word: "poweroff", text: "power off" }
        ])
    }

    onAccepted: (index) => {
        const entry = menu.entries[index]
        menu.close()
        if (entry === undefined)
            return
        if (entry.kind === "game")
            menu.gameMode()
        else if (entry.kind === "session")
            menu.switchTo(entry.word)
        else
            menu.verb(entry.word)
    }
}
