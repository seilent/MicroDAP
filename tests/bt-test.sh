#!/usr/bin/bash
set -eu

here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "${here}")

for tool in dbus-run-session python3; do
    command -v "${tool}" >/dev/null || { echo "bt-test: ${tool} missing" >&2; exit 1; }
done
python3 -c 'import dbus.mainloop.glib, dbus.service; from gi.repository import GLib' 2>/dev/null \
    || { echo "bt-test: python3 dbus (dbus-python) and gi (python3-gobject) needed" >&2; exit 1; }

T=$(mktemp -d)
trap 'rm -rf "${T}"' EXIT
cat > "${T}/device-env" <<'EOF'
#!/usr/bin/bash
echo 'ARMADA_DEVICE_NAME=Test\ Device'
EOF
chmod 0755 "${T}/device-env"
cat > "${T}/microdap-keep" <<EOF
#!/usr/bin/bash
exec bash "${repo}/sysext/usr/libexec/microdap/microdap-keep" "\$@"
EOF
chmod 0755 "${T}/microdap-keep"

export MICRODAP_BT="${repo}/sysext/usr/libexec/microdap/microdap-bt"
export MICRODAP_BT_DEVICE_ENV="${T}/device-env"
export MICRODAP_BT_LOG="${T}/log"
export MICRODAP_KEEP_BIN="${T}/microdap-keep"
export MICRODAP_KEEP_FILE="${T}/keep"
export MICRODAP_BOOT_ID_FILE="${T}/boot_id"
dbus-run-session -- python3 "${here}/bt-test.py"
