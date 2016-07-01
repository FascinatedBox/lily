#!/usr/bin/env python

"""Lily Garden

Usage:
  garden.py install [--file=<filename>|--absfile=<abs_filename>] [(<provider> <source>) (<operator> <version>)] [--recursive]
  garden.py (-h | --help)
  garden.py --version

Options:
  -h --help                 Show this screen.
  --version                 Show version.
  --file=<filename>         The file to use for installing libraries (relative path) [default: .lily]
  --absfile=<abs_filename>  The file to use for installing libraries (absolute)
  --recursive               Download dependencies of installed libraries
"""

import os, subprocess, sys, re, argparse
from docopt import docopt


# Removes start/end quotes from a given input
def strip_outer_quotes(input):
    if (input.startswith("\"") and input.endswith("\"")) or (input.startswith("'") and input.endswith("'")):
        return input[1:-1]
    return input

# Manages the author of a Lily file
def lily_author(author_name):
    print("Library Author: " + author_name)

# Manages the description of a Lily file
def lily_desc(desc):
    print("Library Description: " + desc)

# Fetches a given repository from GitHub with a given version
# You can control your versioning with the operator
# The version should match the version in the Github Release
def lily_github(repo, operator = "=", version = "-1"):
    print("Fetching Repository: " + repo + " - Version " + version)
    cwd = os.getcwd()

    repo_name = "git://github.com/{0}".format(repo)
    subprocess.call(["git", "clone", "--depth", "1", repo_name], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    os.chdir(repo.split("/")[1])

    if os.path.isfile("CMakeLists.txt"):
        subprocess.call(["cmake", "."], stdout=subprocess.PIPE)
    subprocess.call(["make"])

    os.chdir(cwd)
    #@TODO Versioning
    #@TODO Recursively parse any .lily file that the downloaded repository has

# Generates a function call from the given line of text. The line is matched
# against a regular expression that looks for text separated by spaces
def generate_function_call(line):
    if line.startswith("#") : return
    if not line.strip() : return
    args = re.findall("('.*?'|\".*?\"|\S+)", line)

    functionCall = "lily_{0}(".format(args[0])
    for idx, arg in enumerate(args[1:]):
        arg = strip_outer_quotes(arg)
        functionCall += "\"{0}\",".format(arg)

    return functionCall[:-1] + ")"

# Parses a given `.lily` file. Documentation for this file can be found at @TODO
def lily_parse_file(lily_file_path):
    os.chdir("packages")
    if os.path.isfile(lily_file_path):
        with open(lily_file_path, 'r') as lily_file:
            data = lily_file.read().splitlines()
            for line in data:
                function_call = generate_function_call(line)
                if function_call:
                    eval(function_call)

# Installs the dependencies in the "packages directory"
def perform_install(args):
    if not os.path.exists("packages/"):
        os.mkdir("packages/")

    provider = args.get("<provider>")
    if provider is None:
        absfile = args.get("--absfile")
        if absfile:
            lily_parse_file(absfile)
        else:
            relfile = os.getcwd() + "/" + args["--file"]
            lily_parse_file(relfile)
    else:
        source = args.get("<source>")
        version = args.get("<version>") or "-1"
        operator = args.get("<operator>") or "="

        if provider == "github":
            lily_github(source, operator, version)


if __name__ == '__main__':
    arguments = docopt(__doc__, version='Lily Garden 0.0.1')
    if arguments["install"]:
        perform_install(arguments)
