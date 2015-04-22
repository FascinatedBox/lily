class Container[A]() {  }

# This requires a couple things in order to work:
# First, the emitter must use Container[integer] to deduce that A should be
# an integer in this case.
# Second, the vm must use the resulting value's signature to help assist in
# deducing generic types. This is because there is no value given for A.
var c: Container[integer] = Container::new()
