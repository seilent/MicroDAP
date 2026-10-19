# MicroDAP

Music player session and Bluetooth A2DP receiver for MicroOS on the AYANEO Pocket MICRO 2. Packaged by MicroOS as
the `microdap` RPM; settings live in MicroSET (`microdap.*` and the device keys).

## Layout

| Path | Content |
|---|---|
| `player/` | Qt 6 QML player (`microdap-player`) with its C++ core, and `microdap-fiio` |
| `data/` | Session, hotkey, schema, unit, preset and udev files, and the helper scripts in `data/libexec/` |
| `tests/` | Bash tests for the scripts and the installed files |
| `build/` | Builder image (`Containerfile`, `deps.sh`), `ci.sh`, the MicroSET pin (`microset.env`), the Selawik fetch for the QML tests |

## Installed files

| Path | From |
|---|---|
| `/usr/bin/microdap-player`, `/usr/libexec/microdap/` | `player/`, `data/libexec/` |
| `/usr/lib/microos/sessions.d/microdap.conf` | MicroOS session `microdap` (`STOP=unit`, `DEFAULT=yes`) |
| `/usr/lib/microos/hotkeys.d/microdap.conf`, `steam.conf.d/50-microdap.conf`, `microemu.conf.d/50-microdap.conf` | hotkeyd rows: power tap `ToggleScreen`, power hold `ShowPowerMenu`, Guide tap `ToggleOverlay` in Steam, Guide hold switches to MicroDAP |
| `/usr/share/microset/schema.d/50-microdap.json` | `microdap.*` keys |
| `/usr/share/wayland-sessions/microdap.desktop`, `/usr/share/gamescope-session-plus/sessions.d/microdap` | the gamescope session |
| `/usr/lib/systemd/user/` | `microdap-overlay` (in Steam), `microdap-bt`, `microdap-fiio-reset`, session drop ins |
| `/usr/lib/systemd/system/` | `microdap-usb-audio`, `microdap-radio` |
| `/usr/lib/systemd/*-preset/70-microdap.preset`, `/usr/lib/udev/rules.d/70-microdap-usb-audio.rules` | presets, USB DAC rule |

Every MicroDAP session stop runs `microdap-stopped`: backlight `bl_power` back to 0, and the Wi-Fi radio back on when the
player turned it off (`~/.local/state/microdap/wifi-off`). `microdap-radio.service` does the radio part at boot.

## Library

`~/.local/share/microdap/library.db`, schema 6. A newer schema opens when its `meta` row `min_reader` is 6 or lower.
Players older than schema 6 close a schema 6 database and show an empty library; the file stays as is.

## Build and test

```
just test
```

Builds the builder image (Fedora 44), then in it MicroSET from `MICROSET_SRC` (default `../MicroSET`), MicroDAP, ctest,
`cmake --install` and every `tests/*-test.sh`. CI runs the same `build/ci.sh` on `ubuntu-24.04-arm` with MicroSET at
`build/microset.env`.

## License

GPL-3.0-or-later. See [LICENSE](LICENSE).
