# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A tmux plugin that adds a which-key feature, modelled on which-key.nvim. When the user presses the tmux prefix, a popup appears at the bottom-right of the window. It lists the bindings available in prefix mode, split into pages.

## Commands

```sh
make                    # build bin/tmux-which-key
make test               # build and run every suite in tests/
make test T=keys        # run one suite (tests/test_keys.c); T takes a space-separated list
make test T=plugin      # run tests/test_plugin.sh (which-key.tmux against a stub tmux)
make clean
bin/tmux-which-key --list   # print the popup's entries from the live server, no UI (read-only)
```

Tests are plain C using the `CHECK`/`CHECK_STR` macros in `tests/test.h`. Each suite links against all module objects but not `main.c`. `tests/test_plugin.sh` runs a copy of `which-key.tmux`, placed in a path with a space and a quote, against a stub `tmux` on `PATH` that logs its arguments. No tmux server is involved, so it's safe to run anywhere.

## How it fits together

- `which-key.tmux` (TPM entry) runs `scripts/install.sh` if `bin/tmux-which-key` is missing. It also runs it when the binary was built from source (a `build/` directory exists) and `make -q` says it's out of date, e.g. after a TPM update. tmux handles the `prefix` key before any root binding, so a root binding on the prefix never fires. The script therefore saves the real prefix in `@which-key-prefix`, sets `prefix None`, and binds that key in the root table to:
  `set -sF @which-key-client '#{client_name}' \; set -sF @which-key-pane '#{pane_id}' \; display-popup -E -x <x> -y <y> -w 50 -h 14 '<bin>' || tmux switch-client -c <client> -T prefix`.
  The `||` fallback keeps prefix mode working if the binary can't run, since `prefix` is `None`. The binary path is single-quoted for sh, with `'` escaped as `'\''`.
  The position is relative to the window, whatever the pane layout. `<x>` is the popup's left column, `window_width - 50`. `<y>` is the row just below the popup: `window_height`, plus the status lines when `status-position` is top. Don't use `-x R`/`-y P`, which follow the current pane, or `-y S`, which puts the popup at the top when the status bar is at the top.
  The `set -F` calls record the client and pane that pressed the prefix, because the popup command's formats aren't documented to be expanded. On a config reload, tmux.conf sets the real prefix again and the script saves it again. `prefix2` is never touched.
- `send-prefix` with `prefix None` has nothing to send. When the replayed key is bound to plain `send-prefix` (not `-2`), the binary sends `@which-key-prefix` to `@which-key-pane` with `send-keys` instead.
- `main.c`: reads bindings (`tmux.c`), turns them into entries (`classify.c`), draws with `render.c` (which shows control characters in notes and commands as `?`, so they can't send escape sequences to the terminal), reads keys in raw mode and decodes them with `keys.c`. `C-n`/`C-p` page and `Esc` exits. Any other key is replayed.
- Replay (`tmux_replay_argv` + `tmux_spawn_detached`): forks a child with `setsid` and SIGHUP ignored, which runs a single tmux invocation: `display-popup -C ; switch-client -T prefix ; send-keys -K -- <key>`. Closing the popup first, in the same command list, ensures the key reaches the client's key table and not the popup. Keys ending in `;` are passed as `\;`.
- Bindings are read with `list-keys -T prefix -F '#{key_string}\t#{key_repeat}\t#{key_note}\t#{key_command}'`. Keys come back unescaped, and only the first three tabs split fields (commands may contain tabs).
- Key names must match tmux's spelling, with modifiers in the order `C-M-S-` (e.g. `C-M-o`, `S-Up`, `BTab`, `PPage`).

## Status

All suites pass, including under `-fsanitize=address,undefined`. The owner has confirmed the popup position and key replay in their tmux. `send-prefix` emulation and the popup's behaviour in copy mode haven't been confirmed live yet. Changes to the popup are tested by the owner, not by driving a client (see "Testing against tmux safely").

Known gaps (from the first audit; not fixed yet):
- A failed replay (`tmux_spawn_detached` returning -1) is silent, and the key is lost.
- `render_width` counts one column per codepoint, so wide characters (CJK, emoji) overflow a line.
- `tmux_is_send_prefix` detects `-2` by looking for that text anywhere in the arguments.
- `@which-key-client` is one server-wide option. Two clients pressing the prefix at the same moment could replay a key into the wrong client.

## Testing against tmux safely

Running tests that drive a real tmux client from inside the owner's tmux ended a Claude session once. Don't attach test clients inside `script`/ptys or start extra tmux servers without asking the owner first. Read-only commands against the owner's server (`list-keys`, `display -p`, `show`) are fine.

## Decided

### Behaviour

- **Binding source:** the running tmux server's `prefix` table, read with `list-keys` each time the popup opens. There is no defaults cache, and the plugin never starts a separate tmux server.
- **Scope:** only bindings in the `prefix` key table. Bindings in `root`, `copy-mode` and custom tables are never shown.
- **Target tmux:** developed and tested on the current release (3.8 as of 2026-11). Other versions aren't blocked, but none has been tested, and the README says so.
- **Trigger:** the plugin takes over the prefix key by setting tmux's `prefix` to `None` and binding the saved key in the root table. The popup opens immediately. `prefix2` stays a plain prefix with no popup. The key pressed in the popup is replayed into tmux's `prefix` table (`switch-client -T prefix`, then `send-keys -K`), so every existing binding keeps working. The replay must happen after the popup has closed, so the key isn't delivered back into the popup. A which-key.nvim-style delay (show the popup only if no key arrives within N ms) is planned for later; don't build it until the owner asks.
- **Closing:** `Esc` closes the popup without running any binding, like nvim.
- **Paging:** `C-n` goes to the next page and `C-p` to the previous one. Both keys are configurable through `@which-key-*` options. Inside the popup these keys take priority over any binding on the same key. Tab, Shift-Tab, the arrow keys, PageUp, Space, `[` and `]` are tmux 3.8 prefix defaults, so they can't be used for paging.
- **Display:** each entry reads `key → description`, all in the same style. There's no default/custom/overridden distinction (no markers or colors for it).
- **Descriptions:** use the binding's note (`bind -N`), like `prefix + ?` (`list-keys -N`). A binding with no note is still shown, with its raw command as the description (as `list-keys -aN` does).
- **Order:** letters first, case-paired (`a A b B …`). Then digits (the `0-9` run), punctuation, named keys (`Tab`, `Space`, `PPage`, the plain arrow run …), and finally `C-`/`M-`/`S-` combos and their runs. Within each group, keys keep tmux's `list-keys` order.
- **Runs:** a built-in list (in the C code) of tmux 3.8's default runs, the keys that do the same action: `0`–`9` select window, `M-1`–`M-7` set layout, and the plain, `C-`, `M-` and `S-` arrow keys. Each run shows as one line (e.g. `0-9 → Select window N`). The list stores each key's default note. A key whose current note differs, or that is unbound, leaves the run: a rebound key gets its own normal line, and the run's line covers the rest (`0-2,4-9 → Select window N`).
- **Page size:** 10 entries per page, in a single column (a collapsed run counts as one entry).
- **Popup size (provisional, to be tuned while testing):** 50 columns by 14 rows, anchored at the bottom-right of the window (not the pane), above the status line when it is at the bottom. The 14 rows hold a border (2), a title line (1), the 10 entries, and a footer (1) showing the page (`1/7`) plus the `C-n`/`C-p`/`Esc` hints. Descriptions too long for the width are cut off with `…`.

### Architecture

- **Language:** C for the core (reading bindings, drawing the popup, handling keys). Shell is only glue, such as the TPM `*.tmux` entry script.
- **Install:** through TPM. The `.tmux` script downloads a prebuilt binary from GitHub releases for the user's OS and CPU. If that fails, it builds from source with `cc`.
- **Build:** a plain Makefile (`make`, `make test`, `make clean`). It needs only `cc` and `make`.
- **Terminal I/O:** raw ANSI escape codes and termios, with no library (no ncurses). The program decodes terminal key sequences into tmux key names itself (e.g. `ESC [1;3D` → `M-Left`).
- **Replay:** the C binary runs the replay tmux commands itself. There's no shell wrapper.
- **Source layout:** `src/main.c`, `src/tmux.c` (run tmux, parse `list-keys`, replay), `src/classify.c` (runs: collapsing and range labels), `src/keys.c` (terminal input → tmux key names), `src/render.c` (draw the popup). `tests/` has one test file per module.

## Undecided: ask the owner first

- The full set of `@which-key-*` options, their names, and their defaults (paging keys, page size, popup size).
- Release downloads. The repo is `caikmoraes/tmux-which-key`, but `REPO` in `scripts/install.sh` stays empty, so installs always build from source. Before setting it, releases must publish a `SHA256SUMS` file and `install.sh` must verify the download against it.
