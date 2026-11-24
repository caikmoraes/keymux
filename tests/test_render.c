#include <stdio.h>
#include <stdlib.h>

#include "render.h"
#include "test.h"

/* Strip escape sequences and split the screen into lines. */
static int
plain_lines(const char *s, char lines[][128], int max)
{
	int n = 0;
	size_t col = 0;

	lines[0][0] = '\0';
	while (*s != '\0') {
		if (*s == '\x1b') {
			s++;
			if (*s == '[') {
				s++;
				while (*s != '\0' && !(*s >= 0x40 && *s <= 0x7e))
					s++;
			}
			if (*s != '\0')
				s++;
			continue;
		}
		if (*s == '\r') {
			s++;
			continue;
		}
		if (*s == '\n') {
			if (++n == max)
				return n;
			col = 0;
			lines[n][0] = '\0';
			s++;
			continue;
		}
		if (col < 127) {
			lines[n][col++] = *s;
			lines[n][col] = '\0';
		}
		s++;
	}
	return n + 1;
}

static void
add(entry_list_t *l, const char *label, const char *desc)
{
	if (l->len == l->cap) {
		l->cap = l->cap ? l->cap * 2 : 16;
		l->items = realloc(l->items, l->cap * sizeof *l->items);
	}
	l->items[l->len].label = strdup(label);
	l->items[l->len].desc = strdup(desc);
	l->len++;
}

int
main(void)
{
	entry_list_t e = { 0 };
	buf_t        b = { 0 };
	char         lines[16][128];
	int          n;

	CHECK(render_page_count(0, 10) == 1);
	CHECK(render_page_count(10, 10) == 1);
	CHECK(render_page_count(66, 10) == 7);
	CHECK(render_width("C-↑↓←→") == 6);

	for (int i = 0; i < 12; i++) {
		char label[8];

		snprintf(label, sizeof label, "k%d", i);
		add(&e, label, "Something");
	}
	add(&e, "M-Right", "A description that is much too long to fit in the popup");

	/* Page 1: title, 10 entries, footer. Key column padded to widest. */
	CHECK(render_page(&b, &e, 0, 10, 48) == 0);
	n = plain_lines(b.data, lines, 16);
	CHECK(n == 12);
	CHECK_STR(lines[0], " which-key");
	CHECK_STR(lines[1], " k0 → Something");
	CHECK_STR(lines[11], " 1/2  C-n next  C-p prev  Esc close");

	/* Page 2: three entries; long description cut to 48 columns. */
	b.len = 0;
	CHECK(render_page(&b, &e, 1, 10, 48) == 0);
	n = plain_lines(b.data, lines, 16);
	CHECK(n == 5);
	CHECK_STR(lines[1], " k10     → Something");
	CHECK(render_width(lines[3]) == 48);
	CHECK(strstr(lines[3], "…") != NULL);
	CHECK_STR(lines[4], " 2/2  C-n next  C-p prev  Esc close");

	/* Out-of-range page clamps to the last one. */
	b.len = 0;
	CHECK(render_page(&b, &e, 9, 10, 48) == 0);
	CHECK(strstr(b.data, "2/2") != NULL);

	/* Control characters in notes/commands (C0, DEL, 8-bit C1) are shown as
	 * '?' and never reach the terminal. Split literals stop \x from eating
	 * the following hex-looking letters. */
	{
		entry_list_t evil = { 0 };

		add(&evil, "x", "A\x1b]52;c;aGVsbG8=\x07" "B\xc2\x9b" "C\x7f" "D");
		b.len = 0;
		CHECK(render_page(&b, &evil, 0, 10, 48) == 0);
		CHECK(strstr(b.data, "\x1b]") == NULL);
		CHECK(strstr(b.data, "\x07") == NULL);
		CHECK(strstr(b.data, "\xc2\x9b") == NULL);
		CHECK(strstr(b.data, "A?]52;c;aGVsbG8=?B?C?D") != NULL);
		entry_list_free(&evil);
	}

	buf_free(&b);
	entry_list_free(&e);
	TEST_DONE();
}
