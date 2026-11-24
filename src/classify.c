#include "classify.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RUN_MAX 10

typedef enum {
	LABEL_DIGITS, /* "0-9", "0-2,4-9" */
	LABEL_BRACKET_DIGITS, /* "M-[1-7]" */
	LABEL_GLYPHS, /* "C-↑↓←→" */
} label_kind_t;

typedef struct {
	const char *key;
	const char *note; /* tmux 3.8 default note */
	const char *glyph; /* LABEL_DIGITS/BRACKET: the digit; GLYPHS: arrow */
} run_key_t;

typedef struct {
	const char  *prefix;
	label_kind_t kind;
	const char  *desc;
	run_key_t    keys[RUN_MAX];
	size_t       n;
} run_t;

/* tmux 3.8 default runs. A key collapses only while its note is unchanged. */
static const run_t runs[] = {
	{ "", LABEL_DIGITS, "Select window N", {
		{ "0", "Select window 0", "0" }, { "1", "Select window 1", "1" },
		{ "2", "Select window 2", "2" }, { "3", "Select window 3", "3" },
		{ "4", "Select window 4", "4" }, { "5", "Select window 5", "5" },
		{ "6", "Select window 6", "6" }, { "7", "Select window 7", "7" },
		{ "8", "Select window 8", "8" }, { "9", "Select window 9", "9" },
	}, 10 },
	{ "M-", LABEL_BRACKET_DIGITS, "Select a preset layout", {
		{ "M-1", "Set the even-horizontal layout", "1" },
		{ "M-2", "Set the even-vertical layout", "2" },
		{ "M-3", "Set the main-horizontal layout", "3" },
		{ "M-4", "Set the main-vertical layout", "4" },
		{ "M-5", "Select the tiled layout", "5" },
		{ "M-6", "Set the main-horizontal-mirrored layout", "6" },
		{ "M-7", "Set the main-vertical-mirrored layout", "7" },
	}, 7 },
	{ "", LABEL_GLYPHS, "Select the pane in that direction", {
		{ "Up", "Select the pane above the active pane", "↑" },
		{ "Down", "Select the pane below the active pane", "↓" },
		{ "Left", "Select the pane to the left of the active pane", "←" },
		{ "Right", "Select the pane to the right of the active pane", "→" },
	}, 4 },
	{ "C-", LABEL_GLYPHS, "Resize the pane", {
		{ "C-Up", "Resize the pane up", "↑" },
		{ "C-Down", "Resize the pane down", "↓" },
		{ "C-Left", "Resize the pane left", "←" },
		{ "C-Right", "Resize the pane right", "→" },
	}, 4 },
	{ "M-", LABEL_GLYPHS, "Resize the pane by 5", {
		{ "M-Up", "Resize the pane up by 5", "↑" },
		{ "M-Down", "Resize the pane down by 5", "↓" },
		{ "M-Left", "Resize the pane left by 5", "←" },
		{ "M-Right", "Resize the pane right by 5", "→" },
	}, 4 },
	{ "S-", LABEL_GLYPHS, "Move the visible part of the window", {
		{ "S-Up", "Move the visible part of the window up", "↑" },
		{ "S-Down", "Move the visible part of the window down", "↓" },
		{ "S-Left", "Move the visible part of the window left", "←" },
		{ "S-Right", "Move the visible part of the window right", "→" },
	}, 4 },
};
#define NRUNS (sizeof runs / sizeof runs[0])

static int
entry_push(entry_list_t *l, const char *label, const char *desc)
{
	entry_t e;

	if (l->len == l->cap) {
		size_t   ncap = l->cap ? l->cap * 2 : 32;
		entry_t *n = realloc(l->items, ncap * sizeof *n);

		if (n == NULL)
			return -1;
		l->items = n;
		l->cap = ncap;
	}
	e.label = strdup(label);
	e.desc = strdup(desc);
	if (e.label == NULL || e.desc == NULL) {
		free(e.label);
		free(e.desc);
		return -1;
	}
	l->items[l->len++] = e;
	return 0;
}

/* Label for the members of run r that are in (present[i] != 0). */
static void
run_label(const run_t *r, const int *present, char *out, size_t sz)
{
	size_t len;

	snprintf(out, sz, "%s%s", r->prefix,
	    r->kind == LABEL_BRACKET_DIGITS ? "[" : "");
	len = strlen(out);

	if (r->kind == LABEL_GLYPHS) {
		for (size_t i = 0; i < r->n; i++)
			if (present[i])
				len += (size_t)snprintf(out + len, sz - len, "%s",
				    r->keys[i].glyph);
		return;
	}

	/* Digits: consecutive present members become "a-b", joined by ",". */
	for (size_t i = 0, first = 1; i < r->n; i++) {
		size_t j = i;

		if (!present[i])
			continue;
		while (j + 1 < r->n && present[j + 1])
			j++;
		len += (size_t)snprintf(out + len, sz - len, "%s%s", first ? "" : ",",
		    r->keys[i].glyph);
		if (j > i)
			len += (size_t)snprintf(out + len, sz - len, "-%s",
			    r->keys[j].glyph);
		first = 0;
		i = j;
	}
	if (r->kind == LABEL_BRACKET_DIGITS)
		snprintf(out + len, sz - len, "]");
}

static int classify_collapse(const binding_list_t *b, entry_list_t *out);

/* Display groups, in order: letters (case-paired), digits, punctuation, named
 * keys (Tab, PPage, arrow run, ...), then C-/M-/S- combos. */
enum { GROUP_LETTER, GROUP_DIGIT, GROUP_PUNCT, GROUP_NAMED, GROUP_COMBO };

static int
entry_group(const char *label)
{
	unsigned char c = (unsigned char)label[0];

	if (label[1] == '\0' && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
		return GROUP_LETTER;
	if (c >= '0' && c <= '9')
		return GROUP_DIGIT; /* "5" or a digit run like "0-2,4-9" */
	if ((c == 'C' || c == 'M' || c == 'S') && label[1] == '-' &&
	    label[2] != '\0')
		return GROUP_COMBO;
	if (label[1] == '\0')
		return GROUP_PUNCT;
	return GROUP_NAMED;
}

/* Negative if a sorts before b. Ties keep tmux's order (stable sort). */
static int
entry_cmp(const entry_t *a, const entry_t *b)
{
	int ga = entry_group(a->label), gb = entry_group(b->label);

	if (ga != gb)
		return ga - gb;
	if (ga == GROUP_LETTER) {
		int la = a->label[0] | 0x20, lb = b->label[0] | 0x20;

		if (la != lb)
			return la - lb;
		/* Lowercase before its uppercase. */
		return (a->label[0] < 'a') - (b->label[0] < 'a');
	}
	return 0;
}

static void
sort_entries(entry_list_t *l)
{
	/* Insertion sort: stable, and lists are ~100 entries. */
	for (size_t i = 1; i < l->len; i++) {
		entry_t x = l->items[i];
		size_t  j = i;

		while (j > 0 && entry_cmp(&x, &l->items[j - 1]) < 0) {
			l->items[j] = l->items[j - 1];
			j--;
		}
		l->items[j] = x;
	}
}

int
classify_build(const binding_list_t *b, entry_list_t *out)
{
	if (classify_collapse(b, out) != 0)
		return -1;
	sort_entries(out);
	return 0;
}

static int
classify_collapse(const binding_list_t *b, entry_list_t *out)
{
	int present[NRUNS][RUN_MAX];
	int active[NRUNS], emitted[NRUNS];

	/* A run collapses the members still bound with their default note,
	 * and only if at least two remain. */
	for (size_t r = 0; r < NRUNS; r++) {
		size_t count = 0;

		for (size_t i = 0; i < runs[r].n; i++) {
			const binding_t *x = binding_find(b, runs[r].keys[i].key);

			present[r][i] = x != NULL &&
			    strcmp(x->note, runs[r].keys[i].note) == 0;
			count += (size_t)present[r][i];
		}
		active[r] = count >= 2;
		emitted[r] = 0;
	}

	for (size_t k = 0; k < b->len; k++) {
		const binding_t *x = &b->items[k];
		int              in_run = 0;

		for (size_t r = 0; r < NRUNS && !in_run; r++) {
			if (!active[r])
				continue;
			for (size_t i = 0; i < runs[r].n; i++) {
				if (!present[r][i] ||
				    strcmp(runs[r].keys[i].key, x->key) != 0)
					continue;
				in_run = 1;
				if (!emitted[r]) {
					char label[64];

					run_label(&runs[r], present[r], label,
					    sizeof label);
					if (entry_push(out, label, runs[r].desc) != 0)
						return -1;
					emitted[r] = 1;
				}
				break;
			}
		}
		if (in_run)
			continue;
		if (entry_push(out, x->key,
		    x->note[0] != '\0' ? x->note : x->command) != 0)
			return -1;
	}
	return 0;
}

void
entry_list_free(entry_list_t *l)
{
	for (size_t i = 0; i < l->len; i++) {
		free(l->items[i].label);
		free(l->items[i].desc);
	}
	free(l->items);
	l->items = NULL;
	l->len = l->cap = 0;
}
