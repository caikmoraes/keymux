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
test_replay_argv(void)
{
	const char *argv[20];
	char        kb[16];
	size_t      n;

	n = tmux_replay_argv("/dev/pts/3", "c", kb, argv);
	CHECK(argv[n] == NULL);
	CHECK_STR(argv[0], "tmux");
	CHECK_STR(argv[1], "display-popup");
	CHECK_STR(argv[2], "-C");
	CHECK_STR(argv[4], "/dev/pts/3");
	CHECK_STR(argv[n - 2], "--");
	CHECK_STR(argv[n - 1], "c");

	n = tmux_replay_argv(NULL, ";", kb, argv);
	CHECK_STR(argv[n - 1], "\\;");
	CHECK_STR(argv[3], ";");

	n = tmux_replay_argv(NULL, "M-;", kb, argv);
	CHECK_STR(argv[n - 1], "M-\\;");
}

static void
test_send_prefix(void)
{
	const char *argv[20];
	char        kb[16];
	size_t      n;

	CHECK(tmux_is_send_prefix("send-prefix"));
	CHECK(tmux_is_send_prefix("send-prefix -t {last}"));
	CHECK(!tmux_is_send_prefix("send-prefix -2"));
	CHECK(!tmux_is_send_prefix("send-prefixes"));
	CHECK(!tmux_is_send_prefix("send-keys C-Space"));

	n = tmux_send_prefix_argv("/dev/pts/3", "%5", "C-Space", kb, argv);
	CHECK(argv[n] == NULL);
	CHECK_STR(argv[1], "display-popup");
	CHECK_STR(argv[5], ";");
	CHECK_STR(argv[6], "send-keys");
	CHECK_STR(argv[7], "-t");
	CHECK_STR(argv[8], "%5");
	CHECK_STR(argv[n - 1], "C-Space");

	n = tmux_send_prefix_argv(NULL, NULL, "C-;", kb, argv);
	CHECK_STR(argv[4], "send-keys");
	CHECK_STR(argv[n - 1], "C-\\;");
}

int
main(void)
{
	test_parse();
	test_replay_argv();
	test_send_prefix();
	TEST_DONE();
}
