#include "cs_build_info.h"

#if defined(PLATFORM_NEXTUI)
#include "apostrophe.h"
#endif

const char *cs_build_info_platform_name(void) {
#if defined(PLATFORM_TG5040)
    return "tg5040";
#elif defined(PLATFORM_TG5050)
    return "tg5050";
#elif defined(PLATFORM_MY355)
    return "my355";
#elif defined(PLATFORM_NEXTUI)
    return ap_get_platform_name();
#elif defined(PLATFORM_MAC)
    return "mac";
#else
#error "unsupported platform: define PLATFORM_NEXTUI or a supported legacy/desktop platform"
#endif
}
