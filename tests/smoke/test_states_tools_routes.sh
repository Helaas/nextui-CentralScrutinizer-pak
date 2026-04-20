#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-states-tools-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
RESPONSE_BODY="$WORK_DIR/response.body"
CSRF_TOKEN=""

prepare_mock_sdcard "$SDCARD_ROOT"

mkdir -p "$SDCARD_ROOT/.Spotlight-V100"
printf 'finder' > "$SDCARD_ROOT/Roms/.DS_Store"
printf 'appledouble' > "$SDCARD_ROOT/Roms/._Pokemon Emerald.gba"
mkdir -p "$SDCARD_ROOT/Roms/__MACOSX"

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

curl -sf -c "$COOKIE_JAR" -X POST --data "browser_id=states-tools-browser&code=7391" http://127.0.0.1:8877/api/pair | grep -q '"ok":true'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    'http://127.0.0.1:8877/api/states?tag=GBA')"
[ "$STATUS_CODE" = "405" ]
grep -Fq '"error":"method_not_allowed"' "$RESPONSE_BODY"

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -b "$COOKIE_JAR" \
    'http://127.0.0.1:8877/api/states?tag=GBA')"
[ "$STATUS_CODE" = "403" ]
grep -Fq '{"ok":false}' "$RESPONSE_BODY"

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -b "$COOKIE_JAR" \
    -H 'X-CS-CSRF: invalid-token' \
    'http://127.0.0.1:8877/api/states?tag=GBA')"
[ "$STATUS_CODE" = "403" ]
grep -Fq '{"ok":false}' "$RESPONSE_BODY"

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    'http://127.0.0.1:8877/api/states')"
[ "$STATUS_CODE" = "400" ]
grep -Fq '"error":"missing_tag"' "$RESPONSE_BODY"

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    'http://127.0.0.1:8877/api/states?tag=../etc')"
[ "$STATUS_CODE" = "404" ]
grep -Fq '"error":"platform_not_found"' "$RESPONSE_BODY"

STATES_RESPONSE="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/states?tag=GBA')"
printf '%s' "$STATES_RESPONSE" | grep -Fq '"platformTag":"GBA"'
printf '%s' "$STATES_RESPONSE" | grep -Fq '"count":5'
printf '%s' "$STATES_RESPONSE" | grep -Fq '"truncated":false'
printf '%s' "$STATES_RESPONSE" | grep -Fq '"title":"Pokemon Emerald.gba"'
printf '%s' "$STATES_RESPONSE" | grep -Fq '"slotLabel":"Auto Resume (Legacy)"'
printf '%s' "$STATES_RESPONSE" | grep -Fq '".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp"'
printf '%s' "$STATES_RESPONSE" | grep -Fq '"Matching .minui metadata was not found."'

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    'http://127.0.0.1:8877/api/tools/mac-dotfiles')"
[ "$STATUS_CODE" = "405" ]
grep -Fq '"error":"method_not_allowed"' "$RESPONSE_BODY"

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -b "$COOKIE_JAR" \
    'http://127.0.0.1:8877/api/tools/mac-dotfiles')"
[ "$STATUS_CODE" = "403" ]
grep -Fq '{"ok":false}' "$RESPONSE_BODY"

STATUS_CODE="$(curl -sS -o "$RESPONSE_BODY" -w '%{http_code}' \
    -b "$COOKIE_JAR" \
    -H 'X-CS-CSRF: invalid-token' \
    'http://127.0.0.1:8877/api/tools/mac-dotfiles')"
[ "$STATUS_CODE" = "403" ]
grep -Fq '{"ok":false}' "$RESPONSE_BODY"

DOTFILES_RESPONSE="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/tools/mac-dotfiles')"
printf '%s' "$DOTFILES_RESPONSE" | grep -Fq '"count":4'
printf '%s' "$DOTFILES_RESPONSE" | grep -Fq '"truncated":false'
printf '%s' "$DOTFILES_RESPONSE" | grep -Fq '".Spotlight-V100"'
printf '%s' "$DOTFILES_RESPONSE" | grep -Fq '"Roms/.DS_Store"'
printf '%s' "$DOTFILES_RESPONSE" | grep -Fq '"Roms/._Pokemon Emerald.gba"'
printf '%s' "$DOTFILES_RESPONSE" | grep -Fq '"Roms/__MACOSX"'

DELETE_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    --data-urlencode "scope=files" \
    --data-urlencode "path=Roms/.DS_Store" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/item/delete)"
echo "$DELETE_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"delete"}'
echo "$DELETE_RESPONSE" | tail -n 1 | grep -q '^200$'
test ! -f "$SDCARD_ROOT/Roms/.DS_Store"

DELETE_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    --data-urlencode "scope=files" \
    --data-urlencode "path=Roms/__MACOSX" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/item/delete)"
echo "$DELETE_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"delete"}'
echo "$DELETE_RESPONSE" | tail -n 1 | grep -q '^200$'
test ! -d "$SDCARD_ROOT/Roms/__MACOSX"

DOTFILES_AFTER_DELETE="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/tools/mac-dotfiles')"
printf '%s' "$DOTFILES_AFTER_DELETE" | grep -Fq '"count":2'
printf '%s' "$DOTFILES_AFTER_DELETE" | grep -Fq '"truncated":false'
printf '%s' "$DOTFILES_AFTER_DELETE" | grep -Fq '".Spotlight-V100"'
printf '%s' "$DOTFILES_AFTER_DELETE" | grep -Fq '"Roms/._Pokemon Emerald.gba"'

mkdir -p "$SDCARD_ROOT/Roms/truncate"
for i in $(seq 1 600); do
    printf 'appledouble' > "$SDCARD_ROOT/Roms/truncate/._bulk$(printf '%03d' "$i")"
done

DOTFILES_TRUNCATED="$(curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/tools/mac-dotfiles')"
printf '%s' "$DOTFILES_TRUNCATED" | grep -Fq '"count":602'
printf '%s' "$DOTFILES_TRUNCATED" | grep -Fq '"truncated":true'

echo "PASS states/tools smoke"
