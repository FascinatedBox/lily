
# pre-commit-hook.py
# This runs all tests. Tests are dividing into those that should succeed
# (test/pass/*) and tests that check for errors (test/fail/*)

# A successful test should not print anything.

# Tests that expect a specific error message should put that message at the
# top, in a comment block. This allows the tester to check that a given error
# message is correct.

import os, subprocess, sys, signal

pass_count = 0
error_count = 0
crash_count = 0
test_count = 0
# No point in doing 'real' argument parsing when this is the only option.
verbose = (sys.argv[-1] == '--verbose')

def get_expected_str(filename):
    f = open(filename, "r")
    output = ''

    if f.readline().rstrip('\r\n') == '#[':
        for line in f:
            if line.startswith(']#') and line.rstrip('\r\n') == "]#":
                break
            else:
                # This assumes that line is always \n terminated.
                output += line

    return output

def run_test(options, dirpath, filepath):
    global pass_count, error_count, crash_count, test_count, verbose

    test_count += 1

    fullpath = dirpath + filepath
    expected_stderr = get_expected_str(fullpath)

    # There are no paths with spaces, so this is ok.
    command = ("./lily %s %s" % (options['invoke'], fullpath)).split(" ")

    subp = subprocess.Popen(command, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()

    # This makes it so that different tests don't have to reference the
    # directory path in the error (which is tedious and annoying).
    subp_stderr = subp_stderr.replace(dirpath, "")
    # For Windows, this fixes newlines so the strings equal.
    subp_stderr = subp_stderr.replace("\r\n", "\n")
    crashed = (subp.returncode == -signal.SIGSEGV)

    if subp_stderr != expected_stderr or crashed:
        message = ""
        if crashed:
            message = "!!!CRASHED!!!"
            crash_count += 1
        else:
            message = "!!!FAILED!!!"
            error_count += 1

        print("#%d test %s %s\n" % (test_count, \
                os.path.basename(filepath), message))

        if not crashed and verbose:
            print("Expected:\n`%s`" % expected_stderr.rstrip("\r\n"))
            print("Received:\n`%s`" % subp_stderr.rstrip("\r\n"))
    else:
        pass_count += 1

def get_options_for(dirpath):
    # This makes the garbage collector perform a sweep after a third object
    # tries to get a tag. The growth factor is zero, which then forces the gc
    # to continually sweep afterward.
    # This hyperaggressive behavior helps uncover bugs from forgetting to tag
    # values appropriately.
    options = {'invoke': '-gstart 2 -gmul 0'}

    if dirpath.endswith('tag_mode'):
        # This causes the tests run in this directory to be run in
        # tagged mode (code will be between <?lily ... ?> tags only).
        options['invoke'] += ' -t'

    return options

def process_test_dir(basepath):
    # I use _list as a suffix to make the difference more obvious than,
    # say, filepaths versus filepath.
    for (dirpath, dirpath_list, filepath_list) in os.walk(basepath):
        options = get_options_for(dirpath)
        dirpath += os.sep
        for filepath in filepath_list:
            run_test(options, dirpath, filepath)

process_test_dir('test' + os.sep + 'fail')
process_test_dir('test' + os.sep + 'pass')
process_test_dir('try')

print ('Final stats: %d tests passed, %d errors, %d crashed.' \
        % (pass_count, error_count, crash_count))

if error_count == 0 and crash_count == 0:
    sys.exit(0)
else:
    sys.exit(1)
