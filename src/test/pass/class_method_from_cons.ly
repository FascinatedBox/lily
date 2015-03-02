class Point(x: function(), y: integer)
{
    var @y = y

    define add_one() {
        @y += 1
    }

    # This should implicitly receive 'self' as the first value.
    add_one()

    # But this won't, because it's not a function created with 'define' within
    # this class.
    x()
}

define f() {
}

Point::new(f, 10)
