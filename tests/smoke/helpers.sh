#!/bin/bash

CS_SMOKE_HELPERS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CS_SMOKE_REPO_ROOT="$(cd "$CS_SMOKE_HELPERS_DIR/../.." && pwd)"

prepare_mock_sdcard() {
    local sdcard_root="${1:?sdcard root is required}"
    local trust_store_path="$sdcard_root/.userdata/shared/CentralScrutinizer/trusted-clients.json"

    mkdir -p "$sdcard_root"
    cp -R "$CS_SMOKE_REPO_ROOT/fixtures/mock_sdcard/." "$sdcard_root/"
    mkdir -p "$(dirname "$trust_store_path")"
    printf '{"clients":[]}\n' > "$trust_store_path"
}
