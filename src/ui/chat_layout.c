#include "ui/chat_layout.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static float chat_glyph_width(unsigned char c, const float advances[128])
{
    float width = advances[c < 128 ? c : (unsigned char)'?'];
    /* Positive advances guarantee progress even with an incomplete table. */
    return isfinite(width) && width > 0.0f ? width : 16.0f;
}

size_t chat_layout_wrap_ascii(const char *text, size_t max_bytes, const float advances[128], float width,
                              ChatLayoutRow *rows, size_t capacity)
{
    if (text == NULL || advances == NULL || rows == NULL || capacity == 0 || !isfinite(width) || width <= 0.0f) {
        return 0;
    }
    size_t start = 0, written = 0;
    while (start < max_bytes && text[start] != '\0' && written < capacity) {
        size_t end = start, last_space = start;
        float used = 0.0f;
        while (end < max_bytes && text[end] != '\0' && text[end] != '\n' && end - start < CHAT_LAYOUT_ROW_BYTES - 1u) {
            float advance = chat_glyph_width((unsigned char)text[end], advances);
            if (used + advance > width)
                break;
            used += advance;
            if (text[end] == ' ')
                last_space = end;
            ++end;
        }
        if (end == start && text[end] != '\n')
            break;
        bool wrapped = end < max_bytes && text[end] != '\0' && text[end] != '\n';
        size_t next = end;
        if (wrapped && text[end] == ' ') {
            while (next < max_bytes && text[next] == ' ')
                ++next;
        } else if (wrapped && last_space > start) {
            end = last_space;
            next = last_space + 1u;
            while (next < max_bytes && text[next] == ' ')
                ++next;
        } else if (next < max_bytes && text[next] == '\n') {
            ++next;
        }
        size_t bytes = end - start;
        memcpy(rows[written].text, text + start, bytes);
        rows[written++].text[bytes] = '\0';
        start = next;
    }
    return written;
}

size_t chat_layout_tail_ascii(const char *text, size_t max_bytes, const float advances[128], float width, char *out,
                              size_t out_capacity)
{
    if (out == NULL || out_capacity == 0)
        return 0;
    out[0] = '\0';
    if (text == NULL || advances == NULL || !isfinite(width) || width <= 0.0f)
        return 0;
    size_t end = 0;
    while (end < max_bytes && text[end] != '\0')
        ++end;
    size_t start = end;
    float used = 0.0f;
    while (start > 0 && end - start < out_capacity - 1u) {
        float advance = chat_glyph_width((unsigned char)text[start - 1u], advances);
        if (used + advance > width)
            break;
        used += advance;
        --start;
    }
    size_t bytes = end - start;
    memcpy(out, text + start, bytes);
    out[bytes] = '\0';
    return bytes;
}
