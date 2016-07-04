# The tools within this file read from /** ... */ comments and use that as their
# data source. From that source, documentation (among other things) can be
# generated.

import re, sys

def usage():
    message = \
"""\
dyna_tools [command]

This parses special /** ... */ comments to generate specialized code and
documentation.

The format for the arguments is as follows:

input: The file to read and parse comments from.
gen:   What to generate. Options are:
       [gen-docs, source]:
       Reads from 'source' for documentation comments, and uses them to generate
       HTML documentation. The documentation is printed to stdout.

       [gen-dynaload, source]:
       Reads from 'source', using the comments to build a dynaload table that is
       printed to stdout.

       [gen-loader, source, prefix]:
       When a dynaload table specifies a var, or is not dynamically loaded, then
       Lily needs a loader function. A loader function takes requests from the
       interpreter, and returns either a function within the package or creates
       a var to return it.

       This generates a loader, and prints it to stdout. The format of what the
       loader will do is as follows:

       (var loaders are assumed to be static)
       vars:      call   load_var_$name, return the result
       methods:   return lily_$prefix_$class_$name
       functions: return lily_$prefix_$name

       [gen-both, source, prefix]:
       Perform the actions of both gen-loader and gen-dynaload. The dynaload
       table is put into a declaration, and a comment is written at the top as
       to the auto-generated nature of the two.
"""
    print(message)
    sys.exit(0)

def scan_file(filename):
    """\
Scan through a file, picking up all /** ... */ comments. For each one found, a
list of 3 elements is created containing the type, the body, and the
documentation.
    """
    def call_entry(source, name):
        # The format is 'define/method $name($proto)
        split = source.split("\n", 2)
        doc = split[2].strip("\r\n ")

        # TODO: These prototypes should be verified.
        prototype = split[0][6:].lstrip()
        return [name, prototype, doc]

    lines = [line for line in open(filename, "r")]
    i = 0

    doc_blocks = []
    entries = []

    for i in range(len(lines)):
        if lines[i].rstrip("\r\n") == "/**":
            i += 1
            doc = ""
            while lines[i].rstrip("\r\n") != "*/":
                doc += lines[i]
                i += 1

            doc_blocks.append(doc)

    for d in doc_blocks:
        if d.startswith("class"):
            # The format is 'class $name'
            doc = d.split("\n", 2)[2].strip("\r\n ")
            name = re.search("class (.+)", d).group(1)
            entries.append(["class", name, doc])

        elif d.startswith("package"):
            # Same format as for classes.
            doc = d.split("\n", 2)[2].strip("\r\n ")
            name = re.search("package (.+)", d).group(1)
            entries.append(["package", name, doc])

        elif d.startswith("method"):
            entries.append(call_entry(d, "method"))

        elif d.startswith("define"):
            entries.append(call_entry(d, "define"))

        elif d.startswith("var"):
            # The format is 'var $name: $type'
            first_line_pos = d.index("\n")

            # Start at 4 to remove the 'var ' prefix.
            proto = d[4:first_line_pos]
            doc = d[first_line_pos:].strip()

            entries.append(["var", proto, doc])

        # Perhaps this is a "flower box", or not for us, so ignore it.

    return entries

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

    entries = scan_file(filename)
    doc_string = ""

    for e in entries:
        group = e[0]
        doc = doc_as_html(e[2])

        if group == "class":
            name = e[1]
            doc_string += h3(name)
            doc_string += p(doc)

        elif group == "method" or group == "define":
            split_proto = e[1].split("(", 1)
            name = split_proto[0]
            proto = split_proto[1]

            doc_string += lfunc(name)
            # TODO: Pretty print argument types and the return type.
            # For now, settle for making everything bold.
            doc_string += "<span><strong>(%s</strong></span>\n" % (proto)
            doc_string += lexplain(doc)

        elif group == "var":
            split_proto = e[1].split(":", 2)
            name = split_proto[0]
            proto = split_proto[1].strip()

            doc_string += lvar(name)
            doc_string += '<span>:</span>'
            doc_string += ltype(proto)
            doc_string += lexplain(doc)

        elif group == "package":
            name = e[1]
            doc_string += h1(name)
            doc_string += p(doc)

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

    # Remove any default values from the arguments.
    args = args.replace('"', "")
    args = args.replace("=", "")

    args = args.replace(" ", "")

    return proto[0:arg_start] + "\\0(" + args + output

def gen_dynaload(filename):
    """\
Generate dynaload information based on comment blocks in a given filename.
    """

    # Write a numeric value into the dynaload table.
    def dyencode(x):
        return "\\%s" % (oct(x))

    entries = scan_file(filename)

    # Classes need to know how many methods they have so those methods can be
    # skipped over (classes aren't searched at the same time methods are).
    last_class_index = -1
    class_count = 0
    method_count = 0
    table = []
    classes_used = []

    for e in entries:
        group = e[0]

        if group == "class":
            class_count += 1
            if class_count > 1:
                table[last_class_index][1] = method_count
                method_count = 0

            last_class_index = len(table)
            table.append(["class", 0, e[1]])
            classes_used.append(e[1])

        elif group == "method":
            method_count += 1
            table.append(["method", strip_proto(e[1])])

        elif group == "define":
            table.append(["define", strip_proto(e[1])])

        elif group == "var":
            proto = e[1]
            proto = proto.replace(":", "\\0")
            proto = proto.replace(" ", "")
            table.append(["var", proto])

    if class_count:
        table[last_class_index][1] = method_count

    # A dynaload table starts off with how many classes it used, and then their
    # names. This information is used to build the cid table.
    result = ['"' + dyencode(len(classes_used)) + "\\0".join(classes_used) + '\\0"']

    for t in table:
        group = t[0]

        # Classes write down the distance to the next non-class entry, so that
        # the interpreter can skip through methods when it's not looking for
        # them.
        if group == "class":
            result.append(',"C%s%s"' % (dyencode(t[1]), t[2]))

        elif group == "method":
            result.append(',"m:%s"' % (t[1]))

        elif group == "var":
            result.append(',"R\\0%s' % (t[1]))

        elif group == "define":
            result.append(',"F\\0%s"' % (t[1]))

    result.append(',"Z"')

    for r in result:
        print "    ", r

def gen_loader(filename, prefix):
    """\
Generate a loader function based upon information within a given filename.
    """

    entries = scan_file(filename)

    header =  "void lily_%s_loader" % (prefix)
    header += "(lily_options *o, uint16_t *c, int id)\n{"

    loader_entries = [header]

    for i in range(len(entries)):
        e = entries[i]
        group = e[0]

        if group == "define" or group == "method":
            name = e[1]
            name = name[0:name.find("(")].strip()
            # For methods, do this to sanitize their name.
            name = name.replace(".", "_")

            to_append = "    case %d: return lily_%s_%s;" % (i, prefix, name)

            loader_entries.append(to_append)

        elif group == "var":
            name = e[1]
            # No prototype, just the name
            name = name[:name.find(":")].strip()
            name = "load_var_" + name

            # TODO: Scan the prototype to see if the cid table is really needed.
            loader_entries.append("    case %d: return %s(o, c);" % (i, name))

    loader_entries.append("    default: return NULL;\n}")

    for l in loader_entries:
        print l

def gen_both(filename, prefix):
    """\
Generate both a dynaload table and a loader for the filename and prefix given.
    """
    print("/* Loader and dynaload table generated by dyna_tools.py */")
    gen_loader(filename, prefix)
    print("const char *%s_dynaload_table = {" % (prefix))
    gen_dynaload(filename)
    print("}")

if len(sys.argv) < 3:
    usage()
else:
    action = sys.argv[1]
    if action == "gen-docs":
        gen_docs(sys.argv[2])
    elif action == "gen-dynaload":
        gen_dynaload(sys.argv[2])
    elif action == "gen-loader":
        if len(sys.argv) != 4:
            usage()

        gen_loader(sys.argv[2], sys.argv[3])
    elif action == "gen-both":
        if len(sys.argv) != 4:
            usage()

        gen_both(sys.argv[2], sys.argv[3])
    else:
        usage()
