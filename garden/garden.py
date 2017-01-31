#!/usr/bin/env python

"""Lily Garden

Usage:
  garden.py install [--file=<filename>] [--absfile=<abs_filename>]
                    [(<provider> <source>) (<operator> <version>)]
                    [--recursive]
  garden.py (-h | --help)
  garden.py --version

Options:
  -h --help                 Show this screen.
  --version                 Show version.
  --file=<filename>         The file to use for installing libraries
                            (relative path) [default: .garden]
  --absfile=<abs_filename>  The file to use for installing libraries (absolute)
  --recursive               Download dependencies of installed libraries
"""

import os
import subprocess
import sys
import re
import argparse
from docopt import docopt

fields = ["Author", "Description"]


def lily_github(repo, operator="=", version="latest"):
    '''Fetches a given repository from GitHub with a given version you
    can control your versioning with the operator the version should
    match the version in the Github release'''
    print("Fetching Repository: " + repo + " - Version " + version)
    cwd = os.getcwd()
    repo_basename = repo.split("/")[1]
    if repo_basename.startswith("lily-"):
        repo_basename = repo_basename[5:]

    repo_dir = "packages/{0}".format(repo_basename)
    repo_name = "git://github.com/{0}".format(repo)
    command = ["git", "clone", "--depth", "1", repo_name, repo_dir]
    subprocess.call(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    os.chdir(repo_dir)

    if os.path.isfile("CMakeLists.txt"):
        subprocess.call(["cmake", "."], stdout=subprocess.PIPE)
    subprocess.call(["make"])

    os.chdir(cwd)
    # @TODO Versioning
    # @TODO Recursively parse .garden files from the repository.


def parse_field_line(config, line):
    '''Generates a function call from the given line of text. The line
    is matched against a regular expression that looks for text
    separated by spaces'''
    global fields
    line = line.strip()

    if not line or line.startswith("#"):
        return

    args = re.match("\s*(\w+)\s*:\s*(.+)", line)
    field_name = args.group(1)
    if field_name not in fields:
        if field_name.title() in fields:
            print("%s must be titlecased." % field_name)
        else:
            print("%s is not a valid field." % field_name)

        return

    config[field_name] = args.group(2)


def lily_parse_file(lily_file_path):
    '''Parses a given `.garden` file. Documentation for this file can be
    found at @TODO'''
    os.chdir("packages")
    config = {}

    if os.path.isfile(lily_file_path):
        lines = lily_read_file(lily_file_path)
        for l in lines:
            parse_field_line(config, l)

        for (key, value) in config.items():
            print("Package %s: %s" % (key, value))


def lily_read_file(lily_file_path):
    '''Reads lines from a given `.garden` file and splits it'''
    with open(lily_file_path, 'r') as f:
        return f.read().splitlines()


def perform_install(args):
    '''Installs the dependencies in the packages directory'''
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
        version = args.get("<version>") or "latest"
        operator = args.get("<operator>") or "="

        if provider == "github":
            lily_github(source, operator, version)


if __name__ == '__main__':
    arguments = docopt(__doc__, version='Lily Garden 0.0.1')
    if arguments["install"]:
        perform_install(arguments)
