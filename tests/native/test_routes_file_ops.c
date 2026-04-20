#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int cs_file_search_collect_for_test(const char *absolute_path,
                                    const char *relative_path,
                                    const char *query,
                                    size_t *count_out,
                                    int *truncated_out);

static void make_dir(const char *path) {
    assert(mkdir(path, 0700) == 0);
}

static void write_file(const char *path) {
    FILE *file = fopen(path, "wb");

    assert(file != NULL);
    assert(fwrite("data", 1, 4, file) == 4);
    assert(fclose(file) == 0);
}

int main(void) {
    char template[] = "/tmp/cs-file-search-XXXXXX";
    char *root;
    char nested_dir[PATH_MAX];
    char nested_file[PATH_MAX];
    char path[PATH_MAX];
    size_t count = 0;
    int truncated = 0;
    int i;

    root = mkdtemp(template);
    assert(root != NULL);
    assert(snprintf(nested_dir, sizeof(nested_dir), "%s/nested", root) > 0);
    assert(snprintf(nested_file, sizeof(nested_file), "%s/nested/match-nested.txt", root) > 0);

    make_dir(nested_dir);
    write_file(nested_file);

    for (i = 0; i < 199; ++i) {
        assert(snprintf(path, sizeof(path), "%s/match-%03d.txt", root, i) > 0);
        write_file(path);
    }

    assert(cs_file_search_collect_for_test(root, "", "match", &count, &truncated) == 0);
    assert(count == 200);
    assert(truncated == 0);

    for (i = 199; i < 250; ++i) {
        assert(snprintf(path, sizeof(path), "%s/match-%03d.txt", root, i) > 0);
        write_file(path);
    }

    assert(cs_file_search_collect_for_test(root, "", "match", &count, &truncated) == 0);
    assert(count == 200);
    assert(truncated == 1);

    for (i = 0; i < 250; ++i) {
        assert(snprintf(path, sizeof(path), "%s/match-%03d.txt", root, i) > 0);
        assert(unlink(path) == 0);
    }
    assert(unlink(nested_file) == 0);
    assert(rmdir(nested_dir) == 0);
    assert(rmdir(root) == 0);
    return 0;
}
