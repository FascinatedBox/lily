if (1 || 1) != 1:
    print("     1 || 1 == 0 failed.\n")

if (0 || 1) != 1:
    print("     0 || 1 == 0 failed.\n")

if 0 || 0:
    print("     0 || 0 == 1 failed.\n")

if 0 || 0 || 0 || 0:
    print("     0 || 0 || 0 || 0 failed.\n")

if (1 || 1 || 1 || 1) == 0:
    print("     1 || 1 || 1 || 1 failed.\n")

if (1 && 1) == 0:
    print("     1 && 1 == 0 failed.\n")

if 0 && 1:
    print("     0 && 1 == 1 failed.\n")

if 0 && 0:
    print("     0 && 0 == 1 failed.\n")

if 0 && 0 && 0 && 0:
    print("     0 && 0 && 0 && 0 failed.\n")

if (1 && 1 && 1 && 1) == 0:
    print("     1 && 1 && 1 && 1 failed.\n")

define return_1( => integer) { return 1 }

if (0 + 1 || return_1()) == 0:
    print("     0 + 1 || return_1() failed.\n")

if (0 + 1 && return_1()) == 0:
    print("     0 + 1 && return_1()  failed.\n")
