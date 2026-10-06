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
| `player/` | Qt 6 QML player with a C++ core (`casdapcore`). CMake project, built by `build.sh` inside the builder, installed to `/usr/bin/casdap-player`. Playback is `playbin`, audio only, sink from `CASDAP_AUDIO_SINK` (default `pulsesink`), gapless through `about-to-finish`. Queue, index, position, shuffle, repeat, `status` (`playing`, `paused`, `stopped`), `savedAt` (unix seconds), `handoff` and `stack` (the page stack as `name` and `id` pairs from each page's `objectName` and `pageId`, sheets, dialogs, the jump grid and the saver left out) persist in `~/.config/casdap/state.json`, written every 10 s while playing with `handoff` false, on quit and on SIGTERM with `handoff` true, on every overlay hide and on `PrepareForSleep(true)` with `handoff` false, and by the switch prompt before the switch starts (KEEP IN BACKGROUND `handoff` true, CLOSE PLAYER `handoff` false; the session switch ends the player with SIGKILL, so nothing is written at exit); a start resumes playing when `handoff` is true, `status` is `playing` and `savedAt` is under 60 s old, every other file restores the queue paused; both modes restore the saved `stack` at start (an id gone from the library truncates it at that page, no key gives the home). Every play start adds a `history` row. The library (`library.db`, `PRAGMA user_version` 2) reads `ARTISTS` or `ARTIST` tag values as a list, splits a single value on `; `, ` / ` and `, ` unless the whole value equals the album artist (not on ` feat. `), stores each per track in `track_artists(track_id, artist_id, pos)`, keeps `tracks.artist` as the joined display string and `albums.artist` as the album artist; `ArtistsModel` lists individual artists with album and song counts, `Library.artistAlbums(id)` and `artistTracks(id)` resolve through `track_artists`, and a version 1 database gets the table and every track marked for reread on the next scan. Pins live in `pins(id, kind, ref_id, pos)`: `Library.pin(kind, refId)` (kinds `album`, `artist`, `song`, `playlist`; appends at max pos + 1, false on a duplicate or an unknown kind), `unpin(kind, refId)`, `isPinned(kind, refId)`, signal `pinsChanged`; a scan deletes every pin whose ref no longer exists and emits `pinsChanged` when it removed one. `PinsModel` (context property `Pins`) lists the pins in pos order with roles `id` (the ref id), `kind`, `title`, `subtitle`, `artPath`, `section` (`pins`): album title, album artist and art; artist name, `N ALBUMS` and the first album art; song title, artist and its album art; playlist name, `N SONGS`, no art; a pin whose ref is missing is skipped. `Mpris` registers `org.mpris.MediaPlayer2.casdap` at `/org/mpris/MediaPlayer2` on the session bus (root and `Player` interfaces, XML in `player/dbus/`, installed to `share/dbus-1/interfaces`): `Identity` is `CASDAP`, no `DesktopEntry`, track ids are `/org/casdap/track/<id>`, `Volume` reads 1.0 and ignores writes, `OpenUri` plays one `file://` uri, `Seeked` and `PropertiesChanged` are emitted for status, metadata, shuffle and loop. `PowerBridge` registers `org.casdap.Player` at `/org/casdap/Player` (`SetScreenOff(b)`, `ShowPowerMenu()`, `ShowOverlay()`, `HideOverlay()`, `ToggleOverlay()`, `ShowSwitchPrompt()`, `ScreenOff`, `PlaybackStatus`, `OverlayShown`, signal `ScreenOffChanged`; the three overlay methods answer `org.casdap.Error.NotSupported` and `OverlayShown` reads false in DAP mode, `ShowSwitchPrompt` answers `NotSupported` in overlay mode and emits `showSwitchPrompt` to QML in DAP mode), listens to `org.casdap.Hotkeys.VolumeChanged(d)`, reads `/run/casdap/drift` and runs only the verbs `screen-off`, `game-mode`, `suspend`, `poweroff` (`/usr/libexec/casdap/casdap-power <verb>`, `CASDAP_POWER_BIN` overrides the path; `systemctl start casdap-switch-back.service`); settings live in `~/.config/casdap/settings.ini` (`output` `auto`, `speaker`, `headphones`; `btCodec`; `overlayToasts`; `roots`, `accent`, `scale`, `pocketControls`, `saverSeconds`, `screenOffSeconds`, `suspendMinutes`, `wifiOffScreenOff`, written 500 ms after the last change and on quit), battery comes from UPower `DisplayDevice` with a `/sys/class/power_supply` fallback, and local playback pauses while the Bluetooth transport is active. `CASDAP_BLUEZ_FAKE=1` wires `FakeBluezModel` in place of the Bluetooth model. Removing the current queue row starts the next one or stops when none follows. Pad input is SDL3 gamepad events (Guide never mapped), polled every 16 ms while a pad is open and the screen is on, every 1 s while no pad is open, every 50 ms with the screen off only when pocket controls are on, otherwise not at all; d-pad and stick repeat at 300 ms then 80 ms, `CASDAP_PAD_LOG=1` logs each action. `Pad` and `DbusPad` derive from `PadSource` (the `Action` enum, `screenOff`, `pocketControls`, `connected`, `actionName`, `action`, `pressed`, `released`); `main.cpp` hands `Pad` to QML as `Pad`, `DbusPad` under `--overlay`. `DbusPad` reads `org.shadowblip.Input.DBusDevice.InputEvent(s,d)` from InputPlumber on the system bus (`CASDAP_INPUTPLUMBER_BUS` overrides the address): `ui_up/down/left/right`, `ui_accept` A, `ui_back` B, `ui_context` X, `ui_action` Y, `ui_l1` LB, `ui_r1` RB, `ui_l2` LT, `ui_r2` RT, `ui_option` Start, `ui_select` Back, value >= 0.5 press, below release, same 300/80 repeat; `ui_guide`, `ui_quick`, `ui_l3` and unknown names are logged once and dropped; `connected` while a `devices/target/dbus*` path exists; `pocketControls` does nothing. Its `Intercept` writes `InterceptMode` on every `org.shadowblip.Input.CompositeDevice` from `GetManagedObjects` on `/org/shadowblip/InputPlumber`: 3 on `setShown(true)` and on a device added while shown, 0 on `setShown(false)` and in the destructor. |
| `tests/` | bash tests with stubbed `sudo`, `systemctl`, `loginctl`, `btmgmt`, `evtest`, `busctl`. `bt-test.sh` runs `bt-test.py` under `dbus-run-session` with a fake `org.bluez` (needs `dbus-python` and `python3-gobject`). `keep-test.sh` drives `casdap-keep` through `CASDAP_KEEP_FILE` and `CASDAP_BOOT_ID_FILE`; `power-test.sh` drives `casdap-power` with stubbed `busctl`, `systemctl`, `systemd-run`, `nmcli` and `CASDAP_RUNTIME_DIR`, `CASDAP_SETTINGS_FILE`, `CASDAP_CPU_BEFORE_FILE`; `powerd-test.sh` replays `evtest` lines and profile reads into `casdap-powerd`; `wait-x-test.sh` drives `casdap-wait-x` with a stubbed `xprop` and `CASDAP_X_SOCKET_DIR`; `overlay-unit-test.sh` greps `casdap-overlay.service` and its drop in under `sysext/` and runs `systemd-analyze --user verify` where the image units and the player binary exist (`skip:` otherwise). Player tests live in `player/tests/` (QTest, offscreen, generated FLAC and MP3 fixtures; `tst_mpris`, `tst_power`, `tst_systembluez`, `tst_dbuspad` and `tst_overlay` run under `dbus-run-session`, `tst_dbuspad` and `tst_overlay` against `fake_inputplumber.py`, `tst_overlay` and `tst_power` against `fake_login1.py` (a fake `org.freedesktop.login1` with `Inhibit` and `PrepareForSleep` on the private bus, `CASDAP_LOGIN1_BUS`), `tst_overlay` also against a unix datagram `NOTIFY_SOCKET`; each `player/tests/qml/tst_*.qml` is its own ctest entry `qml-<name>` running `tst_qml -input <file>`). |
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
(default `/usr/share/fonts/casdap`, read by the `Env` singleton, which also reports `version` and the
build manifest head as `manifest`); `fontsReady` reports all five.
`Main.qml` writes `Ui.windowW` and `Ui.windowH` on every resize, so a new surface size reflows
without restart. `Motion` (singleton) holds the curves (`expOut6`, `expIn6`, `expIn1`, `expIn15` as
functions on a linear 0..1 progress, `decel` and `strip` as `Easing.BezierSpline` arrays, Qt stand
ins for the shell curves) and every duration. `Turnstile` applies the `Matrix4x4` page rotation about
a hinge with perspective `d = 2.3 * width` px. `PageStack` runs the exit animation (250 ms, expIn6)
to its end before the enter animation (350 ms, expOut6) starts, keeps the incoming page at opacity 0
and visible meanwhile, reports `inputLocked`, `phase` and `transitionKind`, and `completeAll()` jumps a running
transition to its end state. `push(component, properties, kind)` takes `turnstile` (default) or `fade`:
`fade` runs the outgoing page opacity 1 to 0 over 300 pop, then the incoming over 300 with its backdrop at
pop and, for a page declaring `splitFade` (now playing), its content at easeIn (`Holder.backdropFade`,
`contentFade`; the content leaves over 250 easeIn, T7 and T8); `pop()` reverses with the kind the page was
pushed with and flies the outgoing page's `header` and `strip` up (`y` 0 to -0.25 H, opacity to 0, 250 expOut6)
alongside the turn (T4). A page declaring `pageMotion` `home` runs its own exit (the holder stays flat and
opaque for its `exitDuration`) and returns with no turn. The stack emits `entered(page, kind)` when a page's
enter animation ends (at once for a push or pop without animation, `kind` empty) and `leaving(page)` when a
`home` page's exit starts; `Page` relays them as `entered(first, kind)` and `leaving()` and sets `arrived`.
Every page with a row list (hub panes, Settings, Playlists, History, Queue, ArtistDetail, AlbumDetail) feathers
its visible rows once on first show, after `entered` (T2a): rows `x` +34 u to 0, opacity 0 to 1, 380 strip,
stagger `min(i, 10) * 22` from the first visible row; the rows wait at `x` +34 u and opacity 0 while `arriving`
(`animated`, screen on and `PageStack.animate`), and `animated` false, screen off or a stack restore lands them.

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
glyph change after the first one. `Tile` is the TILE_S art square with, while `captioned`, a 24 u caption
band under it: title and artist in the 9 u grid caption role, each as wide as the tile and clipped at its
right edge (no ellipsis), baselines at art bottom + 11 and + 22; the title goes to alpha 1 while focused,
the artist stays at 0.6. `CroppedHeader`, `PivotStrip`, `ListRow`, `Tile`, `LetterTile`,
`Ring` and `FocusBar` take `animated` (default true); cleared, every Behavior is off and a running
swap jumps to its end. `PageSurface` is `Ui.bg` under a vertical gradient from accent at alpha
`Ui.backdropStops[0][1]` (0.15) to accent at 0, the first child of every page except `Home` (`MusicHub`,
`QueueScreen`, `ArtistDetail`, `AlbumDetail`, `NowPlaying`, `Bluetooth`), so the turnstile swings a
visible page. `Backdrop` draws its `Ui.bg` fill, image and shade only with a source (`hasArt`), else the
`PageSurface` under it shows, and pauses its loops while `paused`, `screenOff` or not `visible`.
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
The page stack is kept across hide and show: `PageStack.serialize()` lists `name` and `id` per page
(`objectName`, `pageId`; `album` and `artist` carry their ids), Main writes it to `PlayerState.setStack`
on every depth change, and `restoreStack(list)` at start replays the saved stack without transitions
(`pushNamed` opens `music`, `bluetooth`, `playlists`, `history`, `settings`, `artist`, `album`,
`nowplaying`, `queue`; an `artist` or `album` id the library no longer knows, a `nowplaying` with no
track and no phone transport, or an unknown name truncates the stack there). B released on the home
menu in overlay mode (Quickplay and the pin sheet closed) emits `hideRequested` and Main calls
`OverlayBridge.hide()`. `Home.overlay`, `Bluetooth.overlay` and `SettingsScreen.overlay` carry the mode:
the settings GAME MODE row reads CLOSE PLAYER with a sheet `close player` whose accept calls
`OverlayBridge.closePlayer()`, PAIR A PHONE shows `PAIR IN DAP MODE` and A does nothing, the Bluetooth
PAIRING pivot shows `pair in dap mode` (11 u Light upper case at baseline CONTENT_TOP + 16) in place of
the ring and instructions and A does nothing. The saver and the screen off timer never arm in overlay
mode. Track toasts while hidden follow `Settings.overlayToasts` (row OVERLAY TOASTS under SAVER,
default off): with it on, a new `Player.current` id while hidden maps `toastWindow`, a second 40 u
`Window` (output width, black) carrying its own `Toast`, for the F11 slide, hold and slide out, then
unmaps it; `OverlayBridge.prepareToast()` creates the X window with the overlay atoms before the map and
`releaseToast()` destroys it after; InterceptMode and `OverlayShown` are untouched. While shown the
`OverlayLayer` toast runs and `toastWindow` stays hidden.

`Home` is the root page: the menu (`music`, `bluetooth`, `playlists`, `history`, `settings`), with
Quickplay (`Pins`, `History`, `New`) peeking on the left in portrait: the whole panel at scale 0.30 about
its top left, origin (0, 130), alpha 0.4 (QP_PEEK), so its headers and tiles read in the peek. Every item carries an upper case
subtitle: `music` its pivots, `bluetooth` the phone name while connected, `PAIRING <seconds>` while
the pairing window is open, else `NOT CONNECTED`, `playlists` `N PLAYLISTS` when `playlistCount` is
set (default -1) else `NOW PLAYING SAVED`, `history` `RECENT NEW`, `settings` `OUTPUT LIBRARY DISPLAY
POWER`. Y plays shuffle all; focus is the 2 u bar left of the item with the other items at 0.6. Left or Back/View pans Quickplay
forward (T6: the panel scales 0.30 to 1 and moves y 130 to 0 while the menu shrinks to a column on the
right at 0.6 whole, bar and focused label included); right or B pans back. In landscape both sit side
by side and the panel without focus is at 0.6 whole. The `casdap` watermark (14 u Light, alpha 0.08) sits in
the menu column at HOME_TEXT_X on baseline H - EDGE and hides while Quickplay is in front. Focus on the first row of a section scrolls the panel to that section's header (to the top when Pins is empty and History holds focus), so the headers stay reachable from the pad. Quickplay tiles carry
`section` and `kind`: `Pins` lists `Pins` (the phone tile first while a phone is connected, kind `phone`),
`History` the recent songs (kind `song`), `New` the newest albums (kind `album`). X on a tile opens the
sheet `pin to quickplay` (`unpin` on a Pins tile), A on it runs `Library.pin` or `unpin` with the tile's kind
and id; A on a tile plays it (`album` its tracks from `albumTracks`, `artist` its `artistTracks`, `song`
the song, anything else or no library logs `play <kind> <id>`). The hub, artist and album sheets read
`unpin from quickplay` in place of `pin to quickplay` while `Library.isPinned(kind, id)` is true, and Main
runs `Library.pin` or `unpin` for both verbs. A on `music`, `bluetooth`, `playlists`, `history` and
`settings` pushes the page of that name; Main binds `playlistCount` to `Library.playlistCount`. The entrance (menu items, focus bar, Quickplay peek) plays once when the page is created
with `animated` true; `animated` turning true later changes nothing. A on an item runs the T2 exit before the
pushed page turns in: every menu item flies `y` to -0.25 H and opacity to 0 over 250 expOut6, staggered 28 ms per
item outward from the focused one, the focus bar with the focused item and the Quickplay peek to 0 in the same
250; a pop back to home replays the T1 entrance with 0 stagger (T4) in place of a turn.

`MusicHub` is the `music` page (`objectName` `music`): header `music`, pivots `artists`, `albums`,
`songs`, `genres`, `playlists`, content from CONTENT_TOP. Each pivot is a `FocusList` pane built from the
`Artists`, `Albums`, `Songs`, `Genres` context models (any model with `count` or `rowCount()` and `get(row)`
returning `id`, `title` or `name`, `subtitle`, `artPath`, `section`); the `section` letter (`#`, a..z)
groups the rows. Artists, songs and genres get a divider row per letter (ROW_HEIGHT, the 20 u Regular letter at
HUB_EDGE on the LETTER_TILE baseline inset), skipped by focus; albums are a grid of `Ui.gridCols` cells
(3 portrait, 6 at W 483) with a `LetterTile` cell before each letter group and a `Tile` per album; the cell
is TILE_S + 24 tall, rows at TILE_S + 24 + GUTTER pitch. Artist rows carry the count line (`1 ALBUM, 3 SONGS`) in the upper case
count role. Songs rows carry the 1 based position as the number; genres and
playlists are Bold upper case; `playlists` lists `NOW PLAYING` first with `N SONGS` from
`Player.queue.count` (no saved playlist model exists yet, `playlistsModel` is null). Up/down and LT/RT move
through the pane, left/right move within a grid row (no wrap), LB/RB switch pivots with T5 (outgoing pane
300 strip, incoming rows `min(i, 10) * 22` stagger over 380 strip, header parallax from the strip), no wrap at
either end. A on a song calls `Player.playTracks(ids, position)` with every song id in list order (without a
player it emits `play`); A on an album, artist or genre emits `open(kind, id)` (Main pushes `AlbumDetail` for
`album` and `ArtistDetail` for `artist`, logs `navigate genre <id>`); A on `NOW PLAYING` emits `nowPlaying`; A on a letter cell, or Back/View on an alphabetic pivot, emits
`jumpRequested` and opens the `JumpGrid` over the hub (T14: the grid is full screen on `Ui.bg` over the list, swiveling in about
a horizontal center hinge from -30 degrees over 350 expOut6, out to 30 over 250 expIn1). `jumpTo(letter)` from
the grid snaps the pane so the letter's divider (or letter cell) sits at the top, focuses the first row under it
(the cell itself on albums) and shows the 120 u `JumpLetter` (T15). LT/RT show the focused row's letter in the
same `JumpLetter`. X opens the sheet `play`, `add to queue`, `pin to quickplay`; Y is `add to queue`; `play` on a song
plays, every other verb emits `sheetAction(action, kind, id)` which Main logs. B pops. After
`Library.scanning` went true then false, the next model reset runs F12 on the shown pane (rows `x` -5 to 0,
`o` 0 to 1, 220 decel, `min(i, 10) * 50` stagger); a reset during the scan runs it when the scan ends.
`screenOff` or `animated` false lands the pivot, settles the rows and dismisses the letter.

`JumpGrid` (`screens/JumpGrid.qml`) is the letter grid hosted by the hub: `exit` top left in the 7 u overlay
label role, the 27 `LetterTile` cells (`#` first, then a..z) in `Ui.jumpCols` columns (5 portrait, 9 landscape)
with the JUMP_GRID tile and 8 u gap from EDGE, the first row starting after `Ui.jumpExitCells` (2) empty slots
so `exit` sits in the first slot, the block centered vertically (`Ui.jumpTop`). `open(letters, letter)` takes
the letters that have rows and the hub's focused letter; a letter without rows draws at 0.4 and is skipped;
left/right step to the next letter with rows, up/down the next in the same column. A emits `jumpTo(letter)`
and closes, B, Back/View and Start close with the hub focus unchanged. `screenOff` lands the swivel.

`PlaylistsScreen` is the `playlists` page: header `playlists`, the count `N PLAYLISTS` in the 11 u count role
at HUB_EDGE on the pivot row baseline, the hub PLAYLISTS rows in a `FocusList` from CONTENT_TOP: `NOW PLAYING`
first with `N SONGS` from `Player.queue.count`, then every row of the `Playlists` context model (roles `id`,
`title`, `subtitle`), all Bold upper case with the play ring. A on `NOW PLAYING` emits `nowPlaying`, A on a
playlist calls `Player.playTracks(Library.playlistTracks(id), 0)` (without a player or library it emits
`play("playlist", id)`); X opens the sheet `play`, `add to queue`, `pin to quickplay` (or `unpin from
quickplay`), Y is `add to queue`, both emit `sheetAction`. A playlist model change rebuilds the rows with F12
and swaps the count text with F7 (opacity 1 to 0.2 to 1, 200 + 200). B pops.

`HistoryScreen` is the `history` page: header `history`, pivots `recent`, `new`, each a `FocusList` of rows
holding `Ui.historyCols` (2) uncaptioned QP_TILE tiles at HUB_EDGE from CONTENT_TOP, row pitch QP_TILE +
QP_GUTTER, from the `HistoryRecent` (kind `song`) and `HistoryNew` (kind `album`) models. Up/down and LT/RT
move through the rows, left/right across the two tiles, LB/RB switch pivots with T5 (outgoing pane 300 strip,
incoming rows `x` +-34 u to 0 with `min(i, 10) * 22` stagger over 380 strip, header parallax from the strip). A
model change on the shown pivot rebuilds its rows with F12. A plays the song (`playTracks([id], 0)`) or the album
(`Library.albumTracks(id)`), without a player it emits `play(kind, id)`; X opens the sheet `play`, `add to
queue`, pin or unpin, Y is `add to queue`. B pops. `screenOff` or `animated` false lands the pivot.

`SettingsScreen` is the `settings` page: header `settings`, a scrolling column from CONTENT_TOP of four group
labels (the 8 u Regular upper case section label role at HUB_EDGE, ROW_HEIGHT tall, baseline 8 u above the
row bottom, skipped by focus) and SETTINGS_PITCH rows (17 u Bold upper case title at HUB_EDGE, 11 u Light
upper case value right aligned at W - HUB_EDGE; focused 1.0 and 0.85, others 0.6 and 0.4). OUTPUT: `OUTPUT`
(`AUTO`, `SPEAKER`, `HEADPHONES` from `Settings.output`; the value reads `NO JACK` while it is `headphones`
and `OutputControl.jackPresent` is false, the setting is kept), `BLUETOOTH CODEC` (`AUTO`, `APTX`, `AAC`,
`LDAC`, `SBC` on `Settings.btCodec`), `SAMPLE RATE FOLLOW` (read only, `ON <AudioInfo.graphRate>`, `ON`
without a rate), `REPLAYGAIN` (read only `OFF`), `PAIR A PHONE` (A emits `pairPhone`, Main pushes
`Bluetooth` on its PAIRING pivot; in overlay mode the value reads `PAIR IN DAP MODE` and A does nothing).
LIBRARY: `LIBRARY FOLDERS` (`N ON`; A opens one sub row per folder in
`folders` plus `Settings.roots`, path as written in 11 u Light with `ON` or `OFF`, A toggles it in
`Settings.roots`). DISPLAY: `ACCENT` (the five `Ui.accentOptions` as 12 u swatches at 4 u gap, the current
one with the 2 u border; left/right and A move it and write `Ui.accent` and `Settings.accent`),
`ORIENTATION` (read only, the current orientation), `SCALE` (0.8 to `Ui.sMax` in 0.1 steps; `Ui.userScale` and
`Settings.scale` follow live, focus and the scroll are kept), `SAVER` (10, 15, 30 s, 1, 2, 5 min on
`Settings.saverSeconds`), `OVERLAY TOASTS` (`ON`, `OFF` on `Settings.overlayToasts`). POWER: `SCREEN OFF AFTER`
(`OFF`, 1, 2, 5, 10, 30 MIN on `Settings.screenOffSeconds`),
`SUSPEND AFTER` (`OFF`, 5, 10, 30, 60 MIN on `Settings.suspendMinutes`), `POCKET CONTROLS` (`ON`, `OFF`),
`WI-FI WHEN OFF` (`KEEP`, `OFF` on `Settings.wifiOffScreenOff`), `GAME MODE` (A opens the sheet `game mode`
with `switch to game mode`, T12; accepting calls `PowerBridge.runVerb("game-mode")`, or `closeAndSwitch()` on
a bridge without `runVerb`; in overlay mode the row reads `CLOSE PLAYER`, the sheet `close player` and
accepting calls `OverlayBridge.closePlayer()`). `ABOUT` is the last row with the version (`Env.version`) as its value; A opens
read only lines in the value role: `ARMADA <IMAGE_VERSION>` and `IMAGE <digest>` from `Env.manifest` (the
`key=value` head of `/usr/share/casdap/build-manifest`, `CASDAP_MANIFEST_FILE` overrides), `DRIFT N FILES`
while `PowerBridge.driftPaths` is not empty, `TRACK 44100 HZ 16 BIT` from `Player.current.sampleRate` and
`bitDepth`, `OUTPUT 44100 S24_LE` from `AudioInfo.pcmFormat`. Left/right step a value (A steps it too),
read only rows ignore them, a changed value text runs F7 (opacity 1 to 0.2 over 200, swap, 0.2 to 1 over 200).
The sub rows opened by LIBRARY FOLDERS and ABOUT arrive with F12 from the row under the opener.
LT/RT page by the content height. B pops. Main applies `Settings.scale` and `Settings.accent` to `Ui` at start.

`ArtistDetail` is the `artist` page, pushed with the turnstile (T3) by `open("artist", id)` from the hub:
header `artist`, pivots `albums`, `songs`, the name (11 u Bold upper case) at HUB_EDGE on a baseline 15 u
under the pivot baseline (110 portrait, 94 landscape), the ring row play, add, shuffle (32 u, top y 150, 56 u pitch from HUB_EDGE), `IN COLLECTION` on
baseline 200, content from 212 on a `Backdrop` (L1 pan) of the artist art. The rows come from `Library`:
`artistInfo(id)` (`name`, `artPath`: the first of the artist's albums with art), `artistAlbums(id)` resolved
through `albumInfo(id)` (`title`, `artist`, `year`, `artPath`), `artistTracks(id)` through `trackRows(ids)`
(`id`, `title`, `artist`, `album`, `albumId`, `durationMs`); a null library leaves the lists empty; a new `artistId` reloads them with F12 on the shown pivot. `albums`
is a `FocusList` of DETAIL_ROW cells (TILE_L art at HUB_EDGE with a 32 u play ring inset 8 from its top
right, 13 u Bold upper case title on baseline art top + 28 wrapping to two lines at pitch 15 and clipped at
W, the year under the last line, both at HUB_EDGE + TILE_L + 8), `songs` the SONGS style
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
`openAlbum` from the artist page, laid out as the now playing top stack: no header word, back ring (64 u,
inset 4, decorative) top left, `StatusStrip` top right, the album artist (20 u Bold upper case) on baseline
117 and the title (20 u Light upper case) on 141 at EDGE, TILE_L art at EDGE y 155, track rows from y 283
(art bottom + 12; `albumInfo(id)` and `albumTracks(id)`, a new `albumId` reloads them with F12): 1 based number, title, the
duration `m:ss` right aligned at W - HUB_EDGE (absent when unknown), the accent dot left of the number on the
row whose id is `Player.current.id`. The `Backdrop` is the album art. Focus starts on row 0; up/down and
LT/RT move, A calls `Player.playTracks(ids, row)` with the album in disc and track order, Y emits
`sheetAction("add to queue", "album", id)`, X opens the sheet `play`, `add to queue`, `pin to quickplay`
(`play` from the row, the rest emit `sheetAction(action, "song", id)`), B pops. Landscape keeps the back
ring and the art (y CONTENT_TOP) in the column to 200, artist on baseline 60 and title on 84 from x 216, and
runs the rows from x 216, y CONTENT_TOP.

`NowPlaying` is the `nowplaying` page, pushed with the `fade` kind (T7) by Start on any other page, A on the
hub's `NOW PLAYING` row, A on the Bluetooth page's transport row and the Quickplay phone tile; Start with
no current track and no phone transport logs `now playing empty`. Portrait is three stacks on a `Backdrop`
(the current track's art, L1 pan): back ring (64 u, inset 4, decorative), `StatusStrip` top right (battery
from `StatusModel`, accent under 10 percent, Bluetooth glyph while a phone is connected, play or pause
glyph), artist baseline 117 and album 141 at EDGE, art square at y 155 with side `min(168, W - 2 EDGE,
H - 311)`, progress line at y H - 156 from EDGE to the art right edge (fill accent from `Player.position`,
read every 250 ms; elapsed at EDGE and remaining right aligned at the art right edge under it, refreshed at
1 Hz), title baseline H - 118, the next three queue rows at EDGE + 40
and 18 u pitch, three rings (shuffle, repeat, rate) centered at 0.13, 0.5 and 0.87 of W on y H - 40.
Landscape: back ring and art (y 72, side `min(168, H - 106)`) with the progress line 8 u under it and as
wide as the art in the
left column, artist 60, album 84, title 116, next rows from 140 at x 216, rings on the same fractions of
`W - 216 - EDGE` from x 216. Focus starts on the ring row: left and right move it, A toggles the focused
ring with the F4 swap (`Player.toggleShuffle`, `cycleRepeat`, `rate`), up moves to the title row (2 u bar
at EDGE - 8) where A emits `openQueue` (Main pushes `QueueScreen`), down returns. LB and RB skip, X plays
or pauses, Y and Back/View emit `openQueue`, LT and RT seek 10 s, B and Start pop with the fade kind (T8).
A track change runs T9 (title, next rows and times out 500 easeOut with the art, in 400 easeIn with the
art scaling from 0.8 about (0.2, 0.5)); a new art source crossfades the backdrop over 2000 linear and
restarts its pan (T10). After 5000 ms without input the unfocused rings and the status strip fade to 0.4
over 380 decel, the focused ring stays at 1.0, the first input only restores them. With
`BluezModel.transportActive` the page shows the Bluetooth variant: `FROM <PHONE>` at baseline 97 (39 in
landscape, `Ui.npFromBaselineLandscape`), artist,
album and title from the phone, a 64 u Bluetooth glyph in place of the art, the progress line only while
`durationMs` is set, `N OF M` in the first next row slot, rings play/pause, shuffle, repeat acting on the
phone (A on play/pause, X and Start play or pause, LB and RB skip, LT and RT rewind or fast forward until
release). The variant arrives with the T7 content fade and scale and leaves over 380 decel; local playback
is paused when the transport goes active and stays paused when it ends. `screenOff` or `animated` false
lands every animation and stops the timers.

`QueueScreen` is the `queue` page, pushed with the default turnstile kind (T11) by `openQueue` from now
playing (Main skips the push while the top page is already the queue): header `queue`, the count `N OF M`
(`Player.queue.index + 1` and `count`, `0 OF 0` when empty) in the 11 u upper case count role at HUB_EDGE on
the pivot row baseline (95 portrait, 79 landscape), SONGS style rows (1 based number, title, artist) in a
`FocusList` from CONTENT_TOP, the current row
`playing` (accent title). Both orientations run the same stack. Focus starts on the current row; up/down and
LT/RT move through the list, A calls `Player.playAt(row)`, Y removes the focused row, X opens the sheet
(T12) with `remove`, `play next`, `move up`, `move down`; `play next` is absent on the current row and the
one after it, `move up` on the first row, `move down` on the last. `remove` calls `Queue.remove(row)`
(removing the current row is handled by the queue, the next track starts), `move up` and `move down` call
`Queue.move(row, row +- 1)`, `play next` moves the row to right after the current one; a moved row keeps
focus at its new position, a removed row leaves focus at the same index clamped to the end. Every queue
change (`rowsInserted`, `rowsRemoved`, `rowsMoved`, `modelReset`) rebuilds the rows and runs F12 from the
edited index on the visible rows at and below it (`x` -5 to 0, `o` 0 to 1, 220 decel, stagger
`min(i, 10) * 50` counted from the first affected visible row); a changed count text swaps with F7. B pops with T4; Start pops when the page below
is now playing, else falls through to the router (which opens now playing). `screenOff` or `animated` false
settles the rows, the scroll and the sheet.

`Saver` (`screens/Saver.qml`) replaces the view after `Settings.saverSeconds` without pad input while
`Player.status` is playing (DAP mode only; the timer `saverTimer` in `Main.qml` restarts on every routed
action and never arms in overlay mode, with the screen off, while paused, or while the switch prompt or
the power menu is up): `Ui.bg`, the 56 u art square at EDGE with its bottom 24 u above H (`Ui.saverArtY`),
title 32 u Light, artist 20 u Light at 0.85 and album 16 u Light at 0.6 right of it at EDGE + 56 + 8
(`Ui.saverTextX`; title baseline at the art top + the 32 u cap height, the next two one face size each
under it), the 50 u Light clock right aligned at W - EDGE with its cap top at EDGE (`Ui.saverClockBaseline`),
refreshed at 1 Hz (L4; `fixedClock` pins it for shots). T16: the view fades to 0 and the saver 0 to 1 over 380
decel through `progress` (the `PageStack` opacity is `1 - saver.progress`). 7000 ms after the entrance a
black layer fades to 0.6 over 380 decel (T17, `dimmed`). The text group drifts `y` 8 to -27 u over 18 000 ms
linear, reverse infinite (L2), paused with the screen off. Any pad action goes to `Saver.hide()` through
`PadRouter.saver` and is otherwise dropped (the page below sees nothing); the saver timer rearms when the
page is back. `screenOff` lands the fades and stops the clock, the dim timer and the drift.

`PowerMenu` (`screens/PowerMenu.qml`) is a `Dialog` titled `power` with SCREEN OFF, GAME MODE, SUSPEND,
POWER OFF (3.13, T13), opened by `PowerBridge.showPowerMenu` (DAP mode only; with the screen off the signal
is logged and ignored, an open sheet, the switch prompt and the saver close first). A on SCREEN OFF, SUSPEND
and POWER OFF closes the menu and calls `PowerBridge.runVerb` with `screen-off`, `suspend` or `poweroff`;
A on GAME MODE closes the menu and opens the switch prompt instead. B, Start and Back/View close it. While
the menu or the switch prompt is up `PageStack.dialogFade` follows the scrim and every page reads it as
`dialogUp` and `focusOpacity`, so the focus bar, border and ring stroke under the dialog hide for the T13 300
and return on close. `ShowSwitchPrompt` while the menu is up closes the menu first.

Screen off (`PowerBridge.screenOff` true): Main completes every running transition, overlay animation,
dialog and the saver (the pre saver page is what returns), the root fill and the `Ui.bg` rectangle turn
black, and after the next `frameSwapped` (one black frame, O64) the page stack and the overlay layer are
hidden (`rootHidden`); the saver and screen off timers stop. `PadRouter` drops every action except, with
pocket controls on, `lb`, `rb`, `start` to `Player.previous`, `next`, `playPause`. `screenOff` false shows
the same page again with no transition, restores `Ui.bg` and restarts both timers from zero. The screen off
timer (`screenOffTimer`, `Settings.screenOffSeconds`) runs `PowerBridge.runVerb("screen-off")` after that
long without a routed action, playing or not and with a dialog up; OFF (0) never arms, overlay mode never
arms, a tracked switch (`PowerBridge.switching`) cancels it, every routed action and `screenOff` false
restart it.

Screenshot mode: `casdap-player --screenshot <screen> --size WxH --out file.png [--angle deg] [--overlay]`,
offscreen, software renderer. Screens: `blank`, `home`, `quickplay` (`--bluez off|connected`), `prompt` (the switch prompt over home), `power` (the power menu over home), `saver` and `saver-dim` (the saver with the clock fixed at 7:13, the dim variant at 0.6), `hub`
(`--pivot artists|albums|songs|genres|playlists`, default `albums`; the `--library <dir>` models when
given, else sample rows), `nowplaying` (sample track and queue; `--bluez active` for the Bluetooth variant),
`queue` (a sample queue of 20 with row 3 playing), `artist` and `album` (a sample artist with two albums
and ten tracks, track 3 playing), `settings` (sample values, two folders, a FLAC track and `44100 S24_LE`),
`jump` (the grid over the hub pivot of `--pivot`, default `albums`), `playlists` (`NOW PLAYING` and four
sample playlists), `history` (seven recent songs and five new albums),
`components`, `overlays` (the volume box clamped to the 272 x 420 or 480 x 260 minimum canvas), `volume`, `specimen` (every type role in its weight, labeled
with name and px), `motion` (30 rows mid turnstile at `--angle`, default 40). `--overlay` renders `home` and
`settings` in overlay mode (CLOSE PLAYER and `PAIR IN DAP MODE` rows). CMake cache var
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
| `just dist` | Builds the builder image and runs `build/build.sh`: copies `sysext/`, generates the patched files and hashes, installs the Selawik 1.01 TTFs (`build/fonts.sh`, sha256 pinned, cached in `build/cache/`) to `usr/share/fonts/casdap/`, runs `build/aptx.sh` (the aptX codec rpms, sha256 pinned, cached in `build/cache/aptx/`, into `usr/lib64/`), writes `version` and `build-manifest`. |
| `just build` | `dist` plus `casdap-<ver>-armada<VERSION_ID>-arm64.raw` (squashfs, zstd). |
| `just dev` | Refuses when the device `IMAGE_VERSION`, `VERSION_ID` or digest differ from `dist/usr/share/casdap/build-manifest` (no force flag, run `just base && just dist`). Then rsyncs `dist/` to `/var/lib/extensions/casdap/`, `systemd-sysext refresh`, restarts polkit and `casdap-guard`, restarts inputplumber plus `armada-controller-type.service` when a file under `usr/share/inputplumber/` changed, `systemctl --user daemon-reload`, and restarts the user `pipewire pipewire-pulse wireplumber` when a file under `usr/share/pipewire/`, `usr/lib64/spa-0.2/` or `usr/lib64/libfreeaptx*` changed. |
| `just dev --input` / `just dev-input` | `dev` with the inputplumber restart forced. |
| `just test-player` | ctest of `build/player` inside the builder (after `just dist`); prints `skip: no builder image` where the image is absent. |
| `just off` | Unmerge, remove `/var/lib/extensions/casdap`, restart polkit and inputplumber, re-apply the controller type. |
| `just test` | `tests/session-control-test.sh`, `tests/priv-test.sh`, `tests/hotkeyd-test.sh`, `tests/pad-test.sh`, `tests/intercept-test.sh`, `tests/keep-test.sh`, `tests/power-test.sh`, `tests/powerd-test.sh`, `tests/wait-x-test.sh`, `tests/overlay-unit-test.sh`, `tests/bt-test.sh`, then `just test-player`. Needs the stock `session-control` at `CASDAP_BASE_ROOT/usr/libexec/armada/` (defaults to `/`, else `../armada/system_files`). |

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
warns and continues. CLOSE PLAYER in the overlay settings (`OverlayBridge.closePlayer()`) writes
`InterceptMode` 0, deletes the keep marker, saves `state.json` with `handoff` false and quits 0, so the
unit ends inactive. Every hide saves `state.json` with `handoff` false and the current stack.

Sleep (`SleepInhibitor`, `src/core/`): both modes subscribe to `org.freedesktop.login1.Manager`
`PrepareForSleep` on the system bus (`CASDAP_LOGIN1_BUS` overrides the address for tests) and hold a
delay inhibitor, `Inhibit("sleep", "casdap", <why>, "delay")`, taken at start and after every resume.
Overlay mode (`why` `hide overlay`): `PrepareForSleep(true)` hides the overlay when shown (InterceptMode 0,
the 150 ms fade, unmap, destroy, save), then releases the fd; hidden it writes 0, saves and releases at
once; `PrepareForSleep(false)` writes 0 once more and takes a new fd. DAP mode (`why` `save state`,
`PowerBridge.enableSleepWatch()`): `PrepareForSleep(true)` pauses a playing pipeline, saves `state.json`
with `handoff` false and releases the fd; `PrepareForSleep(false)` runs `casdap-power screen-on` through a
tracked `QProcess` (`wakeFinished(ok)`), takes a new fd and leaves playback paused. logind waits for a
delay inhibitor at most `InhibitDelayMaxSec` (5 s by default), so the pause and the save run synchronously
in the signal handler.

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
The verbs are fixed words (`bt-class-headphones`, `bt-class-default`, `backlight-off`, `backlight-on`,
`cpu-dap`, `cpu-off`, `cpu-restore`); `casdap-priv` exits 1 on anything else.

CPU and GPU floor: the three `cpu-*` words write cpufreq and devfreq under `CASDAP_SYSFS_ROOT` (default
`/sys`) and save the values they replace in `CASDAP_CPU_BEFORE_FILE` (default `/run/casdap/cpu-before`,
one `path value` line per `policy*/scaling_max_freq`, `cpuN/online`, GPU `governor` and `max_freq`, temp file
then rename, written only while absent). `cpu-dap` saves, sets `online` 1 on cpu1 to cpu3,
`policy0/scaling_max_freq` 787200, `online` 0 on cpu4 to cpu7, GPU `governor` simple_ondemand and `max_freq`
305000000. `cpu-off` saves, sets `online` 0 on cpu1 to cpu7 and `policy0/scaling_max_freq` 518400.
`cpu-restore` sets every `online` to 1, writes every other saved line back and removes the file; without a
file it only brings the cores online. A refused write is logged, the remaining writes still run and the word
exits 1 with the save file kept. `org.armada.Power` is never touched.

Power: `casdap-power <verb>` (`/usr/libexec/casdap/`, runs as `armada`) has `screen-off`, `screen-on`,
`toggle`, `suspend`, `poweroff`, `idle-check` and `cpu-apply`; anything else exits 1. Markers live under
`CASDAP_RUNTIME_DIR` (default `$XDG_RUNTIME_DIR/casdap-power`): `off`, `wifi`, `idle-since`, `screen-on-ms`.
Settings come from `CASDAP_SETTINGS_FILE` (default `~/.config/casdap/settings.ini`, keys `wifiOffScreenOff`,
`suspendMinutes`). `screen-off`, `toggle` and `cpu-apply` exit 1 unless `systemctl --user is-active
gamescope-session-plus@casdap.service` prints `active`; `screen-on` runs in every state. `screen-off`, in
order: the `off` marker, `busctl --user call org.casdap.Player /org/casdap/Player org.casdap.Player
SetScreenOff b true` (a failed call is logged), `casdap-priv@backlight-off.service`,
`casdap-priv@cpu-off.service`, `nmcli radio wifi off` with the `wifi` marker when `wifiOffScreenOff=true`
and the marker is absent, then `systemctl --user stop casdap-idle.timer` and `systemd-run --user
--unit=casdap-idle --on-active=60 --on-unit-inactive=60 --timer-property=AccuracySec=1min casdap-power
idle-check`. `screen-on` reverses: stops the timer, `nmcli radio wifi on` when the `wifi` marker exists,
then with the session `active` starts `cpu-restore` (only without the `off` marker and with
`CASDAP_CPU_BEFORE_FILE` present) and `cpu-dap`, in any other state `cpu-restore`; then
`casdap-priv@backlight-on.service`, `SetScreenOff b false` (session `active` only), removes the `off` marker
and writes `screen-on-ms`. `toggle` runs `screen-on` with the `off` marker, else `screen-off`, and does nothing
within 2 s of `screen-on-ms` (`CASDAP_POWER_DEBOUNCE_MS`). `cpu-apply` starts `cpu-off` with the `off` marker,
else `cpu-dap`. `idle-check` exits 0 outside the session; busy (`PlaybackStatus` contains `Playing`, or any
`org.bluez.MediaTransport1` under `/org/bluez` on the system bus has `State` `active`) or `suspendMinutes`
0 removes `idle-since`; idle writes it with the current unix time when absent and runs `systemctl suspend`
once it is `suspendMinutes` old. `suspend` and `poweroff` run `systemctl suspend` and `systemctl poweroff`.
Every verb is idempotent. `gamescope-session-plus@casdap.service.d/30-casdap-power.conf` wants
`casdap-powerd.service` and runs `ExecStopPost=/usr/libexec/casdap/casdap-power screen-on`, so every session
stop restores backlight, cpufreq, devfreq and Wi-Fi and stops the timer.

`casdap-powerd.service` (user unit, `PartOf=`, `BindsTo=` and `After=` the casdap session, `Restart=always`,
`RestartSec=1`, `StartLimitIntervalSec=0`) runs `casdap-powerd` under `systemd-inhibit
--what=handle-power-key:handle-suspend-key --who=casdap-powerd --mode=block`, so logind leaves the power key
alone while casdap runs. The daemon finds the node named `pm8941_pwrkey` (`CASDAP_POWERD_DEVICE_NAME`) through
`/sys/class/input/*/device/name`, reads it with `evtest` and reads `org.armada.Power` `Profile` with `busctl
--system get-property` every 5 s (`CASDAP_POWERD_POLL_SEC`). `KEY_POWER` released under 1000 ms
(`CASDAP_POWERD_LONG_MS`) runs `casdap-power toggle`; held 1000 ms, with the `off` marker it runs
`casdap-power screen-on` first, then `busctl --user call org.casdap.Player /org/casdap/Player
org.casdap.Player ShowPowerMenu`; a release without a press is dropped. A changed `Profile` value runs
`casdap-power cpu-apply`. `CASDAP_POWERD_DRY_RUN=1` logs `would:` lines. In the Steam session the daemon is
not running and Steam owns the key.

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
`no player, switching back` and starts `casdap-switch-back.service`, any other failure is logged. With the
screen off (`$CASDAP_RUNTIME_DIR/off`, default `$XDG_RUNTIME_DIR/casdap-power/off`) a hold in casdap runs
`casdap-power screen-on` instead and the next hold opens the prompt. Tap in
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

Codec offer: `Settings.btCodec` (`auto`, `aptx`, `aac`, `ldac`, `sbc`, default `auto`) is applied by
`CodecFragment` (context property, DAP mode only, null under `--overlay`) at start and on every change.
It writes `~/.config/wireplumber/wireplumber.conf.d/50-casdap-codec.conf` (`CASDAP_WP_CONF_DIR`
overrides the directory) with `monitor.bluez.properties = { bluez5.codecs = [ ... ] }`: `sbc aptx aptx_hd`
for `aptx`, `sbc aac` for `aac`, `sbc ldac` for `ldac`, `sbc` for `sbc`; `auto` removes the file. The
write is a temp file then rename. A file change runs `systemctl --user restart wireplumber`
(`CASDAP_SYSTEMCTL_BIN`) through a tracked `QProcess`, unless `BluezModel.transportActive`: then the
file waits for the next wireplumber start, `pending` reads true and the deferral is logged. An
unchanged file runs nothing. `restarting` is true while the restart runs, `restartFinished(ok)` ends it;
a failed restart leaves `pending` true. The phone picks among the offered codecs when it connects.

## Audio

`pipewire.conf.d/50-casdap-rates.conf` sets `default.clock.allowed-rates` to 44100, 48000, 88200, 96000,
176400 and 192000 for the whole daemon while casdap is merged; `default.clock.rate` stays 48000. With only
one stream active, the graph and the PCM switch to that stream's rate when the sink starts. A concurrent
48 kHz stream keeps the graph at 48000 and the other stream is resampled. PipeWire reads the fragment at
start.

`AudioInfo.graphRate` is the running graph rate: `pw-dump` (`CASDAP_PW_DUMP_BIN`, default `pw-dump` on PATH)
gives the default sink node's `clock.rate`, else its `Format` rate, else the `settings` metadata `clock.rate`;
without the binary it is the `hw_params` rate. Read with `pcmFormat` every 2 s while `active`.

`OutputControl` (context property, DAP mode only, null under `--overlay`) applies `Settings.output`
(`auto`, `speaker`, `headphones`) at start and on every change: `speaker` and `headphones` run
`wpctl set-default <id>` with the id read from `wpctl status --name` for
`alsa_output.platform-sound.HiFi__Speaker__sink` or `HiFi__Headphones__sink`; `auto` runs
`pw-metadata -d 0 default.configured.audio.sink`, so WirePlumber picks the sink by route priority again.
`jackPresent` reads the `Headphone Jack` block of `amixer -c0 contents` (`values=on`; the CARD control has no cget name), polled every 2 s while `active`
and after every apply. `busy` is true while a command chain runs, `applied(output, ok)` ends it.
`CASDAP_WPCTL_BIN`, `CASDAP_PW_METADATA_BIN` and `CASDAP_AMIXER_BIN` replace the binaries.

aptX: `build/aptx.sh <cache> <dist>` downloads `libfreeaptx` (RPM Fusion free releases/44) and
`pipewire-codec-aptx-1.6.7-1.fc44` (updates/44), both aarch64 and sha256 pinned, extracts
`usr/lib64/libfreeaptx.so.0*` and `usr/lib64/spa-0.2/bluez5/libspa-codec-bluez5-aptx.so` into the dist and
puts the LGPL text under `usr/share/licenses/casdap/`. The sysext merge places the codec next to the image's
bluez5 codecs; the user audio stack has to restart once after the merge.

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
