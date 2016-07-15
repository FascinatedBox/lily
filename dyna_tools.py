#!/usr/bin/env python2
#
# The tools within this file read from /** ... */ comments and use that as their
# data source. From that source, documentation (among other things) can be
# generated.

import os, re, sys

def usage():
    message = \
"""\
dyna_tools.py [command]

This parses special /** ... */ comments to generate specialized code and
documentation.

Commands are one of the following:

refresh <filename>:
    Update the dynaload information based on comments within <filename>. An
    include file is generated to hold the information if not already present.
    The name of the generated file is 'dyna_' and the name of the package that
    is mentioned within <filename>

html <filename>:
    Generate html documentation based on <filename>. The documentation is
    written to stdout.
"""
    print(message)
    sys.exit(0)

class CallEntry:
    def __init__(self, source, e_type):
        # The format is 'define/method $name($proto)
        split = source.split("\n", 2)

        # TODO: Verify prototype
        self.proto = split[0][6:].lstrip()
        self.name = self.proto.split("(", 1)[0].strip()
        self.e_type = e_type
        self.doc = split[2].strip("\r\n ")

class ClassEntry:
    def __init__(self, source):
        self.inner_entries = []
        # The format is 'class $name'
        self.name = re.search("class (.+)", source).group(1)
        self.e_type = "class"
        self.doc = source.split("\n", 2)[2].strip("\r\n ")

class PackageEntry:
    def __init__(self, source, embedded):
        self.need_loader = embedded
        self.is_embedded = embedded

        self.inner_entries = []
        # Same format as for classes.
        self.name = re.search("\w+ (.+)", source).group(1)
        self.e_type = "package"
        self.doc = source.split("\n", 2)[2].strip("\r\n ")

class VarEntry:
    def __init__(self, source):
        # The format is 'var $name: $type'
        first_line_pos = source.index("\n")

        # Start at 4 to remove the 'var ' prefix.

        # TODO: Verify prototype
        decl = source[4:first_line_pos]
        decl = decl.split(":", 1)

        self.proto = decl[1].strip()
        self.name = decl[0].strip()
        self.e_type = "var"
        self.doc = source[first_line_pos:].strip()

def scan_file(filename):
    """\
Scan through a file, picking up all /** ... */ comments. This pushes back a
2-tuple with the first value being *Entry records, and the second being the name
of the dynaload generation file (or None).
    """

    lines = [line for line in open(filename, "r")]
    i = 0

    doc_blocks = []
    entries = []
    # Right now, this assumes that one file can only hold one package.
    # Sorry about that.
    package_target = None
    class_target = None
    dyna_include = None

    for i in range(len(lines)):
        l = lines[i].strip()

        if l.startswith('#include "dyna_'):
            # Assume this is the right include file.
            dyna_include = l[8:].strip('" ')
        elif l == "/**":
            i += 1
            doc = ""
            while lines[i].strip() != "*/":
                doc += lines[i]
                i += 1

            doc_blocks.append(doc)

    for d in doc_blocks:
        if d.startswith("class"):
            class_target = ClassEntry(d)
            entries.append(class_target)

        elif d.startswith("embedded"):
            package_target = PackageEntry(d, True)
            entries.append(package_target)

        elif d.startswith("package"):
            package_target = PackageEntry(d, False)
            entries.append(package_target)

        elif d.startswith("method"):
            class_target.inner_entries.append(CallEntry(d, "method"))

        elif d.startswith("define"):
            package_target.inner_entries.append(CallEntry(d, "define"))

        elif d.startswith("var"):
            package_target.need_loader = True
            package_target.inner_entries.append(VarEntry(d))

        # Perhaps this is a "flower box", or not for us, so ignore it.

    return (entries, dyna_include)

def doc_as_html(doc):
    """\
Transform a string given by a comment block to make it look pretty when written
to html.
    """

    # Make sure these are HTML-escaped.
    doc = doc.replace("<", "&lt;")
    doc = doc.replace(">", "&gt;")
    doc = doc.replace("&", "&amp;")

    # `X` should be highlighted as a type.
    doc = re.sub("`(\S+)\`", '<span class="type">\\1</span>', doc)

    # 'Y' is a variable name.
    doc = re.sub("'(\S+)\'", '<span class="varname">\\1</span>', doc)

    # Replace blank lines with breaks to prevent a crammed-in look.
    doc = doc.replace("\n\n", "\n<br />\n")

    return doc

def gen_docs(filename):
    """\
Generate html documentation based on comment blocks within the given filename.
    """
    def h1(text):       return "<h1>%s</h1>\n" % (text)
    def h3(text):       return "<h3>%s</h3>\n" % (text)
    def p(text):        return "<p>%s</p>\n" % (text)
    def lexplain(text): return '<div class="explain">%s</div>\n' % (text)
    def lfunc(text):    return '<span class="funcname">%s</span>' % (text)
    def ltype(text):    return '<span class="type">%s</span>' % (text)
    def lvar(text):     return '<span class="varname">%s</span>' % (text)

    # Not doing dynaload generation, so name is unimportant.
    (entries, _) = scan_file(filename)
    doc_string = ""

    for e in entries:
        if e.e_type == "class":
            doc_string += h3(e.name)
        else: # package
            doc_string += h1(e.name)

        doc_string += doc_as_html(e.doc)

        for inner in e.inner_entries:
            inner_type = inner.e_type
            if inner_type == "method" or inner_type == "define":
                doc_string += lfunc(inner.name)
                # TODO: Pretty print argument types and the return type.
                # For now, settle for making everything bold.
                doc_string += "<span><strong>(%s</strong></span>\n" % (inner.proto)
                doc_string += lexplain(inner.doc)
            else: # var
                doc_string += lvar(inner.name)
                doc_string += '<span>:</span>'
                doc_string += ltype(inner.proto)
                doc_string += lexplain(inner.doc)

        # This ensures that each entry starts on a different line, so that diffs
        # are cleaner.
        doc_string += "\n"

    print doc_string.rstrip("\n")

def strip_proto(proto):
    arg_start = proto.find('(')
    first_dot = proto.find('.')
    output = ")"

    # For methods, cut off the class name at the front.
    if first_dot < arg_start and first_dot != -1:
        proto = proto[first_dot+1:]
        arg_start = proto.find('(')

    arg_end = proto.rfind('):')

    if arg_end != -1:
        output = "):" + proto[arg_end+2:].replace(" ", "")

    args = proto[arg_start+1:arg_end]

    # Remove names from the arguments.
    args = re.sub("\w+:", "", args)

    args = args.replace(" ", "")

    # Strip out the values of optional arguments.
    if args.find("*") != -1:
        # Drop "..."
        args = re.sub("=\"[^\"]*\"", "", args)
        # Drop simple digit literals
        args = re.sub("=\d+", "", args)

    return proto[0:arg_start] + "\\0(" + args + output

def gen_dynaload(entries, package_entry):
    """\
Generate dynaload information based on comment blocks in a given filename.
    """

    # Write a numeric value into the dynaload table.
    def dyencode(x):
        return "\\%s" % (oct(x))

    used = []
    result = []

    for e in entries:
        if e.e_type == "class":
            used.append(e.name)
            result.append('    ,"C%s%s"' % (dyencode(len(e.inner_entries)), e.name))
        # else a package, no-op

        for inner in e.inner_entries:
            inner_type = inner.e_type
            if inner_type == "method":
                result.append('    ,"m:%s"' % (strip_proto(inner.proto)))
            elif inner_type == "define":
                result.append('    ,"F\\0%s"' % (strip_proto(inner.proto)))
            else: # var
                result.append('    ,"R\\0%s\\0%s"' % (inner.name, inner.proto))

    if package_entry.is_embedded:
        name = "_" + package_entry.name
    else:
        name = ""

    header = """\
const char *lily%s_dynaload_table[] = {
    "%s%s\\0"\
""" % (name, dyencode(len(used)), "\\0".join(used))

    result = [header] + result
    result.append('    ,"Z"')
    result.append("};")

    return "\n".join(result)

def gen_loader(entries, package_entry):
    """\
Generate a loader function based upon information within a given filename.
    """

    loader_entries = []
    prefix = package_entry.name
    i = 1

    header = "void *lily_%s_loader(lily_options *o, uint16_t *c, int id)" % (prefix)

    loader_entries.append(header)
    loader_entries.append("{\n    switch (id) {")

    for e in entries:
        # The dynaload id is the # of entries from the first one.
        # This has to therefore adjust for classes.
        if e.e_type == "class":
            i += 1

        for inner in e.inner_entries:
            inner_type = inner.e_type
            if inner_type == "method":
                # Methods have the class name with them, so sanitize that.
                name = inner.name.replace(".", "_")
                to_append = "case %d: return lily_%s_%s;" % (i, prefix, name)
            elif inner_type == "define":
                to_append = "case %d: return lily_%s_%s;" % (i, prefix, inner.name)
            else: # var
                # TODO: Check the proto (is that cid table really needed)?
                to_append = "case %d: return load_var_%s(o, c);" % (i, inner.name)

            loader_entries.append("        " + to_append)
            i += 1

    loader_entries.append("        default: return NULL;\n    }\n}")

    return "\n".join(loader_entries)

def gen_register_func(package_entry):
    """\
Generate a simple register_X function for the caller.
    """

    name = package_entry.name
    if package_entry.need_loader:
        loader_name = "lily_%s_loader" % (name)
    else:
        loader_name = "NULL"

    return """
#define register_%s(p) \
lily_register_package(parser, "%s", lily_%s_dynaload_table, %s);
""" % (name, name, name, loader_name)

def do_refresh(filename):
    (entries, dyna_name) = scan_file(filename)

    package_entry = None
    for e in entries:
        if e.e_type == "package":
            package_entry = e
            break

    prefix = package_entry.name
    base = os.path.dirname(filename)
    if base != "":
        base += os.sep

    if dyna_name == None:
        # This only happens the first time, so stick it down at the bottom.
        # For simple packages, this works out perfectly.
        # Embedders and those needing to register 
        dyna_name = "dyna_%s.h" % (prefix)
        f = open(filename, "a")
        f.write('#include "%s"\n' % (dyna_name))
        f.close()
        dyna_name = base + dyna_name
    else:
        dyna_name = base + "dyna_" + prefix + ".h"

    dyna_file = open(dyna_name, "w")
    dyna_file.write("/* Contents autogenerated by dyna_tools.py */\n")
    dyna_file.write(gen_dynaload(entries, package_entry))
    dyna_file.write("\n")

    if package_entry.need_loader:
        dyna_file.write("\n")
        dyna_file.write(gen_loader(entries, package_entry))
        dyna_file.write("\n")

    if package_entry.is_embedded:
        dyna_file.write(gen_register_func(package_entry))

    dyna_file.close()
    print("Okay, %s has been refreshed." % (dyna_name))

if len(sys.argv) < 3:
    usage()
else:
    action = sys.argv[1]
    if action == "refresh":
        do_refresh(sys.argv[2])
    elif action == "gen-docs":
        gen_docs(sys.argv[2])
    else:
        usage()
