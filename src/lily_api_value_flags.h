#ifndef LILY_API_VALUE_FLAGS_H
# define LILY_API_VALUE_FLAGS_H

#define VAL_IS_BOOLEAN          0x00001
#define VAL_IS_INTEGER          0x00002
#define VAL_IS_DOUBLE           0x00004
#define VAL_IS_STRING           0x00008
#define VAL_IS_BYTESTRING       0x00010
#define VAL_IS_FUNCTION         0x00020
#define VAL_IS_DYNAMIC          0x00040
#define VAL_IS_LIST             0x00080
#define VAL_IS_HASH             0x00100
#define VAL_IS_TUPLE            0x00200
#define VAL_IS_INSTANCE         0x00400
#define VAL_IS_ENUM             0x00800
#define VAL_IS_FILE             0x01000
#define VAL_IS_DEREFABLE        0x02000
#define VAL_IS_FOREIGN          0x04000
/* VAL_IS_GC_TAGGED means it is gc tagged, and must be found during a sweep. */
#define VAL_IS_GC_TAGGED        0x08000
/* VAL_IS_GC_SPECULATIVE means it might have tagged data inside. */
#define VAL_IS_GC_SPECULATIVE   0x10000
#define VAL_IS_GC_SWEEPABLE     (VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE)

#endif
