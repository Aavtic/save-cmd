#ifndef SAVE_CMD_FILEFUNCS_H
#define SAVE_CMD_FILEFUNCS_H

#include <stdbool.h>
#include <stddef.h>

/*
 * A single saved command.
 *
 * `id` is the key used in the JSON database.  New entries use the save time as
 * a decimal string; because the database is an object we make sure that key is
 * unique (two saves in the same second get consecutive timestamps).
 */
typedef struct Command {
    char* id;
    unsigned long timestamp;
    char* binary_name;
    char* command;
    char* description;
} Command;

typedef struct CommandDB {
    Command* items;
    size_t count;
    size_t capacity;
    /* Number of malformed/duplicate records ignored while loading. */
    int skipped;
} CommandDB;

/*
 * Resolve the database location.  `cli_override` (may be NULL) wins over the
 * SAVE_CMD_DB environment variable, which wins over the XDG data directory.
 * The returned string is allocated with malloc().
 */
char* db_resolve_path(const char* cli_override);
char* db_default_path(void);

/* Load a database.  A missing file is an empty database, not an error. */
bool db_load(CommandDB* db, const char* path, char** error);
/* Atomically replace the database. */
bool db_save(const CommandDB* db, const char* path, char** error);
void db_free(CommandDB* db);
/* Newest first. */
void db_sort(CommandDB* db);

/*
 * Convenience mutators.  Each one takes an exclusive lock, reloads the file,
 * applies the change and writes it back atomically, so two save-cmd processes
 * cannot silently lose each other's changes.
 */
bool db_add_path(const char* path, const char* command, const char* description,
                 char** new_id, char** error);
bool db_update_path(const char* path, const char* id, const char* command,
                    const char* description, char** error);
bool db_delete_path(const char* path, const char* id, char** error);

/* Copy every record of `src` into `dst` (used for the legacy db import). */
bool db_import_file(const char* src, const char* dst, size_t* imported,
                    char** error);

/* Helpers shared by the CLI and the TUI. */
char* command_binary_name(const char* command);
char* format_timestamp(unsigned long timestamp);
/* Whole-query substring match (used by the non-interactive CLI). */
bool command_matches(const Command* cmd, const char* query, bool case_sensitive);
/* Tokenised, case-insensitive match (used by the TUI filter). */
bool command_matches_query(const Command* cmd, const char* query);

#endif /* SAVE_CMD_FILEFUNCS_H */
