# pre-commit-hook.py
# This runs all tests. Tests are dividing into those that should succeed
# (test/pass/*) and tests that check for errors (test/fail/*)

# A successful test should not print anything.

# Tests that expect a specific error message should put that message at the
# top, in a comment block. This allows the tester to check that a given error
# message is correct.

import os, subprocess, sys, signal

pass_entries = []
fail_entries = []
MODE_PASS = 1
MODE_FAIL = 0

pass_count = 0
error_count = 0
crash_count = 0

def load_filenames(path):
    ret = {}
    for (dirpath, dirnames, filenames) in os.walk(path):
        for name in filenames:
            path = dirpath + '/'
            try:
                ret[path].append(name)
            except:
                ret[path] = [name]

    return ret

def get_expected_str(filename):
    f = open(filename, "r")
    output = ''
    collecting = 0
    for line in f:
        if line.startswith('#[') and line.rstrip('\r\n') == '#[':
            collecting = True
        elif line.startswith(']#') and line.rstrip('\r\n') == "]#":
            collecting = False
        elif collecting:
            output += line.rstrip("\r\n") + "\n"
    return output

def basic_run(filename, basedir, mode, run_range):
    global pass_count, error_count, crash_count
    should_pass = (mode == MODE_PASS)
    mode_str = ""
    if mode == MODE_PASS:
        mode_str = "pass"
    else:
        mode_str = "fail"

    path = basedir + filename
    # The -g 2 is so that Lily's garbage collector will only allow 2
    # objects at the same time before doing a gc sweep.
    # This ensures that tests aren't passing only because the gc is not
    # being triggered.
    command = "./lily -g 2 %s" % (path)
    subp = subprocess.Popen([command], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, shell=True)
    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()

    did_pass = True
    expected_stderr = get_expected_str(path)

    try:
        if subp_stderr[-1] == '\n':
            subp_stderr[:-1]
    except IndexError:
        pass

    # This makes it so that different tests don't have to reference the
    # directory path in the error (which is tedious and annoying).
    subp_stderr = subp_stderr.replace(basedir, "");

    if  len(subp_stdout) or \
        subp_stderr != expected_stderr or \
        subp.returncode == -signal.SIGSEGV:

        message = ""
        if subp.returncode == -signal.SIGSEGV:
            message = "!!!CRASHED!!!"
            crash_count += 1
        else:
            message = "!!!FAILED!!!"
            error_count += 1

        sys.stdout.write("[%3d/%3d] test %s %s\n" % (run_range[0], \
                run_range[1], os.path.basename(filename), message))
    else:
        pass_count += 1

def count_values(entries):
    result = 0

    for k in entries.iterkeys():
        result += len(entries[k])

    return result

pass_entries = load_filenames('test/pass')
pass_entry_count = count_values(pass_entries)
fail_entries = load_filenames('test/fail')
fail_entry_count = count_values(fail_entries)
run_range = [0, pass_entry_count + fail_entry_count]

print "pre-commit-hook.py: Running %d passing tests..." % pass_entry_count
for dirpath in pass_entries:
    for filename in pass_entries[dirpath]:
        run_range[0] += 1
        basic_run(filename, dirpath, MODE_PASS, run_range)

print "pre-commit-hook.py: Running %d failing tests..." % fail_entry_count
for dirpath in fail_entries:
    for filename in fail_entries[dirpath]:
        run_range[0] += 1
        basic_run(filename, dirpath, MODE_FAIL, run_range)

print ("Final stats: %d tests passed, %d errors, %d crashed." \
        % (pass_count, error_count, crash_count))

if error_count == 0 and crash_count == 0:
    sys.exit(0)
else:
    sys.exit(1)


