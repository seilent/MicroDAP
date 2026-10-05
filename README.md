# CASDAP

Console As DAP. A music player session and Bluetooth A2DP receiver for Linux gaming handhelds,
shipped as a systemd-sysext overlay for [Armada](https://github.com/armada-os/armada).

Status: prototype. The session, the switch units, the guide button mapping, the hold daemon,
the pad target, the boot guard and the Bluetooth agent exist and run on an AYANEO Pocket MICRO 2.
Player and screen off state are not written yet.

## Layout

| Path | Content |
|---|---|
| `sysext/` | Static overlay, copied into `dist/` as is. `/usr` only, nothing under `/etc`. |
| `patches/<class>/<path>.patch` | Patches against files of the Armada image. `armada/` Armada scripts, `generic/` InputPlumber profile, `pm2/` Pocket MICRO 2 capability map. |
| `build/` | `base.env` (pinned image), `Containerfile`, `toolchain.sh`, `build.sh`, device scripts for the dev loop. |
| `player/` | Qt 6 QML player with a C++ core (`casdapcore`). CMake project, built by `build.sh` inside the builder, installed to `/usr/bin/casdap-player`. Playback is `playbin`, audio only, sink from `CASDAP_AUDIO_SINK` (default `pulsesink`), gapless through `about-to-finish`. Queue, index, position, shuffle, repeat, `status` (`playing`, `paused`, `stopped`), `savedAt` (unix seconds) and `handoff` persist in `~/.config/casdap/state.json`, written every 10 s while playing with `handoff` false, on quit and on SIGTERM with `handoff` true, and by the switch prompt before the switch starts (KEEP IN BACKGROUND `handoff` true, CLOSE PLAYER `handoff` false; the session switch ends the player with SIGKILL, so nothing is written at exit); a start resumes playing when `handoff` is true, `status` is `playing` and `savedAt` is under 60 s old, every other file restores the queue paused. Every play start adds a `history` row. The library (`library.db`, `PRAGMA user_version` 2) reads `ARTISTS` or `ARTIST` tag values as a list, splits a single value on `; `, ` / ` and `, ` unless the whole value equals the album artist (not on ` feat. `), stores each per track in `track_artists(track_id, artist_id, pos)`, keeps `tracks.artist` as the joined display string and `albums.artist` as the album artist; `ArtistsModel` lists individual artists with album and song counts, `Library.artistAlbums(id)` and `artistTracks(id)` resolve through `track_artists`, and a version 1 database gets the table and every track marked for reread on the next scan. `Mpris` registers `org.mpris.MediaPlayer2.casdap` at `/org/mpris/MediaPlayer2` on the session bus (root and `Player` interfaces, XML in `player/dbus/`, installed to `share/dbus-1/interfaces`): `Identity` is `CASDAP`, no `DesktopEntry`, track ids are `/org/casdap/track/<id>`, `Volume` reads 1.0 and ignores writes, `OpenUri` plays one `file://` uri, `Seeked` and `PropertiesChanged` are emitted for status, metadata, shuffle and loop. `PowerBridge` registers `org.casdap.Player` at `/org/casdap/Player` (`SetScreenOff(b)`, `ShowPowerMenu()`, `ShowOverlay()`, `HideOverlay()`, `ToggleOverlay()`, `ShowSwitchPrompt()`, `ScreenOff`, `PlaybackStatus`, `OverlayShown`, signal `ScreenOffChanged`; the three overlay methods answer `org.casdap.Error.NotSupported` and `OverlayShown` reads false in DAP mode, `ShowSwitchPrompt` answers `NotSupported` in overlay mode and emits `showSwitchPrompt` to QML in DAP mode), listens to `org.casdap.Hotkeys.VolumeChanged(d)`, reads `/run/casdap/drift` and runs only the verbs `screen-off`, `game-mode`, `suspend`, `poweroff` (`casdap-power <verb>`, `systemctl start casdap-switch-back.service`); settings live in `~/.config/casdap/settings.ini` (`output`, `roots`, `accent`, `scale`, `pocketControls`, `saverSeconds`, written 500 ms after the last change and on quit), battery comes from UPower `DisplayDevice` with a `/sys/class/power_supply` fallback, and local playback pauses while the Bluetooth transport is active. `CASDAP_BLUEZ_FAKE=1` wires `FakeBluezModel` in place of the Bluetooth model. Removing the current queue row starts the next one or stops when none follows. Pad input is SDL3 gamepad events (Guide never mapped), polled every 16 ms while a pad is open and the screen is on, every 1 s while no pad is open, every 50 ms with the screen off only when pocket controls are on, otherwise not at all; d-pad and stick repeat at 300 ms then 80 ms, `CASDAP_PAD_LOG=1` logs each action. `Pad` and `DbusPad` derive from `PadSource` (the `Action` enum, `screenOff`, `pocketControls`, `connected`, `actionName`, `action`, `pressed`, `released`); `main.cpp` hands `Pad` to QML as `Pad`, `DbusPad` under `--overlay`. `DbusPad` reads `org.shadowblip.Input.DBusDevice.InputEvent(s,d)` from InputPlumber on the system bus (`CASDAP_INPUTPLUMBER_BUS` overrides the address): `ui_up/down/left/right`, `ui_accept` A, `ui_back` B, `ui_context` X, `ui_action` Y, `ui_l1` LB, `ui_r1` RB, `ui_l2` LT, `ui_r2` RT, `ui_option` Start, `ui_select` Back, value >= 0.5 press, below release, same 300/80 repeat; `ui_guide`, `ui_quick`, `ui_l3` and unknown names are logged once and dropped; `connected` while a `devices/target/dbus*` path exists; `pocketControls` does nothing. Its `Intercept` writes `InterceptMode` on every `org.shadowblip.Input.CompositeDevice` from `GetManagedObjects` on `/org/shadowblip/InputPlumber`: 3 on `setShown(true)` and on a device added while shown, 0 on `setShown(false)` and in the destructor. |
| `tests/` | bash tests with stubbed `sudo`, `systemctl`, `loginctl`, `btmgmt`, `evtest`, `busctl`. `bt-test.sh` runs `bt-test.py` under `dbus-run-session` with a fake `org.bluez` (needs `dbus-python` and `python3-gobject`). `keep-test.sh` drives `casdap-keep` through `CASDAP_KEEP_FILE` and `CASDAP_BOOT_ID_FILE`; `wait-x-test.sh` drives `casdap-wait-x` with a stubbed `xprop` and `CASDAP_X_SOCKET_DIR`; `overlay-unit-test.sh` greps `casdap-overlay.service` and its drop in under `sysext/` and runs `systemd-analyze --user verify` where the image units and the player binary exist (`skip:` otherwise). Player tests live in `player/tests/` (QTest, offscreen, generated FLAC and MP3 fixtures; `tst_mpris`, `tst_power`, `tst_systembluez`, `tst_dbuspad` and `tst_overlay` run under `dbus-run-session`, `tst_dbuspad` and `tst_overlay` against `fake_inputplumber.py`, `tst_overlay` also against a unix datagram `NOTIFY_SOCKET`; each `player/tests/qml/tst_*.qml` is its own ctest entry `qml-<name>` running `tst_qml -input <file>`). |
| `dist/` | Build output, the sysext tree. Not committed. |

The two patched files (`/usr/libexec/armada/session-control`,
`/usr/share/inputplumber/capability_maps/ayaneo_pocket_micro2.yaml`) are never checked in whole.
`build.sh` copies each base file out of the pinned image, records its sha256 in
`usr/share/casdap/shadow-base.sha256`, applies the patch with `patch -p1` and fails on any reject.

## Player UI

QML module `Casdap`, built as the static library `casdapui` and linked into `casdap-player` and the
QML test runner `tst_qml`. `Ui` (singleton) holds the scale and every token: the Zune HD canvas is
272 x 480 u, `u = min(w/272, h/480) * s` portrait (`h >= w`), `min(w/480, h/272) * s` landscape,
`s` the user scale 0.8 to `sMax` in 0.1 steps (`sMax` 1.1 at 640x960, 1.0 at 960x640, 1080x1920 and
1920x1080). `canvasW`, `canvasH` are the window in u; `px(v)` and `fontPx(v)` (12 px floor) convert.
Type roles are objects (`size`, `style`, `caps`, `alpha`, `tracking`), `font(role)` builds the
`Qt.font` with family Selawik and the face picked by `styleName` (fontconfig reports Light and
Semilight both at weight 50). The five Selawik TTFs load through `FontLoader` from `CASDAP_FONT_DIR`
(default `/usr/share/fonts/casdap`, read by the `Env` singleton); `fontsReady` reports all five.
`Main.qml` writes `Ui.windowW` and `Ui.windowH` on every resize, so a new surface size reflows
without restart. `Motion` (singleton) holds the curves (`expOut6`, `expIn6`, `expIn1`, `expIn15` as
functions on a linear 0..1 progress, `decel` and `strip` as `Easing.BezierSpline` arrays, Qt stand
ins for the shell curves) and every duration. `Turnstile` applies the `Matrix4x4` page rotation about
a hinge with perspective `d = 2.3 * width` px. `PageStack` runs the exit animation (250 ms, expIn6)
to its end before the enter animation (350 ms, expOut6) starts, keeps the incoming page at opacity 0
and visible meanwhile, reports `inputLocked`, `phase` and `transitionKind`, and `completeAll()` jumps a running
transition to its end state. `push(component, properties, kind)` takes `turnstile` (default) or `fade`:
`fade` runs the outgoing page opacity 1 to 0 over 300 pop, then the incoming 0 to 1 over 300 with the
page's `enterEasing` (default pop); `pop()` reverses with the kind the page was pushed with.

`Pad` is a context property; `PadRouter` turns its actions into names (`up down left right a b x y
lb rb lt rt start back`) and hands each to the top page of the `PageStack` through
`handleAction(name, repeat)`, dropping it while a transition runs. Unhandled `start` emits
`nowPlaying()`, unhandled `back` emits `view()`; with the screen off and pocket controls on, `lb`,
`rb` and `start` call `Player.previous`, `next`, `playPause` and no page sees them. Keyboard fallback
on the same router: arrows, Return A, Escape or Backspace B, X, Y, Q and E for LB and RB, PageUp and
PageDown for LT and RT, Space Start, Tab Back/View; key autorepeat counts only for directions.
`Page` is the screen base: one `focusIndex`, A emits `activated(index)`, B pops, up/down and LT/RT go
to its `FocusList` when set. `FocusList` keeps the focused row inside `[CONTENT_TOP, H - ROW_HEIGHT]`
with a 100 ms easeIn `contentY` tween per step; LT/RT move one page of whole rows in 500 ms easeIn.
`FocusBar` is the 2 u white bar at `HUB_EDGE - 8` spanning the row, following in 100 ms sine.
`PressScale` scales its item to 0.96 while A is held, 100 ms linear each way.

Components under `components/`: `Glyph` draws one `GlyphPaths.js` path as a `Shape` scaled from the
32 u box. `Ring` holds a Glyph at half the ring size, stroke 3 u idle and 4 u focused, an accent arc
from `Ui.ringArcStart`, and runs the F4 swap (scale 0.7 to 1 in 400 ms, opacity in 200 ms) on every
glyph change after the first one. `CroppedHeader`, `PivotStrip`, `ListRow`, `Tile`, `LetterTile`,
`Ring` and `FocusBar` take `animated` (default true); cleared, every Behavior is off and a running
swap jumps to its end. `Backdrop` pauses its loops while `paused`, `screenOff` or not `visible`.
`Toast.show()` during the exit restarts the entrance from the strip's current position. The volume
overlay ring is `focused` inside the 0.4 group, so it reads at 0.4 (O20). `CroppedHeader` is 70 u
tall with the x-height bottom at 66 u in both orientations.

`OverlayLayer` sits above the `PageStack` in `Main.qml` (`power`, `bluez`, `player`, `screenOff`,
`topScreen`; every source may be null). `PowerBridge.volumeChanged(int)` and
`BluezModel.volumeChanged(int)` call `VolumeOverlay.show(level)`; the ring glyph and the bottom text
follow the phone while its transport is active, else the local track. A new `Player.current` id
shows the toast with artist and title unless the top page is now playing; the phone transport going
active or its title changing shows `from <phone>` and the title unless the top page is Bluetooth or now
playing. Pages carry `objectName` (`home`, `bluetooth`) for that check. The layer never takes focus;
pad input goes to the page below. With the screen off nothing is drawn and the level is kept.

Overlay mode (`casdap-player --overlay`, the game mode instance): `Main.qml` reads `OverlayBridge`
(`mapped`, `opacity`) into `overlay`, `overlayMapped` and `overlayFade`. The root is opaque `Ui.bg` as in
DAP mode, a full canvas `Ui.bg` rectangle is the first child of the content item, and the
content item follows the bridge opacity. `hidden` (`overlay && !overlayMapped`) feeds the screen off
path (`off` = `screenOff || hidden`) to `PadRouter`, `OverlayLayer` and the pages and completes running
transitions. The bridge owns window visibility in C++: the QML `visible` binding is false in overlay
mode, `show()` maps the window and `hide()` unmaps and destroys it after the fade. `visibility` is
`Window.AutomaticVisibility` in overlay mode,
`Window.FullScreen` in DAP mode. `PadRouter` pocket controls are off in overlay mode. gamescope hands
the output size (960 x 640 on the Pocket MICRO 2), so the overlay runs the landscape layouts at u 2.00.

`Home` is the root page: the menu (`music`, `bluetooth`, `playlists`, `history`, `settings`), with
Quickplay (`Pins`, `History`, `New`) peeking on the left in portrait. Every item carries an upper case
subtitle: `music` its pivots, `bluetooth` the phone name while connected, `PAIRING <seconds>` while
the pairing window is open, else `NOT CONNECTED`, `playlists` `N PLAYLISTS` when `playlistCount` is
set (default -1) else `NOW PLAYING SAVED`, `history` `RECENT NEW`, `settings` `OUTPUT LIBRARY DISPLAY
POWER`. Y plays shuffle all; focus is the 2 u bar left of the item with the other items at 0.6. Left or Back/View pans Quickplay
forward and shrinks the menu to a column on the right; right or B pans back. In landscape both sit side
by side. X on a Quickplay tile opens the pin sheet. Menu items whose screen does not exist yet log
`navigate <name>`. The entrance (menu items, focus bar, Quickplay peek) plays once when the page is created
with `animated` true; `animated` turning true later changes nothing.

`MusicHub` is the `music` page (`objectName` `music`): header `music`, pivots `artists`, `albums`,
`songs`, `genres`, `playlists`, content from CONTENT_TOP. Each pivot is a `FocusList` pane built from the
`Artists`, `Albums`, `Songs`, `Genres` context models (any model with `count` or `rowCount()` and `get(row)`
returning `id`, `title` or `name`, `subtitle`, `artPath`, `section`); the `section` letter (`#`, a..z)
groups the rows. Artists, songs and genres get a divider row per letter (ROW_HEIGHT, the 20 u letter at
HUB_EDGE on the LETTER_TILE baseline inset), skipped by focus; albums are a grid of `Ui.gridCols` cells
(3 portrait, 6 at W 483) with a `LetterTile` cell before each letter group and a `Tile` per album whose
caption runs under the next cell. Songs rows carry the 1 based position as the number; genres and
playlists are Bold upper case; `playlists` lists `NOW PLAYING` first with `N SONGS` from
`Player.queue.count` (no saved playlist model exists yet, `playlistsModel` is null). Up/down and LT/RT move
through the pane, left/right move within a grid row (no wrap), LB/RB switch pivots with T5 (outgoing pane
300 strip, incoming rows `min(i, 10) * 22` stagger over 380 strip, header parallax from the strip), no wrap at
either end. A on a song calls `Player.playTracks(ids, position)` with every song id in list order (without a
player it emits `play`); A on an album, artist or genre emits `open(kind, id)` (Main pushes `AlbumDetail` for
`album` and `ArtistDetail` for `artist`, logs `navigate genre <id>`); A on `NOW PLAYING` emits `nowPlaying`; A on a letter cell, or Back/View on an alphabetic pivot, emits
`jumpRequested` (Main logs `navigate jump`). LT/RT show the focused row's letter in the 120 u `JumpLetter`
(T15). X opens the sheet `play`, `add to queue`, `pin to quickplay`; Y is `add to queue`; `play` on a song
plays, every other verb emits `sheetAction(action, kind, id)` which Main logs. B pops. After
`Library.scanning` went true then false, the next model reset runs F12 on the shown pane (rows `x` -5 to 0,
`o` 0 to 1, 220 decel, `min(i, 10) * 50` stagger); a reset during the scan runs it when the scan ends.
`screenOff` or `animated` false lands the pivot, settles the rows and dismisses the letter.

`ArtistDetail` is the `artist` page, pushed with the turnstile (T3) by `open("artist", id)` from the hub:
header `artist`, pivots `albums`, `songs`, the name (20 u Bold upper case) at HUB_EDGE on the CONTENT_TOP
baseline, the ring row play, add, shuffle (32 u, top y 150, 56 u pitch from HUB_EDGE), `IN COLLECTION` on
baseline 200, content from 212 on a `Backdrop` (L1 pan) of the artist art. The rows come from `Library`:
`artistInfo(id)` (`name`, `artPath`: the first of the artist's albums with art), `artistAlbums(id)` resolved
through `albumInfo(id)` (`title`, `artist`, `year`, `artPath`), `artistTracks(id)` through `trackRows(ids)`
(`id`, `title`, `artist`, `album`, `albumId`, `durationMs`); a null library leaves the lists empty. `albums`
is a `FocusList` of DETAIL_ROW cells (TILE_L art at HUB_EDGE with a 20 u play ring inset 4 from its bottom
left, 13 u Bold upper case title and the year under it at HUB_EDGE + TILE_L + 8), `songs` the SONGS style
rows. Focus starts on the play ring: left/right move along the rings, down enters the list, up from its first
row returns to the rings; up/down and LT/RT move through the list, LB/RB switch pivots with T5 (no wrap).
A on play calls `Player.playTracks(ids, 0)` with every song id, on shuffle the same ids shuffled, on add
logs `add artist <id>` (no queue method exists yet); every ring A runs the F4 swap. A on an album emits
`openAlbum(id)` (Main pushes `AlbumDetail`), A on a song plays from that row. Y plays shuffled. X opens the
sheet `play`, `add to queue`, `pin to quickplay` on the focused row, or on the artist while the rings hold
focus; `play` plays (the artist, the album's tracks from `albumTracks(id)`, or from the song), every other
verb emits `sheetAction(action, kind, id)` which Main logs. B pops. Landscape keeps the name and rings in
the column to 200 and runs the label and the list from x 216 (`IN COLLECTION` with its cap top at
CONTENT_TOP, rows 12 u under its baseline). `screenOff` or `animated` false lands the pivot, settles the
rows and the sheet and pauses the backdrop.

`AlbumDetail` is the `album` page, pushed with the turnstile (T3) by `open("album", id)` from the hub or
`openAlbum` from the artist page: header `album`, `IN COLLECTION` on baseline 60, TILE_L art at HUB_EDGE y
76, the title (13 u Bold upper case, cap top on the art top) and the album artist (9 u, 13 u under) right of
the art, track rows from y 204 (`albumInfo(id)` and `albumTracks(id)`): 1 based number, title, the
duration `m:ss` right aligned at W - HUB_EDGE (absent when unknown), the accent dot left of the number on the
row whose id is `Player.current.id`. The `Backdrop` is the album art. Focus starts on row 0; up/down and
LT/RT move, A calls `Player.playTracks(ids, row)` with the album in disc and track order, Y emits
`sheetAction("add to queue", "album", id)`, X opens the sheet `play`, `add to queue`, `pin to quickplay`
(`play` from the row, the rest emit `sheetAction(action, "song", id)`), B pops. Landscape keeps the label
and art in the column to 200 with the title and artist under the art and runs the rows from x 216, y 60.

`NowPlaying` is the `nowplaying` page, pushed with the `fade` kind (T7) by Start on any other page, A on the
hub's `NOW PLAYING` row, A on the Bluetooth page's transport row and the Quickplay phone tile; Start with
no current track and no phone transport logs `now playing empty`. Portrait is three stacks on a `Backdrop`
(the current track's art, L1 pan): back ring (64 u, inset 4, decorative), `StatusStrip` top right (battery
from `StatusModel`, accent under 10 percent, Bluetooth glyph while a phone is connected, play or pause
glyph), artist baseline 117 and album 141 at EDGE, art square at y 155 with side `min(168, W - 2 EDGE,
H - 311)`, progress line at y H - 156 (fill accent from `Player.position`, read every 250 ms; elapsed and
remaining text under it, refreshed at 1 Hz), title baseline H - 118, the next three queue rows at EDGE + 40
and 18 u pitch, three rings (shuffle, repeat, rate) centered at 0.13, 0.5 and 0.87 of W on y H - 40.
Landscape: back ring and art (y 72, side `min(168, H - 106)`) with the progress line 8 u under it in the
left column, artist 60, album 84, title 116, next rows from 140 at x 216, rings on the same fractions of
`W - 216 - EDGE` from x 216. Focus starts on the ring row: left and right move it, A toggles the focused
ring with the F4 swap (`Player.toggleShuffle`, `cycleRepeat`, `rate`), up moves to the title row (2 u bar
at EDGE - 8) where A emits `openQueue` (Main pushes `QueueScreen`), down returns. LB and RB skip, X plays
or pauses, Y and Back/View emit `openQueue`, LT and RT seek 10 s, B and Start pop with the fade kind (T8).
A track change runs T9 (title, next rows and times out 500 easeOut with the art, in 400 easeIn with the
art scaling from 0.8 about (0.2, 0.5)); a new art source crossfades the backdrop over 2000 linear and
restarts its pan (T10). After 5000 ms without input the unfocused rings and the status strip fade to 0.4
over 380 decel, the focused ring stays at 1.0, the first input only restores them. With
`BluezModel.transportActive` the page shows the Bluetooth variant: `FROM <PHONE>` at baseline 97, artist,
album and title from the phone, a 64 u Bluetooth glyph in place of the art, the progress line only while
`durationMs` is set, `N OF M` in the first next row slot, rings play/pause, shuffle, repeat acting on the
phone (A on play/pause, X and Start play or pause, LB and RB skip, LT and RT rewind or fast forward until
release). The variant arrives with the T7 content fade and scale and leaves over 380 decel; local playback
is paused when the transport goes active and stays paused when it ends. `screenOff` or `animated` false
lands every animation and stops the timers.

`QueueScreen` is the `queue` page, pushed with the default turnstile kind (T11) by `openQueue` from now
playing (Main skips the push while the top page is already the queue): header `queue`, the count `N OF M`
(`Player.queue.index + 1` and `count`, `0 OF 0` when empty) in the 11 u upper case count role at HUB_EDGE on
baseline 60, SONGS style rows (1 based number, title, artist) in a `FocusList` from y 84, the current row
`playing` (accent title). Both orientations run the same stack. Focus starts on the current row; up/down and
LT/RT move through the list, A calls `Player.playAt(row)`, Y removes the focused row, X opens the sheet
(T12) with `remove`, `play next`, `move up`, `move down`; `play next` is absent on the current row and the
one after it, `move up` on the first row, `move down` on the last. `remove` calls `Queue.remove(row)`
(removing the current row is handled by the queue, the next track starts), `move up` and `move down` call
`Queue.move(row, row +- 1)`, `play next` moves the row to right after the current one; a moved row keeps
focus at its new position, a removed row leaves focus at the same index clamped to the end. Every queue
change (`rowsInserted`, `rowsRemoved`, `rowsMoved`, `modelReset`) rebuilds the rows and runs F12 from the
edited index on the visible rows at and below it (`x` -5 to 0, `o` 0 to 1, 220 decel, stagger
`min(i, 10) * 50` counted from the first affected visible row). B pops with T4; Start pops when the page below
is now playing, else falls through to the router (which opens now playing). `screenOff` or `animated` false
settles the rows, the scroll and the sheet.

Screenshot mode: `casdap-player --screenshot <screen> --size WxH --out file.png [--angle deg]`,
offscreen, software renderer. Screens: `blank`, `home`, `quickplay` (`--bluez off|connected`), `prompt` (the switch prompt over home), `hub`
(`--pivot artists|albums|songs|genres|playlists`, default `albums`; the `--library <dir>` models when
given, else sample rows), `nowplaying` (sample track and queue; `--bluez active` for the Bluetooth variant),
`queue` (a sample queue of 20 with row 3 playing), `artist` and `album` (a sample artist with two albums
and ten tracks, track 3 playing),
`components`, `overlays`, `volume`, `specimen` (every type role in its weight, labeled
with name and px), `motion` (30 rows mid turnstile at `--angle`, default 40). CMake cache var
`CASDAP_TEST_FONT_DIR` (default `../dist/usr/share/fonts/casdap`) feeds `CASDAP_FONT_DIR` to the QML
tests and the specimen shots; font dependent QML cases skip when the directory is empty.

## Builder

The builder container is `FROM` the exact Armada image the device runs, pinned by digest in
`build/base.env`. Compilers and `-devel` packages are installed at the EVR of the runtime already in
the image (`gcc`, `gcc-c++`, `libstdc++-devel` at the `libstdc++` EVR, `glibc-devel` at the `glibc`
EVR, `qt6-qtbase`, `qt6-qtdeclarative`, `gstreamer1`, `SDL3`, `pipewire`, `taglib` `-devel` at their runtime
EVR), falling back to koji when the mirrors moved on and to negativo17 when the runtime package came
from there. `toolchain.sh` fails the image build if any pinned runtime package changed. Build host
is any podman with arm64 binfmt (SePC); `ghcr.io/seilent/armada` pulls without a login.

## Recipes

| Recipe | Does |
|---|---|
| `just base` | Reads `IMAGE_VERSION`, `VERSION_ID` and the booted image digest from the device into `build/base.env`. |
| `just dist` | Builds the builder image and runs `build/build.sh`: copies `sysext/`, generates the patched files and hashes, installs the Selawik 1.01 TTFs (`build/fonts.sh`, sha256 pinned, cached in `build/cache/`) to `usr/share/fonts/casdap/`, writes `version` and `build-manifest`. |
| `just build` | `dist` plus `casdap-<ver>-armada<VERSION_ID>-arm64.raw` (squashfs, zstd). |
| `just dev` | Refuses when the device `IMAGE_VERSION`, `VERSION_ID` or digest differ from `dist/usr/share/casdap/build-manifest` (no force flag, run `just base && just dist`). Then rsyncs `dist/` to `/var/lib/extensions/casdap/`, `systemd-sysext refresh`, restarts polkit and `casdap-guard`, restarts inputplumber plus `armada-controller-type.service` when a file under `usr/share/inputplumber/` changed, `systemctl --user daemon-reload`, and restarts the user `pipewire pipewire-pulse wireplumber` when a file under `usr/share/pipewire/` changed. |
| `just dev --input` / `just dev-input` | `dev` with the inputplumber restart forced. |
| `just test-player` | ctest of `build/player` inside the builder (after `just dist`); prints `skip: no builder image` where the image is absent. |
| `just off` | Unmerge, remove `/var/lib/extensions/casdap`, restart polkit and inputplumber, re-apply the controller type. |
| `just test` | `tests/session-control-test.sh`, `tests/priv-test.sh`, `tests/hotkeyd-test.sh`, `tests/pad-test.sh`, `tests/intercept-test.sh`, `tests/keep-test.sh`, `tests/wait-x-test.sh`, `tests/overlay-unit-test.sh`, `tests/bt-test.sh`, then `just test-player`. Needs the stock `session-control` at `CASDAP_BASE_ROOT/usr/libexec/armada/` (defaults to `/`, else `../armada/system_files`). |

`CASDAP_DEVICE` overrides `armada@pm2`. Root steps on the device run through `ssh -t`
and `sudo`. Run the dev loop from game mode or ssh, not from inside the casdap session.

## Session

`casdap.desktop` runs `gamescope-session-plus casdap`. `sessions.d/casdap` sets
`CLIENTCMD=/usr/bin/casdap-player`; `/etc/gamescope-session-plus/sessions.d/casdap` (runtime, not
shipped) overrides it, for example `export CLIENTCMD=konsole`. gamescope runs with `--steam` and shows
only a window whose `STEAM_GAME` matches the root `GAMESCOPECTRL_BASELAYER_APPID`; with no Steam in the
session the player sets both to `CASDAP_GAMESCOPE_APPID` (`0x43415344`, exported by `sessions.d/casdap`)
on its own X window at start. A client without them stays invisible. Five sessions shorter than 60 s in a
row start `casdap-switch-back.service`.

Switching, as user `armada`, no root:

```console
$ systemctl start casdap-switch.service
$ systemctl start casdap-switch-back.service
```

`session-control switch-casdap` writes the SDDM autologin drop-in, asks Steam to exit with
`steam -shutdown` (explicit `HOME`, `XDG_RUNTIME_DIR`, `DBUS_SESSION_BUS_ADDRESS`,
`LD_LIBRARY_PATH`), waits for the Steam unit to stop and the seat0 session to close, and only
restarts sddm when that fails. From Plasma it logs out through `org.kde.Shutdown` after checking the
name has an owner. `switch-gamemode` stops `gamescope-session-plus@casdap.service` when it runs.
Every boot still resets to game mode.

Overlay mode: `casdap-player --overlay` on the Steam session display (`DISPLAY=:0`) is the same
player as the game mode instance. No SDL; the pad is `DbusPad` from InputPlumber. The X window is
created unmapped with `STEAM_OVERLAY` 1 and `GAMESCOPE_NO_FOCUS` 1 and the window state set to
fullscreen before the first map; gamescope draws it as its global overlay and keeps focus and game
audio with the game. `STEAM_GAME` and the baselayer appid are never set. `ShowOverlay` flips `OverlayShown` and
emits `PropertiesChanged`, writes `InterceptMode` 3, maps and fades in over 200 ms; `HideOverlay`
flips it back, writes 0, fades out over 150 ms, unmaps and destroys the X window (gamescope keeps an
unmapped window registered as its overlay and composites a blank frame over Steam; a destroyed one is
gone). The QML root and its page stack stay alive; the next `ShowOverlay` creates the window again
with the same atoms and state before mapping. `ToggleOverlay` during a fade reverses it from
the current opacity without a second map or destroy; `Show` during a show fade and `Hide` during a hide fade do
nothing. `SetScreenOff` and `ShowPowerMenu` are logged and ignored in this mode. While hidden the
4 Hz position poll is off (`position` is read from `playbin` for each save), `StatusModel` and the
Bluetooth interpolation are inactive, the QML root is not visible. With `NOTIFY_SOCKET` set the bridge
sends `READY=1` once after `org.casdap.Player` is registered and the hidden window is created, then
`WATCHDOG=1` every 7 s, shown or hidden. When `org.casdap.Player` is already owned at start the
overlay instance logs `another player owns org.casdap.Player, exiting` and exits 0; the DAP instance
warns and continues.

`casdap-overlay.service` (user unit) runs `casdap-player --overlay` with `DISPLAY=:0` and
`QT_QPA_PLATFORM=xcb`, no `CASDAP_GAMESCOPE_APPID`. `BindsTo=` and `After=`
`gamescope-session-plus@steam.service`, `Conflicts=` and `After=` `gamescope-session-plus@casdap.service`,
so the overlay stop is ordered before the DAP session start on every switch path and two players never
run at once. `gamescope-session-plus@steam.service.d/10-casdap-overlay.conf` wants it with every Steam
session; `ExecCondition=/usr/libexec/casdap/casdap-keep check` passes only while
`~/.config/casdap/keep` holds the running boot id (`/proc/sys/kernel/random/boot_id`, one line,
compared trimmed), else the unit ends `inactive`. `ExecStartPre=/usr/libexec/casdap/casdap-wait-x`
waits for gamescope's X server: every 200 ms it checks `/tmp/.X11-unix/X<n>` for `DISPLAY` (default
`:0`) and, once the socket exists, `xprop -root -display "$DISPLAY"`; exit 0 on the first successful
connection, 1 after `CASDAP_WAIT_X_MS` (default 20000) ms. `CASDAP_X_SOCKET_DIR` overrides the socket
directory. `TimeoutStartSec=40`, `Type=notify`, `NotifyAccess=main`, `WatchdogSec=15`,
`Restart=on-failure`, `RestartSec=2`, `StartLimitIntervalSec=0`; `ExecStopPost` runs `casdap-intercept 0`
on every exit. `casdap-keep check` exits 0 or 1, `casdap-keep clear` removes the marker (0 when absent);
`CASDAP_KEEP_FILE` and `CASDAP_BOOT_ID_FILE` override both paths. Only KEEP IN BACKGROUND writes the
marker; a reboot invalidates it.

Switch prompt (DAP mode only): `ShowSwitchPrompt` on `org.casdap.Player` opens a dialog titled `game mode`
with CLOSE PLAYER and KEEP IN BACKGROUND; focus starts on KEEP IN BACKGROUND while playing, else on
CLOSE PLAYER. Input is locked to the dialog while it is up; B closes it, a second `ShowSwitchPrompt`
closes it, an open sheet on the page below closes first. With `ScreenOff` true the call is logged and
ignored. Both items run `systemctl start casdap-switch-back.service` through a tracked `QProcess`
(`PowerBridge.keepInBackground()`, `closeAndSwitch()`). `gamescope-session-plus@casdap.service` has
`KillMode=mixed`, so the switch ends the player with SIGKILL and nothing runs at exit; everything is
written before the start. KEEP IN BACKGROUND saves `state.json` with `handoff` true, then writes
`~/.config/casdap/keep` with the running boot id (`Keep::write`, temp file next to it then rename),
then starts the switch; a failed state or marker write skips the start and is a failed switch.
CLOSE PLAYER saves `state.json` with `handoff` false, deletes the marker and starts the switch. The
DAP session client start deletes the marker as well; `--overlay`, `--screenshot` and `--scan-only`
runs never touch it. The start process ending with a non zero code, failing to start, or
`CASDAP_SWITCH_TIMEOUT_MS` (default 30000) passing with neither `finished` nor SIGTERM is a failed
switch: the marker is deleted, `state.json` is saved with `handoff` false, the prompt closed and the
toast `switch failed` shown. A second `keepInBackground()` or `closeAndSwitch()` while a start is
tracked is logged and does nothing.

Polkit boundary: any process running as `armada` may start `casdap-switch.service`,
`casdap-switch-back.service`, `casdap-priv@<verb>.service` and `armada-controller-type.service`.
The verbs are fixed words; `casdap-priv` exits 1 on anything else.

Pad target: `casdap-pad.service` (user unit, `BindsTo=` the casdap session) sets every InputPlumber
composite device to `xb360` plus `keyboard` through `SetTargetDevices`, so SDL players see an Xbox
pad. On session stop it starts `armada-controller-type.service`, which restores the configured type.
InputPlumber's own polkit rule grants its actions to group `wheel`, which `armada` is in.

## Input

The pad guide button (`BTN_MODE`) maps to `KeyF13` in the Pocket MICRO 2 capability map and reaches
only the `InputPlumber Keyboard` node; Steam does not see it (the front Home key keeps Guide). F13
does not flow during stick calibration.

`casdap-hotkeyd` (user unit, `Upholds=` from `default.target.d`) reads that node with `evtest` and
classifies each press by the running session. Hold 1 s in game mode starts `casdap-switch.service`.
While a title runs the first hold is ignored and a
second hold within 3 s switches. Tap in game mode runs `systemctl --user is-active
casdap-overlay.service`; `active` means a background player exists and the tap calls
`busctl --user --timeout=2 call org.casdap.Player /org/casdap/Player org.casdap.Player ToggleOverlay`,
anything else logs `no background player`. `Call failed: The name is not activatable` (the unit is
still starting) logs `overlay player starting`; `Call failed: Connection timed out` runs
`casdap-intercept get` and restarts `casdap-overlay.service` when it prints `3`, else logs `overlay
timeout, pad free`. hotkeyd never starts the unit. Hold in casdap calls `busctl --user call
org.casdap.Player /org/casdap/Player org.casdap.Player ShowSwitchPrompt` (the player opens the switch
prompt, a second hold closes it); `Call failed: The name is not activatable` (no player on the bus) logs
`no player, switching back` and starts `casdap-switch-back.service`, any other failure is logged. Tap in
casdap sends `PlayPause` to `org.mpris.MediaPlayer2.casdap`. In Plasma a hold switches; in any other session it
logs. In casdap the volume keys (`KEY_VOLUMEUP` on `gpio-keys`, `KEY_VOLUMEDOWN` on `pm8941_resin`,
list override `CASDAP_HOTKEYD_VOLUME_NAMES=a:b`, read with their own `evtest` streams) step
`wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@` by 5% per press and autorepeat (volume up also
unmutes the sink) and emit
`org.casdap.Hotkeys.VolumeChanged(d)` at `/org/casdap/Hotkeys` with the level from `wpctl get-volume`;
in every other session they are left to Steam. `CASDAP_HOTKEYD_DRY_RUN=1` (runtime drop-in under
`~/.config/systemd/user/`) logs `would:` lines instead of acting.

`/usr/libexec/casdap/casdap-intercept <0|3|get>` writes InputPlumber's `InterceptMode` on every
`/org/shadowblip/InputPlumber/CompositeDevice<N>` from `busctl --system tree --list`. Mode 3 takes the
gamepad away from Steam and delivers it as `org.shadowblip.Input.DBusDevice.InputEvent` on the
`devices/target/dbus*` paths while the keyboard target, and with it `KEY_F13`, keeps flowing; 0 gives it
back. `get` prints `3` when any device reads 3, else `0`. Runs as `armada`, no polkit rule needed.

## Bluetooth

`casdap-bt.service` (user unit, `BindsTo=` the casdap session, `Wants=` from
`gamescope-session-plus@casdap.service.d/10-casdap-bt.conf`) runs `casdap-bt`, the only Bluetooth
agent in casdap. `ExecStartPre` starts `casdap-priv@bt-class-headphones.service` (class
`0x6c0418`), `ExecStopPost` starts `casdap-priv@bt-class-default.service`. The agent registers
`org.bluez.Agent1` at `/org/casdap/agent` with capability `NoInputNoOutput` as the default agent,
sets `Discoverable=false` on start, sets `Pairable=false` after `RequestDefaultAgent` (bluez 5.87 turns it on there) and puts back the value it found on exit and exits 1 when `org.bluez` leaves the bus (`Restart=on-failure`).

Pairing window: `org.casdap.Bluetooth` at `/org/casdap/Bluetooth` on the session bus, `StartPairing(u
seconds)` (1 to 600, anything else raises `org.freedesktop.DBus.Error.InvalidArgs`), `StopPairing()`,
properties `PairingActive` (b) and `PairingRemaining` (u, counts down each
second while open), both with `PropertiesChanged`. `StartPairing` saves `Pairable`, `PairableTimeout`
and `DiscoverableTimeout`, sets `Pairable` and `Discoverable` true with both timeouts at the given
value and `Alias` to `ARMADA_DEVICE_NAME` from `device-env` (hostname without it). While open,
`RequestConfirmation` and `RequestAuthorization` accept (JustWorks, no MITM protection); closed, every
pairing request is answered with `org.bluez.Error.Rejected`. `RequestPinCode`, `RequestPasskey`,
`DisplayPinCode` and `DisplayPasskey` are always rejected. `StopPairing`, the timeout and SIGTERM
restore the saved values and set `Discoverable=false`. The window also closes that way when the gate
keeps a device that paired while it was open (journal `pairing window closed: paired <path>`) or
when an already paired A2DP source connects during it (`closed: connected <path>`); a device the
gate removes leaves it open. On exit it disconnects every connected A2DP source unless
`keepPhoneInGameMode=true` in `~/.config/casdap/settings.ini` (default false; `casdap-bt --print-keep`
prints `keep` or `drop`).

`AuthorizeService` accepts `110b`, `110c`, `110d`, `110e` and rejects every other uuid with a
`Rejected <uuid> <device>` journal line, so HFP (`111e`, `111f`), HSP (`1108`, `1112`) and HID
(`1124`, `1812`) never connect while casdap runs. `Trusted` is never set. Pairing gate: once a device
is `Paired` and `ServicesResolved`, a device without `0000110a` (A2DP source) in `UUIDs` is removed
with `Adapter1.RemoveDevice`; a device with `0000110a` stays even when it also lists a HID role. In
game mode no agent runs and a paired phone cannot connect; in Plasma
bluedevil prompts. `CASDAP_BT_SYSTEM_BUS` (bus address) and `CASDAP_BT_DEVICE_ENV` override the
system bus and the `device-env` path for tests.

## Audio

`pipewire.conf.d/50-casdap-rates.conf` sets `default.clock.allowed-rates` to 44100, 48000, 88200, 96000,
176400 and 192000 for the whole daemon while casdap is merged; `default.clock.rate` stays 48000. With only
one stream active, the graph and the PCM switch to that stream's rate when the sink starts. A concurrent
48 kHz stream keeps the graph at 48000 and the other stream is resampled. PipeWire reads the fragment at
start.

## Guard

`casdap-guard.service` runs at boot (`Upholds=` from `multi-user.target.d`) and after every dev loop.
It resolves the booted deployment from `ostree=` on the kernel command line and compares each file
in `shadow-base.sha256`; a mismatch logs one warning per file and lists the path in
`/run/casdap/drift`. Without a resolvable deployment it compares `IMAGE_VERSION` instead.

## Rebuild per Armada release

Every Armada release (new digest, `VERSION_ID` or pipewire minor) needs a casdap rebuilt against
it: `just base && just build`. A casdap built against an older image hides later changes to the
patched files while merged; the guard warns about it.

## Known limits

Symptoms of a sysext left unmerged after a bootc upgrade to another `VERSION_ID`:
`systemd-sysext status` lists nothing or marks casdap incompatible, the pad guide button opens the
Steam menu (InputPlumber's default Guide translation) and a hold does nothing, `casdap-switch.service` is unknown to systemd, no `casdap-*`
user units. Fix: install the asset for the new `VERSION_ID`.

The short session tracker `/tmp/chimeraos-short-session-tracker` is shared by every
gamescope-session-plus client. Short casdap sessions count against Steam's recovery (5 in 60 s each
rewrite the drop-in to desktop).

## Install contract

Release asset `casdap-<ver>-armada<VERSION_ID>-arm64.raw`, built against one image digest, both in
the release notes and in `usr/share/casdap/build-manifest`.

Install: pick the asset matching `VERSION_ID` in `/etc/os-release`, write it to
`/var/lib/extensions/casdap.raw`, then `systemd-sysext refresh`, `systemctl restart polkit
inputplumber casdap-guard`, `systemctl start armada-controller-type.service`, then as the user
`systemctl --user restart pipewire pipewire-pulse wireplumber` (or log in again). The inputplumber
restart drops the controller for a moment; run it outside a game. Uninstall: delete the file, run the
same commands. Nothing under `/etc` to clean.

The Armada side ships `casdap-check.service` (`ConditionPathExists=/var/lib/extensions/casdap.raw`,
`After=systemd-sysext.service`): passes when `/usr/lib/extension-release.d/extension-release.casdap`
exists after the merge, otherwise notifies and offers the asset for the new `VERSION_ID`. Until it
exists, the `just dev` refusal and the symptom list above are the only detection.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
