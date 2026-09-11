#!/bin/bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK_DIR="$(mktemp -d /tmp/cs-build-failures-XXXXXX)"
trap 'rm -rf "$WORK_DIR"' EXIT
cp "$REPO_ROOT/Makefile" "$WORK_DIR/Makefile"
mkdir -p "$WORK_DIR/tests/smoke"
printf 'exit 1\n' > "$WORK_DIR/tests/smoke/test_1.sh"
printf 'touch should-not-run\n' > "$WORK_DIR/tests/smoke/test_2.sh"
if make -C "$WORK_DIR" test-smoke > "$WORK_DIR/smoke.log" 2>&1; then
    echo "smoke runner hid a test failure" >&2
    exit 1
fi
test ! -e "$WORK_DIR/should-not-run"

cat > "$WORK_DIR/adb" <<'EOF'
#!/bin/sh
# Exercise a failed remote shell command and a failed transfer separately.
[ "$3" != "$FAIL_ADB_COMMAND" ]
EOF
chmod +x "$WORK_DIR/adb"
for command in shell push; do
    if FAIL_ADB_COMMAND="$command" make -C "$WORK_DIR" -o package-universal \
        deploy-platform PLATFORM=h700 SERIAL=test ADB="$WORK_DIR/adb" \
        > "$WORK_DIR/deploy.log" 2>&1; then
        echo "deploy hid an adb $command failure" >&2
        exit 1
    fi
    if grep -q 'Deploy complete' "$WORK_DIR/deploy.log"; then
        echo "deploy reported success after an adb failure" >&2
        exit 1
    fi
done
echo "PASS build and deployment failure reporting"
