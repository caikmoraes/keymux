/* tmux-which-key: runs inside `display-popup -E`, lists the prefix bindings,
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
#define CLIENT_OPTION "@which-key-client"
#define PANE_OPTION "@which-key-pane"
#define PREFIX_OPTION "@which-key-prefix" /* the real prefix; tmux's is None */

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
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &t);
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

/* Read one key name into name. Returns 0, or -1 on EOF/error. */
static int
read_key(char *name, size_t sz)
{
	unsigned char buf[64];
	ssize_t       n;

	do
		n = read(STDIN_FILENO, buf, sizeof buf);
	while (n < 0 && errno == EINTR);
	if (n <= 0)
		return -1;

	/* A lone ESC may be the start of a sequence split across reads. */
	if (n == 1 && buf[0] == 0x1b) {
		struct pollfd pfd = { STDIN_FILENO, POLLIN, 0 };

		if (poll(&pfd, 1, ESC_WAIT_MS) > 0) {
			ssize_t m = read(STDIN_FILENO, buf + 1, sizeof buf - 1);

			if (m > 0)
				n += m;
		}
	}
	if (keys_decode(buf, (size_t)n, name, sz) == 0)
		name[0] = '\0';
	return 0;
}

int
main(int argc, char **argv)
{
	binding_list_t bindings = { 0 };
	entry_list_t   entries = { 0 };
	buf_t          screen = { 0 };
	char           key[32];
	char          *client;
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

	/* Even if reading bindings fails, still forward the key so the prefix
	 * keeps working. */
	if (tmux_read_prefix_bindings(&bindings) != 0 ||
	    classify_build(&bindings, &entries) != 0)
		entry_list_free(&entries);
	client = tmux_show_server_option(CLIENT_OPTION);

	term_size(&cols, &rows);
	if (rows - 2 < PER_PAGE)
		per_page = rows > 3 ? (size_t)(rows - 2) : 1;
	pages = render_page_count(entries.len, per_page);

	term_raw();
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
		const char      *argv[20];
		char            *prefix = NULL, *pane = NULL;
		char             keybuf[40];

		if (b != NULL && tmux_is_send_prefix(b->command))
			prefix = tmux_show_server_option(PREFIX_OPTION);
		if (prefix != NULL && strlen(prefix) + 2 > sizeof keybuf) {
			free(prefix);
			prefix = NULL;
		}
		if (prefix != NULL) {
			pane = tmux_show_server_option(PANE_OPTION);
			tmux_send_prefix_argv(client, pane, prefix, keybuf, argv);
		} else
			tmux_replay_argv(client, key, keybuf, argv);
		tmux_spawn_detached(argv);
		free(prefix);
		free(pane);
	}

	free(client);
	buf_free(&screen);
	entry_list_free(&entries);
	binding_list_free(&bindings);
	return 0;
}
