#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PORT=8987

python3 -m http.server "$PORT" --bind 127.0.0.1 >/tmp/cs-preview-port-test.log 2>&1 &
LISTENER_PID=$!

cleanup() {
    kill "$LISTENER_PID" 2>/dev/null || true
    wait "$LISTENER_PID" 2>/dev/null || true
    rm -f /tmp/cs-preview-port-test.log
}

trap cleanup EXIT INT TERM

READY=0
for _ in $(seq 1 50); do
    if lsof -tiTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
        READY=1
        break
    fi
    sleep 0.1
done

if [ "$READY" -ne 1 ]; then
    echo "test listener did not become ready" >&2
    exit 1
fi

if make -C "$REPO_ROOT" --no-print-directory preview-clear-port PREVIEW_PORT="$PORT" >/tmp/cs-preview-clear-port.out 2>&1; then
    :
else
    cat /tmp/cs-preview-clear-port.out >&2
    exit 1
fi

if lsof -tiTCP:"$PORT" -sTCP:LISTEN >/dev/null 2>&1; then
    echo "preview-clear-port left listener running on $PORT" >&2
    cat /tmp/cs-preview-clear-port.out >&2
    exit 1
fi

grep -q "Stopping existing listener(s) on preview port $PORT" /tmp/cs-preview-clear-port.out

echo "PASS preview reclaims port"
