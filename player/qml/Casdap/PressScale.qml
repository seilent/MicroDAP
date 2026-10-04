pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool pressed: false

    transformOrigin: Item.Center
    scale: root.pressed ? Motion.pressScale : 1

    Behavior on scale {
        NumberAnimation {
            duration: Motion.press
            easing.type: Motion.linear
        }
    }
}
