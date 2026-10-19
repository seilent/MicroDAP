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

    readonly property bool viewShot: root.fixture.startsWith("view")
    property bool viewLive: false

    QtObject {
        id: sampleLibrary

        readonly property var titles: ["Sticking With You", "Hope Now", "What Do I Know", "Fallen Away", "This Love",
            "Change In My Life", "Live Like That", "Somebody Out There", "Your Love Is Life", "Tell Me Now", "Hold On",
            "Waiting", "Running Back", "Without You"]

        function editable(id) {
            return id !== 4
        }

        function isPinned(kind, id) {
            return false
        }

        function playlistInfo(id) {
            return { id: id, name: root.fixture === "view-long" ? "a playlist name long enough to crop" : "morning", count: 0 }
        }

        function playlistTracks(id) {
            if (root.fixture === "view-empty")
                return []
            const n = root.fixture === "view-long" ? 14 : 6
            const out = []
            for (let i = 0; i < n; i++)
                out.push(i + 1)
            return out
        }

        function trackRows(ids) {
            return ids.map((i) => ({ id: i, title: sampleLibrary.titles[(i - 1) % sampleLibrary.titles.length], artist: "Addison Road" }))
        }
    }

    QtObject {
        id: viewPlayer

        readonly property int status: 1
        readonly property var current: ({ id: 2 })
    }

    PlaylistView {
        id: view

        anchors.fill: parent
        visible: root.viewShot
        animated: root.viewLive
        library: sampleLibrary
        player: viewPlayer
        kind: "playlist"
        playlistId: 1

        Component.onCompleted: {
            view.completeAll()
            if (root.viewShot)
                Qt.callLater(root.viewFixture)
        }
    }

    function viewFixture(): void {
        view.handleAction("down", false)
        view.handleAction("down", false)
        if (root.fixture === "view-sheet") {
            root.viewLive = true
            view.openSheet()
        }
        view.completeAll()
        Qt.callLater(view.completeAll)
    }

    PlaylistsScreen {
        id: screen

        visible: !root.viewShot
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
