#include "cs_library.h"
#include "cs_util.h"

#include "cs_file_ops.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct cs_browser_sort_entry {
    char name[256];
    int is_dir;
    unsigned long long size;
    long long modified;
} cs_browser_sort_entry;

static int cs_browser_name_matches_query(const char *name, const char *query) {
    size_t name_len;
    size_t query_len;
    size_t i;

    if (!query || query[0] == '\0') {
        return 1;
    }
    if (!name) {
        return 0;
    }
    name_len = strlen(name);
    query_len = strlen(query);
    if (query_len > name_len) {
        return 0;
    }
    for (i = 0; i + query_len <= name_len; ++i) {
        size_t j;

        for (j = 0; j < query_len; ++j) {
            unsigned char nc = (unsigned char) name[i + j];
            unsigned char qc = (unsigned char) query[j];

            if (tolower(nc) != tolower(qc)) {
                break;
            }
        }
        if (j == query_len) {
            return 1;
        }
    }
    return 0;
}

static int cs_is_regular_file_not_symlink(const char *path) {
    struct stat st;

    if (!path) {
        return 0;
    }
    if (lstat(path, &st) != 0) {
        return 0;
    }

    return S_ISREG(st.st_mode) ? 1 : 0;
}

static int cs_write_relative_path(const char *root,
                                  const char *path,
                                  char *relative,
                                  size_t relative_size) {
    const char *suffix;
    size_t root_len;

    if (!root || !path || !relative || relative_size == 0) {
        return -1;
    }

    root_len = strlen(root);
    if (root_len == 0 || strncmp(path, root, root_len) != 0) {
        return -1;
    }

    suffix = path + root_len;
    if (*suffix == '/') {
        suffix += 1;
    } else if (*suffix != '\0') {
        return -1;
    }

    return CS_SAFE_SNPRINTF(relative, relative_size, "%s", suffix);
}

static int cs_open_root_directory(const char *path) {
    if (!path) {
        return -1;
    }

    return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
}

static int cs_open_directory_from_fd(int start_fd, const char *relative_path) {
    int current_fd = start_fd;

    if (current_fd < 0 || !relative_path) {
        errno = EINVAL;
        return -1;
    }
    if (relative_path[0] == '\0') {
        return current_fd;
    }

    while (*relative_path != '\0') {
        const char *slash = strchr(relative_path, '/');
        size_t length = slash ? (size_t) (slash - relative_path) : strlen(relative_path);
        char component[CS_PATH_MAX];
        int next_fd;

        if (length == 0 || length >= sizeof(component)) {
            close(current_fd);
            errno = EINVAL;
            return -1;
        }

        memcpy(component, relative_path, length);
        component[length] = '\0';
        next_fd = openat(current_fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        close(current_fd);
        if (next_fd < 0) {
            return -1;
        }

        current_fd = next_fd;
        relative_path = slash ? slash + 1 : relative_path + length;
    }

    return current_fd;
}

static int cs_open_guarded_directory(const char *guard_root, const char *path) {
    char relative[CS_PATH_MAX];
    int guard_fd;

    if (cs_write_relative_path(guard_root, path, relative, sizeof(relative)) != 0) {
        return -1;
    }

    guard_fd = cs_open_root_directory(guard_root);
    if (guard_fd < 0) {
        return -1;
    }

    return cs_open_directory_from_fd(guard_fd, relative);
}

static int cs_join_path(char *dst, size_t size, const char *left, const char *right) {
    int written;

    if (!dst || size == 0 || !left || !right) {
        return -1;
    }
    if (right[0] == '\0') {
        written = CS_SAFE_SNPRINTF(dst, size, "%s", left);
    } else {
        written = CS_SAFE_SNPRINTF(dst, size, "%s/%s", left, right);
    }

    return written;
}

static int cs_browser_path_is_art(const char *relative_path) {
    if (!relative_path) {
        return 0;
    }

    return strncmp(relative_path, ".media/", 7) == 0 || strstr(relative_path, "/.media/") != NULL;
}

static int cs_browser_should_include_hidden_rom_entry(const cs_platform_info *platform, const char *name) {
    if (!platform || !name) {
        return 0;
    }

    return cs_platform_allows_hidden_rom_entries(platform) && strcmp(name, ".ports") == 0;
}

static int cs_browser_should_include_entry(cs_browser_scope scope,
                                           const cs_platform_info *platform,
                                           const char *name,
                                           const char *absolute_path) {
    if (!name || name[0] == '\0') {
        return 0;
    }
    if (scope == CS_SCOPE_ROMS && absolute_path && cs_platform_is_shortcut_directory(name, absolute_path)) {
        return 0;
    }
    if (name[0] != '.') {
        return 1;
    }
    if (scope == CS_SCOPE_FILES) {
        return 1;
    }

    return strcmp(name, ".media") == 0 || (scope == CS_SCOPE_ROMS && cs_browser_should_include_hidden_rom_entry(platform, name));
}

static const char *cs_scope_label(cs_browser_scope scope) {
    switch (scope) {
        case CS_SCOPE_ROMS:
            return "ROMs";
        case CS_SCOPE_SAVES:
            return "Saves";
        case CS_SCOPE_BIOS:
            return "BIOS";
        case CS_SCOPE_OVERLAYS:
            return "Overlays";
        case CS_SCOPE_CHEATS:
            return "Cheats";
        case CS_SCOPE_FILES:
            return "File Browser";
        default:
            return "Browser";
    }
}

static int cs_browser_write_title(char *dst,
                                  size_t size,
                                  cs_browser_scope scope,
                                  const cs_platform_info *platform) {
    int written;

    if (!dst || size == 0) {
        return -1;
    }

    if (platform && scope != CS_SCOPE_FILES) {
        written = CS_SAFE_SNPRINTF(dst, size, "%s - %s", cs_scope_label(scope), platform->name);
    } else {
        written = CS_SAFE_SNPRINTF(dst, size, "%s", cs_scope_label(scope));
    }
    return written;
}

static int cs_browser_write_breadcrumbs(cs_browser_result *result, const char *relative_path) {
    const char *cursor = relative_path;
    size_t count = 0;

    if (!result || !relative_path || relative_path[0] == '\0') {
        if (result) {
            result->breadcrumb_count = 0;
        }
        return 0;
    }

    while (*cursor != '\0' && count < CS_BROWSER_MAX_BREADCRUMBS) {
        const char *slash = strchr(cursor, '/');
        size_t length = slash ? (size_t) (slash - cursor) : strlen(cursor);
        size_t prefix_len;

        if (slash) {
            prefix_len = (size_t) (slash - relative_path);
        } else {
            prefix_len = strlen(relative_path);
        }

        if (length == 0 || prefix_len >= sizeof(result->breadcrumbs[count].path)) {
            return -1;
        }

        memcpy(result->breadcrumbs[count].label, cursor, length);
        result->breadcrumbs[count].label[length] = '\0';
        memcpy(result->breadcrumbs[count].path, relative_path, prefix_len);
        result->breadcrumbs[count].path[prefix_len] = '\0';
        count += 1;

        cursor = slash ? slash + 1 : cursor + length;
    }

    result->breadcrumb_count = count;
    return 0;
}

static int cs_browser_write_thumbnail(const char *root,
                                      const char *entry_relative_path,
                                      char *thumbnail_path,
                                      size_t thumbnail_path_size) {
    char candidate_relative[CS_PATH_MAX];
    char candidate_absolute[CS_PATH_MAX];
    char basename[CS_PATH_MAX];
    const char *ext;
    size_t name_len;

    if (!root || !entry_relative_path || !thumbnail_path || thumbnail_path_size == 0) {
        return -1;
    }
    if (cs_browser_path_is_art(entry_relative_path)) {
        thumbnail_path[0] = '\0';
        return 0;
    }

    ext = strrchr(entry_relative_path, '.');
    name_len = ext && ext != entry_relative_path ? (size_t) (ext - entry_relative_path) : strlen(entry_relative_path);
    if (name_len == 0 || name_len >= sizeof(basename)) {
        thumbnail_path[0] = '\0';
        return 0;
    }

    memcpy(basename, entry_relative_path, name_len);
    basename[name_len] = '\0';

    if (CS_SAFE_SNPRINTF(candidate_relative, sizeof(candidate_relative), ".media/%s.png", basename) != 0) {
        return -1;
    }
    if (cs_join_path(candidate_absolute, sizeof(candidate_absolute), root, candidate_relative) != 0) {
        return -1;
    }
    if (cs_is_regular_file_not_symlink(candidate_absolute)) {
        if (CS_SAFE_SNPRINTF(thumbnail_path, thumbnail_path_size, "%s", candidate_relative) != 0) {
            return -1;
        }
        return 0;
    }

    thumbnail_path[0] = '\0';
    return 0;
}

static int cs_browser_sort_compare(const void *left, const void *right) {
    const cs_browser_sort_entry *a = (const cs_browser_sort_entry *) left;
    const cs_browser_sort_entry *b = (const cs_browser_sort_entry *) right;

    if (a->is_dir != b->is_dir) {
        return a->is_dir ? -1 : 1;
    }
    return strcmp(a->name, b->name);
}

static cs_browser_list_status cs_browser_path_failure_status(int error_code) {
    return (error_code == ENOENT || error_code == ENOTDIR) ? CS_BROWSER_LIST_NOT_FOUND : CS_BROWSER_LIST_INTERNAL;
}

static const char *cs_browser_entry_type_for_scope(cs_browser_scope scope, int is_dir, const char *entry_relative) {
    if (is_dir) {
        return "directory";
    }
    switch (scope) {
        case CS_SCOPE_ROMS:
            return cs_browser_path_is_art(entry_relative) ? "art" : "rom";
        case CS_SCOPE_SAVES:
            return "save";
        case CS_SCOPE_BIOS:
            return "bios";
        case CS_SCOPE_OVERLAYS:
            return "overlay";
        case CS_SCOPE_CHEATS:
            return "cheat";
        default:
            return "file";
    }
}

static int cs_browser_guard_root_for_scope(const cs_paths *paths,
                                           cs_browser_scope scope,
                                           char *guard_root,
                                           size_t guard_root_size) {
    const char *source = NULL;

    if (!paths || !guard_root || guard_root_size == 0) {
        return -1;
    }

    switch (scope) {
        case CS_SCOPE_ROMS:
            source = paths->roms_root;
            break;
        case CS_SCOPE_SAVES:
            source = paths->saves_root;
            break;
        case CS_SCOPE_BIOS:
            source = paths->bios_root;
            break;
        case CS_SCOPE_OVERLAYS:
            source = paths->overlays_root;
            break;
        case CS_SCOPE_CHEATS:
            source = paths->cheats_root;
            break;
        case CS_SCOPE_FILES:
            source = paths->sdcard_root;
            break;
        default:
            return -1;
    }

    return CS_SAFE_SNPRINTF(guard_root, guard_root_size, "%s", source);
}

const char *cs_browser_scope_name(cs_browser_scope scope) {
    switch (scope) {
        case CS_SCOPE_ROMS:
            return "roms";
        case CS_SCOPE_SAVES:
            return "saves";
        case CS_SCOPE_BIOS:
            return "bios";
        case CS_SCOPE_OVERLAYS:
            return "overlays";
        case CS_SCOPE_CHEATS:
            return "cheats";
        case CS_SCOPE_FILES:
            return "files";
        default:
            return NULL;
    }
}

cs_browser_scope cs_browser_scope_parse(const char *value) {
    if (!value || value[0] == '\0') {
        return CS_SCOPE_INVALID;
    }
    if (strcmp(value, "roms") == 0) {
        return CS_SCOPE_ROMS;
    }
    if (strcmp(value, "saves") == 0) {
        return CS_SCOPE_SAVES;
    }
    if (strcmp(value, "bios") == 0) {
        return CS_SCOPE_BIOS;
    }
    if (strcmp(value, "overlays") == 0) {
        return CS_SCOPE_OVERLAYS;
    }
    if (strcmp(value, "cheats") == 0) {
        return CS_SCOPE_CHEATS;
    }
    if (strcmp(value, "files") == 0) {
        return CS_SCOPE_FILES;
    }

    return CS_SCOPE_INVALID;
}

int cs_browser_scope_requires_platform(cs_browser_scope scope) {
    return scope == CS_SCOPE_ROMS || scope == CS_SCOPE_SAVES || scope == CS_SCOPE_BIOS
           || scope == CS_SCOPE_OVERLAYS || scope == CS_SCOPE_CHEATS;
}

int cs_browser_scope_allows_hidden(cs_browser_scope scope) {
    return scope == CS_SCOPE_FILES;
}

int cs_browser_scope_supported_for_platform(const cs_platform_info *platform, cs_browser_scope scope) {
    const char *scope_name;

    if (scope == CS_SCOPE_FILES) {
        return 1;
    }

    scope_name = cs_browser_scope_name(scope);
    return scope_name ? cs_platform_supports_resource(platform, scope_name) : 0;
}

int cs_browser_scope_allows_hidden_for_platform(cs_browser_scope scope, const cs_platform_info *platform) {
    return cs_browser_scope_allows_hidden(scope)
           || (scope == CS_SCOPE_ROMS && cs_platform_allows_hidden_rom_entries(platform));
}

int cs_browser_root_for_scope(const cs_paths *paths,
                              cs_browser_scope scope,
                              const cs_platform_info *platform,
                              char *root,
                              size_t root_size) {
    if (!paths || !root || root_size == 0) {
        return -1;
    }
    if (scope != CS_SCOPE_FILES && !cs_browser_scope_supported_for_platform(platform, scope)) {
        return -1;
    }

    switch (scope) {
        case CS_SCOPE_ROMS:
            if (!platform) {
                return -1;
            }
            return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", paths->roms_root, platform->rom_directory);
        case CS_SCOPE_SAVES:
            if (!platform) {
                return -1;
            }
            return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", paths->saves_root, platform->primary_code);
        case CS_SCOPE_BIOS:
            if (!platform) {
                return -1;
            }
            return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", paths->bios_root, platform->primary_code);
        case CS_SCOPE_OVERLAYS:
            if (!platform) {
                return -1;
            }
            return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", paths->overlays_root, platform->primary_code);
        case CS_SCOPE_CHEATS:
            if (!platform) {
                return -1;
            }
            return CS_SAFE_SNPRINTF(root, root_size, "%s/%s", paths->cheats_root, platform->primary_code);
        case CS_SCOPE_FILES:
            return CS_SAFE_SNPRINTF(root, root_size, "%s", paths->sdcard_root);
        default:
            return -1;
    }
}

cs_browser_list_status cs_browser_list(const cs_paths *paths,
                                       cs_browser_scope scope,
                                       const cs_platform_info *platform,
                                       const char *relative_path,
                                       size_t offset,
                                       const char *query,
                                       cs_browser_result *result) {
    char root[CS_PATH_MAX];
    char target_path[CS_PATH_MAX];
    char guard_root[CS_PATH_MAX];
    unsigned int path_flags = CS_PATH_FLAG_ALLOW_EMPTY;
    int root_fd = -1;
    int dir_fd = -1;
    DIR *dir;
    struct dirent *entry;
    cs_browser_sort_entry *sort_buf = NULL;
    size_t sort_count = 0;
    int scan_truncated = 0;
    size_t out_count = 0;
    size_t i;

    if (!paths || !result || scope == CS_SCOPE_INVALID) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_scope_requires_platform(scope) && !platform) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_root_for_scope(paths, scope, platform, root, sizeof(root)) != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_guard_root_for_scope(paths, scope, guard_root, sizeof(guard_root)) != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    memset(result, 0, sizeof(*result));
    result->offset = offset;
    if (CS_SAFE_SNPRINTF(result->scope, sizeof(result->scope), "%s", cs_browser_scope_name(scope)) != 0
        || CS_SAFE_SNPRINTF(result->root_path, sizeof(result->root_path), "%s", root) != 0
        || CS_SAFE_SNPRINTF(result->path, sizeof(result->path), "%s", relative_path ? relative_path : "") != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_write_title(result->title, sizeof(result->title), scope, platform) != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }
    if (cs_browser_write_breadcrumbs(result, relative_path ? relative_path : "") != 0) {
        return CS_BROWSER_LIST_INTERNAL;
    }

    if (cs_browser_scope_allows_hidden_for_platform(scope, platform)) {
        path_flags |= CS_PATH_FLAG_ALLOW_HIDDEN;
    }
    if (cs_resolve_path_under_root_with_flags(root,
                                              relative_path ? relative_path : "",
                                              path_flags,
                                              target_path,
                                              sizeof(target_path))
        != 0) {
        return CS_BROWSER_LIST_NOT_FOUND;
    }

    root_fd = cs_open_guarded_directory(guard_root, root);
    if (root_fd < 0) {
        if (scope != CS_SCOPE_FILES && errno == ENOENT) {
            return CS_BROWSER_LIST_OK;
        }
        return cs_browser_path_failure_status(errno);
    }

    if (relative_path && relative_path[0] != '\0') {
        dir_fd = cs_open_directory_from_fd(root_fd, relative_path);
        root_fd = -1;
    } else {
        dir_fd = root_fd;
    }
    if (dir_fd < 0) {
        return cs_browser_path_failure_status(errno);
    }

    dir = fdopendir(dir_fd);
    if (!dir) {
        int saved_errno = errno;

        close(dir_fd);
        return cs_browser_path_failure_status(saved_errno);
    }
    if (dir_fd == root_fd) {
        root_fd = -1;
    }

    sort_buf = (cs_browser_sort_entry *) calloc(CS_BROWSER_SCAN_CAP, sizeof(*sort_buf));
    if (!sort_buf) {
        (void) closedir(dir);
        if (root_fd >= 0) {
            close(root_fd);
        }
        return CS_BROWSER_LIST_INTERNAL;
    }

    while ((entry = readdir(dir)) != NULL) {
        char entry_absolute[CS_PATH_MAX];
        struct stat st;
        cs_browser_sort_entry *sort_entry;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (cs_join_path(entry_absolute, sizeof(entry_absolute), target_path, entry->d_name) != 0) {
            continue;
        }
        if (!cs_browser_should_include_entry(scope, platform, entry->d_name, entry_absolute)) {
            continue;
        }
        if (!cs_browser_name_matches_query(entry->d_name, query)) {
            continue;
        }
        if (fstatat(dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
            continue;
        }
        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode)) {
            continue;
        }

        if (sort_count >= CS_BROWSER_SCAN_CAP) {
            scan_truncated = 1;
            continue;
        }

        sort_entry = &sort_buf[sort_count];
        if (CS_SAFE_SNPRINTF(sort_entry->name, sizeof(sort_entry->name), "%s", entry->d_name) != 0) {
            continue;
        }
        sort_entry->is_dir = S_ISDIR(st.st_mode) ? 1 : 0;
        sort_entry->size = S_ISREG(st.st_mode) ? (unsigned long long) st.st_size : 0;
        sort_entry->modified = (long long) st.st_mtime;
        sort_count += 1;
    }

    (void) closedir(dir);
    if (root_fd >= 0) {
        close(root_fd);
    }

    qsort(sort_buf, sort_count, sizeof(*sort_buf), cs_browser_sort_compare);

    result->total_count = sort_count;
    result->truncated = scan_truncated;

    for (i = offset; i < sort_count && out_count < CS_BROWSER_PAGE_SIZE; ++i) {
        const cs_browser_sort_entry *src = &sort_buf[i];
        cs_browser_entry *dst = &result->entries[out_count];
        char entry_relative[CS_PATH_MAX];
        const char *type_str;

        if (relative_path && relative_path[0] != '\0') {
            if (CS_SAFE_SNPRINTF(entry_relative, sizeof(entry_relative), "%s/%s", relative_path, src->name) != 0) {
                continue;
            }
        } else if (CS_SAFE_SNPRINTF(entry_relative, sizeof(entry_relative), "%s", src->name) != 0) {
            continue;
        }

        memset(dst, 0, sizeof(*dst));
        if (CS_SAFE_SNPRINTF(dst->name, sizeof(dst->name), "%s", src->name) != 0
            || CS_SAFE_SNPRINTF(dst->path, sizeof(dst->path), "%s", entry_relative) != 0) {
            free(sort_buf);
            return CS_BROWSER_LIST_INTERNAL;
        }
        dst->size = src->size;
        dst->modified = src->modified;

        type_str = cs_browser_entry_type_for_scope(scope, src->is_dir, entry_relative);
        if (CS_SAFE_SNPRINTF(dst->type, sizeof(dst->type), "%s", type_str) != 0) {
            free(sort_buf);
            return CS_BROWSER_LIST_INTERNAL;
        }

        if (scope == CS_SCOPE_ROMS && !src->is_dir) {
            (void) cs_browser_write_thumbnail(root,
                                              entry_relative,
                                              dst->thumbnail_path,
                                              sizeof(dst->thumbnail_path));
        }

        out_count += 1;
    }

    result->count = out_count;
    free(sort_buf);
    return CS_BROWSER_LIST_OK;
}
