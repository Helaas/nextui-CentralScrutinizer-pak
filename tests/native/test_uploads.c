#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cs_paths.h"
#include "cs_uploads.h"

static void write_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite(content, 1, strlen(content), file) == strlen(content));
    assert(fclose(file) == 0);
}

static void read_file(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "rb");
    size_t read_bytes;

    assert(file != NULL);
    read_bytes = fread(buffer, 1, size - 1, file);
    buffer[read_bytes] = '\0';
    assert(fclose(file) == 0);
}

static void path_join(char *dst, size_t size, const char *left, const char *right) {
    assert(snprintf(dst, size, "%s/%s", left, right) > 0);
}

static int run_promote_child(const cs_upload_plan *plan, int start_fd) {
    char token;
    int rc;

    if (read(start_fd, &token, 1) != 1) {
        _exit(2);
    }

    rc = cs_upload_promote(plan);
    _exit(rc == 0 ? 0 : 1);
}

static void test_existing_destination_is_rejected(void) {
    cs_upload_plan conflict;
    char sandbox_template[] = "/tmp/cs-upload-native-XXXXXX";
    char incoming_path[CS_PATH_MAX];
    char final_path[CS_PATH_MAX];
    char final_dir[CS_PATH_MAX];
    char temp_root[CS_PATH_MAX];
    char original[32];

    assert(mkdtemp(sandbox_template) != NULL);
    path_join(final_dir, sizeof(final_dir), sandbox_template, "dest");
    path_join(temp_root, sizeof(temp_root), sandbox_template, "tmp");
    path_join(incoming_path, sizeof(incoming_path), temp_root, "incoming.gba");
    path_join(final_path, sizeof(final_path), final_dir, "existing.gba");

    assert(mkdir(final_dir, 0775) == 0);
    assert(mkdir(temp_root, 0775) == 0);
    write_file(incoming_path, "new-bytes");
    write_file(final_path, "old-bytes");

    memset(&conflict, 0, sizeof(conflict));
    assert(snprintf(conflict.temp_path, sizeof(conflict.temp_path), "%s", incoming_path) > 0);
    assert(snprintf(conflict.final_path, sizeof(conflict.final_path), "%s", final_path) > 0);
    assert(snprintf(conflict.temp_root, sizeof(conflict.temp_root), "%s", temp_root) > 0);
    assert(snprintf(conflict.final_root, sizeof(conflict.final_root), "%s", sandbox_template) > 0);
    assert(snprintf(conflict.temp_guard_root, sizeof(conflict.temp_guard_root), "%s", sandbox_template)
           > 0);
    assert(snprintf(conflict.final_guard_root, sizeof(conflict.final_guard_root), "%s", sandbox_template)
           > 0);

    assert(cs_upload_promote(&conflict) == -1);
    assert(access(incoming_path, F_OK) == 0);
    assert(access(final_path, F_OK) == 0);
    read_file(final_path, original, sizeof(original));
    assert(strcmp(original, "old-bytes") == 0);

    assert(remove(incoming_path) == 0);
    assert(remove(final_path) == 0);
    assert(rmdir(temp_root) == 0);
    assert(rmdir(final_dir) == 0);
    assert(rmdir(sandbox_template) == 0);
}

static void test_existing_destination_is_replaced(void) {
    cs_upload_plan replacement;
    char sandbox_template[] = "/tmp/cs-upload-replace-XXXXXX";
    char incoming_path[CS_PATH_MAX];
    char final_path[CS_PATH_MAX];
    char final_dir[CS_PATH_MAX];
    char temp_root[CS_PATH_MAX];
    char updated[32];

    assert(mkdtemp(sandbox_template) != NULL);
    path_join(final_dir, sizeof(final_dir), sandbox_template, "dest");
    path_join(temp_root, sizeof(temp_root), sandbox_template, "tmp");
    path_join(incoming_path, sizeof(incoming_path), temp_root, "incoming.png");
    path_join(final_path, sizeof(final_path), final_dir, "existing.png");

    assert(mkdir(final_dir, 0775) == 0);
    assert(mkdir(temp_root, 0775) == 0);
    write_file(incoming_path, "new-bytes");
    write_file(final_path, "old-bytes");

    memset(&replacement, 0, sizeof(replacement));
    assert(snprintf(replacement.temp_path, sizeof(replacement.temp_path), "%s", incoming_path) > 0);
    assert(snprintf(replacement.final_path, sizeof(replacement.final_path), "%s", final_path) > 0);
    assert(snprintf(replacement.temp_root, sizeof(replacement.temp_root), "%s", temp_root) > 0);
    assert(snprintf(replacement.final_root, sizeof(replacement.final_root), "%s", sandbox_template) > 0);
    assert(snprintf(replacement.temp_guard_root,
                    sizeof(replacement.temp_guard_root),
                    "%s",
                    sandbox_template)
           > 0);
    assert(snprintf(replacement.final_guard_root,
                    sizeof(replacement.final_guard_root),
                    "%s",
                    sandbox_template)
           > 0);

    assert(cs_upload_promote_replace(&replacement) == 0);
    assert(access(incoming_path, F_OK) != 0);
    read_file(final_path, updated, sizeof(updated));
    assert(strcmp(updated, "new-bytes") == 0);

    assert(remove(final_path) == 0);
    assert(rmdir(temp_root) == 0);
    assert(rmdir(final_dir) == 0);
    assert(rmdir(sandbox_template) == 0);
}

static void test_symlink_escape_is_rejected(void) {
    cs_paths paths = {0};
    char sandbox_template[] = "/tmp/cs-upload-symlink-XXXXXX";
    char managed_root[CS_PATH_MAX];
    char outside_root[CS_PATH_MAX];
    char temp_root[CS_PATH_MAX];
    char roms_root[CS_PATH_MAX];
    char system_link[CS_PATH_MAX];

    assert(mkdtemp(sandbox_template) != NULL);
    path_join(managed_root, sizeof(managed_root), sandbox_template, "managed");
    path_join(outside_root, sizeof(outside_root), sandbox_template, "outside");
    path_join(temp_root, sizeof(temp_root), managed_root, "tmp");
    path_join(roms_root, sizeof(roms_root), managed_root, "roms");

    assert(mkdir(managed_root, 0775) == 0);
    assert(mkdir(outside_root, 0775) == 0);
    assert(mkdir(temp_root, 0775) == 0);
    assert(mkdir(roms_root, 0775) == 0);

    path_join(system_link, sizeof(system_link), roms_root, "Game Boy Advance (GBA)");
    assert(symlink(outside_root, system_link) == 0);

    assert(snprintf(paths.shared_state_root, sizeof(paths.shared_state_root), "%s", managed_root) > 0);
    assert(snprintf(paths.temp_upload_root, sizeof(paths.temp_upload_root), "%s", temp_root) > 0);
    assert(snprintf(paths.roms_root, sizeof(paths.roms_root), "%s", roms_root) > 0);

    assert(cs_upload_plan_make(&paths,
                               paths.roms_root,
                               managed_root,
                               "Game Boy Advance (GBA)",
                               "symlink-escape.gba",
                               0,
                               &(cs_upload_plan) {0})
           == -1);

    assert(unlink(system_link) == 0);
    assert(rmdir(temp_root) == 0);
    assert(rmdir(roms_root) == 0);
    assert(rmdir(outside_root) == 0);
    assert(rmdir(managed_root) == 0);
    assert(rmdir(sandbox_template) == 0);
}

static void test_symlinked_temp_root_is_rejected(void) {
    cs_paths paths = {0};
    char sandbox_template[] = "/tmp/cs-upload-temp-root-XXXXXX";
    char shared_root[CS_PATH_MAX];
    char outside_root[CS_PATH_MAX];
    char uploads_link[CS_PATH_MAX];
    char escaped_tmp[CS_PATH_MAX];

    assert(mkdtemp(sandbox_template) != NULL);
    path_join(shared_root, sizeof(shared_root), sandbox_template, "shared");
    path_join(outside_root, sizeof(outside_root), sandbox_template, "outside");
    path_join(uploads_link, sizeof(uploads_link), shared_root, "uploads");
    path_join(escaped_tmp, sizeof(escaped_tmp), outside_root, "tmp");

    assert(mkdir(shared_root, 0775) == 0);
    assert(mkdir(outside_root, 0775) == 0);
    assert(symlink(outside_root, uploads_link) == 0);

    assert(snprintf(paths.shared_state_root, sizeof(paths.shared_state_root), "%s", shared_root) > 0);
    assert(snprintf(paths.temp_upload_root,
                    sizeof(paths.temp_upload_root),
                    "%s/uploads/tmp",
                    shared_root)
           > 0);

    assert(cs_upload_prepare_temp_root(&paths) == -1);
    assert(access(escaped_tmp, F_OK) != 0);

    if (access(escaped_tmp, F_OK) == 0) {
        assert(rmdir(escaped_tmp) == 0);
    }
    assert(unlink(uploads_link) == 0);
    assert(rmdir(outside_root) == 0);
    assert(rmdir(shared_root) == 0);
    assert(rmdir(sandbox_template) == 0);
}

static void test_symlinked_guard_root_is_rejected(void) {
    cs_paths paths = {0};
    char sandbox_template[] = "/tmp/cs-upload-guard-root-XXXXXX";
    char link_root[CS_PATH_MAX];
    char outside_root[CS_PATH_MAX];
    char escaped_tmp[CS_PATH_MAX];

    assert(mkdtemp(sandbox_template) != NULL);
    path_join(link_root, sizeof(link_root), sandbox_template, "shared-link");
    path_join(outside_root, sizeof(outside_root), sandbox_template, "outside");
    path_join(escaped_tmp, sizeof(escaped_tmp), outside_root, "uploads/tmp");

    assert(mkdir(outside_root, 0775) == 0);
    assert(symlink(outside_root, link_root) == 0);

    assert(snprintf(paths.shared_state_root, sizeof(paths.shared_state_root), "%s", link_root) > 0);
    assert(snprintf(paths.temp_upload_root,
                    sizeof(paths.temp_upload_root),
                    "%s/uploads/tmp",
                    link_root)
           > 0);

    assert(cs_upload_prepare_temp_root(&paths) == -1);
    assert(access(escaped_tmp, F_OK) != 0);

    if (access(escaped_tmp, F_OK) == 0) {
        assert(rmdir(escaped_tmp) == 0);
    }
    if (access(outside_root, F_OK) == 0) {
        char uploads_dir[CS_PATH_MAX];

        path_join(uploads_dir, sizeof(uploads_dir), outside_root, "uploads");
        if (access(uploads_dir, F_OK) == 0) {
            assert(rmdir(uploads_dir) == 0);
        }
    }
    assert(unlink(link_root) == 0);
    assert(rmdir(outside_root) == 0);
    assert(rmdir(sandbox_template) == 0);
}

static void test_concurrent_no_replace_promotion(void) {
    cs_upload_plan plan1;
    cs_upload_plan plan2;
    char sandbox_template[] = "/tmp/cs-upload-race-XXXXXX";
    char final_dir[CS_PATH_MAX];
    char final_path[CS_PATH_MAX];
    char temp_root[CS_PATH_MAX];
    char temp_a[CS_PATH_MAX];
    char temp_b[CS_PATH_MAX];
    char final_contents[32];
    int gate_a[2];
    int gate_b[2];
    pid_t child_a;
    pid_t child_b;
    int status_a = 0;
    int status_b = 0;
    int success_count = 0;

    assert(mkdtemp(sandbox_template) != NULL);
    path_join(final_dir, sizeof(final_dir), sandbox_template, "roms");
    path_join(temp_root, sizeof(temp_root), sandbox_template, "tmp");
    assert(mkdir(final_dir, 0775) == 0);
    assert(mkdir(temp_root, 0775) == 0);
    path_join(final_path, sizeof(final_path), final_dir, "race.gba");
    path_join(temp_a, sizeof(temp_a), temp_root, "incoming-a.gba");
    path_join(temp_b, sizeof(temp_b), temp_root, "incoming-b.gba");

    write_file(temp_a, "first");
    write_file(temp_b, "second");

    memset(&plan1, 0, sizeof(plan1));
    memset(&plan2, 0, sizeof(plan2));
    assert(snprintf(plan1.temp_path, sizeof(plan1.temp_path), "%s", temp_a) > 0);
    assert(snprintf(plan1.final_path, sizeof(plan1.final_path), "%s", final_path) > 0);
    assert(snprintf(plan1.temp_root, sizeof(plan1.temp_root), "%s", temp_root) > 0);
    assert(snprintf(plan1.final_root, sizeof(plan1.final_root), "%s", sandbox_template) > 0);
    assert(snprintf(plan1.temp_guard_root, sizeof(plan1.temp_guard_root), "%s", sandbox_template)
           > 0);
    assert(snprintf(plan1.final_guard_root, sizeof(plan1.final_guard_root), "%s", sandbox_template)
           > 0);
    assert(snprintf(plan2.temp_path, sizeof(plan2.temp_path), "%s", temp_b) > 0);
    assert(snprintf(plan2.final_path, sizeof(plan2.final_path), "%s", final_path) > 0);
    assert(snprintf(plan2.temp_root, sizeof(plan2.temp_root), "%s", temp_root) > 0);
    assert(snprintf(plan2.final_root, sizeof(plan2.final_root), "%s", sandbox_template) > 0);
    assert(snprintf(plan2.temp_guard_root, sizeof(plan2.temp_guard_root), "%s", sandbox_template)
           > 0);
    assert(snprintf(plan2.final_guard_root, sizeof(plan2.final_guard_root), "%s", sandbox_template)
           > 0);

    assert(pipe(gate_a) == 0);
    assert(pipe(gate_b) == 0);

    child_a = fork();
    assert(child_a >= 0);
    if (child_a == 0) {
        close(gate_a[1]);
        close(gate_b[0]);
        close(gate_b[1]);
        run_promote_child(&plan1, gate_a[0]);
    }

    child_b = fork();
    assert(child_b >= 0);
    if (child_b == 0) {
        close(gate_b[1]);
        close(gate_a[0]);
        close(gate_a[1]);
        run_promote_child(&plan2, gate_b[0]);
    }

    close(gate_a[0]);
    close(gate_b[0]);
    assert(write(gate_a[1], "x", 1) == 1);
    assert(write(gate_b[1], "y", 1) == 1);
    close(gate_a[1]);
    close(gate_b[1]);

    assert(waitpid(child_a, &status_a, 0) == child_a);
    assert(waitpid(child_b, &status_b, 0) == child_b);
    assert(WIFEXITED(status_a));
    assert(WIFEXITED(status_b));

    if (WEXITSTATUS(status_a) == 0) {
        success_count++;
    }
    if (WEXITSTATUS(status_b) == 0) {
        success_count++;
    }
    assert(success_count == 1);

    assert(access(final_path, F_OK) == 0);
    read_file(final_path, final_contents, sizeof(final_contents));
    assert(strcmp(final_contents, "first") == 0 || strcmp(final_contents, "second") == 0);

    if (access(temp_a, F_OK) == 0) {
        assert(remove(temp_a) == 0);
    }
    if (access(temp_b, F_OK) == 0) {
        assert(remove(temp_b) == 0);
    }
    assert(remove(final_path) == 0);
    assert(rmdir(temp_root) == 0);
    assert(rmdir(final_dir) == 0);
    assert(rmdir(sandbox_template) == 0);
}

static void test_reserved_temp_paths_are_unique(void) {
    cs_paths paths = {0};
    char sandbox_template[] = "/tmp/cs-upload-reserve-XXXXXX";
    char path_buffer[CS_PATH_MAX];
    char temp_path_a[CS_PATH_MAX];
    char temp_path_b[CS_PATH_MAX];

    assert(mkdtemp(sandbox_template) != NULL);
    assert(snprintf(paths.shared_state_root,
                    sizeof(paths.shared_state_root),
                    "%s/.userdata/shared/CentralScrutinizer",
                    sandbox_template)
           > 0);
    assert(snprintf(paths.sdcard_root, sizeof(paths.sdcard_root), "%s", sandbox_template) > 0);
    assert(snprintf(paths.temp_upload_root,
                    sizeof(paths.temp_upload_root),
                    "%s/uploads/tmp",
                    paths.shared_state_root)
           > 0);

    assert(cs_upload_reserve_temp_path(&paths, "same-name.gba", temp_path_a, sizeof(temp_path_a)) == 0);
    assert(cs_upload_reserve_temp_path(&paths, "same-name.gba", temp_path_b, sizeof(temp_path_b)) == 0);
    assert(strcmp(temp_path_a, temp_path_b) != 0);
    assert(access(temp_path_a, F_OK) == 0);
    assert(access(temp_path_b, F_OK) == 0);

    assert(remove(temp_path_a) == 0);
    assert(remove(temp_path_b) == 0);
    assert(rmdir(paths.temp_upload_root) == 0);
    assert(snprintf(path_buffer, sizeof(path_buffer), "%s/uploads", paths.shared_state_root) > 0);
    assert(rmdir(path_buffer) == 0);
    assert(rmdir(paths.shared_state_root) == 0);
    assert(snprintf(path_buffer, sizeof(path_buffer), "%s/.userdata/shared", sandbox_template) > 0);
    assert(rmdir(path_buffer) == 0);
    assert(snprintf(path_buffer, sizeof(path_buffer), "%s/.userdata", sandbox_template) > 0);
    assert(rmdir(path_buffer) == 0);
    assert(rmdir(sandbox_template) == 0);
}

int main(void) {
    cs_paths paths;
    cs_upload_plan plan;

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    assert(cs_paths_init(&paths) == 0);

    assert(cs_upload_plan_make(&paths,
                               paths.roms_root,
                               paths.sdcard_root,
                               "Game Boy Advance (GBA)",
                               "Golden Sun.gba",
                               0,
                               &plan)
           == 0);
    assert(strstr(plan.final_path, "Roms/Game Boy Advance (GBA)/Golden Sun.gba") != NULL);
    assert(plan.temp_path[0] != '\0');

    assert(cs_upload_plan_make(&paths,
                               paths.saves_root,
                               paths.sdcard_root,
                               "GBA",
                               "Golden Sun.sav",
                               0,
                               &plan)
           == 0);
    assert(strstr(plan.final_path, "Saves/GBA/Golden Sun.sav") != NULL);

    assert(cs_upload_plan_make(&paths,
                               paths.bios_root,
                               paths.sdcard_root,
                               "PS",
                               "scph1001-alt.bin",
                               0,
                               &plan)
           == 0);
    assert(strstr(plan.final_path, "Bios/PS/scph1001-alt.bin") != NULL);

    assert(cs_upload_plan_make(&paths,
                               paths.sdcard_root,
                               paths.sdcard_root,
                               ".userdata/shared/CentralScrutinizer/imports",
                               "notes.txt",
                               CS_PATH_FLAG_ALLOW_HIDDEN,
                               &plan)
           == 0);
    assert(strstr(plan.final_path,
                  ".userdata/shared/CentralScrutinizer/imports/notes.txt")
           != NULL);
    assert(cs_upload_plan_make(&paths,
                               paths.sdcard_root,
                               paths.sdcard_root,
                               ".userdata/shared/CentralScrutinizer/imports",
                               "notes.txt",
                               0,
                               &plan)
           == -1);

    assert(plan.final_path[0] != '\0');
    assert(plan.temp_path[0] != '\0');

    test_existing_destination_is_replaced();
    test_existing_destination_is_rejected();
    test_symlink_escape_is_rejected();
    test_symlinked_temp_root_is_rejected();
    test_symlinked_guard_root_is_rejected();
    test_concurrent_no_replace_promotion();
    test_reserved_temp_paths_are_unique();

    return 0;
}
