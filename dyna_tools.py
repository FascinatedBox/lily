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
"""
    print(message)
    sys.exit(0)

def scan_file(filename):
    """\
Scan through a file, picking up all /** ... */ comments. For each one found, a
list of 3 elements is created containing the type, the body, and the
documentation.
    """
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

        elif d.startswith("method") or d.startswith("define"):
            # The format is 'method $name($prototype):$return'
            split = d.split("\n", 2)
            doc = split[2].strip("\r\n ")

            # TODO: These prototypes should be verified.
            prototype = split[0][6:].lstrip()
            entries.append(["call", prototype, doc])

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

        elif group == "call":
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

if len(sys.argv) < 3:
    usage()
else:
    action = sys.argv[1]
    if action == "gen-docs":
        gen_docs(sys.argv[2])
    else:
        usage()
