#include "keys.h"

#include <stdio.h>
#include <string.h>

/* xterm modifier parameter (1 = none) to tmux's "C-M-S-" prefix order. */
static void
mod_prefix(int param, char *out, size_t sz)
{
	int bits = param > 1 ? param - 1 : 0;

	out[0] = '\0';
	if (bits & 4)
		strncat(out, "C-", sz - strlen(out) - 1);
	if (bits & 2)
		strncat(out, "M-", sz - strlen(out) - 1);
	if (bits & 1)
		strncat(out, "S-", sz - strlen(out) - 1);
}

static size_t
utf8_len(unsigned char c)
{
	if (c < 0x80)
		return 1;
	if ((c & 0xe0) == 0xc0)
		return 2;
	if ((c & 0xf0) == 0xe0)
		return 3;
	if ((c & 0xf8) == 0xf0)
		return 4;
	return 0;
}

static void
utf8_encode(unsigned long cp, char *out)
{
	if (cp < 0x80) {
		out[0] = (char)cp;
		out[1] = '\0';
	} else if (cp < 0x800) {
		out[0] = (char)(0xc0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3f));
		out[2] = '\0';
	} else if (cp < 0x10000) {
		out[0] = (char)(0xe0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[2] = (char)(0x80 | (cp & 0x3f));
		out[3] = '\0';
	} else {
		out[0] = (char)(0xf0 | (cp >> 18));
		out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
		out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
		out[3] = (char)(0x80 | (cp & 0x3f));
		out[4] = '\0';
	}
}

/* Name of a single byte below 0x80 with no modifiers. */
static void
byte_name(unsigned char c, char *out, size_t sz)
{
	switch (c) {
	case 0x00: snprintf(out, sz, "C-Space"); return;
	case 0x09: snprintf(out, sz, "Tab"); return;
	case 0x0d: snprintf(out, sz, "Enter"); return;
	case 0x1b: snprintf(out, sz, "Escape"); return;
	case 0x1c: snprintf(out, sz, "C-\\"); return;
	case 0x1d: snprintf(out, sz, "C-]"); return;
	case 0x1e: snprintf(out, sz, "C-^"); return;
	case 0x1f: snprintf(out, sz, "C-_"); return;
	case 0x20: snprintf(out, sz, "Space"); return;
	case 0x7f: snprintf(out, sz, "BSpace"); return;
	}
	if (c < 0x20)
		snprintf(out, sz, "C-%c", 'a' + c - 1);
	else
		snprintf(out, sz, "%c", c);
}

/* Name for a codepoint reported with modifiers (CSI u, CSI 27;m;c ~). */
static void
codepoint_name(unsigned long cp, int modparam, char *out, size_t sz)
{
	char mods[8], base[8];

	if (cp < 0x80 && cp != 0x20 && cp > 0x20 && cp != 0x7f) {
		/* Shift is already part of a printable character. */
		int bits = modparam > 1 ? modparam - 1 : 0;

		modparam = (bits & ~1) + 1;
	}
	mod_prefix(modparam, mods, sizeof mods);
	if (cp < 0x80)
		byte_name((unsigned char)cp, base, sizeof base);
	else
		utf8_encode(cp, base);
	snprintf(out, sz, "%s%s", mods, base);
}

static const char *
tilde_name(int code)
{
	switch (code) {
	case 1: case 7: return "Home";
	case 2: return "IC";
	case 3: return "DC";
	case 4: case 8: return "End";
	case 5: return "PPage";
	case 6: return "NPage";
	case 11: return "F1";
	case 12: return "F2";
	case 13: return "F3";
	case 14: return "F4";
	case 15: return "F5";
	case 17: return "F6";
	case 18: return "F7";
	case 19: return "F8";
	case 20: return "F9";
	case 21: return "F10";
	case 23: return "F11";
	case 24: return "F12";
	}
	return NULL;
}

static const char *
letter_name(unsigned char final)
{
	switch (final) {
	case 'A': return "Up";
	case 'B': return "Down";
	case 'C': return "Right";
	case 'D': return "Left";
	case 'H': return "Home";
	case 'F': return "End";
	case 'P': return "F1";
	case 'Q': return "F2";
	case 'R': return "F3";
	case 'S': return "F4";
	}
	return NULL;
}

/* buf starts with ESC '['. */
static size_t
decode_csi(const unsigned char *buf, size_t len, char *name, size_t sz)
{
	unsigned long p[4] = { 0, 0, 0, 0 };
	int           np = 0, have_digit = 0;
	size_t        i;
	char          mods[8];
	const char   *base;

	for (i = 2; i < len; i++) {
		unsigned char c = buf[i];

		if (c >= '0' && c <= '9') {
			if (np < 4)
				p[np] = p[np] * 10 + (unsigned long)(c - '0');
			have_digit = 1;
		} else if (c == ';') {
			np++;
		} else if (c >= 0x40 && c <= 0x7e) {
			break;
		} else if (c < 0x20 || c > 0x3f) {
			/* Not a CSI sequence after all. */
			name[0] = '\0';
			return i;
		}
	}
	if (i == len) {
		if (len == 2) {
			snprintf(name, sz, "M-[");
			return 2;
		}
		return 0;
	}
	if (have_digit || np > 0)
		np++;

	name[0] = '\0';
	switch (buf[i]) {
	case 'Z':
		snprintf(name, sz, "BTab");
		break;
	case '~':
		if (p[0] == 27 && np >= 3) {
			codepoint_name(p[2], (int)p[1], name, sz);
		} else if ((base = tilde_name((int)p[0])) != NULL) {
			mod_prefix(np >= 2 ? (int)p[1] : 1, mods, sizeof mods);
			snprintf(name, sz, "%s%s", mods, base);
		}
		break;
	case 'u':
		codepoint_name(p[0], np >= 2 ? (int)p[1] : 1, name, sz);
		break;
	default:
		if ((base = letter_name(buf[i])) != NULL) {
			mod_prefix(np >= 2 ? (int)p[1] : 1, mods, sizeof mods);
			snprintf(name, sz, "%s%s", mods, base);
		}
		break;
	}
	return i + 1;
}

size_t
keys_decode(const unsigned char *buf, size_t len, char *name, size_t namesz)
{
	size_t n;

	if (len == 0)
		return 0;
	name[0] = '\0';

	if (buf[0] == 0x1b) {
		char   inner[32];
		size_t used;

		if (len == 1) {
			snprintf(name, namesz, "Escape");
			return 1;
		}
		if (buf[1] == '[')
			return decode_csi(buf, len, name, namesz);
		if (buf[1] == 'O') {
			const char *base;

			if (len == 2) {
				snprintf(name, namesz, "M-O");
				return 2;
			}
			if ((base = letter_name(buf[2])) != NULL)
				snprintf(name, namesz, "%s", base);
			return 3;
		}
		/* ESC followed by a key is that key with Meta. */
		used = keys_decode(buf + 1, len - 1, inner, sizeof inner);
		if (used == 0)
			return 0;
		/* tmux orders modifiers C-M-S-, so Meta goes after any C-. */
		if (strncmp(inner, "C-", 2) == 0)
			snprintf(name, namesz, "C-M-%s", inner + 2);
		else if (inner[0] != '\0')
			snprintf(name, namesz, "M-%s", inner);
		return used + 1;
	}

	if (buf[0] < 0x80) {
		byte_name(buf[0], name, namesz);
		return 1;
	}
	n = utf8_len(buf[0]);
	if (n == 0)
		return 1;
	if (len < n)
		return 0;
	if (n < namesz) {
		memcpy(name, buf, n);
		name[n] = '\0';
	}
	return n;
}
