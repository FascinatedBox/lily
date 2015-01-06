# Lily tries to be smart about figuring out what an empty list can be, as
# well as fixing things up to be of type any when possible.
# This started as an attempt to allow [] to be assignable to a list.
# In the event of defaulting not working, one or more of these will trigger
# SyntaxError, so no checks are needed.

list[integer]             list_one   = []
list[list[integer]]       list_two   = []
                          list_two   = [[]]
list[list[list[integer]]] list_three = []
                          list_three = [[]]
                          list_three = [[[]]]

# The interpreter creates a hash if it can determine that a hash for [] if
# it determines that a hash is wanted.
hash[integer, string]        hash_one = []
hash[integer, list[integer]] hash_two = [1 => [1]]
list[hash[integer, string]]  hash_three = []

tuple[list[integer], any]    tuple_one = <[[], 0]>

# If [] has nothing to go on, it attempts to create a list[any].
# Since type list[any] can hold anything, this works.
any a = []
    a = [[]]
    a = [[[]]]

define f[A](A first, A second) {}

# Defaulting here is a bit harder since f wants type A.
# In this first case, A becomes list[any]
define k_one[A](A third, A fourth) { f([], []) }

# In the second case, A becomes list[A]
# the [] becomes a list[A]
define k_two[A](A third, A fourth) { f([third], []) }

# In this third case, A becomes list[any]
# third becomes an any, resulting in [third] being list[any].
define k_three[A](A third, A fourth) { f([], [third]) }

k_one(1, 1)
k_two(1, 1)
k_three(1, 1)

integer ok = 1
if list_one == [] && hash_one == []:
    ok = 1
else:
    ok = 0

if sys::argv == []:
    ok = 0

if ok == 0:
    print("Failed.\n")
