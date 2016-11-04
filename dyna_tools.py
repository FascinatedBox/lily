#!/usr/bin/env python2
#
# The tools within this file read from /** ... */ comments and use that as their
# data source. From that source, documentation (among other things) can be
# generated.

import markdown, os, re, sys

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

markdown <filename>:
    Generate markdown documentation based on <filename>. The documentation is
    written to stdout.

html <filename>:
    Generate html documentation based on <filename>. The documentation is
    written to stdout.
"""
    print(message)
    sys.exit(0)

def strip_proto(proto):
    proto = re.sub('\w+:', "", proto)
    proto = proto.replace(" ", "")
    # Strip out the values of optional arguments.
    if proto.find("*") != -1:
        # Drop "..."
        proto = re.sub("=\"[^\"]*\"", "", proto)
        # Drop simple digit literals
        proto = re.sub("=\d+", "", proto)

    return proto

class BootstrapEntry:
    def __init__(self, source):
        split = source.split("\n", 1)

        # The format is 'bootstrap $name $proto'
        header = re.search("bootstrap\s+(\w+)\s*(.+)", split[0])

        self.inner_entries = []
        self.bootstrap = header.group(2)
        self.name = header.group(1)
        self.e_type = "bootstrap"
        self.doc = split[1].strip()
        self.dyna_len = 0
        self.dyna_letter = 'B'

class CallEntry:
    def __init__(self, source, e_type):
        # The format is 'constructor/define/method $name($proto)
        split = source.split("\n", 2)

        # TODO: Verify prototype
        self.proto = split[0][len(e_type):].lstrip()

        index = self.proto.find("(")
        if index != -1:
            brace_index = self.proto.find("[")
            if brace_index != -1:
                index = min(brace_index, index)
        else:
            index = self.proto.find(":")

        self.name = self.proto[0:index].strip()
        self.proto = self.proto[index:].strip()
        self.clean_proto = strip_proto(self.proto)
        self.e_type = e_type
        self.doc = split[2].strip()
        self.dyna_len = 0
        self.dyna_letter = "F" if e_type == 'define' else "m"

class VariantEntry:
    def __init__(self, source):
        proto = ""

        parenth_index = source.find("(")
        # Variants always fall under a parent enum, and thus don't need doc.
        # A variant may or may not get arguments, so check for that.
        if parenth_index != -1:
            proto = source[parenth_index:].strip()
            name = source[0:parenth_index].strip()
        else:
            name = source.strip()

        self.name = name
        self.proto = proto
        self.e_type = "variant"
        self.doc = ""
        self.dyna_len = 0
        self.dyna_letter = 'V'

class EnumEntry:
    def __init__(self, source):
        decl = re.search("enum (\\w+)([^\\n]*)", source)

        self.variants = []
        self.inner_entries = []
        self.name = decl.group(1).strip()
        self.proto = decl.group(2).strip()
        self.e_type = "enum"

        lines = source.split("\n")[1:]
        start = 0

        for l in lines:
            start += 1
            if l == "":
                break
            else:
                self.variants.append(VariantEntry(l))

        lines = lines[start:]
        self.doc = "\n".join(lines)
        self.dyna_len = 0
        self.dyna_letter = 'E'

class ClassEntry:
    def __init__(self, source):
        self.inner_entries = []
        # The format is 'class $name'
        self.name = re.search("class (.+)", source).group(1)
        self.e_type = "class"
        self.doc = source.split("\n", 2)[2].strip("\r\n ")
        self.dyna_len = 0
        self.dyna_letter = 'C'

class PackageEntry:
    def __init__(self, source, embedded):
        self.need_loader = embedded
        self.is_embedded = embedded

        # Same format as for classes.
        self.name = re.search("\w+ (.+)", source).group(1)
        self.e_type = "package"
        self.doc = source.split("\n", 2)[2].strip("\r\n ")

class VarEntry:
    def __init__(self, source):
        decl = re.search("var (\\w+): (.+)\\n\n(.+)", source)

        self.name = decl.group(1).strip()
        self.proto = decl.group(2).strip()
        self.clean_proto = self.proto
        self.doc = decl.group(3).strip()
        self.e_type = "var"
        self.dyna_len = 0
        self.dyna_letter = 'R'

def scan_file(filename):
    """\
Scan through a file, picking up all /** ... */ comments. This pushes back a
List with the first element always being the package entry to which the other
classes/enums belong.
    """

    lines = [line for line in open(filename, "r")]
    i = 0

    doc_blocks = []
    entries = []
    # Right now, this assumes that one file can only hold one package.
    # Sorry about that.
    package_target = None
    class_target = None
    have_dyna = False
    have_extra = False

    for i in range(len(lines)):
        l = lines[i].strip()

        if l.startswith('#include "'):
            name = l[10:]
            if name.startswith("dyna_"):
                # Assume this is the right include file.
                have_dyna = True
            elif name.startswith("extras_"):
                have_extra = True
        elif l == "/**":
            i += 1
            doc = ""
            while lines[i].strip() != "*/":
                doc += lines[i]
                i += 1

            doc_blocks.append(doc)

    decls = []
    toplevel = []

    for d in doc_blocks:
        if d.startswith("class"):
            class_target = ClassEntry(d)
            decls.append(class_target)

        elif d.startswith("embedded"):
            package_target = PackageEntry(d, True)

        elif d.startswith("package"):
            package_target = PackageEntry(d, False)

        elif d.startswith("enum"):
            class_target = EnumEntry(d)
            decls.append(class_target)

        elif d.startswith("constructor"):
            class_target.inner_entries.append(CallEntry(d, "constructor"))
            class_target.dyna_len += 1

        elif d.startswith("method"):
            class_target.inner_entries.append(CallEntry(d, "method"))
            class_target.dyna_len += 1

        elif d.startswith("define"):
            toplevel.append(CallEntry(d, "define"))

        elif d.startswith("bootstrap"):
            class_target = BootstrapEntry(d)
            decls.append(class_target)

        elif d.startswith("var"):
            package_target.need_loader = True
            toplevel.append(VarEntry(d))

        # Perhaps this is a "flower box", or not for us, so ignore it.

    package_target.have_dyna = have_dyna
    package_target.have_extra = have_extra
    package_target.toplevel = toplevel
    package_target.decls = decls

    return package_target

def gen_markdown(filename):
    def mark(m):
        m.doc = m.doc.replace("\n# ", "\n#### ")

        if m.e_type == "var":
            return "### var %s: `%s`\n\n%s\n\n" % (m.name, m.proto, m.doc)
        else:
            return "### %s %s`%s`\n\n%s\n\n" % (m.e_type, m.name, m.proto, m.doc)

    package = scan_file(filename)
    doc_string = ""

    doc_string += "# %s\n\n" % package.name
    doc_string += package.doc + "\n\n"

    if len(package.toplevel):
        doc_string += "## toplevel\n\n"

        for t in package.toplevel:
            t.doc = t.doc.replace("\n# ", "\n#### ")
            doc_string += mark(t)

    for d in package.decls:
        d.doc = d.doc.replace("\n# ", "\n#### ")

        if d.e_type == "class" or d.e_type == "bootstrap":
            doc_string += "## class %s\n\n%s\n\n" % (d.name, d.doc)
        elif d.e_type == "enum":
            doc_string += "## enum %s\n\n" % (d.name)
            doc_string += "```\nenum %s%s {\n" % (d.name, d.proto)
            for v in d.variants:
                doc_string += "    %s%s\n" % (v.name, v.proto)

            doc_string += "}\n```\n\n%s\n" % d.doc

        try:
            for e in d.inner_entries:
                # Method or constructor
                doc_string += mark(e)
        except AttributeError:
            pass

    return doc_string.rstrip()

def gen_html(filename):
    s = gen_markdown(filename)
    return markdown.markdown(s, extensions=["markdown.extensions.fenced_code"])

def run_dyna_entry(e, accum):
    increase = 1

    letter = e.dyna_letter
    dyna_len = e.dyna_len
    name = e.name

    if e.e_type in ["define", "var"]:
        suffix = e.clean_proto
    elif e.e_type == "method":
        name = name.split(".")[1]
        suffix = e.clean_proto
    elif e.e_type == "constructor":
        name = "<new>"
        suffix = e.clean_proto
    elif e.e_type in ["enum", "variant"]:
        suffix = e.proto
        if suffix == "":
            name += "\\0"
    elif e.e_type == "class":
        suffix = ""
    elif e.e_type == "bootstrap":
        suffix = e.bootstrap

    if suffix:
        suffix = "\\0" + suffix

    accum.append('    ,"%s\\%d%s%s"' % (letter, dyna_len, name, suffix))

    try:
        for inner in e.inner_entries:
            run_dyna_entry(inner, accum)
            increase += 1
    except AttributeError:
        pass

    try:
        for inner in e.variants:
            run_dyna_entry(inner, accum)
            increase += 1
    except AttributeError:
        pass

    return increase

def gen_dynaload(package_entry):
    """\
Generate dynaload information based on comment blocks in a given filename.
    """

    used = []
    result = []
    offset = 1

    entries = package_entry.toplevel + package_entry.decls

    for e in entries:
        if e.e_type in ["class", "bootstrap", "enum"]:
            e.offset = offset + 1
            used.append(e.name)

        offset += run_dyna_entry(e, result)

    if package_entry.is_embedded:
        name = "_" + package_entry.name
    else:
        name = ""

    # Builtin classes have ids manually set, so don't write cid entries.
    if package_entry.name == "builtin":
        used = []

    header = """\
const char *lily%s_dynaload_table[] = {
    "\\%d%s\\0"\
""" % (name, len(used), "\\0".join(used))

    result = [header] + result
    result.append('    ,"Z"')
    result.append("};")

    return "\n".join(result)

def run_loader_entry(e, i, accum, package_name):
    increase = 1

    name = e.name
    what = "lily_"

    if e.e_type == "method":
        name = name.replace(".", "_")
    elif e.e_type == "constructor":
        name += "_new"
    elif e.e_type in ["class", "bootstrap", "enum"]:
        what = ""
    elif e.e_type == "var":
        what = "load_var_"
        package_name = ""
        name += "(o, c)"

    if what:
        accum.append("        case %d: return %s%s%s;" % (i, what, package_name, name))

    try:
        for inner in e.inner_entries:
            run_loader_entry(inner, i + increase, accum, package_name)
            increase += 1
    except AttributeError:
        pass

    try:
        increase += len(e.variants)
    except AttributeError:
        pass

    return increase

def gen_loader(package_entry):
    """\
Generate a loader function based upon information within a given filename.
    """

    loader_entries = []
    pname = package_entry.name + "_"
    i = 1

    header = "void *lily_%sloader(lily_options *o, uint16_t *c, int id)" % (pname)

    loader_entries.append(header)
    loader_entries.append("{\n    switch (id) {")

    entries = package_entry.toplevel + package_entry.decls
    to_append = None

    for e in entries:
        i += run_loader_entry(e, i, loader_entries, pname)

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
lily_register_package(p, "%s", lily_%s_dynaload_table, %s);
""" % (name, name, name, loader_name)

def do_refresh(filename):
    package_entry = scan_file(filename)

    base = os.path.dirname(filename)
    if base != "":
        base += os.sep

    to_add = []
    refresh_list = []

    dyna_name = "dyna_%s.h" % (package_entry.name)
    refresh_list.append(base + dyna_name)

    if package_entry.have_dyna == False:
        to_add.append(dyna_name)

    dyna_file = open(base + dyna_name, "w")
    dyna_file.write("/* Contents autogenerated by dyna_tools.py */\n")
    dyna_file.write(gen_dynaload(package_entry))
    dyna_file.write("\n")

    if package_entry.need_loader:
        dyna_file.write("\n")
        dyna_file.write(gen_loader(package_entry))
        dyna_file.write("\n")

    if package_entry.is_embedded:
        dyna_file.write(gen_register_func(package_entry))

    dyna_file.close()

    if package_entry.name == "builtin":
        name = "%sextras_%s.h" % (base, package_entry.name)
        refresh_list.append(name)
        extras_file = open(name, "w")

        # Most of the other classes need offsets for their dynaload entries,
        # because the classes are manually loaded.
        # The slice is to avoid writing an offset for the builtin package.
        l = lambda x: "#define %-26s %d" % (x.name.upper() + "_OFFSET", x.offset)

        extras_file.write("/* Contents autogenerated by dyna_tools.py */\n")
        extras_file.write("\n".join(map(l, package_entry.decls)) + "\n")
        extras_file.close()

    if to_add != []:
        f = open(filename, "a")
        for t in to_add:
            f.write('#include "%s"\n' % (t))
        f.close()

    for r in refresh_list:
        print("dyna_tools.py: Refreshed '%s'." % (r))

if len(sys.argv) < 3:
    usage()
else:
    action = sys.argv[1]
    if action == "refresh":
        do_refresh(sys.argv[2])
    elif action == "markdown":
        print(gen_markdown(sys.argv[2]))
    elif action == "html":
        print(gen_html(sys.argv[2]))
    else:
        usage()
