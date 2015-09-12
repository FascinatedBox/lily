The Lily Programming Language
=====

[![Gitter](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/jesserayadkins/lily?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

~~~
<html>
<?lily print("Hello, world!") ?>
</html>
~~~


Lily is a language that can be used to make dynamically-generated content in a way similar to PHP. It can also be run by itself.

Key features include:

1. Static typing, with type inference inspired by Scala.
2. All variables must always have a value (nil and NULL do not exist)
3. Enum classes and variants (to allow for user-creation of an Option kind of type)
4. Parameteric Polymorphism
5. Lambdas, with support for type inference
6. Any value can be passed to a function called show to reveal the contents
7. User-declarable classes
8. Python-inspired import semantics

Lily is inspired by C++, Python, Ruby, Scala, and Rust.
