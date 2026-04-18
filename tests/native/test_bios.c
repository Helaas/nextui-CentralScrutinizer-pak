#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "cs_bios.h"
#include "cs_paths.h"
#include "cs_platforms.h"

int main(void) {
    cs_paths paths = {0};
    cs_bios_requirement requirements[CS_BIOS_MAX_REQUIREMENTS];
    cs_bios_summary summary = {0};
    size_t count = 0;
    const cs_platform_info *a5200;
    const cs_platform_info *a7800;
    const cs_platform_info *coleco;
    const cs_platform_info *ps;
    const cs_platform_info *gba;
    const cs_platform_info *fds;

    setenv("SDCARD_PATH", "fixtures/mock_sdcard", 1);
    unsetenv("CS_WEB_ROOT");
    assert(cs_paths_init(&paths) == 0);

    a5200 = cs_platform_find("A5200");
    a7800 = cs_platform_find("A7800");
    coleco = cs_platform_find("COLECO");
    ps = cs_platform_find("PS");
    gba = cs_platform_find("GBA");
    fds = cs_platform_find("FDS");
    assert(a5200 != NULL);
    assert(a7800 != NULL);
    assert(coleco != NULL);
    assert(ps != NULL);
    assert(gba != NULL);
    assert(fds != NULL);

    assert(cs_bios_collect_requirements(&paths,
                                        ps,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == 0);
    assert(count == 1);
    assert(summary.required_count == 1);
    assert(summary.present_count == 1);
    assert(summary.satisfied == 1);
    assert(strcmp(requirements[0].label, "System BIOS") == 0);
    assert(strcmp(requirements[0].file_name, "scph1001.bin") == 0);
    assert(strcmp(requirements[0].path, "PS/scph1001.bin") == 0);
    assert(strcmp(requirements[0].status, "present") == 0);
    assert(requirements[0].required == 1);

    assert(cs_bios_collect_requirements(&paths,
                                        gba,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == 0);
    assert(count == 1);
    assert(summary.required_count == 1);
    assert(summary.present_count == 1);
    assert(summary.satisfied == 1);
    assert(strcmp(requirements[0].file_name, "gba_bios.bin") == 0);
    assert(strcmp(requirements[0].status, "present") == 0);

    assert(cs_bios_collect_requirements(&paths,
                                        fds,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == 0);
    assert(count == 1);
    assert(summary.required_count == 1);
    assert(summary.present_count == 0);
    assert(summary.satisfied == 0);
    assert(strcmp(requirements[0].file_name, "disksys.rom") == 0);
    assert(strcmp(requirements[0].status, "missing") == 0);

    assert(cs_bios_collect_requirements(&paths,
                                        a5200,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == 0);
    assert(count == 1);
    assert(summary.required_count == 1);
    assert(summary.present_count == 0);
    assert(summary.satisfied == 0);
    assert(strcmp(requirements[0].file_name, "5200.rom") == 0);
    assert(strcmp(requirements[0].path, "A5200/5200.rom") == 0);

    assert(cs_bios_collect_requirements(&paths,
                                        a7800,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == 0);
    assert(count == 1);
    assert(summary.required_count == 1);
    assert(summary.present_count == 0);
    assert(summary.satisfied == 0);
    assert(strcmp(requirements[0].file_name, "7800 BIOS (U).rom") == 0);
    assert(strcmp(requirements[0].path, "A7800/7800 BIOS (U).rom") == 0);

    assert(cs_bios_collect_requirements(&paths,
                                        coleco,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == 0);
    assert(count == 1);
    assert(summary.required_count == 1);
    assert(summary.present_count == 0);
    assert(summary.satisfied == 0);
    assert(strcmp(requirements[0].file_name, "colecovision.rom") == 0);
    assert(strcmp(requirements[0].path, "COLECO/colecovision.rom") == 0);

    assert(cs_bios_collect_requirements(NULL,
                                        ps,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == -1);
    assert(cs_bios_collect_requirements(&paths,
                                        NULL,
                                        requirements,
                                        CS_BIOS_MAX_REQUIREMENTS,
                                        &count,
                                        &summary)
           == -1);

    return 0;
}
