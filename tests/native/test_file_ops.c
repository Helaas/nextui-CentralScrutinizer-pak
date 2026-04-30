#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cs_file_ops.h"

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static int file_content_equals(const char *path, const char *content) {
    char buffer[128];
    FILE *file;
    size_t expected_len;
    size_t read_len;

    assert(path != NULL);
    assert(content != NULL);

    file = fopen(path, "rb");
    assert(file != NULL);
    read_len = fread(buffer, 1, sizeof(buffer) - 1, file);
    assert(fclose(file) == 0);
    buffer[read_len] = '\0';
    expected_len = strlen(content);

    return read_len == expected_len && strcmp(buffer, content) == 0;
}

static int directory_contains_name(const char *path, const char *name) {
    DIR *dir;
    struct dirent *entry;
    int found = 0;

    assert(path != NULL);
    assert(name != NULL);

    dir = opendir(path);
    assert(dir != NULL);
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, name) == 0) {
            found = 1;
            break;
        }
    }
    assert(closedir(dir) == 0);

    return found;
}

int main(void) {
    char sandbox_template[] = "/tmp/cs-file-ops-XXXXXX";
    char root_path[1024];
    char source_path[1024];
    char renamed_path[1024];
    char managed_dir[1024];
    char symlinked_dir[1024];
    char outside_dir[1024];
    char escaped_source[1024];
    char escaped_write_target[1024];
    char hidden_leaf[1024];
    char hidden_parent[1024];
    char hidden_shared[1024];
    char hidden_userdata[1024];
    char nested_dir[1024];
    char deeper_dir[1024];
    char nested_file[1024];
    char deeper_file[1024];
    char fallback_source_path[1024];
    char fallback_renamed_path[1024];
    char folder_source_path[1024];
    char folder_renamed_path[1024];
    char folder_child_path[1024];
    char case_source_path[1024];
    char case_renamed_path[1024];
    char case_folder_source_path[1024];
    char case_folder_renamed_path[1024];
    char case_folder_child_path[1024];
    char forced_case_source_path[1024];
    char forced_case_target_path[1024];

    assert(cs_validate_relative_path("Game Boy Advance (GBA)/Golden Sun.gba") == 0);
    assert(cs_validate_relative_path("Game Boy Advance (GBA)/.media/Golden Sun.png") == 0);
    assert(cs_validate_path_component_with_flags("foo..bar.zip", 0) == 0);
    assert(cs_validate_path_component_with_flags(".keep", CS_PATH_FLAG_ALLOW_HIDDEN) == 0);
    assert(cs_validate_relative_path_with_flags(".userdata/shared/CentralScrutinizer", CS_PATH_FLAG_ALLOW_HIDDEN) == 0);
    assert(cs_validate_relative_path("../etc/passwd") != 0);
    assert(cs_validate_relative_path("/absolute/path") != 0);
    assert(cs_validate_relative_path("Game Boy Advance (GBA)\\Golden Sun.gba") != 0);
    assert(cs_validate_relative_path("Game Boy Advance (GBA)/.git/config") != 0);
    assert(cs_validate_relative_path("Game Boy Advance (GBA)/COM1.txt") != 0);
    assert(cs_validate_relative_path("Game Boy Advance (GBA)/trailingdot.") != 0);
    assert(cs_validate_relative_path("Game Boy Advance (GBA)/trailingspace ") != 0);
    assert(cs_validate_relative_path("Game Boy Advance (GBA)/") != 0);
    assert(cs_validate_relative_path("") != 0);
    assert(cs_validate_relative_path(".userdata/shared/CentralScrutinizer") != 0);

    assert(mkdtemp(sandbox_template) != NULL);
    assert(snprintf(root_path, sizeof(root_path), "%s/root", sandbox_template) > 0);
    assert(snprintf(managed_dir, sizeof(managed_dir), "%s/Game Boy Advance (GBA)", root_path) > 0);
    assert(snprintf(outside_dir, sizeof(outside_dir), "%s/outside", sandbox_template) > 0);
    assert(mkdir(root_path, 0775) == 0);
    assert(mkdir(managed_dir, 0775) == 0);
    assert(mkdir(outside_dir, 0775) == 0);

    assert(snprintf(source_path, sizeof(source_path), "%s/source.txt", managed_dir) > 0);
    assert(snprintf(renamed_path, sizeof(renamed_path), "%s/renamed.txt", managed_dir) > 0);
    assert(snprintf(nested_dir, sizeof(nested_dir), "%s/manual", managed_dir) > 0);
    assert(snprintf(deeper_dir, sizeof(deeper_dir), "%s/nested", nested_dir) > 0);
    assert(snprintf(nested_file, sizeof(nested_file), "%s/notes.txt", nested_dir) > 0);
    assert(snprintf(deeper_file, sizeof(deeper_file), "%s/deeper.txt", deeper_dir) > 0);
    assert(snprintf(fallback_source_path, sizeof(fallback_source_path), "%s/fallback-source.txt", managed_dir) > 0);
    assert(snprintf(fallback_renamed_path, sizeof(fallback_renamed_path), "%s/fallback-renamed.txt", managed_dir)
           > 0);
    assert(snprintf(folder_source_path, sizeof(folder_source_path), "%s/folder-source", managed_dir) > 0);
    assert(snprintf(folder_renamed_path, sizeof(folder_renamed_path), "%s/folder-renamed", managed_dir) > 0);
    assert(snprintf(folder_child_path, sizeof(folder_child_path), "%s/inside.txt", folder_source_path) > 0);
    assert(snprintf(case_source_path, sizeof(case_source_path), "%s/case-only.txt", managed_dir) > 0);
    assert(snprintf(case_renamed_path, sizeof(case_renamed_path), "%s/Case-Only.txt", managed_dir) > 0);
    assert(snprintf(case_folder_source_path, sizeof(case_folder_source_path), "%s/case-folder", managed_dir) > 0);
    assert(snprintf(case_folder_renamed_path, sizeof(case_folder_renamed_path), "%s/Case-Folder", managed_dir) > 0);
    assert(snprintf(case_folder_child_path, sizeof(case_folder_child_path), "%s/inside.txt", case_folder_source_path)
           > 0);
    assert(snprintf(forced_case_source_path, sizeof(forced_case_source_path), "%s/forced-source.txt", managed_dir)
           > 0);
    assert(snprintf(forced_case_target_path, sizeof(forced_case_target_path), "%s/forced-target.txt", managed_dir)
           > 0);

    write_file(source_path, "payload");
    assert(cs_safe_rename_under_root(root_path,
                                     "Game Boy Advance (GBA)/source.txt",
                                     "Game Boy Advance (GBA)/renamed.txt")
           == 0);
    assert(access(source_path, F_OK) != 0);
    assert(access(renamed_path, F_OK) == 0);
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/renamed.txt") == 0);
    assert(access(renamed_path, F_OK) != 0);

    write_file(fallback_source_path, "fallback");
    assert(setenv("CS_FORCE_RENAME_NOREPLACE_FALLBACK", "1", 1) == 0);
    assert(cs_safe_rename_under_root(root_path,
                                     "Game Boy Advance (GBA)/fallback-source.txt",
                                     "Game Boy Advance (GBA)/fallback-renamed.txt")
           == 0);
    assert(unsetenv("CS_FORCE_RENAME_NOREPLACE_FALLBACK") == 0);
    assert(access(fallback_source_path, F_OK) != 0);
    assert(access(fallback_renamed_path, F_OK) == 0);
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/fallback-renamed.txt") == 0);

    write_file(case_source_path, "case-only");
    assert(setenv("CS_FORCE_CASE_ONLY_RENAME_FALLBACK", "1", 1) == 0);
    assert(cs_safe_rename_under_root(root_path,
                                     "Game Boy Advance (GBA)/case-only.txt",
                                     "Game Boy Advance (GBA)/Case-Only.txt")
           == 0);
    assert(unsetenv("CS_FORCE_CASE_ONLY_RENAME_FALLBACK") == 0);
    assert(directory_contains_name(managed_dir, "case-only.txt") == 0);
    assert(directory_contains_name(managed_dir, "Case-Only.txt") == 1);
    assert(access(case_renamed_path, F_OK) == 0);
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/Case-Only.txt") == 0);

    write_file(forced_case_source_path, "source-kept");
    write_file(forced_case_target_path, "target-kept");
    assert(setenv("CS_FORCE_CASE_ONLY_RENAME_FALLBACK", "1", 1) == 0);
    assert(cs_safe_rename_under_root(root_path,
                                     "Game Boy Advance (GBA)/forced-source.txt",
                                     "Game Boy Advance (GBA)/forced-target.txt")
           != 0);
    assert(unsetenv("CS_FORCE_CASE_ONLY_RENAME_FALLBACK") == 0);
    assert(access(forced_case_source_path, F_OK) == 0);
    assert(file_content_equals(forced_case_target_path, "target-kept") == 1);
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/forced-source.txt") == 0);
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/forced-target.txt") == 0);

    assert(mkdir(folder_source_path, 0775) == 0);
    write_file(folder_child_path, "folder");
    assert(setenv("CS_FORCE_RENAME_NOREPLACE_FALLBACK", "1", 1) == 0);
    assert(cs_safe_rename_under_root(root_path,
                                     "Game Boy Advance (GBA)/folder-source",
                                     "Game Boy Advance (GBA)/folder-renamed")
           == 0);
    assert(unsetenv("CS_FORCE_RENAME_NOREPLACE_FALLBACK") == 0);
    assert(access(folder_source_path, F_OK) != 0);
    assert(access(folder_renamed_path, F_OK) == 0);
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/folder-renamed") == 0);

    assert(mkdir(case_folder_source_path, 0775) == 0);
    write_file(case_folder_child_path, "case-folder");
    assert(setenv("CS_FORCE_CASE_ONLY_RENAME_FALLBACK", "1", 1) == 0);
    assert(cs_safe_rename_under_root(root_path,
                                     "Game Boy Advance (GBA)/case-folder",
                                     "Game Boy Advance (GBA)/Case-Folder")
           == 0);
    assert(unsetenv("CS_FORCE_CASE_ONLY_RENAME_FALLBACK") == 0);
    assert(directory_contains_name(managed_dir, "case-folder") == 0);
    assert(directory_contains_name(managed_dir, "Case-Folder") == 1);
    assert(access(case_folder_renamed_path, F_OK) == 0);
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/Case-Folder") == 0);

    assert(mkdir(nested_dir, 0775) == 0);
    assert(mkdir(deeper_dir, 0775) == 0);
    write_file(nested_file, "nested");
    write_file(deeper_file, "deeper");
    assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/manual") == 0);
    assert(access(nested_dir, F_OK) != 0);
    assert(access(deeper_file, F_OK) != 0);

    assert(snprintf(hidden_leaf,
                    sizeof(hidden_leaf),
                    "%s/.userdata/shared/CentralScrutinizer/imports",
                    root_path)
           > 0);
    assert(snprintf(hidden_parent,
                    sizeof(hidden_parent),
                    "%s/.userdata/shared/CentralScrutinizer",
                    root_path)
           > 0);
    assert(snprintf(hidden_shared, sizeof(hidden_shared), "%s/.userdata/shared", root_path) > 0);
    assert(snprintf(hidden_userdata, sizeof(hidden_userdata), "%s/.userdata", root_path) > 0);
    assert(mkdir(hidden_userdata, 0775) == 0);
    assert(mkdir(hidden_shared, 0775) == 0);
    assert(mkdir(hidden_parent, 0775) == 0);
    assert(cs_safe_create_directory_under_root_with_flags(root_path,
                                                          ".userdata/shared/CentralScrutinizer/imports",
                                                          CS_PATH_FLAG_ALLOW_HIDDEN)
           == 0);
    assert(access(hidden_leaf, F_OK) == 0);
    assert(cs_safe_delete_under_root_with_flags(root_path,
                                                ".userdata/shared/CentralScrutinizer/imports",
                                                CS_PATH_FLAG_ALLOW_HIDDEN)
           == 0);
    assert(access(hidden_leaf, F_OK) != 0);

    assert(snprintf(symlinked_dir, sizeof(symlinked_dir), "%s/Linked", root_path) > 0);
    assert(symlink(outside_dir, symlinked_dir) == 0);
    assert(snprintf(escaped_source, sizeof(escaped_source), "%s/source.txt", outside_dir) > 0);
    assert(snprintf(escaped_write_target, sizeof(escaped_write_target), "%s/write-target.txt", outside_dir) > 0);
    write_file(escaped_source, "outside");
    write_file(escaped_write_target, "outside-write");

    assert(cs_safe_delete_under_root(root_path, "Linked/source.txt") == -1);
    assert(access(escaped_source, F_OK) == 0);

    {
        char write_path[1024];
        char atomic_path[1024];
        char link_path[1024];
        char read_buffer[64];
        FILE *reader;
        size_t read_len;

        assert(snprintf(write_path, sizeof(write_path), "%s/notes.txt", managed_dir) > 0);
        assert(snprintf(atomic_path, sizeof(atomic_path), "%s/atomic.txt", managed_dir) > 0);
        assert(snprintf(link_path, sizeof(link_path), "%s/write-link.txt", managed_dir) > 0);
        write_file(write_path, "initial");
        assert(cs_safe_write_under_root_with_flags(root_path,
                                                   "Game Boy Advance (GBA)/notes.txt",
                                                   "replaced payload",
                                                   strlen("replaced payload"),
                                                   0)
               == 0);
        reader = fopen(write_path, "rb");
        assert(reader != NULL);
        read_len = fread(read_buffer, 1, sizeof(read_buffer) - 1, reader);
        read_buffer[read_len] = '\0';
        assert(fclose(reader) == 0);
        assert(strcmp(read_buffer, "replaced payload") == 0);

        write_file(atomic_path, "preserve");
        assert(chmod(managed_dir, 0555) == 0);
        assert(cs_safe_write_under_root_with_flags(root_path,
                                                   "Game Boy Advance (GBA)/atomic.txt",
                                                   "new payload",
                                                   strlen("new payload"),
                                                   0)
               == -1);
        assert(chmod(managed_dir, 0775) == 0);
        reader = fopen(atomic_path, "rb");
        assert(reader != NULL);
        read_len = fread(read_buffer, 1, sizeof(read_buffer) - 1, reader);
        read_buffer[read_len] = '\0';
        assert(fclose(reader) == 0);
        assert(strcmp(read_buffer, "preserve") == 0);

        assert(symlink(escaped_write_target, link_path) == 0);
        assert(cs_safe_write_under_root_with_flags(root_path,
                                                   "Game Boy Advance (GBA)/write-link.txt",
                                                   "blocked",
                                                   strlen("blocked"),
                                                   0)
               == -1);
        reader = fopen(escaped_write_target, "rb");
        assert(reader != NULL);
        read_len = fread(read_buffer, 1, sizeof(read_buffer) - 1, reader);
        read_buffer[read_len] = '\0';
        assert(fclose(reader) == 0);
        assert(strcmp(read_buffer, "outside-write") == 0);
        assert(unlink(link_path) == 0);

        assert(cs_safe_write_under_root_with_flags(root_path,
                                                   "Game Boy Advance (GBA)/does-not-exist.txt",
                                                   "x",
                                                   1,
                                                   0)
               == -1);
        assert(cs_safe_write_under_root_with_flags(root_path,
                                                   "../escape.txt",
                                                   "x",
                                                   1,
                                                   0)
               == -1);
        assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/notes.txt") == 0);
        assert(cs_safe_delete_under_root(root_path, "Game Boy Advance (GBA)/atomic.txt") == 0);
    }

    assert(remove(escaped_write_target) == 0);
    assert(remove(escaped_source) == 0);
    assert(unlink(symlinked_dir) == 0);
    assert(rmdir(hidden_parent) == 0);
    assert(rmdir(hidden_shared) == 0);
    assert(rmdir(hidden_userdata) == 0);
    assert(rmdir(outside_dir) == 0);
    assert(rmdir(managed_dir) == 0);
    assert(rmdir(root_path) == 0);
    assert(rmdir(sandbox_template) == 0);

    return 0;
}
