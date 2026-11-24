#include "tmux.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define LIST_KEYS_FORMAT \
	"#{key_string}\t#{key_repeat}\t#{key_note}\t#{key_command}"

static char *
dup_n(const char *s, size_t n)
{
	char *p = malloc(n + 1);

	if (p == NULL)
		return NULL;
	memcpy(p, s, n);
	p[n] = '\0';
	return p;
}

static int
list_push(binding_list_t *l, binding_t b)
{
	if (l->len == l->cap) {
		size_t     ncap = l->cap ? l->cap * 2 : 32;
		binding_t *n = realloc(l->items, ncap * sizeof *n);

		if (n == NULL)
			return -1;
		l->items = n;
		l->cap = ncap;
	}
	l->items[l->len++] = b;
	return 0;
}

int
tmux_parse_list_keys(const char *text, binding_list_t *out)
{
	const char *line = text;

	while (*line != '\0') {
		const char *eol = strchr(line, '\n');
		const char *end = eol ? eol : line + strlen(line);
		const char *field[4];
		size_t      flen[4];
		const char *p = line;
		int         nf = 0;

		field[0] = line;
		while (nf < 3) {
			const char *tab = memchr(p, '\t', (size_t)(end - p));

			if (tab == NULL)
				break;
			flen[nf] = (size_t)(tab - field[nf]);
			p = tab + 1;
			field[++nf] = p;
		}
		if (nf == 3 && flen[0] > 0) {
			binding_t b;

			flen[3] = (size_t)(end - field[3]);
			b.key = dup_n(field[0], flen[0]);
			b.repeat = flen[1] == 1 && field[1][0] == '1';
			b.note = dup_n(field[2], flen[2]);
			b.command = dup_n(field[3], flen[3]);
			if (b.key == NULL || b.note == NULL || b.command == NULL ||
			    list_push(out, b) != 0) {
				free(b.key);
				free(b.note);
				free(b.command);
				return -1;
			}
		}
		line = eol ? eol + 1 : end;
	}
	return 0;
}

/* Run argv, return its stdout (malloc'd) or NULL if it failed. */
static char *
run_capture(char *const argv[])
{
	int    fd[2];
	pid_t  pid;
	char  *buf = NULL;
	size_t len = 0, cap = 0;
	int    status;

	if (pipe(fd) != 0)
		return NULL;
	pid = fork();
	if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		return NULL;
	}
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		dup2(fd[1], STDOUT_FILENO);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDERR_FILENO);
		}
		close(fd[0]);
		close(fd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}
	close(fd[1]);
	for (;;) {
		ssize_t n;

		if (cap - len < 4096) {
			char *nb = realloc(buf, cap + 8192);

			if (nb == NULL)
				break;
			buf = nb;
			cap += 8192;
		}
		n = read(fd[0], buf + len, cap - len - 1);
		if (n <= 0)
			break;
		len += (size_t)n;
	}
	close(fd[0]);
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0 || buf == NULL) {
		free(buf);
		return NULL;
	}
	buf[len] = '\0';
	return buf;
}

int
tmux_read_prefix_bindings(binding_list_t *out)
{
	char *const argv[] = { "tmux", "list-keys", "-T", "prefix", "-F",
		LIST_KEYS_FORMAT, NULL };
	char *text = run_capture(argv);
	int   rc;

	if (text == NULL)
		return -1;
	rc = tmux_parse_list_keys(text, out);
	free(text);
	return rc;
}

char *
tmux_show_server_option(const char *name)
{
	char *const argv[] = { "tmux", "show-options", "-sv", (char *)name,
		NULL };
	char  *v = run_capture(argv);
	size_t n;

	if (v == NULL)
		return NULL;
	n = strlen(v);
	while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r'))
		v[--n] = '\0';
	if (n == 0) {
		free(v);
		return NULL;
	}
	return v;
}

/* An argument ending in ';' separates commands on the tmux command line;
 * "\;" keeps it literal (keys ";" and "M-;"). */
static void
escape_key(const char *key, char *keybuf)
{
	size_t klen = strlen(key);

	memcpy(keybuf, key, klen + 1);
	if (klen > 0 && key[klen - 1] == ';') {
		keybuf[klen - 1] = '\\';
		keybuf[klen] = ';';
		keybuf[klen + 1] = '\0';
	}
}

/* "tmux display-popup -C [-c client] ;" — closing the popup first makes the
 * rest reach the client/pane instead of the popup. Commands in one invocation
 * run in order. */
static size_t
close_popup_argv(const char *client, const char **argv)
{
	size_t n = 0;

	argv[n++] = "tmux";
	argv[n++] = "display-popup";
	argv[n++] = "-C";
	if (client != NULL) {
		argv[n++] = "-c";
		argv[n++] = client;
	}
	argv[n++] = ";";
	return n;
}

size_t
tmux_send_prefix_argv(const char *client, const char *pane,
    const char *prefix, char *keybuf, const char **argv)
{
	size_t n = close_popup_argv(client, argv);

	escape_key(prefix, keybuf);
	argv[n++] = "send-keys";
	if (pane != NULL) {
		argv[n++] = "-t";
		argv[n++] = pane;
	}
	argv[n++] = "--";
	argv[n++] = keybuf;
	argv[n] = NULL;
	return n;
}

int
tmux_is_send_prefix(const char *command)
{
	const char *rest;

	if (strncmp(command, "send-prefix", 11) != 0)
		return 0;
	rest = command + 11;
	if (*rest != '\0' && *rest != ' ')
		return 0;
	return strstr(rest, "-2") == NULL;
}

size_t
tmux_replay_argv(const char *client, const char *key, char *keybuf,
    const char **argv)
{
	size_t n = close_popup_argv(client, argv);

	escape_key(key, keybuf);
	argv[n++] = "switch-client";
	if (client != NULL) {
		argv[n++] = "-c";
		argv[n++] = client;
	}
	argv[n++] = "-T";
	argv[n++] = "prefix";
	argv[n++] = ";";
	argv[n++] = "send-keys";
	if (client != NULL) {
		argv[n++] = "-c";
		argv[n++] = client;
	}
	argv[n++] = "-K";
	argv[n++] = "--";
	argv[n++] = keybuf;
	argv[n] = NULL;
	return n;
}

int
tmux_spawn_detached(const char **argv)
{
	pid_t pid = fork();

	if (pid < 0)
		return -1;
	if (pid == 0) {
		/* Own session and no SIGHUP: closing the popup's pty must not
		 * kill us before the commands are sent. */
		int null = open("/dev/null", O_RDWR);

		setsid();
		signal(SIGHUP, SIG_IGN);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDOUT_FILENO);
			dup2(null, STDERR_FILENO);
		}
		execvp(argv[0], (char *const *)argv);
		_exit(127);
	}
	return 0;
}

const binding_t *
binding_find(const binding_list_t *l, const char *key)
{
	for (size_t i = 0; i < l->len; i++)
		if (strcmp(l->items[i].key, key) == 0)
			return &l->items[i];
	return NULL;
}

void
binding_list_free(binding_list_t *l)
{
	for (size_t i = 0; i < l->len; i++) {
		free(l->items[i].key);
		free(l->items[i].note);
		free(l->items[i].command);
	}
	free(l->items);
	l->items = NULL;
	l->len = l->cap = 0;
}
