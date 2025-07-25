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
