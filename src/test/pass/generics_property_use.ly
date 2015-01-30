class ContainerClass[A](value: A) {
    var @container: list[A] = [value]
}

var t: ContainerClass[integer] = ContainerClass::new(10)
t.container.append(10)
