# test_cliexec.py
# This contains some tests to make sure that the cliexec runner hasn't been
# broken by some change.

# This also contains other tests so that new files don't have to be created
# for really short tests.

import subprocess, sys

tests = [
    {
     "command": """  integer a = 10\na = "10" """,
     "message": "Make sure that stack trace numbers are sane",
     "stderr": """\
ErrSyntax: Cannot assign type 'string' to type 'integer'.\n\
Where: File "<str>" at line 2\n""",
     "stdout": ""
    },
    {
     "command": """  @>  """,
     "message": "Make sure @> is a parse error if not parsing tags",
     "stderr": """\
ErrSyntax: Found @> but not expecting tags.\n\
Where: File "<str>" at line 1\n""",
     "stdout": ""
    },
    {
     "command": """  \n\n\nif __line__ == 4: print("ok") else: print("failed")  """,
     "message": "Make sure __line__ is right",
     "stderr": "",
     "stdout": "ok"
    },
    {
     "command": """  1  """,
     "message": "Make sure integers can start an expression",
     "stderr": "",
     "stdout": ""
    },
    {
     "command": """  "1"  """,
     "message": "Make sure strings can start an expression",
     "stderr": "",
     "stdout": ""
    },
    {
     "command": """  11.5  """,
     "message": "Make sure numbers can start an expression",
     "stderr": "",
     "stdout": ""
    },
    {
     "command": """  (1)  """,
     "message": "Make sure parentheses can start an expression",
     "stderr": "",
     "stdout": ""
    },
    {
     "command": """  [1, 2, 3].size()  """,
     "message": "Make sure static lists can start an expression",
     "stderr": "",
     "stdout": ""
    },
    {
     "command": """  __line__  """,
     "message": "Make sure __line__ can start an expression",
     "stderr": "",
     "stdout": ""
    },
]

test_number = 1
test_total = len(tests)
exit_code = 0

for t in tests:
    sys.stdout.write("[%d of %d] Test: %s..." % (test_number, \
        test_number, t["message"]))

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
