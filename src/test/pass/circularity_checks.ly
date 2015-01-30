var a: list[any] = [1.1, 2]
a[0] = a
a[1] = a
a[1] = 1

var b: any = a[0]
var c: any = a[1]

var d: list[any] = [1, 1]
var e: list[any] = [1, 1]
d[0] = e
e[0] = d

var f: any = c
var g: any = d[0]
var h: any = g.@(list[any])[0]

var i: list[list[any]] = [a, a, a, a]
i[0][0] = i[0]
i[0] = i[0]

var j: list[any] = ["1", "1"]
var k: list[any] = [1.1, 1.1]
k[0] = j
j[0] = k
j[1] = k
k[1] = j
j = [1, 1.1]
k = [1, 1.1]

var l: any = 10
var m: list[list[any]] = [[l]]
m[0][0] = m

var n: any = [10]
var o: any = [n]
var p: any = [o, o, o, o]
n = p
p = 1

var q: list[list[any]] = [[1, 1.1]]
var r: list[any] = [1, 1.1, 1.1, 1.1]

r[0] = q
r[1] = [q, q, q, q, [[q]], 1.1, 5]
q[0] = r

var s: list[any] = [1, 2.1, 3]
var t: any = s
s[0] = t

var u: list[any] = [1, 2.2]
var v: list[list[any]] = [[1, 2.2]]
u[0] = [v, v, v]
v[0] = u
u = [1, 1.1]
v = [[1, 1.1]]

var w: any = 0
var x: list[any] = [w, w, w]
x[0] = x
