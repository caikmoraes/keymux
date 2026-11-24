CC       ?= cc
CFLAGS   ?= -O2 -g
CFLAGS   += -std=c11 -Wall -Wextra -Wpedantic
CPPFLAGS += -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE

BIN         := bin/tmux-which-key
MODULES     := tmux classify keys render
MODULE_OBJS := $(MODULES:%=build/%.o)
HEADERS     := $(wildcard src/*.h)

.PHONY: all test clean

all: $(BIN)

$(BIN): build/main.o $(MODULE_OBJS) | bin
	$(CC) $(LDFLAGS) -o $@ $^

build/%.o: src/%.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

build/test_%: tests/test_%.c tests/test.h $(MODULE_OBJS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -o $@ $< $(MODULE_OBJS)

# make test          runs every suite
# make test T=keys   runs one suite (tests/test_keys.c)
T ?= $(MODULES)
test: $(T:%=build/test_%)
	@set -e; for t in $^; do echo "== $$t"; ./$$t; done

bin build:
	mkdir -p $@

clean:
	rm -rf build bin
