#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$REPO_ROOT"

make web-build >/tmp/cs-web-build.log

set -- web/out/_next/static/chunks/*.css
if [ ! -e "$1" ]; then
    echo "no exported css bundle found" >&2
    exit 1
fi

if ! rg -q '\.mx-auto\b' "$@"; then
    echo "expected Tailwind utility .mx-auto in exported css" >&2
    exit 1
fi

if ! rg -q '\.h-10\b' "$@"; then
    echo "expected Tailwind utility .h-10 in exported css" >&2
    exit 1
fi

echo "PASS web export css"
