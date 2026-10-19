pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string fixture: "browse"

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

    QtObject {
        id: sampleLibrary

        function editable(id) {
            return id !== 4
        }

        function isPinned(kind, id) {
            return false
        }
    }

    PlaylistsScreen {
        id: screen

        anchors.fill: parent
        animated: root.fixture === "playlist-sheet" || root.fixture === "playlist-delete"
        playlistsModel: root.contextPlaylists !== null ? root.contextPlaylists : samplePlaylists
        player: samplePlayer
        library: root.contextPlaylists !== null ? null : sampleLibrary

        Component.onCompleted: {
            screen.completeAll()
            if (root.fixture === "playlist-sheet" || root.fixture === "playlist-delete")
                Qt.callLater(root.sheetFixture)
        }
    }

    function sheetFixture(): void {
        screen.completeAll()
        screen.handleAction("down", false)
        screen.openSheet()
        if (root.fixture === "playlist-delete") {
            screen.sheet.close()
            screen.confirmDelete(screen.sheetRow)
        }
        screen.completeAll()
        Qt.callLater(screen.completeAll)
    }
}
