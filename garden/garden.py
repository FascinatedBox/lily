#!/usr/bin/env python

import os, subprocess, sys, re

def strip_outer_quotes(input):
    if (input.startswith("\"") and input.endswith("\"")) or (input.startswith("'") and input.endswith("'")):
        return input[1:-1]
    return input

def lily_author(author_name):
    print("Library Author: " + author_name)

def lily_desc(desc):
    print("Library Description: " + desc)

# Fetches a given repository from GitHub with a given version
# You can control your versioning with the operator
# The version should match the version in the Github Release
def lily_github(repo, operator = "=", version = -1):
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

    print("Planting Seeds...")
    if os.path.isfile(lily_file_path):
        with open(lily_file_path, 'r') as lily_file:
            data = lily_file.read().splitlines()
            for line in data:
                function_call = generate_function_call(line)
                if function_call:
                    eval(function_call)
        print("Your garden is ready!")

def execute():
    try:
        if not os.path.exists("packages/"):
            os.mkdir("packages/")

        lily_parse_file(os.getcwd() + "/.lily")
    except OSError:
        pass

execute()
