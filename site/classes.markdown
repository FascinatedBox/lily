Classes
=======

While Lily does borrow lots of inspiration from more functional languages, it also provides facilities for the creation of user-defined classes. First, here is a class to represent a 2-dimensional point, perhaps on a grid or screen of some sort:

```
class Point2D(x: integer, y: integer)
{
    var @x = x
    var @y = y

    define increase(amount: integer) {
        @x += amount
        @y += amount
    }
}
```

One of the nice things about Lily is that you don't need to declare variables in one area, and then create a constructor in another area. Instead, the statements inside the body of the class are what is used to construct it. The body of the class becomes the constructor, and can be used through `<classname>::new`. In this case, `Point2D::new`.

Lily's class members are referenced using the `@` prefix. For those familiar with more object-oriented languages, `@x` is synonymous with `this.x` or `self.x`.

Class members, such as increase, will automatically get an instance of the current class as their first parameter, and can thus refer to class members without explicitly listing self.

Here are some examples of the newly-created class

```
var v = Point2D::new(10, 20)
v.increase(10) # v.x == 20, v.y == 30

var points = [Point2D::new(1, 2), Point2D::new(3, 4)]

# Point2D's x and y are both public so...
define swap_point_xy(input: Point2D)
{
    var temp = input.x
    input.x = input.y
    input.y = temp
}
```

Since both methods of a class, and parameters of a class are usable through a dot syntax, Lily imposes the restriction that a class parameter and a class method cannot both share the same name.

# Inheritance

A class can opt to inherit from one, and only one class. Currently, the only built-in class that can be inherited from is Exception. 

Here's an example of how to inherit from the previously-declared class:

```
class Point3D(x: integer, y: integer, z: integer) > Point2D(x, y)
{
    var @z = z
}

var v = Point3D::new(10, 20, 30)
# Using an inherited method
v.increase(5) # Point3D has values '15, 25, and 30'.
```

Lily also allows marking both class methods and variables as being either protected or private. There is no `public` keyword, because class methods and variables are public by default.

```
class Point2D(x: integer, y: integer)
{
    protected var @x = x
    protected var @y = y
    define increase(amount: integer) {
        @x += amount
        @y += amount
    }
}

var v = Point2D::new(10, 20)

# Syntax Error: @x is protected within Point2D
# v.x = 20

v.increase(10) # v.x == 20, v.y == 30
```

# Limitations

Since Lily is a work-in-progress, there are some limitations for classes:

* There is no support for virtual functions. However, there are plans to do so in the future, through a C#-styled opt-in.

* There are no traits to constrain classes with just yet.

* Methods cannot be overloaded. This may be fixed in the future, unless it interferes greatly with type inference or requires a lot of casting. Thus far, signs for this point to no.
