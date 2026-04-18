#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-art-replace-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
ROM_NAME="Artwork Test.gba"
ROM_PATH="$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/$ROM_NAME"
ART_DIR="$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/.media"
PNG_SOURCE="$WORK_DIR/replacement.png"
JPG_SOURCE="$WORK_DIR/replacement.jpg"
PNG_TARGET="$ART_DIR/Artwork Test.png"
JPG_TARGET="$ART_DIR/Artwork Test.jpg"
JPEG_TARGET="$ART_DIR/Artwork Test.jpeg"
WEBP_TARGET="$ART_DIR/Artwork Test.webp"
CSRF_TOKEN=""

prepare_mock_sdcard "$SDCARD_ROOT"
mkdir -p "$ART_DIR"
printf 'rom' > "$ROM_PATH"
printf 'old-jpg' > "$JPG_TARGET"
printf 'old-jpeg' > "$JPEG_TARGET"
printf 'old-webp' > "$WEBP_TARGET"
printf 'png' > "$PNG_SOURCE"
printf 'jpg' > "$JPG_SOURCE"

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

UNAUTH_RESPONSE="$(curl -sS -X POST \
    -F "tag=GBA" \
    -F "path=$ROM_NAME" \
    -F "file=@$PNG_SOURCE;filename=replacement.png" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/art/replace)"
echo "$UNAUTH_RESPONSE" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UNAUTH_RESPONSE" | tail -n 1 | grep -q '^403$'

PAIR_RESPONSE="$(curl -sS -c "$COOKIE_JAR" -X POST --data "browser_id=art-browser&code=7391" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$PAIR_RESPONSE" | head -n 1 | grep -Fq '{"ok":true'
echo "$PAIR_RESPONSE" | tail -n 1 | grep -q '^200$'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]

NON_PNG_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "tag=GBA" \
    -F "path=$ROM_NAME" \
    -F "file=@$JPG_SOURCE;filename=replacement.jpg" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/art/replace)"
echo "$NON_PNG_RESPONSE" | head -n 1 | grep -Fq '"error":"unsupported_art_type"'
echo "$NON_PNG_RESPONSE" | tail -n 1 | grep -q '^400$'

PNG_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "tag=GBA" \
    -F "path=$ROM_NAME" \
    -F "file=@$PNG_SOURCE;filename=replacement.png" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/art/replace)"
echo "$PNG_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"replace-art"}'
echo "$PNG_RESPONSE" | tail -n 1 | grep -q '^200$'

test -f "$PNG_TARGET"
cmp -s "$PNG_SOURCE" "$PNG_TARGET"
test ! -f "$JPG_TARGET"
test ! -f "$JPEG_TARGET"
test ! -f "$WEBP_TARGET"

BROWSER_RESPONSE="$(curl -sS -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA')"
printf '%s' "$BROWSER_RESPONSE" | grep -Fq "\"name\":\"$ROM_NAME\""
printf '%s' "$BROWSER_RESPONSE" | grep -Fq '"thumbnailPath":".media/Artwork Test.png"'

echo "PASS art replace smoke"
