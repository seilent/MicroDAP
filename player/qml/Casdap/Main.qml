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

    PageStack {
        id: pages

        anchors.fill: parent
        visible: !root.screenshot
    }

    PadRouter {
        id: router

        anchors.fill: parent
        stack: pages
        pad: typeof Pad === "undefined" ? null : Pad
        player: typeof Player === "undefined" ? null : Player
        screenOff: typeof PowerBridge === "undefined" ? false : PowerBridge.screenOff
        pocketControls: typeof Settings === "undefined" ? false : Settings.pocketControls
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.shotScreen === "specimen" ? specimenComponent
            : root.shotScreen === "motion" ? motionComponent
            : root.shotScreen === "components" ? componentsComponent
            : root.shotScreen === "overlays" ? overlaysComponent
            : root.shotScreen === "volume" ? volumeComponent
            : null
    }

    Component {
        id: componentsComponent
        ComponentsShot {}
    }

    Component {
        id: overlaysComponent
        OverlaysShot {}
    }

    Component {
        id: volumeComponent
        VolumeShot {}
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
