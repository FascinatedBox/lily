# blastmaster.py
# This is Lily's very thorough test runner that runs all of the tests in the
# test directory. Tests are divided into pass and fail:

# Tests are grouped into if they fail, or if they pass. Both sets of tests have
# their resulting trace printed out before the tests begin. For failing tests,
# this means the test will include the error if aft runs with --no-alloc-limit,
# so that the error being raised can be confirmed.

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

def get_file_info(filename):
    # Run the file with aft but without a maximum number of allocs. aft will
    # print the alloc count, as well as any error raised. Some files should
    # pass, but others should always raise an error.
    file_info = {"runs": 0, "success": 1}
    subp = subprocess.Popen(["./lily_aft --no-alloc-limit %s" % filename], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, shell=True)
    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()
    stderr_lines = subp_stderr.split('\n')

    print subp_stderr[0:-1]
    for line in stderr_lines:
        if line.startswith('[aft]: Stats'):
            file_info["runs"] = int(line.split(" ")[6])
        elif line.startswith('Err'):
            file_info["success"] = 0

    if file_info["runs"] == 0:
        raise Exception("aft did not return stats for %s.\n" % filename)
    else:
        return file_info

MODE_PASS = 1
MODE_FAIL = 0

def deep_scan(filename, mode, run_range):
    file_info = get_file_info(filename)
    run_count = file_info["runs"]

    name = "[%d/%d] %s" % (run_range[0], run_range[1], os.path.basename(filename))
    just_printed = True
    block_end = 0
    if run_count > 100:
        block_end = run_count / 100 * 100

    print "%s: Running tests from %d to %d..." % (name, run_count, block_end)
    for i in range(run_count-1, 0, -1):
        if i % 100 == 0 and i != run_count-1:
            just_printed = True
            block_end = i - 100
            if i == 100:
                block_end += 1
            print "%s: Running tests %d to %d..." % (name, i, block_end)

        command = "./lily_aft %d %s" % (i, filename)
        subp = subprocess.Popen([command], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, shell=True)
        (subp_stdout, subp_stderr) = subp.communicate()
        subp.wait()

        line_error = 0

        # Also, ensure that tests don't end on line zero.
        where_str_pos = subp_stderr.find("Where")
        if where_str_pos != -1:
            newline_str_pos = subp_stderr.find("\n", where_str_pos)
            where_str = subp_stderr[where_str_pos:newline_str_pos]
            error_line_pos = int(where_str.split(" ")[-1])
            if error_line_pos == 0:
                line_error = 1

        if subp.returncode != 0:
            if line_error:
                print "%s @ %d: Error: Non-zero exit code (segfault?), and bad line number." % (name, i)
            else:
                print "%s @ %d: Error: Non-zero exit code (segfault?)" % (name, i)
        else:
            if line_error:
                print "%s @ %d: Error: Bad line number." % (name, i)

def basic_run(filename, mode, run_range):
    should_pass = (mode == MODE_PASS)
    mode_str = ""
    if mode == MODE_PASS:
        mode_str = "pass"
    else:
        mode_str = "fail"

    sys.stdout.write("[%d/%d] Running %s test %s..." % (run_range[0], \
            run_range[1], mode_str, os.path.basename(filename)))

    command = "./lily_fs %s" % (filename)
    subp = subprocess.Popen([command], stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, shell=True)
    (subp_stdout, subp_stderr) = subp.communicate()
    subp.wait()

    line_error = 0

    where_str_pos = subp_stderr.find("Where")
    if where_str_pos != -1:
        did_pass = False
        newline_str_pos = subp_stderr.find("\n", where_str_pos)
        where_str = subp_stderr[where_str_pos:newline_str_pos]
        error_line_pos = int(where_str.split(" ")[-1])
        if error_line_pos == 0:
            line_error = 1
    else:
        if subp_stderr.find("Traceback") != -1:
            did_pass = False
        else:
            did_pass = True

    if did_pass == True:
        print "passed."
    else:
        print "failed."
        print subp_stderr

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
fail_files = sorted(load_filenames('test/fail'))
run_range = [1, len(pass_files) + len(fail_files)]
sanity_test = pass_files.pop(pass_files.index("test/pass/sanity.ly"))

print "\nBlastmaster: Running %d pass tests, %d fail tests, %d total." \
        % (len(pass_files), len(fail_files), run_range[1])

print "Blastmaster: Running passing tests."
for i in range(len(pass_files)):
    run_range[0] += 1
    basic_run(pass_files[i], MODE_PASS, run_range)

print "Blastmaster: Running failing tests."
for i in range(len(fail_files)):
    run_range[0] += 1
    basic_run(fail_files[i], MODE_FAIL, run_range)

# Finish off by deep scanning sanity because it takes a LONG time.
print "Blastmaster: Deep scanning sanity.ly."
deep_scan(sanity_test, MODE_PASS, run_range)
