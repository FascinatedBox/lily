#ifndef LILY_UTF8_H
# define LILY_UTF8_H

# include <stdint.h>

#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

uint32_t lily_decode_utf8(uint32_t *, uint32_t *, uint32_t);

int lily_is_valid_utf8(const char *);
int lily_is_valid_sized_utf8(const char *, uint32_t);

#endif
