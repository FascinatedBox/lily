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

def start_running(filename, mode, run_range):
    file_info = get_file_info(filename)
    run_count = file_info["runs"]
    did_pass = file_info["success"]

    if (did_pass == True and mode == MODE_FAIL) or (did_pass == False and mode == MODE_PASS):
        if mode == MODE_PASS:
            msg = "pass"
        else:
            msg = "fail"

        print "blastmaster: Test '%s' did not %s. Stopping." % (filename, msg)
        sys.exit(1)

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

pass_files = sorted(load_filenames('test/pass'))
fail_files = sorted(load_filenames('test/fail'))
run_range = [0, len(pass_files) + len(fail_files)]

print "\nBlastmaster: Running %d pass tests, %d fail tests, %d total." \
        % (len(pass_files), len(fail_files), run_range[1])

# The '\n' makes for two lines between each run, but I like how that splits
# everything up to make it more readable and easier to discern what trace
# belongs where.
for i in range(len(pass_files)):
    print "\n"
    run_range[0] += 1
    start_running(pass_files[i], MODE_PASS, run_range)

for i in range(len(fail_files)):
    print "\n"
    run_range[0] += 1
    start_running(fail_files[i], MODE_FAIL, run_range)
