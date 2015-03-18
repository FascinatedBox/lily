
# This is a function that takes a given value and performs two transformations
# upon it. The result of the transformations is then yielded.
define f[A, B, C](value: A,
                  g: function(A => B),
                  h: function(B => C) => C) {
    return h(g(value))
}

f(
  # A = double
  10.0,

  # A = double, so 'a' is an double. The result is unknown, so the result is
  # left alone.
  # B = integer.
  {|a| a.to_i()},

  # B = integer, so 'b' is an integer. Again, unable to determine the result,
  # so no type inference is done on the result.
  # C = string
  {|b| b.to_s()}
  )
