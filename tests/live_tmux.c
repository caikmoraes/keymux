/* Live test: loads keymux.tmux into a throwaway tmux server, attaches one
 * client on a pty this program owns, and types real keys into it: the prefix
 * opens the popup, then a key is pressed in the popup.
 *
 * Development only (`make test-live`). The plugin never runs it, and
 * `make test` doesn't either.
 *
 * Isolation: the server listens on a socket in a fresh /tmp/keymux-live.XXXXXX
 * directory and starts with `-f /dev/null`. Every `tmux` this test, the plugin
 * script and the keymux binary run is a wrapper script, first on PATH, that
 * adds `-S <that socket>`. TMUX is unset and TMUX_TMPDIR points into the same
 * directory, so nothing can reach the user's own server. The server is killed
 * and the directory removed at the end, and on a timeout.
 *
 * What it checks: each key in tmux's default prefix table, plus some keys
 * that are awkward to quote, is rebound to a probe that records
 * #{client_name} and the key's index. Pressing the prefix and then the key
 * must run the probe as the attached client. A binding that runs as some
 * other tmux process would record a different client_name: that is how
 * `prefix + C` → new-session once created a session without switching to it.
 * Then a few real commands are checked: new-session switches the client,
 * send-prefix reaches the pane, and Esc and the paging keys replay nothing.
 *
 * usage: live_tmux <plugin dir>   (the dir holding keymux.tmux and bin/) */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define KEY_TIMEOUT_MS 4000
#define TOTAL_TIMEOUT_S 300

/* Shown by the keymux binary once it is in raw mode and reading keys. */
#define POPUP_READY "Esc close"
/* Drawn by tmux in the popup's top border (display-popup -T). */
#define POPUP_TITLE "keymux"

static char  dir[64];
static char  sock[96];
static char  wrapper[96];
static char  plugin[4096];
static pid_t client_pid = -1;
static int   master = -1;
static char  client_name[64];

static int checks, failed;

static void
fail(const char *fmt, ...)
{
	va_list ap;

	failed++;
	fputs("FAIL: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}

static long
now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void
nap(int ms)
{
	struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };

	while (nanosleep(&ts, &ts) != 0 && errno == EINTR)
		;
}

/* --- running tmux (always through the wrapper) --------------------------- */

/* Run the wrapper with args; stdout (trailing newlines trimmed) goes to out.
 * Returns the exit status, or -1. */
static int
tmux_argv(char *out, size_t outsz, const char **args)
{
	const char *argv[64];
	int         fd[2], status;
	size_t      n = 0, len = 0;
	pid_t       pid;

	argv[n++] = wrapper;
	while (*args != NULL && n < 63)
		argv[n++] = *args++;
	argv[n] = NULL;

	if (pipe(fd) != 0)
		return -1;
	if ((pid = fork()) < 0)
		return -1;
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		dup2(null, STDIN_FILENO);
		dup2(fd[1], STDOUT_FILENO);
		dup2(null, STDERR_FILENO);
		close(fd[0]);
		close(fd[1]);
		execv(wrapper, (char *const *)argv);
		_exit(127);
	}
	close(fd[1]);
	for (;;) {
		char    tmp[512];
		ssize_t r = read(fd[0], tmp, sizeof tmp);

		if (r < 0 && errno == EINTR)
			continue;
		if (r <= 0)
			break;
		for (ssize_t i = 0; i < r; i++)
			if (out != NULL && len + 1 < outsz)
				out[len++] = tmp[i];
	}
	close(fd[0]);
	if (out != NULL) {
		while (len > 0 && out[len - 1] == '\n')
			len--;
		out[len] = '\0';
	}
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

#define TMUX(out, sz, ...) \
	tmux_argv(out, sz, (const char *[]){ __VA_ARGS__, NULL })

static void
server_option(const char *name, char *out, size_t sz)
{
	if (TMUX(out, sz, "show-options", "-sv", name) != 0)
		out[0] = '\0';
}

/* --- cleanup ------------------------------------------------------------- */

static void
cleanup(void)
{
	char cmd[128];

	if (wrapper[0] != '\0')
		TMUX(NULL, 0, "kill-server");
	if (client_pid > 0) {
		kill(client_pid, SIGKILL);
		waitpid(client_pid, NULL, 0);
		client_pid = -1;
	}
	if (dir[0] != '\0') {
		/* dir comes from mkdtemp: only safe characters. */
		snprintf(cmd, sizeof cmd, "rm -rf '%s'", dir);
		if (system(cmd) != 0)
			fprintf(stderr, "could not remove %s\n", dir);
		dir[0] = '\0';
	}
}

static void
on_alarm(int sig)
{
	(void)sig;
	fprintf(stderr, "FAIL: timed out after %ds\n", TOTAL_TIMEOUT_S);
	cleanup();
	_exit(1);
}

/* --- the attached client's pty ------------------------------------------- */

static char   screen[1 << 16];
static size_t screen_len;

/* Read whatever the client has written; tmux blocks if nobody reads. */
static void
pump(int wait_ms)
{
	struct pollfd pfd = { master, POLLIN, 0 };

	while (poll(&pfd, 1, wait_ms) > 0) {
		char    tmp[4096];
		ssize_t r = read(master, tmp, sizeof tmp);

		if (r <= 0)
			break;
		if (screen_len + (size_t)r >= sizeof screen) {
			/* Keep the tail; markers are short. */
			size_t keep = sizeof screen / 2;

			memmove(screen, screen + screen_len - keep, keep);
			screen_len = keep;
		}
		memcpy(screen + screen_len, tmp, (size_t)r);
		screen_len += (size_t)r;
		wait_ms = 0;
	}
}

/* Read until the client has written nothing for ms milliseconds. */
static void
quiet(int ms)
{
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
		size_t before = screen_len;

		pump(ms);
		if (screen_len == before)
			return;
	}
}

/* Offset just past the first marker at or after from, or 0 if none. */
static size_t
screen_find(size_t from, const char *marker)
{
	size_t m = strlen(marker);

	if (from > screen_len)
		from = 0;
	for (size_t i = from; i + m <= screen_len; i++)
		if (memcmp(screen + i, marker, m) == 0)
			return i + m;
	return 0;
}

static int
screen_has(size_t from, const char *marker)
{
	return screen_find(from, marker) != 0;
}

static void
type(const char *bytes, size_t n)
{
	while (n > 0) {
		ssize_t w = write(master, bytes, n);

		if (w < 0 && errno == EINTR)
			continue;
		if (w <= 0)
			return;
		bytes += w;
		n -= (size_t)w;
	}
}

static int
attach_client(void)
{
	struct winsize ws = { 40, 120, 0, 0 };
	char          *slave;

	if ((master = posix_openpt(O_RDWR | O_NOCTTY)) < 0 ||
	    grantpt(master) != 0 || unlockpt(master) != 0 ||
	    (slave = ptsname(master)) == NULL)
		return -1;
	snprintf(client_name, sizeof client_name, "%s", slave);

	if ((client_pid = fork()) < 0)
		return -1;
	if (client_pid == 0) {
		int fd;

		setsid();
		if ((fd = open(slave, O_RDWR)) < 0)
			_exit(127);
#ifdef TIOCSCTTY
		ioctl(fd, TIOCSCTTY, 0);
#endif
		ioctl(fd, TIOCSWINSZ, &ws);
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > 2)
			close(fd);
		close(master);
		execl(wrapper, wrapper, "attach-session", "-t", "t", (char *)NULL);
		_exit(127);
	}
	for (long end = now_ms() + 5000; now_ms() < end;) {
		char out[256];

		pump(50);
		TMUX(out, sizeof out, "list-clients", "-F", "#{client_name}");
		if (strstr(out, client_name) != NULL)
			return 0;
	}
	return -1;
}

/* --- tmux key name → bytes a terminal sends ------------------------------ */

static int
named_key(const char *base, int mods, char *out, size_t sz)
{
	static const struct {
		const char *name;
		char        csi; /* CSI final for letter keys, 0 for ~ keys */
		int         num; /* parameter for ~ keys */
	} keys[] = {
		{ "Up", 'A', 0 },    { "Down", 'B', 0 },  { "Right", 'C', 0 },
		{ "Left", 'D', 0 },  { "Home", 'H', 0 },  { "End", 'F', 0 },
		{ "IC", 0, 2 },      { "DC", 0, 3 },      { "PPage", 0, 5 },
		{ "NPage", 0, 6 },
	};
	/* xterm modifier parameter: 1 + Shift(1) + Meta(2) + Ctrl(4). */
	int param = 1 + mods;

	for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++) {
		if (strcmp(base, keys[i].name) != 0)
			continue;
		if (keys[i].csi != 0 && param == 1)
			snprintf(out, sz, "\x1b[%c", keys[i].csi);
		else if (keys[i].csi != 0)
			snprintf(out, sz, "\x1b[1;%d%c", param, keys[i].csi);
		else if (param == 1)
			snprintf(out, sz, "\x1b[%d~", keys[i].num);
		else
			snprintf(out, sz, "\x1b[%d;%d~", keys[i].num, param);
		return 0;
	}
	if (mods != 0)
		return -1;
	if (strcmp(base, "BTab") == 0)
		snprintf(out, sz, "\x1b[Z");
	else if (strcmp(base, "Tab") == 0)
		snprintf(out, sz, "\t");
	else if (strcmp(base, "Enter") == 0)
		snprintf(out, sz, "\r");
	else if (strcmp(base, "BSpace") == 0)
		snprintf(out, sz, "\x7f");
	else if (base[0] == 'F' && base[1] >= '1' && base[1] <= '4' &&
	    base[2] == '\0')
		snprintf(out, sz, "\x1bO%c", 'P' + (base[1] - '1'));
	else
		return -1;
	return 0;
}

#define MOD_S 1
#define MOD_M 2
#define MOD_C 4

/* Bytes for a tmux key name, or -1 if this test can't type it. */
static int
key_bytes(const char *name, char *out, size_t sz)
{
	int    mods = 0;
	size_t n = 0;
	char   c;

	for (;;) {
		if (strncmp(name, "C-", 2) == 0 && name[2] != '\0')
			mods |= MOD_C;
		else if (strncmp(name, "M-", 2) == 0 && name[2] != '\0')
			mods |= MOD_M;
		else if (strncmp(name, "S-", 2) == 0 && name[2] != '\0')
			mods |= MOD_S;
		else
			break;
		name += 2;
	}
	if (strlen(name) > 1 && (unsigned char)name[0] < 0x80 &&
	    strcmp(name, "Space") != 0)
		return named_key(name, mods, out, sz);

	if (mods & MOD_S)
		return -1;
	if (mods & MOD_M)
		out[n++] = '\x1b';
	if (strcmp(name, "Space") == 0)
		c = ' ';
	else if ((unsigned char)name[0] >= 0x80) {
		/* A UTF-8 character. */
		if (mods & MOD_C || n + strlen(name) + 1 > sz)
			return -1;
		strcpy(out + n, name);
		return 0;
	} else
		c = name[0];
	if (mods & MOD_C) {
		if (c == ' ')
			c = 0;
		else if (c >= 'a' && c <= 'z')
			c = (char)(c - 'a' + 1);
		else if (c >= '@' && c <= '_')
			c = (char)(c - '@');
		else
			return -1;
	}
	out[n++] = c;
	out[n] = '\0';
	/* C-Space is a NUL byte, so callers use the returned length. */
	return (int)n;
}

static size_t
key_len(const char *name, const char *bytes, int r)
{
	(void)name;
	return r > 0 ? (size_t)r : strlen(bytes);
}

/* --- pressing keys through the popup ------------------------------------- */

/* Type a paging key and wait for the footer to show page. */
static int
handshake(const char *key, const char *page)
{
	size_t from = screen_len;

	type(key, 1);
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
		size_t at;

		pump(10);
		if ((at = screen_find(from, page)) != 0 &&
		    screen_has(at, POPUP_READY))
			return 0;
	}
	return -1;
}

/* Press the prefix and wait for the popup to be reading keys. */
static int
open_popup(void)
{
	size_t from;
	int    seen = 0;

	/* Let the last popup's output arrive first. */
	quiet(60);
	from = screen_len;
	type("\x02", 1); /* C-b, the -f /dev/null prefix */
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end && !seen;) {
		size_t title;

		pump(20);
		title = screen_find(from, POPUP_TITLE);
		seen = title != 0 && screen_has(title, POPUP_READY);
	}
	if (!seen)
		return -1;
	/* A footer on screen doesn't prove that this popup's binary is in raw
	 * mode: an older frame can still be arriving. Until raw mode, a key is
	 * echoed, and C-z or C-c would signal the binary instead. Only a
	 * binary that is reading keys redraws the page number, so page
	 * forward and back and wait for each redraw. */
	if (handshake("\x0e", "2/") != 0 || handshake("\x10", "1/") != 0)
		return -1;
	return 0;
}

/* Wait until @hit is set; returns 0 and copies it to out, or -1. */
static int
wait_hit(char *out, size_t sz)
{
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
		pump(20);
		server_option("@hit", out, sz);
		if (out[0] != '\0')
			return 0;
	}
	return -1;
}

static void
key_table(char *out, size_t sz)
{
	TMUX(out, sz, "display-message", "-p", "-c", client_name,
	    "#{client_key_table}");
}

/* After a failure the popup may still be open: close it and let things
 * settle, so one failure doesn't make every later key fail too. */
static void
recover(void)
{
	TMUX(NULL, 0, "display-popup", "-C", "-c", client_name);
	nap(300);
	pump(0);
	TMUX(NULL, 0, "switch-client", "-c", client_name, "-T", "root");
	TMUX(NULL, 0, "set-option", "-s", "@hit", "");
}

/* Press prefix, then key; return 0 once the popup is up and the key typed. */
static int
press(const char *key)
{
	char bytes[32];
	int  r = key_bytes(key, bytes, sizeof bytes);

	if (r < 0) {
		fail("this test can't type key %s; teach key_bytes()", key);
		return -1;
	}
	if (open_popup() != 0) {
		fail("popup didn't open for key %s", key);
		return -1;
	}
	type(bytes, key_len(key, bytes, r));
	return 0;
}

/* An argument ending in ';' would end the command on tmux's command line. */
static const char *
arg_key(const char *key, char *buf, size_t sz)
{
	size_t n = strlen(key);

	if (n > 0 && key[n - 1] == ';' && n + 2 <= sz) {
		memcpy(buf, key, n - 1);
		buf[n - 1] = '\\';
		buf[n] = ';';
		buf[n + 1] = '\0';
		return buf;
	}
	return key;
}

/* Keys the popup keeps for itself: nothing is replayed for them. */
static int
popup_key(const char *key)
{
	return strcmp(key, "C-n") == 0 || strcmp(key, "C-p") == 0 ||
	    strcmp(key, "Escape") == 0;
}

/* The probe for key i ran new-session -s k<i>: the attached client must be
 * on it. Then go back to session t and remove it. */
static void
check_switched(const char *key, size_t i)
{
	char want[16], got[64] = "";

	snprintf(want, sizeof want, "k%zu", i);
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
		pump(10);
		TMUX(got, sizeof got, "display-message", "-p", "-c",
		    client_name, "#{client_session}");
		if (strcmp(got, want) == 0)
			break;
	}
	if (strcmp(got, want) != 0)
		fail("key %s: new-session left the client on \"%s\", want "
		    "\"%s\"", key, got, want);
	TMUX(NULL, 0, "switch-client", "-c", client_name, "-t", "t");
	TMUX(NULL, 0, "kill-session", "-t", want);
}

/* Every key must run its binding as the attached client. */
static void
test_every_key(void)
{
	/* Not tmux defaults, but awkward to quote or decode. */
	static const char *extra[] = { "\\", "M-;", "M-\"", "M-$", "M-'",
		"M-\\", "C-M-o", "C-Space", "C-a", "Enter", "F1", "Home", "End",
		"NPage", "S-Left", "C-M-Left", "A", "é", NULL };
	char   list[8192], *keys[256];
	size_t nkeys = 0;

	if (TMUX(list, sizeof list, "list-keys", "-T", "prefix", "-F",
	    "#{key_string}") != 0) {
		fail("list-keys failed");
		return;
	}
	for (char *p = strtok(list, "\n"); p != NULL && nkeys < 230;
	    p = strtok(NULL, "\n"))
		keys[nkeys++] = p;
	for (size_t i = 0; extra[i] != NULL && nkeys < 255; i++) {
		int dup = 0;

		for (size_t j = 0; j < nkeys; j++)
			dup |= strcmp(keys[j], extra[i]) == 0;
		if (!dup)
			keys[nkeys++] = (char *)extra[i];
	}

	for (size_t i = 0; i < nkeys; i++) {
		char probe[64], kbuf[40], sname[16];

		if (popup_key(keys[i]))
			continue;
		snprintf(probe, sizeof probe, "#{client_name}:%zu", i);
		snprintf(sname, sizeof sname, "k%zu", i);
		/* new-session acts on the client running the binding (the
		 * queue item's client), not on the key's target client, so
		 * it shows a binding run by some other tmux process; the
		 * @hit format alone would still name the right client. */
		if (TMUX(NULL, 0, "bind-key", "-T", "prefix",
		    arg_key(keys[i], kbuf, sizeof kbuf), "set-option", "-sF",
		    "@hit", probe, "\\;", "new-session", "-s", sname,
		    "cat") != 0)
			fail("could not bind probe on %s", keys[i]);
	}

	for (size_t i = 0; i < nkeys; i++) {
		char want[96], got[128], table[64];

		if (popup_key(keys[i]))
			continue;
		checks++;
		TMUX(NULL, 0, "set-option", "-s", "@hit", "");
		if (press(keys[i]) != 0)
			continue;
		snprintf(want, sizeof want, "%s:%zu", client_name, i);
		if (wait_hit(got, sizeof got) != 0) {
			char replay[256];

			server_option("@keymux-replay", replay, sizeof replay);
			key_table(table, sizeof table);
			fail("key %s: binding never ran (@keymux-replay \"%s\", "
			    "key table %s)", keys[i], replay, table);
			recover();
			continue;
		}
		if (strcmp(got, want) != 0) {
			fail("key %s: binding ran as \"%s\", want \"%s\"",
			    keys[i], got, want);
			recover();
			continue;
		}
		key_table(table, sizeof table);
		if (strcmp(table, "root") != 0)
			fail("key %s: client left in key table %s", keys[i],
			    table);
		check_switched(keys[i], i);
	}
}

/* A key typed as soon as the popup exists, before keymux has drawn its page
 * (it may still be running list-keys), must not be lost. */
static void
test_fast_key(void)
{
	char   got[128], want[96];
	size_t from;
	int    ok = 0;

	TMUX(NULL, 0, "bind-key", "-T", "prefix", "x", "set-option", "-sF",
	    "@hit", "#{client_name}:fast");
	snprintf(want, sizeof want, "%s:fast", client_name);
	for (int round = 0; round < 10; round++) {
		checks++;
		TMUX(NULL, 0, "set-option", "-s", "@hit", "");
		quiet(60);
		from = screen_len;
		type("\x02", 1);
		for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
			pump(1);
			if ((ok = screen_has(from, POPUP_TITLE)))
				break;
		}
		if (!ok) {
			fail("fast key: popup didn't open");
			recover();
			continue;
		}
		type("x", 1);
		if (wait_hit(got, sizeof got) != 0 || strcmp(got, want) != 0) {
			fail("fast key (round %d): binding didn't run as %s",
			    round, client_name);
			recover();
		}
	}
}

/* The bug that started this test: new-session must switch the client. */
static void
test_new_session(void)
{
	char got[128];

	checks++;
	TMUX(NULL, 0, "bind-key", "-T", "prefix", "C", "new-session", "-s",
	    "fresh", "cat");
	if (press("C") != 0)
		return;
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
		pump(20);
		TMUX(got, sizeof got, "display-message", "-p", "-c",
		    client_name, "#{client_session}");
		if (strcmp(got, "fresh") == 0)
			break;
	}
	if (strcmp(got, "fresh") != 0)
		fail("new-session: client is on session \"%s\", want "
		    "\"fresh\"", got);
	TMUX(NULL, 0, "switch-client", "-c", client_name, "-t", "t");
	TMUX(NULL, 0, "kill-session", "-t", "fresh");
}

/* send-prefix has nothing to send with `prefix None`; keymux sends the saved
 * prefix to the pane instead. The pane runs cat, whose tty echoes ^B. */
static void
test_send_prefix(void)
{
	char got[4096] = "";

	checks++;
	TMUX(NULL, 0, "bind-key", "-T", "prefix", "C-b", "send-prefix");
	TMUX(NULL, 0, "send-keys", "-t", "t", "-l", "start:");
	if (press("C-b") != 0)
		return;
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
		pump(20);
		TMUX(got, sizeof got, "capture-pane", "-p", "-t", "t");
		if (strstr(got, "start:^B") != NULL)
			break;
	}
	if (strstr(got, "start:^B") == NULL)
		fail("send-prefix: pane shows \"%s\", want \"start:^B\"", got);
}

/* Esc closes the popup and replays nothing; C-n/C-p page without closing. */
static void
test_popup_keys(void)
{
	char   got[128], table[64];
	size_t from;

	checks++;
	TMUX(NULL, 0, "set-option", "-s", "@hit", "");
	TMUX(NULL, 0, "bind-key", "-T", "prefix", "x", "set-option", "-s",
	    "@hit", "x");
	if (open_popup() != 0) {
		fail("popup didn't open for paging");
		return;
	}
	from = screen_len;
	type("\x0e", 1); /* C-n */
	for (long end = now_ms() + KEY_TIMEOUT_MS; now_ms() < end;) {
		pump(20);
		if (screen_has(from, "2/"))
			break;
	}
	if (!screen_has(from, "2/"))
		fail("C-n didn't show page 2");
	type("\x10", 1); /* C-p */
	type("\x1b", 1); /* Esc */
	nap(500);
	pump(0);
	server_option("@hit", got, sizeof got);
	if (got[0] != '\0')
		fail("Esc/C-n/C-p replayed a key: @hit is \"%s\"", got);
	server_option("@keymux-replay", got, sizeof got);
	if (got[0] != '\0')
		fail("Esc left a replay behind: \"%s\"", got);
	key_table(table, sizeof table);
	if (strcmp(table, "root") != 0)
		fail("Esc left the client in key table %s", table);

	/* The popup is gone: the next prefix opens a new one that works. */
	if (press("x") == 0 && wait_hit(got, sizeof got) != 0)
		fail("prefix after Esc: binding never ran");
}

/* --- setup --------------------------------------------------------------- */

static int
find_tmux(char *out, size_t sz)
{
	const char *path = getenv("PATH");
	char        buf[4096];

	if (path == NULL)
		return -1;
	snprintf(buf, sizeof buf, "%s", path);
	for (char *p = strtok(buf, ":"); p != NULL; p = strtok(NULL, ":")) {
		snprintf(out, sz, "%s/tmux", p);
		if (access(out, X_OK) == 0)
			return 0;
	}
	return -1;
}

static int
setup(void)
{
	char  real[4096], path[8192], cmd[8192];
	FILE *f;

	if (find_tmux(real, sizeof real) != 0 || strchr(real, '\'') != NULL)
		return -2;
	snprintf(dir, sizeof dir, "/tmp/keymux-live.XXXXXX");
	if (mkdtemp(dir) == NULL) {
		dir[0] = '\0';
		return -1;
	}
	snprintf(sock, sizeof sock, "%s/s", dir);
	snprintf(wrapper, sizeof wrapper, "%s/bin/tmux", dir);
	snprintf(path, sizeof path, "%s/bin", dir);
	if (mkdir(path, 0700) != 0 || (f = fopen(wrapper, "w")) == NULL) {
		wrapper[0] = '\0';
		return -1;
	}
	fprintf(f, "#!/bin/sh\nexec '%s' -S '%s' \"$@\"\n", real, sock);
	fclose(f);
	chmod(wrapper, 0700);

	/* Everything started from here on finds the wrapper first. */
	snprintf(path, sizeof path, "%s/bin:%s", dir, getenv("PATH"));
	setenv("PATH", path, 1);
	unsetenv("TMUX");
	setenv("TMUX_TMPDIR", dir, 1);
	setenv("TERM", "xterm-256color", 1);
	setenv("SHELL", "/bin/sh", 1);

	if (TMUX(NULL, 0, "-f", "/dev/null", "new-session", "-d", "-s", "t",
	    "-x", "120", "-y", "40", "cat") != 0)
		return -1;
	if (attach_client() != 0)
		return -1;

	/* Load the plugin like TPM does. */
	if (strchr(plugin, '\'') != NULL)
		return -1;
	snprintf(cmd, sizeof cmd, "'%s/keymux.tmux' >/dev/null 2>&1", plugin);
	if (system(cmd) != 0)
		return -1;
	return 0;
}

int
main(int argc, char **argv)
{
	int rc;

	if (argc != 2) {
		fprintf(stderr, "usage: %s <plugin dir>\n", argv[0]);
		return 2;
	}
	if (realpath(argv[1], plugin) == NULL) {
		perror(argv[1]);
		return 2;
	}
	signal(SIGALRM, on_alarm);
	signal(SIGPIPE, SIG_IGN);
	alarm(TOTAL_TIMEOUT_S);

	if ((rc = setup()) != 0) {
		if (rc == -2) {
			puts("tmux not found; live test skipped");
			cleanup();
			return 0;
		}
		fprintf(stderr, "FAIL: setup (server, client or plugin)\n");
		cleanup();
		return 1;
	}

	test_every_key();
	test_fast_key();
	test_new_session();
	test_send_prefix();
	test_popup_keys();

	cleanup();
	printf("%d checks, %d failed\n", checks, failed);
	return failed == 0 ? 0 : 1;
}
