import QtQuick
import QtTest
import MicroDap

TestCase {
    id: tc

    name: "Motion"
    when: windowShown
    visible: true
    width: 640
    height: 960

    Turnstile {
        id: turnstile

        width: 640
        height: 960
    }

    PageStack {
        id: stack

        width: 640
        height: 960
    }

    Component {
        id: page

        Rectangle {
            property string label: ""
            color: "#202020"
        }
    }

    function init() {
        stack.clear()
        stack.animate = true
        turnstile.angle = 0
        turnstile.hinge = 0
        turnstile.axis = Qt.YAxis
        turnstile.width = 640
    }

    function test_curves() {
        const t = 0.5
        fuzzyCompare(Motion.expOut6(t), 1 - (Math.exp(6 * (1 - t)) - 1) / (Math.exp(6) - 1), 1e-6)
        fuzzyCompare(Motion.expIn6(t), (Math.exp(6 * t) - 1) / (Math.exp(6) - 1), 1e-6)
        fuzzyCompare(Motion.expIn1(t), (Math.exp(t) - 1) / (Math.exp(1) - 1), 1e-6)
        fuzzyCompare(Motion.expIn15(t), (Math.exp(15 * t) - 1) / (Math.exp(15) - 1), 1e-6)
        fuzzyCompare(Motion.expOut6(0), 0, 1e-9)
        fuzzyCompare(Motion.expOut6(1), 1, 1e-9)
        fuzzyCompare(Motion.expIn6(0), 0, 1e-9)
        fuzzyCompare(Motion.expIn6(1), 1, 1e-9)
        fuzzyCompare(Motion.expOut(t, 6), Motion.expOut6(t), 1e-12)
        fuzzyCompare(Motion.expIn(t, 6), Motion.expIn6(t), 1e-12)
        compare(Motion.decel, [0.1, 0.9, 0.2, 1.0, 1, 1])
        compare(Motion.strip, [0.2, 0.8, 0.2, 1, 1, 1])
        compare(Motion.turnstileOut, 250)
        compare(Motion.turnstileIn, 350)
        compare(Motion.turnstileInAngle, -80)
        compare(Motion.turnstileOutAngle, 50)
        fuzzyCompare(Motion.perspective(640), 1472, 1e-9)
    }

    function test_projection_data() {
        return [
            { tag: "640", w: 640 },
            { tag: "960", w: 960 },
            { tag: "1920", w: 1920 }
        ]
    }

    function test_projection(data) {
        turnstile.width = data.w
        fuzzyCompare(turnstile.perspective, 2.3 * data.w, 1e-9)
        for (const angle of [80, -80]) {
            turnstile.angle = angle
            const far = turnstile.projectedW(data.w, 0)
            verify(far >= 0.57 && far <= 1.43, "angle " + angle + " w' " + far)
            const near = turnstile.projectedW(0, 0)
            fuzzyCompare(near, 1, 1e-9)
            const m = turnstile.currentMatrix
            const v = m.times(Qt.vector4d(data.w, 0, 0, 1))
            fuzzyCompare(v.w, far, 1e-6)
            const hingePoint = m.times(Qt.vector4d(0, 100, 0, 1))
            fuzzyCompare(hingePoint.x / hingePoint.w, 0, 1e-6)
            fuzzyCompare(hingePoint.y / hingePoint.w, 100, 1e-6)
        }
        turnstile.angle = 0
        const identity = turnstile.currentMatrix.times(Qt.vector4d(data.w, 50, 0, 1))
        fuzzyCompare(identity.x, data.w, 1e-6)
        fuzzyCompare(identity.y, 50, 1e-6)
        fuzzyCompare(identity.w, 1, 1e-9)
    }

    function test_projection_hinge() {
        turnstile.hinge = 320
        turnstile.angle = 80
        fuzzyCompare(turnstile.projectedW(320, 0), 1, 1e-9)
        const m = turnstile.currentMatrix
        const p = m.times(Qt.vector4d(320, 10, 0, 1))
        fuzzyCompare(p.x / p.w, 320, 1e-6)
        verify(turnstile.projectedW(640, 0) > 1)
        verify(turnstile.projectedW(0, 0) < 1)
    }

    function test_swivel() {
        turnstile.axis = Qt.XAxis
        turnstile.hinge = 480
        turnstile.angle = -30
        const m = turnstile.currentMatrix
        const p = m.times(Qt.vector4d(100, 480, 0, 1))
        fuzzyCompare(p.y / p.w, 480, 1e-6)
        fuzzyCompare(p.x / p.w, 100, 1e-6)
        fuzzyCompare(m.times(Qt.vector4d(0, 960, 0, 1)).w, turnstile.projectedW(0, 960), 1e-6)
    }

    function test_first_push_no_transition() {
        const a = stack.push(page, { label: "a" })
        compare(stack.depth, 1)
        compare(stack.currentItem, a)
        compare(stack.inputLocked, false)
        compare(stack.phase, "idle")
        compare(a.parent.opacity, 1)
        compare(a.parent.angle, 0)
        compare(a.width, 640)
        compare(a.height, 960)
    }

    function test_push_sequence() {
        const a = stack.push(page, { label: "a" })
        const b = stack.push(page, { label: "b" })
        compare(stack.depth, 2)
        compare(stack.inputLocked, true)
        compare(stack.phase, "exit")
        compare(stack.incoming, b.parent)
        compare(stack.outgoing, a.parent)
        compare(b.parent.visible, true)
        compare(b.parent.opacity, 0)
        fuzzyCompare(b.parent.angle, Motion.turnstileInAngle, 1e-6)
        compare(a.parent.visible, true)
        compare(a.parent.opacity, 1)
        wait(50)
        if (stack.phase === "exit") {
            compare(b.parent.opacity, 0)
            verify(a.parent.angle >= 0)
        }
        tryCompare(stack, "phase", "enter", 2000)
        compare(a.parent.visible, false)
        tryVerify(function() { return b.parent.opacity > 0 }, 1000)
        tryCompare(stack, "phase", "idle", 2000)
        compare(stack.inputLocked, false)
        compare(b.parent.opacity, 1)
        fuzzyCompare(b.parent.angle, 0, 1e-6)
        compare(a.parent.visible, false)
        compare(stack.currentItem, b)
    }

    function test_completeAll_push() {
        const a = stack.push(page, { label: "a" })
        const b = stack.push(page, { label: "b" })
        compare(stack.inputLocked, true)
        stack.completeAll()
        compare(stack.inputLocked, false)
        compare(stack.phase, "idle")
        compare(b.parent.visible, true)
        compare(b.parent.opacity, 1)
        fuzzyCompare(b.parent.angle, 0, 1e-6)
        compare(a.parent.visible, false)
        compare(stack.currentItem, b)
        compare(stack.incoming, null)
        compare(stack.outgoing, null)
    }

    function test_completeAll_mid_enter() {
        const a = stack.push(page, { label: "a" })
        const b = stack.push(page, { label: "b" })
        tryCompare(stack, "phase", "enter", 2000)
        stack.completeAll()
        compare(stack.phase, "idle")
        compare(b.parent.opacity, 1)
        fuzzyCompare(b.parent.angle, 0, 1e-6)
        compare(a.parent.visible, false)
    }

    function test_pop_sequence() {
        const a = stack.push(page, { label: "a" })
        const b = stack.push(page, { label: "b" })
        stack.completeAll()
        const bHolder = b.parent
        stack.pop()
        compare(stack.depth, 1)
        compare(stack.inputLocked, true)
        compare(stack.phase, "exit")
        compare(stack.outgoing, bHolder)
        compare(stack.incoming, a.parent)
        compare(a.parent.visible, true)
        compare(a.parent.opacity, 0)
        fuzzyCompare(a.parent.angle, Motion.turnstileOutAngle, 1e-6)
        compare(bHolder.opacity, 1)
        tryCompare(stack, "phase", "enter", 2000)
        tryCompare(stack, "phase", "idle", 2000)
        compare(a.parent.visible, true)
        compare(a.parent.opacity, 1)
        fuzzyCompare(a.parent.angle, 0, 1e-6)
        compare(stack.currentItem, a)
    }

    function test_pop_completeAll() {
        const a = stack.push(page, { label: "a" })
        stack.push(page, { label: "b" })
        stack.completeAll()
        stack.pop()
        stack.completeAll()
        compare(stack.depth, 1)
        compare(stack.phase, "idle")
        compare(a.parent.visible, true)
        compare(a.parent.opacity, 1)
        fuzzyCompare(a.parent.angle, 0, 1e-6)
    }

    function test_pop_bottom_noop() {
        stack.push(page, { label: "a" })
        stack.pop()
        compare(stack.depth, 1)
        compare(stack.phase, "idle")
    }
}
