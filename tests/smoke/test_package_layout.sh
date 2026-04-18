#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make package-local >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-package-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
PAK_DIR="build/staging/Tools/tg5040/CentralScrutinizer.pak"
PORT=8891

prepare_mock_sdcard "$SDCARD_ROOT"

SERVER_PID=""

cleanup() {
    if [ -n "$SERVER_PID" ]; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT INT TERM

for platform in tg5040 tg5050 my355; do
    pak_dir="build/staging/Tools/$platform/CentralScrutinizer.pak"
    test -f "$pak_dir/central-scrutinizer"
    test -f "$pak_dir/launch.sh"
    test -f "$pak_dir/pak.json"
    test -f "$pak_dir/resources/web/index.html"
    test -x "$pak_dir/central-scrutinizer"
    test -x "$pak_dir/launch.sh"
done

(
    cd "$PAK_DIR"
    ./launch.sh --headless --port "$PORT" --sdcard "$SDCARD_ROOT"
) &
SERVER_PID=$!

READY=0
for _ in $(seq 1 50); do
    if curl -sf "http://127.0.0.1:$PORT/api/status" >/dev/null; then
        READY=1
        break
    fi
    sleep 0.1
done

if [ "$READY" -ne 1 ]; then
    echo "packaged server did not become ready" >&2
    exit 1
fi

ROOT_HTML="$(curl -sf "http://127.0.0.1:$PORT/")"
echo "$ROOT_HTML" | grep -qi '<!doctype html'
echo "$ROOT_HTML" | grep -Fq '__next'

test -f "build/release/local/CentralScrutinizer-local.pakz"

echo "PASS package layout smoke"
