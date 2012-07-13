<html>
<head>
<title>Lily concat test</title>
</head>
<body>
<@lily
str a = "a"

a.concat(".")
a.concat(a.concat("."))
a.concat(".").concat(".")

print(a.concat("."))
@>
</body>
</html>
