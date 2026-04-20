#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "cs_ui.h"

int main(void) {
    cs_ui_model model = {0};

    cs_ui_model_make_active(&model, "192.168.1.42", 8877, "4827", 1, 1);
    assert(strcmp(model.ip, "192.168.1.42") == 0);
    assert(strcmp(model.code, "4827") == 0);
    assert(model.trusted_browser_count == 1);
    assert(model.terminal_enabled == 1);

    cs_ui_model_make_offline(&model);
    assert(model.is_offline == 1);
    assert(strcmp(model.status_message, "Connect Wi-Fi in NextUI first.") == 0);

    assert(setenv("CS_PLATFORM_NAME_OVERRIDE", "my355", 1) == 0);
    assert(cs_ui_keep_awake_enable_requires_confirmation() == 1);
    assert(strstr(cs_ui_keep_awake_enable_warning_message(), "NextUI's Screen timeout") != NULL);

    assert(setenv("CS_PLATFORM_NAME_OVERRIDE", "mac", 1) == 0);
    assert(cs_ui_keep_awake_enable_requires_confirmation() == 0);

    return 0;
}
