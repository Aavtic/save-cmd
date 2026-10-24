# save-cmd

Save shell commands once, find them again in seconds. `save-cmd` is a small C
utility with a searchable full-screen browser, a scriptable command line, and a
single JSON database.

```text
save-cmd "git restore --source HEAD~1 path/to/file" -d "restore one file"
save-cmd /restore            # opens the browser filtered to "restore"
```

## Features

- **Save** any command with an optional description.
- **Search** by binary name, command text, or description (case-insensitive).
- **Browse** in a full-screen TUI: filter as you type, inspect details, navigate
  with the keyboard, then run, print, or copy the selected command.
- **Manage** entries without editing JSON: add, edit, and delete from the TUI.
- **Scriptable** non-interactive modes: plain listing, print the first match, or
  execute the first match.
- **Safe storage**: atomic writes, an exclusive lock around mutations, unique
  IDs, and a `0600` database file.
- **No lost data** when two saves happen in the same second.
- **Portable database location** (`$XDG_DATA_HOME/save-cmd/commands.json`), with
  a one-time import of an old `./commands.json`.

## Requirements

- A C11 compiler and `make`.
- `ncursesw` for the TUI (optional). Without it the CLI still builds and works;
  only `--tui` is unavailable.
- `python3` is only needed to run the test suite.

Debian/Ubuntu:

```sh
sudo apt install build-essential libncurses-dev
```

Fedora: `sudo dnf install gcc make ncurses-devel`
Arch: `sudo pacman -S base-devel ncurses`

## Build and install

```sh
git clone https://github.com/Aavtic/save-cmd.git
cd save-cmd

make                 # builds ./save-cmd
make install         # installs to ~/.local/bin/save-cmd
```

`make install` installs a self-contained binary; it does not create a wrapper
script or depend on the clone staying in place. Override the destination with
`PREFIX=/usr/local make install` (or `DESTDIR=` for packaging).

Other targets:

```sh
make run             # build and launch the TUI
make test            # CLI integration tests
make test-tui        # pseudo-terminal tests for the TUI
make test-all
make asan            # AddressSanitizer/UBSan build
make uninstall
```

## Usage

### Save a command

```sh
save-cmd "<command>"
save-cmd "<command>" -d "<description>"
```

The command is stored as a single argument, so quote it. Examples:

```sh
save-cmd "git restore --source HEAD~1 file.txt" -d "restore a file"
save-cmd "docker system prune -af" -d "reclaim disk space"
save-cmd "tar -xzf archive.tar.gz -C target/"
```

Commands that begin with `-` or `/` would otherwise look like an option or a
search, so put them after `--`:

```sh
save-cmd -d "dangerous example" -- "-rf /tmp/scratch"
save-cmd -- "/usr/bin/env"
```

### Open the browser

```sh
save-cmd                 # full TUI
save-cmd /restore        # TUI filtered to "restore"
save-cmd --tui /docker   # force the TUI
```

The TUI opens automatically when both stdin and stdout are terminals. When the
output is piped or redirected, `save-cmd` prints plain text instead, which keeps
scripts and pagers working.

```text
┌ save-cmd ───────────────────────────────────────────── 12 commands ┐
│ Search: restore                                                     │
├─────────────────────────────────────────────────────────────────────┤
│ > git restore --source HEAD~1 file.txt        restore a file        │
│   git restore --staged file.txt               unstage a file        │
│                                                                     │
├─────────────────────────────────────────────────────────────────────┤
│ Command:     git restore --source HEAD~1 file.txt                   │
│ Description: restore a file                                         │
│ Saved: 2026-10-24 08:36:58   Binary: git   ID: 1792811218           │
├─────────────────────────────────────────────────────────────────────┤
│ q quit  ↑↓ move  Enter run  p print  y copy  a add  e edit  d delete │
└─────────────────────────────────────────────────────────────────────┘
```

### TUI keys

| Key | Action |
| --- | --- |
| `/` or `s` | start filtering (the filter applies as you type) |
| `Enter` | run the selected command (confirmation prompt) and stay in the TUI |
| `p` | print the selected command to stdout and exit |
| `Esc` | leave search mode (filter is kept); with an active filter, clear it; otherwise quit |
| `Up` / `Down`, `j` / `k` | move through results |
| `PgUp` / `PgDn` | move one page |
| `g` / `G` | first / last result |
| `y` | copy the command using the terminal's OSC 52 clipboard |
| `x` | run the command after a confirmation prompt |
| `a` | add a command |
| `e` | edit the selected command |
| `d` | delete the selected command (confirmation) |
| `r` | reload the database from disk |
| `?` | help overlay |
| `q` | quit |

While filtering, `Enter` runs the highlighted result; `Esc` leaves the filter
active so you can browse with the arrows. Filtering is case-insensitive and
whitespace-separated terms all have to match somewhere in the binary name,
command, or description.

`Enter` (or `x`) runs the command with `/bin/sh -c` after a confirmation prompt
and suspending the TUI. When it finishes, press Enter to return to the browser.
Use `p` when you want to print a command to stdout for shell substitution
instead of running it.

`y` emits an OSC 52 escape sequence. Most modern terminals (kitty, alacritty,
wezterm, foot, iTerm2, Windows Terminal, recent xterm) honor it; some require
enabling clipboard access.

### Non-interactive use

```sh
save-cmd -l                  # list every command
save-cmd -l /restore         # list matches
save-cmd -i /restore         # print the first matching command
save-cmd -x /restore         # run the first matching command (asks first)
save-cmd -x -y /restore      # run without asking
save-cmd --no-color -l       # disable ANSI highlighting
```

`-i` writes only the command text, so it composes well:

```sh
cmd=$(save-cmd -i /restore)
eval "$cmd"
```

`-x` refuses to run without confirmation when stdin is not a terminal unless
`-y` is given. The process exit status is the executed command's exit status.

### Options

```text
-t, --tui             force the interactive browser
-l, --list            plain text output, no TUI
-i, --insert          print the first match and exit
-x, --execute         run the first match and exit
-y, --yes             skip the confirmation prompt for -x
-d, --description S   description for the command being saved
    --db PATH         use another database file
    --no-color        disable ANSI colour
-h, --help            show help
-v, --version         show the version
```

Use `--` before a command that starts with `-` or `/`. Options after `--` are
treated as part of the command.

## Where data lives

By default:

```text
$XDG_DATA_HOME/save-cmd/commands.json
# or, when XDG_DATA_HOME is unset:
~/.local/share/save-cmd/commands.json
```

Overrides, in order of precedence:

1. `--db PATH`
2. `SAVE_CMD_DB=/path/to/commands.json`
3. `$XDG_DATA_HOME/save-cmd/commands.json`
4. `~/.local/share/save-cmd/commands.json`
5. `./commands.json` (only if `HOME` is unset)

The database is a JSON object keyed by a unique save timestamp:

```json
{
  "1792811218": {
    "binary_name": "git",
    "command": "git restore --source HEAD~1 file.txt",
    "description": "restore a file"
  }
}
```

Writes go to a temporary file in the same directory, are `fsync`ed, and are
renamed over the database. Mutations hold an exclusive `flock` on
`commands.json.lock`, so concurrent saves do not lose entries. New records get a
unique key even when several commands are saved within the same second.

### Upgrading from 1.x

Version 1 kept `commands.json` in the current directory (the install wrapper
`cd`ed into the repository). Version 2 uses the XDG data directory instead. If
the default database does not exist and a non-empty `./commands.json` is found,
the first run imports it automatically and prints a note. You can also point at
the old file explicitly:

```sh
SAVE_CMD_DB=/path/to/old/commands.json save-cmd
```

## Exit codes

| Code | Meaning |
| --- | --- |
| 0 | success, or a match was found |
| 1 | no match, invalid database, or runtime error |
| 2 | invalid command-line usage |
| child status | `-x` returns the executed command's exit status (128 + signal if killed) |

## Environment

| Variable | Effect |
| --- | --- |
| `SAVE_CMD_DB` | database path |
| `XDG_DATA_HOME` | base directory for the default database |
| `NO_COLOR` | disable ANSI colour in plain output and the TUI |

## What changed in 2.0

The 1.x prototype had several correctness problems that are fixed here:

- `read_entire_file` did not NUL-terminate its buffer, causing out-of-bounds
  reads in `strlen`/cJSON on every operation (confirmed with ASan).
- A missing `commands.json` called `fclose(NULL)` and segfaulted.
- `sprintf` into `char timestamp[10]` overflowed the stack for 10-digit epochs.
- Two saves in the same second produced duplicate JSON keys; keys are now
  unique.
- Empty `commands.json` broke listing; empty files are now treated as an empty
  database.
- Unknown options exited successfully and `-i`/`-x` were documented but never
  implemented. Both work now, with proper exit codes.
- Writes were not atomic and concurrent saves could clobber each other; writes
  are now atomic and locked.
- The database path depended on the working directory and the "install" script
  was a wrapper pinned to the clone; storage moved to XDG and install is a real
  binary.
- Output colour is emitted only to a terminal (`NO_COLOR` supported), duplicate
  printing/search code was consolidated, and JSON/string allocations are freed.
- Build artifacts (`cJSON.o`, the binary, the generated `commands.json`) are no
  longer tracked; `make clean`, `make test`, and `make install` behave normally.
