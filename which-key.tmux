#!/usr/bin/env bash
# TPM entry point: make sure the binary exists, then make the prefix key open
# the which-key popup.
#
# tmux handles the `prefix` key before any root-table binding, so a root
# binding on the prefix never runs. The plugin therefore saves the real prefix
# in @which-key-prefix, sets `prefix` to None and binds that key in the root
# table itself. prefix2 is left alone.

CURRENT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$CURRENT_DIR/bin/tmux-which-key"

# Install when the binary is missing. A binary built from source (it has a
# build/ directory) is also rebuilt when its sources changed, e.g. after a TPM
# update. A downloaded binary has no build/ and is left alone.
needs_install() {
	[ ! -x "$BIN" ] && return 0
	[ -d "$CURRENT_DIR/build" ] && command -v make >/dev/null 2>&1 &&
		! make -q -C "$CURRENT_DIR" bin/tmux-which-key >/dev/null 2>&1
}

if needs_install; then
	if ! "$CURRENT_DIR/scripts/install.sh" >/dev/null 2>&1; then
		tmux display-message "tmux-which-key: could not download or build the binary"
		exit 0
	fi
fi

# On a config reload tmux.conf sets the real prefix again; otherwise it is
# already None from an earlier load and the saved value is used.
prefix="$(tmux show-options -gv prefix)"
if [ -n "$prefix" ] && [ "$prefix" != "None" ]; then
	tmux set-option -s @which-key-prefix "$prefix"
else
	prefix="$(tmux show-options -sv @which-key-prefix 2>/dev/null)"
fi
if [ -z "$prefix" ] || [ "$prefix" = "None" ]; then
	exit 0
fi

tmux set-option -g prefix None

popup_w=50
popup_h=14

# Place the popup at the bottom-right of the window, whatever the panes are.
# (-x R and -y P follow the current pane, so use numbers.) -x is the popup's
# left column. -y is the row just below the popup: the window height, plus the
# status lines when the status bar is at the top and pushes the window down
# (`status` is off, on, or a line count 2-5).
status_lines='#{?#{==:#{status},off},0,#{?#{==:#{status},on},1,#{status}}}'
popup_x="#{e|-:#{window_width},$popup_w}"
popup_y="#{e|+:#{window_height},#{?#{==:#{status-position},top},$status_lines,0}}"

# Shell command for the popup. The binary path is single-quoted for sh (each '
# becomes '\''). If the binary can't run (deleted, broken build), enter the
# prefix table anyway: `prefix` is None, so otherwise the key would do nothing.
bin_quoted="'${BIN//\'/\'\\\'\'}'"
popup_cmd="$bin_quoted || tmux switch-client -c \"\$(tmux show-options -sv @which-key-client)\" -T prefix"

# Remember which client and pane pressed the prefix (set -F expands formats in
# the binding's context) so the binary can replay the next key there.
tmux bind-key -n "$prefix" \
	set-option -sF @which-key-client '#{client_name}' '\;' \
	set-option -sF @which-key-pane '#{pane_id}' '\;' \
	display-popup -E -x "$popup_x" -y "$popup_y" -w "$popup_w" -h "$popup_h" "$popup_cmd"
