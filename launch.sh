#!/bin/sh
set -eu

APP_BIN="central-scrutinizer"
PAK_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

cd "$PAK_DIR"

SDCARD_ROOT=${SDCARD_PATH:-/mnt/SDCARD}
export SDCARD_PATH="$SDCARD_ROOT"

if [ -n "${PLATFORM:-}" ] && [ -z "${USERDATA_PATH:-}" ]; then
    export USERDATA_PATH="$SDCARD_ROOT/.userdata/$PLATFORM"
fi

if [ -n "${PLATFORM:-}" ] && [ -z "${SYSTEM_PATH:-}" ]; then
    export SYSTEM_PATH="$SDCARD_ROOT/.system/$PLATFORM"
fi

if [ -z "${SHARED_USERDATA_PATH:-}" ]; then
    export SHARED_USERDATA_PATH="$SDCARD_ROOT/.userdata/shared"
fi

if [ -z "${ROMS_PATH:-}" ]; then
    export ROMS_PATH="$SDCARD_ROOT/Roms"
fi

if [ -z "${SAVES_PATH:-}" ]; then
    export SAVES_PATH="$SDCARD_ROOT/Saves"
fi

if [ -z "${BIOS_PATH:-}" ]; then
    export BIOS_PATH="$SDCARD_ROOT/Bios"
fi

if [ -n "${SYSTEM_PATH:-}" ] && [ -z "${CORES_PATH:-}" ]; then
    export CORES_PATH="$SYSTEM_PATH/cores"
fi

if [ -n "${USERDATA_PATH:-}" ] && [ -z "${LOGS_PATH:-}" ]; then
    export LOGS_PATH="$USERDATA_PATH/logs"
fi

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
