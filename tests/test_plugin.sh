#!/usr/bin/env bash
# Tests keymux.tmux against a stub `tmux` that logs its arguments.
# No tmux server is started or contacted.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"

# Build the plugin copy like TPM would, without the flags of the `make test`
# that runs this suite (e.g. CFLAGS=-fsanitize=... without matching LDFLAGS).
# make passes command-line variables both in MAKEFLAGS and as environment
# variables, so clear both.
unset MAKEFLAGS MFLAGS MAKELEVEL CFLAGS CPPFLAGS LDFLAGS
trap 'rm -rf "$TMP"' EXIT

checks=0
failed=0
check() { # check <description> <command...>
	local desc="$1"
	shift
	checks=$((checks + 1))
	if ! "$@"; then
		failed=$((failed + 1))
		echo "FAIL: $desc" >&2
	fi
}

# A copy of the plugin whose path has a space and a single quote in it.
PLUG="$TMP/plugin dir's"
mkdir -p "$PLUG"
cp -R "$ROOT/Makefile" "$ROOT/keymux.tmux" "$ROOT/src" "$ROOT/scripts" "$PLUG/"
BIN="$PLUG/bin/keymux"

mkdir "$TMP/stub"
cat >"$TMP/stub/tmux" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$STUB_LOG"
case "$*" in
"show-options -gv prefix") echo C-Space ;;
"show-options -sv @keymux-client") echo /dev/pts/9 ;;
esac
if [ "$1" = bind-key ]; then
	printf '%s\0' "$@" >"$STUB_BIND"
fi
exit 0
EOF
chmod +x "$TMP/stub/tmux"
export STUB_LOG="$TMP/tmux.log" STUB_BIND="$TMP/bind.args"
export PATH="$TMP/stub:$PATH"

load() {
	: >"$STUB_LOG"
	"$PLUG/keymux.tmux"
}
logged() { grep -qxF -- "$1" "$STUB_LOG"; }
not_logged() { ! logged "$1"; }

# First load builds the binary and takes over the prefix.
load
check "first load builds the binary" test -x "$BIN"
check "real prefix is saved" logged "set-option -s @keymux-prefix C-Space"
check "tmux prefix is set to None" logged "set-option -g prefix None"

# Up to date: a second load doesn't rebuild.
find "$PLUG" -exec touch -t 202001010000 {} +
load
check "up-to-date binary is not rebuilt" test ! "$BIN" -nt "$PLUG/Makefile"

# A source change (e.g. a TPM update) rebuilds the binary.
touch "$PLUG/src/main.c"
load
check "changed sources rebuild the binary" test "$BIN" -nt "$PLUG/Makefile"

# The popup command is the last argument of the root binding.
mapfile -d '' bind_args <"$STUB_BIND"
popup_cmd="${bind_args[-1]}"
check "prefix key is bound in the root table" \
	test "${bind_args[1]} ${bind_args[2]}" = "-n C-Space"
check "popup title is drawn by tmux in the border" \
	grep -qzx -- "-T" <(printf '%s\0' "${bind_args[@]}")

# The command runs the binary despite the space and quote in its path.
mv "$BIN" "$BIN.real"
printf '#!/bin/sh\ntouch "%s"\n' "$TMP/ran" >"$BIN"
chmod +x "$BIN"
: >"$STUB_LOG"
sh -c "$popup_cmd" 2>/dev/null
check "popup command runs the binary at a quoted path" test -e "$TMP/ran"
check "no fallback when the binary succeeds" \
	not_logged "switch-client -c /dev/pts/9 -T prefix"

# If the binary is missing, the key still enters prefix mode.
rm "$BIN"
: >"$STUB_LOG"
sh -c "$popup_cmd" 2>/dev/null
check "missing binary falls back to the prefix table" \
	logged "switch-client -c /dev/pts/9 -T prefix"
mv "$BIN.real" "$BIN"

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
