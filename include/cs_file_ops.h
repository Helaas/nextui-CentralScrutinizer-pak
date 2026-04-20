#ifndef CS_FILE_OPS_H
#define CS_FILE_OPS_H

#include <stddef.h>

#define CS_PATH_FLAG_ALLOW_HIDDEN (1U << 0)
#define CS_PATH_FLAG_ALLOW_EMPTY (1U << 1)

int cs_validate_relative_path(const char *relative_path);
int cs_validate_relative_path_with_flags(const char *relative_path, unsigned int flags);
int cs_validate_path_component_with_flags(const char *component, unsigned int flags);
int cs_resolve_path_under_root(const char *root,
                               const char *relative_path,
                               char *resolved_path,
                               size_t resolved_path_size);
int cs_resolve_path_under_root_with_flags(const char *root,
                                          const char *relative_path,
                                          unsigned int flags,
                                          char *resolved_path,
                                          size_t resolved_path_size);
int cs_safe_rename_under_root(const char *root, const char *from_relative_path, const char *to_relative_path);
int cs_safe_rename_under_root_with_flags(const char *root,
                                         const char *from_relative_path,
                                         const char *to_relative_path,
                                         unsigned int flags);
int cs_safe_delete_under_root(const char *root, const char *relative_path);
int cs_safe_delete_under_root_with_flags(const char *root,
                                         const char *relative_path,
                                         unsigned int flags);
int cs_safe_create_directory_under_root_with_flags(const char *root,
                                                   const char *relative_path,
                                                   unsigned int flags);
int cs_safe_write_under_root_with_flags(const char *root,
                                        const char *relative_path,
                                        const void *data,
                                        size_t length,
                                        unsigned int flags);
int cs_safe_rename(const char *from_path, const char *to_path);
int cs_safe_delete(const char *path);

#endif
