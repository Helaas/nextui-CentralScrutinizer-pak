#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-library-routes-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
COOKIE_JAR="$WORK_DIR/cookies.txt"
ROM_NAME=""
ROM_BASE=""
CSRF_TOKEN=""

prepare_mock_sdcard "$SDCARD_ROOT"

ROM_NAME="$(find "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)" -maxdepth 1 -type f -name '*.gba' -print | sed 's#.*/##' | head -n 1)"
ROM_BASE="${ROM_NAME%.*}"
[ -n "$ROM_NAME" ]
mkdir -p "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/.media"
printf 'png' > "$SDCARD_ROOT/Roms/Game Boy Advance (GBA)/.media/$ROM_BASE.png"

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

UNAUTH_PLATFORMS="$(curl -s 'http://127.0.0.1:8877/api/platforms' -w '\n%{http_code}')"
echo "$UNAUTH_PLATFORMS" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UNAUTH_PLATFORMS" | tail -n 1 | grep -q '^403$'

UNAUTH_BROWSER="$(curl -s 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA' -w '\n%{http_code}')"
echo "$UNAUTH_BROWSER" | head -n 1 | grep -Fq '{"ok":false}'
echo "$UNAUTH_BROWSER" | tail -n 1 | grep -q '^403$'

curl -sf -c "$COOKIE_JAR" -X POST --data "browser_id=library-browser&code=7391" http://127.0.0.1:8877/api/pair | grep -q '"ok":true'

SESSION_RESPONSE="$(curl -sS -b "$COOKIE_JAR" http://127.0.0.1:8877/api/session)"
CSRF_TOKEN="$(printf '%s' "$SESSION_RESPONSE" | sed -n 's/.*"csrf":"\([^"]*\)".*/\1/p')"
[ -n "$CSRF_TOKEN" ]

curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" http://127.0.0.1:8877/api/platforms | grep -Fq '"tag":"GBA"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" http://127.0.0.1:8877/api/platforms | grep -Fq '"name":"Game Boy Advance"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" http://127.0.0.1:8877/api/platforms | grep -Fq '"group":"Nintendo"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" http://127.0.0.1:8877/api/platforms | grep -Fq '"counts":{"roms":1,"saves":1,"states":5,"bios":1,"overlays":0,"cheats":0}'

curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA' | grep -Fq "\"name\":\"$ROM_NAME\""
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=GBA' | grep -Fq "\"thumbnailPath\":\".media/$ROM_BASE.png\""
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=saves&tag=GBA' | grep -Fq '"name":"Pokemon Emerald.sav"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=bios&tag=PS' | grep -Fq '"name":"scph1001.bin"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=files' | grep -Fq '"name":".userdata"'
curl -sf -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=files' | grep -Fq '"name":"Roms"'

INVALID_SCOPE="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=bogus' -w '\n%{http_code}')"
echo "$INVALID_SCOPE" | head -n 1 | grep -Fq '"error":"invalid_scope"'
echo "$INVALID_SCOPE" | tail -n 1 | grep -q '^400$'

MISSING_PLATFORM="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=roms&tag=NOPE' -w '\n%{http_code}')"
echo "$MISSING_PLATFORM" | head -n 1 | grep -Fq '"error":"platform_not_found"'
echo "$MISSING_PLATFORM" | tail -n 1 | grep -q '^404$'

TRAVERSAL="$(curl -s -b "$COOKIE_JAR" -H "X-CS-CSRF: $CSRF_TOKEN" 'http://127.0.0.1:8877/api/browser?scope=files&path=..%2Foutside' -w '\n%{http_code}')"
echo "$TRAVERSAL" | head -n 1 | grep -Fq '"error":"path_not_found"'
echo "$TRAVERSAL" | tail -n 1 | grep -q '^404$'

echo "PASS library smoke"
