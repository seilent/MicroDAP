pragma ComponentBehavior: Bound
import QtQuick

PressScale {
    id: root

    property real size: Ui.tileS
    property url source: ""
    property string title: ""
    property string subtitle: ""
    property bool focused: false
    property bool captioned: true
    property bool animated: true

    readonly property real borderWidth: Ui.px(Ui.focusBorder)
    readonly property real captionX: Ui.px(root.size + Ui.tileCaptionX)
    readonly property real captionWidth: Ui.px(root.size + Ui.gutter - Ui.tileCaptionX)
    readonly property real captionAlpha: root.focused ? Ui.alphaFull : Ui.gridCaption.alpha
    readonly property Item captionItem: caption

    width: Ui.px(root.size)
    height: Ui.px(root.size)

    Rectangle {
        anchors.fill: parent
        color: Ui.tile

        Image {
            anchors.fill: parent
            source: root.source
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            mipmap: false
            sourceSize: Qt.size(root.width, root.height)
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -root.borderWidth
        color: "transparent"
        border.width: root.borderWidth
        border.color: Ui.textFull
        visible: root.focused
    }

    Item {
        id: caption

        visible: root.captioned
        x: root.captionX
        anchors.verticalCenter: parent.verticalCenter
        width: root.captionWidth
        height: lines.height
        clip: true

        Column {
            id: lines

            Text {
                text: root.title
                color: Ui.white(root.captionAlpha)
                font: Ui.font(Ui.gridCaption)

                Behavior on color {
                    enabled: root.animated
                    ColorAnimation {
                        duration: Motion.focusAlpha
                    }
                }
            }

            Text {
                text: root.subtitle
                color: Ui.white(root.captionAlpha)
                font: Ui.font(Ui.gridCaption)

                Behavior on color {
                    enabled: root.animated
                    ColorAnimation {
                        duration: Motion.focusAlpha
                    }
                }
            }
        }
    }
}
