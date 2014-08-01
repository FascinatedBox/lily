<@lily
    # This is a test for bug GH #10

    # First, make a global object.
    object o = 10

    # Make a function that sets the object to something refcounted. The bug was
    # in the vm's o_set_global. It was treating the object like a plain
    # refcounted value, instead of putting the new value in the object.
    function m():nil { o = [10] }

    # Don't forget to call it!
    m()

    # This causes a crash because what should be an object holding a
    # list[integer] is instead just a list[integer].

    # The bug originally mentions using a string, and an invalid read from
    # debug trying to show the value. Using a list causes a crash, which is more
    # noticeable.
    # If the bug is fixed, this works fine.
    integer i = o.@(list[integer])[0]
@>
