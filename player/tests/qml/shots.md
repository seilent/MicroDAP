# Review renders

| Screen | State | Shot | Rows checked | Date |
|---|---|---|---|---|
| settings | browse | `--size 640x960 --orientation landscape` beside `--size 960x640` | OUTPUT section, header, focus bar | 2026-10-08 |
| home | browse | `--size 640x960 --orientation landscape` beside `--size 960x640` | menu, Quickplay peek | 2026-10-08 |
| nowplaying | browse | `--size 640x960 --orientation landscape` beside `--size 960x640` | art, text column, rings | 2026-10-08 |
| hub | albums | `--size 640x960 --orientation landscape` beside `--size 960x640` | pivot strip, grid captions | 2026-10-08 |
| settings, home, nowplaying | browse | `--size 640x960 --orientation landscape-flipped` | as above, turned 180 | 2026-10-08 |
| settings | `orientation-edit` | `--size 640x960` | ORIENTATION value in accent, PORTRAIT FLIPPED | 2026-10-08 |
| settings | `orientation` | `--size 960x640` | ORIENTATION reads EXTERNAL DISPLAY | 2026-10-08 |
| settings | `orientation` | `--size 640x960`, and with `--orientation landscape` | ORIENTATION reads PORTRAIT, LANDSCAPE | 2026-10-08 |
| settings | `orientation-edit-end` | `--size 640x960` | ORIENTATION draft LANDSCAPE FLIPPED in accent, the longest value | 2026-10-08 |
| settings | `orientation-external` | `--size 640x960` | ORIENTATION reads EXTERNAL DISPLAY on a portrait surface | 2026-10-08 |

`ctest -R screenshot-orientation` turns the `--orientation landscape` settings render back by L and gates it
against the `--size 960x640` render: at most 2% of pixels differ by more than 32 in a channel (0.52% measured
2026-10-08, a wrong turn gives 19%).
