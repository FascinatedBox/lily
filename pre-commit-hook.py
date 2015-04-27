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

def run_test(options, dirpath, filepath):
    global pass_count, error_count, crash_count, test_count

    test_count += 1

    should_pass = (options['mode'] == 'pass')
    fullpath = dirpath + filepath
    expected_stderr = get_expected_str(fullpath)

    command = "./lily %s %s" % (options['invoke'], fullpath)
    subp = subprocess.Popen([command], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, shell=True)
    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()

    try:
        if subp_stderr[-1] == '\n':
            subp_stderr[:-1]
    except IndexError:
        pass

    # This makes it so that different tests don't have to reference the
    # directory path in the error (which is tedious and annoying).
    subp_stderr = subp_stderr.replace(dirpath, "");

    if subp_stderr != expected_stderr or \
       subp.returncode == -signal.SIGSEGV:

        message = ""
        if subp.returncode == -signal.SIGSEGV:
            message = "!!!CRASHED!!!"
            crash_count += 1
        else:
            message = "!!!FAILED!!!"
            error_count += 1

        sys.stdout.write("#%d test %s %s\n" % (test_count, \
                os.path.basename(filepath), message))
    else:
        pass_count += 1

def get_options_for(dirpath):
    options = {'invoke': '-g 2'}

    # By default, the gc threshold is set to 2 so that the tests don't
    # pass only because the garbage collector isn't invoked.
    if dirpath.endswith('tag_mode/'):
        # This causes the tests run in this directory to be run in
        # tagged mode (code will be between <?lily ... ?> tags only).
        options['invoke'] += ' -t'

    options['mode'] = 'pass' if dirpath.startswith('test/pass') else 'fail'
    return options

def process_test_dir(basepath):
    mode = 'pass' if basepath == 'test/pass' else 'fail'

    # I use _list as a suffix to make the difference more obvious than,
    # say, filepaths versus filepath.
    for (dirpath, dirpath_list, filepath_list) in os.walk(basepath):
        dirpath += '/'
        options = get_options_for(dirpath)
        for filepath in filepath_list:
            run_test(options, dirpath, filepath)

if __name__ == '__main__':
    process_test_dir('test/fail')
    process_test_dir('test/pass')

print ('Final stats: %d tests passed, %d errors, %d crashed.' \
        % (pass_count, error_count, crash_count))

if error_count == 0 and crash_count == 0:
    sys.exit(0)
else:
    sys.exit(1)
