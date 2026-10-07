pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string kind: "artist"

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    QtObject {
        id: sampleLibrary

        readonly property bool scanning: false
        readonly property var albums: ({
            1: { id: 1, title: "Addison Road", artist: "Addison Road", year: 2008, artPath: "" },
            2: { id: 2, title: "Stories", artist: "Addison Road", year: 2010, artPath: "" }
        })
        readonly property var tracks: [
            [1, "This Could Be Our Day", 1, 214000],
            [2, "All That Matters", 1, 241000],
            [3, "Sticking With You", 1, 228000],
            [4, "Hope Now", 1, 266000],
            [5, "What Do I Know of Holy", 1, 262000],
            [6, "Run", 2, 203000],
            [7, "Fight Another Day", 2, 221000],
            [8, "Where I Find You", 2, 247000],
            [9, "Change in the Making", 2, 256000],
            [10, "Need You Now", 2, 232000]
        ]

        function artistInfo(id: int): var {
            return { id: id, name: "Addison Road", artPath: "" }
        }

        function artistAlbums(id: int): var {
            return [1, 2]
        }

        function albumInfo(id: int): var {
            const a = sampleLibrary.albums[id]
            return a === undefined ? {} : a
        }

        function rowOf(t: var): var {
            return { id: t[0], title: t[1], artist: "Addison Road", album: sampleLibrary.albums[t[2]].title, albumId: t[2], durationMs: t[3] }
        }

        function artistTracks(id: int): var {
            return sampleLibrary.tracks.map((t) => t[0])
        }

        function artistAlbumRows(id: int): var {
            return [sampleLibrary.albums[1], sampleLibrary.albums[2]]
        }

        function artistTrackRows(id: int): var {
            return sampleLibrary.tracks.map((t) => sampleLibrary.rowOf(t))
        }

        function trackRows(ids: var): var {
            return sampleLibrary.tracks.filter((t) => ids.indexOf(t[0]) >= 0).map((t) => sampleLibrary.rowOf(t))
        }

        function albumTracks(id: int): var {
            return sampleLibrary.tracks.filter((t) => t[2] === id).map((t) => sampleLibrary.rowOf(t))
        }
    }

    QtObject {
        id: samplePlayer

        readonly property int status: 1
        readonly property var current: ({ id: 3, title: "Sticking With You", artist: "Addison Road", album: "Addison Road", artPath: "" })
    }

    Component {
        id: artistComponent

        ArtistDetail {
            id: artistPage

            artistId: 1
            animated: false
            library: sampleLibrary
            player: samplePlayer

            Component.onCompleted: artistPage.completeAll()
        }
    }

    Component {
        id: albumComponent

        AlbumDetail {
            id: albumPage

            albumId: 1
            animated: false
            library: sampleLibrary
            player: samplePlayer

            Component.onCompleted: albumPage.completeAll()
        }
    }

    Loader {
        anchors.fill: parent
        sourceComponent: root.kind === "album" ? albumComponent : artistComponent
    }
}
