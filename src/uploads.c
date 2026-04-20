#include "cs_uploads.h"

#include "cs_file_ops.h"
#include "cs_rename_fallback_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__APPLE__)
extern int renamex_np(const char *from, const char *to, unsigned int flags);
extern int renameatx_np(int fromfd, const char *from, int tofd, const char *to, unsigned int flags);
#endif

#if defined(__linux__) && !defined(RENAME_NOREPLACE)
#define RENAME_NOREPLACE (1U)
#endif

static int cs_upload_write_path(char *dst, size_t size, const char *fmt, ...) {
    va_list args;
    int written;

    if (!dst || size == 0 || !fmt) {
        return -1;
    }

    va_start(args, fmt);
    written = vsnprintf(dst, size, fmt, args);
    va_end(args);

    if (written < 0 || (size_t) written >= size) {
        return -1;
    }

    return 0;
}

static int cs_upload_component_is_safe(const char *value) {
    return cs_validate_path_component_with_flags(value, CS_PATH_FLAG_ALLOW_HIDDEN) == 0;
}

static unsigned long cs_upload_nonce(void) {
    static atomic_ulong counter = ATOMIC_VAR_INIT(0);
    struct timespec now = {0};
    unsigned long sequence;

    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        now.tv_nsec = 0;
    }

    sequence = atomic_fetch_add_explicit(&counter, 1, memory_order_relaxed) + 1;
    return ((unsigned long) now.tv_nsec ^ sequence ^ (unsigned long) getpid());
}

int cs_upload_reserve_temp_path(const cs_paths *paths,
                                const char *filename,
                                char *buffer,
                                size_t buffer_len) {
    size_t attempt;

    if (!paths || !filename || !buffer || buffer_len == 0) {
        return -1;
    }
    if (!cs_upload_component_is_safe(filename)) {
        return -1;
    }
    if (cs_upload_prepare_temp_root(paths) != 0) {
        return -1;
    }

    for (attempt = 0; attempt < 256; ++attempt) {
        int fd;

        if (cs_upload_write_path(buffer,
                                 buffer_len,
                                 "%s/.incoming-%ld-%lu-%s",
                                 paths->temp_upload_root,
                                 (long) getpid(),
                                 cs_upload_nonce(),
                                 filename)
            != 0) {
            return -1;
        }

        fd = open(buffer, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (fd >= 0) {
            if (close(fd) != 0) {
                (void) unlink(buffer);
                return -1;
            }
            return 0;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }

    errno = EEXIST;
    return -1;
}

static int cs_upload_extract_parent(const char *path, char *parent, size_t parent_size) {
    const char *slash;
    size_t length;

    if (!path || !parent || parent_size == 0) {
        return -1;
    }

    slash = strrchr(path, '/');
    if (!slash || slash == path) {
        return -1;
    }

    length = (size_t) (slash - path);
    if (length >= parent_size) {
        return -1;
    }

    memcpy(parent, path, length);
    parent[length] = '\0';
    return 0;
}

static int cs_upload_is_declared_under_root(const char *path, const char *root) {
    size_t root_len;

    if (!path || !root) {
        return 0;
    }

    root_len = strlen(root);
    if (root_len == 0 || strncmp(path, root, root_len) != 0) {
        return 0;
    }

    return path[root_len] == '\0' || path[root_len] == '/';
}

static int cs_upload_write_relative_path(const char *root,
                                         const char *path,
                                         char *relative,
                                         size_t relative_size) {
    const char *suffix;

    if (!root || !path || !relative || relative_size == 0) {
        return -1;
    }
    if (!cs_upload_is_declared_under_root(path, root)) {
        return -1;
    }

    suffix = path + strlen(root);
    if (*suffix == '/') {
        suffix++;
    }

    return cs_upload_write_path(relative, relative_size, "%s", suffix);
}

static int cs_upload_extract_name(const char *path, char *name, size_t name_size) {
    const char *slash;
    const char *leaf;

    if (!path || !name || name_size == 0) {
        return -1;
    }

    slash = strrchr(path, '/');
    leaf = slash ? slash + 1 : path;
    if (leaf[0] == '\0') {
        return -1;
    }

    return cs_upload_write_path(name, name_size, "%s", leaf);
}

static int cs_upload_open_root_directory(const char *path) {
    if (!path) {
        return -1;
    }

    return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
}

static int cs_upload_open_directory_under_root(const char *root, const char *path) {
    char relative[CS_PATH_MAX];
    int current_fd = -1;

    if (cs_upload_write_relative_path(root, path, relative, sizeof(relative)) != 0) {
        return -1;
    }

    current_fd = cs_upload_open_root_directory(root);
    if (current_fd < 0) {
        return -1;
    }

    if (relative[0] != '\0') {
        const char *cursor = relative;

        while (*cursor != '\0') {
            const char *slash = strchr(cursor, '/');
            size_t length = slash ? (size_t) (slash - cursor) : strlen(cursor);
            char component[CS_PATH_MAX];
            int next_fd;

            if (length == 0 || length >= sizeof(component)) {
                close(current_fd);
                return -1;
            }

            memcpy(component, cursor, length);
            component[length] = '\0';
            next_fd = openat(current_fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            close(current_fd);
            if (next_fd < 0) {
                return -1;
            }

            current_fd = next_fd;
            cursor = slash ? slash + 1 : cursor + length;
        }
    }

    return current_fd;
}

static int cs_upload_prepare_directory_within_root(const char *root, const char *path) {
    char relative[CS_PATH_MAX];
    int current_fd = -1;

    if (cs_upload_write_relative_path(root, path, relative, sizeof(relative)) != 0) {
        return -1;
    }

    current_fd = cs_upload_open_root_directory(root);
    if (current_fd < 0) {
        return -1;
    }

    if (relative[0] != '\0') {
        const char *cursor = relative;

        while (*cursor != '\0') {
            const char *slash = strchr(cursor, '/');
            size_t length = slash ? (size_t) (slash - cursor) : strlen(cursor);
            char component[CS_PATH_MAX];
            int next_fd;

            if (length == 0 || length >= sizeof(component)) {
                close(current_fd);
                return -1;
            }

            memcpy(component, cursor, length);
            component[length] = '\0';

            next_fd = openat(current_fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
            if (next_fd < 0) {
                if (errno != ENOENT || mkdirat(current_fd, component, 0775) != 0) {
                    close(current_fd);
                    return -1;
                }

                next_fd = openat(current_fd, component, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
                if (next_fd < 0) {
                    close(current_fd);
                    return -1;
                }
            }

            close(current_fd);
            current_fd = next_fd;
            cursor = slash ? slash + 1 : cursor + length;
        }
    }

    close(current_fd);
    return 0;
}

static int cs_upload_atomic_promote_no_replace(int temp_dir_fd,
                                               const char *temp_name,
                                               int final_dir_fd,
                                               const char *final_name,
                                               const struct stat *temp_st) {
    struct stat final_st;
    int force_fallback;

    if (!temp_name || !final_name || !temp_st) {
        return -1;
    }

    force_fallback = cs_rename_noreplace_force_fallback();

#if defined(__APPLE__) && defined(RENAME_EXCL)
    if (!force_fallback) {
        if (renameatx_np(temp_dir_fd, temp_name, final_dir_fd, final_name, RENAME_EXCL) != 0) {
            if (!cs_rename_noreplace_should_fallback(errno)) {
                return -1;
            }
        } else {
            goto verify_final;
        }
    }
#elif defined(__linux__) && defined(SYS_renameat2)
    if (!force_fallback) {
        if (syscall(SYS_renameat2,
                    temp_dir_fd,
                    temp_name,
                    final_dir_fd,
                    final_name,
                    RENAME_NOREPLACE)
            != 0) {
            if (!cs_rename_noreplace_should_fallback(errno)) {
                return -1;
            }
        } else {
            goto verify_final;
        }
    }
#else
    /* No native atomic no-replace primitive on this platform. */
    force_fallback = 1;
#endif

    /* Some target filesystems, including tg5050's fuseblk SD mount, do not
     * support hard links. Reserve the destination name with O_EXCL and then
     * rename over the empty placeholder as the cross-filesystem fallback. */
    {
        int placeholder_fd = openat(final_dir_fd, final_name, O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW, 0600);

        if (placeholder_fd < 0) {
            return -1;
        }
        if (close(placeholder_fd) != 0) {
            int saved_errno = errno;

            (void) unlinkat(final_dir_fd, final_name, 0);
            errno = saved_errno;
            return -1;
        }
    }
    if (renameat(temp_dir_fd, temp_name, final_dir_fd, final_name) != 0) {
        int saved_errno = errno;

        (void) unlinkat(final_dir_fd, final_name, 0);
        errno = saved_errno;
        return -1;
    }

verify_final:
    if (fstatat(final_dir_fd, final_name, &final_st, AT_SYMLINK_NOFOLLOW) != 0
        || !S_ISREG(final_st.st_mode)) {
        (void) unlinkat(final_dir_fd, final_name, 0);
        errno = EIO;
        return -1;
    }
    if (final_st.st_dev != temp_st->st_dev || final_st.st_ino != temp_st->st_ino) {
        (void) unlinkat(final_dir_fd, final_name, 0);
        errno = EIO;
        return -1;
    }

    return 0;
}

int cs_upload_plan_make(const cs_paths *paths,
                        const char *final_root,
                        const char *final_guard_root,
                        const char *relative_dir,
                        const char *filename,
                        unsigned int path_flags,
                        cs_upload_plan *plan) {
    char resolved_dir[CS_PATH_MAX];
    struct stat st;
    cs_upload_plan temp = {0};
    const char *final_dir = relative_dir ? relative_dir : "";

    if (!paths || !final_root || !final_guard_root || !filename || !plan) {
        return -1;
    }
    if (!cs_upload_component_is_safe(filename)) {
        return -1;
    }
    if (cs_validate_relative_path_with_flags(final_dir, path_flags | CS_PATH_FLAG_ALLOW_EMPTY) != 0) {
        return -1;
    }
    if (lstat(paths->temp_upload_root, &st) == 0 && S_ISLNK(st.st_mode)) {
        return -1;
    }
    if (lstat(final_root, &st) == 0 && S_ISLNK(st.st_mode)) {
        return -1;
    }
    if (cs_resolve_path_under_root_with_flags(final_root,
                                              final_dir,
                                              path_flags | CS_PATH_FLAG_ALLOW_EMPTY,
                                              resolved_dir,
                                              sizeof(resolved_dir))
        != 0) {
        return -1;
    }
    if (cs_upload_prepare_directory_within_root(final_guard_root, resolved_dir) != 0) {
        return -1;
    }

    if (cs_upload_write_path(temp.final_path,
                             sizeof(temp.final_path),
                             "%s/%s",
                             resolved_dir,
                             filename)
        != 0) {
        return -1;
    }

    if (cs_upload_write_path(temp.temp_path,
                             sizeof(temp.temp_path),
                             "%s/.incoming-%ld-%lu-%s",
                             paths->temp_upload_root,
                             (long) getpid(),
                             cs_upload_nonce(),
                             filename)
        != 0) {
        return -1;
    }
    if (cs_upload_write_path(temp.temp_root, sizeof(temp.temp_root), "%s", paths->temp_upload_root)
        != 0) {
        return -1;
    }
    if (cs_upload_write_path(temp.final_root, sizeof(temp.final_root), "%s", final_root) != 0) {
        return -1;
    }
    if (cs_upload_write_path(temp.temp_guard_root,
                             sizeof(temp.temp_guard_root),
                             "%s",
                             paths->shared_state_root)
        != 0) {
        return -1;
    }
    if (cs_upload_write_path(temp.final_guard_root,
                             sizeof(temp.final_guard_root),
                             "%s",
                             final_guard_root)
        != 0) {
        return -1;
    }

    *plan = temp;
    return 0;
}

int cs_upload_prepare_temp_root(const cs_paths *paths) {
    if (!paths) {
        return -1;
    }

    return cs_upload_prepare_directory_within_root(paths->sdcard_root, paths->temp_upload_root);
}

int cs_upload_promote(const cs_upload_plan *plan) {
    char temp_name[CS_PATH_MAX];
    char final_name[CS_PATH_MAX];
    char final_parent[CS_PATH_MAX];
    int temp_dir_fd = -1;
    int final_dir_fd = -1;
    int temp_fd = -1;
    struct stat temp_st;
    int rc = -1;

    if (!plan || plan->temp_path[0] == '\0' || plan->final_path[0] == '\0') {
        return -1;
    }
    if (!cs_upload_is_declared_under_root(plan->temp_root, plan->temp_guard_root)
        || !cs_upload_is_declared_under_root(plan->temp_path, plan->temp_root)
        || !cs_upload_is_declared_under_root(plan->final_root, plan->final_guard_root)
        || !cs_upload_is_declared_under_root(plan->final_path, plan->final_root)) {
        return -1;
    }
    if (cs_upload_extract_name(plan->temp_path, temp_name, sizeof(temp_name)) != 0
        || cs_upload_extract_parent(plan->final_path, final_parent, sizeof(final_parent)) != 0
        || cs_upload_extract_name(plan->final_path, final_name, sizeof(final_name)) != 0) {
        return -1;
    }

    temp_dir_fd = cs_upload_open_directory_under_root(plan->temp_guard_root, plan->temp_root);
    if (temp_dir_fd < 0) {
        goto cleanup;
    }
    temp_fd = openat(temp_dir_fd, temp_name, O_RDONLY | O_NOFOLLOW);
    if (temp_fd < 0) {
        goto cleanup;
    }
    if (fstat(temp_fd, &temp_st) != 0 || !S_ISREG(temp_st.st_mode)) {
        goto cleanup;
    }

    final_dir_fd = cs_upload_open_directory_under_root(plan->final_guard_root, final_parent);
    if (final_dir_fd < 0) {
        goto cleanup;
    }

    if (cs_upload_atomic_promote_no_replace(temp_dir_fd, temp_name, final_dir_fd, final_name, &temp_st)
        != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (temp_fd >= 0) {
        close(temp_fd);
    }
    if (temp_dir_fd >= 0) {
        close(temp_dir_fd);
    }
    if (final_dir_fd >= 0) {
        close(final_dir_fd);
    }

    return rc;
}

int cs_upload_promote_replace(const cs_upload_plan *plan) {
    char temp_name[CS_PATH_MAX];
    char final_name[CS_PATH_MAX];
    char final_parent[CS_PATH_MAX];
    int temp_dir_fd = -1;
    int final_dir_fd = -1;
    int temp_fd = -1;
    struct stat temp_st;
    struct stat final_st;
    int rc = -1;

    if (!plan || plan->temp_path[0] == '\0' || plan->final_path[0] == '\0') {
        return -1;
    }
    if (!cs_upload_is_declared_under_root(plan->temp_root, plan->temp_guard_root)
        || !cs_upload_is_declared_under_root(plan->temp_path, plan->temp_root)
        || !cs_upload_is_declared_under_root(plan->final_root, plan->final_guard_root)
        || !cs_upload_is_declared_under_root(plan->final_path, plan->final_root)) {
        return -1;
    }
    if (cs_upload_extract_name(plan->temp_path, temp_name, sizeof(temp_name)) != 0
        || cs_upload_extract_parent(plan->final_path, final_parent, sizeof(final_parent)) != 0
        || cs_upload_extract_name(plan->final_path, final_name, sizeof(final_name)) != 0) {
        return -1;
    }

    temp_dir_fd = cs_upload_open_directory_under_root(plan->temp_guard_root, plan->temp_root);
    if (temp_dir_fd < 0) {
        goto cleanup;
    }
    temp_fd = openat(temp_dir_fd, temp_name, O_RDONLY | O_NOFOLLOW);
    if (temp_fd < 0) {
        goto cleanup;
    }
    if (fstat(temp_fd, &temp_st) != 0 || !S_ISREG(temp_st.st_mode)) {
        goto cleanup;
    }

    final_dir_fd = cs_upload_open_directory_under_root(plan->final_guard_root, final_parent);
    if (final_dir_fd < 0) {
        goto cleanup;
    }
    if (fstatat(final_dir_fd, final_name, &final_st, AT_SYMLINK_NOFOLLOW) == 0 && !S_ISREG(final_st.st_mode)) {
        errno = EINVAL;
        goto cleanup;
    }
    if (renameat(temp_dir_fd, temp_name, final_dir_fd, final_name) != 0) {
        goto cleanup;
    }
    if (fstatat(final_dir_fd, final_name, &final_st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(final_st.st_mode)) {
        errno = EIO;
        goto cleanup;
    }
    if (final_st.st_dev != temp_st.st_dev || final_st.st_ino != temp_st.st_ino) {
        errno = EIO;
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (temp_fd >= 0) {
        close(temp_fd);
    }
    if (temp_dir_fd >= 0) {
        close(temp_dir_fd);
    }
    if (final_dir_fd >= 0) {
        close(final_dir_fd);
    }

    return rc;
}
