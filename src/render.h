#ifndef WK_RENDER_H
#define WK_RENDER_H

#include "classify.h"

typedef struct {
	char  *data;
	size_t len;
	size_t cap;
} buf_t;

/* Number of pages for n entries (at least 1). */
size_t render_page_count(size_t n, size_t per_page);

/* Width in terminal columns, counting one per UTF-8 codepoint. */
size_t render_width(const char *s);

/* Write a full redraw of page `page` for a screen `cols` wide into out:
 * up to per_page entries and a footer (the title is in tmux's popup border). Control characters in
 * labels and descriptions are shown as '?'. Returns 0 or -1. */
int render_page(buf_t *out, const entry_list_t *entries, size_t page,
    size_t per_page, int cols);

void buf_free(buf_t *b);

#endif
