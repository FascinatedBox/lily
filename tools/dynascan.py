class Scanner(object):
    def __init__(self):
        self.s = ""
        self.offset = 0
        self.token = 0

    def use(self, s):
        self.s = s + "$"
        self.offset = 0

    def next_token(self):
        while self.s[self.offset].isspace():
            self.offset += 1

        ch = self.s[self.offset]
        result = None
        if ch.isalpha():
            start = self.offset
            while ch.isalpha():
                self.offset += 1
                ch = self.s[self.offset]

            result = self.s[start:self.offset]
        elif ch in "()[]:*,":
            result = ch
            self.offset += 1
        elif ch == "$":
            result = ch
            # Don"t advance: Let this instead act like a safe EOF.
        elif ch == "." and self.s[self.offset + 1] != ".":
            result = "."
            self.offset += 1
        elif ch == "=" and self.s[self.offset + 1] == ">":
            result = "=>"
            self.offset += 2
        elif ch == "." and self.s[self.offset:self.offset+3] == "...":
            result = "..."
            self.offset += 3
        else:
            raise ValueError("Bad token '%s'." % ch)

        self.token = result
        return result

    def require(self, expect):
        if self.next_token() != expect:
            raise ValueError("Scanner: Expected '%s', but got '%s'." \
                    % (expect, self.token))

    def require_word(self):
        if self.next_token().isalpha() == False:
            raise ValueError("Scanner: Expected a word, but got '%s'." \
                    % (self.token))
        return self.token

# There's not much commenting in argument or type collection because it was
# ripped straight from Lily"s parser. Why comment twice, right?

def get_type(scanner):
    class_name = scanner.token
    scanner.next_token()

    result = class_name

    # FIXME: Verify the generic count.
    if scanner.token == "[":
        result = [class_name]
        while 1:
            scanner.next_token()
            result.append(get_type(scanner))

            if scanner.token == ",":
                continue
            elif scanner.token == "]":
                break
            else:
                raise ValueError("Expected one of ',]', not '%s'." % scanner.token)
    elif scanner.token == "(":
        result = ["Function", None]

        scanner.next_token()

        if scanner.token != "=>" and scanner.token != ")":
            while 1:
                result.append(get_nameless_arg(scanner))
                if scanner.token == ",":
                    scanner.next_token()
                    continue
                else:
                    break

        if scanner.token == "=>":
            scanner.next_token()
            result[1] = get_type(scanner)

        scanner.next_token()

    return result

def get_nameless_arg(scanner):
    is_optarg = False
    if scanner.token == "*":
        is_optarg = True
        scanner.next_token()

    arg_type = get_type(scanner)
    if is_optarg:
        arg_type = ["*", arg_type]
    elif scanner.token == "...":
        arg_type = ["...", arg_type]
        scanner.next_token()

    return arg_type

def get_named_arg(scanner):
    name = scanner.token
    scanner.require(":")
    scanner.next_token()
    arg_type = get_nameless_arg(scanner)

    return [name, arg_type]

def scan_define(scanner, define_line):
    scanner.use(define_line)
    result = {}

    name = scanner.require_word()
    # Assume that it's a class name if it starts with a capital letter.
    if name[0].upper() == name[0]:
        # Assume it"s a class name.
        result["class"] = name
        scanner.require(".")
        scanner.require_word()

    result["name"] = scanner.token
    scanner.next_token()

    # I"m going to be lazy and say you wrote the generics right.
    # Allowing "$" to stop is so that forgetting "]" won"t infinite loop.
    if scanner.token == "[":
        generics = []
        while scanner.token not in "]$":
            scanner.next_token()
            if scanner.token.isalpha():
                generics.append(scanner.token)

        result["generics"] = "[" + ",".join(generics) + "]"
    else:
        result["generics"] = ""

    scanner.next_token()
    args = [""]

    if scanner.token == "(":
        scanner.next_token()
        if scanner.token == ")":
            raise ValueError("define has empty ().")

        while 1:
            if scanner.token == "self":
                args.append("self")
                scanner.next_token()
            else:
                args.append(get_named_arg(scanner))

            if scanner.token == ",":
                scanner.next_token()
            elif scanner.token == ")":
                scanner.next_token()
                break
            else:
                raise ValueError("Expected one of ',)', got '%s'.\n" % scanner.token)

    if scanner.token == ":":
        scanner.next_token()
        args[0] = get_type(scanner)

    result["proto"] = args

    return result

# While it"s certainly possible to call other functions, this is the only one
# that"s actually meant to be called.

def scan(filename):
    """\
Opens "filename" and scans through it to find dynaload comments.
Those comments are parsed to generate records.
The result of this is a 3-tuple: filename, all lines seen, and a dict of data.
    """
    # For now, assume the caller won"t send non-openable files.
    f = open(filename, "r")
    all_lines = f.readlines()

    # Prevent an unterminated section from causing IndexError.
    all_lines.append("**/")
    f.close()

    s = Scanner()

    dyna_data = {
        "define": [],
    }

    scan_map = {
        "define": scan_define
    }

    i = 0
    stop = len(all_lines)
    while i < stop:
        line = all_lines[i]
        is_define = False

        if line.startswith("/*+") and not line.rstrip().endswith("+*/"):
            i += 1
            line = all_lines[i].lstrip()
            split_line = line.split(" ", 1)
            scan_type = split_line[0]
            scan_data = split_line[1]
            record = None
            try:
                record = scan_map[scan_type](s, scan_data)
            except IndexError:
                print("Invalid scan type '%s'. Stopping.\n" % scan_type)
                assert False

            i += 1
            line = all_lines[i]
            while line.rstrip() != "+*/":
                i += 1
                line = all_lines[i]

            if scan_type == "define":
                i += 1
                line = all_lines[i]

                # For defines, expect the api function to come right after the
                # closing comment. This next expression scoops that up.
                # The line should look like "void apifunc(lily_vm_state...)
                # The only item of interest is "apifunc", because that will be
                # used by the dynaload record.
                record["c_name"] = line.split("(", 1)[0].split(" ")[-1]
                dyna_data[scan_type].append(record)

                record["lily_name"] = record["class"] + "." + record["name"]
        i += 1

    return (filename, all_lines, dyna_data)
