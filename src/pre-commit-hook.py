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
ErrSyntax: Cannot assign type 'string' to type 'integer'.\n\
Where: File "<str>" at line 2\n""",
    },
    {
     "command": """  @>  """,
     "message": "Make sure @> is a parse error if not parsing tags",
     "stderr": """\
ErrSyntax: Found @> but not expecting tags.\n\
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
     "command": """  string s = <[1, "2"]> [3]""",
     "message": "Make sure tuple subscripts don't overflow",
     "stderr": """\
ErrSyntax: Index 3 is out of range for tuple[integer, string].\n\
Where: File "<str>" at line 1\n""",
     "stdout": ""
    },
    {
     "command": """  <[1, "1"]> == <[1, "1"]> """,
     "message": "Make sure tuple literals can compare."
    },
    {
     "command": """  [1, 1.1].append("10")  """,
     "message": "Test that template arguments can default to any."
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
