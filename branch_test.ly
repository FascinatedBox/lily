<@lily

integer a
a = 1


# Lily conditions are a mix of styles.
# This is a single-expression condition. Simple enough.
if a == 1:
    a = 2

# This executes outside of the 'if'.
a = 2

# All of these accept a single expression.
# A single-expression condition can't have more single-expression conditions
# inside of it.
if a == 1:
    a = 2
elif a == 2:
    a = 3
elif a == 3:
    a = 4
# else:
#     a = 4

# This is a multi-line condition.
# In Lily, only the if requires a {, and it means all subsequent elif and else
# statements are multiline. This is really important.
# The } signals the end of the multi-line condition.
# There are a few reasons that Lily does conditions this way:
# * : instead of () looks cleaner to me.
# * {} are for the entire if, so no more forgetting them or having lines with
#   just { or } or both depending on style. At the same time, it's very obvious
#   where the if terminates.
# 
# if a == 1: {
#     a = 2
#     a = 3
# elif a = 2:
#     a = 4
#     a = 5
# }
@>
