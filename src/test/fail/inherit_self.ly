#[
SyntaxError: A class cannot inherit from itself!
Where: File "test/fail/inherit_self.ly" at line 6
]#

class abc() < abc {  }
