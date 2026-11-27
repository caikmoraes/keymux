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

/* The tmux commands that replay key into the prefix table:
 * `switch-client -T prefix ; send-keys -K -- "<key>"`. The binary stores this
 * in @keymux-replay and the root binding runs it with `run-shell -C` after the
 * popup closes, so the binding runs as the client that pressed the prefix.
 * malloc'd; NULL on OOM. */
char *tmux_replay_command(const char *key);

/* For a key bound to plain send-prefix: with tmux's `prefix` set to None,
 * send-prefix has nothing to send, so send the saved prefix key to pane
 * (pane may be NULL) instead. malloc'd; NULL on OOM. */
char *tmux_send_prefix_command(const char *pane, const char *prefix);

/* 1 if command is plain `send-prefix` (not `send-prefix -2`). */
int tmux_is_send_prefix(const char *command);

/* `set-option -s name value`, waiting for it. Returns 0 on success. */
int tmux_set_server_option(const char *name, const char *value);

const binding_t *binding_find(const binding_list_t *l, const char *key);
void binding_list_free(binding_list_t *l);

#endif
