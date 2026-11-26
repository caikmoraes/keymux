# keymux

A [which-key.nvim](https://github.com/folke/which-key.nvim)-style popup for tmux.
When you press your prefix, a popup opens at the bottom-right of the window
listing every prefix binding. Press a key and it runs as usual.

```
┌ keymux ────────────────────────────────────────┐
│ c   → Create a new window                      │
│ d   → Detach the current client                │
│ n   → Select the next window                   │
│ o   → Select the next pane                     │
│ ...                                            │
│ 1/7  C-n next  C-p prev  Esc close             │
└────────────────────────────────────────────────┘
```

## Requirements

- tmux. The plugin has only been tested with tmux 3.8. Other versions may
  work, but they haven't been tried.
- `cc` and `make`. The plugin is a small C program, built from source the
  first time it loads.

## Install

### With TPM

Add the plugin to `tmux.conf`:

```tmux
set -g @plugin 'caikmoraes/keymux'
```

TPM loads plugins at its `run '…/tpm'` line, so set your prefix before that
line (it usually is). Then press `prefix` + `I` to install. The first load builds the binary, which
takes a second or two. After a TPM update (`prefix` + `U`), the binary is
rebuilt automatically the next time the plugin loads.

### Manually

```sh
git clone https://github.com/caikmoraes/keymux ~/.tmux/plugins/keymux
```

and add this to `tmux.conf`, after the line that sets your prefix:

```tmux
run-shell ~/.tmux/plugins/keymux/keymux.tmux
```

## Usage

| Key in the popup | Action |
|---|---|
| any bound key | Closes the popup and runs that prefix binding |
| `C-n` / `C-p` | Next / previous page |
| `Esc` | Closes the popup without running anything |

The list shows the `prefix` table only, 10 entries per page:

- **Descriptions** come from each binding's note (`bind -N "..."`), the same text
  `prefix` + `?` shows. A binding without a note shows its command instead.
- **Order:** letters first, with lower and upper case together (`a A b B …`).
  Then digits, punctuation, named keys (`Tab`, `PPage`, arrows), and finally
  `C-`/`M-`/`S-` combinations.
- **Collapsed runs:** tmux's default runs of similar keys show as one line, for
  example `0-9 → Select window N` or `C-↑↓←→ → Resize the pane`. A key you
  rebind leaves the run and gets its own line.

## How it works

tmux always handles the prefix key itself, before any other binding, so a
plugin can't attach a popup to it directly. When it loads, this plugin:

1. saves your prefix in the `@keymux-prefix` option;
2. sets tmux's `prefix` option to `None`;
3. binds your prefix key in the root table to open the popup.

When you press a key in the popup, the plugin closes the popup, switches the
client into the `prefix` table, and sends the key, so existing bindings work
unchanged. Because tmux's `prefix` is `None`, a key bound to plain
`send-prefix` (usually the prefix itself, pressed twice) is emulated by
sending the saved prefix key to the pane.

`prefix2` is not touched. If you have one, it stays a plain prefix with no
popup, which is a handy fallback.

## Uninstall

Remove the plugin line from `tmux.conf` (with TPM, then press `prefix` +
`alt` + `u`). Then restart the tmux server, or restore the prefix in the
running server:

```sh
tmux set -g prefix C-b        # your usual prefix
tmux unbind -n C-b            # the root binding the plugin added
```

## Development

```sh
make                  # build bin/keymux
make test             # run every test suite
make test T=keys      # run one suite (tmux, classify, keys, render, plugin)
bin/keymux --list   # print the entries the popup would show
```

## License

[MIT](LICENSE)
