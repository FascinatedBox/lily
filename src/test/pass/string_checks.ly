# Empty strs
if "" != """""":
    print("Error: Empty multi-line string is not an empty single-line string.\n")

# Single-line, no escape
if "abc" != """abc""":
    print("Error: Multi-line 'abc' is not single-line 'abc'.\n")

# Single-line, start with escape
if "\aabc" != """\aabc""":
    print("Error: Starting with escape char failed.\n")

# Single-line, escape in middle
if "abc\adef" != """abc\adef""":
    print("Error: Escape at middle failed.\n")

# Single-line, escape at end
if "abcdef\a" != """abcdef\a""":
    print("Error: Escape at end failed.\n")

# Single-line, escape at start+mid
if "\aabc\adef" != """\aabc\adef""":
    print("Error: Escape at start and mid failed.\n")

# Single-line, escape at start+end
if "\aabcdef\a" != """\aabcdef\a""":
    print("Error: Escape at start and end failed.\n")

# Single-line, escape at mid+end
if "abc\adef\a" != """abc\adef\a""":
    print("Error: Escape at mid and end failed.\n")

# Finish off with some multiline strings with escapes in interesting areas:
var s1 = """abc\n
"""
var s2 = """abc\n
\n"""
var s3 = """\aabc\a
\aabc\a"""
