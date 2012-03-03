<html>
<head>
<title>Lily Sanity Test</title>
</head>
<body>
<@lily
# Test very basic assignments.
str a = "a"
integer b = 1
number c = 1.0

# Test utf-8 labels and strs.
str ustr = "á"
str h3llö = "hello"

# Test assigning different types to an object.
object o
o = 1
o = 1.1
o = 1

# Random function call.
print("Hello")

# Declaration list check.
str d, e, f
str g

# Test ast merging, specifically for binary ops.
integer math

math = 1 + 2 + 3
math = 4 - 5
math = 50

# Test comparisons.
integer compa, compb, compc, compd, compe
compa = "1" >= "1"
compb = "1" > "1"
compc = "1" < "1"
compd = "1" <= "1"
compe = "1" == "1"

@>
</body>
</html>
