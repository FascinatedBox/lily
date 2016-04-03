var codemirror = CodeMirror(document.getElementById("editor"), {
    lineNumbers: true,
    tabSize:     4,
    value:       ""
});

var parser_create;
var parser_run;
var parser_get_error;
var parser_destroy;
var results_elem;

function get_char_processor()
{
    var buffer = []
    /* SOURCE: https://docs.omniref.com/js/npm/microflo/0.3.13/symbols/Runtime%23processCChar */
    function processCode(code) {
        code = code & 0xFF;
        if (buffer.length == 0) {
            if ((code & 0x80) == 0x00) {        // 0xxxxxxx
                return String.fromCharCode(code);
            }
            buffer.push(code);
            if ((code & 0xE0) == 0xC0) {        // 110xxxxx
                needed = 1;
            } else if ((code & 0xF0) == 0xE0) { // 1110xxxx
                needed = 2;
            } else {                            // 11110xxx
                needed = 3;
            }
            return '';
        }
        if (needed) {
            buffer.push(code);
            needed--;
            if (needed > 0) return '';
        }
        var c1 = buffer[0];
        var c2 = buffer[1];
        var c3 = buffer[2];
        var c4 = buffer[3];
        var ret;
        if (buffer.length == 2) {
            ret = String.fromCharCode(((c1 & 0x1F) << 6)  | (c2 & 0x3F));
        } else if (buffer.length == 3) {
            ret = String.fromCharCode(((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6)  | (c3 & 0x3F));
        } else {
            // http://mathiasbynens.be/notes/javascript-encoding#surrogate-formulae
            var codePoint = ((c1 & 0x07) << 18) | ((c2 & 0x3F) << 12) |
                            ((c3 & 0x3F) << 6)  | (c4 & 0x3F);
            ret = String.fromCharCode(
                    Math.floor((codePoint - 0x10000) / 0x400) + 0xD800,
                    (codePoint - 0x10000) % 0x400 + 0xDC00);
        }
        buffer.length = 0;
        return ret;
    }
    function put_char(tty, val) {
        var c = processCode(val);
        results_elem.value += c;
    }

    return put_char;
}

function ready() {
    parser_create = Module.cwrap('get_parser', 'number', []);
    parser_run = Module.cwrap('run_parser', 'number', ['number', 'string']);
    parser_get_error = Module.cwrap('get_parser_error', 'string', ['number']);
    parser_destroy = Module.cwrap('destroy_parser', '', ['number']);
    results_elem = document.getElementById("results");

    /* Emscripten provides two printing functions to customize printing (print
       and printErr within Module). These functions are called when either stdout
       or stderr is given a line with the data. Most of the time, that's okay.
       However, Lily provides File.write, which does not have an ending newline.
       This causes a problem, because emscripten won't think to flush stdout /
       stderr. So, as a workaround, redefine stdout and stderr's per-char write
       function to immediately put down a result (and pump it to the result
       window. */
    FS.getStream(1).tty.ops.put_char = get_char_processor();
    FS.getStream(2).tty.ops.put_char = get_char_processor();

    updatecontent();
    execute();
}

function updatecontent() {
    var o = document.getElementById("selectexample");
    var name = o.options[o.selectedIndex].value;
    var content = document.getElementById(name).textContent;
    codemirror.setValue(content.replace(/^\n/,""));
    codemirror.setCursor(codemirror.lineCount(),0);
    codemirror.focus();
}

function execute() {
    /* XXX: DISGUSTING HACK!
       Certain parts of the interpreter are not yet ready for the interpreter to
       fail and for the state to be rewound. But shutting down a parser
       completely IS well tested.
       What this does may seem innocent, and it isn't. This is creating a FULL
       PARSER FROM SCRATCH each time the script is run. This consumes AT LEAST
       10K of memory (and a sliver of time) each time a script is run.
       When the interpreter gets state rewinding, and it's battle-tested, then
       I'll burn this to ash and only one interpreter will be used.
       In the meantime, well, sorry. */
    results_elem.value = "";
    var p = parser_create();
    var code_result = parser_run(p, codemirror.getValue());
    if (code_result == 0)
        results_elem.value = parser_get_error(p);

    parser_destroy(p);
}

var selector = document.getElementById("selectexample")
var exampleroot = document.getElementById("exampleroot")
var examples = exampleroot.getElementsByClassName("example")

for (var i = 0; i < examples.length; i++) {
    var o = document.createElement("option")
    o.textContent = examples[i].id
    selector.appendChild(o)
}
