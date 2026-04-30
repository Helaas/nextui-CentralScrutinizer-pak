#include "cs_file_ops.h"
#include "cs_rename_fallback_internal.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if defined(__APPLE__)
extern int renameatx_np(int fromfd, const char *from, int tofd, const char *to, unsigned int flags);
#endif

#if defined(__linux__) && !defined(RENAME_NOREPLACE)
#define RENAME_NOREPLACE (1U)
#endif

static atomic_ulong g_write_nonce = ATOMIC_VAR_INIT(0);

static int cs_stats_refer_to_same_entry(const struct stat *left, const struct stat *right) {
    if (!left || !right) {
        return 0;
    }

    return left->st_dev == right->st_dev && left->st_ino == right->st_ino;
}

static int cs_directory_fds_refer_to_same_entry(int left_fd, int right_fd) {
    struct stat left_st;
    struct stat right_st;

    if (left_fd < 0 || right_fd < 0) {
        return 0;
    }
    if (fstat(left_fd, &left_st) != 0 || fstat(right_fd, &right_st) != 0) {
        return 0;
    }
    if (!S_ISDIR(left_st.st_mode) || !S_ISDIR(right_st.st_mode)) {
        return 0;
    }

    return cs_stats_refer_to_same_entry(&left_st, &right_st);
}

static int cs_verify_renamed_entry(int dir_fd, const char *name, const struct stat *source_st) {
    struct stat final_st;

    if (dir_fd < 0 || !name || !source_st) {
        errno = EINVAL;
        return -1;
    }
    if (fstatat(dir_fd, name, &final_st, AT_SYMLINK_NOFOLLOW) != 0) {
        errno = EIO;
        return -1;
    }
    if ((S_ISREG(source_st->st_mode) && !S_ISREG(final_st.st_mode))
        || (S_ISDIR(source_st->st_mode) && !S_ISDIR(final_st.st_mode))) {
        errno = EIO;
        return -1;
    }
    if (!cs_stats_refer_to_same_entry(&final_st, source_st)) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int cs_component_is_special(const char *start, size_t length) {
    if (!start) {
        return 1;
    }
    if (length == 1 && start[0] == '.') {
        return 1;
    }
    if (length == 2 && start[0] == '.' && start[1] == '.') {
        return 1;
    }

    return 0;
}

static int cs_component_is_allowed_hidden(const char *start, size_t length, unsigned int flags) {
    if ((flags & CS_PATH_FLAG_ALLOW_HIDDEN) != 0) {
        return 1;
    }

    return length == 6 && strncmp(start, ".media", length) == 0;
}

static int cs_component_has_reserved_windows_name(const char *start, size_t length) {
    char stem[16];
    size_t stem_len = 0;
    size_t i;

    if (!start || length == 0) {
        return 1;
    }

    while (stem_len < length && stem_len + 1 < sizeof(stem) && start[stem_len] != '.') {
        char ch = start[stem_len];

        stem[stem_len] = (char) ((ch >= 'a' && ch <= 'z') ? (ch - ('a' - 'A')) : ch);
        stem_len += 1;
    }
    stem[stem_len] = '\0';

    if (strcmp(stem, "CON") == 0 || strcmp(stem, "PRN") == 0 || strcmp(stem, "AUX") == 0
        || strcmp(stem, "NUL") == 0) {
        return 1;
    }
    if (stem_len == 4 && strncmp(stem, "COM", 3) == 0 && stem[3] >= '1' && stem[3] <= '9') {
        return 1;
    }
    if (stem_len == 4 && strncmp(stem, "LPT", 3) == 0 && stem[3] >= '1' && stem[3] <= '9') {
        return 1;
    }

    for (i = 0; i < length; ++i) {
        if (start[i] == ':' || start[i] == '*' || start[i] == '?' || start[i] == '"'
            || start[i] == '<' || start[i] == '>' || start[i] == '|') {
            return 1;
        }
    }

    return 0;
}

static int cs_write_path(char *dst, size_t size, const char *fmt, ...) {
    va_list args;
    int written;

    if (!dst || size == 0 || !fmt) {
        return -1;
    }

    va_start(args, fmt);
    written = vsnprintf(dst, size, fmt, args);
    va_end(args);

    return (written < 0 || (size_t) written >= size) ? -1 : 0;
}

static int cs_extract_parent(const char *path, char *parent, size_t parent_size) {
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

static int cs_extract_name(const char *path, char *name, size_t name_size) {
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

    return cs_write_path(name, name_size, "%s", leaf);
}

static int cs_is_declared_under_root(const char *path, const char *root) {
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

static int cs_write_relative_path(const char *root,
                                  const char *path,
                                  char *relative,
                                  size_t relative_size) {
    const char *suffix;

    if (!root || !path || !relative || relative_size == 0) {
        return -1;
    }
    if (!cs_is_declared_under_root(path, root)) {
        return -1;
    }

    suffix = path + strlen(root);
    if (*suffix == '/') {
        suffix++;
    }

    return cs_write_path(relative, relative_size, "%s", suffix);
}

static int cs_open_root_directory(const char *path) {
    if (!path) {
        return -1;
    }

    return open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
}

static int cs_open_directory_under_root(const char *root, const char *path) {
    char relative[1024];
    int current_fd = -1;

    if (cs_write_relative_path(root, path, relative, sizeof(relative)) != 0) {
        return -1;
    }

    current_fd = cs_open_root_directory(root);
    if (current_fd < 0) {
        return -1;
    }

    if (relative[0] != '\0') {
        const char *cursor = relative;

        while (*cursor != '\0') {
            const char *slash = strchr(cursor, '/');
            size_t length = slash ? (size_t) (slash - cursor) : strlen(cursor);
            char component[1024];
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

static int cs_case_only_rename_needs_fallback(int from_dir_fd,
                                              const char *from_name,
                                              int to_dir_fd,
                                              const char *to_name,
                                              const struct stat *source_st) {
    struct stat target_st;

    if (!from_name || !to_name || !source_st) {
        return 0;
    }
    if (strcmp(from_name, to_name) == 0) {
        return 0;
    }
    if (!cs_directory_fds_refer_to_same_entry(from_dir_fd, to_dir_fd)) {
        return 0;
    }
    if (fstatat(to_dir_fd, to_name, &target_st, AT_SYMLINK_NOFOLLOW) == 0) {
        return cs_stats_refer_to_same_entry(&target_st, source_st);
    }
    if (errno == ENOENT && cs_rename_case_only_force_fallback()) {
        return 1;
    }

    return 0;
}

static int cs_atomic_case_only_rename(int dir_fd,
                                      const char *from_name,
                                      const char *to_name,
                                      const struct stat *source_st) {
    unsigned long nonce;
    int attempt;

    if (dir_fd < 0 || !from_name || !to_name || !source_st) {
        errno = EINVAL;
        return -1;
    }

    nonce = atomic_fetch_add_explicit(&g_write_nonce, 1, memory_order_relaxed);
    for (attempt = 0; attempt < 32; ++attempt) {
        char temp_name[1024];

        if (cs_write_path(temp_name,
                          sizeof(temp_name),
                          ".csrename.%s.%lu.%d",
                          to_name,
                          nonce,
                          attempt)
            != 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (strcmp(temp_name, from_name) == 0 || strcmp(temp_name, to_name) == 0) {
            continue;
        }
        if (renameat(dir_fd, from_name, dir_fd, temp_name) != 0) {
            if (errno == EEXIST || errno == ENOTEMPTY) {
                continue;
            }
            return -1;
        }
        if (renameat(dir_fd, temp_name, dir_fd, to_name) != 0) {
            int saved_errno = errno;

            (void) renameat(dir_fd, temp_name, dir_fd, from_name);
            errno = saved_errno;
            return -1;
        }

        return cs_verify_renamed_entry(dir_fd, to_name, source_st);
    }

    errno = EEXIST;
    return -1;
}

static int cs_atomic_rename_no_replace(int from_dir_fd,
                                       const char *from_name,
                                       int to_dir_fd,
                                       const char *to_name,
                                       const struct stat *source_st) {
    int force_fallback;

    if (!from_name || !to_name || !source_st) {
        return -1;
    }

    force_fallback = cs_rename_noreplace_force_fallback();

#if defined(__APPLE__) && defined(RENAME_EXCL)
    if (!force_fallback) {
        if (renameatx_np(from_dir_fd, from_name, to_dir_fd, to_name, RENAME_EXCL) != 0) {
            if (!cs_rename_noreplace_should_fallback(errno)) {
                return -1;
            }
        } else {
            goto verify_final;
        }
    }
#elif defined(__linux__) && defined(SYS_renameat2)
    if (!force_fallback) {
        if (syscall(SYS_renameat2, from_dir_fd, from_name, to_dir_fd, to_name, RENAME_NOREPLACE) != 0) {
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

    if (S_ISREG(source_st->st_mode)) {
        int placeholder_fd = -1;

        /* Some target filesystems, including tg5050's fuseblk SD mount, do
         * not support hard links. Reserve the destination name with O_EXCL
         * and then rename over the empty placeholder as the fallback. */
        placeholder_fd = openat(to_dir_fd, to_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (placeholder_fd < 0) {
            return -1;
        }
        if (close(placeholder_fd) != 0) {
            int saved_errno = errno;

            (void) unlinkat(to_dir_fd, to_name, 0);
            errno = saved_errno;
            return -1;
        }
        if (renameat(from_dir_fd, from_name, to_dir_fd, to_name) != 0) {
            int saved_errno = errno;

            (void) unlinkat(to_dir_fd, to_name, 0);
            errno = saved_errno;
            return -1;
        }
    } else if (S_ISDIR(source_st->st_mode)) {
        /* Directories cannot be hardlinked, so fall back to mkdirat
         * placeholder + renameat. Residual race: a concurrent writer could
         * replace the empty placeholder — but renameat over a populated
         * directory fails with ENOTEMPTY, so a non-empty swap cannot be
         * silently clobbered. An empty swap is indistinguishable from our
         * own placeholder; the post-rename dev/ino check still detects a
         * source-side swap. */
        if (mkdirat(to_dir_fd, to_name, 0700) != 0) {
            return -1;
        }
        if (renameat(from_dir_fd, from_name, to_dir_fd, to_name) != 0) {
            int saved_errno = errno;

            (void) unlinkat(to_dir_fd, to_name, AT_REMOVEDIR);
            errno = saved_errno;
            return -1;
        }
    } else {
        errno = EINVAL;
        return -1;
    }

verify_final:
    if (cs_verify_renamed_entry(to_dir_fd, to_name, source_st) != 0) {
        if (S_ISDIR(source_st->st_mode)) {
            (void) unlinkat(to_dir_fd, to_name, AT_REMOVEDIR);
        } else {
            (void) unlinkat(to_dir_fd, to_name, 0);
        }
        return -1;
    }

    return 0;
}

static int cs_atomic_rename_replace(int dir_fd,
                                    const char *from_name,
                                    const char *to_name,
                                    const struct stat *source_st) {
    struct stat final_st;

    if (!from_name || !to_name || !source_st) {
        return -1;
    }
    if (renameat(dir_fd, from_name, dir_fd, to_name) != 0) {
        return -1;
    }
    if (fstatat(dir_fd, to_name, &final_st, AT_SYMLINK_NOFOLLOW) != 0 || !S_ISREG(final_st.st_mode)) {
        errno = EIO;
        return -1;
    }
    if (final_st.st_dev != source_st->st_dev || final_st.st_ino != source_st->st_ino) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int cs_open_temporary_write_file(int dir_fd,
                                        const char *target_name,
                                        mode_t mode,
                                        char *temp_name,
                                        size_t temp_name_size) {
    unsigned long nonce;
    int attempt;

    if (dir_fd < 0 || !target_name || !temp_name || temp_name_size == 0) {
        errno = EINVAL;
        return -1;
    }

    nonce = atomic_fetch_add_explicit(&g_write_nonce, 1, memory_order_relaxed);
    for (attempt = 0; attempt < 32; ++attempt) {
        if (cs_write_path(temp_name,
                          temp_name_size,
                          ".cstmp.%s.%lu.%d",
                          target_name,
                          nonce,
                          attempt)
            != 0) {
            errno = ENAMETOOLONG;
            return -1;
        }

        {
            int temp_fd = openat(dir_fd, temp_name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, mode);

            if (temp_fd >= 0) {
                return temp_fd;
            }
            if (errno != EEXIST) {
                return -1;
            }
        }
    }

    errno = EEXIST;
    return -1;
}

int cs_validate_path_component_with_flags(const char *component, unsigned int flags) {
    size_t length;
    size_t i;

    if (!component) {
        return -1;
    }

    length = strlen(component);
    if (length == 0 || cs_component_is_special(component, length)) {
        return -1;
    }
    if (component[0] == '.' && !cs_component_is_allowed_hidden(component, length, flags)) {
        return -1;
    }
    if (component[length - 1] == ' ' || component[length - 1] == '.') {
        return -1;
    }
    if (cs_component_has_reserved_windows_name(component, length)) {
        return -1;
    }

    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char) component[i];

        if (ch < 0x20 || ch == '/' || ch == '\\') {
            return -1;
        }
    }

    return 0;
}

int cs_validate_relative_path_with_flags(const char *relative_path, unsigned int flags) {
    const char *component_start;
    size_t i;

    if (!relative_path) {
        return -1;
    }
    if (relative_path[0] == '\0') {
        return (flags & CS_PATH_FLAG_ALLOW_EMPTY) != 0 ? 0 : -1;
    }
    if (relative_path[0] == '/') {
        return -1;
    }

    component_start = relative_path;
    for (i = 0;; ++i) {
        if (relative_path[i] == '/' || relative_path[i] == '\0') {
            size_t component_length = (size_t) (&relative_path[i] - component_start);
            char component[1024];

            if (component_length == 0 || component_length >= sizeof(component)) {
                return -1;
            }
            memcpy(component, component_start, component_length);
            component[component_length] = '\0';
            if (cs_validate_path_component_with_flags(component, flags) != 0) {
                return -1;
            }
            if (relative_path[i] == '\0') {
                break;
            }
            component_start = &relative_path[i + 1];
        } else if ((unsigned char) relative_path[i] < 0x20 || relative_path[i] == '\\') {
            return -1;
        }
    }

    return 0;
}

int cs_validate_relative_path(const char *relative_path) {
    return cs_validate_relative_path_with_flags(relative_path, 0);
}

int cs_resolve_path_under_root_with_flags(const char *root,
                                          const char *relative_path,
                                          unsigned int flags,
                                          char *resolved_path,
                                          size_t resolved_path_size) {
    int written;

    if (!root || !relative_path || !resolved_path || resolved_path_size == 0) {
        return -1;
    }
    if (cs_validate_relative_path_with_flags(relative_path, flags) != 0) {
        return -1;
    }
    if (relative_path[0] == '\0') {
        written = snprintf(resolved_path, resolved_path_size, "%s", root);
        return (written < 0 || (size_t) written >= resolved_path_size) ? -1 : 0;
    }

    written = snprintf(resolved_path, resolved_path_size, "%s/%s", root, relative_path);
    return (written < 0 || (size_t) written >= resolved_path_size) ? -1 : 0;
}

int cs_resolve_path_under_root(const char *root,
                               const char *relative_path,
                               char *resolved_path,
                               size_t resolved_path_size) {
    return cs_resolve_path_under_root_with_flags(root, relative_path, 0, resolved_path, resolved_path_size);
}

int cs_safe_rename_under_root_with_flags(const char *root,
                                         const char *from_relative_path,
                                         const char *to_relative_path,
                                         unsigned int flags) {
    char from_path[1024];
    char to_path[1024];
    char from_parent[1024];
    char to_parent[1024];
    char from_name[1024];
    char to_name[1024];
    int from_dir_fd = -1;
    int to_dir_fd = -1;
    int source_fd = -1;
    struct stat source_st;
    int rc = -1;

    if (cs_resolve_path_under_root_with_flags(root, from_relative_path, flags, from_path, sizeof(from_path)) != 0
        || cs_resolve_path_under_root_with_flags(root, to_relative_path, flags, to_path, sizeof(to_path)) != 0) {
        return -1;
    }
    if (cs_extract_parent(from_path, from_parent, sizeof(from_parent)) != 0
        || cs_extract_parent(to_path, to_parent, sizeof(to_parent)) != 0
        || cs_extract_name(from_path, from_name, sizeof(from_name)) != 0
        || cs_extract_name(to_path, to_name, sizeof(to_name)) != 0) {
        return -1;
    }

    from_dir_fd = cs_open_directory_under_root(root, from_parent);
    if (from_dir_fd < 0) {
        goto cleanup;
    }
    source_fd = openat(from_dir_fd, from_name, O_RDONLY | O_NOFOLLOW);
    if (source_fd < 0) {
        goto cleanup;
    }
    if (fstat(source_fd, &source_st) != 0 || (!S_ISREG(source_st.st_mode) && !S_ISDIR(source_st.st_mode))) {
        goto cleanup;
    }

    to_dir_fd = cs_open_directory_under_root(root, to_parent);
    if (to_dir_fd < 0) {
        goto cleanup;
    }
    if (cs_case_only_rename_needs_fallback(from_dir_fd, from_name, to_dir_fd, to_name, &source_st)) {
        if (cs_atomic_case_only_rename(from_dir_fd, from_name, to_name, &source_st) != 0) {
            goto cleanup;
        }
    } else if (cs_atomic_rename_no_replace(from_dir_fd, from_name, to_dir_fd, to_name, &source_st) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (source_fd >= 0) {
        close(source_fd);
    }
    if (from_dir_fd >= 0) {
        close(from_dir_fd);
    }
    if (to_dir_fd >= 0) {
        close(to_dir_fd);
    }

    return rc;
}

int cs_safe_rename_under_root(const char *root, const char *from_relative_path, const char *to_relative_path) {
    return cs_safe_rename_under_root_with_flags(root, from_relative_path, to_relative_path, 0);
}

static int cs_delete_entry_at(int parent_fd, const char *name) {
    struct stat st;

    if (!name) {
        errno = EINVAL;
        return -1;
    }
    if (fstatat(parent_fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
        return -1;
    }
    if (S_ISDIR(st.st_mode)) {
        DIR *dir = NULL;
        int dir_fd = -1;
        int saved_errno = 0;
        struct dirent *entry;

        dir_fd = openat(parent_fd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (dir_fd < 0) {
            return -1;
        }
        dir = fdopendir(dir_fd);
        if (!dir) {
            close(dir_fd);
            return -1;
        }

        errno = 0;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            if (cs_delete_entry_at(dirfd(dir), entry->d_name) != 0) {
                saved_errno = errno;
                (void) closedir(dir);
                errno = saved_errno;
                return -1;
            }
            errno = 0;
        }
        if (errno != 0) {
            saved_errno = errno;
            (void) closedir(dir);
            errno = saved_errno;
            return -1;
        }
        if (closedir(dir) != 0) {
            return -1;
        }
        return unlinkat(parent_fd, name, AT_REMOVEDIR);
    }
    if (!S_ISREG(st.st_mode) && !S_ISLNK(st.st_mode)) {
        errno = EPERM;
        return -1;
    }

    return unlinkat(parent_fd, name, 0);
}

int cs_safe_delete_under_root_with_flags(const char *root,
                                         const char *relative_path,
                                         unsigned int flags) {
    char absolute_path[1024];
    char parent[1024];
    char name[1024];
    int parent_fd = -1;
    struct stat st;
    int rc = -1;

    if (cs_resolve_path_under_root_with_flags(root, relative_path, flags, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }
    if (cs_extract_parent(absolute_path, parent, sizeof(parent)) != 0
        || cs_extract_name(absolute_path, name, sizeof(name)) != 0) {
        return -1;
    }

    parent_fd = cs_open_directory_under_root(root, parent);
    if (parent_fd < 0) {
        goto cleanup;
    }
    if (fstatat(parent_fd, name, &st, AT_SYMLINK_NOFOLLOW) != 0
        || (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))) {
        goto cleanup;
    }
    if (cs_delete_entry_at(parent_fd, name) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (parent_fd >= 0) {
        close(parent_fd);
    }

    return rc;
}

int cs_safe_delete_under_root(const char *root, const char *relative_path) {
    return cs_safe_delete_under_root_with_flags(root, relative_path, 0);
}

int cs_safe_create_directory_under_root_with_flags(const char *root,
                                                   const char *relative_path,
                                                   unsigned int flags) {
    char absolute_path[1024];
    char parent[1024];
    char name[1024];
    int parent_fd = -1;
    int rc = -1;

    if (cs_resolve_path_under_root_with_flags(root, relative_path, flags, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }
    if (cs_extract_parent(absolute_path, parent, sizeof(parent)) != 0
        || cs_extract_name(absolute_path, name, sizeof(name)) != 0) {
        return -1;
    }

    parent_fd = cs_open_directory_under_root(root, parent);
    if (parent_fd < 0) {
        goto cleanup;
    }
    if (mkdirat(parent_fd, name, 0775) != 0) {
        goto cleanup;
    }

    rc = 0;

cleanup:
    if (parent_fd >= 0) {
        close(parent_fd);
    }

    return rc;
}

int cs_safe_write_under_root_with_flags(const char *root,
                                        const char *relative_path,
                                        const void *data,
                                        size_t length,
                                        unsigned int flags) {
    char absolute_path[1024];
    char parent[1024];
    char name[1024];
    int parent_fd = -1;
    int target_fd = -1;
    int temp_fd = -1;
    int rc = -1;
    struct stat st;
    struct stat temp_st;
    char temp_name[1024] = {0};
    const unsigned char *cursor;
    size_t remaining;

    if ((!data && length > 0) || length > (1U << 20)) {
        return -1;
    }
    if (cs_resolve_path_under_root_with_flags(root, relative_path, flags, absolute_path, sizeof(absolute_path)) != 0) {
        return -1;
    }
    if (cs_extract_parent(absolute_path, parent, sizeof(parent)) != 0
        || cs_extract_name(absolute_path, name, sizeof(name)) != 0) {
        return -1;
    }

    parent_fd = cs_open_directory_under_root(root, parent);
    if (parent_fd < 0) {
        goto cleanup;
    }
    target_fd = openat(parent_fd, name, O_RDONLY | O_NOFOLLOW);
    if (target_fd < 0) {
        goto cleanup;
    }
    if (fstat(target_fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        goto cleanup;
    }
    close(target_fd);
    target_fd = -1;

    temp_fd = cs_open_temporary_write_file(parent_fd, name, st.st_mode & 0777, temp_name, sizeof(temp_name));
    if (temp_fd < 0) {
        goto cleanup;
    }
    if (fstat(temp_fd, &temp_st) != 0 || !S_ISREG(temp_st.st_mode)) {
        goto cleanup;
    }

    cursor = (const unsigned char *) data;
    remaining = length;
    while (remaining > 0) {
        ssize_t wrote = write(temp_fd, cursor, remaining);

        if (wrote < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto cleanup;
        }
        if (wrote == 0) {
            errno = EIO;
            goto cleanup;
        }
        cursor += (size_t) wrote;
        remaining -= (size_t) wrote;
    }

    if (fsync(temp_fd) != 0) {
        goto cleanup;
    }
    if (close(temp_fd) != 0) {
        temp_fd = -1;
        goto cleanup;
    }
    temp_fd = -1;

    if (cs_atomic_rename_replace(parent_fd, temp_name, name, &temp_st) != 0) {
        goto cleanup;
    }
    temp_name[0] = '\0';
    if (fsync(parent_fd) != 0) {
        goto cleanup;
    }
    rc = 0;

cleanup:
    if (temp_fd >= 0) {
        close(temp_fd);
    }
    if (target_fd >= 0) {
        close(target_fd);
    }
    if (parent_fd >= 0 && temp_name[0] != '\0') {
        (void) unlinkat(parent_fd, temp_name, 0);
    }
    if (parent_fd >= 0) {
        close(parent_fd);
    }

    return rc;
}

int cs_safe_rename(const char *from_path, const char *to_path) {
    if (!from_path || !to_path) {
        return -1;
    }
    if (access(to_path, F_OK) == 0) {
        return -1;
    }

    return rename(from_path, to_path);
}

int cs_safe_delete(const char *path) {
    if (!path) {
        return -1;
    }

    return unlink(path);
}
