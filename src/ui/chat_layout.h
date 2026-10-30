#pragma once

#include <stddef.h>

/* Renderer strings are capped at 160 bytes. A stored history message can
 * include a username prefix in addition to its 160-byte message body. */
#define CHAT_LAYOUT_ROW_BYTES 161u
#define CHAT_LAYOUT_MESSAGE_BYTES 224u

typedef struct ChatLayoutRow {
    char text[CHAT_LAYOUT_ROW_BYTES];
} ChatLayoutRow;

/* Widths are the active renderer's printable ASCII advances at the intended
 * text scale, in logical canvas pixels. Word wrapping preserves characters
 * except whitespace at a wrap boundary; a long word breaks between glyphs.
 * Returned rows never exceed width or the renderer's string-byte limit.
 * max_bytes bounds reads even for nonterminated input. Invalid arguments,
 * zero capacity or a glyph wider than the panel stop without writing it. */
size_t chat_layout_wrap_ascii(const char *text, size_t max_bytes, const float advances[128], float width,
                              ChatLayoutRow *rows, size_t capacity);

/* Copy the rightmost portion fitting both width and out_capacity (including
 * NUL). Useful for an input cursor that follows a long message. */
size_t chat_layout_tail_ascii(const char *text, size_t max_bytes, const float advances[128], float width, char *out,
                              size_t out_capacity);
