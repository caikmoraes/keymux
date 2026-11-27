CC       ?= cc
CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE

BIN         := bin/keymux
MODULES     := tmux classify keys render
MODULE_OBJS := $(MODULES:%=build/%.o)
HEADERS     := $(wildcard src/*.h)

.PHONY: all test test-live clean

all: $(BIN)

$(BIN): build/main.o $(MODULE_OBJS) | bin
	$(CC) $(LDFLAGS) -o $@ $^

build/%.o: src/%.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build/test_%: tests/test_%.c tests/test.h $(MODULE_OBJS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -o $@ $< $(MODULE_OBJS)

build/test_plugin: tests/test_plugin.sh | build
	cp $< $@
	chmod +x $@

# make test            runs every suite
# make test T=keys     runs one suite (tests/test_keys.c)
# make test T=plugin   runs the keymux.tmux suite (tests/test_plugin.sh)
T ?= $(MODULES) plugin
test: $(T:%=build/test_%)
	@set -e; for t in $^; do echo "== $$t"; ./$$t; done

# make test-live      development only: loads keymux.tmux into a throwaway tmux
#                      server (own socket in /tmp, -f /dev/null), attaches a
#                      client on a pty and presses every prefix key through the
#                      popup. Never touches the running server. Not part of
#                      `make test`, and the plugin never runs it.
build/live_tmux: tests/live_tmux.c | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

test-live: $(BIN) build/live_tmux
	./build/live_tmux .

bin build:
	mkdir -p $@

clean:
	rm -rf build bin
