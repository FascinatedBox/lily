#ifndef LILY_API_VALUE_FLAGS_H
# define LILY_API_VALUE_FLAGS_H

/* These are extra flags for values. They must start at 0x10000, to give 16 bits
   of room for class ids. */

/* VAL_IS_GC_TAGGED means it is gc tagged, and must be found during a sweep. */
#define VAL_IS_GC_TAGGED        0x010000
/* VAL_IS_GC_SPECULATIVE means it might have tagged data inside. */
#define VAL_IS_GC_SPECULATIVE   0x020000
#define VAL_HAS_SWEEP_FLAG      (VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE)
#define VAL_IS_INSTANCE         0x040000
#define VAL_IS_DEREFABLE        0x080000
#define VAL_IS_FOREIGN          0x100000
#define VAL_IS_ENUM             0x200000
#define VAL_IS_CONTAINER        0x400000

/* How much do the CLS flags from lily_class need to be shifted to become vm
   VAL gc flags? */
#define VAL_FROM_CLS_GC_SHIFT   8

#endif
