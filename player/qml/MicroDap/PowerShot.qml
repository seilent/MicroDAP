pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "browse"

    HomeShot {
        anchors.fill: parent
    }

    PowerMenu {
        id: menu

        anchors.fill: parent
        sessions: root.fixture === "power-none" ? []
            : root.fixture === "power-long" ? [{ name: "microemu", label: "MicroEMU Retro Handheld Library" }, { name: "steam", label: "Steam" }]
            : [{ name: "microemu", label: "MicroEMU" }, { name: "steam", label: "Steam" }]
        Component.onCompleted: {
            menu.open()
            menu.complete()
        }
    }
}
