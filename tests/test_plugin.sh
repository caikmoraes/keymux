#!/usr/bin/env bash
# Tests which-key.tmux against a stub `tmux` that logs its arguments.
# No tmux server is started or contacted.
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP="$(mktemp -d)"
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
cp -R "$ROOT/Makefile" "$ROOT/which-key.tmux" "$ROOT/src" "$ROOT/scripts" "$PLUG/"
BIN="$PLUG/bin/tmux-which-key"

mkdir "$TMP/stub"
cat >"$TMP/stub/tmux" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >>"$STUB_LOG"
case "$*" in
"show-options -gv prefix") echo C-Space ;;
"show-options -sv @which-key-client") echo /dev/pts/9 ;;
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
	"$PLUG/which-key.tmux"
}
logged() { grep -qxF -- "$1" "$STUB_LOG"; }

# First load builds the binary and takes over the prefix.
load
check "first load builds the binary" test -x "$BIN"
check "real prefix is saved" logged "set-option -s @which-key-prefix C-Space"
check "tmux prefix is set to None" logged "set-option -g prefix None"

# Up to date: a second load doesn't rebuild.
find "$PLUG" -exec touch -t 202001010000 {} +
load
check "up-to-date binary is not rebuilt" test ! "$BIN" -nt "$PLUG/Makefile"

# A source change (e.g. a TPM update) rebuilds the binary.
touch "$PLUG/src/main.c"
load
check "changed sources rebuild the binary" test "$BIN" -nt "$PLUG/Makefile"

echo "$checks checks, $failed failed"
[ "$failed" -eq 0 ]
