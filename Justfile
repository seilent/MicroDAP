set shell := ["bash", "-euo", "pipefail", "-c"]

builder := "localhost/microdap-builder"
microset := env("MICROSET_SRC", justfile_directory() / ".." / "MicroSET")

default:
    @just --list

builder:
    podman build -t {{builder}} -f build/Containerfile .

test: builder
    podman run --rm -v "$PWD":/work:Z -v "{{microset}}":/microset:ro,Z -e MICROSET_SRC=/microset {{builder}} build/ci.sh
