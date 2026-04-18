#ifndef CS_UTIL_H
#define CS_UTIL_H

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static inline int cs_safe_vsnprintf(char *buffer, size_t buffer_len, const char *fmt, va_list args) {
    int written;

    if (!buffer || buffer_len == 0 || !fmt) {
        return -1;
    }

    written = vsnprintf(buffer, buffer_len, fmt, args);
    return written >= 0 && (size_t) written < buffer_len ? 0 : -1;
}

static inline int cs_safe_snprintf_impl(char *buffer, size_t buffer_len, const char *fmt, ...) {
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = cs_safe_vsnprintf(buffer, buffer_len, fmt, args);
    va_end(args);
    return rc;
}

#define CS_SAFE_SNPRINTF(buffer, buffer_len, ...) \
    cs_safe_snprintf_impl((buffer), (buffer_len), __VA_ARGS__)

#endif
