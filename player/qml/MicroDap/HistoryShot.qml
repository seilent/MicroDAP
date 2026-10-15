pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string pivot: "recent"

    readonly property var contextRecent: typeof HistoryRecent === "undefined" ? null : HistoryRecent
    readonly property var contextNew: typeof HistoryNew === "undefined" ? null : HistoryNew

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    ListModel {
        id: sampleRecent

        Component.onCompleted: {
            const rows = [
                ["This Could Be Our Day", "Addison Road"],
                ["Casimir Pulaski Day", "Sufjan Stevens"],
                ["Big Yellow Taxi", "Counting Crows"],
                ["Chasing Cars", "Snow Patrol"],
                ["Hoshi no Kakera", "ふるーり"],
                ["Shine", "Collective Soul"],
                ["Always", "Erasure"]
            ]
            for (let i = 0; i < rows.length; i++)
                sampleRecent.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], artPath: "", kind: "song" })
        }
    }

    ListModel {
        id: sampleNew

        Component.onCompleted: {
            const rows = [
                ["HARMONICS vol.2", "Various Artists"],
                ["Addison Road", "Addison Road"],
                ["Chase This Light", "Jimmy Eat World"],
                ["All The Right Reasons", "Nickelback"],
                ["Around the Bend", "Randy Travis"]
            ]
            for (let i = 0; i < rows.length; i++)
                sampleNew.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], artPath: "", kind: "album" })
        }
    }

    HistoryScreen {
        id: screen

        anchors.fill: parent
        animated: false
        pivotIndex: root.pivot === "new" ? 1 : 0
        historyModel: root.contextRecent !== null ? root.contextRecent : sampleRecent
        newModel: root.contextNew !== null ? root.contextNew : sampleNew

        Component.onCompleted: screen.completeAll()
    }
}
