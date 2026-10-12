# CASDAP

Console As DAP. A music player session and Bluetooth A2DP receiver for Linux gaming handhelds,
shipped as a systemd-sysext overlay for [Armada](https://github.com/armada-os/armada).

Status: prototype, runs on an AYANEO Pocket MICRO 2. The session, the switch units, the guide button
mapping, the hold daemon, the pad target, the boot guard, the Bluetooth receiver with codec choice and
aptX, the in game overlay, the power key daemon with the screen off state and the CPU floor, USB DAC
output with BIT PERFECT and EXCLUSIVE levels for USB DACs and the player (home, hub, artist and album detail, now
playing, queue, playlists, history, jump grid, settings, saver, power menu) are written. The session runs native portrait with runtime orientation.
Open: the power measurement run and the release image.

## Layout

| Path | Content |
|---|---|
| `sysext/` | Static overlay, copied into `dist/` as is. `/usr` only, nothing under `/etc`. |
| `patches/<class>/<path>.patch` | Patches against files of the Armada image. `pm2/` Pocket MICRO 2 capability map. |
| `build/` | `base.env` (pinned image), `Containerfile`, `toolchain.sh`, `build.sh`, device scripts for the dev loop. |
| `player/` | Qt 6 QML player with a C++ core (`casdapcore`). CMake project, built by `build.sh` inside the builder, installed to `/usr/bin/casdap-player`, linked against libpulse and libasound. `casdap-fiio` (plain C++ over the `casdapfiio` library, no Qt) is installed to `/usr/libexec/casdap/casdap-fiio` and run by `casdap-fiio-reset.service` and the two session drop ins (USB DAC below): `reset [--no-wait] [--deadline <ms>] [--stamp] [--skip-stamped]` forces preset 240 on every FiiO DAC found by VID:PID under `/sys/bus/usb/devices` that reads another preset (after the casdap session check, `systemctl --user show` killed at 500 ms, skipping unless the session reads `deactivating`, `inactive` or `failed` or no user manager runs; the interface 0 driver wait, 5000 ms, none with `--no-wait`, skipping a DAC whose interface is not there yet (`interface not ready`) or still bound to `usbhid` (`hid bound`); the lock; the session check again), `dump` prints firmware, preset, the slot 162 name, preamp and bands as `key=value`, `restore-slot [<file>]` writes the newest slot 162 backup back and refuses unless the casdap session reads `deactivating`, `inactive` or `failed` (or no user manager runs) and while more than one supported DAC is present; `--deadline` bounds every wait and read and exits 0 at expiry, `--stamp` writes `casdap-fiio.reset` only when every DAC was done or none was found, `--skip-stamped` exits when that stamp is under 10 s old. Every verb takes `casdap-fiio.lock`; the lock, the in flight stamps and the reset stamp live in `/run/user/<uid>` (`CASDAP_RUN_DIR`), and without that directory no lock is taken and no stamp written. Exit 0 when nothing was to do, done, skipped or at the deadline, 1 on a failed transaction or a refusal. Playback is `playbin`, audio only, sink from `CASDAP_AUDIO_SINK` (default `pulsesink`), gapless through `about-to-finish`. Index, position, shuffle, repeat, `status` (`playing`, `paused`, `stopped`), `savedAt` (unix seconds), `handoff`, `stack` (the page stack as `name` and `id` pairs from each page's `objectName` and `pageId`, sheets, dialogs, the jump grid and the saver left out) and `queueRev` persist in `~/.config/casdap/state.json`, written every 10 s while playing with `handoff` false, on quit and on SIGTERM with `handoff` true, on every overlay hide and on `PrepareForSleep(true)` with `handoff` false, and by the switch prompt before the switch starts (KEEP IN BACKGROUND `handoff` true, CLOSE PLAYER `handoff` false; the session switch ends the player with SIGKILL, so nothing is written at exit). The queue lives in `queue.json` next to it: `rev` (counts every queue and order change), `index`, `ids` and `paths` (parallel arrays) and `order` (the shuffle play order as queue rows, empty with shuffle off), written 1000 ms after the last change on a worker thread through `QSaveFile` (one write in flight, a newer request replaces the waiting one); every `state.json` write except the 10 s one writes a `queue.json` that is behind on the calling thread first. A start resumes playing when `handoff` is true, `status` is `playing` and `savedAt` is under 60 s old, every other file restores the queue paused (with `outputLevel` other than `off` it restores parked at the saved position with no pipeline and a resume waits as described under Audio); entries resolve by id through `Library.queueEntries`, an id the library lacks or whose path differs by path, and an entry found by neither keeps its path with the file base name as title; a `queueRev` that differs from the `queue.json` `rev` (or no `state.json`) restores at the `queue.json` index, 0:00, paused; a saved `order` that is a permutation of the restored rows is kept, any other is rebuilt with the current row first; a `state.json` that still holds a `queue` array restores from it, then both files are written once; a resume waits for the first `SystemBluezModel` object read (`BluezModel.ready`) and restores paused while a phone transport is active; both modes restore the saved `stack` at start (an id gone from the library truncates it at that page, no key gives the home). Every play start adds a `history` row and emits `Library.played(id)`; every 100th insert of the process keeps the newest 2000 rows. `HistoryModel` takes `limit` (0 unbounded): a Recent model moves the played row to row 0 or inserts it there and drops the row past `limit`, and reloads on `tracksChanged`; a New model reloads only on `albumsChanged`. The library (`library.db`, `PRAGMA user_version` 5) reads `ARTISTS` or `ARTIST` tag values as a list, splits a single value on `; `, ` / ` and `, ` unless the whole value equals the album artist (not on ` feat. `), stores each per track in `track_artists(track_id, artist_id, pos)`, keeps `tracks.artist` as the joined display string and `albums.artist` as the album artist; `ArtistsModel` lists individual artists with album and song counts, `Library.artistAlbums(id)` and `artistTracks(id)` resolve through `track_artists`, and a version 1 database gets the table and every track marked for reread on the next scan. Scans run in DAP mode only and always cover the whole `Settings.roots` list: at start on the first `frameSwapped` after the state and stack restore, on every `Settings.rootsChanged`, from `Library.rescanRoots()`, and 5000 ms after the last `directoryChanged` of `LibraryWatcher` (a `QFileSystemWatcher` on every directory the last scan walked, breadth first in roots order, plus the nearest existing ancestor of each missing root, the climb stopping at the media base and `$HOME`, at most 4096 directories, `casdap: watch limit 4096 reached` logged once per process), and when the 5000 ms poll of a missing root finds it a directory; a trigger during a scan becomes the one pending request. Overlay mode never scans and builds no watcher. A track whose file is gone is removed under an enabled root that exists, kept under an enabled root that is missing or unreadable, kept when no directory between `/run/media` (`CASDAP_MEDIA_ROOT` replaces it) and the file is an existing mount point, and removed when no enabled root holds it (a folder turned OFF). `.m3u` and `.m3u8` files under the roots are read in the same scan: `.m3u8` as UTF-8, `.m3u` as UTF-8 when the whole file decodes, else Latin-1, a BOM dropped, empty and `#` lines skipped, `file://` decoded, `\` read as `/`, relative entries against the playlist folder; an entry takes the exact `tracks.path` match, else the single case folded match, else it is skipped, counted in `playlists.skipped` and logged as `casdap: playlist <name>: <n> entries not in the library`. `playlists` carries `source` (the file path, unique index), `skipped`, `mtime` and `size`; a playlist whose mtime and size match is not read unless the scan added tracks, a read one upserts by `source`, so a playlist keeps its id and pin, rewrites its `playlist_items` in file order (an entry listed twice plays twice) only when the resolved list differs, an unreadable file leaves its row as it is (`casdap: playlist <path>: unreadable, kept as is`), and a row whose file is gone is dropped by the track rules. A scan with nothing added, updated, removed or read runs no transaction and emits only `scanned`; `tracksChanged`, `albumsChanged` and `countsChanged` follow track changes, `playlistsChanged` and `playlistCountChanged` playlist changes, and the album, artist and art prune runs only after track changes. A version 2 or 3 database gains both columns and drops its playlists without `source`; every older version gains `mtime`, `size` and the `playlist_items(track_id)` index. `PlaylistsModel` sorts by name case folded, then `source`. `Library.songCount`, `allTracks()` (every song id in the `Songs` order) and `genreTracks(id)` (that order filtered to the genre) return song ids for the play verbs; `queueEntries` and `trackRows` query 500 ids per statement and keep the given order, `artistAlbumRows(id)` and `artistTrackRows(id)` return the artist page rows in one query each. Pins live in `pins(id, kind, ref_id, pos)`: `Library.pin(kind, refId)` (kinds `album`, `artist`, `song`, `playlist`; appends at max pos + 1, false on a duplicate or an unknown kind), `unpin(kind, refId)`, `isPinned(kind, refId)`, signal `pinsChanged`; a scan deletes every pin whose ref no longer exists and emits `pinsChanged` when it removed one. `PinsModel` (context property `Pins`) lists the pins in pos order with roles `id` (the ref id), `kind`, `title`, `subtitle`, `artPath`, `section` (`pins`): album title, album artist and art; artist name, `N ALBUMS` and the first album art; song title, artist and its album art; playlist name, `N SONGS`, no art; a pin whose ref is missing is skipped. `Mpris` registers `org.mpris.MediaPlayer2.casdap` at `/org/mpris/MediaPlayer2` on the session bus (root and `Player` interfaces, XML in `player/dbus/`, installed to `share/dbus-1/interfaces`): `Identity` is `CASDAP`, no `DesktopEntry`, track ids are `/org/casdap/track/<id>`, `Volume` reads 1.0 and ignores writes, `OpenUri` plays one `file://` uri, `Seeked` and `PropertiesChanged` are emitted for status, metadata, shuffle and loop. `PowerBridge` registers `org.casdap.Player` at `/org/casdap/Player` (`SetScreenOff(b)`, `ShowPowerMenu()`, `ShowOverlay()`, `HideOverlay()`, `ToggleOverlay()`, `ShowSwitchPrompt()`, `ScreenOff`, `PlaybackStatus`, `OverlayShown`, signal `ScreenOffChanged`; the three overlay methods answer `org.casdap.Error.NotSupported` and `OverlayShown` reads false in DAP mode, `ShowSwitchPrompt` answers `NotSupported` in overlay mode and emits `showSwitchPrompt` to QML in DAP mode), listens to `org.casdap.Hotkeys.VolumeChanged(d)`, takes the `VolumeKeys` level through `setVolume(int)` (both emit `volumeChanged(int)`), reads `/run/casdap/drift` and runs only the verbs `screen-off`, `game-mode`, `suspend`, `poweroff` (`/usr/libexec/casdap/casdap-power <verb>`, `CASDAP_POWER_BIN` overrides the path; `systemctl start casdap-switch-back.service`, which with an `outputLevel` set runs as the session switch after the output release); settings live in `~/.config/casdap/settings.ini` (`output` `auto`, `speaker`, `headphones`, `usb`; `btCodec`; `outputLevel` `off`, `bitperfect`, `exclusive`, default `off`; `sampleRate` `auto`, 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000, default `auto` (an unknown value reads the default); `hpGain` `low`, `normal`, `high`, default `normal`; `hpAmp` `lohifi`, `ulp`, `hifi`, default `lohifi`; `spkGain` 84, 90, 96, 99, 102, 105, 108, default 102; `volumeStep` 2, 5, 10, default 5; `rgb` `accent`, `off`, `armada`, default `armada`; `orientation` `portrait`, `portrait-flipped`, `landscape`, `landscape-flipped`, default `portrait`, any other value read as `portrait`; `autoRotate` (default off); `touch` (default on); `overlayToasts`; `eqHeadphones`, `eqUsb`, `eqSpeaker` (`off`, a profile path, `custom`, default `off`); `roots`, `accentHue` (a multiple of 10 from 0 to 350, default 350; an old `accent` hex migrates to its nearest hue and is dropped by the next save), `accentFromArt` (default off), `scale`, `saverSeconds`, `screenOffSeconds`, `suspendMinutes`, `wifiOffScreenOff`, written 500 ms after the last change and on quit), battery comes from UPower `DisplayDevice` with a `/sys/class/power_supply` fallback, and local playback pauses when the Bluetooth transport goes active. Every local start while the transport is active (`play`, `playTracks`, `playAt`, `playPause` and `resume` from paused or stopped, MPRIS `Play`, `PlayPause` and `OpenUri`) emits `localPlayRequested` (wired to `BluezModel.pause`, AVRCP `MediaPlayer1.Pause`), holds the start paused with `playPending` true and starts once the transport leaves `active` or 1000 ms later; `pause` and `stop` drop the pending start. `CASDAP_BLUEZ_FAKE=1` wires `FakeBluezModel` in place of the Bluetooth model. Removing the current queue row starts the next one or stops when none follows. `Player.enqueueTracks(ids)` appends the rows and `playNextTracks(ids)` inserts them after the current row, both dropping ids the library does not know, returning the count added and emitting `added(count)` when it is above 0; on an empty or stopped queue either sets the queue to the added rows (shuffle kept, a new play order) and lands paused at 0:00 on the first one. `Player.playSource(kind, id, mode, startId)` (kinds `album`, `artist`, `playlist`, `genre`, `all` through `Library.entriesFor`, `song` the one track; modes `play` from the `startId` row or the first, with shuffle already on from a random row with no `startId` and from the `startId` row first with one, the rest in a new play order, `shuffle` with shuffle on from a random row, `add` and `next` with the enqueue and play next rules) returns the rows queued. With shuffle on, turning it on puts the current row in slot 0, a new queue from a play verb or `playTracks(ids, start)` puts its start row in slot 0 and the rest in a random order, an appended row takes a random slot after the current one and a play next row the slot right after it; off returns to list order on the same row. `Queue` reads in play order through `orderedRows()`, `rowAt(slot)`, `slotOf(row)`, `currentSlot` and `upcoming(n)` (the next n rows, wrapping with repeat all); `moveSlot(from, to)` and `playNextRow(row)` edit the play order under shuffle and the list without it, `remove(row)` drops the row from both. An index move emits `dataChanged` on the old and new row, `indexChanged` and `currentSlotChanged`; `set` emits one `modelReset`; `orderChanged` follows a shuffle toggle, an enqueue or play next under shuffle, `moveSlot`, `playNextRow` and `setOrder`. `Queue.playOrder` is a list model in play order (roles `row`, `slot`, `number`, `id`, `title`, `artist`, `album`, `durationMs`, `isCurrent`, `get(slot)`, `count`) with one insert, remove or move per queue edit (an enqueue of more than 64 rows under shuffle, the shuffle toggle and `set` reset it). The gapless chain shares the queue's entry list and resyncs on resets, inserts, removes, moves and order, shuffle and repeat changes. A track error starts the next row in the state the player was in; errors on every row of one pass stop the player on the first failed row and log `casdap: no playable track`. Pad input is SDL3 gamepad events (Guide never mapped), polled every 16 ms while a pad is open and the screen is on, every 1 s while no pad is open, not at all with the screen off; d-pad and stick repeat at 300 ms then 80 ms, `CASDAP_PAD_LOG=1` logs each action. `Pad` and `DbusPad` derive from `PadSource` (the `Action` enum, `screenOff`, `connected`, `actionName`, `action`, `pressed`, `released`); `main.cpp` hands `Pad` to QML as `Pad`, `DbusPad` under `--overlay`. `GuideChord` (context property `Guide`), `OutputWatch` (context property `Outputs`), `VolumeKeys`, `OutputMode` (context property `OutputMode`), `DacEq` (context property `DacEq`; `CASDAP_SYS_ROOT` replaces `/sys`, `CASDAP_RUN_DIR` the run dir) with its `FiioWorker` thread, `DeviceReservation`, `AlsaVolume` and `SensorRotation` or `SscRotation` (context property `Sensor`) exist in DAP mode only, not under `--overlay` or `--screenshot`; `CASDAP_EXCLUSIVE_SINK` replaces the EXCLUSIVE sink (`%1` the card id) and `CASDAP_RESERVE_BUS` the bus address of the reservation (unset: the session bus). `Guide.take()` reads `down` in the guide directory (`CASDAP_GUIDE_DIR`, else `$XDG_RUNTIME_DIR/casdap-guide`, else `/run/user/<uid>/casdap-guide`); a value above 0, not in the future and at most 500 ms old is written to `chord` through `QSaveFile` and `take()` returns the commit result, anything else returns false. `Outputs.internal` is true while no connector under `CASDAP_DRM_ROOT` (default `/sys/class/drm`) other than `DSI-*`, `eDP-*`, `LVDS-*` or `Writeback-*` reads `enabled` in its `card*-*/enabled` file (`modes` is never read); it is read at start, by `refresh()` (synchronous), after every `QScreen` geometry change or screen added or removed at once, 250 ms and 1000 ms later, and every 2 s while the screen is on (`setScreenOff` follows `PowerBridge.screenOff`). `Sensor` is `SscRotation` when `/dev/fastrpc-sdsp` exists, else `SensorRotation`; `CASDAP_SENSOR_BACKEND` `ssc` or `proxy` forces one. `SensorRotation` talks to iio-sensor-proxy (`net.hadess.SensorProxy` at `/net/hadess/SensorProxy` on the system bus; `CASDAP_SENSOR_BUS` takes `session` or an address): `supported` while the name has an owner and `HasAccelerometer` is true (read at start, when the name appears and on `PropertiesChanged`); `setWanted(true)` sends `ClaimAccelerometer` while supported, `setWanted(false, keepTurn)` sends `ReleaseAccelerometer` and forgets the last settled turn unless `keepTurn`; after each claim reply it reads `HasAccelerometer` and `AccelerometerOrientation` and then follows `PropertiesChanged` (nothing is read before the reply); `raw` is the last value, `settledRaw` the last settled one (kept across a release that keeps the turn), `turn` the last settled turn (`normal` 0, `bottom-up` 2, `left-up` S, `right-up` S + 2, S from `CASDAP_SENSOR_TURN`, 1 or 3, default 1), set after a value holds 500 ms; `undefined` keeps the turn; `turn` is -1 while nothing is claimed and after a first `undefined` with nothing settled; a claim restores the last settled turn at once. When the name vanishes while wanted and supported or claimed, `supported` and `turn` hold; the name returning or a new owner claims again, and with no owner after 1000 ms (`CASDAP_SENSOR_RECLAIM_MS`) `supported` turns false, `turn` -1 and `settledRaw` empty. `SscRotation` talks to the Qualcomm SSC over QRTR (an `AF_QIPCRTR` datagram socket, service 400) and reads `device_orient` on change: it looks the service up at start and asks for the `device_orient` SUID when the service appears; `supported` is true once the SUID arrives, an answer without one retries after 3000 ms (`CASDAP_SSC_RETRY_MS`), doubling to 60 s, and a socket that cannot open leaves it false. `setWanted(true)` sends the on change enable (514) and only the response to that request with result 0 claims; the claim restores the last settled turn at once and sends one flush for the current value. `setWanted(false)` sends the disable (10) whenever an enable was sent. Values (event 776) are taken only while claimed: 0 is 0, 2 is 2, 1 is S, 3 is S + 2, any other value is dropped; `raw` and `settledRaw` are the number, the 500 ms settle and `keepTurn` work as above, and no event (the device flat) keeps the turn. When the service goes (`DEL_SERVER`, or a receive error, which reopens the socket and looks the service up again after 3000 ms) while wanted and supported, `supported` and `turn` hold for 60 s (`CASDAP_SENSOR_RECLAIM_MS`) and the service returning enables again; past that, or while not wanted, `supported` turns false, `turn` -1 and `settledRaw` empty. It holds a logind delay lock (`release sensor`): `PrepareForSleep(true)` sends the disable and releases the lock on its response or after 500 ms, `PrepareForSleep(false)` takes the lock again and enables when wanted. Journal lines: `casdap sensor: ssc service <node> <port>`, `ssc device_orient <suid>`, `ssc enabled`, `ssc released`, `ssc value <n>`. `VolumeKeys` handles the volume keys with the screen on or off, under the switch prompt and the power menu. `EvdevKeys` opens `gpio-keys` and `pm8941_resin` by name (`CASDAP_VOLUME_NAMES=a:b` overrides) under `CASDAP_DEV_INPUT` (default `/dev/input`) and `CASDAP_SYS_INPUT` (default `/sys/class/input`), read only, no grab; a node that is missing or does not open is retried every 2 s and logged once, a read error or end of file closes it and retries. `KEY_VOLUMEUP` and `KEY_VOLUMEDOWN` value 1 is one step; a key held 400 ms steps every 100 ms until its release, value 2 lines are ignored. A step moves every channel of the default sink by `Settings.volumeStep` (read at each press) percent of `PA_VOLUME_NORM`, clamped to 0 and `PA_VOLUME_NORM`; up also unmutes. `PulseSinkVolume` runs libpulse on `pa_glib_mainloop` over Qt's GLib event loop (without it `casdap volume: no glib event loop` and the keys stay off), connects with `PA_CONTEXT_NOAUTOSPAWN` and reconnects 1000 ms after a failure or a missing server. It keeps the default sink name and volume as the authority: a server change, a connect or a failed set rereads the server info and the sink, presses during that read add up (at most 20 steps) and apply once it lands, and a sink change event 300 ms or more after the last set (a phone's absolute volume) is read into the cache. Each applied press sets `PowerBridge.volume` to the max channel in percent for the toast. While an output level is effective `RoutedSinkVolume` sends the presses to the DAC's `PCM` through `AlsaVolume` instead (Audio), one toast per press, and Pulse level changes are not shown. Journal lines: `casdap volume: <level>` per press, `casdap volume: connected`, `casdap volume: pulse unavailable, retrying <error>` once per outage, `casdap volume: <name> on <path>`, `<name> not found, retrying`, `<name> closed`. `DbusPad` reads `org.shadowblip.Input.DBusDevice.InputEvent(s,d)` from InputPlumber on the system bus (`CASDAP_INPUTPLUMBER_BUS` overrides the address): `ui_up/down/left/right`, `ui_accept` A, `ui_back` B, `ui_context` X, `ui_action` Y, `ui_l1` LB, `ui_r1` RB, `ui_l2` LT, `ui_r2` RT, `ui_option` Start, `ui_select` Back, value >= 0.5 press, below release, same 300/80 repeat; `ui_guide`, `ui_quick`, `ui_l3` and unknown names are logged once and dropped; `connected` while a `devices/target/dbus*` path exists. Its `Intercept` writes `InterceptMode` on every `org.shadowblip.Input.CompositeDevice` from `GetManagedObjects` on `/org/shadowblip/InputPlumber`: 3 on `setShown(true)` and on a device added while shown, 0 on `setShown(false)` and in the destructor. |
| `tests/` | bash tests with stubbed `sudo`, `systemctl`, `loginctl`, `btmgmt`, `evtest`, `busctl`, `amixer`, `armada-rgb`. `bt-test.sh` runs `bt-test.py` under `dbus-run-session` with a fake `org.bluez` (needs `dbus-python` and `python3-gobject`). `keep-test.sh` drives `casdap-keep` through `CASDAP_KEEP_FILE` and `CASDAP_BOOT_ID_FILE`; `power-test.sh` drives `casdap-power` with stubbed `busctl`, `systemctl`, `systemd-run`, `nmcli` and `CASDAP_RUNTIME_DIR`, `CASDAP_SETTINGS_FILE`, `CASDAP_CPU_BEFORE_FILE`; `powerd-test.sh` replays `evtest` lines and profile reads into `casdap-powerd`; `wait-x-test.sh` drives `casdap-wait-x` with a stubbed `xprop` and `CASDAP_X_SOCKET_DIR`; `overlay-unit-test.sh` greps `casdap-overlay.service` and its drop in under `sysext/` and runs `systemd-analyze --user verify` where the image units and the player binary exist (`skip:` otherwise); `fiio-unit-test.sh` checks the exact lines of `casdap-fiio-reset.service`, `05-casdap-fiio.conf` and `40-casdap-fiio.conf` and that `05-casdap-fiio.conf` sorts first in the casdap session drop ins. Player tests live in `player/tests/` (QTest, offscreen, generated FLAC and MP3 fixtures; `tst_mpris`, `tst_power`, `tst_systembluez`, `tst_dbuspad`, `tst_overlay`, `tst_sensor` and `tst_ssc` run under `dbus-run-session`, `tst_dbuspad` and `tst_overlay` against `fake_inputplumber.py`, `tst_sensor` against `fake_sensorproxy.py`, `tst_overlay`, `tst_power` and `tst_ssc` against `fake_login1.py` (a fake `org.freedesktop.login1` with `Inhibit` and `PrepareForSleep` on the private bus, `CASDAP_LOGIN1_BUS`), `tst_overlay` also against a unix datagram `NOTIFY_SOCKET`; `tst_ssc` drives `SscRotation` through a `FakeLink` that records sent datagrams and injects received ones, so no test opens an `AF_QIPCRTR` socket; each `player/tests/qml/tst_*.qml` is its own ctest entry `qml-<name>` running `tst_qml -input <file>`). |
| `dist/` | Build output, the sysext tree. Not committed. |

No Armada file is checked in whole or shadowed. A patch's `--- a/` header names the base file in the pinned image,
its `+++ b/` header the output path, which is the patch's own path under `patches/<class>/`. `build.sh` copies the
base to the output path, records the base's sha256 in `usr/share/casdap/shadow-base.sha256`, applies the patch and
fails on any reject. The one patch builds `zz-casdap-ayaneo_pocket_micro2.yaml` from Armada's
`ayaneo_pocket_micro2.yaml`.

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
`Ui.setSurface(w, h)` takes the window size; `Ui.turn` is the content turn in quarter turns clockwise
and `windowW`/`windowH` are read only, the surface swapped at an odd turn, so every token follows a
rotation. `Main.qml` draws everything but the toast window inside `rotor` (`Ui.windowW` x `Ui.windowH`,
centered at a rounded offset, `rotation: Ui.turn * 90` plus the sweep `spin`). `syncSurface()` runs one `Qt.callLater` after a
resize, a change of `Settings.orientation`, `Outputs.internal`, the taken sensor turn or the pin, and directly in
`Component.onCompleted`.
Orientation applies (`Ui.orientationActive`) in DAP mode while the window is taller than wide and
`Outputs.internal` is true (a null `Outputs` counts as internal); the turn is then 0 for `portrait`, 2 for
`portrait-flipped`, `Env.landscapeTurn` (L, 1, `CASDAP_LANDSCAPE_TURN=3` gives 3) for `landscape` and L + 2 for
`landscape-flipped`, else 0. Auto rotate: with `Settings.autoRotate` on and `Sensor.supported`, the requested turn is
the pin, else the last taken `Sensor.turn` (0 or above), else the saved orientation; the sensor never writes
`Settings.orientation`. Main calls `Sensor.setWanted(autoRotate && orientationActive && !off, autoRotate)`, so screen off
releases the claim and keeps the last turn for the wake, and auto rotate off forgets it. A `Sensor.turn` change is taken unless a settings edit is open,
then it is taken when the edit closes; one whose `Sensor.settledRaw` differs from the pin's clears the pin. Auto rotate
off or unsupported drops the taken turn and the pin. A change of the turn or of `orientationActive` runs T18 once: an ORIENTATION edit
or AUTO ROTATE edit reverts and any other settings edit confirms, the page turn and every page's `completeAll()` (through
`PageStack.eachPage`), the overlay layer, the switch prompt, the power menu and the saver land, the turn
changes, then every page's `keepFocusVisible()` brings its focused row inside; Main emits
`orientationApplied(turn)`. Rotate sweep (design-11, T18a): a turn change with orientation applying before and after,
the window at the current surface size, the window visible, the screen on and `rotateMotion` (`Env.rotateMotion`, false
only with `CASDAP_ROTATE_MOTION=0`, which every ctest sets) lands the motion at once, then the rotor sweeps from the old
angle by the shortest quarter turn delta (a half turn sweeps clockwise) and fades out (250 ms, `expIn1` rotation, `sineOut`
fade), the T18 switch runs at opacity 0 (`orientationApplied` 250 ms after the request), and the new layout sweeps the
same way from the old angle into place and fades in (250 ms, `expOut1`, `sineOut`). The rotor is a smooth layer only
while `sweeping`. `PadRouter.locked` includes `sweeping`, so pad presses and touch are dropped and a drag is canceled;
the Guide chord still takes the Guide and rotates, and a turn requested during the sweep starts a new sweep after it.
Screen off, an overlay hide, a change of `orientationActive` or of the window size land the sweep and run T18 at once.
`Ui.userScale` is a `Binding` to `Settings.scale` clamped to `[sMin, sMax]`, so a
saved 1.1 shows 1.0 in landscape and 1.1 again in portrait. The first `frameSwapped` logs
`casdap: first frame turn <n>`. The wake from screen off calls `Outputs.refresh()`. `Motion` (singleton) holds the curves (`expOut6`, `expIn6`, `expIn1`, `expOut1`, `expIn15`, `sineOut` as
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
Every page with a row list (hub panes, Settings, EQ, Playlists, History, Queue, ArtistDetail, AlbumDetail) feathers
its visible rows once on first show, after `entered` (T2a): rows `x` +34 u to 0, opacity 0 to 1, 380 strip,
stagger `min(i, 10) * 22` from the first visible row; the rows wait at `x` +34 u and opacity 0 while `arriving`
(`animated`, screen on and `PageStack.animate`), and `animated` false, screen off or a stack restore lands them.

`Pad` is a context property; `PadRouter` turns its actions into names (`up down left right a b x y
lb rb lt rt start back`) and hands each to the top page of the `PageStack` through
`handleAction(name, repeat)`, dropping it while a transition runs. Unhandled `start` emits
`nowPlaying()`, unhandled `back` emits `view()`; with the screen off every action is dropped. Keyboard fallback
on the same router: arrows, Return A, Escape or Backspace B, X, Y, Q and E for LB and RB, PageUp and
PageDown for LT and RT, Space Start, Tab Back/View; key autorepeat counts only for directions. Pad directions
and face buttons remap on press with `Ui.padButton` (up, right, down, left and Y, B, A, X clockwise, turned by
`Ui.padTurn`, `(L - turn) mod 4` while orientation applies, else 0), so A is the button at the screen's south, B east,
X west and Y north; LB, RB, LT, RT, Start and Back keep their meaning. The press keeps its mapped name for its
repeats and release; two held buttons that map to one name press and release it once. Rotate chord: while orientation applies and the screen is on, a raw direction press first asks
`Guide.take()` (true when the Guide press is at most 500 ms old); on true the router emits `rotate(raw)` instead of
dispatching, drops that direction's repeats and release, and drops every other direction pressed while it is held,
so one press rotates once. Main's `rotateBy(raw)` is absolute: the raw direction pointing to the sky as the device
is held picks the orientation with that edge up, turn `(L + d) mod 4` for d up 0, right 1, down 2, left 3 (for L 1 up
LANDSCAPE, right PORTRAIT FLIPPED, down LANDSCAPE FLIPPED, left PORTRAIT); a chord for the requested turn changes
nothing. Otherwise an open settings edit on ORIENTATION or AUTO ROTATE reverts and any other confirms, then
`Settings.orientation` takes the target and T18 runs (with auto rotate on and supported the chord pins the target
turn instead and remembers `Sensor.settledRaw`; the pin is not saved); the saver, sheets and dialogs stay open and the chord counts as
input for the saver and screen off timers. `Guide` null, the screen off, the overlay or an external output give
no chord. Keys are not
remapped.
`Page` is the screen base: one `focusIndex`, A emits `activated(index)`, B pops, up/down and LT/RT go
to its `FocusList` when set. `FocusList` keeps the focused row inside `[CONTENT_TOP, H - ROW_HEIGHT]`
with a 100 ms easeIn `contentY` tween per step; LT/RT move one page of whole rows in 500 ms easeIn.
It reuses delegates (`reuseItems`, `cacheBuffer` one list height); every row delegate stops its entrance and
lands at `enter` 1 in `ListView.onPooled`.
`FocusBar` is the 2 u white bar at `HUB_EDGE - 8` spanning the row, following in 100 ms sine.
`PressScale` scales its item to 0.96 while A is held, 100 ms linear each way, at once while `animated` is false.

`TouchLayer`, the last child of `rotor`, takes every touch with one `PointHandler`, enabled while `Main.touchOn`
(DAP mode, not `--screenshot`, `Settings.touch` not false) and the screen is on. A target declares `touchKind`
and `touchIndex` (grid cells also `touchCol`); kinds `row`, `divider`, `cell`, `letter`, `ring`, `title`, `menu`,
`peek`, `menu-peek`, `tile`, `sub`, `item`, `scrim`, `exit`, `pivot`, `back`, `header`, `block` and `page` (every
`Page` root). At press, `locked` or the screen off drop the gesture and a shown saver is hidden through
`dispatch("a")`, nothing more. The walk starts at the visible dialog of `dialogs` (`shown` or animating), else the
top page, with the scene point mapped into each item by `mapFromItem(null, p)`: an item takes part while visible,
enabled and above opacity 0, children are tried top first (`z`, then reverse declaration order), a `clip` item
only when it contains the point, a `ListView` only through `indexAt` and `itemAtIndex`; a tagged item without a
tagged child under the point is the hit, `block` drops the gesture. The scroller is the deepest containing item
with `touchScroll` true and `touchScrollEnabled` not false, tagged or not. The root lands its motion
(`completeAll()` for a page, `complete()` for a dialog) between two walks. The owner, the hit item or its nearest
ancestor with `touchPress(kind, index, x, hit)`, sets focus and returns the tap action (`a`, `b`, `left`, `right`,
`tap` or empty); `a` runs `press("a")` on the nearest ancestor with `press`. Moves map into the scroller's
`Flickable` (else the hit item); past `Ui.touchSlop` (25 u radius) the tap and the press end, a vertical move drags
the scroller 1:1 from that point and a horizontal one leaves the gesture dead. A release inside the slop runs in a
`Qt.callLater` after a snapshot check (same top page and depth, no new dialog, the hit item alive with its
`touchIndex`, no saver, lock or screen off): `owner.touchTap(kind, index, hit)`, then `PadRouter.dispatch` for
`a`, `b`, `left`, `right`; the press returns after `Motion.touchReturnDelay` (200 ms) or at the next press. A
drag release flings with the finger velocity between `t - Motion.flingWindow` (100 ms, interpolated, one older
sample kept) and the release, none under `Ui.flingMin` (50 u/s), capped at `Ui.flingCap` (2000 u/s). A routed pad
input, a dialog, the saver, a lock, the screen off, a page change, the layer disabled, the list's `count` or
`model` changing, the page's `rows` count changing, a `contentY` write it did not make and `cancel()` (the first
step of `applyOrientation`) cancel the gesture: the press returns, drag and fling stop, the rest does nothing; the
layer's own dispatch is exempt, and a routed pad input also stops a fling. One point at a time; `touched()` runs
Main's `touch()`. `TouchScroll` drives one `Flickable`: `begin(fingerY)` calls the flick's `stopScroll()`,
`dragTo(fingerY)` clamps to `[0, limit()]`, `release(v)` flings in a `FrameAnimation` decaying by
`Motion.flingRetention` (0.95) per `Motion.flingTick` (16 ms), integrated exactly per frame with the frame time
capped at `Motion.flingMaxStep` (33 ms), total travel `v0 / k` with `k = 62.5 * -ln 0.95`, until `Ui.flingStop`
(1 u/s) or an end; every write emits `moved()`. `FocusList` holds one as `touch` (limit `maxContentY`), its pad
scrolls stop a fling, `touchFocus(index)` focuses without scrolling and `clampFocusToView()` (on every `moved()`)
moves a focus that left the view to the first or last fully visible row its `focusable(index)` hook accepts (none accepted keeps it); both
emit `touchFocused(index)`. `Page` defaults: `row` focuses the row and returns `a`, `back` and `header` return
`b`, a row's `touchTap` brings it inside (F2), and `touchFocused` calls the page's `syncFocus()` when it has one,
else sets `focusIndex`. Main's `cursorBlank` `HoverHandler` on `rotor` sets `Qt.BlankCursor` in DAP mode.
Targets on the hubs, lists and details: the `CroppedHeader` word (`header`, the band right of it does nothing) and
`PivotStrip` labels (`pivot`, `touchIndex` the label); `MusicHub` rows (`row`, `divider`) and grid cells (`cell`,
`letter`, `touchIndex` the row, `touchCol` the cell) focus the row and the column and act as A, a pivot label
switches with T5 in `touchTap` when it is not the active one, and every `Pane` has `focusable` from
`focusableAt(pivot, index)`; `ArtistDetail` puts a slot sized `ring` Item inside each `Ring` delegate (ring plus half
the gap each side, as high as wide) and tags album and song rows; `AlbumDetail` track rows and `backRingClip`
(`back`); `QueueScreen`, `HistoryScreen` and `PlaylistsScreen` rows. Every list mirrors `touchFocused` into the page
(`syncFocus()` or `focusIndex`). `Sheet` owns its rows (`item`), panel (`block`) and scrim (`scrim`, B); `JumpGrid`
owns its letters with rows (`letter`) and EXIT (`exit`), its root is `block`; while either is not `shown` its
items, scrim, letters and EXIT carry no tag and the root reads `block`, so a tap during the close reaches nothing.
Home: menu items (`menu`, untagged while Quickplay is in front in portrait) call `showMenu()`, focus and act as A;
in portrait the Quickplay holder is `peek` (`left` while the menu is in front) and the scaled menu column is
`menu-peek` while Quickplay is in front (`b`, nothing where it overlaps the Quickplay panel). `Quickplay` owns its tiles and the phone tile (`tile`, the flat
index): in portrait while not `active` a tile returns `left`; otherwise it focuses the tile under `touchFocusing`
and, in landscape while not `active`, emits `touchFront()` (Home runs `showQuickplay()`) under the same guard;
`touchFocusing` keeps `onFocusIndexChanged` and `onActiveChanged` from scrolling, and `touchTap` brings the tile inside. Its `flick` is a scroller only in landscape or while
`active`, with a `TouchScroll` (default limit) and a clamp to the nearest fully visible tile in the focused
column, any column when none in it is fully visible. `NowPlaying` puts a `ring` Item inside each `Ring` delegate (the ring center spacing wide, as high as
wide but at most twice the distance from the ring center up to the title block bottom), a `titleBlock` (`title`) from `textX` to W - EDGE and from the title cap top to the last next track baseline slot,
and tags `backRingClip` (`back`); while `idle` every tap returns `a` with no focus move, so the wake is the only
effect. `Bluetooth` tags the transport row and a `nameBlock` (`row`, HUB_EDGE - 8 to W - HUB_EDGE, name cap top
to the state baseline slot) and the pairing ring (`ring`), all A, and its pivot labels.
`SettingsScreen` and `EqScreen` rows are `row` or `sub` (labels and info rows untagged); outside edit mode a tap
focuses the row and acts as A, in edit mode a tap on the edited row returns `left` or `right` by the half it lands
in, the header word `b` (revert) and any other tap `a` (confirm). Their scrollers (`flick`, each EQ `Pane`) take no
drag while `editing` and clamp the focus to a fully visible focusable row (Settings: `rowAtY` of the view edge,
then `nearestFocusable` inward; EQ: `contentY / pitch` rounded inward). `Dialog` (power menu, switch prompt) owns
its items (`item`) and scrim (`scrim`, B); the title is `block`; while not `shown` the root reads `block` and the
children carry no tag.

Components under `components/`: `Glyph` draws one `GlyphPaths.js` path as a `Shape` scaled from the
32 u box. `Ring` holds a Glyph at half the ring size, stroke 3 u idle and 4 u focused, white only: the
stroke and glyph read at 1.0 while lit and 0.6 otherwise, lit is `on` for a `toggle` ring (the now playing
shuffle, repeat and rate rings, the phone variant's shuffle and repeat) and `focused` for every other ring.
An accent arc from `Ui.ringArcStart` is drawn while `arcSweep` is set (the Bluetooth pairing countdown only).
The ring runs the F4 swap (scale 0.7 to 1 in 400 ms, opacity in 200 ms) on every
glyph change after the first one. `Tile` is the TILE_S art square with, while `captioned`, a 24 u caption
band under it: title and artist in the 9 u grid caption role, each as wide as the tile and clipped at its
right edge (no ellipsis), baselines at art bottom + 11 and + 22; the title goes to alpha 1 while focused,
the artist stays at 0.6. `ListRow` builds its `Ring` in a `Loader` only while `ring` is set (`ringItem` is null
otherwise). `CroppedHeader`, `PivotStrip`, `ListRow`, `Tile`, `LetterTile`,
`Ring` and `FocusBar` take `animated` (default true); cleared, every Behavior is off and a running
swap jumps to its end. `PageSurface` is `Ui.bg` under a vertical gradient from accent at alpha
`Ui.backdropStops[0][1]` (0.15) to accent at 0, the first child of `MusicHub`, `QueueScreen`, `Bluetooth`
and the other pushed list pages, so the turnstile swings a visible page. `ArtistDetail`, `AlbumDetail` and
`NowPlaying` put a plain opaque `Ui.bg` rectangle (`surface`) there instead and carry a `Backdrop` with no
source. `Backdrop` draws its `Ui.bg` fill, image and shade only with a source (`hasArt`) and holds its loops
while it has none, while `paused`, `screenOff` or not `visible`.
`Toast.show()` during the exit restarts the entrance from the strip's current position. The volume
overlay ring is `focused` inside the 0.4 group, so it reads at 0.4 (O20). `CroppedHeader` is 70 u
tall with the x-height bottom at 66 u in both orientations.

`OverlayLayer` sits above the `PageStack` in `Main.qml` (`power`, `bluez`, `player`, `screenOff`,
`topScreen`; every source may be null). `PowerBridge.volumeChanged(int)` and
`BluezModel.volumeChanged(int)` call `VolumeOverlay.show(level)`; the ring glyph and the bottom text
follow the phone while its transport is active, else the local track. A new `Player.current` id
shows the toast with artist and title unless the top page is now playing; the phone transport going
active or its title changing shows `from <phone>` and the title unless the top page is Bluetooth or now
playing. `Player.added(count)` from an `add to queue` or `play next` shows the toast with `ADDED N SONGS`
(`ADDED 1 SONG`) and the source name (album title, artist name, genre, playlist or song title) on every page; zero
rows show nothing. Pages carry `objectName` (`home`, `bluetooth`) for that check. The layer never takes focus;
pad input goes to the page below. With the screen off nothing is drawn and the level is kept.

Overlay mode (`casdap-player --overlay`, the game mode instance): `Main.qml` reads `OverlayBridge`
(`mapped`, `opacity`) into `overlay`, `overlayMapped` and `overlayFade`. The root is opaque `Ui.bg` as in
DAP mode, a full canvas `Ui.bg` rectangle is the first child of the content item, and the
content item follows the bridge opacity. `hidden` (`overlay && !overlayMapped`) feeds the screen off
path (`off` = `screenOff || hidden`) to `PadRouter`, `OverlayLayer` and the pages and completes running
transitions. The bridge owns window visibility in C++: the QML `visible` binding is false in overlay
mode, `show()` maps the window and `hide()` unmaps and destroys it after the fade. `visibility` is
`Window.AutomaticVisibility` in overlay mode,
`Window.FullScreen` in DAP mode. gamescope hands
the output size (960 x 640 on the Pocket MICRO 2), so the overlay runs the landscape layouts at u 2.00.
The page stack is kept across hide and show: `PageStack.serialize()` lists `name` and `id` per page
(`objectName`, `pageId`; `album` and `artist` carry their ids), Main writes it to `PlayerState.setStack`
on every depth change, and `restoreStack(list)` at start replays the saved stack without transitions
(`pushNamed` opens `music`, `bluetooth`, `playlists`, `history`, `settings`, `eq`, `artist`, `album`,
`nowplaying`, `queue`; an `artist` or `album` id the library no longer knows, a `nowplaying` with no
track and no phone transport, or an unknown name truncates the stack there). A B release on the home
menu in overlay mode emits `hideRequested` and Main calls `OverlayBridge.hide()` only when the matching B press
landed on the home menu with Quickplay and the pin sheet closed; a B that pops to home, pans Quickplay back or
closes the sheet does not hide. `Home.overlay`, `Bluetooth.overlay` and `SettingsScreen.overlay` carry the mode:
the settings POWER group ends with CLOSE PLAYER, a sheet `close player` whose accept calls
`OverlayBridge.closePlayer()`, PAIR A PHONE shows `PAIR IN DAP MODE` and A does nothing, the Bluetooth
PAIRING pivot shows `pair in dap mode` (11 u Light upper case at baseline CONTENT_TOP + 16) in place of
the ring and instructions and A does nothing. The saver and the screen off timer never arm in overlay
mode. Track toasts while hidden follow `Settings.overlayToasts` (row TOASTS IN GAME under SAVER,
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
POWER`. Y plays the whole library shuffled through `Player.playSource("all", 0, "shuffle", -1)` (an empty
library logs `casdap: shuffle all, empty library`); focus is the 2 u bar left of the item with the other items at 0.6. Left or Back/View pans Quickplay
forward (T6: the panel scales 0.30 to 1 and moves y 130 to 0 while the menu shrinks to a column on the
right at 0.6 whole, bar and focused label included); right or B pans back. In landscape both sit side
by side and the panel without focus is at 0.6 whole. The `casdap` watermark (14 u Light, alpha 0.08) sits in
the menu column at HOME_TEXT_X on baseline H - EDGE and hides while Quickplay is in front. Focus on the first row of a section scrolls the panel to that section's header (to the top when Pins is empty and History holds focus), so the headers stay reachable from the pad. Tile and header tops come from the section counts (section top + QP_CONTENT_DROP + row * pitch), and `rowsInserted`, `rowsMoved`, `rowsRemoved` and `modelReset` of the Pins, History and New models clamp the focus and scroll it back into view. Quickplay tiles carry
`section` and `kind`: `Pins` lists `Pins` (the phone tile first while a phone is connected, kind `phone`),
`History` the recent songs (kind `song`, `QuickplayRecent`, 8 rows), `New` the newest albums (kind `album`, `QuickplayNew`, 8 rows). X on a tile opens the
sheet `pin to quickplay` (`unpin` on a Pins tile), A on it runs `Library.pin` or `unpin` with the tile's kind
and id; A on a tile plays it through `Player.playSource(kind, id, "play", -1)` (a source with no rows or no player logs `play <kind> <id>`). The hub, artist and album sheets read
`unpin from quickplay` in place of `pin to quickplay` while `Library.isPinned(kind, id)` is true, and Main
runs `Library.pin` or `unpin` for both verbs. A on `music`, `bluetooth`, `playlists`, `history` and
`settings` pushes the page of that name; Main binds `playlistCount` to `Library.playlistCount`. The entrance (menu items, focus bar, Quickplay peek) plays once when the page is created
with `animated` true; `animated` turning true later changes nothing. A on an item runs the T2 exit before the
pushed page turns in: every menu item flies `y` to -0.25 H and opacity to 0 over 250 expOut6, staggered 28 ms per
item outward from the focused one, the focus bar with the focused item and the Quickplay peek to 0 in the same
250; a pop back to home replays the T1 entrance with 0 stagger (T4) in place of a turn.

`MusicHub` is the `music` page (`objectName` `music`): header `music`, pivots `artists`, `albums`,
`songs`, `genres`, `playlists`, content from CONTENT_TOP. Each alphabetic pivot is a `FocusList` pane bound to a C++ hub
model over the lazy `Artists`, `Albums`, `Songs`, `Genres` context models: `ArtistsHub`, `SongsHub`, `GenresHub`
(`SectionedModel`, kind `artist`, `song`, `genre`) and `AlbumsGrid` (`AlbumGridModel`, `columns` bound to
`Ui.gridCols`). A source model loads nothing until `ensureLoaded()`; a hub model holds 0 rows until `active`, which
the hub sets the first time a pivot is shown (the start pivot at creation, a switched to pivot before the T5 out
phase), and the pane binds its model from then on; a source reset while a pane is unbound changes nothing on screen.
The `section` letter (`#`, a..z, the kana rows あ か さ た な は ま や ら わ, 漢) groups the rows. `sectionKey`
(`src/core/SectionKey.cpp`) gives every model, `Library.allTracks()`, `genreTracks(id)` and `entriesFor` `all` and
`genre` the letter and the sort key of a name (the artists drop a leading `the `): a..z by the first character;
after NFKC, a leading kana goes to its row (katakana read as hiragana, small, voiced and semi voiced kana as the base,
ヴ as う, ゐ ゑ を ん as わ, ー, ゝ ゞ ヽ ヾ and the voicing marks skipped), a leading Han ideograph to 漢, anything else
to `#`; groups sort in that order, Latin by the lower case name, kana by the folded hiragana (ties by the unfolded
one), 漢 by code point, equal keys by id. Artists, songs and genres get a divider row per letter (ROW_HEIGHT, the 20 u Regular letter at
HUB_EDGE on the LETTER_TILE baseline inset), focusable like the album letter cells (every row `isFocusable`,
`nearestFocusable` clamps to the list; the focus bar spans the divider row, the letter stays at 1.0); albums are rows of
`Ui.gridCols` cells (3 portrait, 6 at W 483, `cells(row)`) with a `LetterTile` cell before each letter group and a `Tile` per album, one
`Loader` per cell choosing the two; the cell
is TILE_S + 24 tall, rows at TILE_S + 24 + GUTTER pitch; a column change keeps focus on the same album (`cellOfAlbum`). Artist rows carry the count line (`1 ALBUM, 3 SONGS`) in the upper case
count role. Songs rows carry the 1 based position as the number; genres and
playlists are Bold upper case; `playlists` lists `NOW PLAYING` first with `N SONGS` from
`Player.queue.count`, then every row of the `Playlists` context model with its `N SONGS`. Up/down and LT/RT move
through the pane, left/right move within a grid row (no wrap), LB/RB switch pivots with T5 (outgoing pane
300 strip, incoming rows `min(i, 10) * 22` stagger over 380 strip, header parallax from the strip), no wrap at
either end. A on a song calls `Player.playSource("all", 0, "play", id)` (the whole library in the `Songs` order from
that song; without a player it emits `play`); A on a genre or playlist calls `Player.playSource(kind, id, "play", -1)`
(without a player both emit `play(kind, id)`); A on an
album or artist emits `open(kind, id)` (Main pushes `AlbumDetail` for `album` and `ArtistDetail` for `artist`); A on `NOW PLAYING` emits `nowPlaying`; A on a letter cell or a divider, or Back/View on an alphabetic pivot, emits
`jumpRequested(letter)` with the focused letter and opens the `JumpGrid` over the hub with the model's `letters()` (T14: the grid is full screen on `Ui.bg` over the list, swiveling in about
a horizontal center hinge from -30 degrees over 350 expOut6, out to 30 over 250 expIn1). `jumpTo(letter)` from
the grid snaps the pane so the letter's divider (`rowOfLetter`, or the letter cell from `cellOfLetter`) sits at the top, focuses that divider
(the letter cell on albums), so A opens the grid again and down enters the rows, and shows the 120 u `JumpLetter` (T15). `letters()`, `rowOfLetter`,
`firstRowOfLetter` and `cellOfLetter` read a letter index built with the rows (rebuilt on a reset), every focus step and LT/RT page is one
model call. LT/RT show the focused row's letter (a divider's own letter) in the
same `JumpLetter`. X opens the sheet `play`, `play next`, `add to queue`, `shuffle`, `pin to quickplay` (no
`shuffle` on a song; `play` and `add to queue` only on a genre); Y is `add to queue`; X and Y on a divider or a letter cell do nothing; `play` on a song, genre or
playlist plays, every other verb emits `sheetAction(action, kind, id)` and runs through Main's queue verbs. B pops. After
`Library.scanning` went true then false, the next change of a bound hub model runs F12 on the shown pane (rows `x` -5 to 0,
`o` 0 to 1, 220 decel, `min(i, 10) * 50` stagger); a change during the scan runs it when the scan ends. A row
delegate reused while T5, T2a or F12 runs takes the running entrance.
`screenOff` or `animated` false lands the pivot, settles the rows and dismisses the letter.

`JumpGrid` (`screens/JumpGrid.qml`) is the letter grid hosted by the hub: `exit` top left in the 7 u overlay
label role, the 38 `LetterTile` cells (`#`, a..z, あ か さ た な は ま や ら わ, 漢; kana and 漢 through the fontconfig
fallback) in `Ui.jumpCols` columns (5 portrait, 9 landscape)
with the JUMP_GRID tile (`Ui.jumpTile`, the smaller of the width fit and the height fit of `Ui.jumpRows` rows, 8
and 5) and 8 u gap from EDGE, the first row starting after `Ui.jumpExitCells` (2) empty slots
so `exit` sits in the first slot, the block centered vertically with its top at least EDGE (`Ui.jumpTop`; 49 u tiles
from y 16 at 640x960, 42.7 u from y 37.3 at 960x640). `open(letters, letter)` takes
the letters that have rows and the hub's focused letter; a letter without rows draws at 0.4 and is skipped;
left/right step to the next letter with rows, up/down the next in the same column. `exit` is a focus stop in its slot:
left from the first letter with rows, or up from the column under it with no letter with rows in between, reaches it;
focused, the label reads 1.0 with the 2 u bar at EDGE - 8 spanning the slot; right goes to the first letter with rows,
down to the next in its column; a grid opened with no letters starts on it. A emits `jumpTo(letter)`
and closes, A on `exit`, B, Back/View and Start close with the hub focus unchanged. `screenOff` lands the swivel.

`PlaylistsScreen` is the `playlists` page: header `playlists`, the count `N PLAYLISTS` in the 11 u count role
at HUB_EDGE on the pivot row baseline, the hub PLAYLISTS rows in a `FocusList` from CONTENT_TOP: `NOW PLAYING`
first with `N SONGS` from `Player.queue.count`, then every row of the `Playlists` context model (roles `id`,
`title`, `subtitle`), all Bold upper case with the play ring. A on `NOW PLAYING` emits `nowPlaying`, A on a
playlist calls `Player.playSource("playlist", id, "play", -1)` (without a player it emits
`play("playlist", id)`); X opens the sheet `play`, `play next`, `add to queue`, `shuffle`, `pin to quickplay` (or `unpin from
quickplay`), Y is `add to queue`, both emit `sheetAction`. A playlist model change rebuilds the rows with F12
and swaps the count text with F7 (opacity 1 to 0.2 to 1, 200 + 200). B pops.

`HistoryScreen` is the `history` page: header `history`, pivots `recent`, `new`, each a `FocusList` of hub SONGS
rows without the number (title 14 u and subtitle 11 u at HUB_EDGE, ROW_HEIGHT pitch, no dividers, no art) from
CONTENT_TOP, delegates reading the `HistoryRecent` (kind `song`, title and artist, 100 rows) and `HistoryNew` (kind `album`, title and
album artist, 100 rows) models directly. Up/down and LT/RT
move through the rows, LB/RB switch pivots with T5 (outgoing pane 300 strip,
incoming rows `x` +-34 u to 0 with `min(i, 10) * 22` stagger over 380 strip, header parallax from the strip). A
`rowsInserted`, `rowsRemoved`, `rowsMoved` or `modelReset` on the shown pivot runs F12 from the first changed row
(the lower end of a move, the first visible row on a reset) and keeps the focus index. A plays the song or the album
through `Player.playSource(kind, id, "play", -1)`, without a player it emits `play(kind, id)`; X opens the sheet `play`, `play next`,
`add to queue`, `shuffle` (albums only), pin or unpin, Y is `add to queue`, Back/View does nothing. B pops. `screenOff` or `animated` false lands the pivot.

`SettingsScreen` is the `settings` page: header `settings`, a scrolling column from CONTENT_TOP of four group
labels (the 8 u Regular upper case section label role at HUB_EDGE, ROW_HEIGHT tall, baseline 8 u above the
row bottom, skipped by focus) and SETTINGS_PITCH rows (17 u Bold upper case title at HUB_EDGE, 11 u Light
upper case value right aligned at W - HUB_EDGE; focused 1.0 and 0.85, others 0.6 and 0.4). OUTPUT: `OUTPUT`
(`AUTO`, `SPEAKER`, `HEADPHONES`, `USB` from `Settings.output`; `USB` is in the cycle only while
`OutputControl.usbPresent` is true; the value reads `NO JACK` while it is `headphones` and
`OutputControl.jackPresent` is false, `USB (NOT PRESENT)` while it is `usb` and no USB sink is listed, the
setting is kept either way), `HEADPHONE GAIN` (`LOW`, `NORMAL`, `HIGH` on
`Settings.hpGain`), `HEADPHONE AMP` (`LOHIFI`, `ULP`, `HIFI` on `Settings.hpAmp`; both read `BUILT IN ONLY` and
A enters no edit mode while `OutputControl.activeOutput` is `usb`), `SPEAKER DIGITAL GAIN`
(84 to 108 on `Settings.spkGain`, shown as dB from 84: `0 DB`, `+6 DB`, `+12 DB`, `+15 DB`, `+18 DB`, `+21 DB`,
`+24 DB`; the hardware takes the value when playback restarts), `VOLUME STEP` (`2%`, `5%`, `10%` on
`Settings.volumeStep`), `BLUETOOTH CODEC` (`AUTO`, `APTX`, `AAC`,
`LDAC`, `SBC` on `Settings.btCodec`; `<CODEC> (PENDING)` while `CodecFragment.pending`), `BIT PERFECT` (`OFF`, `ON`,
`EXCLUSIVE` on `Settings.outputLevel`; `OFF` on every output for the level `off`; with USB active `OutputMode.levelStatus`,
the word plus the first broken status suffix such as `ON (RESAMPLED)` or `EXCLUSIVE (PHONE)`; `USB ONLY` on the speaker
and the headphones; read only `DAP ONLY` in overlay mode; without `OutputMode` the word alone), `SAMPLE RATE` (`AUTO`
or a rate on `Settings.sampleRate`: `AUTO <AudioInfo.hwRate>`, `AUTO` without a rate, the fixed `<RATE>`, `<RATE> (NOT
SUPPORTED)` when `OutputMode.rates` lacks it; the edit steps `AUTO` then `OutputMode.rates` (the USB card's
`stream0` rates, else 44100, 48000, 88200, 96000, 176400, 192000), a saved rate the output lacks stepping from the
nearest listed rate, and an edit open when the row turns read only closes without a write; read only `AUTO <AudioInfo.hwRate>` while
`OutputMode.rateReadOnly` (a level effective); read only `DAP ONLY` in overlay mode), `REPLAYGAIN` (read only `OFF`), `EQ` (the `Eq.activeOutput` profile: `OFF`, the file name
upper case, `CUSTOM`, `<NAME> (MISSING)`; `DAC EQ FAILED` while `DacEq.status` reads `failed` and the active output is USB, under a
level and with the level `off`; while `OutputMode.effective`, with `DacEq.status` `on` or `applying` a file or CUSTOM reads
`<NAME> (DAC)` or `CUSTOM (DAC)`, with `flat` the plain name, `OFF` and `<NAME> (MISSING)` read as they are, and `none` (the card not resolved yet), `unsupported` and
no `DacEq` (overlay mode, `--screenshot`) read `BYPASSED`; left/right inert, A emits `openEq` and Main pushes `EqScreen`, in
overlay mode as well), `PAIR A PHONE` (A emits `pairPhone`, Main pushes
`Bluetooth` on its PAIRING pivot; in overlay mode the value reads `PAIR IN DAP MODE` and A does nothing).
LIBRARY: `LIBRARY FOLDERS` (`N ON`; A opens one sub row per folder in `Settings.candidateRoots()` (`~/Music`,
`/run/media`, each direct child of `/run/media` that is a mount point or holds no mounted subfolder, and every
mount under the other children, each with its `Music` subfolder when present, `CASDAP_MEDIA_ROOT`
overrides the base) plus `Settings.roots`, path as written in 11 u Light with `ON` or `OFF`, A toggles it in
`Settings.roots`; the last root stays on), `RESCAN LIBRARY` (`N SONGS` from `Library.songCount`, `SCANNING` while
`Library.scanning`; left/right inert, A calls `Library.rescanRoots()` and does nothing while scanning). DISPLAY: `ACCENT` (one 12 u swatch of
`Ui.accentForHue(Settings.accentHue)` at the value position, no border; in edit mode left/right step
`Settings.accentHue` 10 degrees, wrapping 350 to 0 and 0 to 350, held directions repeat; the swatch shows the slider
hue also while the art holds the accent), `ACCENT FROM ART` (`ON`, `OFF` on `Settings.accentFromArt`; left and
right toggle),
`ORIENTATION` (`PORTRAIT`, `PORTRAIT FLIPPED`, `LANDSCAPE`, `LANDSCAPE FLIPPED` on `Settings.orientation`; in
edit mode left/right step only the value text, A writes it and T18 turns the view, B and any other close of
the edit leave it unchanged; `EXTERNAL DISPLAY` read only while orientation does not apply; read only with the
effective orientation while auto rotate is on, supported and orientation applies; absent in overlay mode),
`AUTO ROTATE` (`ON`, `OFF` on `Settings.autoRotate`; left/right toggle the value text, A writes it, B leaves it;
`UNSUPPORTED` read only, no edit mode, while `Sensor` is null or `Sensor.supported` is false (with `SscRotation`, until the
`device_orient` SUID arrives); absent in overlay
mode), `TOUCH` (`ON`, `OFF` on `Settings.touch`; left/right toggle `touchDraft`, A writes it, B and every other close
leave it; OFF disables Main's `TouchLayer`; absent in overlay mode), `SCALE` (0.8 to `Ui.sMax` in 0.1 steps, shows the effective `Ui.userScale`; a step writes
`Settings.scale` only when it differs from the effective value, B restores the saved `Settings.scale`; focus and
the scroll are kept), `SAVER` (10, 15, 30 s, 1, 2, 5 min on
`Settings.saverSeconds`), `RGB` (`ACCENT`, `OFF`, `ARMADA` on `Settings.rgb`; reads `UNSUPPORTED` and A enters no
edit mode while `RgbControl.supported` is false; absent in overlay mode), `TOASTS IN GAME` (`ON`, `OFF`
on `Settings.overlayToasts`). POWER: `SCREEN OFF AFTER`
(`OFF`, 1, 2, 5, 10, 30 MIN on `Settings.screenOffSeconds`),
`SUSPEND AFTER` (`OFF`, 5, 10, 30, 60 MIN on `Settings.suspendMinutes`),
`WI-FI WHEN OFF` (`KEEP`, `OFF` on `Settings.wifiOffScreenOff`), then in overlay mode only `CLOSE PLAYER` (A opens
the sheet `close player`, T12; accepting emits `closePlayerRequested` and calls `OverlayBridge.closePlayer()`).
`ABOUT` is the last row with the version (`Env.version`) as its value; A opens
read only lines in the value role: `ARMADA <IMAGE_VERSION>` and `IMAGE <digest>` from `Env.manifest` (the
`key=value` head of `/usr/share/casdap/build-manifest`, `CASDAP_MANIFEST_FILE` overrides), `DRIFT N FILES`
while `PowerBridge.driftPaths` is not empty, `TRACK 44100 HZ 16 BIT` from `Player.current.sampleRate` and
`bitDepth`, `OUTPUT 44100 S24_LE` from `AudioInfo.pcmFormat`. The page calls `AudioInfo.refresh()` when it opens.
Value rows (OUTPUT, HEADPHONE GAIN, HEADPHONE AMP,
SPEAKER DIGITAL GAIN, VOLUME STEP, BLUETOOTH CODEC, BIT PERFECT, SAMPLE RATE, ACCENT, ACCENT FROM ART, ORIENTATION, SCALE, SAVER, RGB, TOASTS
IN GAME, SCREEN OFF AFTER, SUSPEND AFTER, WI-FI WHEN OFF) change only in edit mode: A enters it (`editing`, `editKey`; the
value text turns `Ui.accent`, the title stays), left/right step the value live (lists stop at their ends), A
confirms, B writes back the value the row had when edit mode began (ORIENTATION, BIT PERFECT and SAMPLE RATE as
ORIENTATION: left/right step the value text only, A writes it once, B leaves it) and leaves edit mode without popping; up,
down, LT, RT, LB, RB, X, Y, Start and Back/View are taken and do nothing while editing; leaving the page, the
screen turning off or the overlay hiding confirms. Outside edit mode left/right do nothing on every row. The
other rows keep A as their action, read only rows take A and do nothing. A changed value text runs F7 (opacity 1
to 0.2 over 200, swap, 0.2 to 1 over 200).
The sub rows opened by LIBRARY FOLDERS and ABOUT arrive with F12 from the row under the opener.
LT/RT page by the content height. B pops. Main binds `Ui.userScale` to `Settings.scale` (clamped) and `Ui.accent` and
`Ui.accentBright` to `Accent.color` and `Accent.brightColor` (without `Accent`, `Ui.accentForHue` and
`Ui.brightForHue` of `Settings.accentHue`, else hue 350).

`EqScreen` is the `eq` page (`objectName` `eq`, `pageId` 0), pushed with the turnstile by the settings EQ row:
header `eq`, pivots `headphones`, `usb`, `speaker` (the `Eq.activeOutput` pivot active at open, an output change
while open leaves it), rows from CONTENT_TOP in the settings row style at SETTINGS_PITCH with the value right
aligned at W - HUB_EDGE in a slot starting `Ui.eqValueGap` (8 u) right of the title, clipped at its left edge.
`PROFILE` (`OFF`, the file name, `CUSTOM`, `<NAME> (MISSING)`; `Eq.cycleProfile(output, delta)`), then, only while
the profile is a parsed file or CUSTOM, `PREAMP` (`-6.5 DB`, `Eq.stepPreamp(output, delta, coarse)`) and
`BAND 1` to `BAND 10` (`PK 105 HZ +2.1 DB Q 0.70`, `LS`, `HS`, `BP`, `LP`, `HP`, `AP`, no gain field for the
four gain free types (`LP 105 HZ Q 0.70`), `2.5 KHZ` from 1000 Hz, `OFF` for a disabled band). A on a band opens
the sub rows `TYPE`, `FREQUENCY`, `GAIN`, `Q` under it (11 u Light upper case,
`Eq.stepBand(output, band, field, delta, coarse)` with field `type`, `freq`, `gain`, `q`) with F12, a second A closes
them, A on another band moves them there. TYPE cycles OFF, PEAK, LOW SHELF, HIGH SHELF, BAND PASS, LOW PASS,
HIGH PASS, ALL PASS (wrapping, coarse the same); on the four gain free types GAIN reads `NOT USED` in the value
style of `BUILT IN ONLY` and is inert (no edit mode, A and touch do nothing), and the stored gain applies again
when TYPE returns to PEAK or a shelf. PROFILE, PREAMP and the sub rows change only in edit mode, as in
`SettingsScreen`: A enters it (the value turns `Ui.accent`), left/right make the fine step (`coarse` false), LB/RB
the coarse step on PREAMP and the sub rows (`coarse` true; nothing on PROFILE), A confirms, B cancels without
popping, every other key is taken and does nothing; leaving the page, the screen turning off or an outside pivot
change confirms. Cancel replays the steps that changed `Eq.profileMap(output)`, newest first, with the direction
inverted (TYPE and PROFILE step back through their cycles), then, while the key differs from the one at edit start,
steps `cycleProfile(output, -1)` at most `Eq.sources.length + 2` times to reach it (a file edited into CUSTOM
returns to the file). A step that hit a clamp or snapped an off grid value is not undone exactly, and a
`<NAME> (MISSING)` key left by a PROFILE edit is not restored. Values come from `Eq.profile(output)` and refresh on
`profilesChanged`; a changed value swaps with F7, rows listed by a profile step arrive with F12. Outside edit mode
up/down move through the rows, LT/RT page, LB/RB switch pivots with T5 (no wrap; the switch closes the sub rows and
focuses PROFILE), left/right, X, Y and Back/View do nothing, B pops. Rows feather once on first show (T2a). Without
`Eq` the page lists PROFILE `OFF` on every pivot, A enters no edit mode and nothing steps. On the USB pivot with
`DacEq.status` `on` or `applying`, a PREAMP, GAIN or Q value outside `DacEq.ranges` (rounded as shown) adds what the
DAC gets: `-15.0 DB (DAC -12.0)`, `10.00 (DAC 8.00)`, `0.10 (DAC 0.25)`, `-6.5 DB (DAC 0.0)`; a band whose TYPE is
OFF and GAIN `NOT USED` take no suffix, the band rows keep their text, and stored values and steps keep the software
ranges. `screenOff` or `animated` false lands the pivot and the rows.

`ArtistDetail` is the `artist` page, pushed with the turnstile (T3) by `open("artist", id)` from the hub:
header `artist`, pivots `albums`, `songs`, the name (11 u Bold upper case) at HUB_EDGE on a baseline 15 u
under the pivot baseline (110 portrait, 94 landscape), the ring row play, add, shuffle (32 u, top y 150, 56 u pitch from HUB_EDGE), `IN COLLECTION` on
baseline 200, content from 212 on plain `Ui.bg` (the `Backdrop` carries no source). The rows come from `Library`:
`artistInfo(id)` (`name`, `artPath`: the first of the artist's albums with art), `artistAlbumRows(id)` (`id`,
`title`, `artist`, `year`, `artPath`) and `artistTrackRows(id)`
(`id`, `title`, `artist`, `album`, `albumId`, `durationMs`), one query each; a null library leaves the lists empty; a new `artistId` reloads them with F12 on the shown pivot. `albums`
is a `FocusList` of DETAIL_ROW cells (TILE_L art at HUB_EDGE with a 32 u play ring inset 8 from its top
right, 13 u Bold upper case title on baseline art top + 28 wrapping to two lines at pitch 15 and clipped at
W, the year under the last line, both at HUB_EDGE + TILE_L + 8), `songs` the SONGS style
rows. Focus starts on the play ring: left/right move along the rings, down enters the list, up from its first
row returns to the rings; up/down and LT/RT move through the list, LB/RB switch pivots with T5 (no wrap).
A on play calls `Player.playSource("artist", id, "play", -1)`, on shuffle the same with `shuffle`, on add
emits `sheetAction("add to queue", "artist", id)`, which appends every artist song; every ring A runs the F4 swap. A on an album emits
`openAlbum(id)` (Main pushes `AlbumDetail`), A on a song plays the artist from that song (`startId`). Y on an album or song row adds that
row to the queue, Y with the rings focused plays shuffled. Back/View emits `openQueue` (Main pushes `QueueScreen`
unless the queue is on top). X opens the
sheet `play`, `play next`, `add to queue`, `shuffle` (not on a song), `pin to quickplay` on the focused row, or on the artist while the rings hold
focus; `play` plays (the artist, the album through `playSource("album", id, "play", -1)`, or from the song), every other
verb emits `sheetAction(action, kind, id)` and runs through Main's queue verbs. B pops. Landscape keeps the name and rings in
the column to 200 and runs the label and the list from x 216 (`IN COLLECTION` with its cap top at
CONTENT_TOP, rows 12 u under its baseline). `screenOff` or `animated` false lands the pivot, settles the
rows and the sheet.

`AlbumDetail` is the `album` page, pushed with the turnstile (T3) by `open("album", id)` from the hub or
`openAlbum` from the artist page, laid out as the now playing top stack: no header word, back ring (64 u at
x 0, y -15, clipped by the screen edge, decorative) top left, `StatusStrip` top right, the album artist (20 u Bold upper case) on baseline
117 and the title (20 u Light upper case) on 141 at EDGE, TILE_L art at EDGE y 155, track rows from y 283
(art bottom + 12; `albumInfo(id)` and `albumTracks(id)`, a new `albumId` reloads them with F12): 1 based number, title, the
duration `m:ss` right aligned at W - HUB_EDGE (absent when unknown), the accent dot left of the number on the
row whose id is `Player.current.id`. The page is plain `Ui.bg`; the art shows only in its square. Focus starts on row 0; up/down and
LT/RT move, A calls `Player.playSource("album", id, "play", trackId)` (the album in disc and track order from that row), Y emits
`sheetAction("add to queue", "album", id)`, X opens the sheet `play`, `play next`, `add to queue`, `pin to quickplay`
(`play` from the row, the rest emit `sheetAction(action, "song", id)`), Back/View emits `openQueue`, B pops. Landscape keeps the back
ring and the art (y CONTENT_TOP) in the column to 200, artist on baseline 60 and title on 84 from x 216, and
runs the rows from x 216, y CONTENT_TOP.

`NowPlaying` is the `nowplaying` page, pushed with the `fade` kind (T7) by Start on any other page, A on the
hub's `NOW PLAYING` row, A on the Bluetooth page's transport row and the Quickplay phone tile; Start with
no current track and no phone transport logs `now playing empty`. Portrait is three stacks on plain `Ui.bg`
(the `Backdrop` carries no source): back ring (64 u at x 0, y -15, clipped by the screen edge, decorative), `StatusStrip` top right (battery
from `StatusModel`, accent under 10 percent, Bluetooth glyph while a phone is connected, play or pause
glyph), artist baseline 117 and album 141 at EDGE, art square at y 155 with side `min(168, W - 2 EDGE,
H - 311)`, progress line at y H - 156 from EDGE to the art right edge (fill accent from `Player.position`,
read every 250 ms; elapsed at EDGE and remaining right aligned at the art right edge under it, refreshed at
1 Hz), title baseline H - 118, the next three rows of the play order (`Queue.upcoming(3)`, refreshed on
`modelReset`, `currentSlotChanged`, `countChanged`, `rowsMoved`, `orderChanged` and `repeatChanged`) at EDGE + 40
and 18 u pitch, three rings (shuffle, repeat, rate) centered at 0.13, 0.5 and 0.87 of W on y H - 40, white
only: on at 1.0, off at 0.6, the focused one with the 4 u stroke. A track outside the library (id 0, an MPRIS
`OpenUri` of a file `library.db` lacks) shows no rate ring and the ring focus stops at repeat.
Landscape: back ring and art (y 72, side `min(168, H - 106)`) with the progress line 8 u under it and as
wide as the art in the
left column, artist 60, album 84, title 116, next rows from 140 at x 216, rings on the same fractions of
`W - 216 - EDGE` from x 216. Focus starts on the ring row: left and right move it, A toggles the focused
ring with the F4 swap (`Player.toggleShuffle`, `cycleRepeat`, `rate`), up moves to the title row (2 u bar
at EDGE - 8) where A emits `openQueue` (Main pushes `QueueScreen`), down returns. LB and RB skip, X plays
or pauses, Y and Back/View emit `openQueue`, LT and RT released under 400 ms seek -10 or +10 s, held 400 ms they
seek 1000 ms every 250 ms until release with no 10 s step; RT clamps at the duration - 1000 ms. B and Start pop with the fade kind (T8).
A track change runs T9 (title, next rows and times out 500 easeOut with the art, in 400 easeIn with the
art scaling from 0.8 about (0.2, 0.5)); a second `currentChanged` for the same track or a new track during
the out phase replaces the pending track without restarting the animation. After 5000 ms without input the unfocused rings and the status strip fade to 0.4
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
(`Player.queue.currentSlot + 1` and `count`, `0 OF 0` when empty) in the 11 u upper case count role at HUB_EDGE on
the pivot row baseline (95 portrait, 79 landscape), SONGS style rows (1 based slot number, title, artist) bound to
`Queue.playOrder` (one row per slot) in a
`FocusList` from CONTENT_TOP, the slot whose `isCurrent` role is set
`playing` (accent title). Both orientations run the same stack. Focus starts on the current slot; up/down and
LT/RT move through the list, A calls `Player.playAt(row)` with the slot's queue row, Y removes the focused row, X opens the sheet
(T12) with `remove`, `play next`, `move up`, `move down`; `play next` is absent on the current slot and the
one after it, `move up` on the first slot, `move down` on the last. `remove` calls `Queue.remove(row)`
(removing the current row is handled by the queue, the next track starts), `move up` and `move down` call
`Queue.moveSlot(slot, slot +- 1)`, `play next` calls `Queue.playNextRow(row)`; a moved row keeps
focus at its new slot, a removed row leaves focus at the same index clamped to the end. Every play order
change (`rowsInserted`, `rowsRemoved`, `rowsMoved`) runs F12 from the first changed slot (the lower end of a move), a
`modelReset` from the first visible slot, on the visible rows at and below it (`x` -5 to 0, `o` 0 to 1, 220 decel, stagger
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
page is back. The stick lights sleep while the saver is up (`RgbControl.setSaver`). `screenOff` lands the fades and stops the clock, the dim timer and the drift.

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
hidden (`rootHidden`); the saver and screen off timers stop. `PadRouter` drops every action and `Pad`
stops polling. `screenOff` false shows
the same page again with no transition, restores `Ui.bg` and restarts both timers from zero. The screen off
timer (`screenOffTimer`, `Settings.screenOffSeconds`) runs `PowerBridge.runVerb("screen-off")` after that
long without a routed action, playing or not and with a dialog up; OFF (0) never arms, overlay mode never
arms, a tracked switch (`PowerBridge.switching`) cancels it, every routed action and `screenOff` false
restart it.

Screenshot mode: `casdap-player --screenshot <screen> --size WxH --out file.png [--angle deg] [--overlay]
[--orientation portrait|portrait-flipped|landscape|landscape-flipped]` (a portrait `--size` renders the rotor at
that turn; `--state orientation` focuses the ORIENTATION row on `settings`, `--state orientation-edit` also opens
its edit and steps right, `orientation-edit-end` steps to LANDSCAPE FLIPPED, `orientation-external` gives Main an
`Outputs` that reads not internal, `auto-rotate` focuses AUTO ROTATE with it ON, `auto-rotate-unsupported` with a
sensor that reads unsupported, `auto-rotate-below` focuses SAVER with AUTO ROTATE ON, `auto-rotate-off` focuses
AUTO ROTATE with it OFF, `auto-rotate-edit` opens its edit and steps to ON; the `settings` sample reads AUTO ROTATE OFF;
`pressed` sets `pressed` on the `hub`, `album`, `queue`, `home`, `quickplay` and `nowplaying` page so the focused
item shows its F3 scale (on `hub` a focused divider or letter tile first moves down or right to a song row or album cell),
`touch-scrolled` sets the `hub` or `queue` list `contentY` to one and a half rows and runs `clampFocusToView()`,
`touch` focuses TOUCH on `settings`, `touch-edit` also opens its edit with OFF drafted; the `settings` sample reads TOUCH ON;
`bit-perfect` focuses BIT PERFECT ON on a USB output, `bit-perfect-edit` opens its edit with EXCLUSIVE drafted,
`bit-perfect-phone` and `bit-perfect-unavailable` read EXCLUSIVE (PHONE) and EXCLUSIVE (UNAVAILABLE),
`bit-perfect-usb-only` reads USB ONLY on the headphones, `sample-rate-edit` drafts 352800 on the FiiO rates,
`sample-rate-unsupported` reads 96000 (NOT SUPPORTED) on the Apple dongle rate, `sample-rate-locked` focuses EQ
BYPASSED under SAMPLE RATE read only AUTO 96000, `eq-dac` focuses EQ under BIT PERFECT ON reading
`SENNHEISER HD 600 PARAMETRICEQ (DAC)`, `eq-dac-custom` `CUSTOM (DAC)`, `eq-dac-failed` `DAC EQ FAILED` and
`eq-dac-unsupported` `BYPASSED`; the USB states set OUTPUT to USB),
offscreen, software renderer; the fonts come from `CASDAP_FONT_DIR` as in the player, and a grab with no Selawik
prints `casdap-player: warning: Selawik not loaded, set CASDAP_FONT_DIR` and still writes the file. Screens: `blank`, `home`, `quickplay` (`--bluez off|connected`), `prompt` (the switch prompt over home), `power` (the power menu over home), `saver` and `saver-dim` (the saver with the clock fixed at 7:13, the dim variant at 0.6), `hub`
(`--pivot artists|albums|songs|genres|playlists`, default `albums`; the `--library <dir>` models when
given, else sample rows), `nowplaying` (sample track and queue; `--bluez active` for the Bluetooth variant),
`queue` (a sample queue of 20 with row 3 playing), `artist` and `album` (a sample artist with two albums
and ten tracks, track 3 playing), `settings` (sample values, two folders, a FLAC track and `44100 S24_LE`),
`jump` (the grid over the hub pivot of `--pivot`, default `albums`), `playlists` (`NOW PLAYING` and four
sample playlists), `history` (seven recent songs and five new albums), `eq` (a parsed file on HEADPHONES with
band 2 open, a missing file on USB; states `eq-types` (focus BAND 5) and `eq-types-end` (focus BAND 10) list
OFF, the seven types and the longest band value with no band open; `eq-open-hs` opens BAND 4 (HIGH SHELF)
focused on TYPE, `eq-type-edit` opens BAND 5 (BAND PASS) with TYPE in edit mode, `eq-gain-unused` opens BAND 6
(LOW PASS) focused on GAIN `NOT USED`, `eq-open-hp` and
`eq-open-ap` open BAND 7 (HIGH PASS) and BAND 8 (ALL PASS) focused on TYPE; `eq-dac-range` opens the USB pivot under
a `DacEq` that reads `on` with BAND 1 open at `-15.0 DB (DAC -12.0)` and `10.00 (DAC 8.00)`, focused on GAIN,
`eq-dac-preamp` focuses PREAMP `-6.5 DB (DAC 0.0)` with BAND 2 open at `-24.0 DB (DAC -12.0)` and `0.10 (DAC 0.25)`,
`eq-dac-long` focuses that GAIN with Q on screen),
`components`, `overlays` (the volume box clamped to the 272 x 420 or 480 x 260 minimum canvas), `volume`, `specimen` (every type role in its weight, labeled
with name and px), `motion` (30 rows mid turnstile at `--angle`, default 40). `--overlay` renders `home` and
`settings` in overlay mode (CLOSE PLAYER and `PAIR IN DAP MODE` rows). CMake cache var
`CASDAP_TEST_FONT_DIR` (default `../dist/usr/share/fonts/casdap`) feeds `CASDAP_FONT_DIR` to the QML
tests and the specimen shots; font dependent QML cases skip when the directory is empty.

## Builder

The builder container is `FROM` the exact Armada image the device runs, pinned by digest in
`build/base.env`. Compilers and `-devel` packages are installed at the EVR of the runtime already in
the image (`gcc`, `gcc-c++`, `libstdc++-devel` at the `libstdc++` EVR, `glibc-devel` at the `glibc`
EVR, `qt6-qtbase`, `qt6-qtdeclarative`, `gstreamer1`, `SDL3`, `pipewire`, `taglib`, `alsa-lib` `-devel` at their runtime
EVR), falling back to koji when the mirrors moved on and to negativo17 when the runtime package came
from there. `toolchain.sh` fails the image build if any pinned runtime package changed. Build host
is any podman with arm64 binfmt (SePC); `ghcr.io/seilent/armada` pulls without a login.

## Recipes

| Recipe | Does |
|---|---|
| `just base` | Reads `IMAGE_VERSION`, `VERSION_ID` and the booted image digest from the device into `build/base.env`. |
| `just dist` | Builds the builder image and runs `build/build.sh`: copies `sysext/`, generates the patched files and hashes, installs the Selawik 1.01 TTFs (`build/fonts.sh`, sha256 pinned, cached in `build/cache/`) to `usr/share/fonts/casdap/`, runs `build/aptx.sh` (the aptX codec rpms, sha256 pinned, cached in `build/cache/aptx/`, into `usr/lib64/`), writes `version` and `build-manifest`. |
| `just build` | `dist` plus `casdap-<ver>-armada<VERSION_ID>-arm64.raw` (squashfs, zstd). |
| `just dev` | Refuses when the device `IMAGE_VERSION`, `VERSION_ID` or digest differ from `dist/usr/share/casdap/build-manifest` (no force flag, run `just base && just dist`). Then rsyncs `dist/` to `/var/lib/extensions/casdap/` and `build/device-apply` to `/tmp/casdap-apply`, runs it as root (refusing unless the user's `gamescope-session-plus@steam.service` is active, `CASDAP_DEPLOY_ANY_SESSION=1` overrides; `systemd-sysext refresh`, restarts polkit and `casdap-guard`, `udevadm control --reload`, `udevadm trigger --settle --action=change` on the USB devices `2972:0149`, `casdap-usb-audio --scan`, restarts inputplumber and runs `controller-type apply` when a file under `usr/share/inputplumber/` changed, touches `/tmp/casdap-restart-audio` when a file under `usr/share/pipewire/`, `usr/share/wireplumber/`, `usr/lib64/spa-0.2/` or `usr/lib64/libfreeaptx*` changed), then as the user `systemctl --user daemon-reload`, restarts `casdap-hotkeyd.service` and, with the marker present, the user `pipewire pipewire-pulse wireplumber`. |
| `just dev --input` / `just dev-input` | `dev` with the inputplumber restart forced. |
| `just test-player` | ctest of `build/player` inside the builder (after `just dist`); prints `skip: no builder image` where the image is absent. |
| `just off` | Runs `build/device-off` as root. With `gamescope-session-plus@casdap.service` active, activating, deactivating, reloading or refreshing it starts `casdap-switch-back.service`; with only the autologin naming `casdap.desktop` it runs `session-control default-gamemode` and refuses if that fails. It waits up to 60 s until the unit has finished stopping (its `ExecStopPost` steps included) and the autologin no longer names casdap, else refuses and leaves the extension in place. Then it stops `casdap-overlay`, `casdap-bt`, `casdap-pad` and `casdap-powerd`, removes only `/var/lib/extensions/casdap` and `/run/casdap` (no global unmerge, other extensions stay), runs `systemd-sysext refresh` and both daemon reloads, stops `casdap-guard` and `casdap-usb-audio` after the system reload and `casdap-hotkeyd` after the user reload, once their `Upholds=` drop-ins are gone, restarts polkit and inputplumber and runs `controller-type apply`. |
| `just test` | `tests/session-control-test.sh`, `tests/guard-test.sh`, `tests/priv-test.sh`, `tests/hotkeyd-test.sh`, `tests/pad-test.sh`, `tests/intercept-test.sh`, `tests/keep-test.sh`, `tests/codec-test.sh`, `tests/power-test.sh`, `tests/powerd-test.sh`, `tests/wait-x-test.sh`, `tests/overlay-unit-test.sh`, `tests/bt-test.sh`, `tests/usb-audio-test.sh`, `tests/fiio-unit-test.sh`, `tests/device-off-test.sh`, then `just test-player`. Needs Armada's `session-control` with `sessions.d` support at `CASDAP_BASE_ROOT/usr/libexec/armada/` (defaults to `/`, else `../armada/system_files`), run against casdap's drop-in. |

`CASDAP_DEVICE` overrides `armada@pm2`. Root steps on the device run through `ssh -t`
and `sudo`. Run the dev loop from game mode or ssh, not from inside the casdap session.

## CI

`.github/workflows/build.yml` runs on `ubuntu-24.04-arm` for pushes to main and pull requests: the builder
image, `build/build.sh`, ctest and the `just test` scripts. The builder is pulled from
`ghcr.io/seilent/casdap-builder:<key>` (key from `Containerfile`, `toolchain.sh` and `base.env`), else built,
and pushed only from main. `build/cache` is kept in `actions/cache`. `dist/` is uploaded for 7 days as
`casdap-dist-<sha>` (`dist.tar`, modes kept): `gh run download -n casdap-dist-<sha> && tar -xf dist.tar && just dev`.

## Session

`casdap.desktop` runs `gamescope-session-plus casdap`. `sessions.d/casdap` sets
`CLIENTCMD=/usr/bin/casdap-player` and `ORIENTATION=normal`, so gamescope keeps the panel's native 640 x 960
portrait and the player turns its own view (Player UI); `/etc/gamescope-session-plus/sessions.d/casdap` (runtime, not
shipped) overrides them, for example `export CLIENTCMD=konsole`. gamescope runs with `--steam` and shows
only a window whose `STEAM_GAME` matches the root `GAMESCOPECTRL_BASELAYER_APPID`; with no Steam in the
session the player sets both to `CASDAP_GAMESCOPE_APPID` (`0x43415344`, exported by `sessions.d/casdap`)
on its own X window at start. A client without them stays invisible. Five sessions shorter than 60 s in a
row start `casdap-switch-back.service`.

Switching, as user `armada`, no root:

```console
$ systemctl start casdap-switch.service
$ systemctl start casdap-switch-back.service
```

casdap registers with Armada's `session-control` through `usr/lib/armada/sessions.d/casdap.conf`
(`DESKTOP=casdap.desktop`, `UNIT=gamescope-session-plus@casdap.service`); `session-control` itself is
Armada's, unpatched. `switch-casdap` writes the SDDM autologin drop-in, asks Steam to exit with
`steam -shutdown` (15 s bound), waits for the Steam unit to stop and the seat0 session to close, removes
`/tmp/chimeraos-short-session-tracker`, and only restarts sddm when that fails. From Plasma it logs out
through `org.kde.Shutdown`. `switch-gamemode` and `switch-desktop` stop `gamescope-session-plus@casdap.service`
when it runs and then run `controller-type apply`; `switch-desktop` from Steam is Armada's sddm restart. Each
switch holds Armada's switch lock until the target session is up (Armada `POCKET_MICRO2.md`); both switch
units allow 150 s (`TimeoutStartSec`), the budget of Armada's `armada-session-switch@.service`. Every boot
still resets to game mode.

Overlay mode: `casdap-player --overlay` on the Steam session display (`DISPLAY=:0`) is the same
player as the game mode instance. No SDL; the pad is `DbusPad` from InputPlumber. The X window is
created unmapped with `STEAM_OVERLAY` 1 and `GAMESCOPE_NO_FOCUS` 1 and the window state set to
fullscreen before the first map; gamescope draws it as its global overlay and keeps focus and game
audio with the game. `STEAM_GAME` and the baselayer appid are never set. `ShowOverlay` flips `OverlayShown` and
emits `PropertiesChanged`, writes `InterceptMode` 3, maps and fades in over 200 ms; `HideOverlay`
flips it back, writes 0 and fades out over 150 ms. After the fade it waits for the next swapped frame
(at most `ReleaseFrameMs`, 100 ms), then unmaps and destroys the X window back to back (gamescope keeps an
unmapped window registered as its overlay and composites a blank frame over Steam; a destroyed one is
gone) and saves `state.json` on a zero timer after the destroy. A show during that wait keeps the window
and fades in again. The QML root and its page stack stay alive; the next `ShowOverlay` creates the window again
with the same atoms and state before mapping. The overlay window has swap interval 0, the atoms are
interned once per process, and the scene graph pipeline cache is
`$XDG_CACHE_HOME/casdap/overlay-pipeline.bin` (loaded at init, saved at each destroy). `InterceptMode`
goes out one write per device at a time: a write asked for while one is in flight waits and only the
newest one is sent, none when it equals the one in flight. The journal carries one
`casdap overlay: <step> after <n> ms` line per step from the request: `created`, `mapped`, `first frame`,
`fade done` on a show, `fade done`, `last frame` (or `no frame`), `unmapped`, `destroyed`, `saved` on a
hide. `ToggleOverlay` during a fade reverses it from
the current opacity without a second map or destroy; `Show` during a show fade and `Hide` during a hide fade do
nothing. `SetScreenOff` and `ShowPowerMenu` are logged and ignored in this mode. While hidden the
4 Hz position poll is off (`position` is read from `playbin` for each save), `StatusModel` and the
Bluetooth interpolation are inactive, the QML root is not visible. With `NOTIFY_SOCKET` set the bridge
sends `READY=1` once after `org.casdap.Player` is registered and the hidden window is created, then
`WATCHDOG=1` every 7 s, shown or hidden. When `org.casdap.Player` is already owned at start the
overlay instance logs `another player owns org.casdap.Player, exiting` and exits 0; the DAP instance
warns and continues. CLOSE PLAYER in the overlay settings (`OverlayBridge.closePlayer()`) writes
`InterceptMode` 0, hides the overlay, deletes the keep marker and saves `state.json` with `handoff` false, then
runs `casdap-bt --disconnect-sources` and `casdap-codec clear` one after the other through a tracked `QProcess`
(`CASDAP_BT_BIN`, `CASDAP_CODEC_BIN` override the paths; a failed or missing step is logged and skipped), emits
`closed` and quits 0, so the unit ends inactive. Steps still running 2500 ms after the call are killed and the
player quits. A second call while it runs is logged and does nothing. Every hide saves `state.json` with
`handoff` false and the current stack.

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
CLOSE PLAYER saves `state.json` with `handoff` false, deletes the marker and starts the switch. The switch first
runs `OutputMode.releaseForSwitch()` and starts the service on `switchReady`, which comes at once with the level
`off` and with nothing held. After an EXCLUSIVE release it comes once the name is released and the DAC sink is back
and read, the sink and read phases bounded at 1000 ms; when it has not come within the reservation call bound plus
that 1000 ms plus 500 ms (2500 ms) the switch starts anyway (`casdap power: no output release within <n> ms,
switching`). `CASDAP_SWITCH_TIMEOUT_MS` runs from the service start. The
DAP session client start deletes the marker as well and runs `casdap-power screen-on` once through the same
tracked `QProcess` (`PowerBridge::runScreenOn`), so the CPU floor and the backlight are set before the first
frame; `--overlay`, `--screenshot` and `--scan-only`
runs never touch either. The start process ending with a non zero code, failing to start, or
`CASDAP_SWITCH_TIMEOUT_MS` (default 30000) passing with neither `finished` nor SIGTERM is a failed
switch: the marker is deleted, `state.json` is saved with `handoff` false, the prompt closed and the
toast `switch failed` shown. A non zero code or a failed start rebuilds the player at its position in the play state
it had before the switch only when the switch released the DAC or parked the player (a play waiting for an acquire or
a codec restart); a timeout never rebuilds and leaves a parked player parked; with the level `off` nothing is
rebuilt. A second `keepInBackground()` or `closeAndSwitch()` while a release or a start is
tracked is logged and does nothing.

Polkit boundary: any process running as `armada` may start `casdap-switch.service`,
`casdap-switch-back.service` and `casdap-priv@<verb>.service`.
The verbs are fixed words (`bt-class-headphones`, `bt-class-default`, `backlight-off`, `backlight-on`,
`cpu-dap`, `cpu-off`, `cpu-resave`, `cpu-restore`, `hp-gain-low`, `hp-gain-normal`, `hp-gain-high`, `hp-amp-lohifi`,
`hp-amp-ulp`, `hp-amp-hifi`, `spk-gain-84`, `spk-gain-90`, `spk-gain-96`, `spk-gain-99`, `spk-gain-102`,
`spk-gain-105`, `spk-gain-108`, `rgb-hue-0`, `rgb-hue-10`, ... `rgb-hue-350` (36 words, multiples of 10),
`rgb-color-<h>-<s>` (144 words, `h` a multiple of 10 from 0 to 350, `s` 1 to 4),
`rgb-off`, `rgb-sleep`, `rgb-wake`, `rgb-restore`);
`casdap-priv` exits 1 on anything else. No value from the caller reaches root: the mixer step, the hue and
the saturation level are part of the word.

Mixer words: `hp-gain-low`, `hp-gain-normal`, `hp-gain-high` write `HPHL Volume` then `HPHR Volume` 8, 20,
24; `hp-amp-lohifi`, `hp-amp-ulp`, `hp-amp-hifi` write `RX HPH Mode` CLS_H_LOHIFI, CLS_H_ULP, CLS_H_HIFI
then `RX_HPH PWR Mode` LOHIFI, ULP, LOHIFI; `spk-gain-<n>` writes `WSA_RX0 Digital Volume` then
`WSA_RX1 Digital Volume` at `<n>`. Every write is `amixer -D hw:0 cset name='<control>' <value>`
(`CASDAP_AMIXER_BIN` replaces the binary); a refused write is logged, the second control is still written
and the word exits 1.

RGB words run `/usr/libexec/armada/armada-rgb-env` (`CASDAP_RGB_BIN`) against `/etc/armada/rgb.json`
(`CASDAP_RGB_CONFIG`) with the save file `/run/casdap/rgb-before.json` (`CASDAP_RGB_BEFORE_FILE`).
`rgb-hue-<n>` takes `<n>` matching `^(0|[1-9][0-9]{1,2})$`, a multiple of 10 and at most 350, and exits 1
before any step otherwise; it is `rgb-color-<n>-4`. `rgb-color-<h>-<s>` takes `<h>` the same way and `<s>` one of
1 to 4, else exits 1 before any step. It copies the config to the save while the save is absent (no config saves a zero
byte file), reads `enabled`, `linkBrightness`, `brightness`, `maxBrightness` and `saturation` from
`armada-rgb get` (a missing key exits 1) and runs `set --enabled <e> --link-brightness <l> --max-brightness <m>
--color <hex> --saturation <sat * L / 100> --brightness <b>` (`sat` is the `saturation` of the save file, Armada's own
value before casdap touched it, 100 when the save is empty or has none) with `enabled`, `linkBrightness`, `maxBrightness` and
`brightness` unchanged, so the stick is never brighter than Armada makes it: the applied level stays Armada's
`brightness`, or with `linkBrightness` the screen percentage times `maxBrightness` that the Armada watcher computes
(`armada-rgb` `controller.rs:35-44,78-86`, `display_brightness.rs:33-38`), and the `rgb.json` save restarts that watcher
through its path unit. `L` is 40, 60, 80, 100 for `s` 1 to 4 and Armada's `saturation` is scaled by it (rounded),
because Armada replaces the color's own saturation with `saturation` (`backend.rs:426-427`, `rgb_saturation.rs:4-15`). The color is `hsv(h, L / 100, 1)`
in integers: `lo = (255 (100 - L) + 50) / 100`, `span = 255 - lo`, sector `k = h / 60`, `f = h % 60`,
`up = lo + (span f + 30) / 60`, `down = lo + (span (60 - f) + 30) / 60`,
RGB `(255, up, lo)`, `(down, 255, lo)`, `(lo, 255, up)`, `(lo, down, 255)`, `(up, lo, 255)`, `(255, lo, down)` for
k 0 to 5 (level 4: 0 `FF0000`, 20 `FF5500`, 200 `00AAFF`, 280 `AA00FF`, 350 `FF002B`; `rgb-color-0-1` `FF9999`,
`rgb-color-200-2` `66CCFF`), 144 distinct colors. With Armada's `enabled` false
the color is saved and the lights stay off; a `maxBrightness` 0 with `linkBrightness` true is passed as is
and `set` rejects it. `rgb-off` saves the same way
and runs `off`. `rgb-sleep` and `rgb-wake` run `sleep` and `wake` and touch no file. `rgb-restore` with a
non empty save writes it over the config (temp file then rename), runs `apply` and removes the save; with
a zero byte save removes the config, runs `apply` and removes the save; without a save runs nothing and
exits 0. A failed `get`, `set`, `off` or `apply` exits 1 and keeps the save.

CPU and GPU floor: the `cpu-*` words write cpufreq and devfreq under `CASDAP_SYSFS_ROOT` (default
`/sys`) and save the values they replace in `CASDAP_CPU_BEFORE_FILE` (default `/run/casdap/cpu-before`,
one `path value` line per `policy*/scaling_max_freq`, `cpuN/online`, GPU `governor` and `max_freq`, temp file
then rename, written only while absent). `cpu-dap` saves, sets `online` 1 on cpu1 to cpu3,
`policy0/scaling_max_freq` 787200, `online` 0 on cpu4 to cpu7, GPU `governor` simple_ondemand and `max_freq`
305000000. `cpu-off` saves, sets `online` 1 on cpu1 to cpu3, `online` 0 on cpu4 to cpu7 and
`policy0/scaling_max_freq` to the saved `policy0` value, or `policy0/cpuinfo_max_freq` without one; the governor and
the GPU stay as they are. `cpu-resave` reads every saved path except `cpuN/online` again and replaces the value when
the read gives a number, else keeps the line (an offline policy refuses the read); without a file it does nothing.
`cpu-restore` sets every `online` to 1, writes every other saved line back and removes the file; without a
file it only brings the cores online. A refused write is logged, the remaining writes still run and the word
exits 1 with the save file kept. `org.armada.Power` is never touched.

Power: `casdap-power <verb>` (`/usr/libexec/casdap/`, runs as `armada`) has `screen-off`, `screen-on`,
`toggle`, `suspend`, `poweroff`, `idle-check` and `cpu-apply`; anything else exits 1. Markers live under
`CASDAP_RUNTIME_DIR` (default `$XDG_RUNTIME_DIR/casdap-power`): `off`, `wifi`, `idle-since`, `last-ms`, `lock`.
Settings come from `CASDAP_SETTINGS_FILE` (default `~/.config/casdap/settings.ini`, keys `wifiOffScreenOff`,
`suspendMinutes`, `rgb`). `screen-off`, `toggle` and `cpu-apply` exit 1 unless `systemctl --user is-active
gamescope-session-plus@casdap.service` prints `active`; `screen-on` runs in every state. `screen-off`, in
order: the `off` marker, `busctl --user call org.casdap.Player /org/casdap/Player org.casdap.Player
SetScreenOff b true` (a failed call is logged), `casdap-priv@backlight-off.service`,
`casdap-priv@rgb-sleep.service` when `rgb` is `accent` or `armada` (the default), `last-ms`,
`casdap-priv@cpu-off.service`, `nmcli radio wifi off`
with the `wifi` marker when `wifiOffScreenOff=true`
and the marker is absent, then `systemctl --user stop casdap-idle.timer` and `systemd-run --user
--unit=casdap-idle --on-active=60 --on-unit-inactive=60 --timer-property=AccuracySec=1min casdap-power
idle-check`. `screen-on`, in order: `casdap-priv@backlight-on.service`, removes the `off` marker, `SetScreenOff b
false` (session `active` only), `casdap-priv@rgb-wake.service` when `rgb` is `accent` or `armada`, `last-ms`,
then stops the timer, `nmcli radio wifi on` when the `wifi` marker
exists, then with the session `active` starts `cpu-restore` (only when the `off` marker was absent at the start and
`CASDAP_CPU_BEFORE_FILE` is present) and `cpu-dap`, in any other state `cpu-restore`. The stick lights change with
the backlight; `last-ms` follows both, the slower CPU and Wi-Fi steps follow it. `screen-off` and `screen-on` hold
`lock` (`flock -w 10`, `lock timeout` and exit 1 when it fails) for their whole run. `toggle` takes the lock
with `flock -n` and with it held logs `toggle during a transition, ignored` and exits 0; it does nothing within
2 s of `last-ms` (`CASDAP_POWER_DEBOUNCE_MS`, both directions), else runs `screen-on` with the `off` marker and
`screen-off` without it. `cpu-apply` starts `casdap-priv@cpu-resave.service`, then `cpu-off` with the `off`
marker, else `cpu-dap`. `idle-check` exits 0 outside the session; busy (`PlaybackStatus` contains `Playing`, or any
`org.bluez.MediaTransport1` under `/org/bluez` on the system bus has `State` `active`) or `suspendMinutes`
0 removes `idle-since`; idle writes it with the current unix time when absent and runs `systemctl suspend`
once it is `suspendMinutes` old. `suspend` and `poweroff` run `systemctl suspend` and `systemctl poweroff`.
Every verb is idempotent. `gamescope-session-plus@casdap.service.d/30-casdap-power.conf` wants
`casdap-powerd.service` and runs, each as its own `ExecStopPost=-` step so a failed one never skips the rest,
`systemctl start casdap-priv@rgb-restore.service`, `/usr/libexec/casdap/casdap-power screen-on`, then
`casdap-priv@hp-gain-normal.service`, `@hp-amp-lohifi` and `@spk-gain-102`, so every session stop (the switch to
Steam with KEEP IN BACKGROUND included; the overlay player never drives the lights) restores backlight, cpufreq, devfreq and Wi-Fi, stops the timer and
puts the mixer and the stick lights back to the values Steam had.

`casdap-powerd.service` (user unit, `PartOf=`, `BindsTo=` and `After=` the casdap session, `Restart=always`,
`RestartSec=1`, `StartLimitIntervalSec=0`) runs `casdap-powerd` under `systemd-inhibit
--what=handle-power-key:handle-suspend-key --who=casdap-powerd --mode=block`, so logind leaves the power key
alone while casdap runs. The daemon finds the node named `pm8941_pwrkey` (`CASDAP_POWERD_DEVICE_NAME`) through
`/sys/class/input/*/device/name`, reads it with `evtest` and reads `org.armada.Power` `Profile` with `busctl
--system get-property` every 5 s (`CASDAP_POWERD_POLL_SEC`). `KEY_POWER` released under 1000 ms
(`CASDAP_POWERD_LONG_MS`) runs `casdap-power toggle`; held 1000 ms, with the `off` marker it runs
`casdap-power screen-on` first, then `busctl --user call org.casdap.Player /org/casdap/Player
org.casdap.Player ShowPowerMenu`; a release without a press is dropped. The hold time is the `evtest` event time:
the hold timer runs from the press event, a line already waiting when it runs out is read first, and a release
1000 ms or more after its press is a hold even when it is read late. A line without a time, or a gap under 0 or
over 60000 ms, uses the read times. The pad ticks for 15 ms
(`CASDAP_POWERD_BUZZ_MS`, level `CASDAP_POWERD_BUZZ_LEVEL` 1.0) when a tap is classified, before the toggle runs,
and once when a hold passes 1000 ms (at the release when it is read late). A tap within `CASDAP_POWER_DEBOUNCE_MS` (2000) of `last-ms` or while
`flock -n` on the `lock` marker fails logs `tap during a transition or the debounce, no buzz` and still runs the
toggle, which ignores it. The buzz is `org.shadowblip.Output.ForceFeedback` `Rumble d <level>`, then `Rumble d 0`, on
every `/org/shadowblip/InputPlumber/CompositeDevice<N>` in a background subshell, a failed call skipped silently.
A changed `Profile` value runs `casdap-power cpu-apply`. `CASDAP_POWERD_DRY_RUN=1` logs `would:` lines (`would: buzz
<ms>` for the buzz). In the Steam session the daemon is
not running and Steam owns the key.

Pad target: `casdap-pad.service` (user unit, `BindsTo=` the casdap session) sets every InputPlumber
composite device to `xb360` plus `keyboard` through `SetTargetDevices`, so SDL players see an Xbox
pad. Leaving casdap for game mode or Plasma, Armada's `session-control` runs `controller-type apply`, which
sets the configured type again.
InputPlumber's own polkit rule grants its actions to group `wheel`, which `armada` is in.

## Input

The pad guide button (`BTN_MODE`) maps to `KeyF13` in `zz-casdap-ayaneo_pocket_micro2.yaml`, Armada's Pocket
MICRO 2 capability map plus that entry under the same `id`. InputPlumber reads maps sorted by file name and the
last map with an `id` wins, so this one replaces Armada's while merged; maps load when a composite device is
created, so a merge or unmerge needs an inputplumber restart. F13 reaches only the `InputPlumber Keyboard` node; Steam does not see it (the front Home key keeps Guide). F13
does not flow during stick calibration.

`casdap-hotkeyd` (user unit, `Upholds=` from `default.target.d`) reads that node with `evtest` and
classifies each press by the running session. Hold 1 s in game mode starts `casdap-switch.service`.
While a title runs the first hold is ignored and a
second hold within 3 s switches. Tap in game mode runs `systemctl --user is-active
casdap-overlay.service`; `active` means a background player exists and the tap calls
`busctl --user --timeout=2 call org.casdap.Player /org/casdap/Player org.casdap.Player ToggleOverlay`,
anything else logs `no background player`. The tap takes its time from the `evtest` event line: a tap
whose release came before the last `ToggleOverlay` call returned logs `tap during a toggle, dropped`, one
older than `CASDAP_HOTKEYD_TAP_MAX_AGE_MS` (default 300) logs `tap <n> ms old, dropped`, and each call
logs `ToggleOverlay rc=<rc> in <n> ms`. `Call failed: The name is not activatable` (the unit is
still starting) logs `overlay player starting`; a first `Call failed: Connection timed out` logs `overlay
timeout, waiting for the next tap`, a second in a row runs `casdap-intercept get` and restarts
`casdap-overlay.service` when it prints `3`, else logs `pad free`; any other result resets the count.
hotkeyd never starts the unit. Hold in casdap calls `busctl --user call
org.casdap.Player /org/casdap/Player org.casdap.Player ShowSwitchPrompt` (the player opens the switch
prompt, a second hold closes it); `Call failed: The name is not activatable` (no player on the bus) logs
`no player, switching back` and starts `casdap-switch-back.service`, any other failure is logged. Tap in
casdap logs `tap, play pause` and sends `PlayPause` to `org.mpris.MediaPlayer2.casdap` (`busctl --user --timeout=2
call`, also for `Next` and `Previous`). Guide chord: at start hotkeyd creates `CASDAP_GUIDE_DIR` (default
`$XDG_RUNTIME_DIR/casdap-guide`) and writes `0` to `down` and `chord`; every guide press writes its evdev event
time (ms since the epoch) to `down`, and `0` goes back when the press resolves. The player writes the `down` value
it took for a rotate chord to `chord`. With the screen on in casdap a release under `CASDAP_HOTKEYD_CHORD_MS`
(500) after its press waits `CASDAP_HOTKEYD_CHORD_GRACE_MS` (40) more, a later one resolves at once; a `chord`
equal to the press time then logs `chord, tap dropped` and sends nothing. A hold whose press time is in `chord`
logs `chord, hold dropped`, no tick, no switch prompt. Pocket and game mode never read `chord`. With the screen off
(`$CASDAP_RUNTIME_DIR/off`, default `$XDG_RUNTIME_DIR/casdap-power/off`) the guide button is the transport: a
hold of 500 ms (`CASDAP_HOTKEYD_POCKET_LONG_MS`) sends `Previous`, a tap
opens a 350 ms window (`CASDAP_HOTKEYD_DOUBLE_MS`) in which a second tap sends `Next` for both, else `PlayPause`
goes out when the window ends; a hold inside the window sends the pending `PlayPause` first. The power key wakes
the screen. The window runs on a
read timeout. Every guide press is classified by its `evtest` event time: the hold timer runs from the press
event, and when the hold timer, the double tap window or the chord grace runs out with a line already waiting, that
line is read first. A release at or past the hold threshold after its press is a hold even when it is read late,
a second press under 350 ms after the first release is a double tap, and the chord
threshold compares the same two times. A line without a time, or a gap under 0 or over 60000 ms, uses the read
times for both sides. In Plasma a hold switches; in any other session it
logs. Every hold that acts (switch, switch prompt, `Previous`) ticks the pad for 15 ms at level 1.0 the
moment the hold threshold (1 s, 500 ms with the screen off) passes, while the button is still down (at the release
when a stall delays the read past it); the first hold while a title runs and a
hold in an unknown session do not. A pocket double tap ticks 15 ms, 50 ms off, 15 ms as it sends `Next`;
taps never tick. The tick runs `org.shadowblip.Output.ForceFeedback` `Rumble d <level>`, then `Rumble d 0`,
on every `/org/shadowblip/InputPlumber/CompositeDevice<N>` in a background subshell, a failed call is
skipped silently; `CASDAP_HOTKEYD_BUZZ_LEVEL` (default 1.0), `CASDAP_HOTKEYD_BUZZ_MS` (15) and
`CASDAP_HOTKEYD_BUZZ_DOUBLE_MS` (`15 50 15`, on and off in turn) override it, dry run logs `would: buzz <ms>`. hotkeyd opens no volume node: in casdap the player's `VolumeKeys`
owns the volume keys, in every other session they are left to Steam. `CASDAP_HOTKEYD_DRY_RUN=1` (runtime drop-in under
`~/.config/systemd/user/`) logs `would:` lines instead of acting.

`/usr/libexec/casdap/casdap-intercept <0|3|get>` writes InputPlumber's `InterceptMode` on every
`/org/shadowblip/InputPlumber/CompositeDevice<N>` from `busctl --system tree --list`. Mode 3 takes the
gamepad away from Steam and delivers it as `org.shadowblip.Input.DBusDevice.InputEvent` on the
`devices/target/dbus*` paths while the keyboard target, and with it `KEY_F13`, keeps flowing; 0 gives it
back. `get` prints `3` when any device reads 3, else `0`. Runs as `armada`, no polkit rule needed.

Touch: the ST7123 panel (`event6`, axes 0..639 and 0..959 on the native panel) reaches the player through
gamescope touch mode 4 (passthrough), the shared launcher's default. The player takes taps, vertical drags and
flings, no swipes and no long press (`touch` in `settings.ini`). Touch is ignored with the screen off (the panel is
not a wake source) and in overlay mode.

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
`/usr/libexec/casdap/casdap-keep check` (`CASDAP_KEEP_BIN`) exits 0, that is unless KEEP IN BACKGROUND wrote the
marker this boot (`casdap-bt --print-keep` prints `keep` or `drop`). `casdap-bt --disconnect-sources` disconnects
every connected A2DP source over the system bus with a 2 s call timeout and exits (1 when bluez is absent or a
disconnect fails); overlay CLOSE PLAYER runs it.

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
(`CASDAP_SYSTEMCTL_BIN`) through a tracked `QProcess`, unless `BluezModel.transportActive`: then `pending`
reads true, the deferral is logged and the restart runs when `transportActive` goes false. An
unchanged file runs nothing. `restarting` is true while the restart runs, `restartFinished(ok)` ends it;
a failed restart leaves `pending` true. The phone picks among the offered codecs when it connects.
`BluezModel.transportState` reads the selected device's `MediaTransport1.State` as `idle`, `pending` or `active`,
and `none` with no transport; `transportActive` is true only for `active`. `BluezModel.volume` is `MediaTransport1.Volume` (0 to 127) in percent; `volumeChanged` follows every read; `phoneVolumeChanged`, and with it the volume overlay, follows a changed value only, not the first read at start or after a device connects. `restartForNet(reason)` restarts
WirePlumber for the release net: a restart already running is joined (its `restartFinished` counts, also when
`apply` replaces it), and it refuses with any transport present (returns false). A net restart that succeeds
clears `pending` unless a deferred write came during it; a failed one leaves `pending` as it was. When it ends with
`pending` true and no transport, the codec restart runs at once.
`gamescope-session-plus@casdap.service.d/10-casdap-bt.conf` runs `casdap-codec clear` from `ExecStopPost`:
with no fragment it runs nothing; while `casdap-keep check` (`CASDAP_KEEP_BIN`) exits 0 it keeps the fragment;
otherwise it removes the fragment and runs `systemctl --user restart wireplumber`. Overlay CLOSE PLAYER runs it
after deleting the marker. `CASDAP_WP_CONF_DIR` and `CASDAP_SYSTEMCTL_BIN` override the directory and the binary.

## Audio

`pipewire.conf.d/50-casdap-rates.conf` sets `default.clock.allowed-rates` to 44100, 48000, 88200, 96000,
176400, 192000, 352800 and 384000 for the whole daemon while casdap is merged; `default.clock.rate` stays 48000. The
built in sinks open at most 192000 and resample above it, the Apple USB-C dongle opens 48000 only, and the FiiO
SNOWSKY TINY B opens every listed rate. With only
one stream active, the graph and the PCM switch to that stream's rate when the sink starts. A concurrent
48 kHz stream keeps the graph at 48000 and the other stream is resampled. PipeWire reads the fragment at
start.

`wireplumber.conf.d/50-casdap-soft-mixer.conf` sets `api.alsa.soft-mixer = true` on every device whose
`device.name` matches `~alsa_card.platform-sound.*` through `monitor.alsa.rules`, so the sink volume is applied in the PipeWire
sink node and the hardware mixer stays at the UCM boot values (`SpkrLeft PA Volume` 12, `HPHL Volume` and
`HPHR Volume` 20). WirePlumber reads the fragment at start; the user audio stack restarts once after the
merge.

Manual mixer controls are the `casdap-priv` words `hp-gain-low`, `hp-gain-normal`, `hp-gain-high` (`HPHL
Volume` and `HPHR Volume` 8, 20, 24: -18, 0, +6 dB), `hp-amp-lohifi`, `hp-amp-ulp`, `hp-amp-hifi` (`RX HPH
Mode` with `RX_HPH PWR Mode`) and `spk-gain-84` to `spk-gain-108` (`WSA_RX0 Digital Volume` and `WSA_RX1
Digital Volume`; 84 is 0 dB, 1 dB per step, 102 the UCM value; the value takes effect when playback
restarts), every write `amixer -D hw:0 cset`. The session stop puts `hp-gain-normal`, `hp-amp-lohifi` and
`spk-gain-102` back through `30-casdap-power.conf`.

`AudioInfo.graphRate` is the running graph rate: `pw-dump` (`CASDAP_PW_DUMP_BIN`, default `pw-dump` on PATH)
gives the default sink node's `clock.rate`, else its `Format` rate, else the `settings` metadata `clock.rate`;
without the binary it is the `hw_params` rate. `hw_params` is the first open `sub0/hw_params` under
`/proc/asound/card*/pcm*p/` (`CASDAP_ASOUND_DIR` replaces `/proc/asound`) whose `status` reads `state: RUNNING`,
else the first open one, else none (speaker and headphones are PCMs of card 0, a USB DAC its own card).
`pcmFormat` is read every 2 s while `active` (in DAP mode from start while the screen is on, in overlay mode while the
overlay is mapped and the screen is on), and each read updates `graphRate` while no `pw-dump` rate is held;
`refreshGraphRate()` runs `pw-dump` and falls back to the latest `hw_params` rate when `pw-dump` is absent, fails or
is killed after its timeout. The settings page calls `refresh()` when it opens, so SAMPLE RATE and the BIT PERFECT
status read the live `hw_params`.

`OutputControl` (context property, DAP mode only, null under `--overlay`) applies `Settings.output`
(`auto`, `speaker`, `headphones`, `usb`) at start and on every change. Every apply reads the jack and the sink
list first (`refresh()`), then resolves `activeOutput` (`usb`, `headphones`, `speaker`) by the priority USB,
headphones, speaker: `auto` is the USB sink when one is listed, else the headphones sink while the jack is in,
else the speaker; `speaker` and `headphones` are that sink; `usb` is the USB sink when listed, else as `auto`.
The resolved sink is set with `pactl set-default-sink <node name>`; a resolved sink missing from the list fails the
apply. The sink list is `pactl list sinks short` (`CASDAP_PACTL_BIN`), name column: `usbSink` is the first
name starting with `alsa_output.usb-` (empty when none), `usbPresent` follows it, the built in sinks are
`alsa_output.platform-sound.HiFi__Speaker__sink` and `HiFi__Headphones__sink`. `jackPresent` reads the
`Headphone Jack` block of `amixer -c0 contents` (`values=on`; the CARD control has no cget name). Both are
polled every 2 s for the whole DAP session (the screen off state included) and read again inside every apply; a jack edge or the USB sink appearing or
disappearing reapplies the current output. A `refresh()` during a read or an apply, the poll's included, queues one
reread that runs when it finishes, so an edge after the apply's own read is applied then. The USB DAC seen on the device
(Apple USB-C dongle) takes 48 kHz only, so 44.1 sources are resampled by the graph while it is the output.
`busy` is true while a command chain runs, `applied(output, ok)` ends it.
`setHeadphone(gain, amp)` takes `Settings.hpGain` and `Settings.hpAmp` at start and on every change and
starts `casdap-priv@hp-gain-<gain>.service` then `casdap-priv@hp-amp-<amp>.service` only while `activeOutput`
is `headphones`; otherwise the words are skipped and logged. Every apply that resolves to the headphones runs
the two words again, so plugging in with `auto` or unplugging a USB DAC with the jack in reruns them.
`setSpkGain(step)` starts `casdap-priv@spk-gain-<step>.service` at start and on every
change. Every word emits `applied(word, ok)`.
`CASDAP_WPCTL_BIN`, `CASDAP_PACTL_BIN`, `CASDAP_AMIXER_BIN` and `CASDAP_SYSTEMCTL_BIN` replace the
binaries.

Output levels: `Settings.outputLevel` is `off` (default), `bitperfect` (BIT PERFECT) or `exclusive` (EXCLUSIVE).
`OutputMode` (context property `OutputMode`, DAP mode only: not under `--overlay`, `--screenshot`, `--scan-only` or
in the tests) is created before `state.json` is restored and decides the chain of every `Player` build. A level is
effective only while `OutputControl.activeOutput` is `usb` and the DAC card is known; on the speaker, the
headphones and in overlay mode every build is the `off` chain. Chains: `off` is the EQ bin as `audio-filter` into
`CASDAP_AUDIO_SINK` (default `pulsesink`), with `audioresample ! capsfilter caps=audio/x-raw,rate=<r>` at the end
of the filter when `sampleRate` is a rate the active output lists; BIT PERFECT is `audioconvert ! capsfilter
caps=audio/x-raw,format=F32LE` into `pulsesink volume=1.0 mute=false` (`CASDAP_AUDIO_SINK` replaces the sink) with
the software EQ bypassed (the profiles stay and apply again when the level leaves; on the FiiO TINY B the
`eqUsb` profile runs in the DAC, DAC EQ below); EXCLUSIVE is no filter into
`audioconvert dithering=none noise-shaping=none ! capsfilter caps=audio/x-raw,format=<f> ! alsasink
device=hw:CARD=<id>,DEV=0`, `<f>` `S32LE` when `card<N>/stream0` lists `S32_LE`, else `S24LE` for `S24_3LE`, else
`S16LE` (`CASDAP_EXCLUSIVE_SINK` replaces it, `%1` the card id). `sampleRate` is not applied while a level is
effective. Its values are the active output's rates: a USB card's `stream0` rates within the PipeWire list above
(FiiO all eight, Apple dongle 48000), the built in outputs 44100, 48000, 88200, 96000, 176400, 192000. With a level
set the DAP start restores the queue parked; a handoff `playing` resumes once `OutputControl` has resolved once and
`BluezModel.ready` is true, each bounded at 3000 ms; it stays paused while a phone transport is active and is dropped
when the play state changes before it. With `off` nothing reserves, nothing parks and the volume keys
go through Pulse.

EXCLUSIVE takes the DAC out of PipeWire through `org.freedesktop.ReserveDevice1.Audio<N>` (the card's
`api.dbus.ReserveDevice1`), owned by `DeviceReservation` on its own thread and session bus connection
(`CASDAP_RESERVE_BUS` replaces the bus address) at priority 10 as `casdap`, `hw:CARD=<id>`. The acquire runs at a
PLAYING build only, never at start or for a paused build: the card from the USB sink (`alsa.card`,
`/proc/asound/card<N>/id`), a playback dB element with at least 40 dB of range (else `exclusive (no volume)` until
the card changes), the sink unmuted and at the element step's dB within 0.05 dB (else `exclusive (volume)` until
the next key press), `RequestRelease(10)` on the owner, `RequestName`, then `pcm0p/sub0/status` `closed` within
1000 ms, the whole acquire within 2000 ms; then the build. An acquire waits up to 3000 ms for a running codec
restart. A refusal, a timeout or an `alsasink` open error with the card present is `exclusive (unavailable)` (a
refusal until the card, the level or OUTPUT changes, a timeout or open error until the next play after 60 s). While
blocked the player builds the BIT PERFECT chain.
Every `RequestRelease` is answered within 500 ms: priority 10 or lower false (WirePlumber's -20 at every card
event), true once the card is gone or after a disconnect event; above 10 the player parks and hands the DAC over.
The name is released: 5 s after a pause (the player parks: position kept, no pipeline, Paused), at once on stop and
at the end of the queue, when a phone connects, when `BluezModel.ready` turns false, on a level or OUTPUT change,
on unplug, before a session switch, and on SIGTERM or any `quit()` (after `saveExit`, joined within 1000 ms). Sleep
keeps the name with the pipeline parked and the mixer closed, and releases after resume. After a release the player
waits for the DAC sink, sets it default, moves every `bluez_input` stream to it and writes the owned level;
`OutputControl` stays held (`usbPresent` and `usb` kept, no USB default set) until a sink read started after the
default set lists the DAC. A DAC sink still missing after 1500 ms restarts WirePlumber once (`casdap output: dac
sink missing after release, restarting wireplumber`; deferred while a phone transport exists, retried when it ends)
and waits 3000 ms more; still missing, the card is `unavailable` until the next disconnect event (`casdap output:
dac sink still missing, unavailable until the next disconnect`), the toast reads `usb dac unavailable` and the
player stays paused. A session switch skips the restart and waits for the DAC sink up to 1000 ms (switch prompt
above for the bounds and the rebuild after a failed start).

While a phone is connected (`BluezModel.connected`) EXCLUSIVE does not hold: a connect while held releases, local
playback continues on the BIT PERFECT chain once the DAC sink is back, and the row reads `exclusive (phone)` until
the next acquire; the next PLAYING build after the phone disconnects acquires. The time between `connected` and the
first phone audio is UNVERIFIED (device check BP, P17).

Volume with a level effective: the keys write the DAC's playback dB element (FiiO `PCM`) through `AlsaVolume`
(libasound `hw:CARD=<id>`, one write per 250 ms, the last press written last), never the sink volume. The toast
shows `100 * 10^(dB / 60)` percent; a press moves that by `volumeStep`, back to the nearest 0.5 dB step, at least
one step, within 0..120 (-60..0 dB), so the floor is 10 % and the keys never reach silence. An up press also
unmutes the DAC sink. The level is owned per card id for the session: a return of the DAC sink (replug, release,
WirePlumber restart, resume) writes it and rewrites a foreign value for 1500 ms; DAP start, a level row change and
USB becoming active with no owned level adopt the current step, and entering a level unmutes the sink once. 2 s
after the last press while paused and not held the sink volume is set to the step's dB and the element written
again, so WirePlumber stores the level. A sink volume under the step (a software part left by a Steam volume
change) reads `volume` until the next key press, which is why `volume` can show right after game mode. The mixer closes for sleep and opens again at resume.

After a crash or SIGKILL the name drops with the process and WirePlumber takes the card back (about 250 ms);
nothing else needs a reset. The DAC then has WirePlumber's stored level: under BIT PERFECT the level of the last
store sync, under EXCLUSIVE the level of the last release. A replug reads as the same card (no static identity
tells it apart): a disconnect event (the mixer handle's poll error or `-ENODEV`, an `alsasink` error with the card
gone, the card id gone on the 2 s poll, a libpulse card remove while not held) bumps a generation per card id, and
what appears after it is the replugged card.

`OutputMode.levelStatus` reads `off`; `usb only` on the speaker and the headphones; `on` or `exclusive` when every
condition holds or nothing plays, else the first broken one with its suffix, in this order: `pending` (the built
chain is not the level's, or a release runs), `volume`, `resampled` (`AudioInfo.hwRate` differs from the source
rate), `converted` (a source other than integer up to 24 bit or F32, up to 32 bit under EXCLUSIVE, or a channel
count change), `mixed` (BIT PERFECT next to an active phone transport), then under EXCLUSIVE while not held
`phone`, `released`, `unavailable`, `no volume`, `volume`. Integer samples reach the DAC unchanged; float decoders
(AAC, MP3) are rounded once to the DAC's integer format.

DAC EQ (FiiO SNOWSKY TINY B `2972:0149`, DAP mode only, UNVERIFIED on the device until device check DQ): `DacEq`
runs the `eqUsb` profile on the DAC's PEQ while a level is effective with the card known, and forces the DAC to
preset 240 (bypass) whenever no level holds it. BIT PERFECT and EXCLUSIVE send the stream unchanged. On the FiiO
TINY B the USB EQ profile runs in the DAC; set it to OFF for a flat DAC. With a parsing profile `DacEq` selects slot
162, renames it `CASDAP` and writes the ten bands (gain clamped to -12..+12 dB, 20 to 20000 Hz, Q 0.25 to 8; a
gain free type sends 0 dB; an OFF band is a 0 dB PEAK); OFF or MISSING selects 240. The preamp is written 0 dB:
casdap's preamp is not applied on the DAC and band gains are not cut. Edits coalesce 150 ms and write only the bands
that differ. Before the first rename the slot is backed up to `~/.config/casdap/fiio-2972-0149-slot162-<n>.txt`
(`$XDG_CONFIG_HOME/casdap`, numbered, skipped when equal to the newest); `casdap-fiio restore-slot [<file>]` writes
the newest back from game mode. Never two EQs: the software EQ is flat on USB while the DAC is held (from the DAP
start scan or the start of any batch until 240 is confirmed or the card is gone), the speaker and the headphones
keep their profiles. A failed batch tries one bypass and keeps the software flat on USB. A phone stream played to
the FiiO under a level passes the DAC EQ. A preset chosen in the FiiO app is turned off when the DAC meets the PM2;
its slot is kept. Every batch takes `/run/user/<uid>/casdap-fiio.lock` after interface 0 has no driver, one frame in
flight; a wedged IN pipe (`overflow`) is reset once (`USBDEVFS_RESET`) with the player stopped or off USB, not
EXCLUSIVE held and the PCM closed. A switch runs the bypass beside the output release (ready after both or 500 ms),
quit runs it after the release and joins the worker up to 500 ms. A DAC unplugged while a level held it plays
`CASDAP` on another host until changed there. The CLI never resets the USB device: a wedge outside DAP stays until the
next DAP start or a replug. Journal lines: `casdap dac eq: start level <l> effective <e> output <o> dacs <n>` (before
the first `casdap eq: apply`), `force`, `take`, `update`, `bypass acked`, `failed <reason>`, `reset`, `driver <bound|unbound> after reset`, `effective, no card`.

USB DAC autosuspend: `/usr/libexec/casdap/casdap-usb-audio <usb device path>` handles a USB device with an
audio class interface and a HID interface bound to `usbhid` whose report descriptor declares no Generic Desktop
usage page or usage (a descriptor that cannot be read or walked counts as Generic Desktop). Each such HID
interface is written to `/sys/bus/usb/drivers/usbhid/unbind`, then the device gets `power/control` `auto` and
`power/autosuspend_delay_ms` 2000; each write is best effort and a failure is logged. The DAC suspends once its
PCM closes, and its HID media buttons stop working. Gamepads with audio (DualSense, DualShock 4), keyboards,
mice and audio only devices are left alone, and a device with no HID interface bound writes nothing.
`70-casdap-usb-audio.rules` runs it on a `usb_device` add or bind and on the parent of a `usbhid` interface
bind; `--scan` walks every device under `/sys/bus/usb/devices`, from `casdap-usb-audio.service` at boot
(`Upholds=` from `multi-user.target.d`, see Guard) and from `device-apply`. `CASDAP_SYS_ROOT` replaces `/sys`. Journal lines:
`casdap-usb-audio: <dev> <vid>:<pid> hid <intf> unbound` (or `unbind failed`), `<dev> autosuspend on`,
`<dev> skipped, generic desktop hid`, `<dev> power/control write failed`. Undo as root:
`echo <intf> > /sys/bus/usb/drivers/usbhid/bind`.
The same rules file gives the usbfs node of the FiiO SNOWSKY TINY B (`2972:0149`) group `audio`, mode 0660
(`crw-rw---- root audio`), on every event but `remove`, tagged for systemd; `casdap-fiio` opens it as the user. Any
process in `audio` can issue usbfs requests on it, a reset included; accepted on a one user device. The mode stays
after the sysext is removed until the DAC is plugged in again. A second rule line adds
`SYSTEMD_USER_WANTS=casdap-fiio-reset.service` on the FiiO `usb_device` add: the user oneshot runs `casdap-fiio
reset` on every plug in (waiting up to 5000 ms for the `usbhid` unbind; UNVERIFIED until check DQ-wire, Q5), only
for a user manager not root and in group `audio` (`ConditionUser=!root`, `ConditionGroup=audio`), and forces 240 outside DAP, in the overlay
and plain Steam too; a fast replug can merge into a running oneshot, and the next DAP or Steam start forces 240.
`gamescope-session-plus@casdap.service.d/05-casdap-fiio.conf` runs `ExecStopPost=-/usr/libexec/casdap/casdap-fiio
reset --no-wait --deadline 3500 --stamp` on every casdap stop (switch, crash, SIGKILL), before the
`30-casdap-power.conf` lines; `--stamp` writes `/run/user/<uid>/casdap-fiio.reset` when every DAC was done.
`gamescope-session-plus@steam.service.d/40-casdap-fiio.conf` runs `ExecStartPre=-/usr/libexec/casdap/casdap-fiio
reset --no-wait --deadline 1000 --skip-stamped` before every Steam start (boot coldplug included, Q7 open), skipped while that
stamp is under 10 s old. The casdap stop reset holds the switch back to Steam up to 3.5 s; the Steam start reset
holds Steam up to 1 s; an interface still bound to `usbhid` is skipped and logged.

EQ: `playbin` `audio-filter` is `audioconvert ! audio/x-raw,format=F32LE ! volume name=casdap-preamp !
audioiirfilter name=casdap-b0 ! ... ! audioiirfilter name=casdap-b9 ! audioconvert`, ten RBJ biquads with no
queue, built once with the pipeline at off (every band `b = [1]`, `a = [1]`, preamp volume 1.0);
`CASDAP_EQ_DISABLE=1` leaves it out. `Player::applyEq` only writes the profile into a snapshot under a mutex.
One pad probe on the sink pad of `casdap-preamp` sets the preamp `volume` (10^(dB/20)) and the coefficients of
every marked band (`b` then `a`, computed in double at the stream rate) together on the streaming thread before
the next buffer enters the bin, so no buffer runs a mixed profile; an apply while paused lands at the first
buffer after resume. Only bands whose coefficients change are set (every set zeroes that band's history). A
band is unity when OFF and when a PEAK or shelf sits at 0 dB; the bands stay unity until the first caps give a
rate. A caps event, `rate-changed` and a relink of the PW chain mark every band for the next buffer; a band at
or above 0.49 of the rate is unity (HIGH PASS and BAND PASS mute), logged `casdap eq: band <n> at <f> Hz
above 0.49 of <rate>, unity` (or `mute`) once until that band changes. Position and the gapless chain are kept. Local playback only: the phone stream and
the Steam stream never pass it. `EqControl` (context property
`Eq`, both modes, a sample CUSTOM profile under `--screenshot`) keeps one profile per
output in `eqHeadphones`, `eqUsb`, `eqSpeaker` (`off`, an absolute file path or `custom`, default `off`) and
emits `profileApplied` for the key of the active output on every output change, every change of that
key or its store, and a relist that finds a missing file again; a change to another output's key or store is
written only. The active output is `OutputControl.activeOutput` in DAP mode; under `--overlay` `pactl
get-default-sink` is polled every 2 s (`CASDAP_PACTL_BIN`, async, a run still going at the next tick is killed)
and a changed mapping is applied. At start the output of `pactl get-default-sink` (`alsa_output.usb-*` usb, the
headphones sink headphones, anything else speaker, `speaker` when the read fails) is applied before `state.json`
is restored. Profiles are AutoEQ
`ParametricEQ.txt` and Equalizer APO files (`Preamp: <g> dB`, `Filter[ <n>]: ON|OFF <code> Fc <f> Hz[ Gain <g>
dB][ Q <q>]`, case insensitive). Codes: `PK` peak (Gain and Q required); `LSC`, `LS` low shelf and `HSC`, `HS`
high shelf (Gain required, Q 0.71 when absent); `LP`, `LPQ` low pass, `HP`, `HPQ` high pass, `BP` band pass (the
RBJ constant 0 dB peak) (Q 0.71 when absent, a Gain is read and kept but not used); `AP` all pass (Q required, a
Gain kept). Skipped and logged, taking no slot: `NO`, `Modal`, `PEQ` and `BW Oct` forms, the `LSC <x> dB` slope
and `LS 6dB`, `HS 12dB` corner forms, `IIR`, any other code, and a line missing a required field. An OFF line
takes a slot (as written when it parses, else the flat default band) only with a mapped code. The first 10
filters are taken, fewer leave the rest OFF, a file with no valid filter is left out; clamps gain -24 to +12 dB,
freq 20 to 20000 Hz, Q 0.1 to 10, preamp -24 to 0 dB; without a `Preamp:` line the preamp is minus the sum over
ON bands of the positive PEAK and shelf gains plus `max(0, 20 log10(Q))` for LOW PASS and HIGH PASS (at least
-24; BAND PASS and ALL PASS add nothing). Sources are `~/.config/casdap/eq/*.txt`, then `<root>/EQ/*.txt` per
`Settings.roots`, each
folder sorted by name case folded, named by the file name without `.txt` and a trailing ` ParametricEQ` in
upper case, listed at start, when
`Library.scanning` goes false, when the roots change and when the EQ page opens; a saved path that is gone or
fails to parse reads `<NAME> (MISSING)` (the map carries the bare `name` and `missing`) and runs flat. CUSTOM lives in
`~/.config/casdap/eq-<output>.ini` (`[General]` `preamp`,
`[band1]` to `[band10]` with `type` `off`, `peak`, `lowshelf`, `highshelf`, `bandpass`, `lowpass`,
`highpass`, `allpass` (any other type reads the store flat), `freqIndex` into the 61 point grid
(0 is 20 Hz, 1 to 59 `1000 * 2^((i - 34)/6)`, 60 is 20000 Hz) or `freq` for a file value, `gain`, `q`),
written 500 ms after the last edit and on quit, temp file then rename; a missing or unreadable store is ten PEAK
bands at grid 4, 10, 16, 22, 28, 34, 40, 46, 52, 58, gain 0, Q 1.4. `cycleProfile` steps OFF, the listed files,
CUSTOM (wrapping); `stepPreamp` and `stepGain` move 0.1 dB (1 dB with `coarse`), `stepQ` 0.1 (1.0 with
`coarse`, clamped 0.1 to 10), each snapping to its step grid and rounded to one decimal (gain, preamp) or two (Q),
`stepFrequency` one grid point (1/6 octave, 6 points with `coarse`), `stepBand` passes `coarse` on, `stepType`
cycles OFF, PEAK, LOW SHELF, HIGH SHELF, BAND PASS, LOW PASS, HIGH PASS, ALL PASS and keeps the stored gain,
`stepGain` does nothing on an ON band of a gain free type. An edit while the key is a file copies the parsed
file into the store and sets the key to `custom` first; the `.txt` is never written. `CASDAP_EQ_DIR` replaces
`~/.config/casdap` for both the `eq/` folder and the stores.

`RgbControl` (context property, DAP mode only, null under `--overlay`) runs `armada-rgb supported`
(`/usr/libexec/armada/armada-rgb-env`, `CASDAP_RGB_BIN` overrides) once at start; exit 0 sets `supported`,
`probed` goes true either way. Supported, it starts `casdap-priv@rgb-restore.service` and then the word for
`Settings.rgb`: `rgb-off` for `off`, `rgb-hue-<Accent.hue>` for `accent` (`rgb-color-<Accent.hue>-<Accent.level>`
while the accent comes from art), nothing more for `armada`. A changed
`Settings.rgb` starts the word for the new mode at once (`rgb-restore` for `armada`). A changed `Accent.hue` or
`Accent.level` (`setColor(hue, level)`, level 0 for the slider) while the mode is `accent` arms a 500 ms single shot
timer, restarted by every further change; when it fires the color word starts unless it names the color last sent
(`rgb-hue-<h>` and `rgb-color-<h>-4` count as one; `rgb-restore` and `rgb-off` clear it, a failed color word clears it too). A color word waiting behind a running word is replaced by a newer one. While
`PowerBridge.screenOff` (`setScreenOff`) is true no color word starts; on screen on the latest color starts at once
when it differs from the last sent. `setSaver(bool)` (property `saver`, called by `Main.qml` on every
`Saver.shown` change) puts the lights to sleep while the saver is up in `accent` and `armada`: true starts
`rgb-sleep`, false starts `rgb-wake` and then, in `accent`, the latest hue when it differs from the last sent
(`rgb-sleep` and `rgb-wake` keep it); no hue word starts while the saver is up. A screen off while the lights
sleep leaves the wake to `casdap-power screen-on`, so a saver closed with the screen off starts no word; a screen
on with the saver still up starts `rgb-sleep` again. An unknown mode is logged and runs nothing. Words run one at a time through
`systemctl start` (`CASDAP_SYSTEMCTL_BIN`), `busy` is true while one runs or waits, `applied(word, ok)` ends each.
Unsupported, nothing runs and the settings RGB row reads `UNSUPPORTED`.

`AccentControl` (context property `Accent`, both modes and `--screenshot`) holds `sliderHue` (from
`Settings.accentHue`), `fromArt` (from `Settings.accentFromArt`), `artHue` (-1 for none), `artLevel`, `hue`, `level`
(the art saturation level 1 to 4, 0 on the slider), `color`,
`brightColor`, `colorFor(h)` and `brightColorFor(h)`. `hue` is `artHue` while `fromArt` is true and `artHue` is
not -1, else `sliderHue`. `artHue` is -1 while `fromArt` is false, while `Player.current` has no `artPath` and
while `BluezModel.transportActive`. On the slider `color` is `hsv(hue, s, 0.98)` with `s` the largest of 0.83, 0.82, ... 0.40
whose WCAG luminance is at least 0.149 (`#FA2A4D` at 350), channels `round(255 c)`; `brightColor` is
`hsv(hue, 0.84 s, 1.0)` (`#FF4D6B` at 350). From art `color` is the picked hue with the cluster's saturation and
value in whole percent: the value rises to at most 0.98 until the luminance reaches 0.149, then the saturation drops
toward 0.40 while it is still under; `brightColor` is `hsv(hue, 0.84 s, 1.0)` of that saturation. The art pick
(`ArtColor`) reads the file (a path or a `file://` url) with `QImageReader` scaled to 48 x 48 on a one thread pool.
Neutral pixels (saturation under 0.18, value under 0.12, or value over 0.97 with saturation under 0.25) are left out;
fewer than 3 percent colored pixels (69 or fewer of 2304) or an unreadable file is -1. The rest is clustered by k means (k 5,
points `(cos h, sin h, s, v)`, each weighted by its saturation, seeds the most saturated pixel then the farthest
saturation weighted pixel at least 0.2 away, 10 passes), clusters closer than 0.2 merge, and the cluster with the top
`share * mean saturation * (0.5 + 0.5 max(0, 1 - |mean value - 0.6| / 0.6))` wins (a tie to the earlier seed). The
result is the circular mean hue 0 to 359, the level from the mean saturation (under 0.35 1, under 0.55 2, under 0.75
3, else 4) and the mean color. A new art path keeps the previous
pick until its result arrives. Results are cached by path (512 entries, cleared when full and on
`Library.albumsChanged`); a result for a path that is no longer the current one is cached and not applied. The
journal gets `casdap accent: art hue <h> s <level> for <path>` or `casdap accent: no art hue for <path>` when a result is
applied for a path other than the last logged one, and `casdap accent: hue <h> s <level> (art|slider)` on every change of
`hue` or `level` (the slider logs `s 4`).

aptX: `build/aptx.sh <cache> <dist>` downloads `libfreeaptx` (RPM Fusion free releases/44) and
`pipewire-codec-aptx-1.6.7-1.fc44` (updates/44), both aarch64 and sha256 pinned, extracts
`usr/lib64/libfreeaptx.so.0*` and `usr/lib64/spa-0.2/bluez5/libspa-codec-bluez5-aptx.so` into the dist and
puts the LGPL text under `usr/share/licenses/casdap/`. The sysext merge places the codec next to the image's
bluez5 codecs; the user audio stack has to restart once after the merge.

## Guard

`casdap-guard.service` runs at boot (`Upholds=` from `multi-user.target.d`) and after every dev loop.
`systemd-sysext.service` merges after systemd has loaded its units and reloads the manager only for an
extension whose `extension-release` sets `EXTENSION_RELOAD_MANAGER=1`; `extension-release.casdap` does, so
the drop-in applies on that boot.
It resolves the booted deployment from `ostree=` on the kernel command line and compares each file
in `shadow-base.sha256`; a mismatch logs one warning per file and lists the path in
`/run/casdap/drift`. Without a resolvable deployment it compares `IMAGE_VERSION` instead. When
`session-control list-sessions` does not print `casdap` (an image without drop-in support) it warns and lists
`session-control` there too.

## Rebuild per Armada release

Every Armada release (new digest, `VERSION_ID` or pipewire minor) needs a casdap rebuilt against
it: `just base && just build`. A casdap built against an older image hides later changes to
Armada's Pocket MICRO 2 capability map while merged (its `zz-casdap-` copy wins the `id`); the guard warns about it.

## Known limits

Symptoms of a sysext left unmerged after a bootc upgrade to another `VERSION_ID`:
`systemd-sysext status` lists nothing or marks casdap incompatible, the pad guide button opens the
Steam menu (InputPlumber's default Guide translation) and a hold does nothing, `casdap-switch.service` is unknown to systemd, no `casdap-*`
user units. Fix: install the asset for the new `VERSION_ID`.

The short session tracker `/tmp/chimeraos-short-session-tracker` is shared by every
gamescope-session-plus client. Short casdap sessions count against Steam's recovery (5 in 60 s each
rewrite the drop-in to desktop); a clean Steam shutdown on the way into casdap clears it.

## Install contract

Release asset `casdap-<ver>-armada<VERSION_ID>-arm64.raw`, built against one image digest, both in
the release notes and in `usr/share/casdap/build-manifest`.

Install: pick the asset matching `VERSION_ID` in `/etc/os-release`, write it to
`/var/lib/extensions/casdap.raw`, then `systemd-sysext refresh`, `systemctl restart polkit
inputplumber casdap-guard`, `udevadm control --reload`, `udevadm trigger --settle --action=change
--subsystem-match=usb --attr-match=idVendor=2972 --attr-match=idProduct=0149`,
`/usr/libexec/casdap/casdap-usb-audio --scan`,
`/usr/libexec/armada/controller-type apply`, then as the user
`systemctl --user restart pipewire pipewire-pulse wireplumber` (or log in again). The inputplumber
restart drops the controller for a moment; run it outside a game. Uninstall: delete the file, run the
same commands without the scan. Nothing under `/etc` to clean.

The Armada side ships `casdap-check.service` (`ConditionPathExists=/var/lib/extensions/casdap.raw`,
`After=systemd-sysext.service`): passes when `/usr/lib/extension-release.d/extension-release.casdap`
exists after the merge, otherwise notifies and offers the asset for the new `VERSION_ID`. Until it
exists, the `just dev` refusal and the symptom list above are the only detection.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
