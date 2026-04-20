#!/bin/bash
set -euo pipefail

. "$(cd "$(dirname "$0")" && pwd)/helpers.sh"

make mac >/dev/null
make web-build >/dev/null

WORK_DIR="$(mktemp -d /tmp/cs-daemon-smoke-XXXXXX)"
SDCARD_ROOT="$WORK_DIR/sdcard"
LOG_FILE="$WORK_DIR/daemon-handoff.log"
PORT=8893
APP_BIN="$CS_SMOKE_REPO_ROOT/build/mac/central-scrutinizer"
WEB_ROOT="$CS_SMOKE_REPO_ROOT/web/out"

prepare_mock_sdcard "$SDCARD_ROOT"

cleanup() {
    rm -rf "$WORK_DIR"
}

trap cleanup EXIT INT TERM

export PORT APP_BIN WEB_ROOT SDCARD_ROOT LOG_FILE

python3 - <<'PY'
import http.cookiejar
import json
import os
import signal
import socket
import subprocess
import time
import urllib.parse
import urllib.request

app_bin = os.environ["APP_BIN"]
web_root = os.environ["WEB_ROOT"]
sdcard_root = os.environ["SDCARD_ROOT"]
log_file = os.environ["LOG_FILE"]
port = int(os.environ["PORT"])

log = open(log_file, "ab", buffering=0)

settings_path = os.path.join(sdcard_root, ".userdata", "shared", "CentralScrutinizer", "settings.json")
nextui_settings_path = os.path.join(sdcard_root, ".userdata", "shared", "minuisettings.txt")
keep_awake_state_path = os.path.join(sdcard_root, ".userdata", "shared", "CentralScrutinizer", "nextui-keep-awake-state.txt")
os.makedirs(os.path.dirname(settings_path), exist_ok=True)
with open(settings_path, "w", encoding="utf-8") as handle:
    handle.write('{"terminal_enabled":false,"keep_awake_in_background":true}')
with open(nextui_settings_path, "w", encoding="utf-8") as handle:
    handle.write("volume=7\nscreentimeout=60\n")

def wait_http(path: str, timeout: float = 5.0) -> str:
    deadline = time.time() + timeout
    last_error = None
    while time.time() < deadline:
        try:
            with urllib.request.urlopen(f"http://127.0.0.1:{port}{path}", timeout=0.5) as response:
                return response.read().decode()
        except Exception as exc:  # noqa: BLE001
            last_error = exc
            time.sleep(0.05)
    raise RuntimeError(f"request to {path} never became ready: {last_error}")

def wait_listener_stopped(timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.settimeout(0.2)
            try:
                sock.connect(("127.0.0.1", port))
            except OSError:
                return
        time.sleep(0.05)
    raise RuntimeError("foreground listener never stopped")

def wait_for_daemon_pid(state_path: str, timeout: float = 5.0) -> int:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            with open(state_path, "r", encoding="utf-8") as handle:
                state = json.load(handle)
            pid = int(state.get("pid", 0))
            if pid > 0:
                return pid
        except Exception:  # noqa: BLE001
            pass
        time.sleep(0.05)
    raise RuntimeError("daemon state was not persisted")

def wait_for_pid_exit(pid: int, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            os.kill(pid, 0)
        except OSError:
            return
        time.sleep(0.05)
    raise RuntimeError(f"pid {pid} did not exit")

def wait_for_path_state(path: str, should_exist: bool, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    while time.time() < deadline:
        if os.path.exists(path) == should_exist:
            return
        time.sleep(0.05)
    expected = "exist" if should_exist else "be removed"
    raise RuntimeError(f"path {path} did not {expected}")

def wait_for_nextui_settings(timeout_value: int, expect_marker: bool, timeout: float = 5.0) -> None:
    deadline = time.time() + timeout
    timeout_line = f"screentimeout={timeout_value}"
    marker_line = "centralscrutinizer_keepawake=1"
    while time.time() < deadline:
        try:
            with open(nextui_settings_path, "r", encoding="utf-8") as handle:
                contents = handle.read()
        except FileNotFoundError:
            contents = None
        if contents is not None and timeout_line in contents and ((marker_line in contents) == expect_marker):
            return
        time.sleep(0.05)
    raise RuntimeError(
        f"NextUI settings never reached timeout {timeout_value} with marker={expect_marker}"
    )

def assert_nextui_settings_equal(expected: str) -> None:
    with open(nextui_settings_path, "r", encoding="utf-8") as handle:
        contents = handle.read()
    assert contents == expected, contents

foreground = subprocess.Popen(
    [
        app_bin,
        "--headless",
        "--port",
        str(port),
        "--web-root",
        web_root,
        "--sdcard",
        sdcard_root,
    ],
    env={**os.environ, "CS_PAIRING_CODE": "7391", "CS_PLATFORM_NAME_OVERRIDE": "my355"},
    stdout=log,
    stderr=log,
)

daemon = None
reclaim = None
daemon_pid = None

try:
    wait_http("/api/status")

    cookie_jar = http.cookiejar.CookieJar()
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cookie_jar))
    pair_body = urllib.parse.urlencode({"browser_id": "daemon-smoke", "code": "7391"}).encode()
    pair_request = urllib.request.Request(
        f"http://127.0.0.1:{port}/api/pair",
        data=pair_body,
        headers={"Content-Type": "application/x-www-form-urlencoded"},
        method="POST",
    )
    with opener.open(pair_request, timeout=2) as response:
        pair_response = response.read().decode()
        assert '"ok":true' in pair_response

    start_r, start_w = os.pipe()
    ready_r, ready_w = os.pipe()
    daemon_launcher = subprocess.Popen(
        [
            "python3",
            "-c",
            (
                "import os, subprocess, sys; "
                "log = open(os.environ['DAEMON_LOG'], 'ab', buffering=0); "
                "subprocess.Popen([os.environ['DAEMON_APP'], '--headless', '--daemonized', '--port', os.environ['DAEMON_PORT'], "
                "'--web-root', os.environ['DAEMON_WEB_ROOT'], '--sdcard', os.environ['DAEMON_SDCARD_ROOT'], "
                "'--daemon-start-fd', os.environ['DAEMON_START_FD'], '--daemon-ready-fd', os.environ['DAEMON_READY_FD']], "
                "pass_fds=(int(os.environ['DAEMON_START_FD']), int(os.environ['DAEMON_READY_FD'])), stdout=log, stderr=log); "
                "sys.exit(0)"
            ),
        ],
        pass_fds=(start_r, ready_w),
        env={
            **os.environ,
            "DAEMON_APP": app_bin,
            "DAEMON_LOG": log_file,
            "DAEMON_PORT": str(port),
            "DAEMON_WEB_ROOT": web_root,
            "DAEMON_SDCARD_ROOT": sdcard_root,
            "DAEMON_START_FD": str(start_r),
            "DAEMON_READY_FD": str(ready_w),
            "CS_PLATFORM_NAME_OVERRIDE": "my355",
        },
        stdout=log,
        stderr=log,
    )
    os.close(start_r)
    os.close(ready_w)
    daemon_launcher.wait(timeout=5)

    foreground.send_signal(signal.SIGTERM)
    foreground.wait(timeout=5)
    wait_listener_stopped()

    os.write(start_w, b"1")
    os.close(start_w)
    if os.read(ready_r, 1) != b"1":
        raise RuntimeError("daemonized server did not report ready")
    os.close(ready_r)
    daemon_state_path = os.path.join(sdcard_root, ".userdata", "shared", "CentralScrutinizer", "daemon-state.json")
    daemon_pid = wait_for_daemon_pid(daemon_state_path)
    wait_for_path_state(keep_awake_state_path, True)
    wait_for_nextui_settings(0, True)

    if os.path.isdir(f"/proc/{daemon_pid}/fd"):
        for fd in (0, 1, 2):
            target = os.readlink(f"/proc/{daemon_pid}/fd/{fd}")
            assert target == "/dev/null", f"daemon fd {fd} -> {target}, expected /dev/null"

    with opener.open(f"http://127.0.0.1:{port}/api/session", timeout=2) as response:
        session_response = response.read().decode()
        assert '"paired":true' in session_response
        assert '"pairingAvailable":false' in session_response

    reclaim = subprocess.Popen(
        [
            app_bin,
            "--headless",
            "--port",
            str(port),
            "--web-root",
            web_root,
            "--sdcard",
            sdcard_root,
        ],
        env={**os.environ, "CS_PAIRING_CODE": "7391", "CS_PLATFORM_NAME_OVERRIDE": "my355"},
        stdout=log,
        stderr=log,
    )
    wait_for_pid_exit(daemon_pid)
    wait_for_path_state(keep_awake_state_path, False)
    assert_nextui_settings_equal("volume=7\nscreentimeout=60\n")
    wait_http("/api/status")

    with opener.open(f"http://127.0.0.1:{port}/api/session", timeout=2) as response:
        session_response = response.read().decode()
        assert '"paired":true' in session_response
        assert '"pairingAvailable":true' in session_response

    reclaim.send_signal(signal.SIGTERM)
    reclaim.wait(timeout=5)
finally:
    if reclaim and reclaim.poll() is None:
        reclaim.kill()
        reclaim.wait(timeout=5)
    if foreground.poll() is None:
        foreground.kill()
        foreground.wait(timeout=5)
    if daemon_pid is not None:
        try:
            os.kill(daemon_pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            wait_for_pid_exit(daemon_pid)
        except Exception:  # noqa: BLE001
            pass
    log.close()

print("PASS daemon handoff smoke")
PY
