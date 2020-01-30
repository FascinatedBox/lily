#ifndef LILY_API_VALUE_FLAGS_H
# define LILY_API_VALUE_FLAGS_H

/* This file contains markers and flags for use with lily_value. Some of these
   are used with lily_literal in symtab and the same rules documented herein
   apply to them as well.

   Every value belongs to a group. Some groups map directly to a single class,
   such as the Integer group. Others are more vague, like the Instance group for
   class instances.

   Values have two different sets of membership markers. The first set are the
   _BASE markers. These markers are used, for example, when comparing or
   printing a value. Using the FLAGS_TO_BASE macro strips away all but the
   _BASE marker of a value's flags.

   The _FLAG set of markers exist to allow fast testing of what group a value
   belongs to without using the above-mentioned macro and then doing a
   comparison. A function can simply tap to check of one of the flags exist.

   Where _FLAG macros start is not important. However, if the gc tag or
   speculative flags move, then the shift macro below must be updated.

   There are some rules:

   No partial membership. Do not create a value with a _BASE marker but without
   a corresponding _FLAG marker. It is fine to omit a _FLAG if it does not
   exist for a _BASE. Some _FLAG macros would go unused (nothing needs to do a
   quick tap to check for Tuple for example).

   Any value that is not primiive should have VAL_IS_DEREFABLE set on it.
   Symtab literals are given 1 ref as a base, and Symtab is later responsible
   for dropping them.

   The gc speculative flag means it might hold a gc tagged value, or it might
   not. This is used aggressively. The other flag, gc tagged, means that the
   value has a gc tag somewhere associated to it. The absense of either means
   the gc can skip the value in question. Only interesting values get either
   flag. Boring values like String will never get it.

   All of this is done strictly internally. If you're using the api, refcounting
   and group marking is done automatically under the hood. */

#define VAL_IS_GC_TAGGED        0x0010000
#define VAL_IS_GC_SPECULATIVE   0x0020000
#define VAL_HAS_SWEEP_FLAG      (VAL_IS_GC_TAGGED | VAL_IS_GC_SPECULATIVE)
#define VAL_IS_DEREFABLE        0x0040000

#define V_INTEGER_FLAG          0x0100000
#define V_DOUBLE_FLAG           0x0200000
#define V_STRING_FLAG           0x0400000
#define V_BYTE_FLAG             0x0800000
#define V_BYTESTRING_FLAG       0x1000000
#define V_FUNCTION_FLAG	        0x2000000

#define V_INTEGER_BASE          1
#define V_DOUBLE_BASE           2
#define V_STRING_BASE           3
#define V_BYTE_BASE             4
#define V_BYTESTRING_BASE       5
#define V_BOOLEAN_BASE          6
#define V_FUNCTION_BASE         7
#define V_LIST_BASE             8
#define V_HASH_BASE             9
#define V_TUPLE_BASE            10
#define V_FILE_BASE             11
#define V_COROUTINE_BASE        12
#define V_FOREIGN_BASE          13
#define V_INSTANCE_BASE         14
#define V_UNIT_BASE             15
#define V_VARIANT_BASE          16
#define V_EMPTY_VARIANT_BASE    17

#define FLAGS_TO_BASE(x)        (x->flags & 31)

/* How much do the CLS flags from lily_class need to be shifted to become vm
   VAL gc flags? */
#define VAL_FROM_CLS_GC_SHIFT   10

#endif
