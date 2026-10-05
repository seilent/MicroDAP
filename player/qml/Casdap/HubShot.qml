pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string pivot: "albums"

    readonly property var pivots: ["artists", "albums", "songs", "genres", "playlists"]
    readonly property var contextArtists: typeof Artists === "undefined" ? null : Artists
    readonly property var contextAlbums: typeof Albums === "undefined" ? null : Albums
    readonly property var contextSongs: typeof Songs === "undefined" ? null : Songs
    readonly property var contextGenres: typeof Genres === "undefined" ? null : Genres
    readonly property var contextLibrary: typeof Library === "undefined" ? null : Library

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    ListModel {
        id: sampleArtists

        Component.onCompleted: {
            const rows = [
                ["3 Doors Down", "1 album, 12 songs", "#"],
                ["Addison Road", "2 albums, 21 songs", "A"],
                ["Anberlin", "1 album, 11 songs", "A"],
                ["Casting Crowns", "2 albums, 24 songs", "C"],
                ["Guns N' Roses", "1 album, 12 songs", "G"],
                ["Jimmy Eat World", "1 album, 11 songs", "J"],
                ["MercyMe", "1 album, 12 songs", "M"],
                ["Nickelback", "1 album, 11 songs", "N"],
                ["Randy Travis", "1 album, 10 songs", "R"]
            ]
            for (let i = 0; i < rows.length; i++)
                sampleArtists.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], artPath: "", section: rows[i][2] })
        }
    }

    ListModel {
        id: sampleAlbums

        Component.onCompleted: {
            const rows = [
                ["3 Doors Down", "3 Doors Down", "#"],
                ["Addison Road", "Addison Road", "A"],
                ["All That Matters", "MercyMe", "A"],
                ["All The Right Reasons", "Nickelback", "A"],
                ["Almost There", "MercyMe", "A"],
                ["The Altar and the Door", "Casting Crowns", "A"],
                ["Anything Worth Saying", "Aaron Shust", "A"],
                ["Appetite for Destruction", "Guns N' Roses", "A"],
                ["Around the Bend", "Randy Travis", "A"],
                ["Casting Crowns", "Casting Crowns", "C"],
                ["Chase This Light", "Jimmy Eat World", "C"],
                ["HARMONICS vol.2", "Various Artists", "H"]
            ]
            for (let i = 0; i < rows.length; i++)
                sampleAlbums.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], artPath: "", section: rows[i][2] })
        }
    }

    ListModel {
        id: sampleSongs

        Component.onCompleted: {
            const rows = [
                ["All That Matters", "Addison Road", "A"],
                ["Always", "Erasure", "A"],
                ["Big Yellow Taxi", "Counting Crows", "B"],
                ["Casimir Pulaski Day", "Sufjan Stevens", "C"],
                ["Chasing Cars", "Snow Patrol", "C"],
                ["Hoshi no Kakera", "ふるーり", "H"],
                ["Hope Now", "Addison Road", "H"],
                ["Shine", "Collective Soul", "S"],
                ["This Could Be Our Day", "Addison Road", "T"],
                ["Yume", "わかばやし", "Y"]
            ]
            for (let i = 0; i < rows.length; i++)
                sampleSongs.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], artPath: "", section: rows[i][2] })
        }
    }

    ListModel {
        id: sampleGenres

        Component.onCompleted: {
            const rows = [
                ["Alternative", "34 songs", "A"],
                ["Country", "10 songs", "C"],
                ["Electronic", "9 songs", "E"],
                ["Rock", "48 songs", "R"]
            ]
            for (let i = 0; i < rows.length; i++)
                sampleGenres.append({ id: i + 1, title: rows[i][0], subtitle: rows[i][1], artPath: "", section: rows[i][2] })
        }
    }

    MusicHub {
        id: hub

        anchors.fill: parent
        animated: false
        pivotIndex: Math.max(0, root.pivots.indexOf(root.pivot))
        artistsModel: root.contextArtists !== null ? root.contextArtists : sampleArtists
        albumsModel: root.contextAlbums !== null ? root.contextAlbums : sampleAlbums
        songsModel: root.contextSongs !== null ? root.contextSongs : sampleSongs
        genresModel: root.contextGenres !== null ? root.contextGenres : sampleGenres
        library: root.contextLibrary

        Component.onCompleted: hub.completeAll()
    }
}
