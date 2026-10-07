import QtQuick

ListModel {
    id: grid

    property int columns
    property bool active
    property var flat

    onColumnsChanged: grid.regroup()

    function letterOf(e: var): string {
        if (e.section !== undefined && e.section !== null && String(e.section) !== "")
            return String(e.section).toLowerCase()
        const key = String(e.title).trim().toLowerCase()
        if (key.length === 0)
            return "#"
        const c = key.charAt(0)
        return c >= "a" && c <= "z" ? c : "#"
    }

    function load(list: var): void {
        const cells = []
        let last = ""
        for (let i = 0; i < list.length; i++) {
            const e = list[i]
            const letter = grid.letterOf(e)
            if (cells.length === 0 || letter !== last) {
                cells.push({ kind: "letter", letter: letter, id: -1, title: letter, subtitle: "", artPath: "" })
                last = letter
            }
            cells.push({
                kind: "album",
                letter: letter,
                id: Number(e.id),
                title: String(e.title),
                subtitle: e.subtitle === undefined ? "" : String(e.subtitle),
                artPath: e.artPath === undefined ? "" : String(e.artPath),
                row: i,
                number: String(i + 1)
            })
        }
        grid.flat = cells
        grid.regroup()
    }

    function span(): int {
        return Math.max(1, grid.columns > 0 ? grid.columns : 3)
    }

    function rowsOf(): int {
        const cells = grid.flat === undefined || grid.flat === null ? [] : grid.flat
        return Math.ceil(cells.length / grid.span())
    }

    function regroup(): void {
        grid.clear()
        const n = grid.rowsOf()
        for (let r = 0; r < n; r++)
            grid.append({ cells: grid.span() + ":" + grid.cells(r).map((c) => c.kind + c.id).join(",") })
    }

    function cells(row: int): var {
        const all = grid.flat === undefined || grid.flat === null ? [] : grid.flat
        const c = grid.span()
        if (row < 0 || row * c >= all.length)
            return []
        return all.slice(row * c, row * c + c)
    }

    function cellAt(row: int, col: int): var {
        const list = grid.cells(row)
        return col >= 0 && col < list.length ? list[col] : ({})
    }

    function cellCount(row: int): int {
        return grid.cells(row).length
    }

    function position(index: int): var {
        return { row: index < 0 ? -1 : Math.floor(index / grid.span()), col: index < 0 ? -1 : index % grid.span() }
    }

    function letters(): var {
        const all = grid.flat === undefined || grid.flat === null ? [] : grid.flat
        return all.filter((c) => c.kind === "letter").map((c) => c.letter)
    }

    function cellOfLetter(letter: string): var {
        const all = grid.flat === undefined || grid.flat === null ? [] : grid.flat
        return grid.position(all.findIndex((c) => c.kind === "letter" && c.letter === letter))
    }

    function rowOfLetter(letter: string): int {
        return grid.cellOfLetter(letter).row
    }

    function cellOfAlbum(id: int): var {
        const all = grid.flat === undefined || grid.flat === null ? [] : grid.flat
        return grid.position(all.findIndex((c) => c.kind === "album" && c.id === id))
    }

    function rowOfAlbum(id: int): int {
        return grid.cellOfAlbum(id).row
    }
}
