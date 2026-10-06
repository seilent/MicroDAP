pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    HomeShot {
        anchors.fill: parent
    }

    PowerMenu {
        id: menu

        anchors.fill: parent
        Component.onCompleted: {
            menu.open()
            menu.complete()
        }
    }
}
