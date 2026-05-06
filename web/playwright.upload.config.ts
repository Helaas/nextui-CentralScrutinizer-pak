import { defineConfig, devices } from "@playwright/test";

export default defineConfig({
  testDir: "./tests",
  testMatch: /upload-folders\.spec\.ts/,
  projects: [
    { name: "chromium", use: { ...devices["Desktop Chrome"] } },
    { name: "firefox", use: { ...devices["Desktop Firefox"] } },
    { name: "webkit", use: { ...devices["Desktop Safari"] } },
  ],
  use: {
    baseURL: "http://127.0.0.1:8877",
    headless: true,
  },
  webServer: {
    command:
      "bash -lc 'set -euo pipefail; make -C .. mac >/dev/null; npm --prefix . run build >/dev/null; WORK_DIR=\"$(mktemp -d /tmp/cs-playwright-upload-XXXXXX)\"; cleanup() { rm -rf \"$WORK_DIR\"; }; trap cleanup EXIT INT TERM; source ../tests/smoke/helpers.sh; prepare_mock_sdcard \"$WORK_DIR/sdcard\"; CS_PAIRING_CODE=7391 CS_PAIRING_CODE_REUSE=1 ../build/mac/central-scrutinizer --headless --port 8877 --web-root ../web/out --sdcard \"$WORK_DIR/sdcard\"'",
    reuseExistingServer: false,
    timeout: 120000,
    url: "http://127.0.0.1:8877/api/status",
  },
});
