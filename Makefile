CC ?= cc
PKG_CONFIG ?= pkg-config
PREFIX ?= $(HOME)/.local
DESTDIR ?=
BUILD ?= build

CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -MMD -MP
LDLIBS ?=

# ncurses is optional: without it the CLI still builds, only the TUI is
# disabled (libraries/tui_stub.c is used instead).
TUI_CFLAGS := $(shell $(PKG_CONFIG) --cflags ncursesw 2>/dev/null)
TUI_LIBS := $(shell $(PKG_CONFIG) --libs ncursesw 2>/dev/null)
ifeq ($(strip $(TUI_LIBS)),)
TUI_CFLAGS :=
TUI_LIBS := -lncursesw
endif

HAVE_TUI := $(shell printf 'int main(void){return 0;}\n' | \
	$(CC) -x c -include curses.h - -o /dev/null $(TUI_CFLAGS) $(TUI_LIBS) >/dev/null 2>&1 && echo 1 || echo 0)

ifeq ($(HAVE_TUI),1)
TUI_SRC := libraries/tui.c
TUI_FLAGS := -DHAVE_TUI=1
else
TUI_SRC := libraries/tui_stub.c
TUI_FLAGS :=
TUI_LIBS :=
endif

SRC := save-cmd.c libraries/filefuncs.c libraries/cJSON.c $(TUI_SRC)
OBJ := $(patsubst %.c,$(BUILD)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)
BIN := save-cmd

.PHONY: all compile run test test-tui test-all asan clean install uninstall

all: $(BIN)
compile: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) $(TUI_FLAGS) -o $@ $(OBJ) $(LDLIBS) $(TUI_LIBS)

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(TUI_FLAGS) $(TUI_CFLAGS) -c -o $@ $<

-include $(DEP)

run: $(BIN)
	./$(BIN)

test: $(BIN)
	./tests/run_tests.sh ./$(BIN)

test-tui: $(BIN)
	python3 ./tests/tui_test.py ./$(BIN)

test-all: test test-tui

asan:
	$(MAKE) BUILD=build-asan \
		CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -std=c11 -Wall -Wextra -Werror -D_GNU_SOURCE -I. -MMD -MP' \
		LDLIBS='-fsanitize=address,undefined' all

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	rm -f $(DESTDIR)$(PREFIX)/bin/save-cmd
	install -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/save-cmd
	@echo "Installed $(DESTDIR)$(PREFIX)/bin/save-cmd"
	@case ":$$PATH:" in \
		*":$(PREFIX)/bin:"*) ;; \
		*) echo "Note: $(PREFIX)/bin is not in your PATH." ;; \
	esac

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/save-cmd

clean:
	rm -rf build build-asan bin save-cmd.sh $(BIN) tests/__pycache__
