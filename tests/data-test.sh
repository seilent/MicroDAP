#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")
data="${repo}/data"

failures=0
fail() { echo "FAIL $*" >&2; failures=$((failures + 1)); }

T=$(mktemp -d)
cleanup() {
    [[ -n "${daemon:-}" ]] && kill "${daemon}" 2>/dev/null
    [[ -s "${T}/bus.pid" ]] && kill "$(cat "${T}/bus.pid")" 2>/dev/null
    rm -rf "${T}"
}
trap cleanup EXIT

session="${data}/microos/sessions.d/microdap.conf"
[[ "$(cat "${session}")" == $'LABEL=MicroDAP\nDESKTOP=microdap.desktop\nUNIT=gamescope-session-plus@microdap.service\nSTOP=unit\nDEFAULT=yes' ]] \
    || fail "sessions.d/microdap.conf content"

client="${data}/gamescope-session-plus/sessions.d/microdap"
grep -qxF 'export CLIENTCMD=/usr/bin/microdap-player' "${client}" || fail "client file lacks CLIENTCMD"
grep -qxF 'export ORIENTATION=normal' "${client}" || fail "client file lacks ORIENTATION=normal"
grep -qxF 'export QT_QPA_PLATFORM=xcb' "${client}" || fail "client file lacks QT_QPA_PLATFORM=xcb"
grep -qxF 'export MICRODAP_GAMESCOPE_APPID=0x4d444150' "${client}" || fail "client file lacks the app id"
grep -q 'switch\|systemctl' "${client}" && fail "client file starts a switch"
bash -n "${client}" || fail "client file does not parse"

desktop="${data}/wayland-sessions/microdap.desktop"
grep -qxF 'Exec=gamescope-session-plus microdap' "${desktop}" || fail "desktop Exec"

player='net.seilent.MicroDap.Player /net/seilent/MicroDap/Player net.seilent.MicroDap.Player'
[[ "$(cat "${data}/microos/hotkeys.d/microdap.conf")" == "guide tap none
guide hold none
power tap call ${player} ToggleScreen
power hold call ${player} ShowPowerMenu" ]] || fail "hotkeys.d/microdap.conf content"
[[ "$(cat "${data}/microos/hotkeys.d/steam.conf.d/50-microdap.conf")" == "guide tap call ${player} ToggleOverlay
guide hold switch microdap confirm-in-game" ]] || fail "steam.conf.d/50-microdap.conf content"
[[ "$(cat "${data}/microos/hotkeys.d/microemu.conf.d/50-microdap.conf")" == "guide hold switch microdap confirm-in-game" ]] \
    || fail "microemu.conf.d/50-microdap.conf content"

while IFS= read -r -d '' table; do
    n=0
    while read -r key gesture action args; do
        n=$((n + 1))
        where="${table#"${data}/"}:${n}"
        [[ "${key}" =~ ^(power|guide|lc|rc|volup|voldown)$ ]] || fail "${where}: key ${key}"
        [[ "${gesture}" =~ ^(tap|hold|press)$ ]] || fail "${where}: gesture ${gesture}"
        read -r -a words <<< "${args}"
        case "${action}" in
            none) (( ${#words[@]} == 0 )) || fail "${where}: none takes no words" ;;
            call) (( ${#words[@]} == 4 )) || fail "${where}: call takes 4 words" ;;
            switch) [[ "${args}" =~ ^[a-z0-9]+( confirm-in-game)?$ ]] || fail "${where}: switch ${args}" ;;
            *) fail "${where}: action ${action}" ;;
        esac
        [[ "${key}" == vol* ]] && fail "${where}: volume keys belong to the player"
    done < "${table}"
done < <(find "${data}/microos/hotkeys.d" -name '*.conf' -print0)

python3 - "${data}/microset/schema.d/50-microdap.json" <<'PY' || fail "schema check"
import json, re, sys
d = json.load(open(sys.argv[1]))
assert d["owner"] == "microdap", d["owner"]
want = {"output_level", "sample_rate", "play_through", "resume", "auto_now_playing", "replaygain", "roots", "accent_hue",
        "accent_from_art", "scale", "saver_seconds", "saver_lyrics", "touch", "rgb", "overlay_toasts", "clock",
        "battery_percent", "quickplay", "screen_off_seconds", "suspend_minutes", "wifi_off_screen_off"}
keys = d["keys"]
assert {k.split(".", 1)[1] for k in keys} == want, sorted(keys)
for name, e in keys.items():
    assert re.fullmatch(r"[a-z][a-z0-9]*(\.[A-Za-z0-9_]+)+", name), name
    assert e["applier"] is None and e["group"].startswith("microdap_") and e["label"], name
    t, v = e["type"], e["default"]
    ok = {"s": isinstance(v, str), "b": isinstance(v, bool), "u": isinstance(v, int) and not isinstance(v, bool) and v >= 0,
          "d": isinstance(v, (int, float)) and not isinstance(v, bool), "as": isinstance(v, list)}[t]
    assert ok, (name, t, v)
    if "enum" in e:
        assert v in e["enum"], name
    if "min" in e:
        assert e["min"] <= v <= e["max"], name
assert keys["microdap.rgb"]["enum"] == ["system", "accent", "off"]
assert keys["microdap.play_through"]["enum"] == ["off", "folders", "albums"]
assert keys["microdap.resume"]["enum"] == ["off", "track", "position"]
assert keys["microdap.resume"]["default"] == "position"
assert keys["microdap.replaygain"]["enum"] == ["off", "track", "album"]
assert keys["microdap.replaygain"]["default"] == "off"
assert keys["microdap.auto_now_playing"]["default"] is False
assert keys["microdap.clock"]["enum"] == ["24h", "12h", "off"]
PY

for f in $(find "${data}" -type f) "${repo}/CMakeLists.txt"; do
    grep -v '^#!' "${f}" | grep -qE '^\s*#' && fail "${f#"${repo}/"}: comment"
    LC_ALL=C grep -q -P '\xe2\x80[\x93\x94]' "${f}" && fail "${f#"${repo}/"}: dash"
    grep -qi 'armada' "${f}" && fail "${f#"${repo}/"}: armada"
    grep -qE 'date \+%s|EPOCHREALTIME|SECONDS' "${f}" && fail "${f#"${repo}/"}: wall clock"
done
[[ ! -e "${repo}/sysext" ]] || fail "sysext/ still present"

root="${MICRODAP_ROOT:-/}"
[[ -x "${root}/usr/bin/microdap-player" ]] || { echo "data-test: ${root} has no installed build, run cmake --install first" >&2; exit 1; }
for f in \
    usr/libexec/microdap/microdap-bt usr/libexec/microdap/microdap-fiio usr/libexec/microdap/microdap-keep \
    usr/libexec/microdap/microdap-radio usr/libexec/microdap/microdap-stopped usr/libexec/microdap/microdap-usb-audio \
    usr/libexec/microdap/microdap-wait-x; do
    [[ -x "${root}/${f}" ]] || fail "${f} not installed executable"
done
for f in \
    usr/lib/microos/sessions.d/microdap.conf usr/lib/microos/hotkeys.d/microdap.conf \
    usr/lib/microos/hotkeys.d/steam.conf.d/50-microdap.conf usr/lib/microos/hotkeys.d/microemu.conf.d/50-microdap.conf \
    usr/lib/systemd/user/microdap-overlay.service usr/lib/systemd/user/microdap-bt.service \
    usr/lib/systemd/user/microdap-fiio-reset.service \
    usr/lib/systemd/user/gamescope-session-plus@microdap.service.d/05-microdap-fiio.conf \
    usr/lib/systemd/user/gamescope-session-plus@microdap.service.d/50-microdap.conf \
    usr/lib/systemd/user/gamescope-session-plus@steam.service.d/40-microdap-fiio.conf \
    usr/lib/systemd/system/microdap-usb-audio.service usr/lib/systemd/system/microdap-radio.service \
    usr/lib/systemd/user-preset/70-microdap.preset usr/lib/systemd/system-preset/70-microdap.preset \
    usr/lib/udev/rules.d/70-microdap-usb-audio.rules usr/share/microset/schema.d/50-microdap.json \
    usr/share/gamescope-session-plus/sessions.d/microdap usr/share/wayland-sessions/microdap.desktop; do
    [[ -f "${root}/${f}" ]] || fail "${f} not installed"
    [[ -x "${root}/${f}" ]] && fail "${f} installed executable"
done

[[ "${root}" == / ]] || { echo "data-test: unit and schema checks need the build installed to /" >&2; exit 1; }

export XDG_RUNTIME_DIR="${T}/run" HOME="${T}/home"
mkdir -p "${XDG_RUNTIME_DIR}" "${HOME}" "${T}/units"
printf '[Unit]\nDescription=stub\n[Service]\nExecStart=/usr/bin/true\n' > "${T}/units/gamescope-session-plus@.service"
out=$(cd "${T}" && SYSTEMD_UNIT_PATH="${T}/units:" systemd-analyze --user verify microdap-overlay.service microdap-bt.service \
    microdap-fiio-reset.service gamescope-session-plus@microdap.service gamescope-session-plus@steam.service 2>&1) \
    || fail "user units verify: ${out}"
[[ -z "${out}" ]] || fail "user units verify: ${out}"
out=$(cd "${T}" && systemd-analyze verify microdap-radio.service microdap-usb-audio.service 2>&1) || fail "system units verify: ${out}"
[[ -z "${out}" ]] || fail "system units verify: ${out}"

cat > "${T}/bus.conf" <<BUS
<busconfig>
  <type>system</type>
  <listen>unix:path=${T}/bus</listen>
  <auth>EXTERNAL</auth>
  <policy context="default">
    <allow user="*"/>
    <allow own="*"/>
    <allow send_destination="*"/>
    <allow receive_sender="*"/>
  </policy>
</busconfig>
BUS
dbus-daemon --config-file="${T}/bus.conf" --fork --print-pid > "${T}/bus.pid"
export DBUS_SYSTEM_BUS_ADDRESS="unix:path=${T}/bus"
mkdir -p "${T}/state"
STATE_DIRECTORY="${T}/state" MICROSET_RGB_DEVICE="${T}/tty" /usr/libexec/microsetd > "${T}/microsetd.log" 2>&1 &
daemon=$!
for _ in $(seq 100); do
    microset list microdap. > "${T}/list" 2>/dev/null && break
    sleep 0.1
done
grep -q '50-microdap.json' "${T}/microsetd.log" && fail "microsetd: $(grep '50-microdap.json' "${T}/microsetd.log")"
microset schema microdap. > "${T}/schema.json" || fail "microset schema"
python3 - "${T}/schema.json" "${data}/microset/schema.d/50-microdap.json" <<'PY' || fail "microsetd did not load every key"
import json, sys
loaded = json.load(open(sys.argv[1]))
file = json.load(open(sys.argv[2]))
mine = file["keys"]
assert set(loaded) == set(mine), sorted(set(mine) ^ set(loaded))
for k, e in mine.items():
    assert loaded[k]["owner"] == "microdap" and loaded[k]["default"] == e["default"], k
    assert loaded[k]["ui"] == e["ui"], k
    assert loaded[k]["group_ui"] == file["groups"][e["group"]], k
assert loaded["microdap.roots"]["ui"]["widget"] == "none"
PY
microset set microdap.rgb accent || fail "set microdap.rgb accent refused"
[[ "$(microset get microdap.rgb)" == accent ]] || fail "microdap.rgb reads $(microset get microdap.rgb)"
microset set microdap.rgb armada 2>/dev/null && fail "set microdap.rgb armada accepted"
microset set microdap.scale 2.0 2>/dev/null && fail "set microdap.scale 2.0 accepted"
microset set microdap.screen_off_seconds 45 2>/dev/null && fail "set microdap.screen_off_seconds 45 accepted"

if (( failures )); then
    echo "data-test: ${failures} failure(s)" >&2
    exit 1
fi
echo "data-test: ok"
