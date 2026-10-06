pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool animate: true
    property real dialogFade: 0
    property var pages: []
    readonly property int depth: root.pages.length
    readonly property Item currentItem: root.pages.length > 0 ? root.pages[root.pages.length - 1].content : null
    readonly property Item incoming: d.incoming
    readonly property Item outgoing: d.outgoing
    readonly property string phase: d.phase
    readonly property string transitionKind: d.kind
    readonly property bool inputLocked: d.phase !== "idle"

    component Holder: Turnstile {
        id: holder

        property Item content: null
        property bool entering: true
        property string kind: "turnstile"
        property string pushKind: "turnstile"
        property int easingType: Easing.Linear
        property real fromAngle: 0
        property real toAngle: 0
        property int duration: Motion.turnstileIn
        property real progress: 1
        readonly property real elapsed: holder.progress * holder.duration
        readonly property bool fade: holder.kind === "fade"

        anchors.fill: holder.parent
        angle: holder.fromAngle + (holder.toAngle - holder.fromAngle)
            * (holder.entering ? Motion.expOut6(holder.progress) : Motion.expIn6(holder.progress))
        opacity: holder.fade
            ? (holder.entering ? holder.progress : 1 - holder.progress)
            : holder.entering
                ? Math.min(1, holder.elapsed / Motion.turnstileInFade)
                : (holder.elapsed < Motion.turnstileOutHold ? 1 : 0)
    }

    Component {
        id: holderComponent
        Holder {}
    }

    QtObject {
        id: d

        property Holder outgoing: null
        property Holder incoming: null
        property bool removeOutgoing: false
        property string phase: "idle"
        property string kind: "turnstile"

        function enterEasing(holder): int {
            if (holder.content !== null && holder.content.enterEasing !== undefined)
                return holder.content.enterEasing
            return Motion.pop
        }

        function begin(from, to, outTo, inFrom, remove, kind) {
            d.outgoing = from
            d.incoming = to
            d.removeOutgoing = remove
            d.kind = kind
            const fade = kind === "fade"
            from.kind = kind
            from.entering = false
            from.fromAngle = 0
            from.toAngle = fade ? 0 : outTo
            from.duration = fade ? Motion.nowPlayingFade : Motion.turnstileOut
            from.easingType = fade ? Motion.pop : Easing.Linear
            from.progress = 0
            from.visible = true
            to.kind = kind
            to.entering = true
            to.fromAngle = fade ? 0 : inFrom
            to.toAngle = 0
            to.duration = fade ? Motion.nowPlayingIn : Motion.turnstileIn
            to.easingType = fade ? d.enterEasing(to) : Easing.Linear
            to.progress = 0
            to.visible = true
            d.phase = "exit"
            transition.start()
        }

        function showIncoming() {
            if (d.outgoing)
                d.outgoing.visible = false
            d.phase = "enter"
        }

        function finish() {
            if (d.outgoing) {
                d.outgoing.visible = false
                if (d.removeOutgoing)
                    d.outgoing.destroy()
            }
            d.outgoing = null
            d.incoming = null
            d.removeOutgoing = false
            d.phase = "idle"
        }
    }

    SequentialAnimation {
        id: transition

        NumberAnimation {
            target: d.outgoing
            property: "progress"
            from: 0
            to: 1
            duration: d.outgoing !== null ? d.outgoing.duration : Motion.turnstileOut
            easing.type: d.outgoing !== null ? d.outgoing.easingType : Easing.Linear
        }
        ScriptAction {
            script: d.showIncoming()
        }
        NumberAnimation {
            target: d.incoming
            property: "progress"
            from: 0
            to: 1
            duration: d.incoming !== null ? d.incoming.duration : Motion.turnstileIn
            easing.type: d.incoming !== null ? d.incoming.easingType : Easing.Linear
        }
        onFinished: d.finish()
    }

    function push(component, properties, kind) {
        if (d.phase !== "idle")
            root.completeAll()
        const transitionKind = kind === undefined ? "turnstile" : kind
        const holder = holderComponent.createObject(root)
        const content = component.createObject(holder, properties === undefined ? {} : properties)
        content.anchors.fill = holder
        holder.content = content
        holder.pushKind = transitionKind
        holder.kind = transitionKind
        const previous = root.pages.length > 0 ? root.pages[root.pages.length - 1] : null
        root.pages = root.pages.concat([holder])
        if (previous === null || !root.animate) {
            holder.progress = 1
            holder.visible = true
            if (previous !== null)
                previous.visible = false
            return content
        }
        d.begin(previous, holder, Motion.turnstileOutAngle, Motion.turnstileInAngle, false, transitionKind)
        return content
    }

    function pop() {
        if (root.pages.length < 2)
            return
        if (d.phase !== "idle")
            root.completeAll()
        const top = root.pages[root.pages.length - 1]
        const below = root.pages[root.pages.length - 2]
        root.pages = root.pages.slice(0, root.pages.length - 1)
        if (!root.animate) {
            below.progress = 1
            below.visible = true
            top.destroy()
            return
        }
        d.begin(top, below, Motion.turnstileInAngle, Motion.turnstileOutAngle, true, top.pushKind)
    }

    function completeAll() {
        if (d.phase === "idle")
            return
        transition.stop()
        if (d.outgoing)
            d.outgoing.progress = 1
        if (d.incoming) {
            d.incoming.progress = 1
            d.incoming.visible = true
        }
        d.finish()
    }

    function serialize(): var {
        const out = []
        for (const holder of root.pages) {
            const content = holder.content
            if (content === null || content === undefined)
                continue
            const name = String(content.objectName)
            if (name === "")
                continue
            const id = content.pageId === undefined ? -1 : Number(content.pageId)
            out.push({ name: name, id: id })
        }
        return out
    }

    function clear() {
        root.completeAll()
        const holders = root.pages
        root.pages = []
        for (const holder of holders)
            holder.destroy()
    }
}
