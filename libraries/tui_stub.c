/*
 * Fallback used when ncurses is not available at build time.  The rest of the
 * program keeps working; only the interactive browser is disabled.
 */

#include "tui.h"

#include <stdlib.h>
#include <string.h>

bool tui_available(void) {
    return false;
}

int tui_run(const char* db_path, const char* initial_query, char** selected,
            char** error) {
    (void)db_path;
    (void)initial_query;
    if (selected != NULL) {
        *selected = NULL;
    }
    if (error != NULL) {
        *error = strdup("this build has no TUI support (install ncurses and "
                        "rebuild)");
    }
    return -1;
}
