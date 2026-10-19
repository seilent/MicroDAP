pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: column

    property var lines: []
    property var plain: []
    property bool pending: false
    property string title: ""
    property string next: ""
    property int from: -2
    property int to: -2
    property real p: 1
    property real textLeft: Ui.edge
    property real textTop: Ui.npTextTop
    property real textBottom: (Ui.landscape ? Ui.npNextBaselineLandscape : Ui.npTitleY + Ui.npNextPitch)
        + Ui.npNextPitch * (Ui.npNextCount - 1)

    readonly property int none: -2
    readonly property real textRight: Ui.canvasW - Ui.edge
    readonly property real wrapWidth: Ui.px(column.textRight - column.textLeft)
    readonly property var plainRole: Object.assign({}, Ui.npAlbum, { name: "npPlain", caps: Font.MixedCase })
    readonly property bool plainShown: !column.pending && column.to === column.none && column.lines.length === 0
        && column.plain.length > 0
    readonly property bool timedShown: !column.pending && !column.plainShown
    readonly property real plainBottom: column.textTop
        + Math.floor((column.textBottom - column.textTop) / Ui.npPlainPitch + 1e-6) * Ui.npPlainPitch
    readonly property var plan: column.build(column.from, column.to, column.lines, column.title, column.next,
        column.wrapWidth, Ui.u, column.textTop, column.textBottom, column.timedShown)
    readonly property var frame: column.place(column.plan, column.p)
    readonly property var plainRows: column.wrapPlain(column.plain, column.wrapWidth, Ui.u, column.plainShown)

    property alias drawRepeater: drawRepeater
    property alias plainView: plainView
    property alias plainRepeater: plainRepeater

    function textOf(i: int, big: bool): string {
        if (i < 0)
            return big ? column.title : column.next
        const line = column.lines[i]
        return line === undefined || line.text === undefined || line.text === null ? "" : String(line.text)
    }

    function wrapper(width: real): var {
        const cache = ({})
        return (i, big) => {
            const key = (big ? "b" : "s") + i
            if (cache[key] === undefined) {
                const t = column.textOf(i, big)
                cache[key] = t === "" ? [] : Env.wrapLines(t, Ui.font(big ? Ui.npTitle : Ui.npNext), width)
            }
            return cache[key]
        }
    }

    function blocks(line: int, out: var, wrap: var, top: real, bottom: real): real {
        const cp = Ui.npLyricPitch
        const np = Ui.npNextPitch
        const none = line === column.none
        const count = column.lines.length
        let curN = none || line >= 0 ? Math.max(wrap(none ? -1 : line, true).length, 1) : 0
        const nextIndex = (state) => {
            if (none)
                return state.i++ === 0 && column.next !== "" ? -1 : column.none
            while (state.i < count) {
                const k = state.i++
                if (column.textOf(k, false) !== "")
                    return k
            }
            return column.none
        }
        const room = bottom - top
        curN = Math.min(curN, Math.floor(room / cp + 1e-6))
        const used = curN * cp
        const prevN = !none && line >= 1 ? wrap(line - 1, false).length : 0
        const after = none ? 0 : line + 1
        const more = nextIndex({ i: after }) !== column.none
        let slot = top
        if (prevN > 0 && used + prevN * np + (more ? np : 0) <= room + 1e-6) {
            out.push({ line: line - 1, big: false, top: slot, rows: prevN, pitch: np, alpha: Ui.alphaLow, texts: wrap(line - 1, false) })
            slot += prevN * np
        }
        if (curN > 0) {
            const k = none ? -1 : line
            out.push({ line: k, big: true, top: slot, rows: curN, pitch: cp, alpha: Ui.npTitle.alpha, texts: wrap(k, true) })
            slot += curN * cp
        }
        const state = { i: after }
        while (slot + np <= bottom + 1e-6) {
            const k = nextIndex(state)
            if (k === column.none)
                break
            const texts = wrap(k, false)
            out.push({ line: k, big: false, top: slot, rows: texts.length, pitch: np, alpha: Ui.npNext.alpha, texts: texts })
            const fit = Math.min(texts.length, Math.floor((bottom - slot) / np + 1e-6))
            if (fit < texts.length)
                return slot + fit * np
            slot += texts.length * np
        }
        return slot
    }

    function has(v: var, line: int): bool {
        for (const x of v) {
            if (x.line === line)
                return true
        }
        return false
    }

    function find(v: var, line: int): var {
        for (const x of v) {
            if (x.line === line)
                return x
        }
        return null
    }

    function offBlock(line: int, wrap: var): var {
        const texts = wrap(line, false)
        return { line: line, big: false, top: 0, rows: texts.length, pitch: Ui.npNextPitch, alpha: 0, texts: texts }
    }

    function pair(a: var, b: var, wrap: var): void {
        if (a.length === 0)
            return
        const first = a[0].line
        const last = a[a.length - 1].line
        for (let i = b.length - 1; i >= 0; i--) {
            if (b[i].line < first && !column.has(a, b[i].line)) {
                const v = column.offBlock(b[i].line, wrap)
                v.top = a[0].top - v.rows * v.pitch
                a.unshift(v)
            }
        }
        for (const x of b) {
            if (x.line > last && !column.has(a, x.line)) {
                const v = column.offBlock(x.line, wrap)
                const end = a[a.length - 1]
                v.top = end.top + end.rows * end.pitch
                a.push(v)
            }
        }
    }

    function blockHeight(x: var): real {
        return x.rows * x.pitch
    }

    function build(from: int, to: int, lines: var, title: string, next: string, width: real, u: real, top: real,
                   bottom: real, shown: bool): var {
        const empty = { a: [], b: [], visA: top, visB: top, draws: [], same: true, to: to }
        if (!shown || width <= 0)
            return empty
        const wrap = column.wrapper(width)
        const b = []
        const visB = column.blocks(to, b, wrap, top, bottom)
        if (from === to)
            return { a: b, b: b, visA: visB, visB: visB, draws: b.map((x) => ({ fa: x, fb: x, kind: "pair" })), same: true, to: to }
        const a = []
        const visA = column.blocks(from, a, wrap, top, bottom)
        column.pair(a, b, wrap)
        column.pair(b, a, wrap)
        const draws = []
        for (const x of a) {
            const y = column.find(b, x.line)
            if (y !== null)
                draws.push({ fa: x, fb: y, kind: "pair" })
        }
        for (let i = a.length - 1; i >= 0; i--) {
            if (column.find(b, a[i].line) !== null)
                continue
            draws.push({ fa: a[i], fb: null, kind: "out", anchor: i + 1 < a.length ? a[i + 1].line : null })
        }
        for (let i = 0; i < b.length; i++) {
            if (column.find(a, b[i].line) !== null)
                continue
            const gap = i > 0 ? b[i].top - (b[i - 1].top + column.blockHeight(b[i - 1])) : 0
            draws.push({ fa: null, fb: b[i], kind: "in", anchor: i > 0 ? b[i - 1].line : null, gap: gap })
        }
        return { a: a, b: b, visA: visA, visB: visB, draws: draws, same: false, to: to }
    }

    function edge(v: real): real {
        return Math.ceil(Ui.px(v) - 1e-6)
    }

    function place(plan: var, p: real): var {
        const half = 0.5
        const ds = plan.draws.map((d) => ({ d: d, top: 0, h: 0, side: 0, ao: 0, an: 0, y0: 0, y1: 0, swap: false }))
        const drawn = (line) => {
            for (const e of ds) {
                if (e.placed && ((e.d.fa !== null && e.d.fa.line === line) || (e.d.fb !== null && e.d.fb.line === line)))
                    return e
            }
            return null
        }
        if (plan.same) {
            for (const e of ds) {
                e.top = e.d.fb.top
                e.h = column.blockHeight(e.d.fb)
                e.placed = true
            }
        } else {
            let inn = null
            let nx = null
            for (const e of ds) {
                if (e.d.kind !== "pair")
                    continue
                e.top = Motion.lerp(e.d.fa.top, e.d.fb.top, p)
                e.h = Motion.lerp(column.blockHeight(e.d.fa), column.blockHeight(e.d.fb), p)
                e.placed = true
                if (e.d.fb.line === plan.to)
                    inn = e
                else if (e.d.fb.line > plan.to && nx === null)
                    nx = e
            }
            if (inn !== null) {
                const held = nx !== null && nx.d.fb.top > nx.d.fa.top
                if (held) {
                    for (const e of ds) {
                        if (e.d.kind !== "pair" || e.d.fb.line <= plan.to)
                            continue
                        e.side = p < half ? 1 : 2
                        e.top = p < half ? e.d.fa.top : e.d.fb.top
                        e.h = column.blockHeight(p < half ? e.d.fa : e.d.fb)
                    }
                }
                let lim = column.textBottom
                if (held)
                    lim = p < half ? nx.d.fa.top : nx.d.fb.top
                else if (nx !== null)
                    lim = Motion.lerp(nx.d.fa.top, nx.d.fb.top, p)
                inn.h = Math.min(inn.top + column.blockHeight(p < half ? inn.d.fa : inn.d.fb), lim) - inn.top
            }
            for (const e of ds) {
                if (e.d.kind === "out") {
                    e.h = column.blockHeight(e.d.fa) * (1 - p)
                    e.top = e.d.fa.top
                    const r = e.d.anchor === null ? null : drawn(e.d.anchor)
                    if (r !== null)
                        e.top = r.top - e.h
                    e.placed = true
                } else if (e.d.kind === "in") {
                    e.top = e.d.fb.top
                    const r = e.d.anchor === null ? null : drawn(e.d.anchor)
                    if (r !== null)
                        e.top = r.top + r.h + e.d.gap
                    e.h = column.blockHeight(e.d.fb) * p
                    e.placed = true
                }
            }
        }
        const y0 = column.edge(column.textTop)
        const y1 = column.edge(Math.max(plan.visA, plan.visB))
        for (const e of ds) {
            const fa = e.d.fa
            const fb = e.d.fb
            e.y0 = Math.max(y0, column.edge(e.top))
            e.y1 = Math.max(e.y0, Math.min(y1, column.edge(e.top + e.h)))
            const change = fa !== null && fb !== null && fa.big !== fb.big
            e.swap = change || e.side !== 0
            if (fa !== null && fb !== null && !e.swap) {
                e.an = Motion.lerp(fa.alpha, fb.alpha, p)
            } else if (e.swap) {
                e.ao = p < half ? fa.alpha * (1 - 2 * p) : 0
                e.an = p < half ? 0 : fb.alpha * (2 * p - 1)
            } else if (fa !== null) {
                e.ao = fa.alpha * (1 - p)
            } else {
                e.an = fb.alpha * p
            }
        }
        return ds
    }

    function wrapPlain(plain: var, width: real, u: real, shown: bool): var {
        const rows = []
        if (!shown || width <= 0)
            return rows
        const f = Ui.font(column.plainRole)
        for (const t of plain) {
            const s = String(t)
            if (s === "") {
                rows.push("")
                continue
            }
            for (const w of Env.wrapLines(s, f, width))
                rows.push(w)
        }
        return rows
    }

    function rowBaseline(block: var, i: int): real {
        const role = block.big ? Ui.npTitle : Ui.npNext
        return block.top + i * block.pitch + (block.pitch + Ui.capHeight * role.size) / 2
    }

    component Form: Item {
        id: form

        property var block: null
        property real rowTop: 0
        property real clipTop: 0
        readonly property var role: form.block !== null && form.block.big ? Ui.npTitle : Ui.npNext
        readonly property var rows: form.block === null ? []
            : form.block.big ? form.block.texts.slice(0, form.block.rows) : form.block.texts

        anchors.fill: parent

        Repeater {
            model: form.rows

            Text {
                id: rowText

                required property int index
                required property var modelData

                x: 0
                y: Ui.px(form.rowTop + rowText.index * form.block.pitch
                    + (form.block.pitch + Ui.capHeight * form.role.size) / 2) - rowText.baselineOffset - form.clipTop
                text: rowText.modelData
                color: Ui.textFull
                font: Ui.font(form.role)
            }
        }
    }

    Item {
        id: timed

        readonly property string touchKind: column.visible ? "lyrics" : ""

        x: Ui.px(column.textLeft)
        y: Ui.px(column.textTop)
        width: column.wrapWidth
        height: Math.max(0, Ui.px(column.textBottom - column.textTop))
        visible: column.timedShown
    }

    Repeater {
        id: drawRepeater

        model: column.timedShown ? column.plan.draws : []

        Item {
            id: drawItem

            required property int index
            required property var modelData
            readonly property var placed: column.frame[drawItem.index] === undefined ? null : column.frame[drawItem.index]
            readonly property real placedTop: drawItem.placed === null ? 0 : drawItem.placed.top

            x: Ui.px(column.textLeft)
            y: drawItem.placed === null ? 0 : drawItem.placed.y0
            width: column.wrapWidth
            height: drawItem.placed === null ? 0 : drawItem.placed.y1 - drawItem.placed.y0
            clip: true

            Form {
                block: drawItem.modelData.fa
                rowTop: drawItem.placedTop
                clipTop: drawItem.y
                visible: drawItem.placed !== null && drawItem.placed.swap
                opacity: drawItem.placed === null ? 0 : drawItem.placed.ao
            }

            Form {
                block: drawItem.placed !== null && drawItem.placed.swap ? drawItem.modelData.fb
                    : drawItem.modelData.fb !== null ? drawItem.modelData.fb : drawItem.modelData.fa
                rowTop: drawItem.placedTop
                clipTop: drawItem.y
                opacity: drawItem.placed === null ? 0
                    : drawItem.placed.swap || drawItem.modelData.fb !== null ? drawItem.placed.an : drawItem.placed.ao
            }
        }
    }

    Flickable {
        id: plainView

        readonly property string touchKind: column.visible ? "lyrics" : ""
        readonly property bool touchScroll: true
        property alias touch: plainScroller

        x: Ui.px(column.textLeft)
        y: Ui.px(column.textTop)
        width: column.wrapWidth
        height: Math.max(0, Ui.px(column.plainBottom - column.textTop))
        visible: column.plainShown
        contentWidth: plainView.width
        contentHeight: column.plainRows.length * Ui.px(Ui.npPlainPitch)
        interactive: false
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Repeater {
            id: plainRepeater

            model: column.plainRows

            Text {
                id: plainText

                required property int index
                required property var modelData

                x: 0
                y: Ui.px(plainText.index * Ui.npPlainPitch + (Ui.npPlainPitch + Ui.capHeight * column.plainRole.size) / 2)
                    - plainText.baselineOffset
                text: plainText.modelData
                color: Ui.white(column.plainRole.alpha)
                font: Ui.font(column.plainRole)
            }
        }
    }

    TouchScroll {
        id: plainScroller

        flick: plainView
    }

    onPlainRowsChanged: {
        plainScroller.stop()
        plainView.contentY = 0
    }
}
