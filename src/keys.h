#ifndef WK_KEYS_H
#define WK_KEYS_H

#include <stddef.h>

/* Decode the first key in buf (bytes read from the popup's terminal) into a
 * tmux key name such as "C-n", "M-Left", "BTab" or "Escape".
 *
 * Returns the number of bytes consumed, or 0 if buf holds no complete key.
 * An unrecognised sequence is consumed and leaves name empty. A lone ESC is
 * "Escape": the caller must have waited briefly for any following bytes. */
size_t keys_decode(const unsigned char *buf, size_t len, char *name,
    size_t namesz);

#endif
