set shell := ["bash", "-euo", "pipefail", "-c"]

device := env("MICRODAP_DEVICE", "armada@pm2")
platform := "linux/arm64"
builder := "localhost/microdap-builder"
base_root := env("MICRODAP_BASE_ROOT", if path_exists("/usr/libexec/armada/session-control") == "true" { "/" } else { "../armada/system_files" })

default:
    @just --list

base:
    build/device-state {{device}} > build/base.env
    cat build/base.env

builder:
    . build/base.env && podman build --platform {{platform}} --build-arg BASE_IMAGE="$BASE_IMAGE" -t {{builder}} -f build/Containerfile .

dist: builder
    podman run --rm --platform {{platform}} -v "$PWD":/work:Z -e MICRODAP_VERSION="$(git describe --always --dirty 2>/dev/null || echo unknown)" {{builder}} build/build.sh

build: builder
    podman run --rm --platform {{platform}} -v "$PWD":/work:Z -e MICRODAP_VERSION="$(git describe --always --dirty 2>/dev/null || echo unknown)" -e MICRODAP_IMAGE=1 {{builder}} build/build.sh

dev *flags:
    build/dev-loop {{device}} {{flags}}

dev-input:
    build/dev-loop {{device}} --input

off:
    rsync -a build/device-off {{device}}:/tmp/microdap-off
    ssh -t {{device}} sudo bash /tmp/microdap-off

test:
    MICRODAP_BASE_ROOT={{base_root}} bash tests/session-control-test.sh
    bash tests/guard-test.sh
    bash tests/priv-test.sh
    bash tests/hotkeyd-test.sh
    bash tests/pad-test.sh
    bash tests/intercept-test.sh
    bash tests/keep-test.sh
    bash tests/codec-test.sh
    bash tests/power-test.sh
    bash tests/powerd-test.sh
    bash tests/wait-x-test.sh
    bash tests/overlay-unit-test.sh
    bash tests/bt-test.sh
    bash tests/usb-audio-test.sh
    bash tests/fiio-unit-test.sh
    bash tests/device-off-test.sh
    just test-player

test-player:
    if ! command -v podman >/dev/null || ! podman image exists {{builder}}; then echo "skip: no builder image"; exit 0; fi; \
    [[ -f build/player/CTestTestfile.cmake ]] || { echo "test-player: run just dist first" >&2; exit 1; }; \
    podman run --rm --platform {{platform}} -v "$PWD":/work:Z -w /work/build/player {{builder}} ctest --output-on-failure
