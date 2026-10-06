pragma ComponentBehavior: Bound
import QtQuick

PressScale {
    id: root

    property string primary: ""
    property string secondary: ""
    property string number: ""
    property string trailing: ""
    property bool focused: false
    property bool playing: false
    property bool playingDot: false
    property bool bold: false
    property bool countLine: false
    property bool ring: false
    property string ringGlyph: "play"
    property bool animated: true

    readonly property real indent: Ui.songIndent
    readonly property bool indented: root.number !== "" || root.ring
    readonly property real textX: Ui.px(Ui.hubEdge + (root.indented ? root.indent : 0))
    readonly property var primaryRole: root.bold ? Ui.listPlaylist : Ui.listPrimary
    readonly property var secondaryRole: root.bold || root.countLine ? Ui.listCount : Ui.listSecondary
    readonly property real primaryAlpha: root.focused ? root.primaryRole.alpha : root.primaryRole.alphaDim
    readonly property color primaryColor: root.playing ? Ui.accent : Ui.white(root.primaryAlpha)
    readonly property real primaryBaseline: primaryText.y + primaryText.baselineOffset
    readonly property real secondaryBaseline: secondaryText.y + secondaryText.baselineOffset
    readonly property Item primaryItem: primaryText
    readonly property Item secondaryItem: secondaryText
    readonly property Item trailingItem: trailingText
    readonly property Item dotItem: dot
    readonly property Item ringItem: rowRing

    width: Ui.windowW
    height: Ui.px(Ui.rowHeight)

    Rectangle {
        id: dot

        visible: root.playingDot
        x: Ui.px(Ui.hubEdge - Ui.playingDotOffset)
        anchors.baseline: primaryText.baseline
        anchors.baselineOffset: -Ui.px(Ui.playingDot)
        width: Ui.px(Ui.playingDot)
        height: Ui.px(Ui.playingDot)
        radius: dot.width / 2
        color: Ui.accent
    }

    Ring {
        id: rowRing

        visible: root.ring
        x: Ui.px(Ui.hubEdge)
        anchors.verticalCenter: parent.verticalCenter
        size: Ui.rowRing
        glyph: root.ringGlyph
        focused: root.focused
        animated: root.animated
    }

    Text {
        id: numberText

        visible: root.number !== ""
        x: Ui.px(Ui.hubEdge)
        anchors.baseline: primaryText.baseline
        text: root.number
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }

    Text {
        id: primaryText

        x: root.textX
        y: root.secondary !== "" ? Ui.px(Ui.rowTextInset) : (root.height - primaryText.height) / 2
        width: trailingText.visible ? trailingText.x - Ui.px(Ui.gutter) - primaryText.x : root.width - Ui.px(Ui.hubEdge) - primaryText.x
        text: root.primary
        color: root.primaryColor
        font: Ui.font(root.primaryRole)
        clip: true

        Behavior on color {
            enabled: root.animated
            ColorAnimation {
                duration: Motion.focusAlpha
            }
        }
    }

    Text {
        id: secondaryText

        visible: root.secondary !== ""
        x: root.textX
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Ui.px(Ui.rowTextInset)
        width: root.width - Ui.px(Ui.hubEdge) - secondaryText.x
        text: root.secondary
        color: Ui.white(root.secondaryRole.alpha)
        font: Ui.font(root.secondaryRole)
        clip: true
    }

    Text {
        id: trailingText

        visible: root.trailing !== ""
        x: root.width - Ui.px(Ui.hubEdge) - trailingText.width
        anchors.baseline: primaryText.baseline
        text: root.trailing
        color: Ui.white(Ui.listCount.alpha)
        font: Ui.font(Ui.listCount)
    }
}
