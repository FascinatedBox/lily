var a: any = 10

define f[A](first: A, second: A) {
    
}

# This tests that a static list and hashes can do type inference if it's given
# a parameter which is a generic that may or may not have been resolved yet.
# For the first argument, A is unknown.
# For the second, it's known to be list[any].
# Since it's known, then the second list build knows that the parameters it has
# should be of type any.
f([a], [1])

# In this case, A becomes hash[integer, any] from the first argument.
# The second argument receives A which is resolved, and so picks it out.
# A is hash[integer, any], so the 3 is put into an any so that [2 => 3]
# is inferred as hash[integer => any]
f([1 => a], [2 => 3])

f(<[1, "2", a]>, <[1, "2", 3]>)
