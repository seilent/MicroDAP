pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: sampleQueue

        readonly property var rows: [
            ["This Could Be Our Day", "Addison Road"],
            ["All That Matters", "Addison Road"],
            ["Sticking With You", "Addison Road"],
            ["Hope Now", "Addison Road"],
            ["Casimir Pulaski Day", "Sufjan Stevens"],
            ["Chicago", "Sufjan Stevens"],
            ["Big Yellow Taxi", "Counting Crows"],
            ["Chasing Cars", "Snow Patrol"],
            ["Run", "Snow Patrol"],
            ["Shine", "Collective Soul"],
            ["Always", "Erasure"],
            ["Kryptonite", "3 Doors Down"],
            ["Here Without You", "3 Doors Down"],
            ["The Middle", "Jimmy Eat World"],
            ["Sweetness", "Jimmy Eat World"],
            ["Photograph", "Nickelback"],
            ["Forever and Ever, Amen", "Randy Travis"],
            ["Hoshi no Kakera", "ふるーり"],
            ["Yume", "わかばやし"],
            ["Sweet Child O' Mine", "Guns N' Roses"]
        ]
        readonly property int count: sampleQueue.rows.length
        readonly property int index: 2
        readonly property int currentSlot: 2
        readonly property bool shuffle: false
        readonly property int repeat: 0
        readonly property var playOrder: sampleOrder

        function get(row: int): var {
            const r = sampleQueue.rows[row]
            return { id: row + 1, title: r[0], artist: r[1], album: "", artPath: "", durationMs: 240000 }
        }
    }

    ListModel {
        id: sampleOrder

        function fill(): void {
            sampleOrder.clear()
            for (let i = 0; i < sampleQueue.rows.length; i++) {
                const r = sampleQueue.rows[i]
                sampleOrder.append({ row: i, slot: i, number: i + 1, id: i + 1, title: r[0], artist: r[1], album: "", durationMs: 240000, isCurrent: i === sampleQueue.index })
            }
        }
    }

    QtObject {
        id: samplePlayer

        readonly property int status: 1
        readonly property QtObject queue: sampleQueue
    }

    QueueScreen {
        id: screen

        anchors.fill: parent
        animated: false
        player: samplePlayer

        Component.onCompleted: {
            sampleOrder.fill()
            screen.list.snapTo(sampleQueue.currentSlot)
            screen.syncFocus()
            screen.completeAll()
        }
    }
}
