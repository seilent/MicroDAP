pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: page

    property PageStack stack: null
    property FocusList list: null
    property int focusIndex: 0
    property int focusCount: page.list !== null ? page.list.count : 0
    property bool pressed: false
    readonly property bool active: page.stack !== null && page.stack.currentItem === page
    readonly property real dialogFade: page.stack !== null ? page.stack.dialogFade : 0
    readonly property real focusOpacity: 1 - page.dialogFade

    signal action(string name, bool repeat)
    signal activated(int index)

    function focusItem(): Item {
        return page.list !== null ? page.list.focusItem() : null
    }

    function handleAction(name: string, repeat: bool): bool {
        switch (name) {
        case "up":
            return page.moveFocus(-1)
        case "down":
            return page.moveFocus(1)
        case "lt":
            return page.pageFocus(-1)
        case "rt":
            return page.pageFocus(1)
        case "a":
            if (!repeat)
                page.activated(page.focusIndex)
            return true
        case "b":
            if (repeat || page.stack === null || page.stack.depth < 2)
                return false
            page.stack.pop()
            return true
        default:
            return false
        }
    }

    function moveFocus(delta: int): bool {
        if (page.list !== null) {
            const moved = page.list.moveFocus(delta)
            page.focusIndex = page.list.focusIndex
            return moved
        }
        if (page.focusCount <= 0)
            return false
        page.focusIndex = Math.max(0, Math.min(page.focusCount - 1, page.focusIndex + delta))
        return true
    }

    function pageFocus(direction: int): bool {
        if (page.list === null)
            return false
        const moved = page.list.pageFocus(direction)
        page.focusIndex = page.list.focusIndex
        return moved
    }

    function setFocus(index: int) {
        if (page.list !== null) {
            page.list.snapTo(index)
            page.focusIndex = page.list.focusIndex
            return
        }
        page.focusIndex = Math.max(0, Math.min(Math.max(0, page.focusCount - 1), index))
    }

    function dispatch(name: string, repeat: bool): bool {
        page.action(name, repeat)
        return page.handleAction(name, repeat)
    }

    function press(name: string) {
        if (name === "a")
            page.pressed = true
    }

    function release(name: string) {
        if (name === "a")
            page.pressed = false
    }

    Component.onCompleted: {
        if (page.stack !== null || page.parent === null)
            return
        const owner = page.parent.parent
        if (owner !== null && owner !== undefined && typeof owner.pop === "function" && typeof owner.completeAll === "function")
            page.stack = owner
    }
}
