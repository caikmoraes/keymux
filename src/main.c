/* keymux: runs inside `display-popup -E`, lists the prefix bindings,
 * and replays the key the user presses into tmux's prefix table. */

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include "classify.h"
#include "keys.h"
#include "render.h"
#include "tmux.h"

#define PER_PAGE 10
#define ESC_WAIT_MS 30
#define PANE_OPTION "@keymux-pane"
#define REPLAY_OPTION "@keymux-replay" /* run by the root binding */
#define PREFIX_OPTION "@keymux-prefix" /* the real prefix; tmux's is None */

static struct termios saved_tio;
static int            have_saved_tio;

static void
term_raw(void)
{
	struct termios t;

	if (tcgetattr(STDIN_FILENO, &saved_tio) != 0)
		return;
	have_saved_tio = 1;
	t = saved_tio;
	t.c_iflag &= ~(tcflag_t)(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_oflag &= ~(tcflag_t)OPOST;
	t.c_lflag &= ~(tcflag_t)(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cflag |= CS8;
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	/* TCSANOW, not TCSAFLUSH: a key typed right after the prefix may
	 * already be waiting, and it must be read, not discarded. */
	tcsetattr(STDIN_FILENO, TCSANOW, &t);
	fputs("\x1b[?25l", stdout);
	fflush(stdout);
}

static void
term_restore(void)
{
	fputs("\x1b[?25h", stdout);
	fflush(stdout);
	if (have_saved_tio)
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_tio);
}

static void
term_size(int *cols, int *rows)
{
	struct winsize ws;

	*cols = 48;
	*rows = 12;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 &&
	    ws.ws_row > 0) {
		*cols = ws.ws_col;
		*rows = ws.ws_row;
	}
}

/* Bytes read but not decoded yet: one read can hold several keys (fast
 * typing, key repeat), and each must be handled, not just the first. */
static unsigned char pending[64];
static size_t        npending;

/* Read one key name into name. Returns 0, or -1 on EOF/error. */
static int
read_key(char *name, size_t sz)
{
	size_t used;

	if (npending == 0) {
		ssize_t n;

		do
			n = read(STDIN_FILENO, pending, sizeof pending);
		while (n < 0 && errno == EINTR);
		if (n <= 0)
			return -1;
		npending = (size_t)n;
	}

	/* A lone ESC may be the start of a sequence split across reads. */
	if (npending == 1 && pending[0] == 0x1b) {
		struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };

		if (poll(&pfd, 1, ESC_WAIT_MS) > 0) {
			ssize_t m = read(STDIN_FILENO, pending + 1,
			    sizeof pending - 1);

			if (m > 0)
				npending += (size_t)m;
		}
	}
	used = keys_decode(pending, npending, name, sz);
	if (used == 0) {
		/* An incomplete sequence: drop it rather than wait forever. */
		name[0] = '\0';
		used = npending;
	}
	memmove(pending, pending + used, npending - used);
	npending -= used;
	return 0;
}

int
main(int argc, char **argv)
{
	binding_list_t bindings = { 0 };
	entry_list_t   entries = { 0 };
	buf_t          screen = { 0 };
	char           key[32];
	size_t         page = 0, pages, per_page = PER_PAGE;
	int            cols, rows, replay = 0;

	if (argc > 1 && strcmp(argv[1], "--list") == 0) {
		/* Debug aid: print the entries without opening the UI. */
		if (tmux_read_prefix_bindings(&bindings) != 0 ||
		    classify_build(&bindings, &entries) != 0)
			return 1;
		for (size_t i = 0; i < entries.len; i++)
			printf("%s\t%s\n", entries.items[i].label,
			    entries.items[i].desc);
		return 0;
	}

	/* Raw mode before anything slow (list-keys runs tmux): until then the
	 * tty is in cooked mode, so a key typed quickly would be echoed and
	 * held for a newline. */
	term_raw();

	/* Even if reading bindings fails, still forward the key so the prefix
	 * keeps working. */
	if (tmux_read_prefix_bindings(&bindings) != 0 ||
	    classify_build(&bindings, &entries) != 0)
		entry_list_free(&entries);

	term_size(&cols, &rows);
	if (rows - 2 < PER_PAGE)
		per_page = rows > 3 ? (size_t)(rows - 2) : 1;
	pages = render_page_count(entries.len, per_page);

	for (;;) {
		screen.len = 0;
		if (render_page(&screen, &entries, page, per_page, cols) == 0) {
			fwrite(screen.data, 1, screen.len, stdout);
			fflush(stdout);
		}
		if (read_key(key, sizeof key) != 0)
			break;
		if (key[0] == '\0')
			continue;
		if (strcmp(key, "C-n") == 0) {
			if (page + 1 < pages)
				page++;
			continue;
		}
		if (strcmp(key, "C-p") == 0) {
			if (page > 0)
				page--;
			continue;
		}
		if (strcmp(key, "Escape") != 0)
			replay = 1;
		break;
	}
	term_restore();

	if (replay) {
		const binding_t *b = binding_find(&bindings, key);
		char            *prefix = NULL, *pane = NULL, *cmd;

		if (b != NULL && tmux_is_send_prefix(b->command))
			prefix = tmux_show_server_option(PREFIX_OPTION);
		if (prefix != NULL) {
			pane = tmux_show_server_option(PANE_OPTION);
			cmd = tmux_send_prefix_command(pane, prefix);
		} else
			cmd = tmux_replay_command(key);
		/* The root binding runs this once the popup has closed. */
		if (cmd != NULL)
			tmux_set_server_option(REPLAY_OPTION, cmd);
		free(cmd);
		free(prefix);
		free(pane);
	}

	buf_free(&screen);
	entry_list_free(&entries);
	binding_list_free(&bindings);
	return 0;
}
