pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    HomeShot {
        anchors.fill: parent
    }

    SwitchPrompt {
        id: prompt

        anchors.fill: parent
        Component.onCompleted: {
            prompt.show(true)
            prompt.complete()
        }
    }
}
