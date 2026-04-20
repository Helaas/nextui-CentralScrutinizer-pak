#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-file-ops-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
ORIGINAL_ROM=""
RENAMED_ROM="renamed-$RANDOM-$$.gba"
DOWNLOAD_PATH="$WORK_DIR/downloaded-rom.gba"
FILES_FOLDER=".userdata/shared/CentralScrutinizer/manual"
CSRF_TOKEN=""

prepare_mock_sdcard "$SDCARD_ROOT"
ORIGINAL_ROM="$(find "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)" -maxdepth 1 -type f -name '*.gba' -print | sed 's#.*/##' | head -n 1)"
[ -n "$ORIGINAL_ROM" ]

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

PAIR_RESPONSE="$(curl -sS -c "$COOKIE_JAR" -X POST --data "browser_id=file-ops-browser&code=7391" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$PAIR_RESPONSE" | head -n 1 | grep -Fq '{"ok":true'
echo "$PAIR_RESPONSE" | tail -n 1 | grep -q '^200$'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]

RENAME_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    --data-urlencode "scope=roms" \
    --data-urlencode "tag=GBA" \
    --data-urlencode "from=$ORIGINAL_ROM" \
    --data-urlencode "to=$RENAMED_ROM" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/item/rename)"

echo "$RENAME_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"rename"}'
echo "$RENAME_RESPONSE" | tail -n 1 | grep -q '^200$'
test ! -f "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/$ORIGINAL_ROM"
test -f "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/$RENAMED_ROM"

CREATE_FOLDER_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    --data-urlencode "scope=files" \
    --data-urlencode "path=$FILES_FOLDER" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/folder/create)"

echo "$CREATE_FOLDER_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"create-folder"}'
echo "$CREATE_FOLDER_RESPONSE" | tail -n 1 | grep -q '^200$'
test -d "$SDCARD_ROOT/$FILES_FOLDER"
mkdir -p "$SDCARD_ROOT/$FILES_FOLDER/nested"
printf 'child payload' > "$SDCARD_ROOT/$FILES_FOLDER/nested/child.txt"

curl -sS -G \
    -b "$COOKIE_JAR" \
    --data-urlencode "scope=roms" \
    --data-urlencode "tag=GBA" \
    --data-urlencode "path=$RENAMED_ROM" \
    --data-urlencode "csrf=$CSRF_TOKEN" \
    http://127.0.0.1:8877/api/download \
    -o "$DOWNLOAD_PATH"
cmp -s "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/$RENAMED_ROM" "$DOWNLOAD_PATH"

DELETE_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    --data-urlencode "scope=roms" \
    --data-urlencode "tag=GBA" \
    --data-urlencode "path=$RENAMED_ROM" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/item/delete)"

echo "$DELETE_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"delete"}'
echo "$DELETE_RESPONSE" | tail -n 1 | grep -q '^200$'
test ! -f "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/$RENAMED_ROM"

DELETE_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    --data-urlencode "scope=files" \
    --data-urlencode "path=$FILES_FOLDER" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/item/delete)"

echo "$DELETE_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"delete"}'
echo "$DELETE_RESPONSE" | tail -n 1 | grep -q '^200$'
test ! -d "$SDCARD_ROOT/$FILES_FOLDER"

WRITE_TARGET="$SDCARD_ROOT/notes.txt"
printf 'before' > "$WRITE_TARGET"

WRITE_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -H "Content-Type: text/plain" \
    --data-binary 'after payload' \
    -w '\n%{http_code}' \
    "http://127.0.0.1:8877/api/item/write?scope=files&path=notes.txt")"

echo "$WRITE_RESPONSE" | head -n 1 | grep -Fq '{"ok":true,"action":"write"}'
echo "$WRITE_RESPONSE" | tail -n 1 | grep -q '^200$'
test "$(cat "$WRITE_TARGET")" = "after payload"

MISSING_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -H "Content-Type: text/plain" \
    --data-binary 'x' \
    -w '\n%{http_code}' \
    "http://127.0.0.1:8877/api/item/write?scope=files&path=does-not-exist.txt")"

echo "$MISSING_RESPONSE" | tail -n 1 | grep -q '^404$'

rm -f "$WRITE_TARGET"

echo "PASS file ops smoke"
