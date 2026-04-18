import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  use: {
    baseURL: "http://127.0.0.1:8877",
    headless: true,
  },
  webServer: {
    // These specs rely on a deterministic preview PIN and isolated server state.
    // Each Playwright invocation gets a fresh copied fixture so one spec cannot
    // consume the fixed code or mutate trust state for the next one.
    command:
      "bash -lc 'set -euo pipefail; make -C .. mac >/dev/null; npm --prefix . run build >/dev/null; WORK_DIR=\"$(mktemp -d /tmp/cs-playwright-XXXXXX)\"; cleanup() { rm -rf \"$WORK_DIR\"; }; trap cleanup EXIT INT TERM; source ../tests/smoke/helpers.sh; prepare_mock_sdcard \"$WORK_DIR/sdcard\"; CS_PAIRING_CODE=7391 CS_PAIRING_CODE_REUSE=1 ../build/mac/central-scrutinizer --headless --port 8877 --web-root ../web/out --sdcard \"$WORK_DIR/sdcard\"'",
    reuseExistingServer: false,
    timeout: 120000,
    url: "http://127.0.0.1:8877/api/status",
  },
});
