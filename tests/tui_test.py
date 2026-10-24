#!/usr/bin/env python3
"""Pseudo-terminal tests for the save-cmd TUI."""

import fcntl
import json
import os
import pty
import select
import shutil
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time

BIN = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "./save-cmd")
TMP = tempfile.mkdtemp(prefix="save-cmd-tui-test.")
DB_DIR = os.path.join(TMP, "db")
DB = os.path.join(DB_DIR, "commands.json")
ENV = dict(
    os.environ,
    TERM="xterm-256color",
    LANG="C.UTF-8",
    LC_ALL="C.UTF-8",
    SAVE_CMD_DB=DB,
)

failures = 0
checks = 0


def check(condition, message):
    global failures, checks
    checks += 1
    if not condition:
        failures += 1
        print("FAIL: " + message, file=sys.stderr)


def cli(args):
    return subprocess.run(
        [BIN] + args, env=ENV, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )


def reset_db(entries=()):
    shutil.rmtree(DB_DIR, ignore_errors=True)
    for command, description in entries:
        result = cli([command, "-d", description])
        assert result.returncode == 0, result.stderr


def commands():
    if not os.path.exists(DB):
        return []
    with open(DB, encoding="utf-8") as handle:
        return [item["command"] for item in json.load(handle).values()]


def spawn(argv):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 30, 100, 0, 0))
    process = subprocess.Popen(
        argv,
        stdin=slave,
        stdout=slave,
        stderr=slave,
        env=ENV,
        close_fds=True,
        start_new_session=True,
    )
    os.close(slave)
    return process, master


def read_available(fd, seconds):
    data = b""
    deadline = time.time() + seconds
    while time.time() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        data += chunk
    return data


def wait_for(fd, needle, timeout=4.0):
    data = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        chunk = read_available(fd, 0.1)
        if chunk:
            data += chunk
            if needle in data:
                return data, True
        if not chunk:
            ready, _, _ = select.select([fd], [], [], 0.05)
            if not ready:
                continue
    return data, False


def run_tui(keys, argv=None, delay=0.3, timeout=5.0):
    """Run the TUI, send byte keys, return (exit code, captured output)."""
    if argv is None:
        argv = [BIN, "--tui"]
    process, master = spawn(argv)
    output = b""
    try:
        startup, seen = wait_for(master, b"save-cmd", 3.0)
        output += startup
        check(seen, "TUI did not render a startup screen")
        for key in keys:
            os.write(master, key)
            time.sleep(delay)
            output += read_available(master, 0.05)
        try:
            process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            process.kill()
            check(False, "TUI did not exit (keys=%r)" % (keys,))
            return 99, output
        output += read_available(master, 0.25)
        return process.returncode, output
    finally:
        try:
            os.close(master)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Skip when built without ncurses
# ---------------------------------------------------------------------------
probe = subprocess.run(
    [BIN, "--tui"],
    stdin=subprocess.DEVNULL,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    env=ENV,
)
if b"no TUI support" in probe.stderr:
    print("SKIP: this build has no TUI support")
    sys.exit(0)

# ---------------------------------------------------------------------------
# 1. Open the default browser and quit
# ---------------------------------------------------------------------------
reset_db([("echo alpha", "first command"), ("ls -la", "list files")])
code, output = run_tui([b"q"])
check(code == 0, "plain TUI should exit 0 (got %s)" % code)
check(b"Command:" in output, "details pane should be rendered")

# ---------------------------------------------------------------------------
# 2. Search from the command line, leave search mode, and print with p
# ---------------------------------------------------------------------------
reset_db([("git restore --source HEAD~1 file.txt", "restore a file")])
code, output = run_tui([b"\x1b", b"p"], argv=[BIN, "/restore"])
check(code == 0, "search TUI should exit 0")
check(
    output.rstrip().endswith(b"git restore --source HEAD~1 file.txt"),
    "p should print the selected command to stdout",
)

# ---------------------------------------------------------------------------
# 3. Type a filter and press Enter to run the selected command
# ---------------------------------------------------------------------------
reset_db([("true", "typed search execution")])
process, master = spawn([BIN, "--tui"])
try:
    startup, _ = wait_for(master, b"save-cmd", 3.0)
    os.write(master, b"/")
    time.sleep(0.2)
    read_available(master, 0.1)
    os.write(master, b"typed")
    time.sleep(0.2)
    read_available(master, 0.1)
    os.write(master, b"\r")
    time.sleep(0.3)
    read_available(master, 0.2)
    os.write(master, b"y")
    run_output, seen = wait_for(master, b"Press Enter", 6.0)
    check(seen, "Enter in search mode did not run the selected command")
    os.write(master, b"\n")
    time.sleep(0.3)
    read_available(master, 0.2)
    os.write(master, b"q")
    process.wait(timeout=5.0)
    final = read_available(master, 0.3)
    combined = startup + run_output + final
    check(process.returncode == 0, "typed search run should exit 0")
    check(b"$ true" in combined, "command selected from typed search was not run")
    check(b"[exit status 0]" in combined, "exit status was not reported")
finally:
    try:
        os.close(master)
    except OSError:
        pass

# ---------------------------------------------------------------------------
# 4. No match: Enter leaves search mode, then quit
# ---------------------------------------------------------------------------
code, output = run_tui([b"/", b"zzzz-no-match", b"\r", b"q"])
check(code == 0, "empty filter result should not crash")
check(b"Nothing matches this filter" in output, "empty state should be shown")

# ---------------------------------------------------------------------------
# 5. Add a command through the form
# ---------------------------------------------------------------------------
reset_db()
code, output = run_tui(
    [
        b"a",
        b"echo tui-added",
        b"\t",
        b"added through the TUI",
        b"\r",
        b"q",
    ]
)
check(code == 0, "add flow should exit 0")
check("echo tui-added" in commands(), "command was not added to the database")
with open(DB, encoding="utf-8") as handle:
    record = next(iter(json.load(handle).values()))
check(
    record["description"] == "added through the TUI",
    "description was not saved by the form",
)

# ---------------------------------------------------------------------------
# 6. Edit the command
# ---------------------------------------------------------------------------
reset_db([("echo old-command", "old description")])
code, output = run_tui(
    [
        b"e",
        b"\x15",  # Ctrl-U clears Command
        b"echo new-command",
        b"\t",
        b"\x15",  # Ctrl-U clears Description
        b"new description",
        b"\r",
        b"q",
    ],
    argv=[BIN, "--tui", "/old-command"],
)
check(code == 0, "edit flow should exit 0")
check("echo new-command" in commands(), "command was not updated")
check("echo old-command" not in commands(), "old command still present")
with open(DB, encoding="utf-8") as handle:
    record = next(iter(json.load(handle).values()))
check(
    record["description"] == "new description",
    "description was not updated",
)

# ---------------------------------------------------------------------------
# 7. Delete the command (confirm and cancel)
# ---------------------------------------------------------------------------
reset_db([("echo delete-me", "temporary")])
code, output = run_tui([b"d", b"n", b"q"], argv=[BIN, "--tui", "/delete-me"])
check(code == 0, "cancelled delete should exit 0")
check("echo delete-me" in commands(), "cancelled delete removed the command")
code, output = run_tui([b"d", b"y", b"q"], argv=[BIN, "--tui", "/delete-me"])
check(code == 0, "delete flow should exit 0")
check("echo delete-me" not in commands(), "delete did not remove the command")

# ---------------------------------------------------------------------------
# 8. Cancel an add form
# ---------------------------------------------------------------------------
reset_db()
code, output = run_tui([b"a", b"echo cancelled", b"\x1b", b"q"])
check(code == 0, "cancelled add should exit 0")
check(commands() == [], "cancelled add wrote a record")

# ---------------------------------------------------------------------------
# 9. Copy (OSC 52) shows a status message
# ---------------------------------------------------------------------------
reset_db([("echo copy-me", "clipboard")])
code, output = run_tui([b"y", b"q"], argv=[BIN, "--tui", "/copy-me"])
check(code == 0, "copy should exit 0")
check(b"Copied to clipboard" in output, "copy status message missing")
check(b"\x1b]52;c;" in output, "OSC 52 sequence missing")

# ---------------------------------------------------------------------------
# 10. Execute with Enter (confirm, wait, return, quit)
# ---------------------------------------------------------------------------
reset_db([("true", "always succeeds")])
process, master = spawn([BIN, "--tui", "/always"])
try:
    startup, _ = wait_for(master, b"save-cmd", 3.0)
    os.write(master, b"\r")
    time.sleep(0.3)
    read_available(master, 0.2)
    os.write(master, b"y")
    run_output, seen = wait_for(master, b"Press Enter", 6.0)
    check(seen, "execute did not reach the return prompt")
    os.write(master, b"\n")
    time.sleep(0.3)
    read_available(master, 0.2)
    os.write(master, b"q")
    process.wait(timeout=5.0)
    final = read_available(master, 0.3)
    combined = startup + run_output + final
    check(process.returncode == 0, "execute flow should exit 0")
    check(b"$ true" in combined, "executed command was not echoed")
    check(b"[exit status 0]" in combined, "exit status was not reported")
finally:
    try:
        os.close(master)
    except OSError:
        pass

# ---------------------------------------------------------------------------
# 11. Resize while running
# ---------------------------------------------------------------------------
reset_db([("echo alpha", "first"), ("echo beta", "second")])
process, master = spawn([BIN, "--tui"])
try:
    wait_for(master, b"save-cmd", 3.0)
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 45, 140, 0, 0))
    os.kill(process.pid, signal.SIGWINCH)
    time.sleep(0.4)
    read_available(master, 0.2)
    os.write(master, b"j")
    time.sleep(0.2)
    read_available(master, 0.1)
    os.write(master, b"q")
    process.wait(timeout=4.0)
    check(process.returncode == 0, "TUI did not survive a resize")
finally:
    try:
        os.close(master)
    except OSError:
        pass

# ---------------------------------------------------------------------------
# 12. Help overlay
# ---------------------------------------------------------------------------
reset_db([("echo alpha", "first")])
code, output = run_tui([b"?", b"q", b"q"])
check(code == 0, "help overlay should close cleanly")
check(b"save-cmd help" in output, "help overlay did not render")

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
shutil.rmtree(TMP, ignore_errors=True)
print("%d checks, %d failed" % (checks, failures))
sys.exit(1 if failures else 0)
