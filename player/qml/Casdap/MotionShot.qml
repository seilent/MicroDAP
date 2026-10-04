pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property real angle: 40
    property int rows: 30

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    Turnstile {
        id: page

        anchors.fill: parent
        angle: root.angle
        hinge: 0

        Column {
            x: Ui.px(Ui.hubEdge)
            y: Ui.px(Ui.contentTop)

            Repeater {
                model: root.rows

                delegate: Item {
                    id: row

                    required property int index

                    width: page.width - 2 * Ui.px(Ui.hubEdge)
                    height: Ui.px(Ui.rowHeight)

                    Text {
                        y: Ui.px(4)
                        text: "Track " + (row.index + 1)
                        color: Ui.white(row.index === 0 ? Ui.listPrimary.alpha : Ui.listPrimary.alphaDim)
                        font: Ui.font(Ui.listPrimary)
                    }

                    Text {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: Ui.px(4)
                        text: "Artist " + (row.index + 1)
                        color: Ui.white(Ui.listSecondary.alpha)
                        font: Ui.font(Ui.listSecondary)
                    }

                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: Ui.border
                    }
                }
            }
        }
    }
}
