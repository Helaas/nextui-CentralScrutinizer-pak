#if defined(CS_ENABLE_APOSTROPHE_UI)
#define AP_IMPLEMENTATION
#include "apostrophe.h"
#define AP_WIDGETS_IMPLEMENTATION
#include "apostrophe_widgets.h"
#endif

#include "cs_app.h"

int main(int argc, char **argv) {
    return cs_app_run(argc, argv);
}
