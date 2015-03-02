class Point(x: integer, y: integer) {
    define test( => integer) { return 10 }
    var @z = self.test()
}
