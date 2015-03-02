#[
SyntaxError: Use @<name> to get/set properties, not self.<name>.
Where: File "test/fail/self_get_property.ly" at line 9
]#

class Point(x: integer, y: integer) {
    var @x = x
    var @y = self.x
}
