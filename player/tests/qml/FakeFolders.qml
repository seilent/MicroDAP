import QtQuick

ListModel {
    id: model

    property bool active
    property int folder: -1
    property var lines: []

    readonly property var dirs: ({
        "1": { name: "internal", parent: -1, dirs: [2], songs: [{ id: 11, title: "One" }, { id: 12, title: "Two" }] },
        "2": { name: "a", parent: 1, dirs: [], songs: [{ id: 21, title: "Deep" }] }
    })

    function pathOf(id: int): string {
        const d = model.dirs[String(id)]
        return d.parent < 0 ? d.name : model.pathOf(d.parent) + " / " + d.name
    }

    function countOf(id: int): int {
        const d = model.dirs[String(id)]
        let n = d.songs.length
        for (const c of d.dirs)
            n += model.countOf(c)
        return n
    }

    function folderRow(id: int): var {
        const n = model.countOf(id)
        return { kind: "folder", letter: "", id: id, title: model.dirs[String(id)].name, subtitle: n + (n === 1 ? " song" : " songs"), artPath: "", number: "", sourceRow: model.count }
    }

    function fill(): void {
        model.clear()
        if (model.folder < 0) {
            model.append(model.folderRow(1))
            return
        }
        const d = model.dirs[String(model.folder)]
        model.append({ kind: "path", letter: "", id: -1, title: model.pathOf(model.folder), subtitle: "", artPath: "", number: "", sourceRow: 0 })
        for (const c of d.dirs)
            model.append(model.folderRow(c))
        for (let i = 0; i < d.songs.length; i++)
            model.append({ kind: "song", letter: "", id: d.songs[i].id, title: d.songs[i].title, subtitle: "Band", artPath: "", number: String(i + 1), sourceRow: model.count })
    }

    function reset(): void {
        model.folder = -1
        model.lines = []
        model.fill()
    }

    function isFocusable(row: int): bool {
        return row >= 0 && row < model.count && model.get(row).kind !== "path"
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

    function openFolder(id: int): bool {
        if (model.dirs[String(id)] === undefined || id === model.folder)
            return false
        model.folder = id
        model.fill()
        return true
    }

    function up(): int {
        if (model.folder < 0)
            return -1
        const left = model.folder
        model.folder = model.dirs[String(left)].parent
        model.fill()
        for (let i = 0; i < model.count; i++) {
            if (model.get(i).kind === "folder" && model.get(i).id === left)
                return i
        }
        return 0
    }

    function tracks(id: int): var {
        const d = model.dirs[String(id)]
        let out = []
        for (const c of d.dirs)
            out = out.concat(model.tracks(c))
        for (const s of d.songs)
            out.push(s.id)
        return out
    }

    function name(id: int): string {
        return model.dirs[String(id)].name
    }

    function pathLine(font: var, width: real): string {
        model.lines = model.lines.concat([width])
        return model.folder < 0 ? "" : model.pathOf(model.folder)
    }
}
