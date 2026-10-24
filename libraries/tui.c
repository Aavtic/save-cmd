
#include "tui.h"
#include "filefuncs.h"

#include <curses.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>
#include <wctype.h>

/* ------------------------------------------------------------------------- */
/* Globals                                                                   */
/* ------------------------------------------------------------------------- */

enum {
    CP_ACCENT = 1,
    CP_ERROR,
    CP_OK,
};

static volatile sig_atomic_t g_interrupted = 0;
static bool g_curses_active = false;
static bool g_use_color = false;
static bool g_utf8 = true;

static void on_signal(int signo) {
    (void)signo;
    g_interrupted = 1;
}

static void tui_cleanup(void) {
    if (g_curses_active) {
        curs_set(1);
        endwin();
        g_curses_active = false;
    }
}

static int attr_accent(void) {
    return A_BOLD | (g_use_color ? COLOR_PAIR(CP_ACCENT) : 0);
}

static int attr_error(void) {
    return A_BOLD | (g_use_color ? COLOR_PAIR(CP_ERROR) : 0);
}

static int attr_ok(void) {
    return A_BOLD | (g_use_color ? COLOR_PAIR(CP_OK) : 0);
}

/* ------------------------------------------------------------------------- */
/* Editable string buffer                                                    */
/* ------------------------------------------------------------------------- */

typedef struct EditBuf {
    char* data;
    size_t len;
    size_t capacity;
    size_t cursor;
} EditBuf;

static bool eb_init(EditBuf* buf, const char* text) {
    memset(buf, 0, sizeof(*buf));
    size_t len = text != NULL ? strlen(text) : 0;
    size_t capacity = len + 32;
    if (capacity < 64) {
        capacity = 64;
    }
    buf->data = malloc(capacity);
    if (buf->data == NULL) {
        return false;
    }
    if (len > 0) {
        memcpy(buf->data, text, len);
    }
    buf->data[len] = '\0';
    buf->len = len;
    buf->capacity = capacity;
    buf->cursor = len;
    return true;
}

static void eb_free(EditBuf* buf) {
    free(buf->data);
    memset(buf, 0, sizeof(*buf));
}

static bool eb_reserve(EditBuf* buf, size_t needed) {
    if (needed + 1 <= buf->capacity) {
        return true;
    }
    size_t capacity = buf->capacity == 0 ? 64 : buf->capacity;
    while (capacity < needed + 1) {
        capacity *= 2;
    }
    char* data = realloc(buf->data, capacity);
    if (data == NULL) {
        return false;
    }
    buf->data = data;
    buf->capacity = capacity;
    return true;
}

static bool eb_insert_bytes(EditBuf* buf, const char* text, size_t len) {
    if (!eb_reserve(buf, buf->len + len)) {
        return false;
    }
    memmove(buf->data + buf->cursor + len, buf->data + buf->cursor,
            buf->len - buf->cursor + 1);
    memcpy(buf->data + buf->cursor, text, len);
    buf->len += len;
    buf->cursor += len;
    return true;
}

static bool eb_insert_wchar(EditBuf* buf, wchar_t wc) {
    char mb[MB_LEN_MAX * 2];
    mbstate_t state;
    memset(&state, 0, sizeof(state));
    size_t len = wcrtomb(mb, wc, &state);
    if (len == (size_t)-1) {
        return false;
    }
    return eb_insert_bytes(buf, mb, len);
}

static size_t utf8_prev(const char* text, size_t pos) {
    if (pos == 0) {
        return 0;
    }
    --pos;
    while (pos > 0 && ((unsigned char)text[pos] & 0xC0) == 0x80) {
        --pos;
    }
    return pos;
}

static size_t utf8_next(const char* text, size_t len, size_t pos) {
    if (pos >= len) {
        return len;
    }
    ++pos;
    while (pos < len && ((unsigned char)text[pos] & 0xC0) == 0x80) {
        ++pos;
    }
    return pos;
}

static void eb_backspace(EditBuf* buf) {
    if (buf->cursor == 0) {
        return;
    }
    size_t start = utf8_prev(buf->data, buf->cursor);
    size_t removed = buf->cursor - start;
    memmove(buf->data + start, buf->data + buf->cursor,
            buf->len - buf->cursor + 1);
    buf->len -= removed;
    buf->cursor = start;
}

static void eb_delete(EditBuf* buf) {
    if (buf->cursor >= buf->len) {
        return;
    }
    size_t end = utf8_next(buf->data, buf->len, buf->cursor);
    size_t removed = end - buf->cursor;
    memmove(buf->data + buf->cursor, buf->data + end,
            buf->len - end + 1);
    buf->len -= removed;
}

static void eb_clear(EditBuf* buf) {
    buf->len = 0;
    buf->cursor = 0;
    if (buf->data != NULL) {
        buf->data[0] = '\0';
    }
}

static void eb_home(EditBuf* buf) {
    buf->cursor = 0;
}

static void eb_end(EditBuf* buf) {
    buf->cursor = buf->len;
}

static void eb_left(EditBuf* buf) {
    buf->cursor = utf8_prev(buf->data, buf->cursor);
}

static void eb_right(EditBuf* buf) {
    buf->cursor = utf8_next(buf->data, buf->len, buf->cursor);
}

static void eb_delete_word(EditBuf* buf) {
    while (buf->cursor > 0 &&
           isspace((unsigned char)buf->data[buf->cursor - 1])) {
        eb_backspace(buf);
    }
    while (buf->cursor > 0 &&
           !isspace((unsigned char)buf->data[buf->cursor - 1])) {
        eb_backspace(buf);
    }
}

/* ------------------------------------------------------------------------- */
/* Drawing helpers                                                           */
/* ------------------------------------------------------------------------- */

static int codepoint_width(wchar_t wc) {
    if (wc < 32 || wc == 127) {
        return 1;
    }
    int width = wcwidth(wc);
    return width > 0 ? width : 0;
}

static size_t decode_next(const char* text, size_t len, size_t pos,
                          wchar_t* out) {
    mbstate_t state;
    memset(&state, 0, sizeof(state));
    size_t used = mbrtowc(out, text + pos, len - pos, &state);
    if (used == (size_t)-1 || used == (size_t)-2 || used == 0) {
        *out = L'?';
        return 1;
    }
    return used;
}

static int width_between(const char* text, size_t start, size_t end) {
    int width = 0;
    size_t pos = start;
    while (pos < end) {
        wchar_t wc = L'?';
        size_t used = decode_next(text, end, pos, &wc);
        if (pos + used > end) {
            used = end - pos;
        }
        width += codepoint_width(wc);
        pos += used;
    }
    return width;
}

static void draw_text_range(WINDOW* win, int row, int col, int width,
                            const char* text, size_t start, size_t end,
                            int attrs) {
    if (width <= 0 || text == NULL) {
        return;
    }
    if (end > strlen(text)) {
        end = strlen(text);
    }
    if (start > end) {
        start = end;
    }

    wattrset(win, attrs);
    wmove(win, row, col);

    int used = 0;
    bool truncated = false;
    size_t pos = start;
    while (pos < end) {
        wchar_t wc = L'?';
        size_t bytes = decode_next(text, end, pos, &wc);
        if (pos + bytes > end) {
            bytes = end - pos;
        }
        int cw = codepoint_width(wc);
        if (used + cw > width) {
            truncated = true;
            break;
        }
        if (wc == L'\n' || wc == L'\r' || wc == L'\t' || wc < 32 || wc == 127) {
            wc = L' ';
        }
        if (waddnwstr(win, &wc, 1) == ERR) {
            break;
        }
        used += cw;
        pos += bytes;
    }
    if (truncated && used < width) {
        wchar_t ellipsis = g_utf8 ? L'\u2026' : L'~';
        (void)waddnwstr(win, &ellipsis, 1);
    }
    wattrset(win, A_NORMAL);
}

static void draw_text(WINDOW* win, int row, int col, int width,
                      const char* text, int attrs) {
    draw_text_range(win, row, col, width, text, 0, strlen(text), attrs);
}

static void draw_separator(int row, int cols) {
    mvhline(row, 1, ACS_HLINE, cols - 2);
    mvaddch(row, 0, ACS_LTEE);
    mvaddch(row, cols - 1, ACS_RTEE);
}

/*
 * Draw an editable field.  The visible slice is scrolled horizontally so the
 * cursor always stays on screen.
 */
static void draw_input(WINDOW* win, int row, int col, int width, EditBuf* buf,
                       bool active) {
    wattrset(win, active ? A_UNDERLINE : A_NORMAL);
    mvwhline(win, row, col, ' ', width);
    wattrset(win, A_NORMAL);

    size_t start = 0;
    if (active) {
        while (start < buf->cursor &&
               width_between(buf->data, start, buf->cursor) >= width) {
            start = utf8_next(buf->data, buf->len, start);
        }
    }
    draw_text_range(win, row, col, width, buf->data, start, buf->len,
                    active ? A_BOLD : A_NORMAL);
    if (active) {
        int offset = width_between(buf->data, start, buf->cursor);
        wmove(win, row, col + offset);
    }
}

/* ------------------------------------------------------------------------- */
/* Application state                                                         */
/* ------------------------------------------------------------------------- */

typedef struct App {
    CommandDB db;
    char* db_path;
    size_t* visible;
    size_t visible_count;
    size_t selected;
    size_t top;
    EditBuf query;
    bool searching;
    char status[512];
    int status_attr;
} App;

static void set_status(App* app, int attrs, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(app->status, sizeof(app->status), fmt, ap);
    va_end(ap);
    app->status_attr = attrs;
}

static char* current_id(const App* app) {
    if (app->visible_count == 0 || app->selected >= app->visible_count) {
        return NULL;
    }
    size_t index = app->visible[app->selected];
    if (app->db.items[index].id == NULL) {
        return NULL;
    }
    return strdup(app->db.items[index].id);
}

static void refilter(App* app, const char* keep_id) {
    free(app->visible);
    app->visible = NULL;
    app->visible_count = 0;
    app->selected = 0;
    app->top = 0;

    if (app->db.count > 0) {
        app->visible = malloc(app->db.count * sizeof(*app->visible));
        if (app->visible == NULL) {
            return;
        }
    }
    for (size_t i = 0; i < app->db.count; ++i) {
        if (command_matches_query(&app->db.items[i], app->query.data)) {
            app->visible[app->visible_count++] = i;
        }
    }
    if (keep_id != NULL) {
        for (size_t i = 0; i < app->visible_count; ++i) {
            const char* id = app->db.items[app->visible[i]].id;
            if (id != NULL && strcmp(id, keep_id) == 0) {
                app->selected = i;
                break;
            }
        }
    }
    if (app->selected >= app->visible_count) {
        app->selected = app->visible_count > 0 ? app->visible_count - 1 : 0;
    }
}

static bool reload_db(App* app) {
    char* keep = current_id(app);
    CommandDB fresh;
    char* error = NULL;
    if (!db_load(&fresh, app->db_path, &error)) {
        set_status(app, attr_error(), "Cannot reload: %s", error);
        free(error);
        free(keep);
        return false;
    }
    db_free(&app->db);
    app->db = fresh;
    refilter(app, keep);
    free(keep);
    if (app->db.skipped > 0) {
        set_status(app, attr_error(), "Skipped %d invalid record(s) in %s",
                   app->db.skipped, app->db_path);
    }
    return true;
}

static void select_id(App* app, const char* id) {
    if (id == NULL) {
        return;
    }
    for (size_t i = 0; i < app->visible_count; ++i) {
        const char* item_id = app->db.items[app->visible[i]].id;
        if (item_id != NULL && strcmp(item_id, id) == 0) {
            app->selected = i;
            return;
        }
    }
}

static int list_height(void) {
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);
    (void)cols;
    int height = rows - 11;
    return height > 0 ? height : 1;
}

static void move_selection(App* app, int delta) {
    if (app->visible_count == 0) {
        return;
    }
    long next = (long)app->selected + delta;
    if (next < 0) {
        next = 0;
    }
    if (next >= (long)app->visible_count) {
        next = (long)app->visible_count - 1;
    }
    app->selected = (size_t)next;

    int height = list_height();
    if (app->selected < app->top) {
        app->top = app->selected;
    }
    if (app->selected >= app->top + (size_t)height) {
        app->top = app->selected - (size_t)height + 1;
    }
}

static void page_selection(App* app, int direction) {
    int height = list_height();
    move_selection(app, direction * (height > 1 ? height - 1 : 1));
}

/* ------------------------------------------------------------------------- */
/* Main screen                                                               */
/* ------------------------------------------------------------------------- */

static void draw_frame(App* app) {
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);
    erase();

    if (rows < 16 || cols < 46) {
        curs_set(0);
        draw_text(stdscr, 0, 0, cols,
                  "Terminal too small for save-cmd TUI (need at least 46x16).",
                  attr_error());
        draw_text(stdscr, 1, 0, cols, "Press q to quit.", A_DIM);
        refresh();
        return;
    }

    box(stdscr, 0, 0);

    const char* title = " save-cmd ";
    attron(A_REVERSE | A_BOLD);
    mvaddstr(0, 2, title);
    attroff(A_REVERSE | A_BOLD);

    char counts[64];
    if (app->visible_count == app->db.count) {
        snprintf(counts, sizeof(counts), " %zu command%s ",
                 app->db.count, app->db.count == 1 ? "" : "s");
    } else {
        snprintf(counts, sizeof(counts), " %zu/%zu ",
                 app->visible_count, app->db.count);
    }
    int counts_x = cols - 1 - (int)strlen(counts);
    if (counts_x > 14) {
        attron(A_REVERSE);
        mvaddstr(0, counts_x, counts);
        attroff(A_REVERSE);
    }

    draw_text(stdscr, 1, 2, 8, "Search:", attr_accent());
    int query_x = 10;
    int query_width = cols - query_x - 2;
    if (app->query.len == 0 && !app->searching) {
        draw_text(stdscr, 1, query_x, query_width,
                  g_utf8 ? "type / to filter\u2026" : "type / to filter...",
                  A_DIM);
    } else {
        {
            size_t start = 0;
            if (app->searching) {
                while (start < app->query.cursor &&
                       width_between(app->query.data, start,
                                     app->query.cursor) >= query_width) {
                    start = utf8_next(app->query.data, app->query.len, start);
                }
            }
            draw_text_range(stdscr, 1, query_x, query_width,
                            app->query.data, start, app->query.len, A_BOLD);
            if (app->searching) {
                int offset = width_between(app->query.data, start,
                                           app->query.cursor);
                wmove(stdscr, 1, query_x + offset);
            }
        }
    }

    draw_separator(2, cols);

    int list_top = 3;
    int list_bottom = rows - 9;
    int height = list_bottom - list_top + 1;
    if (height < 1) {
        height = 1;
    }

    if (app->selected < app->top) {
        app->top = app->selected;
    }
    if (app->selected >= app->top + (size_t)height) {
        app->top = app->selected - (size_t)height + 1;
    }

    if (app->visible_count == 0) {
        const char* message = app->db.count == 0
                                  ? "No commands saved yet. Press a to add one."
                                  : "Nothing matches this filter.";
        draw_text(stdscr, list_top + height / 2, 3,
                  cols - 6, message, A_DIM);
    } else {
        int description_col = 3 + (cols - 2) * 3 / 5;
        int command_width = description_col - 5;
        int description_width = cols - 1 - description_col;
        if (command_width < 8) {
            command_width = 8;
        }
        if (description_width < 0) {
            description_width = 0;
        }

        for (int row = 0; row < height; ++row) {
            size_t index = app->top + (size_t)row;
            if (index >= app->visible_count) {
                break;
            }
            const Command* cmd = &app->db.items[app->visible[index]];
            bool selected = index == app->selected;
            int screen_row = list_top + row;

            int attrs = selected ? A_REVERSE : 0;
            wattrset(stdscr, attrs);
            mvwhline(stdscr, screen_row, 1, ' ', cols - 2);
            wattrset(stdscr, A_NORMAL);

            draw_text(stdscr, screen_row, 2, 2, selected ? "> " : "  ", attrs);
            draw_text(stdscr, screen_row, 4, command_width, cmd->command,
                      attrs | (selected ? 0 : A_BOLD));
            if (description_width > 2) {
                const char* description =
                    cmd->description[0] != '\0' ? cmd->description
                                                : "no description";
                int desc_attrs = attrs;
                if (!selected && cmd->description[0] == '\0') {
                    desc_attrs |= A_DIM;
                }
                if (cmd->description[0] != '\0') {
                    desc_attrs |= A_DIM;
                }
                draw_text(stdscr, screen_row, description_col,
                          description_width - 1, description, desc_attrs);
            }
        }

        if (app->visible_count > (size_t)height) {
            char position[64];
            size_t bottom = app->top + (size_t)height;
            if (bottom > app->visible_count) {
                bottom = app->visible_count;
            }
            snprintf(position, sizeof(position), " %zu-%zu/%zu ",
                     app->top + 1, bottom, app->visible_count);
            draw_text(stdscr, list_bottom, cols - 1 - (int)strlen(position),
                      (int)strlen(position), position, A_DIM);
        }
    }

    draw_separator(rows - 8, cols);

    int detail = rows - 7;
    if (app->visible_count == 0) {
        draw_text(stdscr, detail, 3, cols - 6, "No command selected.", A_DIM);
    } else {
        const Command* cmd = &app->db.items[app->visible[app->selected]];
        draw_text(stdscr, detail, 2, 9, "Command:", attr_accent());
        draw_text(stdscr, detail, 12, cols - 14, cmd->command, A_BOLD);
        draw_text(stdscr, detail + 1, 2, 12, "Description:", attr_accent());
        draw_text(stdscr, detail + 1, 16, cols - 18,
                  cmd->description[0] != '\0' ? cmd->description
                                              : "(none)",
                  A_NORMAL);

        char* saved = format_timestamp(cmd->timestamp);
        char meta[1024];
        snprintf(meta, sizeof(meta), "Saved: %s   Binary: %s   ID: %s",
                 saved != NULL ? saved : "unknown",
                 cmd->binary_name != NULL ? cmd->binary_name : "?",
                 cmd->id != NULL ? cmd->id : "?");
        free(saved);
        draw_text(stdscr, detail + 2, 2, cols - 4, meta, A_DIM);
    }

    draw_separator(rows - 4, cols);

    if (app->status[0] != '\0') {
        draw_text(stdscr, rows - 3, 2, cols - 4, app->status,
                  app->status_attr != 0 ? app->status_attr : 0);
    } else if (app->db.count == 0) {
        draw_text(stdscr, rows - 3, 2, cols - 4,
                  "Press a to add your first command.", A_DIM);
    } else {
        draw_text(stdscr, rows - 3, 2, cols - 4,
                  "Enter runs the selection; p prints it to stdout.", A_DIM);
    }

    const char* keys;
    if (app->searching) {
        keys = "Type to filter  Enter run  Esc browse  Ctrl-U clear  "
               "Up/Down move";
    } else if (g_utf8) {
        keys = "q quit  \u2191\u2193 move  Enter run  p print  y copy  "
               "a add  e edit  d delete  / search  ? help";
    } else {
        keys = "q quit  Up/Down move  Enter run  p print  y copy  "
               "a add  e edit  d delete  / search  ? help";
    }
    draw_text(stdscr, rows - 2, 2, cols - 4, keys, A_DIM);

    if (app->searching) {
        curs_set(1);
    } else {
        curs_set(0);
    }
    refresh();
}

/* ------------------------------------------------------------------------- */
/* Dialogs                                                                   */
/* ------------------------------------------------------------------------- */

static bool dialog_confirm(App* app, const char* title, const char* detail) {
    (void)app;
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    int width = cols - 8;
    if (width > 78) {
        width = 78;
    }
    if (width < 30) {
        width = 30;
    }
    if (width > cols) {
        width = cols;
    }
    int height = 7;
    if (height > rows) {
        height = rows;
    }
    WINDOW* win = newwin(height, width, (rows - height) / 2,
                         (cols - width) / 2);
    if (win == NULL) {
        return false;
    }
    keypad(win, TRUE);
    wtimeout(win, 200);

    bool confirmed = false;
    for (;;) {
        werase(win);
        box(win, 0, 0);
        draw_text(win, 0, 2, width - 4, title, attr_accent());
        draw_text(win, 2, 2, width - 4, detail, A_BOLD);
        draw_text(win, height - 2, 2, width - 4,
                  "Press y to confirm, any other key to cancel.", A_DIM);
        wrefresh(win);

        wint_t ch = 0;
        int result = wget_wch(win, &ch);
        if (result == ERR) {
            if (g_interrupted) {
                break;
            }
            continue;
        }
        if (result == KEY_CODE_YES) {
            if (ch == KEY_RESIZE) {
                continue;
            }
            break;
        }
        if (ch == 'y' || ch == 'Y') {
            confirmed = true;
            break;
        }
        break;
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
    return confirmed;
}

static bool form_dialog(const char* title, EditBuf* command, EditBuf* description) {
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    int width = cols - 8;
    if (width > 92) {
        width = 92;
    }
    if (width < 40) {
        width = cols - 2;
    }
    if (width > cols) {
        width = cols;
    }
    int height = 10;
    if (height > rows) {
        height = rows;
    }
    WINDOW* win = newwin(height, width, (rows - height) / 2,
                         (cols - width) / 2);
    if (win == NULL) {
        return false;
    }
    keypad(win, TRUE);
    wtimeout(win, 200);

    int field = 0;
    bool saved = false;
    bool show_error = false;

    for (;;) {
        werase(win);
        box(win, 0, 0);
        draw_text(win, 0, 2, width - 4, title, attr_accent());
        draw_text(win, 2, 2, width - 4, "Command", A_BOLD);
        draw_input(win, 3, 2, width - 4, command, field == 0);
        draw_text(win, 5, 2, width - 4, "Description", A_BOLD);
        draw_input(win, 6, 2, width - 4, description, field == 1);

        if (show_error) {
            draw_text(win, height - 2, 2, width - 4,
                      "Command cannot be empty.", attr_error());
        } else {
            draw_text(win, height - 2, 2, width - 4,
                      "Tab switch   Enter save   Esc cancel", A_DIM);
        }
        wrefresh(win);
        curs_set(1);

        wint_t ch = 0;
        int result = wget_wch(win, &ch);
        if (result == ERR) {
            if (g_interrupted) {
                break;
            }
            continue;
        }
        if (result == KEY_CODE_YES) {
            if (ch == KEY_RESIZE) {
                continue;
            }
            if (ch == KEY_BTAB) {
                field = field == 0 ? 1 : 0;
                continue;
            }
            EditBuf* active = field == 0 ? command : description;
            switch (ch) {
            case KEY_BACKSPACE:
                eb_backspace(active);
                show_error = false;
                break;
            case KEY_DC:
                eb_delete(active);
                break;
            case KEY_LEFT:
                eb_left(active);
                break;
            case KEY_RIGHT:
                eb_right(active);
                break;
            case KEY_HOME:
                eb_home(active);
                break;
            case KEY_END:
                eb_end(active);
                break;
            case KEY_UP:
                field = 0;
                break;
            case KEY_DOWN:
                field = 1;
                break;
            case KEY_ENTER:
                if (command->len == 0) {
                    show_error = true;
                    field = 0;
                } else {
                    saved = true;
                }
                break;
            default:
                break;
            }
            if (saved) {
                break;
            }
            continue;
        }

        switch (ch) {
        case 27: /* Esc */
            goto done;
        case 3: /* Ctrl-C */
            g_interrupted = 1;
            goto done;
        case '\t':
            field = field == 0 ? 1 : 0;
            show_error = false;
            break;
        case '\n':
        case '\r':
            if (command->len == 0) {
                show_error = true;
                field = 0;
            } else {
                saved = true;
            }
            break;
        case 127:
        case 8:
            eb_backspace(field == 0 ? command : description);
            show_error = false;
            break;
        case 21: /* Ctrl-U */
            eb_clear(field == 0 ? command : description);
            break;
        case 23: /* Ctrl-W */
            eb_delete_word(field == 0 ? command : description);
            break;
        case 1: /* Ctrl-A */
            eb_home(field == 0 ? command : description);
            break;
        case 5: /* Ctrl-E */
            eb_end(field == 0 ? command : description);
            break;
        default:
            if (ch >= 32) {
                eb_insert_wchar(field == 0 ? command : description, (wchar_t)ch);
                show_error = false;
            }
            break;
        }
        if (saved) {
            break;
        }
    }

done:
    delwin(win);
    touchwin(stdscr);
    refresh();
    return saved;
}

static void show_help(App* app) {
    (void)app;
    int rows = 0;
    int cols = 0;
    getmaxyx(stdscr, rows, cols);

    int width = cols - 8;
    if (width > 72) {
        width = 72;
    }
    if (width < 40) {
        width = cols - 2;
    }
    if (width > cols) {
        width = cols;
    }
    int height = 21;
    if (height > rows) {
        height = rows - 2;
    }
    if (height < 8) {
        height = rows;
    }
    WINDOW* win = newwin(height, width, (rows - height) / 2,
                         (cols - width) / 2);
    if (win == NULL) {
        return;
    }
    keypad(win, TRUE);
    wtimeout(win, 200);

    const char* lines[] = {
        "Keys",
        "",
        "  /            search / filter",
        "  Esc          leave search / clear filter / quit",
        "  Up/Down      move through results",
        "  PgUp/PgDn    jump one page",
        "  g / G        first / last result",
        "  Enter        run the selected command",
        "  p            print selected command to stdout and exit",
        "  y            copy selected command (OSC 52 clipboard)",
        "  x            run selected command",
        "  a            add a command",
        "  e            edit selected command",
        "  d            delete selected command",
        "  r            reload database",
        "  ?            this help",
        "  q / Esc      quit",
        NULL,
    };

    for (;;) {
        werase(win);
        box(win, 0, 0);
        draw_text(win, 0, 2, width - 4, "save-cmd help", attr_accent());
        int row = 1;
        for (int i = 0; lines[i] != NULL && row < height - 3; ++i, ++row) {
            draw_text(win, row, 2, width - 4, lines[i],
                      i == 0 ? A_BOLD : A_NORMAL);
        }
        draw_text(win, height - 2, 2, width - 4, "Press any key to close.",
                  A_DIM);
        wrefresh(win);

        wint_t ch = 0;
        int result = wget_wch(win, &ch);
        if (result == ERR) {
            if (g_interrupted) {
                break;
            }
            continue;
        }
        break;
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}

/* ------------------------------------------------------------------------- */
/* Actions                                                                   */
/* ------------------------------------------------------------------------- */

static void action_add(App* app) {
    EditBuf command;
    EditBuf description;
    if (!eb_init(&command, "") || !eb_init(&description, "")) {
        eb_free(&command);
        eb_free(&description);
        set_status(app, attr_error(), "Out of memory");
        return;
    }
    if (form_dialog("Add command", &command, &description)) {
        char* error = NULL;
        char* id = NULL;
        if (db_add_path(app->db_path, command.data, description.data, &id,
                        &error)) {
            if (reload_db(app)) {
                select_id(app, id);
                set_status(app, attr_ok(), "Added: %s", command.data);
            }
        } else {
            set_status(app, attr_error(), "Cannot add: %s", error);
        }
        free(error);
        free(id);
    }
    eb_free(&command);
    eb_free(&description);
}

static void action_edit(App* app) {
    if (app->visible_count == 0) {
        return;
    }
    const Command* cmd = &app->db.items[app->visible[app->selected]];
    char* id = cmd->id != NULL ? strdup(cmd->id) : NULL;
    EditBuf command;
    EditBuf description;
    if (!eb_init(&command, cmd->command) ||
        !eb_init(&description, cmd->description)) {
        eb_free(&command);
        eb_free(&description);
        free(id);
        set_status(app, attr_error(), "Out of memory");
        return;
    }
    if (form_dialog("Edit command", &command, &description) && id != NULL) {
        char* error = NULL;
        if (db_update_path(app->db_path, id, command.data, description.data,
                           &error)) {
            if (reload_db(app)) {
                select_id(app, id);
                set_status(app, attr_ok(), "Updated: %s", command.data);
            }
        } else {
            set_status(app, attr_error(), "Cannot update: %s", error);
        }
        free(error);
    }
    eb_free(&command);
    eb_free(&description);
    free(id);
}

static void action_delete(App* app) {
    if (app->visible_count == 0) {
        return;
    }
    const Command* cmd = &app->db.items[app->visible[app->selected]];
    char* id = cmd->id != NULL ? strdup(cmd->id) : NULL;
    if (id == NULL) {
        return;
    }
    char detail[512];
    snprintf(detail, sizeof(detail), "Delete: %s", cmd->command);
    char* deleted = strdup(cmd->command);
    if (dialog_confirm(app, "Delete command", detail)) {
        char* error = NULL;
        if (db_delete_path(app->db_path, id, &error)) {
            if (reload_db(app)) {
                set_status(app, attr_ok(), "Deleted: %s",
                           deleted != NULL ? deleted : id);
            }
        } else {
            set_status(app, attr_error(), "Cannot delete: %s", error);
        }
        free(error);
    }
    free(deleted);
    free(id);
}

static char* base64_encode(const unsigned char* data, size_t len) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_len = 4 * ((len + 2) / 3);
    char* out = malloc(out_len + 1);
    if (out == NULL) {
        return NULL;
    }
    size_t pos = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned int value = (unsigned int)data[i] << 16;
        if (i + 1 < len) {
            value |= (unsigned int)data[i + 1] << 8;
        }
        if (i + 2 < len) {
            value |= data[i + 2];
        }
        out[pos++] = table[(value >> 18) & 0x3F];
        out[pos++] = table[(value >> 12) & 0x3F];
        out[pos++] = i + 1 < len ? table[(value >> 6) & 0x3F] : '=';
        out[pos++] = i + 2 < len ? table[value & 0x3F] : '=';
    }
    out[pos] = '\0';
    return out;
}

static void action_copy(App* app) {
    if (app->visible_count == 0) {
        return;
    }
    const Command* cmd = &app->db.items[app->visible[app->selected]];
    char* encoded =
        base64_encode((const unsigned char*)cmd->command, strlen(cmd->command));
    if (encoded == NULL) {
        set_status(app, attr_error(), "Out of memory");
        return;
    }
    FILE* tty = fopen("/dev/tty", "w");
    bool close_tty = true;
    if (tty == NULL) {
        tty = stdout;
        close_tty = false;
    }
    fprintf(tty, "\033]52;c;%s\a", encoded);
    fflush(tty);
    if (close_tty) {
        fclose(tty);
    }
    free(encoded);
    set_status(app, attr_ok(),
               "Copied to clipboard (terminal must support OSC 52)");
}

static void run_command(App* app) {
    if (app->visible_count == 0) {
        return;
    }
    const Command* cmd = &app->db.items[app->visible[app->selected]];
    char detail[1024];
    snprintf(detail, sizeof(detail), "%s", cmd->command);
    if (!dialog_confirm(app, "Run command", detail)) {
        return;
    }

    char* command = strdup(cmd->command);
    if (command == NULL) {
        set_status(app, attr_error(), "Out of memory");
        return;
    }

    def_prog_mode();
    g_curses_active = false;
    endwin();

    printf("\n$ %s\n\n", command);
    fflush(stdout);
    int status = system(command);

    int exit_code = 1;
    if (status == -1) {
        printf("\nfailed to run command: %s\n", strerror(errno));
    } else if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
        printf("\n[exit status %d]\n", exit_code);
    } else if (WIFSIGNALED(status)) {
        exit_code = 128 + WTERMSIG(status);
        printf("\n[terminated by signal %d]\n", WTERMSIG(status));
    }
    printf("Press Enter to return to save-cmd...");
    fflush(stdout);
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        /* drain */
    }

    reset_prog_mode();
    g_curses_active = true;
    curs_set(0);
    clear();
    refresh();
    set_status(app, exit_code == 0 ? attr_ok() : attr_error(),
               "Command exited with status %d", exit_code);
    free(command);
}

/* ------------------------------------------------------------------------- */
/* Key handling                                                              */
/* ------------------------------------------------------------------------- */

static void choose_current(App* app, bool* quit, char** selected) {
    if (app->visible_count == 0) {
        return;
    }
    const Command* cmd = &app->db.items[app->visible[app->selected]];
    *selected = strdup(cmd->command);
    *quit = true;
}

static void handle_search_key(App* app, wint_t ch, bool* quit) {
    switch (ch) {
    case 27:
        app->searching = false;
        break;
    case 3:
        *quit = true;
        break;
    case '\n':
    case '\r':
        app->searching = false;
        if (app->visible_count > 0) {
            run_command(app);
        }
        break;
    case 127:
    case 8:
        eb_backspace(&app->query);
        refilter(app, NULL);
        break;
    case 21:
        eb_clear(&app->query);
        refilter(app, NULL);
        break;
    case 23:
        eb_delete_word(&app->query);
        refilter(app, NULL);
        break;
    case 1:
        eb_home(&app->query);
        break;
    case 5:
        eb_end(&app->query);
        break;
    default:
        if (ch >= 32) {
            if (eb_insert_wchar(&app->query, (wchar_t)ch)) {
                refilter(app, NULL);
            }
        }
        break;
    }
}

static void handle_browse_key(App* app, wint_t ch, bool* quit,
                              char** selected) {
    switch (ch) {
    case 'q':
    case 3:
        *quit = true;
        break;
    case 27:
        if (app->query.len > 0) {
            eb_clear(&app->query);
            refilter(app, NULL);
            set_status(app, 0, "Filter cleared.");
        } else {
            *quit = true;
        }
        break;
    case '\n':
    case '\r':
        run_command(app);
        break;
    case 'p':
        choose_current(app, quit, selected);
        break;
    case '/':
    case 's':
        app->searching = true;
        eb_end(&app->query);
        break;
    case 'j':
        move_selection(app, 1);
        break;
    case 'k':
        move_selection(app, -1);
        break;
    case 'g':
        app->selected = 0;
        break;
    case 'G':
        if (app->visible_count > 0) {
            app->selected = app->visible_count - 1;
        }
        break;
    case 'a':
        action_add(app);
        break;
    case 'e':
        action_edit(app);
        break;
    case 'd':
        action_delete(app);
        break;
    case 'x':
        run_command(app);
        break;
    case 'y':
        action_copy(app);
        break;
    case 'r':
        if (reload_db(app)) {
            set_status(app, attr_ok(), "Reloaded %zu command(s).",
                       app->db.count);
        }
        break;
    case '?':
        show_help(app);
        break;
    default:
        break;
    }
}

static void handle_function_key(App* app, int key) {
    if (app->searching) {
        switch (key) {
        case KEY_UP:
            move_selection(app, -1);
            break;
        case KEY_DOWN:
            move_selection(app, 1);
            break;
        case KEY_PPAGE:
            page_selection(app, -1);
            break;
        case KEY_NPAGE:
            page_selection(app, 1);
            break;
        case KEY_LEFT:
            eb_left(&app->query);
            break;
        case KEY_RIGHT:
            eb_right(&app->query);
            break;
        case KEY_HOME:
            eb_home(&app->query);
            break;
        case KEY_END:
            eb_end(&app->query);
            break;
        case KEY_BACKSPACE:
            eb_backspace(&app->query);
            refilter(app, NULL);
            break;
        case KEY_DC:
            eb_delete(&app->query);
            refilter(app, NULL);
            break;
        case KEY_ENTER:
            app->searching = false;
            if (app->visible_count > 0) {
                run_command(app);
            }
            break;
        case KEY_RESIZE:
            clear();
            break;
        default:
            break;
        }
        return;
    }

    switch (key) {
    case KEY_UP:
        move_selection(app, -1);
        break;
    case KEY_DOWN:
        move_selection(app, 1);
        break;
    case KEY_PPAGE:
        page_selection(app, -1);
        break;
    case KEY_NPAGE:
        page_selection(app, 1);
        break;
    case KEY_HOME:
        app->selected = 0;
        break;
    case KEY_END:
        if (app->visible_count > 0) {
            app->selected = app->visible_count - 1;
        }
        break;
    case KEY_ENTER:
        run_command(app);
        break;
    case KEY_BACKSPACE:
        if (app->query.len > 0) {
            eb_backspace(&app->query);
            refilter(app, NULL);
        }
        break;
    case KEY_RESIZE:
        clear();
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------------- */
/* Entry point                                                               */
/* ------------------------------------------------------------------------- */

bool tui_available(void) {
    return true;
}

int tui_run(const char* db_path, const char* initial_query, char** selected,
            char** error) {
    if (selected != NULL) {
        *selected = NULL;
    }
    if (error != NULL) {
        *error = NULL;
    }

    CommandDB db;
    char* load_error = NULL;
    if (!db_load(&db, db_path, &load_error)) {
        if (error != NULL) {
            *error = load_error;
        } else {
            free(load_error);
        }
        return -1;
    }

    setlocale(LC_ALL, "");
    g_utf8 = MB_CUR_MAX > 1;

    const char* no_color = getenv("NO_COLOR");
    bool want_color = no_color == NULL;

    if (initscr() == NULL) {
        db_free(&db);
        if (error != NULL) {
            *error = strdup("cannot initialize the terminal");
        }
        return -1;
    }
    g_curses_active = true;
    atexit(tui_cleanup);

    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);
    if (want_color && has_colors()) {
        start_color();
        if (use_default_colors() == OK) {
            init_pair(CP_ACCENT, COLOR_CYAN, -1);
            init_pair(CP_ERROR, COLOR_RED, -1);
            init_pair(CP_OK, COLOR_GREEN, -1);
            g_use_color = true;
        }
    }

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGHUP, &action, NULL);

    App app;
    memset(&app, 0, sizeof(app));
    app.db = db;
    app.db_path = strdup(db_path);
    if (app.db_path == NULL || !eb_init(&app.query, initial_query)) {
        free(app.db_path);
        eb_free(&app.query);
        db_free(&app.db);
        curs_set(1);
        endwin();
        g_curses_active = false;
        if (error != NULL) {
            *error = strdup("out of memory");
        }
        return -1;
    }
    refilter(&app, NULL);
    if (app.db.skipped > 0) {
        set_status(&app, attr_error(), "Skipped %d invalid record(s).",
                   app.db.skipped);
    }
    app.searching = false;

    timeout(200);
    bool quit = false;
    char* chosen = NULL;
    while (!quit && !g_interrupted) {
        draw_frame(&app);

        wint_t ch = 0;
        int result = wget_wch(stdscr, &ch);
        if (result == ERR) {
            if (g_interrupted) {
                break;
            }
            continue;
        }
        if (result == KEY_CODE_YES) {
            handle_function_key(&app, (int)ch);
        } else if (app.searching) {
            handle_search_key(&app, ch, &quit);
        } else {
            handle_browse_key(&app, ch, &quit, &chosen);
        }
    }

    curs_set(1);
    endwin();
    g_curses_active = false;

    if (selected != NULL) {
        *selected = chosen;
    } else {
        free(chosen);
    }

    free(app.visible);
    eb_free(&app.query);
    db_free(&app.db);
    free(app.db_path);
    return 0;
}
