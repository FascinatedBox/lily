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

current_dir = os.getcwd() + "/"
original_dir = current_dir

def load_filenames(path):
    ret = {}
    for (dirpath, dirnames, filenames) in os.walk(path):
        for name in filenames:
            try:
                ret[dirpath].append(name)
            except:
                ret[dirpath] = [name]

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

def basic_run(filename, mode, run_range):
    global pass_count, error_count, crash_count
    should_pass = (mode == MODE_PASS)
    mode_str = ""
    if mode == MODE_PASS:
        mode_str = "pass"
    else:
        mode_str = "fail"

    command = "lily %s" % (filename)
    subp = subprocess.Popen([command], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, shell=True)
    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()

    did_pass = True
    expected_stderr = get_expected_str(filename)

    try:
        if subp_stderr[-1] == '\n':
            subp_stderr[:-1]
    except IndexError:
        pass

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
    os.chdir(original_dir + dirpath + "/")
    for filename in pass_entries[dirpath]:
        run_range[0] += 1
        basic_run(filename, MODE_PASS, run_range)

print "pre-commit-hook.py: Running %d failing tests..." % fail_entry_count
for dirpath in fail_entries:
    os.chdir(original_dir + dirpath + "/")
    for filename in fail_entries[dirpath]:
        run_range[0] += 1
        basic_run(filename, MODE_FAIL, run_range)

print ("Final stats: %d tests passed, %d errors, %d crashed." \
        % (pass_count, error_count, crash_count))

os.chdir(original_dir)

if error_count == 0 and crash_count == 0:
    sys.exit(0)
else:
    sys.exit(1)


