integer ok = 1

# Integers are 64-bit signed integers. Maximum values are:
integer binary_max = +0b111111111111111111111111111111111111111111111111111111111111111
integer binary_min = -0b1000000000000000000000000000000000000000000000000000000000000000
integer octal_max   = +0c777777777777777777777
integer octal_min   = -0c1000000000000000000000
integer decimal_max = +9223372036854775807
integer decimal_min = -9223372036854775808
integer hex_max     = +0x7fffffffffffffff
integer hex_min     = -0x8000000000000000

# Binary tests

if 0b0110 != 6:
    print("Error: 0b0110 should be 6.\n")

if 0b000110 != 6:
    print("Error: 0b000110 should be 6.\n")

if binary_max != decimal_max:
    print("Error: Binary max is not decimal max.\n")

if binary_min != decimal_min:
    print("Error: Binary min is not decimal min.\n")

# Octal tests

if 0c1234567 != 342391:
    print("Error: 0c1234567 is not 342391.\n")

if octal_max != decimal_max:
    print("Error: Octal max is not decimal max.\n")

if octal_min != decimal_min:
    print("Error: Octal min is not decimal min.\n")

# Hex tests

if 0x1234567890 != 78187493520:
    print("Error: 0x1234567890 is not 78187493520.\n")

if 0xabcdef != 11259375:
    print("Error: 0xabcdef is not 11259375.\n")

if 0xABCDEF != 11259375:
    print("Error: 0xABCDEF is not 11259375.\n")

if hex_max != decimal_max:
    print("Error: Hex max is not decimal max.\n")

if hex_min != decimal_min:
    print("Error: Hex min is not decimal min.\n")

# Random other tests.

if 1e-1 != 0.1:
    print("Error: 1e-1 is not 0.1.\n")

if 1e+1 != 10:
    print("Error: 1e+1 is not 10.\n")

if 1e+1 != 10:
    print("Error: 1e1 is not 10.\n")

if .1 != 0.1:
    print("Error: .1 is not 0.1.\n")
