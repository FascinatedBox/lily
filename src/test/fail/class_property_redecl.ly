###
SyntaxError: Property a already exists in class ABC.
Where: File "test/fail/class_property_redecl.ly" at line 6
###

class ABC() { var @a = 1 var @a = 1 }
