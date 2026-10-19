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
| settings | `auto-rotate` | `--size 640x960`, and with `--orientation landscape` | AUTO ROTATE ON under ORIENTATION, ORIENTATION read only PORTRAIT, LANDSCAPE | 2026-10-09 |
| settings | `auto-rotate-unsupported` | `--size 640x960` | AUTO ROTATE reads UNSUPPORTED | 2026-10-09 |
| settings | `auto-rotate-below` | `--size 640x960`, and with `--orientation landscape` | SCALE and SAVER under AUTO ROTATE ON, focus on SAVER | 2026-10-09 |
| settings | `auto-rotate-off` | `--size 640x960` | AUTO ROTATE OFF, ORIENTATION editable above it | 2026-10-09 |
| settings | `auto-rotate-edit` | `--size 640x960` | AUTO ROTATE draft ON in accent | 2026-10-09 |
| settings | browse | `--size 640x960` | AUTO ROTATE reads OFF | 2026-10-09 |
| hub | `pressed` | `--pivot songs` and `--pivot albums`, `--size 640x960` and `960x640` | focus on the first song row or album cell, F3 scale inside the slot, a pressed tile's caption not clipped | pending |
| hub | `touch-scrolled` | `--pivot artists`, `--size 640x960` and `960x640` | focus row fully inside `[CONTENT_TOP, H - ROW_HEIGHT]` | pending |
| hub | folders | `--pivot folders --library <dir>`, `--size 640x960` and `960x640` | sixth pivot label, one root row in the GENRES form, focus bar | pending |
| hub | `folder-open` | `--pivot folders --library <dir>`, `--size 640x960` and `960x640` | path line in the settings label form under the strip, album folder songs numbered from 1 in the SONGS form, long path elided behind `...` | pending |
| album | `pressed` | `--size 640x960` and `960x640` | focused track row at F3 scale | pending |
| queue | `touch-scrolled` | `--size 640x960` and `960x640` | focus row fully inside `[CONTENT_TOP, H - ROW_HEIGHT]` | pending |
| home | `pressed` | `--size 640x960` and `960x640` | focused menu item at F3 scale | pending |
| quickplay | `pressed` | `--size 640x960` and `960x640` | focused tile at F3 scale inside its slot | pending |
| nowplaying | `pressed` | `--size 640x960` and `960x640` | focused ring at F3 scale | pending |
| nowplaying | `outside-library` | `--size 640x960`, `--orientation landscape` and `960x640` | no rate ring, shuffle and repeat at their NP_RINGS centers | pending |
| settings | `touch-edit` | `--size 640x960` and `960x640` | TOUCH draft OFF in accent after AUTO ROTATE | pending |
| settings | `touch` | `--size 640x960` and `960x640` | TOUCH reads ON after AUTO ROTATE, focus on TOUCH | pending |
| eq | `pressed` | `--size 640x960` and `960x640` | renders as browse (EQ rows have no F3) | pending |
| power | `pressed` | `--size 640x960` and `960x640` | renders as browse (dialog items have no F3) | pending |
| settings | browse | `--size 640x960` | BIT PERFECT OFF under BLUETOOTH CODEC, the OUTPUT rows above unchanged | pending |
| settings | `bit-perfect` | `--size 640x960` and `960x640` | BIT PERFECT ON on USB, focus on BIT PERFECT | pending |
| settings | `bit-perfect-edit` | `--size 640x960` and `960x640` | BIT PERFECT draft EXCLUSIVE in accent | pending |
| settings | `bit-perfect-phone` | `--size 640x960` and `960x640` | BIT PERFECT reads EXCLUSIVE (PHONE) | pending |
| settings | `bit-perfect-unavailable` | `--size 640x960` and `960x640` | BIT PERFECT reads EXCLUSIVE (UNAVAILABLE), the longest value, unclipped, clear of the title | pending |
| settings | `bit-perfect-usb-only` | `--size 640x960` and `960x640` | BIT PERFECT reads USB ONLY on the headphones | pending |
| settings | `sample-rate-edit` | `--size 640x960` and `960x640` | SAMPLE RATE draft 352800 in accent on the FiiO rates | pending |
| settings | `sample-rate-unsupported` | `--size 640x960` and `960x640` | SAMPLE RATE reads 96000 (NOT SUPPORTED) on the Apple dongle rate | pending |
| settings | `sample-rate-locked` | `--size 640x960` and `960x640` | focus on EQ reading BYPASSED, SAMPLE RATE above it read only AUTO 96000 | pending |
| eq | browse | `--size 640x960` and `960x640` | BAND rows PK, LS, HS text unchanged, band 2 sub rows | 2026-10-12 (Selawik) |
| eq | `eq-types` | `--size 640x960` and `960x640` | BAND rows OFF, PK, LS, HS, BP, focus on BAND 5, gain free rows with no gain | 2026-10-12 (Selawik) |
| eq | `eq-types-end` | `--size 640x960` and `960x640` | BAND rows LP, HP, AP, `HS 20 KHZ -24.0 DB Q 10.00` unclipped and clear of the title, focus on BAND 10 | 2026-10-12 (Selawik) |
| eq | `eq-open-hs` | `--size 640x960` and `960x640` | BAND 4 sub rows TYPE to Q on screen, TYPE HIGH SHELF focused, GAIN +12.0 DB | 2026-10-12 (Selawik) |
| eq | `eq-type-edit` | `--size 640x960` and `960x640` | BAND 5 sub rows TYPE to Q on screen, TYPE BAND PASS in accent (edit mode), GAIN NOT USED | 2026-10-12 (Selawik) |
| eq | `eq-gain-unused` | `--size 640x960` and `960x640` | BAND 6 sub rows TYPE to Q on screen, TYPE LOW PASS, focus on GAIN NOT USED in the read only value style | 2026-10-12 (Selawik) |
| eq | `eq-open-hp` | `--size 640x960` and `960x640` | BAND 7 sub rows TYPE to Q on screen, TYPE HIGH PASS focused, GAIN NOT USED | 2026-10-12 (Selawik) |
| eq | `eq-open-ap` | `--size 640x960` and `960x640` | BAND 8 sub rows TYPE to Q on screen, TYPE ALL PASS focused, GAIN NOT USED | 2026-10-12 (Selawik) |
| settings | `eq-dac` | `--size 640x960` and `960x640` | focus on EQ under BIT PERFECT ON reading `SENNHEISER HD 600 PARAMETRICEQ (DAC)`, the longest value, unclipped, clear of the title, right at W - HUB_EDGE | 2026-10-12 (Selawik) |
| settings | `eq-dac-custom` | `--size 640x960` and `960x640` | EQ reads `CUSTOM (DAC)` | 2026-10-12 (Selawik) |
| settings | `eq-dac-failed` | `--size 640x960` and `960x640` | EQ reads `DAC EQ FAILED` | 2026-10-12 (Selawik) |
| settings | `eq-dac-unsupported` | `--size 640x960` and `960x640` | EQ reads `BYPASSED` | 2026-10-12 (Selawik) |
| eq | `eq-dac-range` | `--size 640x960` and `960x640` | USB pivot, BAND 1 sub rows, focus on GAIN `-15.0 DB (DAC -12.0)`, Q `10.00 (DAC 8.00)`, PREAMP `-6.5 DB (DAC 0.0)` (portrait), band row text unchanged, values clear of the titles | 2026-10-12 (Selawik) |
| eq | `eq-dac-preamp` | `--size 640x960` and `960x640` | focus on PREAMP `-6.5 DB (DAC 0.0)`, BAND 2 sub rows, GAIN `-24.0 DB (DAC -12.0)` (the longest value) and Q `0.10 (DAC 0.25)` on screen in portrait, landscape down to FREQUENCY | 2026-10-12 (Selawik) |
| eq | `eq-dac-long` | `--size 640x960` and `960x640` | BAND 2 sub rows, focus on GAIN `-24.0 DB (DAC -12.0)`, the longest value, with Q `0.10 (DAC 0.25)` on screen in both orientations | 2026-10-12 (Selawik) |
| settings | `boot` | `--size 640x960` and `960x640` | BOOT INTO STEAM after WI-FI WHEN OFF, focus on BOOT INTO | 2026-10-16 |
| settings | `boot-edit` | `--size 640x960` and `960x640` | BOOT INTO draft in accent | 2026-10-16 |
| settings | `boot-missing` | `--size 640x960` | `HANDHELD-DESKTOP-SESSION (NOT AVAILABLE)` clips the title to BOOT I, the title clip rule | 2026-10-16 |
| settings | `about` | `--size 640x960` and `960x640` | About open: IMAGE, SYSTEM UPDATE UP TO DATE, focus on SYSTEM UPDATE | 2026-10-16 |
| settings | `about-available` | `--size 640x960` and `960x640` | SYSTEM UPDATE `TESTING-0628A41 AVAILABLE` clear of the title | 2026-10-16 |
| settings | `about-missing` | `--size 640x960` and `960x640` | no IMAGE row, SYSTEM UPDATE NOT CHECKED under ABOUT | 2026-10-16 |
| power | browse | `--size 640x960` and `960x640` | SCREEN OFF, GAME MODE, MICROEMU, SUSPEND, POWER OFF, focus bar on SCREEN OFF in both | 2026-10-16 |
| prompt | browse | `--size 640x960` and `960x640` | focus bar and the full white on KEEP IN BACKGROUND, CLOSE PLAYER dim | 2026-10-16 |
| overlays | browse | `--size 640x960` and `960x640` | dialog and sheet focus bars on their first item | 2026-10-16 |
| power | `power-none` | `--size 640x960` and `960x640` | SCREEN OFF, SUSPEND, POWER OFF | 2026-10-16 |
| power | `power-long` | `--size 640x960` and `960x640` | a long label runs past the right edge at 640x960 (dialog items do not clip) | 2026-10-16 |
| settings | browse, `pivot-library`, `pivot-display`, `pivot-power`, `pivot-system` | `--size 640x960` and `960x640` | pivot strip in the hub form, no label rows, every pivot's last row | pending |
| settings | browse | `--overlay --size 640x960` and `960x640` | `system` with CLOSE PLAYER, no ORIENTATION rows in `display` | pending |
| overlays | browse | `--size 960x640` | the sheet cell shorter than the sheet: panel pinned at the cell top, REMOVE shown with its focus bar | pending |
| settings | `sample-rate-edit` | `--size 640x960` and `960x640` | value sheet of 9 rates high to low, AUTO last in the accent, focus 48000 | pending |
| settings | `saver-sheet` | `--size 640x960` and `960x640` | live row: 15 S previewed under the focus, the accent mark kept on 30 S | pending |
| settings | `touch`, `touch-edit` | `--size 640x960` and `960x640` | TOUCH ON, then OFF after one A, no sheet | pending |
| settings | `accent-edit` | `--size 640x960` and `960x640` | ACCENT edit mode, swatch on hue 0 | pending |
| keyboard | `browse` | `--size 640x960` and `960x640` | typed text, cursor at the end, focus on DONE | pending |
| keyboard | `kb-empty` | `--size 640x960` and `960x640` | hint in the field, DONE at 0.4 only under the minimum | pending |
| keyboard | `kb-long` | `--size 640x960` and `960x640` | text wider than the field keeps the end | pending |
| keyboard | `kb-petal` | `--size 640x960` and `960x640` | key 7 petal, pick left, cells one pitch inward | pending |
| keyboard | `kb-petal-corner` | `--size 640x960` and `960x640` | key 1 petal, up cell over the field | pending |
| keyboard | `kb-shift` | `--size 640x960` and `960x640` | SHIFT locked in the accent, capital labels | pending |
| keyboard | `kb-symbols` | `--size 640x960` and `960x640` | symbols page, `abc` label | pending |
| keyboard | `kb-discard` | `--size 640x960` and `960x640` | discard confirm, KEEP focused, keyboard bare under it | pending |
| keyboard | `kb-number-freq` | `--size 640x960` and `960x640` | `0` three cells wide, `done` two rows, unit HZ | pending |
| keyboard | `kb-number-q` | `--size 640x960` and `960x640` | `0` two cells, `.` | pending |
| keyboard | `kb-number-gain` | `--size 640x960` and `960x640` | `-` `0` `.`, unit DB | pending |
| keyboard | `kb-refused` | `--size 640x960` and `960x640` | toast GAIN over `-24 to 12 db`, draft kept | pending |
| settings | `play-through` | `--size 640x960` and `960x640` | PLAY THROUGH ALBUMS, RESUME POSITION, AUTO NOW PLAYING ON after EQ | pending |
| settings | `play-through-sheet` | `--size 640x960` and `960x640` | OFF FOLDERS ALBUMS, OFF in the accent | pending |
| settings | `resume-sheet` | `--size 640x960` and `960x640` | OFF TRACK POSITION, POSITION in the accent | pending |
| album | browse | `--size 640x960` and `960x640` | STICKING WITH YOU title in the accent, no dot | pending |
| components | `tracknum` | `--size 640x960` and `960x640` | 9999 title at +24 u, 10000 title one digit right | pending |
| nowplaying | `seek` | `--size 640x960` and `960x640` | fill and times at the 60 % target | pending |
| home | browse | `--size 640x960` and `960x640` | play ring left of `music` at the bar center, no bar on `music` | pending |
| album | `art-collapse`, `art-dock`, `art-docked` | `--size 640x960`, docked also `960x640` | art 71 u at 155; art at 150 with the lines at 60 u; docked art at 103 beside the lines, rows from 186 | pending |
| nowplaying | `clock-24h`, `clock-12h`, `percent`, `percent-low` | `--size 640x960`, `percent` also `960x640` | 19:30, 7:30, 73% left of the glyph, 5% white beside the accent glyph | pending |
| home | `quickplay-off` | `--size 640x960` and `960x640` | no Quickplay panel, play ring kept | pending |
| hub | `playlist-pick` | `--pivot albums` and `--pivot songs`, `--size 640x960` and `960x640` | second sheet titled with the target: NEW PLAYLIST, FAVORITES, the playlists | 2026-10-20 |
| playlists | `playlist-sheet`, `playlist-delete` | `--size 640x960` and `960x640` | DELETE PLAYLIST last on a `.m3u8` row; T13 `delete playlist?` with KEEP PLAYLIST focused, the page bar faded | 2026-10-20 |
| playlists | `view`, `view-sheet`, `view-long`, `view-empty` | `--size 640x960` and `960x640` | playlist view: name header, `N SONGS` at the queue count slot, numbered rows, playing row in the accent; REMOVE, MOVE UP, MOVE DOWN; a long name cropped; no rows | 2026-10-20 |
| hub, nowplaying | `track-info`, `track-info-short` | hub `--pivot songs`, `--size 640x960` and `960x640` | info line sheet at 0.6 with no bar, the path in its own case, cut behind `...` or clipped at W - EDGE | 2026-10-20 |
| settings | `max-volume`, `max-volume-sheet`, `power-on-sheet` | `--size 640x960` and `960x640` | MAX VOLUME 70 after VOLUME STEP; sheet 100 to 50, 70 marked; POWER ON VOLUME 100 to 10 then LAST, 40 marked | 2026-10-20 |
| settings | `balance`, `balance-edit` | `--size 640x960` and `960x640` | BALANCE L 3.5 DB after VOLUME STEP; edit mode L 4 DB in the accent | 2026-10-20 |
| settings | `crossfeed-sheet` | `--size 640x960` and `960x640` | CROSSFEED sheet OFF, DEFAULT, CHU MOY, JAN MEIER marked in the accent | 2026-10-20 |
| settings | `crossfeed-speaker` | `--size 640x960` and `960x640` | CROSSFEED HEADPHONES ONLY on the speaker, BALANCE editable | 2026-10-20 |
| settings | `crossfeed-bit-perfect` | `--size 640x960` and `960x640` | BALANCE and CROSSFEED values with `(NOT ON BIT PERFECT)`, clear of the titles | 2026-10-20 |
| settings | `replaygain-sheet` | `--size 640x960` and `960x640` | REPLAYGAIN sheet OFF, TRACK, ALBUM, ALBUM marked in the accent, focus on OFF | 2026-10-20 |
| settings | `replaygain-bit-perfect` | `--size 640x960` and `960x640` | REPLAYGAIN reads BIT PERFECT, read only, while bit perfect is in effect | 2026-10-20 |
| settings | `crossfade-sheet` | `--size 640x960` and `960x640` | CROSSFADE sheet 5 S, 3 S, 2 S, 1 S, OFF, 3 S marked in the accent | 2026-10-20 |
| settings | `crossfade-bit-perfect` | `--size 640x960` and `960x640` | REPLAYGAIN and CROSSFADE read BIT PERFECT, read only | 2026-10-20 |
| nowplaying | `lyrics-short`, `lyrics-long`, `lyrics-cjk`, `lyrics-before`, `lyrics-none`, `lyrics-plain`, `lyrics-pending`, `lyricslide-<short or long>-<t>` | `--size 640x960` and `960x640` (slides 640x960) | lyrics mode: previous 0.4, current 26 u, upcoming 0.6, shade 0.55, no art, title or progress; long current wraps; title and next track with no lyrics; plain text 20 u mixed case; blank while pending; F13 frames | 2026-10-20 |
| nowplaying | `mark`, `mark-longnext`, `mark-nonext`, `mark-pending` | `--size 640x960`, `mark` and `mark-longnext` also `960x640` | mic glyph at the first next row end at 0.6, the row clipped 8 u before it, shown with no next track, hidden while pending | 2026-10-20 |
| eq | `eq-bands`, `eq-bands-one`, `eq-bands-full` | `--size 640x960`, `eq-bands` and `eq-bands-full` also `960x640` | band rows `1` to `n`, ADD BAND below 10, REMOVE BAND with the count above 1 | 2026-10-20 |
| eq | `eq-custom`, `eq-saved`, `eq-delete`, `eq-rename` | `--size 640x960` and `960x640` | SAVE PRESET on CUSTOM; DELETE PRESET and RENAME PRESET on a saved preset; T13 `delete preset?` with KEEP PRESET focused; keyboard titled `rename preset` with the name | 2026-10-20 |
| eq | `eq-typed-sheet`, `eq-typed-freq`, `eq-typed-gain` | `--size 640x960` and `960x640` | `type value` sheet titled PREAMP; number keyboard: frequency digits with HZ, gain with `-` and `.` and DB | 2026-10-20 |
| eq | `eq-graphic`, `eq-graphic-flat`, `eq-graphic-edit` | `--size 640x960` and `960x640` | MODE GRAPHIC; ten bands 31 HZ to 16 KHZ with gains; PRESET FLAT and the read only PREAMP; a band gain in edit mode in the accent | 2026-10-20 |
| bluetooth | `bt-eq`, `bt-eq-band` | `--bluez connected`, `--size 640x960` and `960x640` | `eq` pivot: HEADPHONES slot line, MODE, PROFILE CUSTOM, bands; a band open with GAIN focused | 2026-10-20 |
| saver | `phone-active` | `--size 640x960` and `960x640` | phone title, artist, `from Pixel 8`, bluetooth glyph 21.3 u in the art box, no tile | 2026-10-20 |
| bluetooth | `pairing-pulse`, `browse` | `--bluez pairing` and `--bluez last`, `--size 640x960` and `960x640` | the hint clear of the ring at s 1.4; PHONE with the last phone, DISCONNECTED and the bar | 2026-10-20 |
| bluetooth | `guard-toast` | `--bluez connected`, `--size 640x960` and `960x640` | toast `bluetooth` / `hold b 2 s to leave` over PHONE connected | 2026-10-20 |
| nowplaying | `--bluez art` | `--size 640x960` and `960x640` | phone thumbnail in the art square, no glyph, progress at the art width | 2026-10-20 |
| nowplaying | `--bluez active` | `--size 640x960` and `960x640` | no handle or a fetch in flight: the bluetooth glyph, no tile | 2026-10-20 |
| saver | `phone-active` `--bluez art` | `--size 640x960` and `960x640` | phone thumbnail in the art box on the tile, no glyph | 2026-10-20 |
| settings | `sleep`, `sleep-sheet`, `sleep-left-sheet` | `--size 640x960` and `960x640` | SLEEP TIMER reads 29 MIN LEFT; sheet 120 MIN to OFF with OFF marked; the 29 MIN LEFT lead under the title, focus on 30 MIN | 2026-10-20 |

`ctest -R screenshot-orientation` turns the `--orientation landscape` settings render back by L and gates it
against the `--size 960x640` render: at most 2% of pixels differ by more than 32 in a channel (0.52% measured
2026-10-08, a wrong turn gives 19%).
