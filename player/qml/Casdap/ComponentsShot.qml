pragma ComponentBehavior: Bound
import QtQuick
import "GlyphPaths.js" as GlyphPaths

Item {
    id: root

    readonly property bool sideColumn: Ui.landscape
    readonly property real gap: 4
    readonly property real tileRowY: Ui.contentTop
    readonly property real tilesW: 3 * Ui.tileS + 2 * Ui.gutter
    readonly property real listX: root.sideColumn ? Ui.px(Ui.hubEdge + root.tilesW + Ui.gutter) : 0
    readonly property real listRowY: root.sideColumn ? root.tileRowY : root.tileRowY + Ui.tileS + Ui.gutter
    readonly property real ringRowY: root.tileRowY + Ui.tileS + Ui.gutter + (root.sideColumn ? 0 : 3 * Ui.rowHeight + 8)
    readonly property real smallRingY: root.ringRowY + Ui.backRing + root.gap
    readonly property real glyphX: root.listX + Ui.px(Ui.hubEdge)
    readonly property real glyphRowY: root.sideColumn ? root.listRowY + 3 * Ui.rowHeight + 8 : root.smallRingY + Ui.npRing + root.gap

    Rectangle {
        anchors.fill: parent
        color: Ui.bg
    }

    CroppedHeader {
        text: "music"
        shift: strip.headerShift
        animated: false
    }

    PivotStrip {
        id: strip

        labels: ["albums", "artists", "songs", "genres", "playlists"]
        activeIndex: 0
        animated: false
    }

    LetterTile {
        x: Ui.px(Ui.hubEdge)
        y: Ui.px(root.tileRowY)
        letter: "#"
        animated: false
    }

    Tile {
        x: Ui.px(Ui.hubEdge + Ui.tileS + Ui.gutter)
        y: Ui.px(root.tileRowY)
        title: "harmonics vol.2 a long album title"
        subtitle: "various artists"
        focused: true
        animated: false
    }

    LetterTile {
        x: Ui.px(Ui.hubEdge + 2 * (Ui.tileS + Ui.gutter))
        y: Ui.px(root.tileRowY)
        letter: "a"
        focused: true
        animated: false
    }

    FocusBar {
        x: root.listX + Ui.px(Ui.hubEdge - Ui.focusBarOffset)
        rowY: list.y
        rowHeight: Ui.px(Ui.rowHeight)
        animated: false
    }

    Column {
        id: list

        x: root.listX
        y: Ui.px(root.listRowY)
        width: root.width - root.listX

        ListRow {
            width: list.width
            number: "1"
            primary: "This Could Be Our Day"
            secondary: "Addison Road"
            trailing: "3:48"
            focused: true
            animated: false
        }

        ListRow {
            width: list.width
            number: "2"
            primary: "All That Matters"
            secondary: "Addison Road"
            trailing: "4:02"
            playing: true
            playingDot: true
            animated: false
        }

        ListRow {
            width: list.width
            ring: true
            bold: true
            primary: "now playing"
            secondary: "749 songs"
            animated: false
        }
    }

    Row {
        id: bigRings

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(root.ringRowY)
        spacing: Ui.px(8)

        Ring {
            size: Ui.backRing
            glyph: "back"
            focused: true
            animated: false
        }

        Ring {
            size: Ui.backRing
            glyph: "bluetooth"
            arcSweep: 220
            animated: false
        }
    }

    Row {
        id: smallRings

        x: Ui.px(Ui.hubEdge)
        y: Ui.px(root.smallRingY)
        spacing: Ui.px(8)

        Ring {
            anchors.bottom: parent.bottom
            glyph: "shuffle"
            animated: false
        }

        Ring {
            anchors.bottom: parent.bottom
            glyph: "repeat"
            focused: true
            animated: false
        }

        Ring {
            anchors.bottom: parent.bottom
            glyph: "repeatOne"
            on: true
            animated: false
        }

        Ring {
            anchors.bottom: parent.bottom
            glyph: "heart"
            size: Ui.rowRing
            animated: false
        }
    }

    Flow {
        x: root.glyphX
        y: Ui.px(root.glyphRowY)
        width: root.width - root.glyphX
        spacing: Ui.px(8)

        Repeater {
            model: GlyphPaths.names()

            delegate: Glyph {
                required property var modelData

                name: modelData
                size: Ui.overlayGlyph
                level: 0.6
            }
        }
    }
}
