#ifndef CS_UPLOADS_H
#define CS_UPLOADS_H

#include "cs_file_ops.h"
#include "cs_paths.h"

typedef struct cs_upload_plan {
    char temp_path[CS_PATH_MAX];
    char final_path[CS_PATH_MAX];
    char temp_root[CS_PATH_MAX];
    char final_root[CS_PATH_MAX];
    char temp_guard_root[CS_PATH_MAX];
    char final_guard_root[CS_PATH_MAX];
} cs_upload_plan;

int cs_upload_plan_make(const cs_paths *paths,
                        const char *final_root,
                        const char *final_guard_root,
                        const char *relative_dir,
                        const char *filename,
                        unsigned int path_flags,
                        cs_upload_plan *plan);

int cs_upload_prepare_temp_root(const cs_paths *paths);
int cs_upload_prepare_final_directory(const char *final_root,
                                      const char *final_guard_root,
                                      const char *relative_dir,
                                      unsigned int path_flags);
int cs_upload_reserve_temp_path(const cs_paths *paths,
                                const char *filename,
                                char *buffer,
                                size_t buffer_len);
int cs_upload_promote(const cs_upload_plan *plan);
int cs_upload_promote_replace(const cs_upload_plan *plan);

#endif
