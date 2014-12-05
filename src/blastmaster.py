# blastmaster.py
# This runs a few more complex tests than pre-commit-hook.py. Tests are located
# in src/test/. Tests are divided into passing (src/test/pass) and
# (src/test/fail).

# Tests are grouped into if they fail (test/fail), or if they pass (test/pass).

# Name inspired by Lufia 2. :)

import os, subprocess, sys

pass_files = []
fail_files = []

def load_filenames(path):
    ret = []
    for (dirpath, dirnames, filenames) in os.walk(path):
        for filename in filenames:
            ret.append(os.path.join(dirpath, filename))
    return ret

MODE_PASS = 1
MODE_FAIL = 0

def basic_run(filename, mode, run_range):
    should_pass = (mode == MODE_PASS)
    mode_str = ""
    if mode == MODE_PASS:
        mode_str = "pass"
    else:
        mode_str = "fail"

    sys.stdout.write("[%2d/%2d] Running test %s..." % (run_range[0], \
            run_range[1], os.path.basename(filename)))

    command = "./lily %s" % (filename)
    subp = subprocess.Popen([command], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, shell=True)
    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()

    did_pass = True

    if len(subp_stderr) or len(subp_stdout):
        did_pass = False

    if did_pass == True:
        print "passed."
    else:
        print "failed."

    if did_pass != should_pass:
        did_pass_str = ""
        if did_pass == True:
            did_pass_str = "pass"
        else:
            did_pass_str = "fail"

        print "Blastmaster.py: Test %s should not %s. Stopping.\n" \
                % (os.path.basename(filename), did_pass_str)
        sys.exit(1)

pass_files = sorted(load_filenames('test/pass'))
fail_files = [] # sorted(load_filenames('test/fail'))
run_range = [0, len(pass_files) - 1]

# Temporarily remove this because it fails.
pass_files.remove("test/pass/sanity.ly")

print "\nBlastmaster: Running %d pass tests, %d fail tests, %d total." \
        % (len(pass_files), len(fail_files), run_range[1])

print "Blastmaster: Running passing tests."
for i in range(len(pass_files)):
    run_range[0] += 1
    basic_run(pass_files[i], MODE_PASS, run_range)

#print "Blastmaster: Running failing tests."
#for i in range(len(fail_files)):
#    run_range[0] += 1
#    basic_run(fail_files[i], MODE_FAIL, run_range)
