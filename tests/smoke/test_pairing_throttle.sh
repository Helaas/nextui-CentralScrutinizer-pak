#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-pairing-throttle-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"

prepare_mock_sdcard "$SDCARD_ROOT"

CS_PAIRING_CODE=7391 ./build/mac/central-scrutinizer --headless --port 8877 --web-root web/out --sdcard "$SDCARD_ROOT" &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT INT TERM

READY=0
for _ in $(seq 1 50); do
    if curl -sf http://127.0.0.1:8877/api/status >/dev/null; then
        READY=1
        break
    fi
    sleep 0.1
done

if [ "$READY" -ne 1 ]; then
    echo "server did not become ready" >&2
    exit 1
fi

for code in 0000 1111 2222 3333; do
    RESPONSE="$(curl -s -X POST --data "browser_id=browser-lock&code=$code" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
    echo "$RESPONSE" | head -n 1 | grep -q '"ok":false'
    echo "$RESPONSE" | tail -n 1 | grep -q '^403$'
done

LOCKED_RESPONSE="$(curl -s -X POST --data "browser_id=browser-lock&code=4444" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$LOCKED_RESPONSE" | head -n 1 | grep -q '"error":"pairing_throttled"'
echo "$LOCKED_RESPONSE" | tail -n 1 | grep -q '^429$'

VALID_WHILE_LOCKED="$(curl -s -X POST --data "browser_id=browser-lock&code=7391" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$VALID_WHILE_LOCKED" | head -n 1 | grep -q '"error":"pairing_throttled"'
echo "$VALID_WHILE_LOCKED" | tail -n 1 | grep -q '^429$'

echo "PASS pairing throttle smoke"
