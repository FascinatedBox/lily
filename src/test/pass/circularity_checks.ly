list[any] a = [1.1, 2]
a[0] = a
a[1] = a
a[1] = 1

any b = a[0]
any c = a[1]

list[any] d = [1, 1]
list[any] e = [1, 1]
d[0] = e
e[0] = d

any f = c
any g = d[0]
any h = g.@(list[any])[0]

list[list[any]] i = [a, a, a, a]
i[0][0] = i[0]
i[0] = i[0]

list[any] j = ["1", "1"]
list[any] k = [1.1, 1.1]
k[0] = j
j[0] = k
j[1] = k
k[1] = j
j = [1, 1.1]
k = [1, 1.1]

any l = 10
list[list[any]] m = [[l]]
m[0][0] = m

any n = [10]
any o = [n]
any p = [o, o, o, o]
n = p
p = 1

list[list[any]] q = [[1, 1.1]]
list[any] r = [1, 1.1, 1.1, 1.1]

r[0] = q
r[1] = [q, q, q, q, [[q]], 1.1, 5]
q[0] = r

list[any] s = [1, 2.1, 3]
any t = s
s[0] = t

list[any] u = [1, 2.2]
list[list[any]] v = [[1, 2.2]]
u[0] = [v, v, v]
v[0] = u
u = [1, 1.1]
v = [[1, 1.1]]

any w = 0
list[any] x = [w, w, w]
x[0] = x
