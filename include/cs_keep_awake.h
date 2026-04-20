#ifndef CS_KEEP_AWAKE_H
#define CS_KEEP_AWAKE_H

#include "cs_paths.h"

const char *cs_keep_awake_platform_name(void);
int cs_keep_awake_current_platform_uses_settings_override(void);
int cs_keep_awake_enable(const cs_paths *paths);
int cs_keep_awake_disable(const cs_paths *paths);

#endif
