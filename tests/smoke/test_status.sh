#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-status-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$(mktemp /tmp/cs-status-cookies-XXXXXX)"
CSRF_TOKEN=""

prepare_mock_sdcard "$SDCARD_ROOT"

CS_PAIRING_CODE=7391 ./build/mac/central-scrutinizer --headless --port 8877 --web-root web/out --sdcard "$SDCARD_ROOT" &
SERVER_PID=$!

cleanup() {
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
    rm -f "$COOKIE_JAR"
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

curl -sf http://127.0.0.1:8877/api/status | grep -q '"platform":"mac"'
curl -sf http://127.0.0.1:8877/api/session | grep -q '"paired":false'
curl -sf http://127.0.0.1:8877/api/status | grep -q '"trustedCount":0'
curl -sf http://127.0.0.1:8877/api/session | grep -q '"trustedCount":0'
if curl -sf http://127.0.0.1:8877/api/status | grep -q '"paired"'; then
    echo "/api/status should not expose paired" >&2
    exit 1
fi
STATUS_HEADERS="$(curl -sD - -o /dev/null http://127.0.0.1:8877/api/status)"
printf '%s' "$STATUS_HEADERS" | grep -qi '^X-Content-Type-Options: nosniff'
printf '%s' "$STATUS_HEADERS" | grep -qi '^X-Frame-Options: DENY'
printf '%s' "$STATUS_HEADERS" | grep -qi '^Referrer-Policy: no-referrer'
printf '%s' "$STATUS_HEADERS" | grep -qi '^Content-Security-Policy:'

INVALID_ZERO="$(curl -s -X POST --data "browser_id=browser-a&code=0000" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$INVALID_ZERO" | head -n 1 | grep -q '"ok":false'
echo "$INVALID_ZERO" | head -n 1 | grep -q '"error":"invalid_code"'
echo "$INVALID_ZERO" | tail -n 1 | grep -q '^403$'

INVALID_OLD="$(curl -s -X POST --data "browser_id=browser-a&code=4827" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$INVALID_OLD" | head -n 1 | grep -q '"ok":false'
echo "$INVALID_OLD" | head -n 1 | grep -q '"error":"invalid_code"'
echo "$INVALID_OLD" | tail -n 1 | grep -q '^403$'

PAIR_RESPONSE="$(curl -sS -c "$COOKIE_JAR" -X POST --data "browser_id=browser-a&code=7391" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$PAIR_RESPONSE" | head -n 1 | grep -Fq '{"ok":true'
echo "$PAIR_RESPONSE" | head -n 1 | grep -q '"trustedCount":1'
echo "$PAIR_RESPONSE" | tail -n 1 | grep -q '^200$'

REPLAY_RESPONSE="$(curl -s -X POST --data "browser_id=browser-b&code=7391" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$REPLAY_RESPONSE" | head -n 1 | grep -q '"ok":false'
echo "$REPLAY_RESPONSE" | tail -n 1 | grep -q '^403$'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]
printf '%s' "$SESSION_RESPONSE" | grep -q '"trustedCount":1'

curl -sf -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session | grep -q '"paired":true'
curl -sf http://127.0.0.1:8877/api/status | grep -q '"trustedCount":1'
if curl -sf http://127.0.0.1:8877/api/status | grep -q '"paired"'; then
    echo "/api/status should not expose paired" >&2
    exit 1
fi

MISSING_CSRF_PLATFORMS="$(curl -s -b "$COOKIE_JAR" -w '\n%{http_code}' http://127.0.0.1:8877/api/platforms)"
echo "$MISSING_CSRF_PLATFORMS" | head -n 1 | grep -q '"ok":false'
echo "$MISSING_CSRF_PLATFORMS" | tail -n 1 | grep -q '^403$'

curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" http://127.0.0.1:8877/api/platforms | grep -q '"groups"'
curl -sf -b "$COOKIE_JAR" "http://127.0.0.1:8877/api/download?scope=files&path=Bios%2FGBA%2Fgba_bios.bin&csrf=$CSRF_TOKEN" >/dev/null

MISSING_CSRF_REVOKE="$(curl -s -b "$COOKIE_JAR" -X POST -w '\n%{http_code}' http://127.0.0.1:8877/api/revoke)"
echo "$MISSING_CSRF_REVOKE" | head -n 1 | grep -q '"ok":false'
echo "$MISSING_CSRF_REVOKE" | tail -n 1 | grep -q '^403$'

curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" -X POST http://127.0.0.1:8877/api/revoke | grep -q '"ok":true'
curl -sf -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session | grep -q '"paired":false'
curl -sf http://127.0.0.1:8877/api/status | grep -q '"trustedCount":0'

echo "PASS status smoke"
