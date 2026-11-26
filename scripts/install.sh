#!/usr/bin/env bash
# Put the binary at bin/keymux: download a release build for this
# OS/CPU, or build from source if that fails.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/bin/keymux"

# GitHub "owner/repo" that publishes release binaries. Not decided yet; while
# empty, the download is skipped and the binary is built from source.
REPO=""

download() {
	[ -n "$REPO" ] || return 1
	command -v curl >/dev/null 2>&1 || return 1

	local os arch tmp
	os="$(uname -s | tr '[:upper:]' '[:lower:]')"
	arch="$(uname -m)"
	tmp="$(mktemp)"
	if curl -fsSL -o "$tmp" \
		"https://github.com/$REPO/releases/latest/download/keymux-$os-$arch"; then
		mkdir -p "$ROOT/bin"
		chmod +x "$tmp"
		mv "$tmp" "$BIN"
		return 0
	fi
	rm -f "$tmp"
	return 1
}

build() {
	make -C "$ROOT"
}

download || build
