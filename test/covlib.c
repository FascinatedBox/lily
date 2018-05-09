/**
library covlib

This library exercises less-used parts of the interpreter to help increase
coverage.
*/

#include "lily.h"

/** Begin autogen section. **/
const char *lily_covlib_info_table[] = {
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
lily_call_entry_func lily_covlib_call_table[] = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
};
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

