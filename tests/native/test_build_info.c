#include <assert.h>
#include <string.h>

#include "cs_build_info.h"

int main(void) {
    assert(strcmp(cs_build_info_platform_name(), "mac") == 0);
    return 0;
}
