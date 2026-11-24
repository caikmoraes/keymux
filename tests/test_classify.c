#include <stdio.h>
#include <stdlib.h>

#include "classify.h"
#include "test.h"

static binding_list_t bindings;
static entry_list_t   entries;

static void
load(const char *text)
{
	binding_list_free(&bindings);
	entry_list_free(&entries);
	tmux_parse_list_keys(text, &bindings);
	CHECK(classify_build(&bindings, &entries) == 0);
}

static const char *
desc_of(const char *label)
{
	for (size_t i = 0; i < entries.len; i++)
		if (strcmp(entries.items[i].label, label) == 0)
			return entries.items[i].desc;
	return NULL;
}

/* list-keys lines for 0-9 with default notes; slot `skip` may be replaced. */
static void
digits(char *out, size_t sz, int skip, const char *replacement)
{
	size_t len = 0;

	out[0] = '\0';
	for (int d = 0; d <= 9; d++) {
		if (d == skip) {
			if (replacement != NULL)
				len += (size_t)snprintf(out + len, sz - len, "%s\n",
				    replacement);
			continue;
		}
		len += (size_t)snprintf(out + len, sz - len,
		    "%d\t0\tSelect window %d\tselect-window -t :=%d\n", d, d, d);
	}
}

static void
test_full_digit_run(void)
{
	char text[1024];

	digits(text, sizeof text, -1, NULL);
	load(text);
	CHECK(entries.len == 1);
	CHECK_STR(entries.items[0].label, "0-9");
	CHECK_STR(entries.items[0].desc, "Select window N");
}

static void
test_rebound_key_leaves_run(void)
{
	char text[1024];

	digits(text, sizeof text, 3, "3\t0\tOpen notes\trun-shell notes");
	load(text);
	CHECK(entries.len == 2);
	CHECK_STR(entries.items[0].label, "0-2,4-9");
	CHECK_STR(desc_of("3"), "Open notes");
}

static void
test_unbound_key_leaves_run(void)
{
	char text[1024];

	digits(text, sizeof text, 9, NULL);
	load(text);
	CHECK(entries.len == 1);
	CHECK_STR(entries.items[0].label, "0-8");
}

static void
test_order_and_other_runs(void)
{
	load("c\t0\tCreate a new window\tnew-window\n"
	     "Up\t1\tSelect the pane above the active pane\tselect-pane -U\n"
	     "Down\t1\tSelect the pane below the active pane\tselect-pane -D\n"
	     "x\t0\t\tkill-pane\n"
	     "Left\t1\tSelect the pane to the left of the active pane\tselect-pane -L\n"
	     "M-1\t0\tSet the even-horizontal layout\tselect-layout even-horizontal\n"
	     "M-2\t0\tSet the even-vertical layout\tselect-layout even-vertical\n"
	     "M-4\t0\tSet the main-vertical layout\tselect-layout main-vertical\n"
	     "C-Up\t1\tResize the pane up\tresize-pane -U\n");
	CHECK(entries.len == 5);
	CHECK_STR(entries.items[0].label, "c");
	CHECK_STR(entries.items[1].label, "x");
	CHECK_STR(entries.items[1].desc, "kill-pane"); /* no note: command */
	CHECK_STR(entries.items[2].label, "↑↓←");
	CHECK_STR(entries.items[3].label, "M-[1-2,4]");
	/* A run with a single remaining key is shown as a normal line. */
	CHECK_STR(entries.items[4].label, "C-Up");
	CHECK_STR(entries.items[4].desc, "Resize the pane up");
}

static void
test_group_order(void)
{
	/* Input in tmux's list-keys order. */
	load("Tab\t0\tn\tx\n"
	     "Space\t0\tn\tx\n"
	     "!\t0\tn\tx\n"
	     "\"\t0\tn\tx\n"
	     "5\t0\tn\tx\n"
	     "B\t0\tn\tx\n"
	     "C\t0\tn\tx\n"
	     "a\t0\tn\tx\n"
	     "b\t0\tn\tx\n"
	     "z\t0\tn\tx\n"
	     "PPage\t0\tn\tx\n"
	     "M-o\t0\tn\tx\n"
	     "C-b\t0\tn\tx\n"
	     "S-Up\t0\tn\tx\n");
	{
		const char *want[] = { "a", "b", "B", "C", "z", "5", "!", "\"",
			"Tab", "Space", "PPage", "M-o", "C-b", "S-Up" };

		CHECK(entries.len == sizeof want / sizeof want[0]);
		for (size_t i = 0; i < entries.len && i < sizeof want / sizeof want[0]; i++)
			CHECK_STR(entries.items[i].label, want[i]);
	}
}

int
main(void)
{
	test_group_order();
	test_full_digit_run();
	test_rebound_key_leaves_run();
	test_unbound_key_leaves_run();
	test_order_and_other_runs();
	binding_list_free(&bindings);
	entry_list_free(&entries);
	TEST_DONE();
}
