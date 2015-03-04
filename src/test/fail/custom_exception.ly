#[
MyException: Correct!
Traceback:
    Function __main__ at line 11
]#

class MyException(msg: string) < Exception(msg) {

}

raise MyException::new("Correct!\n")
