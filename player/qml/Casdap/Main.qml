import QtQuick

Window {
    id: root

    property bool screenshot: false
    property int shotWidth: Screen.width
    property int shotHeight: Screen.height

    width: shotWidth
    height: shotHeight
    visible: true
    visibility: screenshot ? Window.Windowed : Window.FullScreen
    color: "#111111"
}
