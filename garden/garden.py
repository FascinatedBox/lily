#!/usr/bin/env python

import os, subprocess, sys

if len(sys.argv) != 3 or sys.argv[1] != "install":
    print("Usage: garden.py install 'githubname/package'")
    sys.exit(1)

try:
    os.mkdir("packages")
except OSError:
    pass

os.chdir("packages")

repo_name = "git://github.com/%s" % (sys.argv[2])
print("garden: Cloning repo: '%s'..." % repo_name)
subprocess.call(["git", "clone", "--depth", "1", repo_name],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
os.chdir(sys.argv[2].split("/")[1])

print("garden: Checking to see if cmake should be run...")
if os.path.isfile("CMakeLists.txt"):
    subprocess.call(["cmake", "."], stdout=subprocess.PIPE)

subprocess.call(["make"])
