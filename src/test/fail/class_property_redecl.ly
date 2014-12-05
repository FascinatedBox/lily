###
SyntaxError: Property a already exists in class ABC.
Where: File "test/fail/class_property_redecl.ly" at line 6
###

class ABC() { integer @a = 1, @a = 1 }
