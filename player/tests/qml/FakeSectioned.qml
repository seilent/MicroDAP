import QtQuick

ListModel {
    id: model

    property string kind
    property bool active
    property var entries

    function letterOf(e: var): string {
        if (e.section !== undefined && e.section !== null && String(e.section) !== "")
            return String(e.section).toLowerCase()
        const key = String(e.title).trim().toLowerCase()
        if (key.length === 0)
            return "#"
        const c = key.charAt(0)
        return c >= "a" && c <= "z" ? c : "#"
    }

    function dividerRow(letter: string): var {
        return { kind: "divider", letter: letter, id: -1, title: letter, subtitle: "", artPath: "", number: "", sourceRow: -1 }
    }

    function entryRow(e: var, i: int, letter: string): var {
        return {
            kind: model.kind,
            letter: letter,
            id: Number(e.id),
            title: String(e.title),
            subtitle: e.subtitle === undefined ? "" : String(e.subtitle),
            artPath: e.artPath === undefined ? "" : String(e.artPath),
            number: String(i + 1),
            sourceRow: i
        }
    }

    function load(list: var, sourceKind: string): void {
        model.kind = sourceKind
        model.entries = []
        model.clear()
        for (let i = 0; i < list.length; i++)
            model.push(list[i])
    }

    function push(e: var): void {
        const i = model.entries.length
        model.entries = model.entries.concat([e])
        const letter = model.letterOf(e)
        const last = model.count > 0 ? model.get(model.count - 1).letter : ""
        if (model.count === 0 || last !== letter)
            model.append(model.dividerRow(letter))
        model.append(model.entryRow(e, i, letter))
    }

    function isFocusable(row: int): bool {
        return row >= 0 && row < model.count && model.get(row).kind !== "divider"
    }

    function nearestFocusable(from: int, dir: int): int {
        const step = dir >= 0 ? 1 : -1
        for (let i = from; i >= 0 && i < model.count; i += step) {
            if (model.isFocusable(i))
                return i
        }
        for (let i = from - step; i >= 0 && i < model.count; i -= step) {
            if (model.isFocusable(i))
                return i
        }
        return -1
    }

    function letters(): var {
        const out = []
        for (let i = 0; i < model.count; i++) {
            if (model.get(i).kind === "divider")
                out.push(model.get(i).letter)
        }
        return out
    }

    function rowOfLetter(letter: string): int {
        for (let i = 0; i < model.count; i++) {
            const r = model.get(i)
            if (r.kind === "divider" && r.letter === letter)
                return i
        }
        return -1
    }

    function firstRowOfLetter(letter: string): int {
        const divider = model.rowOfLetter(letter)
        return divider < 0 ? -1 : model.nearestFocusable(divider + 1, 1)
    }

    function rowForSource(sourceRow: int): int {
        for (let i = 0; i < model.count; i++) {
            if (model.get(i).sourceRow === sourceRow)
                return i
        }
        return -1
    }
}
