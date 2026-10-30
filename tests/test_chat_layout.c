#include "test_main.h"
#include "ui/chat_layout.h"

#include <math.h>
#include <string.h>

static void chat_test_advances(float advances[128])
{
    for (unsigned i = 0; i < 128; ++i)
        advances[i] = 1.0f;
}

static float chat_test_width(const char *text, const float advances[128])
{
    float width = 0.0f;
    for (const unsigned char *p = (const unsigned char *)text; *p != '\0'; ++p)
        width += advances[*p < 128 ? *p : (unsigned char)'?'];
    return width;
}

int test_chat_layout_wrapping(void)
{
    int failures = 0;
    float advances[128];
    chat_test_advances(advances);
    ChatLayoutRow rows[CHAT_LAYOUT_MESSAGE_BYTES];
    size_t count = chat_layout_wrap_ascii("one two three", 13u, advances, 7.0f, rows, CHAT_LAYOUT_MESSAGE_BYTES);
    TEST_ASSERT(count == 2u);
    TEST_ASSERT(strcmp(rows[0].text, "one two") == 0);
    TEST_ASSERT(strcmp(rows[1].text, "three") == 0);

    count = chat_layout_wrap_ascii("abcdefghij", 10u, advances, 4.0f, rows, CHAT_LAYOUT_MESSAGE_BYTES);
    TEST_ASSERT(count == 3u);
    TEST_ASSERT(strcmp(rows[0].text, "abcd") == 0);
    TEST_ASSERT(strcmp(rows[1].text, "efgh") == 0);
    TEST_ASSERT(strcmp(rows[2].text, "ij") == 0);

    /* A proportional font cannot be wrapped by counting characters. */
    advances[(unsigned char)'W'] = 3.0f;
    count = chat_layout_wrap_ascii("WWW iii iii", 11u, advances, 9.0f, rows, CHAT_LAYOUT_MESSAGE_BYTES);
    TEST_ASSERT(count == 2u);
    TEST_ASSERT(strcmp(rows[0].text, "WWW") == 0);
    TEST_ASSERT(strcmp(rows[1].text, "iii iii") == 0);
    for (size_t i = 0; i < count; ++i)
        TEST_ASSERT(chat_test_width(rows[i].text, advances) <= 9.0f);

    char message[CHAT_LAYOUT_MESSAGE_BYTES];
    memset(message, 'i', sizeof(message) - 1u);
    message[sizeof(message) - 1u] = '\0';
    count = chat_layout_wrap_ascii(message, sizeof(message) - 1u, advances, 1000.0f, rows, CHAT_LAYOUT_MESSAGE_BYTES);
    TEST_ASSERT(count == 2u);
    TEST_ASSERT(strlen(rows[0].text) == CHAT_LAYOUT_ROW_BYTES - 1u);
    TEST_ASSERT(strlen(rows[0].text) + strlen(rows[1].text) == strlen(message));
    TEST_ASSERT(message[0] == 'i' && message[sizeof(message) - 2u] == 'i');
    return failures;
}

int test_chat_layout_tail(void)
{
    int failures = 0;
    float advances[128];
    chat_test_advances(advances);
    char tail[32];
    size_t count = chat_layout_tail_ascii("abcdefghij", 10u, advances, 5.0f, tail, sizeof(tail));
    TEST_ASSERT(count == 5u && strcmp(tail, "fghij") == 0);
    advances[(unsigned char)'W'] = 3.0f;
    count = chat_layout_tail_ascii("iiWWi", 5u, advances, 4.0f, tail, sizeof(tail));
    TEST_ASSERT(count == 2u && strcmp(tail, "Wi") == 0);
    count = chat_layout_tail_ascii("abcdefghij", 10u, advances, 100.0f, tail, 4u);
    TEST_ASSERT(count == 3u && strcmp(tail, "hij") == 0);
    count = chat_layout_tail_ascii("abcd", 4u, advances, 0.0f, tail, sizeof(tail));
    TEST_ASSERT(count == 0u && tail[0] == '\0');
    count = chat_layout_tail_ascii("abcd", 4u, advances, 100.0f, tail, 1u);
    TEST_ASSERT(count == 0u && tail[0] == '\0');
    return failures;
}

int test_chat_layout_bounds(void)
{
    int failures = 0;
    float advances[128];
    chat_test_advances(advances);
    ChatLayoutRow rows[2];
    strcpy(rows[0].text, "unchanged");
    TEST_ASSERT(chat_layout_wrap_ascii("text", 4u, advances, 0, rows, 2u) == 0u);
    TEST_ASSERT(chat_layout_wrap_ascii("text", 4u, advances, NAN, rows, 2u) == 0u);
    TEST_ASSERT(chat_layout_wrap_ascii(NULL, 4u, advances, 4, rows, 2u) == 0u);
    TEST_ASSERT(chat_layout_wrap_ascii("text", 4u, advances, 4, rows, 0u) == 0u);
    TEST_ASSERT(strcmp(rows[0].text, "unchanged") == 0);
    const char nonterminated[4] = {'a', 'b', 'c', 'd'};
    size_t count = chat_layout_wrap_ascii(nonterminated, sizeof(nonterminated), advances, 3, rows, 2u);
    TEST_ASSERT(count == 2u && strcmp(rows[0].text, "abc") == 0 && strcmp(rows[1].text, "d") == 0);
    count = chat_layout_wrap_ascii("\nword", 5u, advances, 4, rows, 2u);
    TEST_ASSERT(count == 2u && rows[0].text[0] == '\0' && strcmp(rows[1].text, "word") == 0);
    advances[(unsigned char)'W'] = 10;
    count = chat_layout_wrap_ascii("W", 1u, advances, 5, rows, 2u);
    TEST_ASSERT(count == 0u);
    advances[(unsigned char)'W'] = NAN;
    count = chat_layout_wrap_ascii("WW", 2u, advances, 16, rows, 2u);
    TEST_ASSERT(count == 2u && strcmp(rows[0].text, "W") == 0 && strcmp(rows[1].text, "W") == 0);
    return failures;
}
