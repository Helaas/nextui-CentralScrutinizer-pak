#ifndef CS_LIBRARY_H
#define CS_LIBRARY_H

#include <stddef.h>

#include "cs_paths.h"
#include "cs_platforms.h"

#define CS_BROWSER_PAGE_SIZE 512
#define CS_BROWSER_SCAN_CAP 4096
#define CS_BROWSER_MAX_BREADCRUMBS 32
#define CS_BROWSER_QUERY_MAX 256

typedef enum cs_browser_scope {
    CS_SCOPE_INVALID = -1,
    CS_SCOPE_ROMS = 0,
    CS_SCOPE_SAVES = 1,
    CS_SCOPE_BIOS = 2,
    CS_SCOPE_OVERLAYS = 3,
    CS_SCOPE_CHEATS = 4,
    CS_SCOPE_FILES = 5,
} cs_browser_scope;

typedef struct cs_browser_entry {
    char name[256];
    char path[CS_PATH_MAX];
    char type[32];
    unsigned long long size;
    long long modified;
    char status[32];
    char thumbnail_path[CS_PATH_MAX];
} cs_browser_entry;

typedef struct cs_browser_breadcrumb {
    char label[256];
    char path[CS_PATH_MAX];
} cs_browser_breadcrumb;

typedef struct cs_browser_result {
    char scope[16];
    char title[256];
    char root_path[CS_PATH_MAX];
    char path[CS_PATH_MAX];
    cs_browser_breadcrumb breadcrumbs[CS_BROWSER_MAX_BREADCRUMBS];
    size_t breadcrumb_count;
    cs_browser_entry entries[CS_BROWSER_PAGE_SIZE];
    size_t count;
    size_t total_count;
    size_t offset;
    int truncated;
} cs_browser_result;

const char *cs_browser_scope_name(cs_browser_scope scope);
cs_browser_scope cs_browser_scope_parse(const char *value);
int cs_browser_scope_requires_platform(cs_browser_scope scope);
int cs_browser_scope_allows_hidden(cs_browser_scope scope);
int cs_browser_scope_supported_for_platform(const cs_platform_info *platform, cs_browser_scope scope);
int cs_browser_scope_allows_hidden_for_platform(cs_browser_scope scope, const cs_platform_info *platform);
int cs_browser_root_for_scope(const cs_paths *paths,
                              cs_browser_scope scope,
                              const cs_platform_info *platform,
                              char *root,
                              size_t root_size);
int cs_browser_list(const cs_paths *paths,
                    cs_browser_scope scope,
                    const cs_platform_info *platform,
                    const char *relative_path,
                    size_t offset,
                    const char *query,
                    cs_browser_result *result);

#endif
