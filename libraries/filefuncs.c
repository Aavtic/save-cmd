
#include "filefuncs.h"
#include "cJSON.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------------- */
/* Small utilities                                                           */
/* ------------------------------------------------------------------------- */

static void set_error(char** out, const char* fmt, ...) {
    if (out == NULL) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    va_list copy;
    va_copy(copy, ap);
    int needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(ap);
        *out = strdup("unknown error");
        return;
    }
    char* msg = malloc((size_t)needed + 1);
    if (msg == NULL) {
        va_end(ap);
        *out = strdup("out of memory");
        return;
    }
    vsnprintf(msg, (size_t)needed + 1, fmt, ap);
    va_end(ap);
    *out = msg;
}

static char* join_path(const char* base, const char* suffix) {
    size_t blen = strlen(base);
    size_t slen = strlen(suffix);
    bool slash = blen > 0 && base[blen - 1] == '/';
    char* out = malloc(blen + (slash ? 0 : 1) + slen + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, base, blen);
    size_t pos = blen;
    if (!slash) {
        out[pos++] = '/';
    }
    memcpy(out + pos, suffix, slen + 1);
    return out;
}

static char* path_dirname(const char* path) {
    const char* slash = strrchr(path, '/');
    if (slash == NULL) {
        return strdup(".");
    }
    if (slash == path) {
        return strdup("/");
    }
    size_t len = (size_t)(slash - path);
    char* out = malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return out;
}

static bool mkdir_p(const char* dir, char** error) {
    if (dir == NULL || dir[0] == '\0' || strcmp(dir, ".") == 0) {
        return true;
    }
    char* tmp = strdup(dir);
    if (tmp == NULL) {
        set_error(error, "out of memory");
        return false;
    }
    for (char* p = tmp + 1; *p != '\0'; ++p) {
        if (*p != '/') {
            continue;
        }
        *p = '\0';
        if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
            set_error(error, "cannot create directory '%s': %s", tmp,
                      strerror(errno));
            free(tmp);
            return false;
        }
        *p = '/';
    }
    if (mkdir(tmp, 0700) != 0 && errno != EEXIST) {
        set_error(error, "cannot create directory '%s': %s", tmp,
                  strerror(errno));
        free(tmp);
        return false;
    }
    struct stat st;
    if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
        set_error(error, "'%s' is not a directory", tmp);
        free(tmp);
        return false;
    }
    free(tmp);
    return true;
}

static bool ensure_parent_dir(const char* path, char** error) {
    char* dir = path_dirname(path);
    if (dir == NULL) {
        set_error(error, "out of memory");
        return false;
    }
    bool ok = mkdir_p(dir, error);
    free(dir);
    return ok;
}

static char* expand_tilde(const char* path) {
    if (path[0] == '~' && path[1] == '/') {
        const char* home = getenv("HOME");
        if (home != NULL && home[0] != '\0') {
            return join_path(home, path + 2);
        }
    }
    return strdup(path);
}

/* ------------------------------------------------------------------------- */
/* Database path                                                             */
/* ------------------------------------------------------------------------- */

char* db_default_path(void) {
    const char* xdg = getenv("XDG_DATA_HOME");
    if (xdg != NULL && xdg[0] != '\0') {
        return join_path(xdg, "save-cmd/commands.json");
    }
    const char* home = getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        return join_path(home, ".local/share/save-cmd/commands.json");
    }
    return strdup("./commands.json");
}

char* db_resolve_path(const char* cli_override) {
    const char* path = cli_override;
    if (path == NULL || path[0] == '\0') {
        path = getenv("SAVE_CMD_DB");
    }
    if (path != NULL && path[0] != '\0') {
        return expand_tilde(path);
    }
    return db_default_path();
}

/* ------------------------------------------------------------------------- */
/* File I/O                                                                  */
/* ------------------------------------------------------------------------- */

static bool read_text_file(const char* path, char** out, size_t* out_len,
                           char** error) {
    *out = NULL;
    *out_len = 0;

    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            return true; /* missing database == empty database */
        }
        set_error(error, "cannot open '%s': %s", path, strerror(errno));
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        set_error(error, "cannot seek in '%s': %s", path, strerror(errno));
        fclose(file);
        return false;
    }
    long size = ftell(file);
    if (size < 0) {
        set_error(error, "cannot determine size of '%s': %s", path,
                  strerror(errno));
        fclose(file);
        return false;
    }
    rewind(file);

    char* buffer = malloc((size_t)size + 1);
    if (buffer == NULL) {
        set_error(error, "out of memory reading '%s'", path);
        fclose(file);
        return false;
    }
    size_t got = fread(buffer, 1, (size_t)size, file);
    if (got != (size_t)size && ferror(file)) {
        set_error(error, "cannot read '%s': %s", path, strerror(errno));
        free(buffer);
        fclose(file);
        return false;
    }
    fclose(file);
    buffer[got] = '\0';

    *out = buffer;
    *out_len = got;
    return true;
}

static bool write_all(int fd, const char* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        ssize_t written = write(fd, data + off, len - off);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        off += (size_t)written;
    }
    return true;
}

/*
 * Write to a temporary file in the same directory, fsync it, then rename it
 * over the destination.  A crash can therefore never leave a half-written
 * database behind.
 */
static bool atomic_write(const char* path, const char* data, size_t len,
                         char** error) {
    if (!ensure_parent_dir(path, error)) {
        return false;
    }

    size_t tlen = strlen(path) + sizeof(".tmp.XXXXXX");
    char* tmp = malloc(tlen);
    if (tmp == NULL) {
        set_error(error, "out of memory");
        return false;
    }
    snprintf(tmp, tlen, "%s.tmp.XXXXXX", path);

    int fd = mkstemp(tmp);
    if (fd < 0) {
        set_error(error, "cannot create temporary file for '%s': %s", path,
                  strerror(errno));
        free(tmp);
        return false;
    }
    (void)fchmod(fd, 0600);

    bool ok = write_all(fd, data, len);
    if (ok && fsync(fd) != 0) {
        ok = false;
    }
    int saved_errno = errno;
    if (close(fd) != 0 && ok) {
        ok = false;
        saved_errno = errno;
    }
    if (!ok) {
        set_error(error, "cannot write '%s': %s", path, strerror(saved_errno));
        unlink(tmp);
        free(tmp);
        return false;
    }
    if (rename(tmp, path) != 0) {
        set_error(error, "cannot replace '%s': %s", path, strerror(errno));
        unlink(tmp);
        free(tmp);
        return false;
    }
    free(tmp);
    return true;
}

static int lock_database(const char* path, char** error) {
    if (!ensure_parent_dir(path, error)) {
        return -1;
    }
    size_t len = strlen(path) + sizeof(".lock");
    char* lock_path = malloc(len);
    if (lock_path == NULL) {
        set_error(error, "out of memory");
        return -1;
    }
    snprintf(lock_path, len, "%s.lock", path);

    int fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    free(lock_path);
    if (fd < 0) {
        set_error(error, "cannot open lock for '%s': %s", path, strerror(errno));
        return -1;
    }
    while (flock(fd, LOCK_EX) != 0) {
        if (errno == EINTR) {
            continue;
        }
        /* Some filesystems do not implement flock; carry on without it. */
        if (errno == ENOTSUP || errno == ENOSYS || errno == EOPNOTSUPP) {
            break;
        }
        set_error(error, "cannot lock '%s': %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

static void unlock_database(int fd) {
    if (fd < 0) {
        return;
    }
    (void)flock(fd, LOCK_UN);
    close(fd);
}

/* ------------------------------------------------------------------------- */
/* Commands                                                                  */
/* ------------------------------------------------------------------------- */

static void command_clear(Command* cmd) {
    free(cmd->id);
    free(cmd->binary_name);
    free(cmd->command);
    free(cmd->description);
    memset(cmd, 0, sizeof(*cmd));
}

void db_free(CommandDB* db) {
    if (db == NULL) {
        return;
    }
    for (size_t i = 0; i < db->count; ++i) {
        command_clear(&db->items[i]);
    }
    free(db->items);
    memset(db, 0, sizeof(*db));
}

static bool db_push(CommandDB* db, Command cmd) {
    if (db->count == db->capacity) {
        size_t capacity = db->capacity == 0 ? 16 : db->capacity * 2;
        Command* items = realloc(db->items, capacity * sizeof(*items));
        if (items == NULL) {
            command_clear(&cmd);
            return false;
        }
        db->items = items;
        db->capacity = capacity;
    }
    db->items[db->count++] = cmd;
    return true;
}

static int compare_commands(const void* a, const void* b) {
    const Command* ca = a;
    const Command* cb = b;
    if (ca->timestamp < cb->timestamp) {
        return 1;
    }
    if (ca->timestamp > cb->timestamp) {
        return -1;
    }
    return strcmp(cb->id ? cb->id : "", ca->id ? ca->id : "");
}

void db_sort(CommandDB* db) {
    if (db->count > 1) {
        qsort(db->items, db->count, sizeof(*db->items), compare_commands);
    }
}

char* command_binary_name(const char* command) {
    if (command == NULL) {
        return strdup("(unknown)");
    }
    const char* p = command;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        ++p;
    }
    char quote = '\0';
    if (*p == '"' || *p == '\'') {
        quote = *p++;
    }
    const char* start = p;
    while (*p != '\0' && !isspace((unsigned char)*p) &&
           (quote == '\0' || *p != quote)) {
        ++p;
    }
    if (p == start) {
        return strdup("(unknown)");
    }
    char* out = malloc((size_t)(p - start) + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, start, (size_t)(p - start));
    out[p - start] = '\0';
    return out;
}

char* format_timestamp(unsigned long timestamp) {
    if (timestamp == 0) {
        return strdup("unknown");
    }
    time_t seconds = (time_t)timestamp;
    struct tm tm;
    if (localtime_r(&seconds, &tm) == NULL) {
        return strdup("unknown");
    }
    char buffer[64];
    if (strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm) == 0) {
        return strdup("unknown");
    }
    return strdup(buffer);
}

/* ------------------------------------------------------------------------- */
/* JSON                                                                      */
/* ------------------------------------------------------------------------- */

static const char* json_string_field(const cJSON* object, const char* key) {
    const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item != NULL && cJSON_IsString(item) && item->valuestring != NULL) {
        return item->valuestring;
    }
    return NULL;
}

static bool parse_ulong_prefix(const char* text, unsigned long* out) {
    if (text == NULL || !isdigit((unsigned char)text[0])) {
        return false;
    }
    errno = 0;
    char* end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text) {
        return false;
    }
    *out = value;
    return true;
}

static bool database_has_id(const CommandDB* db, const char* id) {
    for (size_t i = 0; i < db->count; ++i) {
        if (db->items[i].id != NULL && strcmp(db->items[i].id, id) == 0) {
            return true;
        }
    }
    return false;
}

bool db_load(CommandDB* db, const char* path, char** error) {
    memset(db, 0, sizeof(*db));

    char* text = NULL;
    size_t length = 0;
    if (!read_text_file(path, &text, &length, error)) {
        return false;
    }
    if (text == NULL || length == 0) {
        free(text);
        return true;
    }

    const char* json_text = text;
    size_t json_length = length;
    if (json_length >= 3 && (unsigned char)json_text[0] == 0xEF &&
        (unsigned char)json_text[1] == 0xBB &&
        (unsigned char)json_text[2] == 0xBF) {
        json_text += 3;
        json_length -= 3;
    }

    bool only_space = true;
    for (size_t i = 0; i < json_length; ++i) {
        if (!isspace((unsigned char)json_text[i])) {
            only_space = false;
            break;
        }
    }
    if (only_space) {
        free(text);
        return true;
    }

    cJSON* root = cJSON_ParseWithLength(json_text, json_length);
    if (root == NULL) {
        const char* where = cJSON_GetErrorPtr();
        size_t offset = 0;
        if (where != NULL && where >= json_text && where <= json_text + json_length) {
            offset = (size_t)(where - json_text);
        }
        set_error(error, "invalid JSON in '%s' near byte %zu", path, offset);
        free(text);
        return false;
    }
    if (!cJSON_IsObject(root)) {
        set_error(error, "'%s' does not contain a JSON object", path);
        cJSON_Delete(root);
        free(text);
        return false;
    }

    for (const cJSON* item = root->child; item != NULL; item = item->next) {
        if (!cJSON_IsObject(item) || item->string == NULL ||
            item->string[0] == '\0') {
            ++db->skipped;
            continue;
        }
        const char* id = item->string;
        if (database_has_id(db, id)) {
            ++db->skipped; /* the file should not contain duplicate keys */
            continue;
        }

        const char* command = json_string_field(item, "command");
        if (command == NULL || command[0] == '\0') {
            ++db->skipped;
            continue;
        }

        Command cmd = {0};
        cmd.id = strdup(id);
        cmd.command = strdup(command);
        const char* description = json_string_field(item, "description");
        cmd.description = strdup(description != NULL ? description : "");
        const char* binary = json_string_field(item, "binary_name");
        cmd.binary_name =
            (binary != NULL && binary[0] != '\0') ? strdup(binary)
                                                 : command_binary_name(command);

        if (cmd.id == NULL || cmd.command == NULL || cmd.description == NULL ||
            cmd.binary_name == NULL) {
            command_clear(&cmd);
            set_error(error, "out of memory");
            cJSON_Delete(root);
            free(text);
            db_free(db);
            return false;
        }

        if (!parse_ulong_prefix(id, &cmd.timestamp)) {
            const cJSON* ts = cJSON_GetObjectItemCaseSensitive(item, "timestamp");
            if (ts != NULL && cJSON_IsNumber(ts) && ts->valuedouble >= 0) {
                cmd.timestamp = (unsigned long)ts->valuedouble;
            }
        }

        if (!db_push(db, cmd)) {
            set_error(error, "out of memory");
            cJSON_Delete(root);
            free(text);
            db_free(db);
            return false;
        }
    }

    cJSON_Delete(root);
    free(text);
    db_sort(db);
    return true;
}

bool db_save(const CommandDB* db, const char* path, char** error) {
    for (size_t i = 0; i < db->count; ++i) {
        for (size_t j = i + 1; j < db->count; ++j) {
            if (db->items[i].id == NULL || db->items[j].id == NULL) {
                continue;
            }
            if (strcmp(db->items[i].id, db->items[j].id) == 0) {
                set_error(error, "refusing to write duplicate id '%s'",
                          db->items[i].id);
                return false;
            }
        }
    }

    cJSON* root = cJSON_CreateObject();
    if (root == NULL) {
        set_error(error, "out of memory");
        return false;
    }
    for (size_t i = 0; i < db->count; ++i) {
        const Command* cmd = &db->items[i];
        if (cmd->id == NULL || cmd->command == NULL ||
            cmd->binary_name == NULL || cmd->description == NULL) {
            cJSON_Delete(root);
            set_error(error, "refusing to write an incomplete record");
            return false;
        }
        cJSON* item = cJSON_CreateObject();
        if (item == NULL ||
            cJSON_AddStringToObject(item, "binary_name", cmd->binary_name) == NULL ||
            cJSON_AddStringToObject(item, "command", cmd->command) == NULL ||
            cJSON_AddStringToObject(item, "description", cmd->description) == NULL) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            set_error(error, "out of memory");
            return false;
        }
        cJSON_AddItemToObject(root, cmd->id, item);
    }

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        set_error(error, "out of memory");
        return false;
    }
    bool ok = atomic_write(path, text, strlen(text), error);
    cJSON_free(text);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Mutations                                                                 */
/* ------------------------------------------------------------------------- */

static unsigned long next_timestamp(const CommandDB* db) {
    time_t now = time(NULL);
    unsigned long timestamp = now > 0 ? (unsigned long)now : 0;
    for (size_t i = 0; i < db->count; ++i) {
        if (db->items[i].timestamp >= timestamp &&
            db->items[i].timestamp != ULONG_MAX) {
            timestamp = db->items[i].timestamp + 1;
        }
    }
    char candidate[32];
    for (;;) {
        snprintf(candidate, sizeof(candidate), "%lu", timestamp);
        if (!database_has_id(db, candidate)) {
            return timestamp;
        }
        if (timestamp == ULONG_MAX) {
            timestamp = 1;
        } else {
            ++timestamp;
        }
    }
}

bool db_add_path(const char* path, const char* command, const char* description,
                 char** new_id, char** error) {
    if (new_id != NULL) {
        *new_id = NULL;
    }
    if (command == NULL || command[0] == '\0') {
        set_error(error, "cannot save an empty command");
        return false;
    }
    int lock = lock_database(path, error);
    if (lock < 0) {
        return false;
    }

    CommandDB db;
    if (!db_load(&db, path, error)) {
        unlock_database(lock);
        return false;
    }

    Command cmd = {0};
    unsigned long timestamp = next_timestamp(&db);
    char id[32];
    snprintf(id, sizeof(id), "%lu", timestamp);
    cmd.id = strdup(id);
    cmd.timestamp = timestamp;
    cmd.command = strdup(command);
    cmd.description = strdup(description != NULL ? description : "");
    cmd.binary_name = command_binary_name(command);
    if (cmd.id == NULL || cmd.command == NULL || cmd.description == NULL ||
        cmd.binary_name == NULL) {
        command_clear(&cmd);
        db_free(&db);
        unlock_database(lock);
        set_error(error, "out of memory");
        return false;
    }

    if (!db_push(&db, cmd)) {
        db_free(&db);
        unlock_database(lock);
        set_error(error, "out of memory");
        return false;
    }
    db_sort(&db);

    bool ok = db_save(&db, path, error);
    if (ok && new_id != NULL) {
        *new_id = strdup(id);
    }
    db_free(&db);
    unlock_database(lock);
    return ok;
}

bool db_update_path(const char* path, const char* id, const char* command,
                    const char* description, char** error) {
    if (command == NULL || command[0] == '\0') {
        set_error(error, "command cannot be empty");
        return false;
    }
    int lock = lock_database(path, error);
    if (lock < 0) {
        return false;
    }

    CommandDB db;
    if (!db_load(&db, path, error)) {
        unlock_database(lock);
        return false;
    }

    Command* target = NULL;
    for (size_t i = 0; i < db.count; ++i) {
        if (db.items[i].id != NULL && strcmp(db.items[i].id, id) == 0) {
            target = &db.items[i];
            break;
        }
    }
    if (target == NULL) {
        set_error(error, "command '%s' no longer exists", id);
        db_free(&db);
        unlock_database(lock);
        return false;
    }

    char* new_command = strdup(command);
    char* new_description = strdup(description != NULL ? description : "");
    char* new_binary = command_binary_name(command);
    if (new_command == NULL || new_description == NULL || new_binary == NULL) {
        free(new_command);
        free(new_description);
        free(new_binary);
        db_free(&db);
        unlock_database(lock);
        set_error(error, "out of memory");
        return false;
    }
    free(target->command);
    free(target->description);
    free(target->binary_name);
    target->command = new_command;
    target->description = new_description;
    target->binary_name = new_binary;

    bool ok = db_save(&db, path, error);
    db_free(&db);
    unlock_database(lock);
    return ok;
}

bool db_delete_path(const char* path, const char* id, char** error) {
    int lock = lock_database(path, error);
    if (lock < 0) {
        return false;
    }

    CommandDB db;
    if (!db_load(&db, path, error)) {
        unlock_database(lock);
        return false;
    }

    size_t index = db.count;
    for (size_t i = 0; i < db.count; ++i) {
        if (db.items[i].id != NULL && strcmp(db.items[i].id, id) == 0) {
            index = i;
            break;
        }
    }
    if (index == db.count) {
        set_error(error, "command '%s' no longer exists", id);
        db_free(&db);
        unlock_database(lock);
        return false;
    }

    command_clear(&db.items[index]);
    if (index + 1 < db.count) {
        memmove(&db.items[index], &db.items[index + 1],
                (db.count - index - 1) * sizeof(*db.items));
    }
    --db.count;

    bool ok = db_save(&db, path, error);
    db_free(&db);
    unlock_database(lock);
    return ok;
}

bool db_import_file(const char* src, const char* dst, size_t* imported,
                    char** error) {
    if (imported != NULL) {
        *imported = 0;
    }
    CommandDB db;
    if (!db_load(&db, src, error)) {
        return false;
    }
    bool ok = db_save(&db, dst, error);
    if (ok && imported != NULL) {
        *imported = db.count;
    }
    db_free(&db);
    return ok;
}

/* ------------------------------------------------------------------------- */
/* Matching                                                                  */
/* ------------------------------------------------------------------------- */

static bool contains_case_insensitive(const char* haystack, const char* needle) {
    if (needle == NULL || needle[0] == '\0') {
        return true;
    }
    size_t needle_len = strlen(needle);
    for (const char* p = haystack; *p != '\0'; ++p) {
        size_t i = 0;
        while (i < needle_len && p[i] != '\0' &&
               tolower((unsigned char)p[i]) ==
                   tolower((unsigned char)needle[i])) {
            ++i;
        }
        if (i == needle_len) {
            return true;
        }
    }
    return false;
}

bool command_matches(const Command* cmd, const char* query, bool case_sensitive) {
    if (query == NULL || query[0] == '\0') {
        return true;
    }
    const char* fields[] = {cmd->binary_name, cmd->command, cmd->description};
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
        const char* field = fields[i] != NULL ? fields[i] : "";
        if (case_sensitive) {
            if (strstr(field, query) != NULL) {
                return true;
            }
        } else if (contains_case_insensitive(field, query)) {
            return true;
        }
    }
    return false;
}

bool command_matches_query(const Command* cmd, const char* query) {
    if (query == NULL || query[0] == '\0') {
        return true;
    }
    char* copy = strdup(query);
    if (copy == NULL) {
        return command_matches(cmd, query, false);
    }
    bool matches = true;
    char* save = NULL;
    for (char* token = strtok_r(copy, " \t\r\n", &save); token != NULL;
         token = strtok_r(NULL, " \t\r\n", &save)) {
        if (!command_matches(cmd, token, false)) {
            matches = false;
            break;
        }
    }
    free(copy);
    return matches;
}
