class ListHolder[A](list[A] value) {
    list[A] @value = value
}

ListHolder[integer] holder = ListHolder::new([1])
