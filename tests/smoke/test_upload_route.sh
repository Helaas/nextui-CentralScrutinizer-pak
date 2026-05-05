#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-upload-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
SOURCE_ROM="$(find "fixtures/mock_sdcard/Roms/Game Boy Advance (GBA)" -maxdepth 1 -type f -name '*.gba' -print | head -n 1)"
SOURCE_SAVE="fixtures/mock_sdcard/Saves/GBA/Pokemon Emerald.sav"
SOURCE_BIOS="fixtures/mock_sdcard/Bios/GBA/gba_bios.bin"
SOURCE_NOTE="$WORK_DIR/source-note.txt"
SOURCE_NOTE_A="$WORK_DIR/source-note-a.txt"
SOURCE_NOTE_B="$WORK_DIR/source-note-b.txt"
ROM_UPLOAD_NAME="upload-$RANDOM-$$.gba"
SAVE_UPLOAD_NAME="upload-$RANDOM-$$.sav"
BIOS_UPLOAD_NAME="upload-$RANDOM-$$.bin"
NOTE_UPLOAD_NAME="upload-$RANDOM-$$.txt"
NOTE_UPLOAD_RELATIVE_PATH="Favorites/GBA/$NOTE_UPLOAD_NAME"
EMPTY_DIR_NAME="empty-dir-$RANDOM-$$"
MIXED_DIR_NAME="mixed-dir-$RANDOM-$$"
NORMALIZED_PARENT_NOTE_UPLOAD_NAME="normalized-parent-$RANDOM-$$.txt"
NORMALIZED_DOT_SEGMENT_NOTE_UPLOAD_NAME="normalized-dot-$RANDOM-$$.txt"
NORMALIZED_DOUBLE_SEPARATOR_NOTE_UPLOAD_NAME="normalized-double-$RANDOM-$$.txt"
MALFORMED_DIRECTORY_NAME="malformed-dir-$RANDOM-$$"
UPLOADED_ROM="$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/$ROM_UPLOAD_NAME"
UPLOADED_SAVE="$SDCARD_ROOT/Saves/GBA/$SAVE_UPLOAD_NAME"
UPLOADED_BIOS="$SDCARD_ROOT/Bios/GBA/$BIOS_UPLOAD_NAME"
UPLOADED_NOTE="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/Favorites/GBA/$NOTE_UPLOAD_NAME"
UPLOADED_EMPTY_DIR="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/$EMPTY_DIR_NAME"
UPLOADED_EMPTY_NESTED_DIR="$UPLOADED_EMPTY_DIR/Nested"
UPLOADED_MIXED_EMPTY_DIR="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/$MIXED_DIR_NAME/Empty"
UPLOADED_MIXED_NOTE="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/$MIXED_DIR_NAME/$NOTE_UPLOAD_NAME"
NORMALIZED_PARENT_UPLOADED_NOTE="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/$NORMALIZED_PARENT_NOTE_UPLOAD_NAME"
NORMALIZED_DOT_SEGMENT_UPLOADED_NOTE="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/$NORMALIZED_DOT_SEGMENT_NOTE_UPLOAD_NAME"
NORMALIZED_DOUBLE_SEPARATOR_UPLOADED_NOTE="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/Favorites/$NORMALIZED_DOUBLE_SEPARATOR_NOTE_UPLOAD_NAME"
EMPTY_FILENAME_NOTE="$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/empty-filename-should-not-exist.txt"
PARALLEL_CLIENT_NAME="parallel-$RANDOM-$$.txt"
PARALLEL_PATH_A=".userdata/shared/CentralScrutinizer/imports/parallel-a"
PARALLEL_PATH_B=".userdata/shared/CentralScrutinizer/imports/parallel-b"
PARALLEL_UPLOAD_A="$SDCARD_ROOT/$PARALLEL_PATH_A/$PARALLEL_CLIENT_NAME"
PARALLEL_UPLOAD_B="$SDCARD_ROOT/$PARALLEL_PATH_B/$PARALLEL_CLIENT_NAME"
CSRF_TOKEN=""

[ -n "$SOURCE_ROM" ]

assert_upload_rejected() {
    local scope="$1"
    local path_value="$2"
    local client_name="$3"
    local unexpected_path="$4"
    local curl_args=(
        -sS
        -X POST
        -b "$COOKIE_JAR"
        -H "X-CS-CSRF: $CSRF_TOKEN"
        -F "scope=$scope"
    )
    local upload_response=""

    if [ -n "$path_value" ]; then
        curl_args+=(-F "path=$path_value")
    fi
    curl_args+=(
        -F "file=@$SOURCE_NOTE;filename=$client_name"
        -w '\n%{http_code}'
        http://127.0.0.1:8877/api/upload
    )

    upload_response="$(curl "${curl_args[@]}")"

    echo "$upload_response" | head -n 1 | grep -Fq '{"ok":false}'
    echo "$upload_response" | tail -n 1 | grep -q '^400$'
    test ! -e "$unexpected_path"
}

assert_upload_stored() {
    local client_name="$1"
    local expected_path="$2"
    local upload_response=""

    upload_response="$(curl -sS -X POST \
        -b "$COOKIE_JAR" \
        -H "X-CS-CSRF: $CSRF_TOKEN" \
        -F "scope=files" \
        -F "path=.userdata/shared/CentralScrutinizer/imports" \
        -F "file=@$SOURCE_NOTE;filename=$client_name" \
        -w '\n%{http_code}' \
        http://127.0.0.1:8877/api/upload)"

    echo "$upload_response" | head -n 1 | grep -Fq '{"ok":true}'
    echo "$upload_response" | tail -n 1 | grep -q '^200$'
    test -f "$expected_path"
    cmp -s "$SOURCE_NOTE" "$expected_path"
}

assert_directory_rejected() {
    local scope="$1"
    local path_value="$2"
    local directory_name="$3"
    local unexpected_path="$4"
    local upload_response=""

    upload_response="$(curl -sS -X POST \
        -b "$COOKIE_JAR" \
        -H "X-CS-CSRF: $CSRF_TOKEN" \
        -F "scope=$scope" \
        -F "path=$path_value" \
        -F "directory=$directory_name" \
        -w '\n%{http_code}' \
        http://127.0.0.1:8877/api/upload)"

    echo "$upload_response" | head -n 1 | grep -Fq '{"ok":false}'
    echo "$upload_response" | tail -n 1 | grep -q '^400$'
    test ! -e "$unexpected_path"
}

prepare_mock_sdcard "$SDCARD_ROOT"
# Simulate a fresh SD card with no pre-existing shared-state dir so
# cs_upload_prepare_temp_root has to create the full chain itself.
rm -rf "$SDCARD_ROOT/.userdata/shared/CentralScrutinizer"
printf 'Central Scrutinizer file-browser upload\n' > "$SOURCE_NOTE"
python3 - <<PY
from pathlib import Path

Path("$SOURCE_NOTE_A").write_text("A" * 262144 + "\\n", encoding="utf-8")
Path("$SOURCE_NOTE_B").write_text("B" * 262144 + "\\n", encoding="utf-8")
PY

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

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -F "scope=roms" \
    -F "tag=GBA" \
    -F "file=@$SOURCE_ROM;filename=$ROM_UPLOAD_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^403$'
test ! -f "$UPLOADED_ROM"

PAIR_RESPONSE="$(curl -sS -c "$COOKIE_JAR" -X POST --data "browser_id=upload-browser&code=7391" -w '\n%{http_code}' http://127.0.0.1:8877/api/pair)"
echo "$PAIR_RESPONSE" | head -n 1 | grep -Fq '{"ok":true'
echo "$PAIR_RESPONSE" | tail -n 1 | grep -q '^200$'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=roms" \
    -F "tag=GBA" \
    -F "file=@$SOURCE_ROM;filename=$ROM_UPLOAD_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":true}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^200$'
test -f "$UPLOADED_ROM"
cmp -s "$SOURCE_ROM" "$UPLOADED_ROM"

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=saves" \
    -F "tag=GBA" \
    -F "file=@$SOURCE_SAVE;filename=$SAVE_UPLOAD_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":true}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^200$'
test -f "$UPLOADED_SAVE"
cmp -s "$SOURCE_SAVE" "$UPLOADED_SAVE"

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=bios" \
    -F "tag=GBA" \
    -F "file=@$SOURCE_BIOS;filename=$BIOS_UPLOAD_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":true}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^200$'
test -f "$UPLOADED_BIOS"
cmp -s "$SOURCE_BIOS" "$UPLOADED_BIOS"

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=.userdata/shared/CentralScrutinizer/imports" \
    -F "file=@$SOURCE_NOTE;filename=$NOTE_UPLOAD_RELATIVE_PATH" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":true}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^200$'
test -f "$UPLOADED_NOTE"
cmp -s "$SOURCE_NOTE" "$UPLOADED_NOTE"

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=.userdata/shared/CentralScrutinizer/imports" \
    -F "directory=$EMPTY_DIR_NAME" \
    -F "directory=$EMPTY_DIR_NAME/Nested" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":true}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^200$'
test -d "$UPLOADED_EMPTY_DIR"
test -d "$UPLOADED_EMPTY_NESTED_DIR"

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=.userdata/shared/CentralScrutinizer/imports" \
    -F "directory=$EMPTY_DIR_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":true}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^200$'

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=.userdata/shared/CentralScrutinizer/imports" \
    -F "directory=$MIXED_DIR_NAME/Empty" \
    -F "file=@$SOURCE_NOTE;filename=$MIXED_DIR_NAME/$NOTE_UPLOAD_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":true}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^200$'
test -d "$UPLOADED_MIXED_EMPTY_DIR"
test -f "$UPLOADED_MIXED_NOTE"
cmp -s "$SOURCE_NOTE" "$UPLOADED_MIXED_NOTE"

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=.userdata/shared/CentralScrutinizer/imports" \
    -F "file=@$SOURCE_NOTE_B;filename=$NOTE_UPLOAD_RELATIVE_PATH" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^500$'
cmp -s "$SOURCE_NOTE" "$UPLOADED_NOTE"

# CivetWeb normalizes some malformed multipart filenames before our callback
# sees them, so these uploads must stay rooted inside the requested import
# directory when the client name is cleaned up by the parser.
assert_upload_stored "../$NORMALIZED_PARENT_NOTE_UPLOAD_NAME" "$NORMALIZED_PARENT_UPLOADED_NOTE"
assert_upload_stored "Favorites//$NORMALIZED_DOUBLE_SEPARATOR_NOTE_UPLOAD_NAME" "$NORMALIZED_DOUBLE_SEPARATOR_UPLOADED_NOTE"

assert_upload_rejected "files" ".userdata/shared/CentralScrutinizer/imports" "Favorites/../$NORMALIZED_DOT_SEGMENT_NOTE_UPLOAD_NAME" \
    "$NORMALIZED_DOT_SEGMENT_UPLOADED_NOTE"
assert_upload_rejected "files" ".userdata/shared/CentralScrutinizer/imports" "/abs/$NOTE_UPLOAD_NAME" \
    "$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/abs/$NOTE_UPLOAD_NAME"
assert_upload_rejected "files" ".userdata/shared/CentralScrutinizer/imports" "$MALFORMED_DIRECTORY_NAME/" \
    "$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/imports/$MALFORMED_DIRECTORY_NAME"
assert_upload_rejected "roms" "" ".hidden/$NOTE_UPLOAD_NAME" \
    "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/.hidden/$NOTE_UPLOAD_NAME"
assert_directory_rejected "files" ".userdata/shared/CentralScrutinizer/imports" "../dir-escape" \
    "$SDCARD_ROOT/.userdata/shared/CentralScrutinizer/dir-escape"
assert_directory_rejected "roms" "" ".hidden" \
    "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/.hidden"

UPLOAD_RESPONSE="$(curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=.userdata/shared/CentralScrutinizer/imports" \
    -F "file=@$SOURCE_NOTE;filename=" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload)"

echo "$UPLOAD_RESPONSE" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UPLOAD_RESPONSE" | tail -n 1 | grep -q '^400$'
test ! -e "$EMPTY_FILENAME_NOTE"

PARALLEL_RESPONSE_A="$WORK_DIR/parallel-a.response"
PARALLEL_RESPONSE_B="$WORK_DIR/parallel-b.response"
curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=$PARALLEL_PATH_A" \
    -F "file=@$SOURCE_NOTE_A;filename=$PARALLEL_CLIENT_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload > "$PARALLEL_RESPONSE_A" &
PARALLEL_PID_A=$!
curl -sS -X POST \
    -b "$COOKIE_JAR" \
    -H "X-CS-CSRF: $CSRF_TOKEN" \
    -F "scope=files" \
    -F "path=$PARALLEL_PATH_B" \
    -F "file=@$SOURCE_NOTE_B;filename=$PARALLEL_CLIENT_NAME" \
    -w '\n%{http_code}' \
    http://127.0.0.1:8877/api/upload > "$PARALLEL_RESPONSE_B" &
PARALLEL_PID_B=$!

wait "$PARALLEL_PID_A"
wait "$PARALLEL_PID_B"

echo "$(head -n 1 "$PARALLEL_RESPONSE_A")" | grep -Fq '{"ok":true}'
echo "$(tail -n 1 "$PARALLEL_RESPONSE_A")" | grep -q '^200$'
echo "$(head -n 1 "$PARALLEL_RESPONSE_B")" | grep -Fq '{"ok":true}'
echo "$(tail -n 1 "$PARALLEL_RESPONSE_B")" | grep -q '^200$'
test -f "$PARALLEL_UPLOAD_A"
test -f "$PARALLEL_UPLOAD_B"
cmp -s "$SOURCE_NOTE_A" "$PARALLEL_UPLOAD_A"
cmp -s "$SOURCE_NOTE_B" "$PARALLEL_UPLOAD_B"

echo "PASS upload route smoke"
