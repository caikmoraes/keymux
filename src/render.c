#include "render.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define KEY_WIDTH_MAX 12
#define ARROW " → "
#define ARROW_WIDTH 3
#define ELLIPSIS "…"

#define SGR_RESET "\x1b[0m"
#define SGR_BOLD "\x1b[1m"
#define SGR_DIM "\x1b[2m"

static int
buf_add(buf_t *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		size_t ncap = b->cap ? b->cap : 1024;
		char  *nd;

		while (b->len + n + 1 > ncap)
			ncap *= 2;
		nd = realloc(b->data, ncap);
		if (nd == NULL)
			return -1;
		b->data = nd;
		b->cap = ncap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

static int
buf_str(buf_t *b, const char *s)
{
	return buf_add(b, s, strlen(s));
}

static int
buf_spaces(buf_t *b, size_t n)
{
	while (n-- > 0)
		if (buf_add(b, " ", 1) != 0)
			return -1;
	return 0;
}

size_t
render_width(const char *s)
{
	size_t w = 0;

	for (; *s != '\0'; s++)
		if (((unsigned char)*s & 0xc0) != 0x80)
			w++;
	return w;
}

/* Byte length of the first `cols` codepoints of s. */
static size_t
prefix_bytes(const char *s, size_t cols)
{
	size_t i = 0, w = 0;

	while (s[i] != '\0') {
		if (((unsigned char)s[i] & 0xc0) != 0x80) {
			if (w == cols)
				break;
			w++;
		}
		i++;
	}
	return i;
}

/* Copy of s with control characters (C0, DEL, and C1 as UTF-8 U+0080-U+009F)
 * replaced by '?', so text from binding notes and commands can't send escape
 * sequences to the terminal. */
static char *
sanitize(const char *s)
{
	const unsigned char *p = (const unsigned char *)s;
	char                *out = malloc(strlen(s) + 1), *o = out;

	if (out == NULL)
		return NULL;
	for (; *p != '\0'; p++) {
		if (*p < 0x20 || *p == 0x7f) {
			*o++ = '?';
		} else if (*p == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f) {
			*o++ = '?';
			p++;
		} else {
			*o++ = (char)*p;
		}
	}
	*o = '\0';
	return out;
}

/* Append s, sanitized and cut to at most `cols` columns, ending in "…" when
 * cut. */
static int
buf_fit(buf_t *b, const char *s, size_t cols)
{
	char *clean = sanitize(s);
	int   rc = 0;

	if (clean == NULL)
		return -1;
	if (render_width(clean) <= cols)
		rc = buf_str(b, clean);
	else if (cols > 0) {
		rc = buf_add(b, clean, prefix_bytes(clean, cols - 1));
		if (rc == 0)
			rc = buf_str(b, ELLIPSIS);
	}
	free(clean);
	return rc;
}

size_t
render_page_count(size_t n, size_t per_page)
{
	if (per_page == 0 || n == 0)
		return 1;
	return (n + per_page - 1) / per_page;
}

int
render_page(buf_t *out, const entry_list_t *e, size_t page, size_t per_page,
    int cols)
{
	size_t pages = render_page_count(e->len, per_page);
	size_t first, last, keyw = 0, width = cols > 0 ? (size_t)cols : 1;
	char   footer[96];

	if (page >= pages)
		page = pages - 1;
	first = page * per_page;
	last = first + per_page;
	if (last > e->len)
		last = e->len;

	for (size_t i = first; i < last; i++) {
		size_t w = render_width(e->items[i].label);

		if (w > keyw)
			keyw = w;
	}
	if (keyw > KEY_WIDTH_MAX)
		keyw = KEY_WIDTH_MAX;

	if (buf_str(out, "\x1b[H\x1b[2J") != 0)
		return -1;

	/* The title is drawn by tmux in the popup's top border (display-popup
	 * -T), so the first line is the first entry. */
	for (size_t i = first; i < last; i++) {
		const entry_t *x = &e->items[i];
		size_t         used = 1 + keyw + ARROW_WIDTH;
		size_t         lw = render_width(x->label);

		if (buf_str(out, i == first ? " " SGR_BOLD : "\r\n " SGR_BOLD) != 0 ||
		    buf_fit(out, x->label, keyw) != 0 ||
		    buf_str(out, SGR_RESET) != 0 ||
		    buf_spaces(out, lw < keyw ? keyw - lw : 0) != 0 ||
		    buf_str(out, ARROW) != 0 ||
		    buf_fit(out, x->desc, width > used ? width - used : 0) != 0)
			return -1;
	}

	snprintf(footer, sizeof footer, "%zu/%zu  C-n next  C-p prev  Esc close",
	    page + 1, pages);
	if (buf_str(out, last > first ? "\r\n " SGR_DIM : " " SGR_DIM) != 0 ||
	    buf_fit(out, footer, width > 1 ? width - 1 : 0) != 0 ||
	    buf_str(out, SGR_RESET) != 0)
		return -1;
	return 0;
}

void
buf_free(buf_t *b)
{
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}
