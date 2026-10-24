
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libraries/filefuncs.h"
#include "libraries/tui.h"

#define SAVE_CMD_VERSION "2.0.0"

typedef struct Options {
    bool help;
    bool version;
    bool tui;
    bool list;
    bool insert;
    bool execute;
    bool assume_yes;
    bool color;
    bool search_set;
    bool command_set;
    bool description_set;
    const char* search;
    const char* command;
    const char* description;
    const char* db_path;
} Options;

static bool g_color = false;

static void print_usage(FILE* out) {
    fprintf(out,
            "save-cmd %s - save and recall shell commands\n"
            "\n"
            "Usage:\n"
            "  save-cmd                          open the browser (list when piped)\n"
            "  save-cmd <command> [-d <desc>]    save a command\n"
            "  save-cmd /<term>                  browse or filter saved commands\n"
            "  save-cmd -i /<term>               print the first matching command\n"
            "  save-cmd -x /<term> [-y]          run the first matching command\n"
            "  save-cmd -l [/<term>]             plain listing (never opens the TUI)\n"
            "\n"
            "Options:\n"
            "  -t, --tui             force the interactive browser\n"
            "  -l, --list            plain text output, no TUI\n"
            "  -i, --insert          print the first match and exit\n"
            "  -x, --execute         run the first match and exit\n"
            "  -y, --yes             skip the confirmation prompt for -x\n"
            "  -d, --description S   description for the command being saved\n"
            "      --db PATH         use another database file\n"
            "      --no-color        disable ANSI colour\n"
            "  -h, --help            show this help\n"
            "  -v, --version         show the version\n"
            "\n"
            "Use `--` before a command that starts with '-' or '/'.\n"
            "Environment: SAVE_CMD_DB, XDG_DATA_HOME, NO_COLOR\n",
            SAVE_CMD_VERSION);
}

static bool set_error(char** error, const char* message) {
    *error = strdup(message);
    return false;
}

static bool parse_arguments(int argc, char** argv, Options* options,
                            char** error) {
    bool end_of_options = false;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];

        if (!end_of_options && strcmp(arg, "--") == 0) {
            end_of_options = true;
            continue;
        }

        if (!end_of_options) {
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                options->help = true;
                continue;
            }
            if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
                options->version = true;
                continue;
            }
            if (strcmp(arg, "-t") == 0 || strcmp(arg, "--tui") == 0) {
                options->tui = true;
                continue;
            }
            if (strcmp(arg, "-l") == 0 || strcmp(arg, "--list") == 0) {
                options->list = true;
                continue;
            }
            if (strcmp(arg, "-i") == 0 || strcmp(arg, "--insert") == 0) {
                options->insert = true;
                continue;
            }
            if (strcmp(arg, "-x") == 0 || strcmp(arg, "--execute") == 0) {
                options->execute = true;
                continue;
            }
            if (strcmp(arg, "-y") == 0 || strcmp(arg, "--yes") == 0) {
                options->assume_yes = true;
                continue;
            }
            if (strcmp(arg, "--no-color") == 0) {
                options->color = false;
                continue;
            }

            const char* value = NULL;
            if (strcmp(arg, "-d") == 0 || strcmp(arg, "--description") == 0) {
                if (options->description_set) {
                    return set_error(error, "description specified twice");
                }
                if (i + 1 >= argc) {
                    return set_error(error, "-d requires a description");
                }
                value = argv[++i];
                options->description = value;
                options->description_set = true;
                continue;
            }
            if (strncmp(arg, "--description=", 14) == 0) {
                if (options->description_set) {
                    return set_error(error, "description specified twice");
                }
                options->description = arg + 14;
                options->description_set = true;
                continue;
            }
            if (strcmp(arg, "--db") == 0) {
                if (i + 1 >= argc) {
                    return set_error(error, "--db requires a path");
                }
                options->db_path = argv[++i];
                continue;
            }
            if (strncmp(arg, "--db=", 5) == 0) {
                options->db_path = arg + 5;
                continue;
            }

            if (arg[0] == '-' && arg[1] != '\0') {
                size_t len = strlen(arg) + 32;
                *error = malloc(len);
                if (*error != NULL) {
                    snprintf(*error, len, "unknown option '%s'", arg);
                }
                return false;
            }

            if (arg[0] == '/') {
                if (options->search_set) {
                    return set_error(error, "more than one search pattern");
                }
                options->search = arg + 1;
                options->search_set = true;
                continue;
            }
        }

        if (options->command_set) {
            return set_error(error, "more than one command specified");
        }
        options->command = arg;
        options->command_set = true;
    }

    if (options->help || options->version) {
        return true;
    }
    if (options->insert && options->execute) {
        return set_error(error, "-i and -x cannot be used together");
    }
    if (options->tui && options->list) {
        return set_error(error, "--tui and --list cannot be used together");
    }
    if (options->list && (options->insert || options->execute)) {
        return set_error(error, "--list cannot be combined with -i/-x");
    }
    if (options->db_path != NULL && options->db_path[0] == '\0') {
        return set_error(error, "--db requires a non-empty path");
    }
    if ((options->insert || options->execute) && options->tui) {
        return set_error(error, "-i/-x cannot be combined with --tui");
    }
    if (options->insert && !options->search_set) {
        return set_error(error, "-i requires a /search pattern");
    }
    if (options->execute && !options->search_set) {
        return set_error(error, "-x requires a /search pattern");
    }
    if (options->search_set && options->search[0] == '\0' &&
        (options->insert || options->execute)) {
        return set_error(error,
                         "-i/-x require a non-empty search pattern");
    }
    if (options->command_set && options->search_set) {
        return set_error(error,
                         "cannot save a command and search at the same time");
    }
    if (options->command_set && options->description_set &&
        options->command[0] == '\0') {
        return set_error(error, "cannot save an empty command");
    }
    if (options->description_set && !options->command_set) {
        return set_error(error, "-d can only be used when saving a command");
    }
    if (options->command_set && options->list) {
        return set_error(error, "--list cannot be used when saving");
    }
    if (options->command_set && (options->insert || options->execute)) {
        return set_error(error, "-i/-x cannot be used when saving");
    }
    if (options->command_set && options->tui) {
        return set_error(error, "--tui cannot be used when saving");
    }
    if (options->command_set && options->command[0] == '\0') {
        return set_error(error, "cannot save an empty command");
    }
    return true;
}

/* ------------------------------------------------------------------------- */
/* Output                                                                    */
/* ------------------------------------------------------------------------- */

static bool starts_with_case_insensitive(const char* text, const char* prefix,
                                         size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (text[i] == '\0' ||
            tolower((unsigned char)text[i]) !=
                tolower((unsigned char)prefix[i])) {
            return false;
        }
    }
    return true;
}

static void print_highlighted(FILE* out, const char* text,
                              const char* highlight) {
    if (!g_color || highlight == NULL || highlight[0] == '\0') {
        fputs(text, out);
        return;
    }
    size_t len = strlen(highlight);
    const char* p = text;
    while (*p != '\0') {
        if (starts_with_case_insensitive(p, highlight, len)) {
            fputs("\033[31;1;4m", out);
            fwrite(p, 1, len, out);
            fputs("\033[0m", out);
            p += len;
        } else {
            fputc(*p, out);
            ++p;
        }
    }
}

static void print_field(FILE* out, const char* label, const char* value,
                        const char* highlight) {
    fprintf(out, "%-13s", label);
    print_highlighted(out, value, highlight);
    fputc('\n', out);
}

static void print_command(FILE* out, const Command* cmd, const char* highlight) {
    print_field(out, "Binary Name:", cmd->binary_name, highlight);
    print_field(out, "Command:", cmd->command, highlight);
    print_field(out, "Description:",
                cmd->description[0] != '\0' ? cmd->description
                                            : "No Description",
                highlight);
}

static const Command* find_first(const CommandDB* db, const char* query) {
    for (size_t i = 0; i < db->count; ++i) {
        if (command_matches(&db->items[i], query, false)) {
            return &db->items[i];
        }
    }
    return NULL;
}

static const char* command_for_execution(const Command* cmd) {
    return cmd->command;
}

static int execute_command(const Command* cmd, bool assume_yes) {
    if (!assume_yes) {
        if (!isatty(STDIN_FILENO)) {
            fprintf(stderr,
                    "save-cmd: refusing to run a command non-interactively; "
                    "use -y to allow it\n");
            return 1;
        }
        fprintf(stderr, "$ %s\nRun? [y/N] ", cmd->command);
        fflush(stderr);
        char answer[16];
        if (fgets(answer, sizeof(answer), stdin) == NULL ||
            (answer[0] != 'y' && answer[0] != 'Y')) {
            fprintf(stderr, "Cancelled.\n");
            return 1;
        }
    }
    fflush(stdout);
    int status = system(command_for_execution(cmd));
    if (status == -1) {
        fprintf(stderr, "save-cmd: cannot run command: %s\n", strerror(errno));
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

static bool database_warning(const CommandDB* db, const char* path) {
    if (db->skipped > 0) {
        fprintf(stderr,
                "save-cmd: warning: skipped %d invalid or duplicate record(s) "
                "in %s\n",
                db->skipped, path);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------------- */
/* Legacy import                                                             */
/* ------------------------------------------------------------------------- */

static void maybe_import_legacy(const char* destination, bool explicit_db) {
    if (explicit_db) {
        return;
    }
    struct stat st;
    if (stat(destination, &st) == 0) {
        return;
    }
    if (strcmp(destination, "./commands.json") == 0 ||
        strcmp(destination, "commands.json") == 0) {
        return;
    }
    if (stat("./commands.json", &st) != 0 || !S_ISREG(st.st_mode) ||
        st.st_size < 3) {
        return;
    }
    size_t imported = 0;
    char* error = NULL;
    if (db_import_file("./commands.json", destination, &imported, &error) &&
        imported > 0) {
        fprintf(stderr,
                "save-cmd: imported %zu command(s) from ./commands.json into "
                "%s\n",
                imported, destination);
    }
    free(error);
}

/* ------------------------------------------------------------------------- */
/* Main                                                                      */
/* ------------------------------------------------------------------------- */

int main(int argc, char** argv) {
    Options options = {0};
    options.color = true;

    char* error = NULL;
    if (!parse_arguments(argc, argv, &options, &error)) {
        if (error != NULL) {
            fprintf(stderr, "save-cmd: %s\n\n", error);
            free(error);
        } else {
            fprintf(stderr, "save-cmd: invalid arguments\n\n");
        }
        print_usage(stderr);
        return 2;
    }

    if (options.help) {
        print_usage(stdout);
        return 0;
    }
    if (options.version) {
        printf("save-cmd %s\n", SAVE_CMD_VERSION);
        return 0;
    }

    g_color = options.color && isatty(STDOUT_FILENO) &&
              getenv("NO_COLOR") == NULL;

    char* db_path = db_resolve_path(options.db_path);
    if (db_path == NULL) {
        fprintf(stderr, "save-cmd: out of memory\n");
        return 1;
    }
    bool explicit_db =
        options.db_path != NULL || getenv("SAVE_CMD_DB") != NULL;
    maybe_import_legacy(db_path, explicit_db);

    if (options.command_set) {
        char* new_id = NULL;
        if (!db_add_path(db_path, options.command, options.description,
                         &new_id, &error)) {
            fprintf(stderr, "save-cmd: %s\n", error != NULL ? error
                                                             : "cannot save");
            free(error);
            free(db_path);
            return 1;
        }
        printf("Saved: %s\n", options.command);
        free(new_id);
        free(db_path);
        return 0;
    }

    CommandDB db;
    if (!db_load(&db, db_path, &error)) {
        fprintf(stderr, "save-cmd: %s\n", error != NULL ? error
                                                         : "cannot load database");
        free(error);
        free(db_path);
        return 1;
    }

    database_warning(&db, db_path);

    bool interactive = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    bool want_tui = !options.list && interactive;
    if (options.tui) {
        if (!tui_available()) {
            fprintf(stderr,
                    "save-cmd: this build has no TUI support (install ncurses "
                    "and rebuild)\n");
            db_free(&db);
            free(db_path);
            return 1;
        }
        if (!interactive) {
            fprintf(stderr,
                    "save-cmd: the TUI requires an interactive terminal\n");
            db_free(&db);
            free(db_path);
            return 1;
        }
        want_tui = true;
    }

    if (options.insert || options.execute) {
        const Command* found =
            find_first(&db, options.search_set ? options.search : "");
        if (found == NULL) {
            fprintf(stderr, "save-cmd: no matching command\n");
            db_free(&db);
            free(db_path);
            return 1;
        }
        if (options.insert) {
            printf("%s\n", found->command);
            db_free(&db);
            free(db_path);
            return 0;
        }
        int status = execute_command(found, options.assume_yes);
        db_free(&db);
        free(db_path);
        return status;
    }

    if (want_tui) {
        char* selected = NULL;
        if (tui_run(db_path, options.search_set ? options.search : "",
                    &selected, &error) != 0) {
            fprintf(stderr, "save-cmd: %s\n",
                    error != NULL ? error : "cannot start the TUI");
            free(error);
            db_free(&db);
            free(db_path);
            return 1;
        }
        if (selected != NULL) {
            printf("%s\n", selected);
            free(selected);
        }
        db_free(&db);
        free(db_path);
        return 0;
    }

    const char* query = options.search_set ? options.search : NULL;
    size_t matches = 0;
    for (size_t i = 0; i < db.count; ++i) {
        if (query != NULL && !command_matches(&db.items[i], query, false)) {
            continue;
        }
        print_command(stdout, &db.items[i], query);
        fputc('\n', stdout);
        ++matches;
    }

    int status = 0;
    if (query != NULL && query[0] != '\0' && matches == 0) {
        printf("No Pattern Found\n");
        status = 1;
    }

    db_free(&db);
    free(db_path);
    return status;
}
