#ifndef CS_PATHS_H
#define CS_PATHS_H

#define CS_PATH_MAX 1024

typedef struct cs_paths {
    char sdcard_root[CS_PATH_MAX];
    char shared_state_root[CS_PATH_MAX];
    char web_root[CS_PATH_MAX];
    char roms_root[CS_PATH_MAX];
    char saves_root[CS_PATH_MAX];
    char bios_root[CS_PATH_MAX];
    char overlays_root[CS_PATH_MAX];
    char cheats_root[CS_PATH_MAX];
    char collections_root[CS_PATH_MAX];
    char screenshots_root[CS_PATH_MAX];
    char temp_upload_root[CS_PATH_MAX];
} cs_paths;

int cs_paths_init(cs_paths *paths);

#endif
