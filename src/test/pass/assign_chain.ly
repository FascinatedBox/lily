integer ok = 1
integer a = 10, b = 20, c = 30
string result = ""

a = b = c
if a != 30 || b != 30 || c != 30:
    ok = 0

a = 10
b = 20
c = 30

a *= b *= c
if a != 6000 || b != 600 || c != 30:
    ok = 0

a = 1000
b = 100
c = 10
a /= b /= c
if a != 100 || b != 10 || c != 10:
    ok = 0

a = 10
list[integer] d = [20]
a += d[0] += a
if a != 40 || d[0] != 30:
    ok = 0

if ok == 0:
    print("Failed.\n")
