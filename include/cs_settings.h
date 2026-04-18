#ifndef CS_SETTINGS_H
#define CS_SETTINGS_H

#include <stddef.h>

#include "cs_paths.h"

typedef struct cs_settings {
    int terminal_enabled;
} cs_settings;

int cs_settings_default_terminal_enabled(void);
int cs_settings_make_path(const cs_paths *paths, char *buffer, size_t buffer_len);
int cs_settings_load(const cs_paths *paths, cs_settings *settings);
int cs_settings_save(const cs_paths *paths, const cs_settings *settings);

#endif
