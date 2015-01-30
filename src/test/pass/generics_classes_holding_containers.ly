class ListHolder[A](value: list[A]) {
    var @value = value
}

var holder: ListHolder[integer] = ListHolder::new([1])
