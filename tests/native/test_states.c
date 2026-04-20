#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_paths.h"
#include "cs_platforms.h"
#include "cs_states.h"

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static void set_sdcard_root_realpath(const char *root) {
    char resolved[PATH_MAX];

    assert(realpath(root, resolved) != NULL);
    setenv("SDCARD_PATH", resolved, 1);
    unsetenv("CS_WEB_ROOT");
}

static const cs_state_entry *find_entry(const cs_state_entry *entries,
                                        size_t count,
                                        const char *title,
                                        const char *core_dir,
                                        int slot) {
    size_t i;

    for (i = 0; i < count; ++i) {
        if (entries[i].slot == slot && strcmp(entries[i].title, title) == 0
            && strcmp(entries[i].core_dir, core_dir) == 0) {
            return &entries[i];
        }
    }

    return NULL;
}

static int has_download_path(const cs_state_entry *entry, const char *path) {
    size_t i;

    if (!entry || !path) {
        return 0;
    }

    for (i = 0; i < entry->download_path_count; ++i) {
        if (strcmp(entry->download_paths[i], path) == 0) {
            return 1;
        }
    }

    return 0;
}

static void test_fixture_states_are_grouped_and_metadata_is_attached(void) {
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    size_t count_only = 0;
    int truncated = 1;
    int count_only_truncated = 1;
    const cs_state_entry *slot_zero;
    const cs_state_entry *legacy_auto;
    const cs_state_entry *auto_resume;
    const cs_state_entry *retroarchish;
    const cs_state_entry *metadata_missing;

    assert(gba != NULL);
    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    unsetenv("CS_WEB_ROOT");
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, NULL, 0, &count_only, &count_only_truncated) == 0);
    assert(count_only == 5);
    assert(count_only_truncated == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == 5);
    assert(truncated == 0);

    slot_zero = find_entry(entries, count, "Pokemon Emerald.gba", "GBA-mGBA", 0);
    assert(slot_zero != NULL);
    assert(strcmp(slot_zero->slot_label, "Slot 1") == 0);
    assert(strcmp(slot_zero->format, "MinUI") == 0);
    assert(strcmp(slot_zero->preview_path, ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.0.bmp") == 0);
    assert(slot_zero->download_path_count >= 3);
    assert(slot_zero->warning_count == 0);

    legacy_auto = find_entry(entries, count, "Pokemon Emerald.gba", "GBA-mGBA", 8);
    assert(legacy_auto != NULL);
    assert(strcmp(legacy_auto->slot_label, "Auto Resume (Legacy)") == 0);
    assert(strcmp(legacy_auto->kind, "legacy-auto-resume") == 0);

    auto_resume = find_entry(entries, count, "Pokemon Emerald.gba", "GBA-mGBA", 9);
    assert(auto_resume != NULL);
    assert(strcmp(auto_resume->slot_label, "Auto Resume") == 0);
    assert(strcmp(auto_resume->kind, "auto-resume") == 0);

    retroarchish = find_entry(entries, count, "Pokemon Emerald.gba", "GBA-gpSP", 1);
    assert(retroarchish != NULL);
    assert(strcmp(retroarchish->format, "RetroArch-ish") == 0);
    assert(strcmp(retroarchish->preview_path, ".userdata/shared/.minui/GBA/Pokemon Emerald.gba.1.bmp") == 0);

    metadata_missing = find_entry(entries, count, "Archive Extracted.gba", "GBA-gpSP", 1);
    assert(metadata_missing != NULL);
    assert(strcmp(metadata_missing->format, "RetroArch") == 0);
    assert(metadata_missing->warning_count == 1);
    assert(strcmp(metadata_missing->warnings[0], "Matching .minui metadata was not found.") == 0);
}

static void test_mixed_formats_share_one_slot_group(void) {
    char template[] = "/tmp/cs-states-XXXXXX";
    char *root;
    char userdata_root[PATH_MAX];
    char shared_root[PATH_MAX];
    char minui_root[PATH_MAX];
    char emu_metadata_root[PATH_MAX];
    char core_root[PATH_MAX];
    char slot_pointer[PATH_MAX];
    char preview_path[PATH_MAX];
    char state_minui[PATH_MAX];
    char state_retroarch[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    int truncated = 1;
    const cs_state_entry *mixed;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(userdata_root, sizeof(userdata_root), "%s/.userdata", root) > 0);
    assert(snprintf(shared_root, sizeof(shared_root), "%s/.userdata/shared", root) > 0);
    assert(snprintf(minui_root, sizeof(minui_root), "%s/.userdata/shared/.minui", root) > 0);
    assert(snprintf(emu_metadata_root, sizeof(emu_metadata_root), "%s/.userdata/shared/.minui/GBA", root) > 0);
    assert(snprintf(core_root, sizeof(core_root), "%s/.userdata/shared/GBA-mGBA", root) > 0);
    assert(snprintf(slot_pointer, sizeof(slot_pointer), "%s/Test Mix.gba.txt", emu_metadata_root) > 0);
    assert(snprintf(preview_path, sizeof(preview_path), "%s/Test Mix.gba.0.bmp", emu_metadata_root) > 0);
    assert(snprintf(state_minui, sizeof(state_minui), "%s/Test Mix.gba.st0", core_root) > 0);
    assert(snprintf(state_retroarch, sizeof(state_retroarch), "%s/Test Mix.gba.state", core_root) > 0);

    make_dir(userdata_root);
    make_dir(shared_root);
    make_dir(minui_root);
    make_dir(emu_metadata_root);
    make_dir(core_root);
    write_file(slot_pointer, "0\n");
    write_file(preview_path, "bmp");
    write_file(state_minui, "minui");
    write_file(state_retroarch, "retroarch");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == 1);
    assert(truncated == 0);

    mixed = find_entry(entries, count, "Test Mix.gba", "GBA-mGBA", 0);
    assert(mixed != NULL);
    assert(strcmp(mixed->format, "Mixed") == 0);
    assert(strcmp(mixed->slot_label, "Slot 1") == 0);
    assert(strcmp(mixed->preview_path, ".userdata/shared/.minui/GBA/Test Mix.gba.0.bmp") == 0);
    assert(mixed->warning_count == 1);
    assert(strcmp(mixed->warnings[0], "Multiple payload formats found for this slot.") == 0);
}

static void test_numbered_retroarch_slots_stay_regular_slots(void) {
    char template[] = "/tmp/cs-states-slots-XXXXXX";
    char *root;
    char userdata_root[PATH_MAX];
    char shared_root[PATH_MAX];
    char core_root[PATH_MAX];
    char state_eight[PATH_MAX];
    char state_nine[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    int truncated = 1;
    const cs_state_entry *slot_eight;
    const cs_state_entry *slot_nine;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(userdata_root, sizeof(userdata_root), "%s/.userdata", root) > 0);
    assert(snprintf(shared_root, sizeof(shared_root), "%s/.userdata/shared", root) > 0);
    assert(snprintf(core_root, sizeof(core_root), "%s/.userdata/shared/GBA-gpSP", root) > 0);
    assert(snprintf(state_eight, sizeof(state_eight), "%s/Manual Slot.gba.state8", core_root) > 0);
    assert(snprintf(state_nine, sizeof(state_nine), "%s/Manual Slot.gba.state.9", core_root) > 0);

    make_dir(userdata_root);
    make_dir(shared_root);
    make_dir(core_root);
    write_file(state_eight, "slot8");
    write_file(state_nine, "slot9");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == 2);
    assert(truncated == 0);

    slot_eight = find_entry(entries, count, "Manual Slot.gba", "GBA-gpSP", 8);
    assert(slot_eight != NULL);
    assert(strcmp(slot_eight->slot_label, "Slot 9") == 0);
    assert(strcmp(slot_eight->kind, "slot") == 0);

    slot_nine = find_entry(entries, count, "Manual Slot.gba", "GBA-gpSP", 9);
    assert(slot_nine != NULL);
    assert(strcmp(slot_nine->slot_label, "Slot 10") == 0);
    assert(strcmp(slot_nine->kind, "slot") == 0);
}

static void test_state_collection_reports_truncation_without_failing(void) {
    char template[] = "/tmp/cs-states-limit-XXXXXX";
    char *root;
    char userdata_root[PATH_MAX];
    char shared_root[PATH_MAX];
    char core_root[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    size_t count_only = 0;
    int truncated = 0;
    int count_only_truncated = 1;
    size_t i;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(userdata_root, sizeof(userdata_root), "%s/.userdata", root) > 0);
    assert(snprintf(shared_root, sizeof(shared_root), "%s/.userdata/shared", root) > 0);
    assert(snprintf(core_root, sizeof(core_root), "%s/.userdata/shared/GBA-mGBA", root) > 0);

    make_dir(userdata_root);
    make_dir(shared_root);
    make_dir(core_root);

    for (i = 0; i < CS_STATE_MAX_ENTRIES + 1; ++i) {
        char state_path[PATH_MAX];

        assert(snprintf(state_path,
                        sizeof(state_path),
                        "%s/Game %03zu.gba.st0",
                        core_root,
                        i)
               > 0);
        write_file(state_path, "state");
    }

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, NULL, 0, &count_only, &count_only_truncated) == 0);
    assert(count_only == CS_STATE_MAX_ENTRIES + 1);
    assert(count_only_truncated == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == CS_STATE_MAX_ENTRIES + 1);
    assert(truncated == 1);
    assert(entries[0].title[0] != '\0');
}

static void test_slot_pointer_rejects_trailing_garbage(void) {
    char template[] = "/tmp/cs-states-slot-pointer-XXXXXX";
    char *root;
    char userdata_root[PATH_MAX];
    char shared_root[PATH_MAX];
    char minui_root[PATH_MAX];
    char emu_metadata_root[PATH_MAX];
    char core_root[PATH_MAX];
    char slot_pointer[PATH_MAX];
    char state_path[PATH_MAX];
    cs_paths paths = {0};
    const cs_platform_info *gba = cs_platform_find("GBA");
    cs_state_entry entries[CS_STATE_MAX_ENTRIES];
    size_t count = 0;
    int truncated = 1;
    const cs_state_entry *entry;

    assert(gba != NULL);
    root = mkdtemp(template);
    assert(root != NULL);

    assert(snprintf(userdata_root, sizeof(userdata_root), "%s/.userdata", root) > 0);
    assert(snprintf(shared_root, sizeof(shared_root), "%s/.userdata/shared", root) > 0);
    assert(snprintf(minui_root, sizeof(minui_root), "%s/.userdata/shared/.minui", root) > 0);
    assert(snprintf(emu_metadata_root, sizeof(emu_metadata_root), "%s/.userdata/shared/.minui/GBA", root) > 0);
    assert(snprintf(core_root, sizeof(core_root), "%s/.userdata/shared/GBA-mGBA", root) > 0);
    assert(snprintf(slot_pointer, sizeof(slot_pointer), "%s/Garbage Pointer.gba.txt", emu_metadata_root) > 0);
    assert(snprintf(state_path, sizeof(state_path), "%s/Garbage Pointer.gba.st0", core_root) > 0);

    make_dir(userdata_root);
    make_dir(shared_root);
    make_dir(minui_root);
    make_dir(emu_metadata_root);
    make_dir(core_root);
    write_file(slot_pointer, "0oops\n");
    write_file(state_path, "state");

    set_sdcard_root_realpath(root);
    assert(cs_paths_init(&paths) == 0);
    assert(cs_states_collect(&paths, gba, entries, CS_STATE_MAX_ENTRIES, &count, &truncated) == 0);
    assert(count == 1);
    assert(truncated == 0);

    entry = find_entry(entries, count, "Garbage Pointer.gba", "GBA-mGBA", 0);
    assert(entry != NULL);
    assert(entry->download_path_count == 1);
    assert(has_download_path(entry, ".userdata/shared/GBA-mGBA/Garbage Pointer.gba.st0") == 1);
    assert(has_download_path(entry, ".userdata/shared/.minui/GBA/Garbage Pointer.gba.txt") == 0);
}

int main(void) {
    test_fixture_states_are_grouped_and_metadata_is_attached();
    test_mixed_formats_share_one_slot_group();
    test_numbered_retroarch_slots_stay_regular_slots();
    test_state_collection_reports_truncation_without_failing();
    test_slot_pointer_rejects_trailing_garbage();
    return 0;
}
