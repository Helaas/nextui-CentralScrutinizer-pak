#include "cs_states.h"
#include "cs_util.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum cs_state_family {
    CS_STATE_FAMILY_UNKNOWN = 0,
    CS_STATE_FAMILY_MINUI = 1,
    CS_STATE_FAMILY_RETROARCH_DOT = 2,
    CS_STATE_FAMILY_RETROARCH = 4,
    CS_STATE_FAMILY_AUTO = 8,
} cs_state_family;

typedef struct cs_state_group {
    cs_state_entry entry;
    char stem[256];
    unsigned int family_mask;
} cs_state_group;

typedef struct cs_state_file_match {
    char stem[256];
    int slot;
    cs_state_family family;
} cs_state_file_match;

static int cs_state_path_is_regular_file(const char *path, struct stat *st_out) {
    struct stat st;

    if (!path || lstat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    if (st_out) {
        *st_out = st;
    }
    return 1;
}

static int cs_state_path_is_directory(const char *path) {
    struct stat st;

    return path && lstat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int cs_state_copy_relative_path(char *dst,
                                       size_t dst_size,
                                       const char *left,
                                       const char *right) {
    if (!dst || dst_size == 0 || !left || !right) {
        return -1;
    }

    return CS_SAFE_SNPRINTF(dst, dst_size, ".userdata/shared/%s/%s", left, right);
}

static int cs_state_has_suffix_digits(const char *value, const char *prefix, int *slot_out) {
    const char *digits;
    char *end = NULL;
    long slot;

    if (!value || !prefix || strncmp(value, prefix, strlen(prefix)) != 0) {
        return 0;
    }

    digits = value + strlen(prefix);
    if (*digits == '\0') {
        return 0;
    }

    errno = 0;
    slot = strtol(digits, &end, 10);
    if (errno != 0 || !end || *end != '\0' || slot < 0 || slot > CS_STATE_SLOT_MAX) {
        return 0;
    }

    if (slot_out) {
        *slot_out = (int) slot;
    }
    return 1;
}

static int cs_state_parse_filename(const char *name, cs_state_file_match *match) {
    const char *suffix = NULL;
    const char *cursor;
    size_t name_len;
    size_t stem_len = 0;
    int slot = 0;

    if (!name || !match) {
        return -1;
    }

    memset(match, 0, sizeof(*match));
    name_len = strlen(name);
    if (name_len > strlen(".state.auto") && strcmp(name + name_len - strlen(".state.auto"), ".state.auto") == 0) {
        stem_len = name_len - strlen(".state.auto");
        match->slot = 9;
        match->family = CS_STATE_FAMILY_AUTO;
    } else {
        cursor = name;
        while ((cursor = strstr(cursor, ".state.")) != NULL) {
            suffix = cursor;
            cursor += 1;
        }
        if (suffix && cs_state_has_suffix_digits(suffix, ".state.", &slot)) {
            stem_len = (size_t) (suffix - name);
            match->slot = slot;
            match->family = CS_STATE_FAMILY_RETROARCH_DOT;
        } else if (name_len > strlen(".state") && strcmp(name + name_len - strlen(".state"), ".state") == 0) {
            suffix = name + name_len - strlen(".state");
            stem_len = (size_t) (suffix - name);
            match->slot = 0;
            match->family = CS_STATE_FAMILY_RETROARCH;
        } else {
            cursor = name;
            suffix = NULL;
            while ((cursor = strstr(cursor, ".state")) != NULL) {
                suffix = cursor;
                cursor += 1;
            }
            if (suffix && cs_state_has_suffix_digits(suffix, ".state", &slot)) {
                stem_len = (size_t) (suffix - name);
                match->slot = slot;
                match->family = CS_STATE_FAMILY_RETROARCH;
            } else {
                cursor = name;
                suffix = NULL;
                while ((cursor = strstr(cursor, ".st")) != NULL) {
                    suffix = cursor;
                    cursor += 1;
                }
                if (!suffix || !cs_state_has_suffix_digits(suffix, ".st", &slot)) {
                    return -1;
                }
                stem_len = (size_t) (suffix - name);
                match->slot = slot;
                match->family = CS_STATE_FAMILY_MINUI;
            }
        }
    }

    if (stem_len == 0 || stem_len >= sizeof(match->stem)) {
        return -1;
    }

    memcpy(match->stem, name, stem_len);
    match->stem[stem_len] = '\0';
    return 0;
}

static int cs_state_add_path(char paths[][CS_PATH_MAX],
                             size_t *count,
                             size_t capacity,
                             const char *value) {
    size_t i;

    if (!paths || !count || !value || value[0] == '\0') {
        return -1;
    }

    for (i = 0; i < *count; ++i) {
        if (strcmp(paths[i], value) == 0) {
            return 0;
        }
    }
    if (*count >= capacity) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(paths[*count], CS_PATH_MAX, "%s", value) != 0) {
        return -1;
    }
    *count += 1;
    return 0;
}

static int cs_state_add_warning(cs_state_entry *entry, const char *warning) {
    size_t i;

    if (!entry || !warning || warning[0] == '\0') {
        return -1;
    }

    for (i = 0; i < entry->warning_count; ++i) {
        if (strcmp(entry->warnings[i], warning) == 0) {
            return 0;
        }
    }
    if (entry->warning_count >= CS_STATE_MAX_WARNINGS) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(entry->warnings[entry->warning_count],
                         sizeof(entry->warnings[entry->warning_count]),
                         "%s",
                         warning)
        != 0) {
        return -1;
    }
    entry->warning_count += 1;
    return 0;
}

static int cs_state_note_file(cs_state_entry *entry,
                              const char *relative_path,
                              const struct stat *st,
                              int delete_only) {
    if (!entry || !relative_path || !st) {
        return -1;
    }

    if (!delete_only
        && cs_state_add_path(entry->download_paths,
                             &entry->download_path_count,
                             CS_STATE_MAX_PATHS,
                             relative_path)
               != 0) {
        return -1;
    }
    if (cs_state_add_path(entry->delete_paths, &entry->delete_path_count, CS_STATE_MAX_PATHS, relative_path) != 0) {
        return -1;
    }

    entry->size += (unsigned long long) st->st_size;
    if ((long long) st->st_mtime > entry->modified) {
        entry->modified = (long long) st->st_mtime;
    }
    return 0;
}

static void cs_state_write_slot_label(cs_state_group *group) {
    unsigned int mask;
    int slot;

    if (!group) {
        return;
    }

    mask = group->family_mask;
    slot = group->entry.slot;

    if ((mask & CS_STATE_FAMILY_AUTO) != 0) {
        (void) CS_SAFE_SNPRINTF(group->entry.slot_label,
                                sizeof(group->entry.slot_label),
                                "%s",
                                "Auto Resume");
        (void) CS_SAFE_SNPRINTF(group->entry.kind, sizeof(group->entry.kind), "%s", "auto-resume");
        return;
    }
    if (slot == 8 && (mask & CS_STATE_FAMILY_MINUI) != 0) {
        (void) CS_SAFE_SNPRINTF(group->entry.slot_label,
                                sizeof(group->entry.slot_label),
                                "%s",
                                "Auto Resume (Legacy)");
        (void) CS_SAFE_SNPRINTF(group->entry.kind, sizeof(group->entry.kind), "%s", "legacy-auto-resume");
        return;
    }

    (void) CS_SAFE_SNPRINTF(group->entry.slot_label, sizeof(group->entry.slot_label), "Slot %d", slot + 1);
    (void) CS_SAFE_SNPRINTF(group->entry.kind, sizeof(group->entry.kind), "%s", "slot");
}

static void cs_state_write_format(cs_state_group *group) {
    unsigned int mask;
    const char *label = "Unknown";

    if (!group) {
        return;
    }

    mask = group->family_mask;
    if (mask == CS_STATE_FAMILY_MINUI || mask == (CS_STATE_FAMILY_MINUI | CS_STATE_FAMILY_AUTO)) {
        label = "MinUI";
    } else if (mask == CS_STATE_FAMILY_RETROARCH_DOT
               || mask == (CS_STATE_FAMILY_RETROARCH_DOT | CS_STATE_FAMILY_AUTO)) {
        label = "RetroArch-ish";
    } else if (mask == CS_STATE_FAMILY_RETROARCH
               || mask == (CS_STATE_FAMILY_RETROARCH | CS_STATE_FAMILY_AUTO)) {
        label = "RetroArch";
    } else if (mask == CS_STATE_FAMILY_AUTO) {
        label = "State Auto";
    } else {
        label = "Mixed";
        (void) cs_state_add_warning(&group->entry, "Multiple payload formats found for this slot.");
    }

    (void) CS_SAFE_SNPRINTF(group->entry.format, sizeof(group->entry.format), "%s", label);
}

static int cs_state_find_group(cs_state_group *groups,
                               size_t count,
                               const char *core_dir,
                               const char *stem,
                               int slot) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (groups[i].entry.slot == slot && strcmp(groups[i].entry.core_dir, core_dir) == 0
            && strcmp(groups[i].stem, stem) == 0) {
            return (int) i;
        }
    }

    return -1;
}

static int cs_state_ensure_group_capacity(cs_state_group **groups, size_t *capacity, size_t required) {
    size_t new_capacity;
    cs_state_group *resized;

    if (!groups || !capacity) {
        return -1;
    }
    if (required <= *capacity) {
        return 0;
    }

    new_capacity = *capacity > 0 ? *capacity : 32;
    while (new_capacity < required) {
        if (new_capacity > (SIZE_MAX / 2)) {
            return -1;
        }
        new_capacity *= 2;
    }
    if (new_capacity > SIZE_MAX / sizeof(**groups)) {
        return -1;
    }

    resized = (cs_state_group *) realloc(*groups, new_capacity * sizeof(**groups));
    if (!resized) {
        return -1;
    }

    *groups = resized;
    *capacity = new_capacity;
    return 0;
}

static int cs_state_add_payload(cs_state_group **groups,
                                size_t *count,
                                size_t *capacity,
                                const char *core_dir,
                                const char *relative_path,
                                const struct stat *st,
                                const cs_state_file_match *match) {
    int index;
    cs_state_group *group;

    if (!groups || !count || !capacity || !core_dir || !relative_path || !st || !match) {
        return -1;
    }

    index = cs_state_find_group(*groups, *count, core_dir, match->stem, match->slot);
    if (index < 0) {
        if (cs_state_ensure_group_capacity(groups, capacity, *count + 1) != 0) {
            return -1;
        }

        index = (int) *count;
        group = &(*groups)[index];
        memset(group, 0, sizeof(*group));
        if (CS_SAFE_SNPRINTF(group->entry.id,
                             sizeof(group->entry.id),
                             "%s:%s:%d",
                             core_dir,
                             match->stem,
                             match->slot)
                != 0
            || CS_SAFE_SNPRINTF(group->entry.title, sizeof(group->entry.title), "%s", match->stem) != 0
            || CS_SAFE_SNPRINTF(group->entry.core_dir, sizeof(group->entry.core_dir), "%s", core_dir) != 0
            || CS_SAFE_SNPRINTF(group->stem, sizeof(group->stem), "%s", match->stem) != 0) {
            return -1;
        }

        group->entry.slot = match->slot;
        *count += 1;
    }

    group = &(*groups)[index];
    group->family_mask |= (unsigned int) match->family;
    cs_state_write_slot_label(group);
    if (cs_state_note_file(&group->entry, relative_path, st, 0) != 0) {
        return -1;
    }
    cs_state_write_format(group);
    return 0;
}

static int cs_state_read_slot_pointer(const char *path, int *slot_out) {
    FILE *file;
    char buffer[32];
    char *end = NULL;
    long slot;

    if (!path || !slot_out) {
        return -1;
    }

    file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    if (!fgets(buffer, sizeof(buffer), file)) {
        fclose(file);
        return -1;
    }
    fclose(file);

    slot = strtol(buffer, &end, 10);
    if (end == buffer || (*end != '\0' && *end != '\n' && *end != '\r')) {
        return -1;
    }

    *slot_out = (int) slot;
    return 0;
}

static int cs_states_attach_metadata(const cs_paths *paths,
                                     const cs_platform_info *platform,
                                     cs_state_group *groups,
                                     size_t count) {
    size_t i;
    char metadata_root[CS_PATH_MAX];
    char metadata_relative_root[CS_PATH_MAX];

    if (!paths || !platform || !groups) {
        return -1;
    }
    if (CS_SAFE_SNPRINTF(metadata_root,
                         sizeof(metadata_root),
                         "%s/.minui/%s",
                         paths->shared_userdata_root,
                         platform->primary_code)
        != 0
        || CS_SAFE_SNPRINTF(metadata_relative_root,
                            sizeof(metadata_relative_root),
                            ".userdata/shared/.minui/%s",
                            platform->primary_code)
               != 0) {
        return -1;
    }

    for (i = 0; i < count; ++i) {
        cs_state_group *group = &groups[i];
        char slot_file_name[CS_PATH_MAX];
        char slot_path[CS_PATH_MAX];
        char slot_relative[CS_PATH_MAX];
        char preview_file_name[CS_PATH_MAX];
        char preview_path[CS_PATH_MAX];
        char preview_relative[CS_PATH_MAX];
        char disc_file_name[CS_PATH_MAX];
        char disc_path[CS_PATH_MAX];
        char disc_relative[CS_PATH_MAX];
        struct stat st;
        int slot_value = -1;
        int metadata_found = 0;

        if (CS_SAFE_SNPRINTF(slot_file_name, sizeof(slot_file_name), "%s.txt", group->stem) != 0
            || CS_SAFE_SNPRINTF(slot_path, sizeof(slot_path), "%s/%s", metadata_root, slot_file_name) != 0
            || CS_SAFE_SNPRINTF(slot_relative,
                                sizeof(slot_relative),
                                "%s/%s",
                                metadata_relative_root,
                                slot_file_name)
                   != 0
            || CS_SAFE_SNPRINTF(preview_file_name,
                                sizeof(preview_file_name),
                                "%s.%d.bmp",
                                group->stem,
                                group->entry.slot)
                   != 0
            || CS_SAFE_SNPRINTF(preview_path, sizeof(preview_path), "%s/%s", metadata_root, preview_file_name) != 0
            || CS_SAFE_SNPRINTF(preview_relative,
                                sizeof(preview_relative),
                                "%s/%s",
                                metadata_relative_root,
                                preview_file_name)
                   != 0
            || CS_SAFE_SNPRINTF(disc_file_name,
                                sizeof(disc_file_name),
                                "%s.%d.txt",
                                group->stem,
                                group->entry.slot)
                   != 0
            || CS_SAFE_SNPRINTF(disc_path, sizeof(disc_path), "%s/%s", metadata_root, disc_file_name) != 0
            || CS_SAFE_SNPRINTF(disc_relative, sizeof(disc_relative), "%s/%s", metadata_relative_root, disc_file_name)
                   != 0) {
            return -1;
        }

        if (cs_state_path_is_regular_file(slot_path, &st)) {
            metadata_found = 1;
            if (cs_state_read_slot_pointer(slot_path, &slot_value) == 0 && slot_value == group->entry.slot) {
                if (cs_state_note_file(&group->entry, slot_relative, &st, 0) != 0) {
                    return -1;
                }
            }
        }
        if (cs_state_path_is_regular_file(preview_path, &st)) {
            metadata_found = 1;
            if (CS_SAFE_SNPRINTF(group->entry.preview_path, sizeof(group->entry.preview_path), "%s", preview_relative)
                != 0
                || cs_state_note_file(&group->entry, preview_relative, &st, 0) != 0) {
                return -1;
            }
        }
        if (cs_state_path_is_regular_file(disc_path, &st)) {
            metadata_found = 1;
            if (cs_state_note_file(&group->entry, disc_relative, &st, 0) != 0) {
                return -1;
            }
        }

        if (!metadata_found) {
            if (cs_state_add_warning(&group->entry, "Matching .minui metadata was not found.") != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static int cs_state_compare_descending(const void *left, const void *right) {
    const cs_state_group *a = (const cs_state_group *) left;
    const cs_state_group *b = (const cs_state_group *) right;

    if (a->entry.modified != b->entry.modified) {
        return a->entry.modified > b->entry.modified ? -1 : 1;
    }
    if (a->entry.slot != b->entry.slot) {
        return a->entry.slot - b->entry.slot;
    }

    return strcmp(a->entry.id, b->entry.id);
}

int cs_states_collect(const cs_paths *paths,
                      const cs_platform_info *platform,
                      cs_state_entry *entries,
                      size_t entry_capacity,
                      size_t *entry_count_out,
                      int *truncated_out) {
    DIR *dir = NULL;
    struct dirent *entry;
    cs_state_group *groups = NULL;
    size_t count = 0;
    size_t group_capacity = 0;

    if (entry_count_out) {
        *entry_count_out = 0;
    }
    if (truncated_out) {
        *truncated_out = 0;
    }
    if (!paths || !platform) {
        return -1;
    }

    dir = opendir(paths->shared_userdata_root);
    if (!dir) {
        free(groups);
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        char core_root[CS_PATH_MAX];
        DIR *core_dir;
        struct dirent *core_entry;

        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (strncmp(entry->d_name, platform->primary_code, strlen(platform->primary_code)) != 0
            || entry->d_name[strlen(platform->primary_code)] != '-') {
            continue;
        }
        if (CS_SAFE_SNPRINTF(core_root,
                             sizeof(core_root),
                             "%s/%s",
                             paths->shared_userdata_root,
                             entry->d_name)
            != 0
            || !cs_state_path_is_directory(core_root)) {
            continue;
        }

        core_dir = opendir(core_root);
        if (!core_dir) {
            continue;
        }
        while ((core_entry = readdir(core_dir)) != NULL) {
            char absolute_path[CS_PATH_MAX];
            char relative_path[CS_PATH_MAX];
            struct stat st;
            cs_state_file_match match;

            if (strcmp(core_entry->d_name, ".") == 0 || strcmp(core_entry->d_name, "..") == 0) {
                continue;
            }
            if (cs_state_parse_filename(core_entry->d_name, &match) != 0) {
                continue;
            }
            if (CS_SAFE_SNPRINTF(absolute_path,
                                 sizeof(absolute_path),
                                 "%s/%s",
                                 core_root,
                                 core_entry->d_name)
                    != 0
                || cs_state_copy_relative_path(relative_path, sizeof(relative_path), entry->d_name, core_entry->d_name)
                       != 0
                || !cs_state_path_is_regular_file(absolute_path, &st)) {
                continue;
            }
            if (cs_state_add_payload(&groups,
                                     &count,
                                     &group_capacity,
                                     entry->d_name,
                                     relative_path,
                                     &st,
                                     &match)
                != 0) {
                closedir(core_dir);
                closedir(dir);
                free(groups);
                return -1;
            }
        }
        closedir(core_dir);
    }
    closedir(dir);

    if (cs_states_attach_metadata(paths, platform, groups, count) != 0) {
        free(groups);
        return -1;
    }

    qsort(groups, count, sizeof(groups[0]), cs_state_compare_descending);
    if (entries) {
        size_t i;
        size_t limit = count < entry_capacity ? count : entry_capacity;

        for (i = 0; i < limit; ++i) {
            entries[i] = groups[i].entry;
        }
        if (entry_count_out) {
            *entry_count_out = count;
        }
    } else if (entry_count_out) {
        *entry_count_out = count;
    }
    if (truncated_out && entries && count > entry_capacity) {
        *truncated_out = 1;
    }
    free(groups);
    return 0;
}
