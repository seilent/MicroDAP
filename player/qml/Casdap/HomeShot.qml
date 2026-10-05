pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property bool quickplay: false

    readonly property var contextBluez: typeof BluezModel === "undefined" ? null : BluezModel
    readonly property var contextRecent: typeof HistoryRecent === "undefined" ? null : HistoryRecent
    readonly property var contextNew: typeof HistoryNew === "undefined" ? null : HistoryNew

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    ListModel {
        id: sampleHistory

        Component.onCompleted: {
            const rows = [
                ["This Could Be Our Day", "Addison Road"],
                ["All That Matters", "Addison Road"],
                ["Hoshi no Kakera", "ふるーり"],
                ["Yume", "わかばやし"],
                ["Track Five", "Fixture Artist"],
                ["Track Six", "Fixture Artist"]
            ]
            for (let i = 0; i < rows.length; i++)
                sampleHistory.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], artPath: "" })
        }
    }

    ListModel {
        id: sampleNew

        Component.onCompleted: {
            sampleNew.append({ id: 1, title: "HARMONICS vol.2", subtitle: "Various Artists", artPath: "" })
            sampleNew.append({ id: 2, title: "Fixture Album", subtitle: "Fixture Artist", artPath: "" })
        }
    }

    Home {
        id: home

        anchors.fill: parent
        animated: false
        bluez: root.contextBluez
        historyModel: root.contextRecent !== null ? root.contextRecent : sampleHistory
        newModel: root.contextNew !== null ? root.contextNew : sampleNew

        Component.onCompleted: {
            if (root.quickplay)
                home.showQuickplay()
            home.completeAll()
        }
    }
}
