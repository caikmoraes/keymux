#include "tmux.h"

#include <errno.h>
#include <fcntl.h>
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

/* Append s to out as one word of tmux's command syntax. Single quotes are
 * used when possible: tmux expands nothing inside them. Inside double quotes
 * it expands $VAR and ~ (to $HOME, even as "~"), so a key like ~ must not be
 * double-quoted. A word containing ' (no escape exists inside single quotes)
 * is double-quoted with \, " and $ escaped; no tmux key holds both ' and ~.
 * out needs room for 2 * strlen(s) + 3. */
static char *
quote_word(char *out, const char *s)
{
	if (strchr(s, '\'') == NULL) {
		*out++ = '\'';
		out = stpcpy(out, s);
		*out++ = '\'';
		*out = '\0';
		return out;
	}
	*out++ = '"';
	for (; *s != '\0'; s++) {
		if (*s == '\\' || *s == '"' || *s == '$')
			*out++ = '\\';
		*out++ = *s;
	}
	*out++ = '"';
	*out = '\0';
	return out;
}

static char *
build_command(const char *head, const char *mid, const char *word1,
    const char *tail, const char *word2)
{
	size_t len = strlen(head) + strlen(mid) + strlen(tail) + 1;
	char  *cmd, *p;

	if (word1 != NULL)
		len += 2 * strlen(word1) + 2;
	len += 2 * strlen(word2) + 2;
	if ((cmd = malloc(len)) == NULL)
		return NULL;
	p = stpcpy(cmd, head);
	if (word1 != NULL) {
		p = stpcpy(p, mid);
		p = quote_word(p, word1);
	}
	p = stpcpy(p, tail);
	quote_word(p, word2);
	return cmd;
}

char *
tmux_replay_command(const char *key)
{
	return build_command("switch-client -T prefix ; send-keys -K", "",
	    NULL, " -- ", key);
}

char *
tmux_send_prefix_command(const char *pane, const char *prefix)
{
	return build_command("send-keys", " -t ", pane, " -- ", prefix);
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

int
tmux_set_server_option(const char *name, const char *value)
{
	char *const argv[] = { "tmux", "set-option", "-s", (char *)name,
		(char *)value, NULL };
	pid_t pid = fork();
	int   status;

	if (pid < 0)
		return -1;
	if (pid == 0) {
		int null = open("/dev/null", O_RDWR);

		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDOUT_FILENO);
			dup2(null, STDERR_FILENO);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	while (waitpid(pid, &status, 0) < 0)
		if (errno != EINTR)
			return -1;
	return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : -1;
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
