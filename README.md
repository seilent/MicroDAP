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
| `player/` | Qt 6 QML player with a C++ core (`casdapcore`). CMake project, built by `build.sh` inside the builder, installed to `/usr/bin/casdap-player`. |
| `tests/` | bash tests with stubbed `sudo`, `systemctl`, `loginctl`, `btmgmt`, `evtest`. Player tests live in `player/tests/` (QTest, offscreen, generated FLAC and MP3 fixtures). |
| `dist/` | Build output, the sysext tree. Not committed. |

The three patched files (`/usr/libexec/armada/session-control`,
`/usr/share/inputplumber/profiles/default.yaml`,
`/usr/share/inputplumber/capability_maps/ayaneo_pocket_micro2.yaml`) are never checked in whole.
`build.sh` copies each base file out of the pinned image, records its sha256 in
`usr/share/casdap/shadow-base.sha256`, applies the patch with `patch -p1` and fails on any reject.

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
logs. `CASDAP_HOTKEYD_DRY_RUN=1` (runtime drop-in under `~/.config/systemd/user/`) logs `would:`
lines instead of acting.

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
