#ifndef WK_CLASSIFY_H
#define WK_CLASSIFY_H

#include "tmux.h"

/* One line in the popup: a single binding or a collapsed run of keys. */
typedef struct {
	char *label; /* "C-o", "0-2,4-9", "M-[1-7]", "C-↑↓←→" */
	char *desc;
} entry_t;

typedef struct {
	entry_t *items;
	size_t   len;
	size_t   cap;
} entry_list_t;

/* Turn bindings (in list-keys order) into popup entries, collapsing the
 * built-in runs, then order them: letters case-paired (a A b B ...), digits,
 * punctuation, named keys, C-/M-/S- combos; ties keep list-keys order.
 * Returns 0, or -1 on OOM. */
int classify_build(const binding_list_t *bindings, entry_list_t *out);

void entry_list_free(entry_list_t *l);

#endif
