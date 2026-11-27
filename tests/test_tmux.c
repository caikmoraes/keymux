#include <stdlib.h>

#include "test.h"
#include "tmux.h"

static void
test_parse(void)
{
	binding_list_t l = { 0 };
	const char    *text =
	    "Space\t0\tSelect next layout\tnext-layout\n"
	    "C-Up\t1\tResize the pane up\tresize-pane -U\n"
	    "x\t0\t\trun-shell 'a\tb'\n" /* no note; tab inside command */
	    "malformed line\n"
	    "\t0\tempty key\tfoo\n";

	CHECK(tmux_parse_list_keys(text, &l) == 0);
	CHECK(l.len == 3);
	CHECK_STR(l.items[0].key, "Space");
	CHECK(l.items[0].repeat == 0);
	CHECK_STR(l.items[0].note, "Select next layout");
	CHECK_STR(l.items[0].command, "next-layout");
	CHECK(l.items[1].repeat == 1);
	CHECK_STR(l.items[2].note, "");
	CHECK_STR(l.items[2].command, "run-shell 'a\tb'");
	CHECK(binding_find(&l, "C-Up") == &l.items[1]);
	CHECK(binding_find(&l, "C-Down") == NULL);
	binding_list_free(&l);
}

static void
test_replay_command(void)
{
	char *c;

	c = tmux_replay_command("C");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- 'C'");
	free(c);

	/* Inside quotes ; is literal, so no \; is needed. */
	c = tmux_replay_command("M-;");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- 'M-;'");
	free(c);

	c = tmux_replay_command("\"");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- '\"'");
	free(c);

	c = tmux_replay_command("\\");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- '\\'");
	free(c);

	c = tmux_replay_command("$");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- '$'");
	free(c);

	/* tmux expands ~ inside double quotes, even "~". */
	c = tmux_replay_command("~");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- '~'");
	free(c);

	c = tmux_replay_command("M-'");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- \"M-'\"");
	free(c);

	c = tmux_replay_command("'");
	CHECK_STR(c, "switch-client -T prefix ; send-keys -K -- \"'\"");
	free(c);
}

static void
test_send_prefix(void)
{
	char *c;

	CHECK(tmux_is_send_prefix("send-prefix"));
	CHECK(tmux_is_send_prefix("send-prefix -t {last}"));
	CHECK(!tmux_is_send_prefix("send-prefix -2"));
	CHECK(!tmux_is_send_prefix("send-prefixes"));
	CHECK(!tmux_is_send_prefix("send-keys C-Space"));

	c = tmux_send_prefix_command("%5", "C-Space");
	CHECK_STR(c, "send-keys -t '%5' -- 'C-Space'");
	free(c);

	c = tmux_send_prefix_command(NULL, "C-;");
	CHECK_STR(c, "send-keys -- 'C-;'");
	free(c);
}

int
main(void)
{
	test_parse();
	test_replay_command();
	test_send_prefix();
	TEST_DONE();
}
