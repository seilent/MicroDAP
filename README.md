# CASDAP

Console As DAP. A music player session and Bluetooth A2DP receiver for Linux gaming handhelds,
shipped as a systemd-sysext overlay for [Armada](https://github.com/armada-os/armada).

Status: prototype. The session, the switch units, the Guide preserving pad mapping, the hold daemon,
the pad target and the boot guard exist and run on an AYANEO Pocket MICRO 2. Player, receiver and
screen off state are not written yet.

## Layout

| Path | Content |
|---|---|
| `sysext/` | Static overlay, copied into `dist/` as is. `/usr` only, nothing under `/etc`. |
| `patches/<class>/<path>.patch` | Patches against files of the Armada image. `armada/` Armada scripts, `generic/` InputPlumber profile, `pm2/` Pocket MICRO 2 capability map. |
| `build/` | `base.env` (pinned image), `Containerfile`, `toolchain.sh`, `build.sh`, device scripts for the dev loop. |
| `player/` | Qt 6 QML player with a C++ core (`casdapcore`). CMake project, built by `build.sh` inside the builder, installed to `/usr/bin/casdap-player`. Playback is `playbin`, audio only, sink from `CASDAP_AUDIO_SINK` (default `pulsesink`), gapless through `about-to-finish`. Queue, index, position, shuffle and repeat persist in `~/.config/casdap/state.json`, written every 10 s while playing, on quit and on SIGTERM; a start restores the queue paused. Every play start adds a `history` row. `Mpris` registers `org.mpris.MediaPlayer2.casdap` at `/org/mpris/MediaPlayer2` on the session bus (root and `Player` interfaces, XML in `player/dbus/`, installed to `share/dbus-1/interfaces`): `Identity` is `CASDAP`, no `DesktopEntry`, track ids are `/org/casdap/track/<id>`, `Volume` reads 1.0 and ignores writes, `OpenUri` plays one `file://` uri, `Seeked` and `PropertiesChanged` are emitted for status, metadata, shuffle and loop. `PowerBridge` registers `org.casdap.Player` at `/org/casdap/Player` (`SetScreenOff(b)`, `ShowPowerMenu()`, `ScreenOff`, `PlaybackStatus`, signal `ScreenOffChanged`), listens to `org.casdap.Hotkeys.VolumeChanged(d)`, reads `/run/casdap/drift` and runs only the verbs `screen-off`, `game-mode`, `suspend`, `poweroff` (`casdap-power <verb>`, `systemctl start casdap-switch-back.service`); settings live in `~/.config/casdap/settings.ini` (`output`, `roots`, `accent`, `scale`, `pocketControls`, `saverSeconds`, written 500 ms after the last change and on quit), battery comes from UPower `DisplayDevice` with a `/sys/class/power_supply` fallback, and local playback pauses while the Bluetooth transport is active. `CASDAP_BLUEZ_FAKE=1` wires `FakeBluezModel` in place of the Bluetooth model. Removing the current queue row starts the next one or stops when none follows. Pad input is SDL3 gamepad events (Guide never mapped), polled every 16 ms while a pad is open and the screen is on, every 1 s while no pad is open, every 50 ms with the screen off only when pocket controls are on, otherwise not at all; d-pad and stick repeat at 300 ms then 80 ms, `CASDAP_PAD_LOG=1` logs each action. |
| `tests/` | bash tests with stubbed `sudo`, `systemctl`, `loginctl`, `btmgmt`, `evtest`. Player tests live in `player/tests/` (QTest, offscreen, generated FLAC and MP3 fixtures; `tst_mpris` and `tst_power` run under `dbus-run-session`). |
| `dist/` | Build output, the sysext tree. Not committed. |

The three patched files (`/usr/libexec/armada/session-control`,
`/usr/share/inputplumber/profiles/default.yaml`,
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
and visible meanwhile, reports `inputLocked` and `phase`, and `completeAll()` jumps a running
transition to its end state.

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
overlay ring is `focused` inside the 0.4 group, so it reads at 0.4 (O20).

`Home` is the root page: the menu (`music`, `bluetooth`, `playlists`, `history`, `settings`), with
Quickplay (`Pins`, `History`, `New`) peeking on the left in portrait. Left or Back/View pans Quickplay
forward and shrinks the menu to a column on the right; right or B pans back. In landscape both sit side
by side. X on a Quickplay tile opens the pin sheet. Menu items whose screen does not exist yet log
`navigate <name>`.

Screenshot mode: `casdap-player --screenshot <screen> --size WxH --out file.png [--angle deg]`,
offscreen, software renderer. Screens: `blank`, `home`, `quickplay` (`--bluez off|connected`), `components`, `overlays`, `volume`, `specimen` (every type role in its weight, labeled
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
| `just dev` | Refuses when the device `IMAGE_VERSION`, `VERSION_ID` or digest differ from `dist/usr/share/casdap/build-manifest` (no force flag, run `just base && just dist`). Then rsyncs `dist/` to `/var/lib/extensions/casdap/`, `systemd-sysext refresh`, restarts polkit and `casdap-guard`, restarts inputplumber plus `armada-controller-type.service` when a file under `usr/share/inputplumber/` changed, `systemctl --user daemon-reload`. |
| `just dev --input` / `just dev-input` | `dev` with the inputplumber restart forced. |
| `just test-player` | ctest of `build/player` inside the builder (after `just dist`); prints `skip: no builder image` where the image is absent. |
| `just off` | Unmerge, remove `/var/lib/extensions/casdap`, restart polkit and inputplumber, re-apply the controller type. |
| `just test` | `tests/session-control-test.sh`, `tests/priv-test.sh`, `tests/hotkeyd-test.sh`, `tests/pad-test.sh`, then `just test-player`. Needs the stock `session-control` at `CASDAP_BASE_ROOT/usr/libexec/armada/` (defaults to `/`, else `../armada/system_files`). |

`CASDAP_DEVICE` overrides `armada@pm2`. Root steps on the device run through `ssh -t`
and `sudo`. Run the dev loop from game mode or ssh, not from inside the casdap session.

## Session

`casdap.desktop` runs `gamescope-session-plus casdap`. `sessions.d/casdap` sets
`CLIENTCMD=/usr/bin/casdap-player`; `/etc/gamescope-session-plus/sessions.d/casdap` (runtime, not
shipped) overrides it, for example `export CLIENTCMD=konsole`. Five sessions shorter than 60 s in a
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

Polkit boundary: any process running as `armada` may start `casdap-switch.service`,
`casdap-switch-back.service`, `casdap-priv@<verb>.service` and `armada-controller-type.service`.
The verbs are fixed words; `casdap-priv` exits 1 on anything else.

Pad target: `casdap-pad.service` (user unit, `PartOf=` the casdap session) sets every InputPlumber
composite device to `xb360` plus `keyboard` through `SetTargetDevices`, so SDL players see an Xbox
pad. On session stop it starts `armada-controller-type.service`, which restores the configured type.
InputPlumber's own polkit rule grants its actions to group `wheel`, which `armada` is in.

## Input

The pad guide button (`BTN_MODE`) maps to `KeyF13` in the Pocket MICRO 2 capability map; the
default InputPlumber profile maps `KeyF13` to Guide plus `KeyF13`, so Steam keeps its menu and a
daemon can read F13 on the `InputPlumber Keyboard` node. F13 does not flow during stick calibration.

`casdap-hotkeyd` (user unit, `Upholds=` from `default.target.d`) reads that node with `evtest` and
classifies each press by the running session. Hold 1 s in game mode starts `casdap-switch.service`;
Steam shows its shortcut overlay just before. While a title runs the first hold is ignored and a
second hold within 3 s switches. Hold in casdap starts `casdap-switch-back.service`, tap sends
`PlayPause` to `org.mpris.MediaPlayer2.casdap`. In Plasma a hold switches; in any other session it
logs. In casdap the volume keys (`KEY_VOLUMEUP` on `gpio-keys`, `KEY_VOLUMEDOWN` on `pm8941_resin`,
list override `CASDAP_HOTKEYD_VOLUME_NAMES=a:b`, read with their own `evtest` streams) step
`wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@` by 5% per press and autorepeat (volume up also
unmutes the sink) and emit
`org.casdap.Hotkeys.VolumeChanged(d)` at `/org/casdap/Hotkeys` with the level from `wpctl get-volume`;
in every other session they are left to Steam. `CASDAP_HOTKEYD_DRY_RUN=1` (runtime drop-in under
`~/.config/systemd/user/`) logs `would:` lines instead of acting.

## Guard

`casdap-guard.service` runs at boot (`Upholds=` from `multi-user.target.d`) and after every dev loop.
It resolves the booted deployment from `ostree=` on the kernel command line and compares each file
in `shadow-base.sha256`; a mismatch logs one warning per file and lists the path in
`/run/casdap/drift`. Without a resolvable deployment it compares `IMAGE_VERSION` instead.

## Rebuild per Armada release

Every Armada release (new digest, `VERSION_ID` or pipewire minor) needs a casdap rebuilt against
it: `just base && just build`. A casdap built against an older image hides later changes to the
three patched files while merged; the guard warns about it.

## Known limits

Symptoms of a sysext left unmerged after a bootc upgrade to another `VERSION_ID`:
`systemd-sysext status` lists nothing or marks casdap incompatible, the pad guide tap opens the
Steam menu and a hold does nothing, `casdap-switch.service` is unknown to systemd, no `casdap-*`
user units. Fix: install the asset for the new `VERSION_ID`.

The short session tracker `/tmp/chimeraos-short-session-tracker` is shared by every
gamescope-session-plus client. Short casdap sessions count against Steam's recovery (5 in 60 s each
rewrite the drop-in to desktop).

## Install contract

Release asset `casdap-<ver>-armada<VERSION_ID>-arm64.raw`, built against one image digest, both in
the release notes and in `usr/share/casdap/build-manifest`.

Install: pick the asset matching `VERSION_ID` in `/etc/os-release`, write it to
`/var/lib/extensions/casdap.raw`, then `systemd-sysext refresh`, `systemctl restart polkit
inputplumber casdap-guard`, `systemctl start armada-controller-type.service`. The inputplumber
restart drops the controller for a moment; run it outside a game. Uninstall: delete the file, run the
same commands. Nothing under `/etc` to clean.

The Armada side ships `casdap-check.service` (`ConditionPathExists=/var/lib/extensions/casdap.raw`,
`After=systemd-sysext.service`): passes when `/usr/lib/extension-release.d/extension-release.casdap`
exists after the merge, otherwise notifies and offers the asset for the new `VERSION_ID`. Until it
exists, the `just dev` refusal and the symptom list above are the only detection.

## License

GPL-2.0-or-later. See [LICENSE](LICENSE).
