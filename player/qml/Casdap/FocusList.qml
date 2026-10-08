pragma ComponentBehavior: Bound
import QtQuick

ListView {
    id: list

    property int focusIndex: 0
    property real rowPitch: Ui.px(Ui.rowHeight)
    readonly property int pageRows: Math.max(1, Math.floor(list.height / list.rowPitch))
    readonly property real pageDistance: list.pageRows * list.rowPitch
    readonly property real maxContentY: Math.max(0, list.contentHeight - list.height)
    readonly property real focusY: list.focusIndex * list.rowPitch - list.contentY
    readonly property bool scrolling: scrollAnimation.running
    readonly property real scrollTarget: scrollAnimation.to
    readonly property int scrollDuration: scrollAnimation.duration

    function focusItem(): Item {
        return list.itemAtIndex(list.focusIndex)
    }

    interactive: false
    reuseItems: true
    currentIndex: -1
    keyNavigationEnabled: false
    boundsBehavior: Flickable.StopAtBounds
    cacheBuffer: Math.max(0, Math.ceil(list.height))
    clip: true

    NumberAnimation {
        id: scrollAnimation

        target: list
        property: "contentY"
        easing.type: Motion.easeIn
    }

    function clampIndex(index: int): int {
        return Math.max(0, Math.min(list.count - 1, index))
    }

    function visibleY(index: int, fromY: real): real {
        if (list.height <= 0)
            return Math.max(0, fromY)
        const top = index * list.rowPitch
        const low = top - (list.height - list.rowPitch)
        let y = fromY
        if (top < y)
            y = top
        else if (y < low)
            y = low
        return Math.max(0, Math.min(list.maxContentY, y))
    }

    function scrollTo(y: real, duration: int) {
        const target = Math.max(0, Math.min(list.maxContentY, y))
        scrollAnimation.stop()
        if (target === list.contentY)
            return
        scrollAnimation.duration = duration
        scrollAnimation.from = list.contentY
        scrollAnimation.to = target
        scrollAnimation.start()
    }

    function ensureVisible(duration: int) {
        const base = scrollAnimation.running ? scrollAnimation.to : list.contentY
        list.scrollTo(list.visibleY(list.focusIndex, base), duration)
    }

    function setFocus(index: int, duration: int): bool {
        if (list.count <= 0)
            return false
        list.focusIndex = list.clampIndex(index)
        list.ensureVisible(duration)
        return true
    }

    function moveFocus(delta: int): bool {
        return list.setFocus(list.focusIndex + delta, Motion.scroll)
    }

    function pageFocus(direction: int): bool {
        if (list.count <= 0)
            return false
        const base = scrollAnimation.running ? scrollAnimation.to : list.contentY
        list.focusIndex = list.clampIndex(list.focusIndex + direction * list.pageRows)
        list.scrollTo(list.visibleY(list.focusIndex, base + direction * list.pageDistance), Motion.page)
        return true
    }

    function snapTo(index: int) {
        if (list.count <= 0)
            return
        scrollAnimation.stop()
        list.focusIndex = list.clampIndex(index)
        list.contentY = list.visibleY(list.focusIndex, list.contentY)
    }

    function keepFocusVisible() {
        list.snapTo(list.focusIndex)
    }

    function completeScroll() {
        if (!scrollAnimation.running)
            return
        scrollAnimation.stop()
        list.contentY = scrollAnimation.to
    }

    onHeightChanged: list.snapTo(list.focusIndex)
}
