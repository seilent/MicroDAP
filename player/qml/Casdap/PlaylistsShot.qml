pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    readonly property var contextPlaylists: typeof Playlists === "undefined" ? null : Playlists

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    ListModel {
        id: samplePlaylists

        Component.onCompleted: {
            const rows = [
                ["Morning", "14 songs"],
                ["Night Drive", "22 songs"],
                ["Harmonics", "9 songs"],
                ["Workout", "31 songs"]
            ]
            for (let i = 0; i < rows.length; i++)
                samplePlaylists.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], kind: "playlist" })
        }
    }

    QtObject {
        id: samplePlayer

        readonly property int status: 1
        readonly property QtObject queue: QtObject {
            readonly property int count: 20
        }
    }

    PlaylistsScreen {
        id: screen

        anchors.fill: parent
        animated: false
        playlistsModel: root.contextPlaylists !== null ? root.contextPlaylists : samplePlaylists
        player: samplePlayer

        Component.onCompleted: screen.completeAll()
    }
}
