class ContainerClass[A](A value) {
    list[A] @container = [value]
}

ContainerClass[integer] t = ContainerClass::new(10)
t.container.append(10)
