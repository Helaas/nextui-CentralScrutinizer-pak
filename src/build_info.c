#include "cs_build_info.h"

const char *cs_build_info_platform_name(void) {
#if defined(PLATFORM_TG5040)
    return "tg5040";
#elif defined(PLATFORM_TG5050)
    return "tg5050";
#elif defined(PLATFORM_MY355)
    return "my355";
#elif defined(PLATFORM_MAC)
    return "mac";
#else
#error "unsupported platform: define one of PLATFORM_TG5040, PLATFORM_TG5050, PLATFORM_MY355, or PLATFORM_MAC"
#endif
}
