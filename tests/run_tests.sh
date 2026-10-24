#!/usr/bin/env bash
#
# Integration tests for the non-interactive save-cmd interface.
#
# Usage: tests/run_tests.sh [path-to-save-cmd]

set -u
set -o pipefail

BIN=${1:-./save-cmd}
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")
TMP=$(mktemp -d "${TMPDIR:-/tmp}/save-cmd-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

PASS=0
FAIL=0
OUT=""
RC=0

ok() {
    PASS=$((PASS + 1))
}

bad() {
    FAIL=$((FAIL + 1))
    printf 'FAIL: %s\n' "$*" >&2
}

eq() { # description expected actual
    if [ "$2" = "$3" ]; then
        ok
    else
        bad "$1: expected [$3], got [$2]"
    fi
}

contains() { # description haystack needle
    case "$2" in
    *"$3"*) ok ;;
    *) bad "$1: output does not contain [$3]: [$2]" ;;
    esac
}

lacks() { # description haystack needle
    case "$2" in
    *"$3"*) bad "$1: output unexpectedly contains [$3]" ;;
    *) ok ;;
    esac
}

run() {
    OUT=$("$@" 2>&1)
    RC=$?
}

json_count() {
    python3 -c 'import json,sys; print(len(json.load(open(sys.argv[1]))))' "$1"
}

export SAVE_CMD_DB="$TMP/db/commands.json"
unset NO_COLOR || true

# ---------------------------------------------------------------------------
# Help and version
# ---------------------------------------------------------------------------
run "$BIN" --version
eq "version exit" "$RC" 0
contains "version output" "$OUT" "save-cmd 2.0.0"

run "$BIN" --help
eq "help exit" "$RC" 0
contains "help usage" "$OUT" "Usage:"
contains "help tui" "$OUT" "--tui"

# ---------------------------------------------------------------------------
# Empty database
# ---------------------------------------------------------------------------
run "$BIN" -l
eq "empty list exit" "$RC" 0
eq "empty list output" "$OUT" ""

run "$BIN"
eq "no-arg piped exit" "$RC" 0

# ---------------------------------------------------------------------------
# Saving and listing
# ---------------------------------------------------------------------------
run "$BIN" "git restore --source HEAD~1 file.txt" -d "restore a file"
eq "save exit" "$RC" 0
contains "save output" "$OUT" "Saved:"
[ -f "$SAVE_CMD_DB" ] && ok || bad "database was not created"
eq "one record" "$(json_count "$SAVE_CMD_DB")" "1"

run "$BIN" -l
eq "list after save exit" "$RC" 0
contains "list binary name" "$OUT" "Binary Name: git"
contains "list command" "$OUT" "git restore --source HEAD~1 file.txt"
contains "list description" "$OUT" "restore a file"

"$BIN" "ls -la" -d "list all files" >/dev/null
"$BIN" "echo héllo" >/dev/null

run "$BIN" -l
contains "unicode command" "$OUT" "echo héllo"
contains "no description" "$OUT" "No Description"

FIRST_COMMAND=$("$BIN" -l | awk '/^Command:/ { sub(/^Command:[[:space:]]*/, ""); print; exit }')
eq "newest first" "$FIRST_COMMAND" "echo héllo"

eq "three records" "$(json_count "$SAVE_CMD_DB")" "3"
eq "file mode" "$(stat -c '%a' "$SAVE_CMD_DB")" "600"

# ---------------------------------------------------------------------------
# Search, insert, execute
# ---------------------------------------------------------------------------
run "$BIN" -l /RESTORE
eq "case-insensitive search exit" "$RC" 0
contains "case-insensitive search" "$OUT" "git restore"

run "$BIN" -l /zzz-no-such-command
eq "empty result exit" "$RC" 1
contains "empty result message" "$OUT" "No Pattern Found"

run "$BIN" -i /restore
eq "insert exit" "$RC" 0
eq "insert prints only command" "$OUT" "git restore --source HEAD~1 file.txt"

run "$BIN" -i /zzz-no-such-command
eq "insert miss exit" "$RC" 1

"$BIN" "true" -d "always succeeds" >/dev/null
run "$BIN" -x -y /always
eq "execute success" "$RC" 0

"$BIN" "exit 7" -d "seven" >/dev/null
run "$BIN" -x -y /"exit 7"
eq "execute preserves exit status" "$RC" 7

run "$BIN" -x /true
eq "execute refuses without -y in a pipe" "$RC" 1
contains "execute refusal message" "$OUT" "use -y"

# ---------------------------------------------------------------------------
# Argument validation
# ---------------------------------------------------------------------------
run "$BIN" --bogus
eq "unknown option exit" "$RC" 2
contains "unknown option message" "$OUT" "unknown option"

run "$BIN" -i
eq "-i without search exit" "$RC" 2

run "$BIN" -x
eq "-x without search exit" "$RC" 2

run "$BIN" -i -x /foo
eq "-i with -x exit" "$RC" 2

run "$BIN" "cmd" /foo
eq "command plus search exit" "$RC" 2

run "$BIN" -d "orphan"
eq "description without command exit" "$RC" 2

run "$BIN" --tui --list
eq "tui plus list exit" "$RC" 2

run "$BIN" --list -i /foo
eq "list plus insert exit" "$RC" 2

run "$BIN" --db=
eq "empty db path exit" "$RC" 2

run "$BIN" -d
eq "-d without value exit" "$RC" 2

run "$BIN" --tui </dev/null
eq "tui without terminal exit" "$RC" 1

run "$BIN" -l /
eq "empty search lists everything" "$RC" 0
contains "empty search has record" "$OUT" "echo héllo"

# ---------------------------------------------------------------------------
# Commands that look like options / paths
# ---------------------------------------------------------------------------
run "$BIN" -- "/usr/bin/env"
eq "path command exit" "$RC" 0

run "$BIN" -d "looks like a flag" -- "-rf /tmp/nope"
eq "dash command exit" "$RC" 0

run "$BIN" -l /flag
contains "dash command listed" "$OUT" "-rf /tmp/nope"

run "$BIN" -l /usr/bin/env
contains "path command listed" "$OUT" "/usr/bin/env"

# ---------------------------------------------------------------------------
# Escaping: quotes, backslashes, unicode, newlines
# ---------------------------------------------------------------------------
rm -rf "$TMP/escape"
SAVE_CMD_DB="$TMP/escape/commands.json"
export SAVE_CMD_DB
TRICKY='echo "double" and '\''single'\'' and \backslash'
run "$BIN" "$TRICKY" -d 'description with "quotes" and \slashes'
eq "tricky save exit" "$RC" 0
SAVE_CMD_DB="$TMP/escape/commands.json" python3 - "$TRICKY" "$TMP/escape/commands.json" <<'PY'
import json, sys
db = json.load(open(sys.argv[2]))
cmd = next(iter(db.values()))['command']
expected = sys.argv[1]
assert cmd == expected, (cmd, expected)
PY
if [ $? -eq 0 ]; then ok; else bad "JSON escaping of quotes/backslashes"; fi

run "$BIN" $'echo line1\necho line2'
eq "newline save exit" "$RC" 0
SAVE_CMD_DB="$TMP/escape/commands.json" python3 - <<'PY'
import json, os
db = json.load(open(os.environ['SAVE_CMD_DB']))
assert any('\n' in v['command'] for v in db.values())
PY
if [ $? -eq 0 ]; then ok; else bad "JSON escaping of newline"; fi

# ---------------------------------------------------------------------------
# Unique ids for rapid saves
# ---------------------------------------------------------------------------
rm -rf "$TMP/unique"
export SAVE_CMD_DB="$TMP/unique/commands.json"
for i in 1 2 3 4 5; do
    "$BIN" "echo same-second-$i" >/dev/null || bad "rapid save $i failed"
done
eq "five same-second saves" "$(json_count "$SAVE_CMD_DB")" "5"
SAVE_CMD_DB="$TMP/unique/commands.json" python3 - <<'PY'
import json, os
raw = open(os.environ['SAVE_CMD_DB']).read()
pairs = json.loads(raw, object_pairs_hook=lambda p: p)
keys = [k for k, _ in pairs]
assert len(keys) == len(set(keys)) == 5, keys
PY
if [ $? -eq 0 ]; then ok; else bad "duplicate JSON keys after rapid saves"; fi

# ---------------------------------------------------------------------------
# Broken / empty / missing databases
# ---------------------------------------------------------------------------
printf '{oops' >"$TMP/bad.json"
cp "$TMP/bad.json" "$TMP/bad.original"
SAVE_CMD_DB="$TMP/bad.json" run "$BIN" -l
eq "corrupt db list exit" "$RC" 1
SAVE_CMD_DB="$TMP/bad.json" run "$BIN" "echo nope"
eq "corrupt db save exit" "$RC" 1
if cmp -s "$TMP/bad.json" "$TMP/bad.original"; then
    ok
else
    bad "corrupt database was overwritten"
fi

: >"$TMP/empty.json"
SAVE_CMD_DB="$TMP/empty.json" run "$BIN" -l
eq "empty file list exit" "$RC" 0
SAVE_CMD_DB="$TMP/empty.json" run "$BIN" "pwd"
eq "save into empty file exit" "$RC" 0
eq "save into empty file count" "$(json_count "$TMP/empty.json")" "1"

SAVE_CMD_DB="$TMP/deep/a/b/commands.json" run "$BIN" "mkdir -p test"
eq "missing directory save exit" "$RC" 0
[ -f "$TMP/deep/a/b/commands.json" ] && ok || bad "missing directories not created"
if find "$TMP/deep" -name '*.tmp.*' -print -quit | grep -q .; then
    bad "temporary file left behind"
else
    ok
fi

# ---------------------------------------------------------------------------
# Concurrent writers
# ---------------------------------------------------------------------------
rm -rf "$TMP/concurrent"
export SAVE_CMD_DB="$TMP/concurrent/commands.json"
for i in $(seq 1 20); do
    "$BIN" "echo concurrent-$i" >/dev/null 2>&1 &
done
wait
eq "concurrent saves count" "$(json_count "$SAVE_CMD_DB")" "20"

# ---------------------------------------------------------------------------
# --db override
# ---------------------------------------------------------------------------
export SAVE_CMD_DB="$TMP/db/commands.json"
run "$BIN" --db "$TMP/alternate.json" "alternate command"
eq "db override save exit" "$RC" 0
[ -f "$TMP/alternate.json" ] && ok || bad "--db file not created"
run "$BIN" --db "$TMP/alternate.json" -l
contains "db override list" "$OUT" "alternate command"
run "$BIN" -l
lacks "default db unaffected" "$OUT" "alternate command"

run "$BIN" --db="$TMP/alternate.json" -l /alternate
contains "db= syntax" "$OUT" "alternate command"

# ---------------------------------------------------------------------------
# Legacy ./commands.json import
# ---------------------------------------------------------------------------
mkdir -p "$TMP/legacy" "$TMP/xdg-home"
cat >"$TMP/legacy/commands.json" <<'JSON'
{
    "1700000000": {
        "binary_name": "echo",
        "command": "legacy imported command",
        "description": "from the old database"
    }
}
JSON
LEGACY_OUT=$(cd "$TMP/legacy" && env -u SAVE_CMD_DB XDG_DATA_HOME="$TMP/xdg-home" "$BIN" -l 2>&1)
contains "legacy import" "$LEGACY_OUT" "legacy imported command"
eq "legacy import record count" "$(json_count "$TMP/xdg-home/save-cmd/commands.json")" "1"
LEGACY_AGAIN=$(cd "$TMP/legacy" && env -u SAVE_CMD_DB XDG_DATA_HOME="$TMP/xdg-home" "$BIN" -l 2>&1)
eq "legacy import is one-shot" "$(json_count "$TMP/xdg-home/save-cmd/commands.json")" "1"
contains "legacy still listed" "$LEGACY_AGAIN" "legacy imported command"

# ---------------------------------------------------------------------------
# Output details
# ---------------------------------------------------------------------------
run "$BIN" -l
lacks "piped output has no ANSI escapes" "$OUT" "$(printf '\033')"

run "$BIN" -l
contains "fields are aligned" "$OUT" "Command:     "

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
printf '\n%d passed, %d failed\n' "$PASS" "$FAIL"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
