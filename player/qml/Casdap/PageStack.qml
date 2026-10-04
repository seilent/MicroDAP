pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool animate: true
    property var pages: []
    readonly property int depth: root.pages.length
    readonly property Item currentItem: root.pages.length > 0 ? root.pages[root.pages.length - 1].content : null
    readonly property Item incoming: d.incoming
    readonly property Item outgoing: d.outgoing
    readonly property string phase: d.phase
    readonly property bool inputLocked: d.phase !== "idle"

    component Holder: Turnstile {
        id: holder

        property Item content: null
        property bool entering: true
        property real fromAngle: 0
        property real toAngle: 0
        property int duration: Motion.turnstileIn
        property real progress: 1
        readonly property real elapsed: holder.progress * holder.duration

        anchors.fill: holder.parent
        angle: holder.fromAngle + (holder.toAngle - holder.fromAngle)
            * (holder.entering ? Motion.expOut6(holder.progress) : Motion.expIn6(holder.progress))
        opacity: holder.entering
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

        function begin(from, to, outTo, inFrom, remove) {
            d.outgoing = from
            d.incoming = to
            d.removeOutgoing = remove
            from.entering = false
            from.fromAngle = 0
            from.toAngle = outTo
            from.duration = Motion.turnstileOut
            from.progress = 0
            from.visible = true
            to.entering = true
            to.fromAngle = inFrom
            to.toAngle = 0
            to.duration = Motion.turnstileIn
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
            duration: Motion.turnstileOut
        }
        ScriptAction {
            script: d.showIncoming()
        }
        NumberAnimation {
            target: d.incoming
            property: "progress"
            from: 0
            to: 1
            duration: Motion.turnstileIn
        }
        onFinished: d.finish()
    }

    function push(component, properties) {
        if (d.phase !== "idle")
            root.completeAll()
        const holder = holderComponent.createObject(root)
        const content = component.createObject(holder, properties === undefined ? {} : properties)
        content.anchors.fill = holder
        holder.content = content
        const previous = root.pages.length > 0 ? root.pages[root.pages.length - 1] : null
        root.pages = root.pages.concat([holder])
        if (previous === null || !root.animate) {
            holder.progress = 1
            holder.visible = true
            if (previous !== null)
                previous.visible = false
            return content
        }
        d.begin(previous, holder, Motion.turnstileOutAngle, Motion.turnstileInAngle, false)
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
        d.begin(top, below, Motion.turnstileInAngle, Motion.turnstileOutAngle, true)
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

    function clear() {
        root.completeAll()
        const holders = root.pages
        root.pages = []
        for (const holder of holders)
            holder.destroy()
    }
}
