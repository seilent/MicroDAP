pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    Flow {
        x: Ui.px(Ui.edge)
        y: Ui.px(Ui.edge)
        width: root.width - 2 * Ui.px(Ui.edge)
        height: root.height - 2 * Ui.px(Ui.edge)
        flow: Flow.TopToBottom
        spacing: Ui.px(8)

        Repeater {
            model: Ui.roleList

            delegate: Column {
                id: entry

                required property var modelData

                spacing: Ui.px(2)

                Text {
                    text: entry.modelData.name + " " + Ui.fontPx(entry.modelData.size) + " px " + entry.modelData.style
                    color: Ui.textLow
                    font: Ui.font(Ui.sectionLabel)
                }

                Item {
                    id: box

                    readonly property bool cropped: entry.modelData.cropped === true

                    width: sample.implicitWidth
                    height: box.cropped ? Ui.px(Ui.headerBand) : sample.implicitHeight
                    clip: box.cropped

                    Text {
                        id: sample

                        y: box.cropped ? Ui.px(Ui.headerXHeightBottom) - sample.baselineOffset : 0
                        text: entry.modelData.sample
                        color: Ui.white(entry.modelData.alpha)
                        font: Ui.font(entry.modelData)
                    }
                }
            }
        }
    }
}
