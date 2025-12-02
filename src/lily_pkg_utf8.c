#include "lily.h"
#include "lily_utf8.h"
#define LILY_NO_EXPORT
#include "lily_pkg_utf8_bindings.h"

static uint32_t read_codepoint(const char *input, uint32_t *bytes_read)
{
    uint8_t *start = (uint8_t *)input;
    uint8_t *s = start;

    uint32_t codepoint;
    uint32_t state = 0;

    do {
        if (*s == '\0') {
            if (bytes_read)
                *bytes_read = (uint32_t)(s - start);

            return 0xFFFD;
        }

        lily_decode_utf8(&state, &codepoint, *s);
        s++;
    } while (state > UTF8_REJECT);

    if (bytes_read)
        *bytes_read = (uint32_t)(s - start);

    if (state == UTF8_REJECT)
        return 0xFFFD;

    return codepoint;
}

static uint32_t length_in_codepoints(const char *input)
{
    uint32_t length = 0;
    uint32_t bytes_read;

    for (;*input; length++) {
        read_codepoint(input, &bytes_read);
        input += bytes_read;
    }

    return length;
}

static const char *advance_to_codepoint(lily_state *s, const char *input,
        int64_t n)
{
    const char *start = input;

    if (n >= 0) {
        int64_t i = 0;
        uint32_t bytes_read;

        for (;i < n; i++) {
            if (*input == '\0')
                lily_IndexError(s, "Index %ld is out of range.", n);

            read_codepoint(input, &bytes_read);
            input += bytes_read;
        }

        return input;
    }
    else {
        while (*input)
            input++;

        for (int64_t i = 0; i > n; i--) {
            if (input == start)
                lily_IndexError(s, "Index %ld is out of range.", n);

            input--;
            while ((input > start) && ((*input & 0xC0) == 0x80))
                input--;
        }

        return input;
    }
}

static int codepoint_to_utf8(int64_t codepoint, char result[5])
{
    /* The main conditional chain assumes the codepoint is non-negative, but we
       need to reject negative codepoints as well. */
    if (codepoint < 0)
        return 0;

    if (codepoint <= 0x7f) {
        result[0] = (char) codepoint;
        result[1] = '\0';
    }
    else if (codepoint <= 0x07ff) {
        result[0] = (codepoint >> 6 & 0x1f) | 0xc0;
        result[1] = (codepoint      & 0x3f) | 0x80;
        result[2] = '\0';
    }
    else if (codepoint <= 0xffff) {
        result[0] = (codepoint >> 12 & 0x0f) | 0xe0;
        result[1] = (codepoint >> 6  & 0x3f) | 0x80;
        result[2] = (codepoint       & 0x3f) | 0x80;
        result[3] = '\0';
    }
    else if (codepoint <= 0x10ffff) {
        result[0] = (codepoint >> 18 & 0x07) | 0xf0;
        result[1] = (codepoint >> 12 & 0x3f) | 0x80;
        result[2] = (codepoint >> 6  & 0x3f) | 0x80;
        result[3] = (codepoint       & 0x3f) | 0x80;
        result[4] = '\0';
    }
    else
        return 0;

    return 1;
}

void lily_utf8__as_list(lily_state *s)
{
    const char *input = lily_arg_string_raw(s, 0);
    lily_container_val *result = lily_push_list(s, length_in_codepoints(input));
    uint32_t bytes_read;

    for (uint32_t i = 0; *input; i++) {
        lily_push_integer(s, read_codepoint(input, &bytes_read));
        lily_con_set_from_stack(s, result, i);
        input += bytes_read;
    }

    lily_return_top(s);
}

void lily_utf8__compare(lily_state *s)
{
    const char *a = lily_arg_string_raw(s, 0);
    const char *b = lily_arg_string_raw(s, 1);
    uint32_t bytes_read;

    while (1) {
        if (*a == '\0') {
            lily_return_integer(s, *b ? -1 : 0);
            return;
        }
        else if (*b == '\0') {
            lily_return_integer(s, *a ? 1 : 0);
            return;
        }

        uint32_t a_codepoint = read_codepoint(a, &bytes_read);
        a += bytes_read;

        uint32_t b_codepoint = read_codepoint(b, &bytes_read);
        b += bytes_read;

        if (a_codepoint < b_codepoint) {
            lily_return_integer(s, -1);
            return;
        }
        else if (a_codepoint > b_codepoint) {
            lily_return_integer(s, 1);
            return;
        }
    }

    lily_return_integer(s, 0);
}

void lily_utf8__each_codepoint(lily_state *s)
{
    const char *input = lily_arg_string_raw(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    uint32_t bytes_read;

    while (*input) {
        uint32_t codepoint = read_codepoint(input, &bytes_read);
        lily_push_integer(s, (int64_t)codepoint);
        lily_call(s, 1);

        input += bytes_read;
    }

    lily_return_unit(s);
}

void lily_utf8__each_codepoint_with_index(lily_state *s)
{
    const char *input = lily_arg_string_raw(s, 0);
    lily_call_prepare(s, lily_arg_function(s, 1));
    uint32_t bytes_read;

    for (int64_t index = 0; *input; index++) {
        uint32_t codepoint = read_codepoint(input, &bytes_read);
        lily_push_integer(s, (int64_t)codepoint);
        lily_push_integer(s, index);
        lily_call(s, 2);

        input += bytes_read;
    }

    lily_return_unit(s);
}

void lily_utf8__encode(lily_state *s)
{
    int64_t codepoint = lily_arg_integer(s, 0);
    char result[5];

    if (!codepoint_to_utf8(codepoint, result)) {
        lily_return_none(s);
        return;
    }

    lily_push_string(s, result);
    lily_return_some_of_top(s);
}

void lily_utf8__encode_list(lily_state *s)
{
    lily_container_val *codepoints = lily_arg_container(s, 0);
    uint32_t size = lily_con_size(codepoints);

    lily_msgbuf *msgbuf = lily_msgbuf_get(s);
    char result[5];

    for (uint32_t i = 0; i < size; i++) {
        if (!codepoint_to_utf8(lily_as_integer(lily_con_get(codepoints, i)),
                               result)) {
            lily_return_none(s);
            return;
        }

        lily_mb_add(msgbuf, result);
    }

    lily_push_string(s, lily_mb_raw(msgbuf));
    lily_return_some_of_top(s);
}

void lily_utf8__get(lily_state *s)
{
    const char *input = lily_arg_string_raw(s, 0);
    int64_t index = lily_arg_integer(s, 1);

    const char *start = advance_to_codepoint(s, input, index);
    if (*start == '\0')
        lily_IndexError(s, "Index %ld is out of range.", index);

    lily_return_integer(s, read_codepoint(start, NULL));
}

void lily_utf8__length(lily_state *s)
{
    lily_return_integer(s, length_in_codepoints(lily_arg_string_raw(s, 0)));
}

void lily_utf8__slice(lily_state *s)
{
    const char *input = lily_arg_string_raw(s, 0);
    uint32_t length = length_in_codepoints(input);

    int64_t start = lily_optional_integer(s, 1, 0);
    int64_t stop = lily_optional_integer(s, 2, length);

    if (start < 0)
        start += length;

    if (stop < 0)
        stop += length;

    if (start < 0 || stop < 0 || start > length || stop > length
        || start > stop) {
        lily_return_string(s, "");
        return;
    }

    const char *start_str = advance_to_codepoint(s, input, start);
    const char *stop_str = advance_to_codepoint(s, start_str, stop - start);

    lily_push_string_sized(s, start_str, (uint32_t)(stop_str - start_str));
    lily_return_top(s);
}

LILY_DECLARE_UTF8_CALL_TABLE
