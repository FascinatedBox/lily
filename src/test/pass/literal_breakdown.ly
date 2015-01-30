# Issue #36: '1+1' does not work because parser sees it as '1, +1' (two
# integer literals) instead of breaking it down to '1, +, 1'.
var test_36 = 1+1
