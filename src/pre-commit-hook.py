# pre-commit-hook.py
# An interpreter is a strange and complex beast. It's very easy to accidentally
# break something without realizing that it's been broken. This pre-commit
# script runs a bunch of small tests to ensure some basic sanity, then runs
# the actual sanity test.
# This should only be used to hold simple tests no more than a line or two.
# More complex tests should really get their own file.

import subprocess, sys

tests = [
    {
     "command": """  integer a = 10\na = "10" """,
     "message": "Make sure that stack trace numbers are sane",
     "stderr": """\
SyntaxError: Cannot assign type 'string' to type 'integer'.\n\
Where: File "<str>" at line 2\n""",
    },
    {
     "command": """  @>  """,
     "message": "Make sure @> is a parse error if not parsing tags",
     "stderr": """\
SyntaxError: Found @> but not expecting tags.\n\
Where: File "<str>" at line 1\n""",
    },
    {
     "command": """  \n\n\nif __line__ == 4: print("ok") else: print("failed")  """,
     "message": "Make sure __line__ is right",
     "stdout": "ok"
    },
    {
     "command": """  1  """,
     "message": "Make sure integers can start an expression",
    },
    {
     "command": """  "1"  """,
     "message": "Make sure strings can start an expression",
    },
    {
     "command": """  11.5  """,
     "message": "Make sure doubles can start an expression",
    },
    {
     "command": """  (1)  """,
     "message": "Make sure parentheses can start an expression",
    },
    {
     "command": """  <[1]>  """,
     "message": "Make sure tuple literals can start an expression",
    },
    {
     "command": """  [1, 2, 3].size()  """,
     "message": "Make sure static lists can start an expression",
    },
    {
     "command": """  __line__  """,
     "message": "Make sure __line__ can start an expression",
    },
    {
     "command": """  string s = <[1, "2"]> [1]  """,
     "message": "Make sure tuple subscripts know the type",
    },
    {
     "command": """  <[1, "1"]> == <[1, "1"]> """,
     "message": "Make sure tuple literals can compare."
    },
    {
     "command": """  [1, 1.1].append("10")  """,
     "message": "Test that template arguments can default to any."
    },
    {
     "command": """  if 1: elif 1: 1  """,
     "message": "Test a blank if expression."
    },
    {
     "command": """  10.@(integer)()  """,
     "message": "Failcheck: Bad anonymous call",
     "stderr": """\
SyntaxError: Cannot anonymously call resulting type 'integer'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  print("test.\\n"]  """,
     "message": "Failcheck: Bad close token",
     "stderr": """\
SyntaxError: Expected closing token ')', not ']'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  integer a    a = 10,  """,
     "message": "Failcheck: Bad comma",
     "stderr": """\
SyntaxError: Unexpected token ,.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  printfmt("%s%s%s%s%s", "")  """,
     "message": "Failcheck: Bad printfmt",
     "stderr": """\
FormatError: Not enough args for printfmt.\n\
Traceback:\n\
    Function printfmt [builtin]\n\
    Function __main__ at line 1\n"""
    },
    {
     "command": """  list[integer] a    a = [10]]  """,
     "message": "Failcheck: Bad right brace",
     "stderr": """\
SyntaxError: Unexpected token ']'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  +  """,
     "message": "Failcheck: Bad start token",
     "stderr": """\
SyntaxError: Unexpected token +.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  list[integer] lsi = [10, 20, 30)  """,
     "message": "Failcheck: Bad list close token",
     "stderr": """\
SyntaxError: Expected closing token ']', not ')'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  integer a    a = ((a)  """,
     "message": "Failcheck: Missing matching '('.",
     "stderr": """\
SyntaxError: Unexpected token 'end of file'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  }  """,
     "message": "Failcheck: '}' outside of a block",
     "stderr": """\
SyntaxError: '}' outside of a block.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  break  """,
     "message": "Failcheck: break outside loop",
     "stderr": """\
SyntaxError: 'break' used outside of a loop.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  continue  """,
     "message": "Failcheck: continue outside loop",
     "stderr": """\
SyntaxError: 'continue' used outside of a loop.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  elif  """,
     "message": "Failcheck: elif without if.",
     "stderr": """\
SyntaxError: 'elif' without 'if'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  else  """,
     "message": "Failcheck: else without if.",
     "stderr": """\
SyntaxError: 'else' without 'if'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  return  """,
     "message": "Failcheck: return outside a function.",
     "stderr": """\
SyntaxError: 'return' used outside of a function.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  function f(integer a) {} f("a")  """,
     "message": "Failcheck: Function with wrong argument type",
     "stderr": """\
SyntaxError: f arg #0 expects type 'integer' but got type 'string'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  function f(integer a, integer a) {}  """,
     "message": "Failcheck: Function argument redeclaration",
     "stderr": """\
SyntaxError: a has already been declared.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  function f() {} function f() {}  """,
     "message": "Failcheck: Function redeclaration",
     "stderr": """\
SyntaxError: f has already been declared.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  function f() {} if f():f()  """,
     "message": "Failcheck: if with no value",
     "stderr": """\
SyntaxError: Conditional expression has no value.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  [1] == ["1"]  """,
     "message": "Failcheck: Comparing list[integer] to list[string]",
     "stderr": """\
SyntaxError: Invalid operation: list[integer] == list[string].\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  function f( => integer) { }  f()  """,
     "message": "Failcheck: Check that return expected triggers",
     "stderr": """\
NoReturnError: Function f completed without returning a value.\n\
Traceback:\n\
    Function f at line 1\n\
    Function __main__ at line 1\n"""
    },
    {
     "command": """  if 1: {  """,
     "message": "Failcheck: Unterminated multi-line if",
     "stderr": """\
SyntaxError: Expected '}', not end of file.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  if 1:  """,
     "message": "Failcheck: Unterminated single-line if",
     "stderr": """\
SyntaxError: Expected a value, not 'end of file'.\n\
Where: File "<str>" at line 1\n"""
    },
    {
     "command": """  string s = <[1, "2"]> [3]""",
     "message": "Failcheck: tuple subscript with a too-high index",
     "stderr": """\
SyntaxError: Index 3 is out of range for tuple[integer, string].\n\
Where: File "<str>" at line 1\n""",
    },
    {
     "command": """  function f() {  } function g( => integer) { return f() } """,
     "message": "Failcheck: 'return' with a no-value expression.",
     "stderr": """\
SyntaxError: 'return' expression has no value.\n\
Where: File "<str>" at line 1\n"""
    }
]


test_number = 1
test_total = len(tests)
exit_code = 0

for t in tests:
    try:
        t["stderr"]
    except:
        t["stderr"] = ""

    try:
        t["stdout"]
    except:
        t["stdout"] = ""

    sys.stdout.write("[%2d of %2d] Test: %s..." % (test_number, \
        test_total, t["message"]))

    subp = subprocess.Popen(["./lily_cliexec '%s'" % (t["command"])], \
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)

    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()
    if (subp_stderr != t["stderr"]) or (subp_stdout != t["stdout"]):
        print("failed.\n(Unexpected output). Stopping.\n")
        sys.exit(1)
    else:
        print("passed.")

    test_number += 1
