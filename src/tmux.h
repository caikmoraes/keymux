#ifndef WK_TMUX_H
#define WK_TMUX_H

#include <stddef.h>

/* One line of `list-keys -T prefix`. */
typedef struct {
	char *key;     /* tmux key name, e.g. "M-Left" */
	int   repeat;  /* bound with -r */
	char *note;    /* "" when the binding has no note */
	char *command;
} binding_t;

typedef struct {
	binding_t *items;
	size_t     len;
	size_t     cap;
} binding_list_t;

/* Parse output of
 *   list-keys -T prefix -F '#{key_string}\t#{key_repeat}\t#{key_note}\t#{key_command}'
 * appending to out. Malformed lines are skipped. Returns 0, or -1 on OOM. */
int tmux_parse_list_keys(const char *text, binding_list_t *out);

/* Run list-keys on the current server and parse it. Returns 0 or -1. */
int tmux_read_prefix_bindings(binding_list_t *out);

/* Value of a server option (`show -sv`), malloc'd; NULL if unset or empty. */
char *tmux_show_server_option(const char *name);

/* Fill argv (NULL-terminated) with the tmux command that closes the popup,
 * enters the prefix table and replays key on client (client may be NULL).
 * keybuf receives the escaped key argument and must outlive argv.
 * Returns argc. argv needs room for 20 entries, keybuf for strlen(key)+2. */
size_t tmux_replay_argv(const char *client, const char *key, char *keybuf,
    const char **argv);

/* Like tmux_replay_argv, but for a key bound to send-prefix: with tmux's
 * `prefix` set to None, send-prefix has nothing to send, so send the saved
 * prefix key straight to pane instead (pane may be NULL). */
size_t tmux_send_prefix_argv(const char *client, const char *pane,
    const char *prefix, char *keybuf, const char **argv);

/* 1 if command is plain `send-prefix` (not `send-prefix -2`). */
int tmux_is_send_prefix(const char *command);

/* Run argv in a detached child, so it runs after this process (and with it
 * the popup) is gone. Returns 0 if the child was started. */
int tmux_spawn_detached(const char **argv);

const binding_t *binding_find(const binding_list_t *l, const char *key);
void binding_list_free(binding_list_t *l);

#endif
