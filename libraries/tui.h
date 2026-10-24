#ifndef SAVE_CMD_TUI_H
#define SAVE_CMD_TUI_H

#include <stdbool.h>

/* True when this build includes the ncurses interface. */
bool tui_available(void);

/*
 * Run the interactive browser.
 *
 * `initial_query` may be NULL or empty.  When the user presses Enter the
 * selected command is returned through `selected` (caller frees it).  Quitting
 * normally returns 0 with *selected == NULL.  On failure -1 is returned and
 * `error` is set to an allocated message (caller frees it).
 */
int tui_run(const char* db_path, const char* initial_query, char** selected,
            char** error);

#endif /* SAVE_CMD_TUI_H */
