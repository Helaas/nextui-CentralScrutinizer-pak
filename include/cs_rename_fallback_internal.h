#ifndef CS_RENAME_FALLBACK_INTERNAL_H
#define CS_RENAME_FALLBACK_INTERNAL_H

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#if defined(CS_TESTING)
static inline int cs_rename_noreplace_force_fallback(void) {
    const char *value = getenv("CS_FORCE_RENAME_NOREPLACE_FALLBACK");

    return value != NULL && value[0] != '\0' && strcmp(value, "0") != 0;
}
#else
static inline int cs_rename_noreplace_force_fallback(void) {
    return 0;
}
#endif

/* Only RENAME_NOREPLACE/RENAME_EXCL is passed, so EINVAL from the native
 * call is treated as "flag unsupported" alongside ENOSYS/ENOTSUP/EOPNOTSUPP.
 * If more flags are added later, revisit this. */
static inline int cs_rename_noreplace_should_fallback(int error) {
    return error == ENOSYS || error == EINVAL || error == ENOTSUP || error == EOPNOTSUPP;
}

#endif
