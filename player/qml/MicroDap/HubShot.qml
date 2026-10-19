pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: root

    property string pivot: "albums"
    property bool jump: false
    property string fixture: "browse"

    readonly property var pivots: ["artists", "albums", "songs", "genres", "playlists", "folders"]
    readonly property var contextArtists: typeof Artists === "undefined" ? null : Artists
    readonly property var contextAlbums: typeof Albums === "undefined" ? null : Albums
    readonly property var contextSongs: typeof Songs === "undefined" ? null : Songs
    readonly property var contextGenres: typeof Genres === "undefined" ? null : Genres
    readonly property var contextLibrary: typeof Library === "undefined" ? null : Library
    readonly property var artistsHub: typeof ArtistsHub === "undefined" ? null : ArtistsHub
    readonly property var albumsGrid: typeof AlbumsGrid === "undefined" ? null : AlbumsGrid
    readonly property var songsHub: typeof SongsHub === "undefined" ? null : SongsHub
    readonly property var genresHub: typeof GenresHub === "undefined" ? null : GenresHub
    readonly property var foldersHub: typeof FoldersHub === "undefined" ? null : FoldersHub

    function attach(hubModel: var, context: var, sample: var): void {
        if (hubModel !== null && context === null)
            hubModel.source = sample
    }

    function infoFixture(): void {
        const entry = hub.focusedEntry()
        if (entry !== null && (entry.kind === "letter" || entry.kind === "divider"))
            hub.handleAction("down", false)
        hub.completeAll()
        hub.openSheet()
        const parts = root.fixture === "track-info-short" ? ["Music", "Single", "01 Single.mp3"]
            : ["Music", "HARMONICS vol.2 夢", "Disc 1", "07 A very long track title that keeps on going past the edge.flac"]
        const width = hub.width - 2 * Ui.px(Ui.edge)
        const path = Env.fitPath(parts, hub.sheet.plainFont, width)
        const lines = root.fixture === "track-info-short" ? ["mp3", "44100 hz", "320 kbps", "9.8 mb", path]
            : ["flac", "96000 hz", "24 bit", "4608 kbps", "212.6 mb", path]
        hub.sheet.showLines(hub.sheet.title, lines, lines.length - 1)
        hub.completeAll()
    }

    function pickFixture(): void {
        const entry = hub.focusedEntry()
        if (entry !== null && (entry.kind === "letter" || entry.kind === "divider"))
            hub.handleAction(hub.grid ? "right" : "down", false)
        hub.completeAll()
        hub.openSheet()
        hub.sheet.push(hub.sheet.title, ["new playlist", "favorites", "Harmonics", "Morning", "Night Drive", "Workout"])
        hub.completeAll()
    }

    function scrollFixture(): void {
        hub.activeList.contentY = 1.5 * hub.activeList.rowPitch
        hub.activeList.clampFocusToView()
    }

    Component.onCompleted: {
        sampleArtists.fill()
        sampleAlbums.fill()
        sampleSongs.fill()
        sampleGenres.fill()
        root.attach(root.artistsHub, root.contextArtists, sampleArtists)
        root.attach(root.albumsGrid, root.contextAlbums, sampleAlbums)
        root.attach(root.songsHub, root.contextSongs, sampleSongs)
        root.attach(root.genresHub, root.contextGenres, sampleGenres)
        hub.bindPane(hub.pivotIndex)
        hub.fixFocus(hub.pivotIndex)
        hub.completeAll()
        if (root.jump) {
            hub.openJumpGrid()
            hub.completeAll()
        }
        if (root.fixture === "touch-scrolled")
            Qt.callLater(root.scrollFixture)
        if (root.fixture === "folder-open") {
            for (let depth = 0; depth < 2; depth++) {
                const entry = hub.focusedEntry()
                if (entry !== null && entry.kind === "folder")
                    hub.activate()
            }
            hub.completeAll()
        }
        if (root.fixture === "playlist-pick")
            Qt.callLater(root.pickFixture)
        if (root.fixture.startsWith("track-info"))
            Qt.callLater(root.infoFixture)
        if (root.fixture === "pressed") {
            const entry = hub.focusedEntry()
            if (entry !== null && (entry.kind === "letter" || entry.kind === "divider"))
                hub.handleAction(hub.grid ? "right" : "down", false)
            hub.completeAll()
            hub.pressed = true
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    ListModel {
        id: sampleArtists

        function fill(): void {
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

        function fill(): void {
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

        function fill(): void {
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

        function fill(): void {
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
        animated: root.jump || root.fixture === "playlist-pick" || root.fixture.startsWith("track-info")
        pivotIndex: Math.max(0, root.pivots.indexOf(root.pivot))
        artistsModel: root.artistsHub !== null ? root.artistsHub : sampleArtists
        albumsModel: root.albumsGrid !== null ? root.albumsGrid : sampleAlbums
        songsModel: root.songsHub !== null ? root.songsHub : sampleSongs
        genresModel: root.genresHub !== null ? root.genresHub : sampleGenres
        foldersModel: root.foldersHub
        library: root.contextLibrary
    }
}
