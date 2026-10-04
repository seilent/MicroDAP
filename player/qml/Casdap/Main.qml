pragma ComponentBehavior: Bound
import QtQuick

Window {
    id: root

    property bool screenshot: false
    property string shotScreen: ""
    property real shotAngle: 40
    property int shotWidth: Screen.width
    property int shotHeight: Screen.height

    width: shotWidth
    height: shotHeight
    visible: true
    visibility: screenshot ? Window.Windowed : Window.FullScreen
    color: Ui.bg

    onWidthChanged: Ui.windowW = root.width
    onHeightChanged: Ui.windowH = root.height
    Component.onCompleted: {
        Ui.windowW = root.width
        Ui.windowH = root.height
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.shotScreen === "specimen" ? specimenComponent
            : root.shotScreen === "motion" ? motionComponent
            : null
    }

    Component {
        id: specimenComponent
        Specimen {}
    }

    Component {
        id: motionComponent
        MotionShot {
            angle: root.shotAngle
        }
    }
}
