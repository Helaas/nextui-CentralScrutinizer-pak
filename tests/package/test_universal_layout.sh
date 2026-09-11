#!/bin/bash
set -euo pipefail

ARCHIVE="${1:?release archive is required}"
PACKAGE_DIR="${2:?packaged universal directory is required}"
WORK_DIR="$(mktemp -d /tmp/cs-universal-package-XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT

# A Pak Store archive must install directly into the pak directory.
unzip -Z1 "$ARCHIVE" > "$WORK_DIR/entries"
if grep -Ev '^(central-scrutinizer|launch\.sh|pak\.json|resources/.*)$' "$WORK_DIR/entries"; then
    echo "unexpected path in universal release archive" >&2
    exit 1
fi
unzip -q "$ARCHIVE" -d "$WORK_DIR/extracted"
for name in central-scrutinizer launch.sh; do
    test -x "$WORK_DIR/extracted/$name"
done
test -f "$WORK_DIR/extracted/pak.json"
test -f "$WORK_DIR/extracted/resources/web/index.html"
diff -qr "$PACKAGE_DIR" "$WORK_DIR/extracted"
echo "PASS universal release archive layout, permissions, and contents"
