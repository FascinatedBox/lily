/**
library covlib

This library exercises less-used parts of the interpreter to help increase
coverage.
*/

#include "lily.h"

/** Begin autogen section. **/
const char *lily_covlib_table[] = {
    "\02FlatEnum\0ScopedEnum\0"
    ,"E\0FlatEnum\0"
    ,"V\0FlatOne\0"
    ,"V\0FlatTwo\0"
    ,"V\0FlatThree\0"
    ,"E\03ScopedEnum\0"
    ,"V\0ScopedOne\0"
    ,"V\0ScopedTwo\0"
    ,"V\0ScopedThree\0"
    ,"Z"
};
#define FlatEnum_OFFSET 1
#define ScopedEnum_OFFSET 5
void *lily_covlib_loader(lily_state *s, int id)
{
    switch (id) {
        default: return NULL;
    }
}
/** End autogen section. **/

/**
enum FlatEnum {
    FlatOne,
    FlatTwo,
    FlatThree
}

Flat enum test.
*/

/**
scoped enum ScopedEnum {
    ScopedOne,
    ScopedTwo,
    ScopedThree
}

Scoped enum test.
*/

