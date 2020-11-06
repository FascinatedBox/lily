#ifndef LILY_UTF8_H
# define LILY_UTF8_H

# include <stdint.h>

int lily_is_valid_utf8(const char *);
int lily_is_valid_sized_utf8(const char *, uint32_t);

#endif
