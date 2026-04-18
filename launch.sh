#!/bin/sh
set -eu

APP_BIN="central-scrutinizer"
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

cd "$PAK_DIR"

if [ -z "${CS_WEB_ROOT:-}" ] && [ -d "$PAK_DIR/resources/web" ]; then
    export CS_WEB_ROOT="$PAK_DIR/resources/web"
fi

if [ -n "${SHARED_USERDATA_PATH:-}" ]; then
    SHARED_USERDATA_ROOT="$SHARED_USERDATA_PATH"
elif [ -d "/mnt/SDCARD/.userdata/shared" ] || [ -d "/mnt/SDCARD" ]; then
    SHARED_USERDATA_ROOT="/mnt/SDCARD/.userdata/shared"
else
    SHARED_USERDATA_ROOT="${HOME:-/tmp}/.userdata/shared"
fi

LOG_ROOT=${LOGS_PATH:-"$SHARED_USERDATA_ROOT/logs"}
mkdir -p "$LOG_ROOT"
LOG_FILE="$LOG_ROOT/$APP_BIN.txt"
: >"$LOG_FILE"

exec >>"$LOG_FILE"
exec 2>&1

echo "=== Launching Central Scrutinizer at $(date) ==="
echo "platform=${PLATFORM:-unknown} device=${DEVICE:-unknown}"
echo "args: $*"

exec "./$APP_BIN" "$@"
