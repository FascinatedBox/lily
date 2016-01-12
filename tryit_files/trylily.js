// Note: For maximum-speed code, see "Optimizing Code" on the Emscripten wiki, https://github.com/kripken/emscripten/wiki/Optimizing-Code
// Note: Some Emscripten settings may limit the speed of the generated code.
// The Module object: Our interface to the outside world. We import
// and export values on it, and do the work to get that through
// closure compiler if necessary. There are various ways Module can be used:
// 1. Not defined. We create it here
// 2. A function parameter, function(Module) { ..generated code.. }
// 3. pre-run appended it, var Module = {}; ..generated code..
// 4. External script tag defines var Module.
// We need to do an eval in order to handle the closure compiler
// case, where this code here is minified but Module was defined
// elsewhere (e.g. case 4 above). We also need to check if Module
// already exists (e.g. case 3 above).
// Note that if you want to run closure, and also to use Module
// after the generated code, you will need to define   var Module = {};
// before the code. Then that object will be used in the code, and you
// can continue to use Module afterwards as well.
var Module;
if (!Module) Module = eval('(function() { try { return Module || {} } catch(e) { return {} } })()');

// Sometimes an existing Module object exists with properties
// meant to overwrite the default module functionality. Here
// we collect those properties and reapply _after_ we configure
// the current environment's defaults to avoid having to be so
// defensive during initialization.
var moduleOverrides = {};
for (var key in Module) {
  if (Module.hasOwnProperty(key)) {
    moduleOverrides[key] = Module[key];
  }
}

// The environment setup code below is customized to use Module.
// *** Environment setup code ***
var ENVIRONMENT_IS_NODE = typeof process === 'object' && typeof require === 'function';
var ENVIRONMENT_IS_WEB = typeof window === 'object';
var ENVIRONMENT_IS_WORKER = typeof importScripts === 'function';
var ENVIRONMENT_IS_SHELL = !ENVIRONMENT_IS_WEB && !ENVIRONMENT_IS_NODE && !ENVIRONMENT_IS_WORKER;

if (ENVIRONMENT_IS_NODE) {
  // Expose functionality in the same simple way that the shells work
  // Note that we pollute the global namespace here, otherwise we break in node
  if (!Module['print']) Module['print'] = function print(x) {
    process['stdout'].write(x + '\n');
  };
  if (!Module['printErr']) Module['printErr'] = function printErr(x) {
    process['stderr'].write(x + '\n');
  };

  var nodeFS = require('fs');
  var nodePath = require('path');

  Module['read'] = function read(filename, binary) {
    filename = nodePath['normalize'](filename);
    var ret = nodeFS['readFileSync'](filename);
    // The path is absolute if the normalized version is the same as the resolved.
    if (!ret && filename != nodePath['resolve'](filename)) {
      filename = path.join(__dirname, '..', 'src', filename);
      ret = nodeFS['readFileSync'](filename);
    }
    if (ret && !binary) ret = ret.toString();
    return ret;
  };

  Module['readBinary'] = function readBinary(filename) { return Module['read'](filename, true) };

  Module['load'] = function load(f) {
    globalEval(read(f));
  };

  Module['arguments'] = process['argv'].slice(2);

  module['exports'] = Module;
}
else if (ENVIRONMENT_IS_SHELL) {
  if (!Module['print']) Module['print'] = print;
  if (typeof printErr != 'undefined') Module['printErr'] = printErr; // not present in v8 or older sm

  if (typeof read != 'undefined') {
    Module['read'] = read;
  } else {
    Module['read'] = function read() { throw 'no read() available (jsc?)' };
  }

  Module['readBinary'] = function readBinary(f) {
    return read(f, 'binary');
  };

  if (typeof scriptArgs != 'undefined') {
    Module['arguments'] = scriptArgs;
  } else if (typeof arguments != 'undefined') {
    Module['arguments'] = arguments;
  }

  this['Module'] = Module;

  eval("if (typeof gc === 'function' && gc.toString().indexOf('[native code]') > 0) var gc = undefined"); // wipe out the SpiderMonkey shell 'gc' function, which can confuse closure (uses it as a minified name, and it is then initted to a non-falsey value unexpectedly)
}
else if (ENVIRONMENT_IS_WEB || ENVIRONMENT_IS_WORKER) {
  Module['read'] = function read(url) {
    var xhr = new XMLHttpRequest();
    xhr.open('GET', url, false);
    xhr.send(null);
    return xhr.responseText;
  };

  if (typeof arguments != 'undefined') {
    Module['arguments'] = arguments;
  }

  if (typeof console !== 'undefined') {
    if (!Module['print']) Module['print'] = function print(x) {
      console.log(x);
    };
    if (!Module['printErr']) Module['printErr'] = function printErr(x) {
      console.log(x);
    };
  } else {
    // Probably a worker, and without console.log. We can do very little here...
    var TRY_USE_DUMP = false;
    if (!Module['print']) Module['print'] = (TRY_USE_DUMP && (typeof(dump) !== "undefined") ? (function(x) {
      dump(x);
    }) : (function(x) {
      // self.postMessage(x); // enable this if you want stdout to be sent as messages
    }));
  }

  if (ENVIRONMENT_IS_WEB) {
    this['Module'] = Module;
  } else {
    Module['load'] = importScripts;
  }
}
else {
  // Unreachable because SHELL is dependant on the others
  throw 'Unknown runtime environment. Where are we?';
}

function globalEval(x) {
  eval.call(null, x);
}
if (!Module['load'] == 'undefined' && Module['read']) {
  Module['load'] = function load(f) {
    globalEval(Module['read'](f));
  };
}
if (!Module['print']) {
  Module['print'] = function(){};
}
if (!Module['printErr']) {
  Module['printErr'] = Module['print'];
}
if (!Module['arguments']) {
  Module['arguments'] = [];
}
// *** Environment setup code ***

// Closure helpers
Module.print = Module['print'];
Module.printErr = Module['printErr'];

// Callbacks
Module['preRun'] = [];
Module['postRun'] = [];

// Merge back in the overrides
for (var key in moduleOverrides) {
  if (moduleOverrides.hasOwnProperty(key)) {
    Module[key] = moduleOverrides[key];
  }
}



// === Auto-generated preamble library stuff ===

//========================================
// Runtime code shared with compiler
//========================================

var Runtime = {
  stackSave: function () {
    return STACKTOP;
  },
  stackRestore: function (stackTop) {
    STACKTOP = stackTop;
  },
  forceAlign: function (target, quantum) {
    quantum = quantum || 4;
    if (quantum == 1) return target;
    if (isNumber(target) && isNumber(quantum)) {
      return Math.ceil(target/quantum)*quantum;
    } else if (isNumber(quantum) && isPowerOfTwo(quantum)) {
      return '(((' +target + ')+' + (quantum-1) + ')&' + -quantum + ')';
    }
    return 'Math.ceil((' + target + ')/' + quantum + ')*' + quantum;
  },
  isNumberType: function (type) {
    return type in Runtime.INT_TYPES || type in Runtime.FLOAT_TYPES;
  },
  isPointerType: function isPointerType(type) {
  return type[type.length-1] == '*';
},
  isStructType: function isStructType(type) {
  if (isPointerType(type)) return false;
  if (isArrayType(type)) return true;
  if (/<?{ ?[^}]* ?}>?/.test(type)) return true; // { i32, i8 } etc. - anonymous struct types
  // See comment in isStructPointerType()
  return type[0] == '%';
},
  INT_TYPES: {"i1":0,"i8":0,"i16":0,"i32":0,"i64":0},
  FLOAT_TYPES: {"float":0,"double":0},
  or64: function (x, y) {
    var l = (x | 0) | (y | 0);
    var h = (Math.round(x / 4294967296) | Math.round(y / 4294967296)) * 4294967296;
    return l + h;
  },
  and64: function (x, y) {
    var l = (x | 0) & (y | 0);
    var h = (Math.round(x / 4294967296) & Math.round(y / 4294967296)) * 4294967296;
    return l + h;
  },
  xor64: function (x, y) {
    var l = (x | 0) ^ (y | 0);
    var h = (Math.round(x / 4294967296) ^ Math.round(y / 4294967296)) * 4294967296;
    return l + h;
  },
  getNativeTypeSize: function (type) {
    switch (type) {
      case 'i1': case 'i8': return 1;
      case 'i16': return 2;
      case 'i32': return 4;
      case 'i64': return 8;
      case 'float': return 4;
      case 'double': return 8;
      default: {
        if (type[type.length-1] === '*') {
          return Runtime.QUANTUM_SIZE; // A pointer
        } else if (type[0] === 'i') {
          var bits = parseInt(type.substr(1));
          assert(bits % 8 === 0);
          return bits/8;
        } else {
          return 0;
        }
      }
    }
  },
  getNativeFieldSize: function (type) {
    return Math.max(Runtime.getNativeTypeSize(type), Runtime.QUANTUM_SIZE);
  },
  dedup: function dedup(items, ident) {
  var seen = {};
  if (ident) {
    return items.filter(function(item) {
      if (seen[item[ident]]) return false;
      seen[item[ident]] = true;
      return true;
    });
  } else {
    return items.filter(function(item) {
      if (seen[item]) return false;
      seen[item] = true;
      return true;
    });
  }
},
  set: function set() {
  var args = typeof arguments[0] === 'object' ? arguments[0] : arguments;
  var ret = {};
  for (var i = 0; i < args.length; i++) {
    ret[args[i]] = 0;
  }
  return ret;
},
  STACK_ALIGN: 8,
  getAlignSize: function (type, size, vararg) {
    // we align i64s and doubles on 64-bit boundaries, unlike x86
    if (vararg) return 8;
    if (!vararg && (type == 'i64' || type == 'double')) return 8;
    if (!type) return Math.min(size, 8); // align structures internally to 64 bits
    return Math.min(size || (type ? Runtime.getNativeFieldSize(type) : 0), Runtime.QUANTUM_SIZE);
  },
  calculateStructAlignment: function calculateStructAlignment(type) {
    type.flatSize = 0;
    type.alignSize = 0;
    var diffs = [];
    var prev = -1;
    var index = 0;
    type.flatIndexes = type.fields.map(function(field) {
      index++;
      var size, alignSize;
      if (Runtime.isNumberType(field) || Runtime.isPointerType(field)) {
        size = Runtime.getNativeTypeSize(field); // pack char; char; in structs, also char[X]s.
        alignSize = Runtime.getAlignSize(field, size);
      } else if (Runtime.isStructType(field)) {
        if (field[1] === '0') {
          // this is [0 x something]. When inside another structure like here, it must be at the end,
          // and it adds no size
          // XXX this happens in java-nbody for example... assert(index === type.fields.length, 'zero-length in the middle!');
          size = 0;
          if (Types.types[field]) {
            alignSize = Runtime.getAlignSize(null, Types.types[field].alignSize);
          } else {
            alignSize = type.alignSize || QUANTUM_SIZE;
          }
        } else {
          size = Types.types[field].flatSize;
          alignSize = Runtime.getAlignSize(null, Types.types[field].alignSize);
        }
      } else if (field[0] == 'b') {
        // bN, large number field, like a [N x i8]
        size = field.substr(1)|0;
        alignSize = 1;
      } else if (field[0] === '<') {
        // vector type
        size = alignSize = Types.types[field].flatSize; // fully aligned
      } else if (field[0] === 'i') {
        // illegal integer field, that could not be legalized because it is an internal structure field
        // it is ok to have such fields, if we just use them as markers of field size and nothing more complex
        size = alignSize = parseInt(field.substr(1))/8;
        assert(size % 1 === 0, 'cannot handle non-byte-size field ' + field);
      } else {
        assert(false, 'invalid type for calculateStructAlignment');
      }
      if (type.packed) alignSize = 1;
      type.alignSize = Math.max(type.alignSize, alignSize);
      var curr = Runtime.alignMemory(type.flatSize, alignSize); // if necessary, place this on aligned memory
      type.flatSize = curr + size;
      if (prev >= 0) {
        diffs.push(curr-prev);
      }
      prev = curr;
      return curr;
    });
    if (type.name_ && type.name_[0] === '[') {
      // arrays have 2 elements, so we get the proper difference. then we scale here. that way we avoid
      // allocating a potentially huge array for [999999 x i8] etc.
      type.flatSize = parseInt(type.name_.substr(1))*type.flatSize/2;
    }
    type.flatSize = Runtime.alignMemory(type.flatSize, type.alignSize);
    if (diffs.length == 0) {
      type.flatFactor = type.flatSize;
    } else if (Runtime.dedup(diffs).length == 1) {
      type.flatFactor = diffs[0];
    }
    type.needsFlattening = (type.flatFactor != 1);
    return type.flatIndexes;
  },
  generateStructInfo: function (struct, typeName, offset) {
    var type, alignment;
    if (typeName) {
      offset = offset || 0;
      type = (typeof Types === 'undefined' ? Runtime.typeInfo : Types.types)[typeName];
      if (!type) return null;
      if (type.fields.length != struct.length) {
        printErr('Number of named fields must match the type for ' + typeName + ': possibly duplicate struct names. Cannot return structInfo');
        return null;
      }
      alignment = type.flatIndexes;
    } else {
      var type = { fields: struct.map(function(item) { return item[0] }) };
      alignment = Runtime.calculateStructAlignment(type);
    }
    var ret = {
      __size__: type.flatSize
    };
    if (typeName) {
      struct.forEach(function(item, i) {
        if (typeof item === 'string') {
          ret[item] = alignment[i] + offset;
        } else {
          // embedded struct
          var key;
          for (var k in item) key = k;
          ret[key] = Runtime.generateStructInfo(item[key], type.fields[i], alignment[i]);
        }
      });
    } else {
      struct.forEach(function(item, i) {
        ret[item[1]] = alignment[i];
      });
    }
    return ret;
  },
  dynCall: function (sig, ptr, args) {
    if (args && args.length) {
      if (!args.splice) args = Array.prototype.slice.call(args);
      args.splice(0, 0, ptr);
      return Module['dynCall_' + sig].apply(null, args);
    } else {
      return Module['dynCall_' + sig].call(null, ptr);
    }
  },
  functionPointers: [],
  addFunction: function (func) {
    for (var i = 0; i < Runtime.functionPointers.length; i++) {
      if (!Runtime.functionPointers[i]) {
        Runtime.functionPointers[i] = func;
        return 2*(1 + i);
      }
    }
    throw 'Finished up all reserved function pointers. Use a higher value for RESERVED_FUNCTION_POINTERS.';
  },
  removeFunction: function (index) {
    Runtime.functionPointers[(index-2)/2] = null;
  },
  getAsmConst: function (code, numArgs) {
    // code is a constant string on the heap, so we can cache these
    if (!Runtime.asmConstCache) Runtime.asmConstCache = {};
    var func = Runtime.asmConstCache[code];
    if (func) return func;
    var args = [];
    for (var i = 0; i < numArgs; i++) {
      args.push(String.fromCharCode(36) + i); // $0, $1 etc
    }
    code = Pointer_stringify(code);
    if (code[0] === '"') {
      // tolerate EM_ASM("..code..") even though EM_ASM(..code..) is correct
      if (code.indexOf('"', 1) === code.length-1) {
        code = code.substr(1, code.length-2);
      } else {
        // something invalid happened, e.g. EM_ASM("..code($0)..", input)
        abort('invalid EM_ASM input |' + code + '|. Please use EM_ASM(..code..) (no quotes) or EM_ASM({ ..code($0).. }, input) (to input values)');
      }
    }
    return Runtime.asmConstCache[code] = eval('(function(' + args.join(',') + '){ ' + code + ' })'); // new Function does not allow upvars in node
  },
  warnOnce: function (text) {
    if (!Runtime.warnOnce.shown) Runtime.warnOnce.shown = {};
    if (!Runtime.warnOnce.shown[text]) {
      Runtime.warnOnce.shown[text] = 1;
      Module.printErr(text);
    }
  },
  funcWrappers: {},
  getFuncWrapper: function (func, sig) {
    assert(sig);
    if (!Runtime.funcWrappers[func]) {
      Runtime.funcWrappers[func] = function dynCall_wrapper() {
        return Runtime.dynCall(sig, func, arguments);
      };
    }
    return Runtime.funcWrappers[func];
  },
  UTF8Processor: function () {
    var buffer = [];
    var needed = 0;
    this.processCChar = function (code) {
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
    this.processJSString = function processJSString(string) {
      string = unescape(encodeURIComponent(string));
      var ret = [];
      for (var i = 0; i < string.length; i++) {
        ret.push(string.charCodeAt(i));
      }
      return ret;
    }
  },
  stackAlloc: function (size) { var ret = STACKTOP;STACKTOP = (STACKTOP + size)|0;STACKTOP = (((STACKTOP)+7)&-8); return ret; },
  staticAlloc: function (size) { var ret = STATICTOP;STATICTOP = (STATICTOP + size)|0;STATICTOP = (((STATICTOP)+7)&-8); return ret; },
  dynamicAlloc: function (size) { var ret = DYNAMICTOP;DYNAMICTOP = (DYNAMICTOP + size)|0;DYNAMICTOP = (((DYNAMICTOP)+7)&-8); if (DYNAMICTOP >= TOTAL_MEMORY) enlargeMemory();; return ret; },
  alignMemory: function (size,quantum) { var ret = size = Math.ceil((size)/(quantum ? quantum : 8))*(quantum ? quantum : 8); return ret; },
  makeBigInt: function (low,high,unsigned) { var ret = (unsigned ? ((+((low>>>0)))+((+((high>>>0)))*(+4294967296))) : ((+((low>>>0)))+((+((high|0)))*(+4294967296)))); return ret; },
  GLOBAL_BASE: 8,
  QUANTUM_SIZE: 4,
  __dummy__: 0
}


Module['Runtime'] = Runtime;









//========================================
// Runtime essentials
//========================================

var __THREW__ = 0; // Used in checking for thrown exceptions.

var ABORT = false; // whether we are quitting the application. no code should run after this. set in exit() and abort()
var EXITSTATUS = 0;

var undef = 0;
// tempInt is used for 32-bit signed values or smaller. tempBigInt is used
// for 32-bit unsigned values or more than 32 bits. TODO: audit all uses of tempInt
var tempValue, tempInt, tempBigInt, tempInt2, tempBigInt2, tempPair, tempBigIntI, tempBigIntR, tempBigIntS, tempBigIntP, tempBigIntD, tempDouble, tempFloat;
var tempI64, tempI64b;
var tempRet0, tempRet1, tempRet2, tempRet3, tempRet4, tempRet5, tempRet6, tempRet7, tempRet8, tempRet9;

function assert(condition, text) {
  if (!condition) {
    abort('Assertion failed: ' + text);
  }
}

var globalScope = this;

// C calling interface. A convenient way to call C functions (in C files, or
// defined with extern "C").
//
// Note: LLVM optimizations can inline and remove functions, after which you will not be
//       able to call them. Closure can also do so. To avoid that, add your function to
//       the exports using something like
//
//         -s EXPORTED_FUNCTIONS='["_main", "_myfunc"]'
//
// @param ident      The name of the C function (note that C++ functions will be name-mangled - use extern "C")
// @param returnType The return type of the function, one of the JS types 'number', 'string' or 'array' (use 'number' for any C pointer, and
//                   'array' for JavaScript arrays and typed arrays; note that arrays are 8-bit).
// @param argTypes   An array of the types of arguments for the function (if there are no arguments, this can be ommitted). Types are as in returnType,
//                   except that 'array' is not possible (there is no way for us to know the length of the array)
// @param args       An array of the arguments to the function, as native JS values (as in returnType)
//                   Note that string arguments will be stored on the stack (the JS string will become a C string on the stack).
// @return           The return value, as a native JS value (as in returnType)
function ccall(ident, returnType, argTypes, args) {
  return ccallFunc(getCFunc(ident), returnType, argTypes, args);
}
Module["ccall"] = ccall;

// Returns the C function with a specified identifier (for C++, you need to do manual name mangling)
function getCFunc(ident) {
  try {
    var func = Module['_' + ident]; // closure exported function
    if (!func) func = eval('_' + ident); // explicit lookup
  } catch(e) {
  }
  assert(func, 'Cannot call unknown function ' + ident + ' (perhaps LLVM optimizations or closure removed it?)');
  return func;
}

// Internal function that does a C call using a function, not an identifier
function ccallFunc(func, returnType, argTypes, args) {
  var stack = 0;
  function toC(value, type) {
    if (type == 'string') {
      if (value === null || value === undefined || value === 0) return 0; // null string
      value = intArrayFromString(value);
      type = 'array';
    }
    if (type == 'array') {
      if (!stack) stack = Runtime.stackSave();
      var ret = Runtime.stackAlloc(value.length);
      writeArrayToMemory(value, ret);
      return ret;
    }
    return value;
  }
  function fromC(value, type) {
    if (type == 'string') {
      return Pointer_stringify(value);
    }
    assert(type != 'array');
    return value;
  }
  var i = 0;
  var cArgs = args ? args.map(function(arg) {
    return toC(arg, argTypes[i++]);
  }) : [];
  var ret = fromC(func.apply(null, cArgs), returnType);
  if (stack) Runtime.stackRestore(stack);
  return ret;
}

// Returns a native JS wrapper for a C function. This is similar to ccall, but
// returns a function you can call repeatedly in a normal way. For example:
//
//   var my_function = cwrap('my_c_function', 'number', ['number', 'number']);
//   alert(my_function(5, 22));
//   alert(my_function(99, 12));
//
function cwrap(ident, returnType, argTypes) {
  var func = getCFunc(ident);
  return function() {
    return ccallFunc(func, returnType, argTypes, Array.prototype.slice.call(arguments));
  }
}
Module["cwrap"] = cwrap;

// Sets a value in memory in a dynamic way at run-time. Uses the
// type data. This is the same as makeSetValue, except that
// makeSetValue is done at compile-time and generates the needed
// code then, whereas this function picks the right code at
// run-time.
// Note that setValue and getValue only do *aligned* writes and reads!
// Note that ccall uses JS types as for defining types, while setValue and
// getValue need LLVM types ('i8', 'i32') - this is a lower-level operation
function setValue(ptr, value, type, noSafe) {
  type = type || 'i8';
  if (type.charAt(type.length-1) === '*') type = 'i32'; // pointers are 32-bit
    switch(type) {
      case 'i1': HEAP8[(ptr)]=value; break;
      case 'i8': HEAP8[(ptr)]=value; break;
      case 'i16': HEAP16[((ptr)>>1)]=value; break;
      case 'i32': HEAP32[((ptr)>>2)]=value; break;
      case 'i64': (tempI64 = [value>>>0,(tempDouble=value,(+(Math_abs(tempDouble))) >= (+1) ? (tempDouble > (+0) ? ((Math_min((+(Math_floor((tempDouble)/(+4294967296)))), (+4294967295)))|0)>>>0 : (~~((+(Math_ceil((tempDouble - +(((~~(tempDouble)))>>>0))/(+4294967296))))))>>>0) : 0)],HEAP32[((ptr)>>2)]=tempI64[0],HEAP32[(((ptr)+(4))>>2)]=tempI64[1]); break;
      case 'float': HEAPF32[((ptr)>>2)]=value; break;
      case 'double': HEAPF64[((ptr)>>3)]=value; break;
      default: abort('invalid type for setValue: ' + type);
    }
}
Module['setValue'] = setValue;

// Parallel to setValue.
function getValue(ptr, type, noSafe) {
  type = type || 'i8';
  if (type.charAt(type.length-1) === '*') type = 'i32'; // pointers are 32-bit
    switch(type) {
      case 'i1': return HEAP8[(ptr)];
      case 'i8': return HEAP8[(ptr)];
      case 'i16': return HEAP16[((ptr)>>1)];
      case 'i32': return HEAP32[((ptr)>>2)];
      case 'i64': return HEAP32[((ptr)>>2)];
      case 'float': return HEAPF32[((ptr)>>2)];
      case 'double': return HEAPF64[((ptr)>>3)];
      default: abort('invalid type for setValue: ' + type);
    }
  return null;
}
Module['getValue'] = getValue;

var ALLOC_NORMAL = 0; // Tries to use _malloc()
var ALLOC_STACK = 1; // Lives for the duration of the current function call
var ALLOC_STATIC = 2; // Cannot be freed
var ALLOC_DYNAMIC = 3; // Cannot be freed except through sbrk
var ALLOC_NONE = 4; // Do not allocate
Module['ALLOC_NORMAL'] = ALLOC_NORMAL;
Module['ALLOC_STACK'] = ALLOC_STACK;
Module['ALLOC_STATIC'] = ALLOC_STATIC;
Module['ALLOC_DYNAMIC'] = ALLOC_DYNAMIC;
Module['ALLOC_NONE'] = ALLOC_NONE;

// allocate(): This is for internal use. You can use it yourself as well, but the interface
//             is a little tricky (see docs right below). The reason is that it is optimized
//             for multiple syntaxes to save space in generated code. So you should
//             normally not use allocate(), and instead allocate memory using _malloc(),
//             initialize it with setValue(), and so forth.
// @slab: An array of data, or a number. If a number, then the size of the block to allocate,
//        in *bytes* (note that this is sometimes confusing: the next parameter does not
//        affect this!)
// @types: Either an array of types, one for each byte (or 0 if no type at that position),
//         or a single type which is used for the entire block. This only matters if there
//         is initial data - if @slab is a number, then this does not matter at all and is
//         ignored.
// @allocator: How to allocate memory, see ALLOC_*
function allocate(slab, types, allocator, ptr) {
  var zeroinit, size;
  if (typeof slab === 'number') {
    zeroinit = true;
    size = slab;
  } else {
    zeroinit = false;
    size = slab.length;
  }

  var singleType = typeof types === 'string' ? types : null;

  var ret;
  if (allocator == ALLOC_NONE) {
    ret = ptr;
  } else {
    ret = [_malloc, Runtime.stackAlloc, Runtime.staticAlloc, Runtime.dynamicAlloc][allocator === undefined ? ALLOC_STATIC : allocator](Math.max(size, singleType ? 1 : types.length));
  }

  if (zeroinit) {
    var ptr = ret, stop;
    assert((ret & 3) == 0);
    stop = ret + (size & ~3);
    for (; ptr < stop; ptr += 4) {
      HEAP32[((ptr)>>2)]=0;
    }
    stop = ret + size;
    while (ptr < stop) {
      HEAP8[((ptr++)|0)]=0;
    }
    return ret;
  }

  if (singleType === 'i8') {
    if (slab.subarray || slab.slice) {
      HEAPU8.set(slab, ret);
    } else {
      HEAPU8.set(new Uint8Array(slab), ret);
    }
    return ret;
  }

  var i = 0, type, typeSize, previousType;
  while (i < size) {
    var curr = slab[i];

    if (typeof curr === 'function') {
      curr = Runtime.getFunctionIndex(curr);
    }

    type = singleType || types[i];
    if (type === 0) {
      i++;
      continue;
    }

    if (type == 'i64') type = 'i32'; // special case: we have one i32 here, and one i32 later

    setValue(ret+i, curr, type);

    // no need to look up size unless type changes, so cache it
    if (previousType !== type) {
      typeSize = Runtime.getNativeTypeSize(type);
      previousType = type;
    }
    i += typeSize;
  }

  return ret;
}
Module['allocate'] = allocate;

function Pointer_stringify(ptr, /* optional */ length) {
  // TODO: use TextDecoder
  // Find the length, and check for UTF while doing so
  var hasUtf = false;
  var t;
  var i = 0;
  while (1) {
    t = HEAPU8[(((ptr)+(i))|0)];
    if (t >= 128) hasUtf = true;
    else if (t == 0 && !length) break;
    i++;
    if (length && i == length) break;
  }
  if (!length) length = i;

  var ret = '';

  if (!hasUtf) {
    var MAX_CHUNK = 1024; // split up into chunks, because .apply on a huge string can overflow the stack
    var curr;
    while (length > 0) {
      curr = String.fromCharCode.apply(String, HEAPU8.subarray(ptr, ptr + Math.min(length, MAX_CHUNK)));
      ret = ret ? ret + curr : curr;
      ptr += MAX_CHUNK;
      length -= MAX_CHUNK;
    }
    return ret;
  }

  var utf8 = new Runtime.UTF8Processor();
  for (i = 0; i < length; i++) {
    t = HEAPU8[(((ptr)+(i))|0)];
    ret += utf8.processCChar(t);
  }
  return ret;
}
Module['Pointer_stringify'] = Pointer_stringify;

// Given a pointer 'ptr' to a null-terminated UTF16LE-encoded string in the emscripten HEAP, returns
// a copy of that string as a Javascript String object.
function UTF16ToString(ptr) {
  var i = 0;

  var str = '';
  while (1) {
    var codeUnit = HEAP16[(((ptr)+(i*2))>>1)];
    if (codeUnit == 0)
      return str;
    ++i;
    // fromCharCode constructs a character from a UTF-16 code unit, so we can pass the UTF16 string right through.
    str += String.fromCharCode(codeUnit);
  }
}
Module['UTF16ToString'] = UTF16ToString;

// Copies the given Javascript String object 'str' to the emscripten HEAP at address 'outPtr',
// null-terminated and encoded in UTF16LE form. The copy will require at most (str.length*2+1)*2 bytes of space in the HEAP.
function stringToUTF16(str, outPtr) {
  for(var i = 0; i < str.length; ++i) {
    // charCodeAt returns a UTF-16 encoded code unit, so it can be directly written to the HEAP.
    var codeUnit = str.charCodeAt(i); // possibly a lead surrogate
    HEAP16[(((outPtr)+(i*2))>>1)]=codeUnit;
  }
  // Null-terminate the pointer to the HEAP.
  HEAP16[(((outPtr)+(str.length*2))>>1)]=0;
}
Module['stringToUTF16'] = stringToUTF16;

// Given a pointer 'ptr' to a null-terminated UTF32LE-encoded string in the emscripten HEAP, returns
// a copy of that string as a Javascript String object.
function UTF32ToString(ptr) {
  var i = 0;

  var str = '';
  while (1) {
    var utf32 = HEAP32[(((ptr)+(i*4))>>2)];
    if (utf32 == 0)
      return str;
    ++i;
    // Gotcha: fromCharCode constructs a character from a UTF-16 encoded code (pair), not from a Unicode code point! So encode the code point to UTF-16 for constructing.
    if (utf32 >= 0x10000) {
      var ch = utf32 - 0x10000;
      str += String.fromCharCode(0xD800 | (ch >> 10), 0xDC00 | (ch & 0x3FF));
    } else {
      str += String.fromCharCode(utf32);
    }
  }
}
Module['UTF32ToString'] = UTF32ToString;

// Copies the given Javascript String object 'str' to the emscripten HEAP at address 'outPtr',
// null-terminated and encoded in UTF32LE form. The copy will require at most (str.length+1)*4 bytes of space in the HEAP,
// but can use less, since str.length does not return the number of characters in the string, but the number of UTF-16 code units in the string.
function stringToUTF32(str, outPtr) {
  var iChar = 0;
  for(var iCodeUnit = 0; iCodeUnit < str.length; ++iCodeUnit) {
    // Gotcha: charCodeAt returns a 16-bit word that is a UTF-16 encoded code unit, not a Unicode code point of the character! We must decode the string to UTF-32 to the heap.
    var codeUnit = str.charCodeAt(iCodeUnit); // possibly a lead surrogate
    if (codeUnit >= 0xD800 && codeUnit <= 0xDFFF) {
      var trailSurrogate = str.charCodeAt(++iCodeUnit);
      codeUnit = 0x10000 + ((codeUnit & 0x3FF) << 10) | (trailSurrogate & 0x3FF);
    }
    HEAP32[(((outPtr)+(iChar*4))>>2)]=codeUnit;
    ++iChar;
  }
  // Null-terminate the pointer to the HEAP.
  HEAP32[(((outPtr)+(iChar*4))>>2)]=0;
}
Module['stringToUTF32'] = stringToUTF32;

function demangle(func) {
  try {
    // Special-case the entry point, since its name differs from other name mangling.
    if (func == 'Object._main' || func == '_main') {
      return 'main()';
    }
    if (typeof func === 'number') func = Pointer_stringify(func);
    if (func[0] !== '_') return func;
    if (func[1] !== '_') return func; // C function
    if (func[2] !== 'Z') return func;
    switch (func[3]) {
      case 'n': return 'operator new()';
      case 'd': return 'operator delete()';
    }
    var i = 3;
    // params, etc.
    var basicTypes = {
      'v': 'void',
      'b': 'bool',
      'c': 'char',
      's': 'short',
      'i': 'int',
      'l': 'long',
      'f': 'float',
      'd': 'double',
      'w': 'wchar_t',
      'a': 'signed char',
      'h': 'unsigned char',
      't': 'unsigned short',
      'j': 'unsigned int',
      'm': 'unsigned long',
      'x': 'long long',
      'y': 'unsigned long long',
      'z': '...'
    };
    function dump(x) {
      //return;
      if (x) Module.print(x);
      Module.print(func);
      var pre = '';
      for (var a = 0; a < i; a++) pre += ' ';
      Module.print (pre + '^');
    }
    var subs = [];
    function parseNested() {
      i++;
      if (func[i] === 'K') i++; // ignore const
      var parts = [];
      while (func[i] !== 'E') {
        if (func[i] === 'S') { // substitution
          i++;
          var next = func.indexOf('_', i);
          var num = func.substring(i, next) || 0;
          parts.push(subs[num] || '?');
          i = next+1;
          continue;
        }
        if (func[i] === 'C') { // constructor
          parts.push(parts[parts.length-1]);
          i += 2;
          continue;
        }
        var size = parseInt(func.substr(i));
        var pre = size.toString().length;
        if (!size || !pre) { i--; break; } // counter i++ below us
        var curr = func.substr(i + pre, size);
        parts.push(curr);
        subs.push(curr);
        i += pre + size;
      }
      i++; // skip E
      return parts;
    }
    var first = true;
    function parse(rawList, limit, allowVoid) { // main parser
      limit = limit || Infinity;
      var ret = '', list = [];
      function flushList() {
        return '(' + list.join(', ') + ')';
      }
      var name;
      if (func[i] === 'N') {
        // namespaced N-E
        name = parseNested().join('::');
        limit--;
        if (limit === 0) return rawList ? [name] : name;
      } else {
        // not namespaced
        if (func[i] === 'K' || (first && func[i] === 'L')) i++; // ignore const and first 'L'
        var size = parseInt(func.substr(i));
        if (size) {
          var pre = size.toString().length;
          name = func.substr(i + pre, size);
          i += pre + size;
        }
      }
      first = false;
      if (func[i] === 'I') {
        i++;
        var iList = parse(true);
        var iRet = parse(true, 1, true);
        ret += iRet[0] + ' ' + name + '<' + iList.join(', ') + '>';
      } else {
        ret = name;
      }
      paramLoop: while (i < func.length && limit-- > 0) {
        //dump('paramLoop');
        var c = func[i++];
        if (c in basicTypes) {
          list.push(basicTypes[c]);
        } else {
          switch (c) {
            case 'P': list.push(parse(true, 1, true)[0] + '*'); break; // pointer
            case 'R': list.push(parse(true, 1, true)[0] + '&'); break; // reference
            case 'L': { // literal
              i++; // skip basic type
              var end = func.indexOf('E', i);
              var size = end - i;
              list.push(func.substr(i, size));
              i += size + 2; // size + 'EE'
              break;
            }
            case 'A': { // array
              var size = parseInt(func.substr(i));
              i += size.toString().length;
              if (func[i] !== '_') throw '?';
              i++; // skip _
              list.push(parse(true, 1, true)[0] + ' [' + size + ']');
              break;
            }
            case 'E': break paramLoop;
            default: ret += '?' + c; break paramLoop;
          }
        }
      }
      if (!allowVoid && list.length === 1 && list[0] === 'void') list = []; // avoid (void)
      return rawList ? list : ret + flushList();
    }
    return parse();
  } catch(e) {
    return func;
  }
}

function demangleAll(text) {
  return text.replace(/__Z[\w\d_]+/g, function(x) { var y = demangle(x); return x === y ? x : (x + ' [' + y + ']') });
}

function stackTrace() {
  var stack = new Error().stack;
  return stack ? demangleAll(stack) : '(no stack trace available)'; // Stack trace is not available at least on IE10 and Safari 6.
}

// Memory management

var PAGE_SIZE = 4096;
function alignMemoryPage(x) {
  return (x+4095)&-4096;
}

var HEAP;
var HEAP8, HEAPU8, HEAP16, HEAPU16, HEAP32, HEAPU32, HEAPF32, HEAPF64;

var STATIC_BASE = 0, STATICTOP = 0, staticSealed = false; // static area
var STACK_BASE = 0, STACKTOP = 0, STACK_MAX = 0; // stack area
var DYNAMIC_BASE = 0, DYNAMICTOP = 0; // dynamic area handled by sbrk

function enlargeMemory() {
  abort('Cannot enlarge memory arrays in asm.js. Either (1) compile with -s TOTAL_MEMORY=X with X higher than the current value ' + TOTAL_MEMORY + ', or (2) set Module.TOTAL_MEMORY before the program runs.');
}

var TOTAL_STACK = Module['TOTAL_STACK'] || 5242880;
var TOTAL_MEMORY = Module['TOTAL_MEMORY'] || 16777216;
var FAST_MEMORY = Module['FAST_MEMORY'] || 2097152;

var totalMemory = 4096;
while (totalMemory < TOTAL_MEMORY || totalMemory < 2*TOTAL_STACK) {
  if (totalMemory < 16*1024*1024) {
    totalMemory *= 2;
  } else {
    totalMemory += 16*1024*1024
  }
}
if (totalMemory !== TOTAL_MEMORY) {
  Module.printErr('increasing TOTAL_MEMORY to ' + totalMemory + ' to be more reasonable');
  TOTAL_MEMORY = totalMemory;
}

// Initialize the runtime's memory
// check for full engine support (use string 'subarray' to avoid closure compiler confusion)
assert(typeof Int32Array !== 'undefined' && typeof Float64Array !== 'undefined' && !!(new Int32Array(1)['subarray']) && !!(new Int32Array(1)['set']),
       'Cannot fallback to non-typed array case: Code is too specialized');

var buffer = new ArrayBuffer(TOTAL_MEMORY);
HEAP8 = new Int8Array(buffer);
HEAP16 = new Int16Array(buffer);
HEAP32 = new Int32Array(buffer);
HEAPU8 = new Uint8Array(buffer);
HEAPU16 = new Uint16Array(buffer);
HEAPU32 = new Uint32Array(buffer);
HEAPF32 = new Float32Array(buffer);
HEAPF64 = new Float64Array(buffer);

// Endianness check (note: assumes compiler arch was little-endian)
HEAP32[0] = 255;
assert(HEAPU8[0] === 255 && HEAPU8[3] === 0, 'Typed arrays 2 must be run on a little-endian system');

Module['HEAP'] = HEAP;
Module['HEAP8'] = HEAP8;
Module['HEAP16'] = HEAP16;
Module['HEAP32'] = HEAP32;
Module['HEAPU8'] = HEAPU8;
Module['HEAPU16'] = HEAPU16;
Module['HEAPU32'] = HEAPU32;
Module['HEAPF32'] = HEAPF32;
Module['HEAPF64'] = HEAPF64;

function callRuntimeCallbacks(callbacks) {
  while(callbacks.length > 0) {
    var callback = callbacks.shift();
    if (typeof callback == 'function') {
      callback();
      continue;
    }
    var func = callback.func;
    if (typeof func === 'number') {
      if (callback.arg === undefined) {
        Runtime.dynCall('v', func);
      } else {
        Runtime.dynCall('vi', func, [callback.arg]);
      }
    } else {
      func(callback.arg === undefined ? null : callback.arg);
    }
  }
}

var __ATPRERUN__  = []; // functions called before the runtime is initialized
var __ATINIT__    = []; // functions called during startup
var __ATMAIN__    = []; // functions called when main() is to be run
var __ATEXIT__    = []; // functions called during shutdown
var __ATPOSTRUN__ = []; // functions called after the runtime has exited

var runtimeInitialized = false;

function preRun() {
  // compatibility - merge in anything from Module['preRun'] at this time
  if (Module['preRun']) {
    if (typeof Module['preRun'] == 'function') Module['preRun'] = [Module['preRun']];
    while (Module['preRun'].length) {
      addOnPreRun(Module['preRun'].shift());
    }
  }
  callRuntimeCallbacks(__ATPRERUN__);
}

function ensureInitRuntime() {
  if (runtimeInitialized) return;
  runtimeInitialized = true;
  callRuntimeCallbacks(__ATINIT__);
}

function preMain() {
  callRuntimeCallbacks(__ATMAIN__);
}

function exitRuntime() {
  callRuntimeCallbacks(__ATEXIT__);
}

function postRun() {
  // compatibility - merge in anything from Module['postRun'] at this time
  if (Module['postRun']) {
    if (typeof Module['postRun'] == 'function') Module['postRun'] = [Module['postRun']];
    while (Module['postRun'].length) {
      addOnPostRun(Module['postRun'].shift());
    }
  }
  callRuntimeCallbacks(__ATPOSTRUN__);
}

function addOnPreRun(cb) {
  __ATPRERUN__.unshift(cb);
}
Module['addOnPreRun'] = Module.addOnPreRun = addOnPreRun;

function addOnInit(cb) {
  __ATINIT__.unshift(cb);
}
Module['addOnInit'] = Module.addOnInit = addOnInit;

function addOnPreMain(cb) {
  __ATMAIN__.unshift(cb);
}
Module['addOnPreMain'] = Module.addOnPreMain = addOnPreMain;

function addOnExit(cb) {
  __ATEXIT__.unshift(cb);
}
Module['addOnExit'] = Module.addOnExit = addOnExit;

function addOnPostRun(cb) {
  __ATPOSTRUN__.unshift(cb);
}
Module['addOnPostRun'] = Module.addOnPostRun = addOnPostRun;

// Tools

// This processes a JS string into a C-line array of numbers, 0-terminated.
// For LLVM-originating strings, see parser.js:parseLLVMString function
function intArrayFromString(stringy, dontAddNull, length /* optional */) {
  var ret = (new Runtime.UTF8Processor()).processJSString(stringy);
  if (length) {
    ret.length = length;
  }
  if (!dontAddNull) {
    ret.push(0);
  }
  return ret;
}
Module['intArrayFromString'] = intArrayFromString;

function intArrayToString(array) {
  var ret = [];
  for (var i = 0; i < array.length; i++) {
    var chr = array[i];
    if (chr > 0xFF) {
      chr &= 0xFF;
    }
    ret.push(String.fromCharCode(chr));
  }
  return ret.join('');
}
Module['intArrayToString'] = intArrayToString;

// Write a Javascript array to somewhere in the heap
function writeStringToMemory(string, buffer, dontAddNull) {
  var array = intArrayFromString(string, dontAddNull);
  var i = 0;
  while (i < array.length) {
    var chr = array[i];
    HEAP8[(((buffer)+(i))|0)]=chr;
    i = i + 1;
  }
}
Module['writeStringToMemory'] = writeStringToMemory;

function writeArrayToMemory(array, buffer) {
  for (var i = 0; i < array.length; i++) {
    HEAP8[(((buffer)+(i))|0)]=array[i];
  }
}
Module['writeArrayToMemory'] = writeArrayToMemory;

function writeAsciiToMemory(str, buffer, dontAddNull) {
  for (var i = 0; i < str.length; i++) {
    HEAP8[(((buffer)+(i))|0)]=str.charCodeAt(i);
  }
  if (!dontAddNull) HEAP8[(((buffer)+(str.length))|0)]=0;
}
Module['writeAsciiToMemory'] = writeAsciiToMemory;

function unSign(value, bits, ignore, sig) {
  if (value >= 0) {
    return value;
  }
  return bits <= 32 ? 2*Math.abs(1 << (bits-1)) + value // Need some trickery, since if bits == 32, we are right at the limit of the bits JS uses in bitshifts
                    : Math.pow(2, bits)         + value;
}
function reSign(value, bits, ignore, sig) {
  if (value <= 0) {
    return value;
  }
  var half = bits <= 32 ? Math.abs(1 << (bits-1)) // abs is needed if bits == 32
                        : Math.pow(2, bits-1);
  if (value >= half && (bits <= 32 || value > half)) { // for huge values, we can hit the precision limit and always get true here. so don't do that
                                                       // but, in general there is no perfect solution here. With 64-bit ints, we get rounding and errors
                                                       // TODO: In i64 mode 1, resign the two parts separately and safely
    value = -2*half + value; // Cannot bitshift half, as it may be at the limit of the bits JS uses in bitshifts
  }
  return value;
}

// check for imul support, and also for correctness ( https://bugs.webkit.org/show_bug.cgi?id=126345 )
if (!Math['imul'] || Math['imul'](0xffffffff, 5) !== -5) Math['imul'] = function imul(a, b) {
  var ah  = a >>> 16;
  var al = a & 0xffff;
  var bh  = b >>> 16;
  var bl = b & 0xffff;
  return (al*bl + ((ah*bl + al*bh) << 16))|0;
};
Math.imul = Math['imul'];


var Math_abs = Math.abs;
var Math_cos = Math.cos;
var Math_sin = Math.sin;
var Math_tan = Math.tan;
var Math_acos = Math.acos;
var Math_asin = Math.asin;
var Math_atan = Math.atan;
var Math_atan2 = Math.atan2;
var Math_exp = Math.exp;
var Math_log = Math.log;
var Math_sqrt = Math.sqrt;
var Math_ceil = Math.ceil;
var Math_floor = Math.floor;
var Math_pow = Math.pow;
var Math_imul = Math.imul;
var Math_fround = Math.fround;
var Math_min = Math.min;

// A counter of dependencies for calling run(). If we need to
// do asynchronous work before running, increment this and
// decrement it. Incrementing must happen in a place like
// PRE_RUN_ADDITIONS (used by emcc to add file preloading).
// Note that you can add dependencies in preRun, even though
// it happens right before run - run will be postponed until
// the dependencies are met.
var runDependencies = 0;
var runDependencyWatcher = null;
var dependenciesFulfilled = null; // overridden to take different actions when all run dependencies are fulfilled

function addRunDependency(id) {
  runDependencies++;
  if (Module['monitorRunDependencies']) {
    Module['monitorRunDependencies'](runDependencies);
  }
}
Module['addRunDependency'] = addRunDependency;
function removeRunDependency(id) {
  runDependencies--;
  if (Module['monitorRunDependencies']) {
    Module['monitorRunDependencies'](runDependencies);
  }
  if (runDependencies == 0) {
    if (runDependencyWatcher !== null) {
      clearInterval(runDependencyWatcher);
      runDependencyWatcher = null;
    }
    if (dependenciesFulfilled) {
      var callback = dependenciesFulfilled;
      dependenciesFulfilled = null;
      callback(); // can add another dependenciesFulfilled
    }
  }
}
Module['removeRunDependency'] = removeRunDependency;

Module["preloadedImages"] = {}; // maps url to image data
Module["preloadedAudios"] = {}; // maps url to audio data


var memoryInitializer = null;

// === Body ===



STATIC_BASE = 8;

STATICTOP = STATIC_BASE + 21640;


/* global initializers */ __ATINIT__.push({ func: function() { runPostSets() } });





















var _stdout;
var _stdout=_stdout=allocate([0,0,0,0,0,0,0,0], "i8", ALLOC_STATIC);;
var _stdin;
var _stdin=_stdin=allocate([0,0,0,0,0,0,0,0], "i8", ALLOC_STATIC);;
var _stderr;
var _stderr=_stderr=allocate([0,0,0,0,0,0,0,0], "i8", ALLOC_STATIC);;














































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































































/* memory initializer */ allocate([56,4,0,0,192,71,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,10,3,3,3,3,3,3,3,3,3,3,3,3,4,3,3,11,6,6,6,5,8,8,8,8,8,8,8,8,8,8,8,0,1,2,3,5,8,7,1,1,1,4,6,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,232,65,0,0,0,0,0,0,0,0,0,0,1,0,255,255,0,0,0,0,0,0,0,0,26,0,0,0,8,0,0,0,24,0,0,0,216,2,0,0,56,80,0,0,1,0,0,0,0,0,0,0,80,82,0,0,22,0,0,0,168,43,0,0,8,39,0,0,240,34,0,0,104,31,0,0,8,81,0,0,144,78,0,0,56,76,0,0,24,74,0,0,0,72,0,0,72,71,0,0,152,70,0,0,208,69,0,0,248,68,0,0,40,68,0,0,136,67,0,0,192,66,0,0,56,66,0,0,80,65,0,0,200,64,0,0,48,64,0,0,144,63,0,0,48,63,0,0,208,62,0,0,240,61,0,0,96,61,0,0,240,60,0,0,136,60,0,0,168,59,0,0,224,58,0,0,48,58,0,0,64,57,0,0,208,56,0,0,152,56,0,0,72,56,0,0,192,55,0,0,64,55,0,0,232,54,0,0,56,54,0,0,128,53,0,0,232,52,0,0,104,52,0,0,16,52,0,0,168,51,0,0,80,51,0,0,200,50,0,0,240,49,0,0,136,49,0,0,24,49,0,0,120,48,0,0,48,48,0,0,168,47,0,0,88,47,0,0,216,46,0,0,0,0,0,0,0,0,0,0,112,49,0,0,1,0,0,0,0,0,0,0,40,45,0,0,30,0,0,0,8,3,0,0,112,77,0,0,1,0,0,0,0,0,0,0,104,75,0,0,24,0,0,0,0,0,0,0,120,54,0,0,1,0,0,0,0,0,0,0,32,49,0,0,132,0,0,0,96,3,0,0,184,71,0,0,1,0,0,0,0,0,0,0,160,71,0,0,106,0,0,0,0,0,0,0,112,37,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,1,0,0,88,28,0,0,0,0,0,0,12,0,0,0,26,0,0,0,72,4,0,0,128,69,0,0,1,0,0,0,0,0,0,0,136,68,0,0,80,0,0,0,72,3,0,0,232,70,0,0,1,0,0,0,0,0,0,0,48,70,0,0,14,0,0,0,216,3,0,0,224,80,0,0,1,0,0,0,0,0,0,0,48,78,0,0,6,0,0,0,8,30,0,0,184,33,0,0,1,0,0,0,0,0,0,0,16,38,0,0,68,0,0,0,120,3,0,0,56,34,0,0,1,0,0,0,0,0,0,0,48,31,0,0,12,0,0,0,96,4,0,0,56,49,0,0,1,0,0,0,0,0,0,0,184,44,0,0,50,0,0,0,120,4,0,0,248,75,0,0,1,0,0,0,0,0,0,0,168,72,0,0,72,0,0,0,32,4,0,0,184,69,0,0,2,0,0,0,0,0,0,0,16,63,0,0,0,0,0,0,240,3,0,0,96,38,0,0,2,0,0,0,0,0,0,0,16,63,0,0,0,0,0,0,208,4,0,0,184,56,0,0,2,0,0,0,0,0,0,0,16,63,0,0,0,0,0,0,24,20,0,0,240,70,0,0,3,0,0,0,0,0,0,0,192,17,0,0,176,67,0,0,1,0,0,0,0,0,0,0,160,71,0,0,52,0,0,0,96,17,0,0,248,39,0,0,1,0,0,0,0,0,0,0,184,44,0,0,84,0,0,0,184,4,0,0,224,71,0,0,1,0,0,0,0,0,0,0,168,72,0,0,54,0,0,0,0,0,0,0,240,67,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,5,0,0,8,71,0,0,1,0,0,0,0,0,0,0,136,38,0,0,10,0,0,0,232,4,0,0,240,51,0,0,1,0,0,0,0,0,0,0,48,47,0,0,98,0,0,0,144,3,0,0,128,42,0,0,1,0,0,0,0,0,0,0,16,38,0,0,96,0,0,0,144,17,0,0,120,70,0,0,1,0,0,0,0,0,0,0,48,31,0,0,124,0,0,0,0,0,0,0,0,0,0,0,255,255,255,255,1,0,0,0,0,0,0,0,255,255,255,255,2,0,0,0,1,0,0,0,255,255,255,255,3,0,0,0,1,0,0,0,255,255,255,255,4,0,0,0,0,0,0,0,255,255,255,255,5,0,0,0,0,0,0,0,15,0,0,0,6,0,0,0,0,0,0,0,255,255,255,255,7,0,0,0,0,0,0,0,7,0,0,0,8,0,0,0,0,0,0,0,8,0,0,0,9,0,0,0,0,0,0,0,24,0,0,0,10,0,0,0,0,0,0,0,9,0,0,0,11,0,0,0,0,0,0,0,25,0,0,0,12,0,0,0,0,0,0,0,10,0,0,0,13,0,0,0,0,0,0,0,26,0,0,0,14,0,0,0,0,0,0,0,0,0,0,0,15,0,0,0,0,0,0,0,22,0,0,0,16,0,0,0,0,0,0,0,1,0,0,0,17,0,0,0,0,0,0,0,23,0,0,0,18,0,0,0,0,0,0,0,3,0,0,0,19,0,0,0,0,0,0,0,4,0,0,0,20,0,0,0,0,0,0,0,11,0,0,0,21,0,0,0,0,0,0,0,27,0,0,0,22,0,0,0,0,0,0,0,5,0,0,0,23,0,0,0,0,0,0,0,6,0,0,0,24,0,0,0,0,0,0,0,12,0,0,0,25,0,0,0,0,0,0,0,28,0,0,0,26,0,0,0,0,0,0,0,21,0,0,0,27,0,0,0,0,0,0,0,2,0,0,0,28,0,0,0,0,0,0,0,255,255,255,255,29,0,0,0,1,0,0,0,255,255,255,255,30,0,0,0,0,0,0,0,255,255,255,255,31,0,0,0,0,0,0,0,255,255,255,255,32,0,0,0,0,0,0,0,255,255,255,255,33,0,0,0,0,0,0,0,255,255,255,255,34,0,0,0,1,0,0,0,255,255,255,255,35,0,0,0,1,0,0,0,255,255,255,255,36,0,0,0,1,0,0,0,255,255,255,255,37,0,0,0,1,0,0,0,255,255,255,255,38,0,0,0,0,0,0,0,255,255,255,255,39,0,0,0,0,0,0,0,255,255,255,255,40,0,0,0,0,0,0,0,255,255,255,255,41,0,0,0,1,0,0,0,255,255,255,255,42,0,0,0,0,0,0,0,255,255,255,255,43,0,0,0,0,0,0,0,13,0,0,0,44,0,0,0,0,0,0,0,18,0,0,0,45,0,0,0,0,0,0,0,14,0,0,0,46,0,0,0,0,0,0,0,19,0,0,0,47,0,0,0,0,0,0,0,255,255,255,255,48,0,0,0,1,0,0,0,255,255,255,255,49,0,0,0,0,0,0,0,20,0,0,0,50,0,0,0,0,0,0,0,255,255,255,255,51,0,0,0,1,0,0,0,255,255,255,255,52,0,0,0,1,0,0,0,255,255,255,255,0,0,0,0,0,0,0,0,144,66,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,56,63,0,0,128,48,0,0,56,48,0,0,184,47,0,0,96,47,0,0,232,46,0,0,160,46,0,0,32,46,0,0,216,45,0,0,80,45,0,0,176,44,0,0,16,44,0,0,160,43,0,0,24,43,0,0,176,42,0,0,72,42,0,0,232,41,0,0,128,48,0,0,104,41,0,0,216,40,0,0,104,40,0,0,240,39,0,0,88,39,0,0,0,39,0,0,128,38,0,0,32,38,0,0,208,37,0,0,144,37,0,0,72,37,0,0,0,0,0,0,0,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,1,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,2,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,3,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,4,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,5,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,6,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,7,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,8,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,9,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,10,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,11,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,12,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,13,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,14,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,15,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,16,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,17,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,18,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,19,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,20,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,21,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,22,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,23,0,0,0,1,0,0,0,4,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,24,0,0,0,3,0,0,0,5,0,0,0,1,0,0,0,4,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,25,0,0,0,6,0,0,0,0,0,0,0,14,0,0,0,15,0,0,0,8,0,0,0,2,0,0,0,9,0,0,0,26,0,0,0,2,0,0,0,0,0,0,0,1,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,27,0,0,0,1,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,28,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,29,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,30,0,0,0,4,0,0,0,0,0,0,0,8,0,0,0,9,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,31,0,0,0,4,0,0,0,0,0,0,0,8,0,0,0,9,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,32,0,0,0,5,0,0,0,0,0,0,0,10,0,0,0,8,0,0,0,9,0,0,0,2,0,0,0,255,255,255,255,33,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,34,0,0,0,6,0,0,0,0,0,0,0,1,0,0,0,2,0,0,0,1,0,0,0,1,0,0,0,4,0,0,0,35,0,0,0,6,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,10,0,0,0,36,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,37,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,0,0,255,255,255,255,255,255,255,255,38,0,0,0,3,0,0,0,0,0,0,0,12,0,0,0,1,0,0,0,255,255,255,255,255,255,255,255,0,0,0,0,39,0,0,0,3,0,0,0,0,0,0,0,1,0,0,0,13,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,40,0,0,0,3,0,0,0,0,0,0,0,11,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,41,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,10,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,42,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,10,0,0,0,1,0,0,0,255,255,255,255,255,255,255,255,43,0,0,0,2,0,0,0,0,0,0,0,4,0,0,0,10,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,44,0,0,0,1,0,0,0,3,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,45,0,0,0,4,0,0,0,0,0,0,0,4,0,0,0,10,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,46,0,0,0,2,0,0,0,0,0,0,0,1,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,47,0,0,0,1,0,0,0,19,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,48,0,0,0,2,0,0,0,0,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,49,0,0,0,4,0,0,0,0,0,0,0,17,0,0,0,8,0,0,0,16,0,0,0,255,255,255,255,255,255,255,255,50,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,8,0,0,0,18,0,0,0,255,255,255,255,255,255,255,255,51,0,0,0,3,0,0,0,0,0,0,0,10,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,52,0,0,0,3,0,0,0,0,0,0,0,10,0,0,0,1,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,53,0,0,0,3,0,0,0,0,0,0,0,10,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,54,0,0,0,3,0,0,0,1,0,0,0,11,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,55,0,0,0,4,0,0,0,0,0,0,0,1,0,0,0,10,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,56,0,0,0,4,0,0,0,0,0,0,0,8,0,0,0,20,0,0,0,2,0,0,0,255,255,255,255,255,255,255,255,57,0,0,0,1,0,0,0,3,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,88,45,0,0,208,70,0,0,160,63,0,0,144,57,0,0,144,52,0,0,192,47,0,0,40,43,0,0,152,38,0,0,112,34,0,0,64,31,0,0,232,80,0,0,72,78,0,0,0,76,0,0,224,73,0,0,232,71,0,0,48,71,0,0,128,70,0,0,192,69,0,0,232,68,0,0,24,68,0,0,120,67,0,0,176,66,0,0,32,66,0,0,72,65,0,0,192,64,0,0,32,64,0,0,128,63,0,0,24,63,0,0,192,62,0,0,216,61,0,0,72,61,0,0,224,60,0,0,120,60,0,0,152,59,0,0,200,58,0,0,32,58,0,0,48,57,0,0,192,56,0,0,136,56,0,0,56,56,0,0,176,55,0,0,48,55,0,0,216,54,0,0,40,54,0,0,120,53,0,0,224,52,0,0,96,52,0,0,0,52,0,0,152,51,0,0,64,51,0,0,176,50,0,0,216,49,0,0,120,49,0,0,8,49,0,0,104,48,0,0,24,48,0,0,152,47,0,0,72,47,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,0,0,0,0,0,0,0,0,0,0,0,120,17,0,0,8,36,0,0,1,0,0,0,0,0,0,0,192,31,0,0,18,0,0,0,208,22,0,0,136,81,0,0,1,0,0,0,0,0,0,0,16,79,0,0,128,0,0,0,192,20,0,0,168,69,0,0,1,0,0,0,0,0,0,0,176,68,0,0,118,0,0,0,40,20,0,0,128,66,0,0,1,0,0,0,0,0,0,0,160,71,0,0,82,0,0,0,168,17,0,0,72,67,0,0,1,0,0,0,0,0,0,0,80,82,0,0,92,0,0,0,0,0,0,0,240,47,0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,112,28,0,0,16,0,0,0,6,0,0,0,12,0,0,0,32,9,13,10,0,0,0,0,1,0,0,0,0,0,0,0,255,255,255,255,255,255,255,255,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,96,31,0,0,8,68,0,0,192,61,0,0,40,56,0,0,136,55,0,0,104,50,0,0,232,45,0,0,224,40,0,0,248,36,0,0,144,32,0,0,136,72,0,0,0,0,0,0,105,102,0,0,0,0,0,0,192,78,0,0,0,0,0,0,100,111,0,0,0,0,0,0,200,66,0,0,0,0,0,0,118,97,114,0,0,0,0,0,248,60,0,0,0,0,0,0,102,111,114,0,0,0,0,0,80,55,0,0,0,0,0,0,116,114,121,0,0,0,0,0,248,49,0,0,0,0,0,0,99,97,115,101,0,0,0,0,208,45,0,0,0,0,0,0,101,108,115,101,0,0,0,0,208,40,0,0,0,0,0,0,116,114,117,101,0,0,0,0,208,36,0,0,0,0,0,0,101,108,105,102,0,0,0,0,88,32,0,0,0,0,0,0,115,101,108,102,0,0,0,0,240,81,0,0,0,0,0,0,101,110,117,109,0,0,0,0,232,79,0,0,0,0,0,0,119,104,105,108,101,0,0,0,16,77,0,0,0,0,0,0,114,97,105,115,101,0,0,0,48,75,0,0,0,0,0,0,102,97,108,115,101,0,0,0,120,72,0,0,0,0,0,0,109,97,116,99,104,0,0,0,152,71,0,0,0,0,0,0,98,114,101,97,107,0,0,0,216,70,0,0,0,0,0,0,99,108,97,115,115,0,0,0,40,70,0,0,0,0,0,0,100,101,102,105,110,101,0,0,120,69,0,0,0,0,0,0,114,101,116,117,114,110,0,0,128,68,0,0,0,0,0,0,101,120,99,101,112,116,0,0,168,67,0,0,0,0,0,0,105,109,112,111,114,116,0,0,64,67,0,0,0,0,0,0,112,114,105,118,97,116,101,0,112,66,0,0,0,0,0,0,95,95,102,105,108,101,95,95,192,65,0,0,0,0,0,0,95,95,108,105,110,101,95,95,208,64,0,0,0,0,0,0,112,114,111,116,101,99,116,101,120,64,0,0,0,0,0,0,99,111,110,116,105,110,117,101,176,63,0,0,0,0,0,0,95,95,102,117,110,99,116,105,40,22,0,0,80,72,0,0,1,0,0,0,0,0,0,0,120,71,0,0,8,0,0,0,120,25,0,0,80,70,0,0,3,0,0,0,0,0,0,0,112,20,0,0,208,65,0,0,1,0,0,0,0,0,0,0,224,64,0,0,64,0,0,0,88,20,0,0,208,63,0,0,1,0,0,0,0,0,0,0,224,64,0,0,44,0,0,0,232,21,0,0,72,63,0,0,1,0,0,0,0,0,0,0,224,64,0,0,42,0,0,0,64,20,0,0,216,63,0,0,1,0,0,0,0,0,0,0,224,64,0,0,76,0,0,0,0,0,0,0,168,68,0,0,3,0,0,0,0,0,0,0,0,0,0,0,208,74,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,136,28,0,0,0,0,0,0,18,0,0,0,0,0,0,0,40,27,0,0,248,67,0,0,1,0,0,0,0,0,0,0,80,67,0,0,86,0,0,0,112,30,0,0,208,75,0,0,3,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,136,25,0,0,248,62,0,0,1,0,0,0,0,0,0,0,80,82,0,0,94,0,0,0,0,0,0,0,216,65,0,0,0,0,0,0,0,0,0,0,1,0,2,0,0,0,0,0,160,28,0,0,72,0,0,0,14,0,0,0,20,0,0,0,16,28,0,0,200,70,0,0,1,0,0,0,0,0,0,0,0,70,0,0,78,0,0,0,12,0,0,0,62,0,0,0,22,0,0,0,80,0,0,0,34,0,0,0,20,0,0,0,74,0,0,0,50,0,0,0,32,0,0,0,54,0,0,0,56,0,0,0,52,0,0,0,38,0,0,0,30,0,0,0,24,0,0,0,82,0,0,0,78,0,0,0,6,0,0,0,2,0,0,0,48,0,0,0,36,0,0,0,46,0,0,0,8,0,0,0,42,0,0,0,44,0,0,0,58,0,0,0,28,0,0,0,0,0,0,0,6,0,0,0,8,0,0,0,10,0,0,0,12,0,0,0,7,0,0,0,9,0,0,0,11,0,0,0,13,0,0,0,0,20,0,0,160,76,0,0,1,0,0,0,0,0,0,0,144,74,0,0,100,0,0,0,0,0,0,0,160,82,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,3,0,0,0,13,0,0,0,255,255,255,255,13,0,0,0,13,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,4,0,0,0,14,0,0,0,255,255,255,255,14,0,0,0,14,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,17,0,0,0,17,0,0,0,255,255,255,255,17,0,0,0,17,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,17,0,0,0,19,0,0,0,19,0,0,0,255,255,255,255,19,0,0,0,19,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,19,0,0,0,20,0,0,0,20,0,0,0,255,255,255,255,20,0,0,0,20,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,20,0,0,0,21,0,0,0,21,0,0,0,255,255,255,255,21,0,0,0,21,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,21,0,0,0,22,0,0,0,22,0,0,0,255,255,255,255,22,0,0,0,22,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,22,0,0,0,18,0,0,0,18,0,0,0,255,255,255,255,18,0,0,0,18,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,18,0,0,0,5,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,6,0,0,0,15,0,0,0,255,255,255,255,15,0,0,0,15,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,7,0,0,0,16,0,0,0,255,255,255,255,16,0,0,0,16,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,8,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,9,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,10,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,11,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,12,0,0,0,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,0,0,0,0,168,64,0,0,0,0,0,0,0,0,0,0,1,0,255,255,0,0,0,0,0,0,0,0,14,0,0,0,10,0,0,0,4,0,0,0,136,20,0,0,152,69,0,0,3,0,0,0,0,0,0,0,64,27,0,0,144,62,0,0,1,0,0,0,0,0,0,0,144,61,0,0,60,0,0,0,0,0,1,0,2,0,3,0,4,0,5,0,6,0,7,0,8,0,9,0,10,0,11,0,12,0,13,0,14,0,15,0,16,0,17,0,18,0,19,0,20,0,21,0,22,0,23,0,24,0,25,0,26,0,27,0,28,0,29,0,30,0,31,0,32,0,33,0,34,0,35,0,36,0,37,0,38,0,39,0,40,0,41,0,42,0,43,0,44,0,45,0,46,0,47,0,48,0,49,0,50,0,51,0,52,0,53,0,54,0,55,0,56,0,57,0,58,0,59,0,60,0,61,0,62,0,63,0,64,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,4,4,4,4,4,255,255,255,255,255,255,255,255,255,255,255,88,27,0,0,152,66,0,0,1,0,0,0,0,0,0,0,240,65,0,0,122,0,0,0,248,27,0,0,40,61,0,0,1,0,0,0,0,0,0,0,176,60,0,0,70,0,0,0,40,28,0,0,248,64,0,0,1,0,0,0,0,0,0,0,144,64,0,0,2,0,0,0,0,0,0,0,136,66,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,208,28,0,0,0,0,0,0,10,0,0,0,18,0,0,0,176,27,0,0,200,49,0,0,1,0,0,0,0,0,0,0,144,45,0,0,66,0,0,0,200,27,0,0,152,40,0,0,1,0,0,0,0,0,0,0,32,55,0,0,120,0,0,0,224,27,0,0,200,36,0,0,1,0,0,0,0,0,0,0,48,32,0,0,62,0,0,0,0,0,0,0,232,81,0,0,1,0,0,0,0,0,0,0,224,79,0,0,28,0,0,0,192,29,0,0,32,60,0,0,1,0,0,0,0,0,0,0,136,68,0,0,40,0,0,0,64,29,0,0,48,69,0,0,1,0,0,0,0,0,0,0,88,68,0,0,110,0,0,0,64,28,0,0,224,63,0,0,1,0,0,0,0,0,0,0,80,63,0,0,48,0,0,0,88,29,0,0,8,63,0,0,1,0,0,0,0,0,0,0,152,62,0,0,108,0,0,0,208,1,0,0,184,32,0,0,1,0,0,0,0,0,0,0,80,82,0,0,126,0,0,0,168,3,0,0,32,43,0,0,1,0,0,0,0,0,0,0,136,38,0,0,102,0,0,0,240,2,0,0,152,65,0,0,1,0,0,0,0,0,0,0,248,59,0,0,134,0,0,0,192,3,0,0,48,60,0,0,1,0,0,0,0,0,0,0,160,54,0,0,116,0,0,0,192,2,0,0,104,60,0,0,1,0,0,0,0,0,0,0,192,54,0,0,34,0,0,0,152,27,0,0,216,60,0,0,1,0,0,0,0,0,0,0,32,55,0,0,32,0,0,0,0,0,0,0,128,55,0,0,1,0,0,0,0,0,0,0,72,50,0,0,90,0,0,0,0,0,0,0,96,82,0,0,1,0,0,0,0,0,0,0,184,67,0,0,36,0,0,0,0,0,0,0,104,82,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,29,0,0,0,0,0,0,16,0,0,0,0,0,0,0,216,29,0,0,160,67,0,0,1,0,0,0,0,0,0,0,248,66,0,0,130,0,0,0,168,29,0,0,176,61,0,0,1,0,0,0,0,0,0,0,48,61,0,0,114,0,0,0,95,112,137,0,255,9,47,15,10,0,0,0,100,0,0,0,232,3,0,0,16,39,0,0,160,134,1,0,64,66,15,0,128,150,152,0,0,225,245,5,216,20,0,0,224,77,0,0,3,0,0,0,0,0,0,0,240,29,0,0,208,60,0,0,1,0,0,0,0,0,0,0,56,60,0,0,136,0,0,0,0,0,0,0,64,59,0,0,1,0,0,0,0,0,0,0,160,71,0,0,4,0,0,0,0,0,0,0,104,66,0,0,1,0,0,0,0,0,0,0,128,65,0,0,88,0,0,0,0,0,0,0,72,59,0,0,1,0,0,0,0,0,0,0,184,58,0,0,20,0,0,0,152,29,0,0,8,31,0,0,1,0,0,0,0,0,0,0,152,80,0,0,104,0,0,0,0,0,0,0,8,61,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,232,28,0,0,0,0,0,0,4,0,0,0,26,0,0,0,0,0,0,0,224,65,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,184,28,0,0,0,0,0,0,18,0,0,0,0,0,0,0,8,0,0,0,144,72,0,0,3,0,0,0,0,0,0,0,0,0,0,0,224,70,0,0,2,0,0,0,0,0,0,0,192,63,0,0,0,0,0,0,0,0,0,0,0,65,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,4,0,0,0,0,0,0,64,0,0,0,2,0,0,0,34,0,0,0,69,120,112,101,99,116,101,100,32,39,97,115,39,44,32,110,111,116,32,39,37,115,39,46,10,0,0,0,0,0,0,0,39,37,115,39,32,105,115,32,110,111,116,32,97,32,118,97,108,105,100,32,101,120,99,101,112,116,105,111,110,32,99,108,97,115,115,46,10,0,0,0,99,97,108,108,116,114,97,99,101,0,0,0,0,0,0,0,93,0,0,0,0,0,0,0,69,120,99,101,112,116,105,111,110,0,0,0,0,0,0,0,91,65,93,40,108,105,115,116,91,65,93,41,58,65,0,0,114,105,103,104,116,32,115,104,105,102,116,32,40,62,62,41,0,0,0,0,0,0,0,0,37,115,37,115,37,115,0,0,69,114,114,111,114,0,0,0,125,0,0,0,0,0,0,0,32,32,32,32,110,111,32,102,105,108,101,32,39,37,115,37,115,37,115,39,10,0,0,0,94,86,10,0,0,0,0,0,110,111,32,98,117,105,108,116,105,110,32,109,111,100,117,108,101,32,39,37,115,39,10,0,39,37,115,39,32,97,102,116,101,114,32,39,101,108,115,101,39,46,10,0,0,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,44,32,104,97,115,104,91,65,44,32,66,93,46,46,46,41,58,32,104,97,115,104,91,65,44,32,66,93,0,0,0,67,97,110,110,111,116,32,105,109,112,111,114,116,32,39,37,115,39,58,10,0,0,0,0,94,73,124,95,95,95,95,91,94,86,93,32,61,32,0,0,42,99,108,111,115,117,114,101,0,0,0,0,0,0,0,0,46,115,111,0,0,0,0,0,40,115,116,114,105,110,103,44,32,115,116,114,105,110,103,41,58,102,105,108,101,0,0,0,94,73,124,95,95,95,95,91,37,100,93,32,61,32,0,0,115,101,108,102,0,0,0,0,77,105,115,115,105,110,103,32,114,101,116,117,114,110,32,115,116,97,116,101,109,101,110,116,32,97,116,32,101,110,100,32,111,102,32,102,117,110,99,116,105,111,110,46,10,0,0,0,73,79,69,114,114,111,114,0,46,108,108,121,0,0,0,0,63,10,0,0,0,0,0,0,42,32,37,115,10,0,0,0,97,115,0,0,0,0,0,0,117,112,112,101,114,0,0,0,94,73,124,95,95,95,95,91,40,37,100,41,32,37,115,37,115,93,32,61,32,0,0,0,77,97,116,99,104,32,112,97,116,116,101,114,110,32,110,111,116,32,101,120,104,97,117,115,116,105,118,101,46,32,84,104,101,32,102,111,108,108,111,119,105,110,103,32,99,97,115,101,40,115,41,32,97,114,101,32,109,105,115,115,105,110,103,58,10,0,0,0,0,0,0,0,85,110,116,101,114,109,105,110,97,116,101,100,32,98,108,111,99,107,40,115,41,32,97,116,32,101,110,100,32,111,102,32,102,105,108,101,46,10,0,0,32,40,112,114,111,116,101,99,116,101,100,41,0,0,0,0,67,97,110,110,111,116,32,99,108,111,115,101,32,111,118,101,114,32,97,32,118,97,114,32,111,102,32,97,110,32,105,110,99,111,109,112,108,101,116,101,32,116,121,112,101,32,105,110,32,116,104,105,115,32,115,99,111,112,101,46,10,0,0,0,39,125,39,32,111,117,116,115,105,100,101,32,111,102,32,97,32,98,108,111,99,107,46,10,0,0,0,0,0,0,0,0,115,104,111,119,0,0,0,0,32,40,112,114,105,118,97,116,101,41,0,0,0,0,0,0,70,117,110,99,116,105,111,110,32,110,101,101,100,101,100,32,116,111,32,114,101,116,117,114,110,32,97,32,118,97,108,117,101,44,32,98,117,116,32,100,105,100,32,110,111,116,46,10,0,0,0,0,0,0,0,0,32,61,62,32,0,0,0,0,95,95,105,109,112,111,114,116,95,95,0,0,0,0,0,0,94,73,124,95,95,95,95,32,70,114,111,109,32,37,115,58,10,0,0,0,0,0,0,0,115,104,105,102,116,0,0,0,83,117,98,115,99,114,105,112,116,32,97,115,115,105,103,110,32,110,111,116,32,97,108,108,111,119,101,100,32,111,110,32,116,121,112,101,32,115,116,114,105,110,103,46,10,0,0,0,108,101,102,116,32,115,104,105,102,116,32,40,60,60,41,0,80,97,99,107,97,103,101,32,39,37,115,39,32,104,97,115,32,97,108,114,101,97,100,121,32,98,101,101,110,32,105,109,112,111,114,116,101,100,32,105,110,32,116,104,105,115,32,102,105,108,101,46,10,0,0,0,94,73,40,99,105,114,99,117,108,97,114,41,10,0,0,0,76,101,102,116,32,115,105,100,101,32,111,102,32,37,115,32,105,115,32,110,111,116,32,97,115,115,105,103,110,97,98,108,101,46,10,0,0,0,0,0,44,0,0,0,0,0,0,0,67,97,110,110,111,116,32,105,109,112,111,114,116,32,97,32,102,105,108,101,32,104,101,114,101,46,10,0,0,0,0,0,32,40,37,115,32,91,98,117,105,108,116,105,110,93,41,10,0,0,0,0,0,0,0,0,67,97,110,110,111,116,32,97,115,115,105,103,110,32,116,121,112,101,32,39,94,84,39,32,116,111,32,116,121,112,101,32,39,94,84,39,46,10,0,0,83,117,98,115,99,114,105,112,116,32,105,110,100,101,120,32,37,100,32,105,115,32,111,117,116,32,111,102,32,114,97,110,103,101,46,10,0,0,0,0,67,97,110,110,111,116,32,117,115,101,32,97,32,99,108,97,115,115,32,112,114,111,112,101,114,116,121,32,111,117,116,115,105,100,101,32,111,102,32,97,32,99,111,110,115,116,114,117,99,116,111,114,46,10,0,0,39,37,115,39,32,119,105,116,104,111,117,116,32,39,105,102,39,46,10,0,0,0,0,0,32,40,37,115,32,102,114,111,109,32,108,105,110,101,32,37,100,41,10,0,0,0,0,0,73,110,118,97,108,105,100,32,99,111,109,112,111,117,110,100,32,111,112,58,32,37,115,46,10,0,0,0,0,0,0,0,109,101,114,103,101,0,0,0,67,108,97,115,115,32,112,114,111,112,101,114,116,105,101,115,32,109,117,115,116,32,115,116,97,114,116,32,119,105,116,104,32,64,46,10,0,0,0,0,94,73,37,115,40,94,84,41,32,37,115,32,114,101,103,105,115,116,101,114,32,35,37,100,0,0,0,0,0,0,0,0,67,97,110,110,111,116,32,110,101,115,116,32,97,110,32,97,115,115,105,103,110,109,101,110,116,32,119,105,116,104,105,110,32,97,110,32,101,120,112,114,101,115,115,105,111,110,46,10,0,0,0,0,0,0,0,0,65,32,109,101,116,104,111,100,32,105,110,32,99,108,97,115,115,32,39,37,115,39,32,97,108,114,101,97,100,121,32,104,97,115,32,116,104,101,32,110,97,109,101,32,39,37,115,39,46,10,0,0,0,0,0,0,111,112,101,110,0,0,0,0,101,108,105,102,0,0,0,0,73,110,118,97,108,105,100,32,111,112,101,114,97,116,105,111,110,58,32,94,84,32,37,115,32,94,84,46,10,0,0,0,70,111,114,109,97,116,69,114,114,111,114,0,0,0,0,0,80,114,111,112,101,114,116,121,32,37,115,32,97,108,114,101,97,100,121,32,101,120,105,115,116,115,32,105,110,32,99,108,97,115,115,32,37,115,46,10,0,0,0,0,0,0,0,0,124,32,32,32,32,32,61,61,61,61,62,32,0,0,0,0,62,62,61,0,0,0,0,0,69,120,112,101,99,116,101,100,32,39,44,39,32,111,114,32,39,41,39,44,32,110,111,116,32,37,115,46,10,0,0,0,115,116,114,105,110,103,0,0,124,32,32,32,32,32,60,45,45,45,45,32,0,0,0,0,91,116,114,121,105,116,93,0,60,60,61,0,0,0,0,0,65,110,32,105,110,105,116,105,97,108,105,122,97,116,105,111,110,32,101,120,112,114,101,115,115,105,111,110,32,105,115,32,114,101,113,117,105,114,101,100,32,104,101,114,101,46,10,0,108,111,99,97,108,0,0,0,47,61,0,0,0,0,0,0,65,32,112,114,111,112,101,114,116,121,32,105,110,32,99,108,97,115,115,32,39,37,115,39,32,97,108,114,101,97,100,121,32,104,97,115,32,116,104,101,32,110,97,109,101,32,39,37,115,39,46,10,0,0,0,0,91,65,93,40,65,41,0,0,103,108,111,98,97,108,0,0,42,61,0,0,0,0,0,0,41,0,0,0,0,0,0,0,73,110,118,97,108,105,100,32,103,101,110,101,114,105,99,32,110,97,109,101,32,40,119,97,110,116,101,100,32,37,115,44,32,103,111,116,32,37,115,41,46,10,0,0,0,0,0,0,115,116,100,105,110,0,0,0,94,73,124,32,32,32,32,32,60,45,45,45,45,32,40,94,84,41,32,94,86,10,0,0,37,61,0,0,0,0,0,0,91,65,93,40,108,105,115,116,91,65,93,44,32,65,41,0,105,110,116,101,103,101,114,32,100,105,118,105,100,101,32,40,47,41,0,0,0,0,0,0,79,110,108,121,32,118,97,114,105,97,110,116,115,32,116,104,97,116,32,116,97,107,101,32,110,111,32,97,114,103,117,109,101,110,116,115,32,99,97,110,32,98,101,32,100,101,102,97,117,108,116,32,97,114,103,117,109,101,110,116,115,46,10,0,37,115,32,91,98,117,105,108,116,105,110,93,10,0,0,0,45,61,0,0,0,0,0,0,41,0,0,0,0,0,0,0,39,37,115,39,32,100,111,101,115,32,110,111,116,32,104,97,118,101,32,97,32,118,97,114,105,97,110,116,32,110,97,109,101,100,32,39,37,115,39,46,10,0,0,0,0,0,0,0,37,115,32,102,114,111,109,32,108,105,110,101,32,37,100,10,0,0,0,0,0,0,0,0,43,61,0,0,0,0,0,0,102,111,114,32,108,111,111,112,32,115,116,101,112,32,99,97,110,110,111,116,32,98,101,32,48,46,10,0,0,0,0,0,39,37,115,39,32,105,115,32,110,111,116,32,97,32,118,97,108,105,100,32,100,101,102,97,117,108,116,32,118,97,108,117,101,32,102,111,114,32,97,32,98,111,111,108,101,97,110,46,10,0,0,0,0,0,0,0,101,108,115,101,0,0,0,0,37,115,58,58,0,0,0,0,73,110,118,97,108,105,100,32,117,116,102,45,56,32,115,101,113,117,101,110,99,101,32,111,110,32,108,105,110,101,32,37,100,46,10,0,0,0,0,0,61,0,0,0,0,0,0,0,114,101,106,101,99,116,0,0,37,115,32,104,97,115,32,97], "i8", ALLOC_NONE, Runtime.GLOBAL_BASE);
/* memory initializer */ allocate([108,114,101,97,100,121,32,98,101,101,110,32,100,101,99,108,97,114,101,100,46,10,0,0,94,73,124,32,32,32,32,32,60,45,45,45,45,32,40,94,84,41,32,0,0,0,0,0,70,105,108,101,115,32,105,110,32,116,97,103,103,101,100,32,109,111,100,101,32,109,117,115,116,32,115,116,97,114,116,32,119,105,116,104,32,39,60,63,108,105,108,121,39,46,10,0,124,62,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,101,105,116,104,101,114,32,39,44,39,32,111,114,32,39,41,39,44,32,110,111,116,32,39,37,115,39,46,10,0,0,112,114,105,110,116,0,0,0,37,100,0,0,0,0,0,0,94,73,124,32,32,32,32,32,45,62,32,124,32,91,37,100,93,32,40,99,97,115,101,58,32,37,115,41,10,0,0,0,60,63,108,105,108,121,0,0,116,114,117,101,0,0,0,0,124,124,0,0,0,0,0,0,75,101,121,69,114,114,111,114,0,0,0,0,0,0,0,0,69,109,112,116,121,32,40,41,32,110,111,116,32,110,101,101,100,101,100,32,102,111,114,32,97,32,100,101,102,105,110,101,46,10,0,0,0,0,0,0,94,73,124,32,32,32,32,32,60,45,45,45,45,32,37,100,10,0,0,0,0,0,0,0,85,110,116,101,114,109,105,110,97,116,101,100,32,109,117,108,116,105,45,108,105,110,101,32,99,111,109,109,101,110,116,32,40,115,116,97,114,116,101,100,32,97,116,32,108,105,110,101,32,37,100,41,46,10,0,0,38,38,0,0,0,0,0,0,40,115,101,108,102,41,0,0,73,110,100,101,120,32,37,100,32,105,115,32,111,117,116,32,111,102,32,114,97,110,103,101,46,10,0,0,0,0,0,0,94,73,124,32,32,32,32,32,45,62,32,124,32,91,37,100,93,10,0,0,0,0,0,0,85,110,116,101,114,109,105,110,97,116,101,100,32,109,117,108,116,105,45,108,105,110,101,32,115,116,114,105,110,103,32,40,115,116,97,114,116,101,100,32,97,116,32,108,105,110,101,32,37,100,41,46,10,0,0,0,33,0,0,0,0,0,0,0,77,117,108,116,105,45,108,105,110,101,32,98,108,111,99,107,32,119,105,116,104,105,110,32,115,105,110,103,108,101,45,108,105,110,101,32,98,108,111,99,107,46,10,0,0,0,0,0,32,116,114,117,101,10,0,0,78,101,119,108,105,110,101,32,105,110,32,115,105,110,103,108,101,45,108,105,110,101,32,115,116,114,105,110,103,46,10,0,94,0,0,0,0,0,0,0,105,110,102,105,110,105,116,121,0,0,0,0,0,0,0,0,67,97,110,110,111,116,32,100,101,102,105,110,101,32,97,32,102,117,110,99,116,105,111,110,32,104,101,114,101,46,10,0,112,114,105,110,116,0,0,0,32,102,97,108,115,101,10,0,73,110,118,97,108,105,100,32,101,115,99,97,112,101,32,115,101,113,117,101,110,99,101,46,10,0,0,0,0,0,0,0,124,0,0,0,0,0,0,0,46,46,46,0,0,0,0,0,69,120,112,101,99,116,101,100,32,101,105,116,104,101,114,32,39,118,97,114,39,32,111,114,32,39,100,101,102,105,110,101,39,44,32,98,117,116,32,103,111,116,32,39,37,115,39,46,10,0,0,0,0,0,0,0,93,32,37,115,0,0,0,0,69,120,112,111,110,101,110,116,32,105,115,32,116,111,111,32,108,97,114,103,101,46,10,0,38,0,0,0,0,0,0,0,117,110,115,104,105,102,116,0,105,110,116,101,103,101,114,32,109,117,108,116,105,112,108,121,32,40,42,41,0,0,0,0,39,37,115,39,32,105,115,32,110,111,116,32,97,108,108,111,119,101,100,32,104,101,114,101,46,0,0,0,0,0,0,0,94,73,124,95,95,95,95,32,91,0,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,97,32,98,97,115,101,32,49,48,32,110,117,109,98,101,114,32,97,102,116,101,114,32,101,120,112,111,110,101,110,116,46,10,0,0,0,0,0,0,62,62,0,0,0,0,0,0,40,0,0,0,0,0,0,0,39,99,111,110,116,105,110,117,101,39,32,110,111,116,32,97,116,32,116,104,101,32,101,110,100,32,111,102,32,97,32,109,117,108,116,105,45,108,105,110,101,32,98,108,111,99,107,46,10,0,0,0,0,0,0,0,94,73,124,10,0,0,0,0,68,111,117,98,108,101,32,118,97,108,117,101,32,105,115,32,116,111,111,32,108,97,114,103,101,46,10,0,0,0,0,0,60,60,0,0,0,0,0,0,67,97,110,110,111,116,32,99,97,115,116,32,97,110,121,32,99,111,110,116,97,105,110,105,110,103,32,116,121,112,101,32,39,94,84,39,32,116,111,32,116,121,112,101,32,39,94,84,39,46,10,0,0,0,0,0,69,120,112,101,99,116,101,100,32,97,32,118,97,108,117,101,44,32,110,111,116,32,39,37,115,39,46,10,0,0,0,0,101,108,105,102,0,0,0,0,94,73,124,95,95,95,95,32,40,108,105,110,101,32,37,100,41,10,0,0,0,0,0,0,73,110,116,101,103,101,114,32,118,97,108,117,101,32,105,115,32,116,111,111,32,108,97,114,103,101,46,10,0,0,0,0,47,0,0,0,0,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,44,32,102,117,110,99,116,105,111,110,40,65,44,32,66,32,61,62,32,98,111,111,108,101,97,110,41,41,58,104,97,115,104,91,65,44,32,66,93,0,0,0,0,0,0,0,0,65,116,116,101,109,112,116,32,116,111,32,117,115,101,32,117,110,105,110,105,116,105,97,108,105,122,101,100,32,118,97,108,117,101,32,39,37,115,39,46,10,0,0,0,0,0,0,0,40,98,111,111,108,101,97,110,41,58,105,110,116,101,103,101,114,0,0,0,0,0,0,0,40,94,84,41,32,0,0,0,37,115,37,115,37,115,0,0,42,0,0,0,0,0,0,0,102,97,115,116,32,97,115,115,105,103,110,0,0,0,0,0,39,115,101,108,102,39,32,109,117,115,116,32,98,101,32,117,115,101,100,32,119,105,116,104,105,110,32,97,32,99,108,97,115,115,46,10,0,0,0,0,40,102,105,108,101,41,58,98,121,116,101,115,116,114,105,110,103,0,0,0,0,0,0,0,108,105,108,121,95,100,121,110,97,108,111,97,100,95,116,97,98,108,101,0,0,0,0,0,94,84,32,37,115,10,0,0,34,0,0,0,0,0,0,0,101,108,115,101,0,0,0,0,37,0,0,0,0,0,0,0,115,121,115,0,0,0,0,0,82,117,110,116,105,109,101,69,114,114,111,114,0,0,0,0,37,115,58,58,37,115,32,100,111,101,115,32,110,111,116,32,101,120,105,115,116,46,10,0,94,84,10,0,0,0,0,0,34,34,34,0,0,0,0,0,33,61,0,0,0,0,0,0,37,115,32,104,97,115,32,110,111,116,32,98,101,101,110,32,100,101,99,108,97,114,101,100,46,10,0,0,0,0,0,0,73,110,118,97,108,105,100,32,98,97,115,101,32,49,48,32,108,105,116,101,114,97,108,32,39,37,115,39,46,10,0,0,10,0,0,0,0,0,0,0,85,110,116,101,114,109,105,110,97,116,101,100,32,108,97,109,98,100,97,32,40,115,116,97,114,116,101,100,32,97,116,32,108,105,110,101,32,37,100,41,46,10,0,0,0,0,0,0,62,61,0,0,0,0,0,0,80,114,111,112,101,114,116,121,32,37,115,32,105,115,32,110,111,116,32,105,110,32,99,108,97,115,115,32,37,115,46,10,0,0,0,0,0,0,0,0,86,97,108,117,101,58,32,0,101,110,100,32,111,102,32,102,105,108,101,0,0,0,0,0,62,0,0,0,0,0,0,0,80,114,111,112,101,114,116,105,101,115,32,99,97,110,110,111,116,32,98,101,32,117,115,101,100,32,111,117,116,115,105,100,101,32,111,102,32,97,32,99,108,97,115,115,32,99,111,110,115,116,114,117,99,116,111,114,46,10,0,0,0,0,0,0,40,115,116,114,105,110,103,44,32,97,110,121,46,46,46,41,0,0,0,0,0,0,0,0,114,101,116,117,114,110,32,102,114,111,109,32,118,109,0,0,63,62,0,0,0,0,0,0,60,61,0,0,0,0,0,0,32,40,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,99,108,111,115,105,110,103,32,116,111,107,101,110,32,39,37,115,39,44,32,110,111,116,32,39,37,115,39,46,10,0,108,111,97,100,32,99,108,111,115,117,114,101,0,0,0,0,105,110,118,97,108,105,100,32,116,111,107,101,110,0,0,0,60,0,0,0,0,0,0,0,109,111,100,117,108,111,32,40,37,41,0,0,0,0,0,0,67,108,97,115,115,32,39,37,115,39,32,100,111,101,115,32,110,111,116,32,101,120,105,115,116,46,10,0,0,0,0,0,108,105,115,116,0,0,0,0,73,110,102,105,110,105,116,101,32,108,111,111,112,32,105,110,32,99,111,109,112,97,114,105,115,111,110,46,10,0,0,0,108,111,97,100,32,99,108,97,115,115,32,99,108,111,115,117,114,101,0,0,0,0,0,0,124,62,0,0,0,0,0,0,61,61,0,0,0,0,0,0,63,108,105,108,121,0,0,0,39,94,84,39,32,105,115,32,110,111,116,32,97,32,118,97,108,105,100,32,104,97,115,104,32,107,101,121,46,10,0,0,99,114,101,97,116,101,32,102,117,110,99,116,105,111,110,0,46,46,46,0,0,0,0,0,45,0,0,0,0,0,0,0,70,117,110,99,116,105,111,110,32,99,97,108,108,32,114,101,99,117,114,115,105,111,110,32,108,105,109,105,116,32,114,101,97,99,104,101,100,46,10,0,67,108,97,115,115,32,37,115,32,101,120,112,101,99,116,115,32,37,100,32,116,121,112,101,40,115,41,44,32,98,117,116,32,103,111,116,32,37,100,32,116,121,112,101,40,115,41,46,10,0,0,0,0,0,0,0,39,125,39,32,111,117,116,115,105,100,101,32,111,102,32,97,32,98,108,111,99,107,46,10,0,0,0,0,0,0,0,0,99,114,101,97,116,101,32,99,108,111,115,117,114,101,0,0,64,40,0,0,0,0,0,0,40,105,110,116,101,103,101,114,41,58,100,111,117,98,108,101,0,0,0,0,0,0,0,0,115,101,108,101,99,116,0,0,69,120,112,101,99,116,101,100,32,101,105,116,104,101,114,32,39,61,62,39,32,111,114,32,39,41,39,32,97,102,116,101,114,32,118,97,114,97,114,103,115,46,10,0,0,0,0,0,116,111,95,105,0,0,0,0,115,101,116,32,117,112,118,97,108,117,101,0,0,0,0,0,124,124,0,0,0,0,0,0,76,105,115,116,32,101,108,101,109,101,110,116,115,0,0,0,84,121,112,101,32,39,94,84,39,32,99,97,110,110,111,116,32,104,97,118,101,32,97,32,100,101,102,97,117,108,116,32,118,97,108,117,101,46,10,0,114,101,97,100,108,105,110,101,0,0,0,0,0,0,0,0,103,101,116,32,117,112,118,97,108,117,101,0,0,0,0,0,114,0,0,0,0,0,0,0,124,0,0,0,0,0,0,0,99,97,115,101,0,0,0,0,37,115,32,100,111,32,110,111,116,32,104,97,118,101,32,97,32,99,111,110,115,105,115,116,101,110,116,32,116,121,112,101,46,10,69,120,112,101,99,116,101,100,32,84,121,112,101,58,32,94,84,10,82,101,99,101,105,118,101,100,32,84,121,112,101,58,32,94,84,10,0,0,40,98,121,116,101,115,116,114,105,110,103,44,32,42,115,116,114,105,110,103,41,58,115,116,114,105,110,103,0,0,0,0,86,97,108,117,101,69,114,114,111,114,0,0,0,0,0,0,78,111,110,45,111,112,116,105,111,110,97,108,32,97,114,103,117,109,101,110,116,32,102,111,108,108,111,119,115,32,111,112,116,105,111,110,97,108,32,97,114,103,117,109,101,110,116,46,10,0,0,0,0,0,0,0,118,97,114,105,97,110,116,32,100,101,99,111,109,112,111,115,101,0,0,0,0,0,0,0,38,38,0,0,0,0,0,0,84,121,112,101,32,39,94,84,39,32,105,115,32,110,111,116,32,97,32,118,97,108,105,100,32,104,97,115,104,32,107,101,121,46,10,0,0,0,0,0,69,120,112,101,99,116,101,100,32,101,105,116,104,101,114,32,39,44,39,32,111,114,32,39,93,39,44,32,110,111,116,32,39,37,115,39,46,10,0,0,86,97,108,117,101,32,101,120,99,101,101,100,115,32,97,108,108,111,119,101,100,32,114,97,110,103,101,46,10,0,0,0,109,97,116,99,104,32,100,105,115,112,97,116,99,104,0,0,38,0,0,0,0,0,0,0,72,97,115,104,32,118,97,108,117,101,115,0,0,0,0,0,86,97,114,105,97,110,116,32,116,121,112,101,115,32,110,111,116,32,97,108,108,111,119,101,100,32,105,110,32,97,32,100,101,99,108,97,114,97,116,105,111,110,46,10,0,0,0,0,110,101,119,32,105,110,115,116,97,110,99,101,0,0,0,0,58,58,0,0,0,0,0,0,72,97,115,104,32,107,101,121,115,0,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,97,32,107,101,121,32,61,62,32,118,97,108,117,101,32,112,97,105,114,32,98,101,102,111,114,101,32,39,44,39,46,10,0,0,0,0,0,0,0,112,114,105,110,116,102,109,116,0,0,0,0,0,0,0,0,115,101,116,117,112,32,111,112,116,97,114,103,115,0,0,0,58,0,0,0,0,0,0,0,67,97,110,110,111,116,32,99,114,101,97,116,101,32,97,110,32,101,109,112,116,121,32,116,117,112,108,101,46,10,0,0,93,40,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,97,32,118,97,108,117,101,44,32,110,111,116,32,39,44,39,46,10,0,0,0,0,0,114,97,105,115,101,0,0,0,46,0,0,0,0,0,0,0,67,97,110,110,111,116,32,115,117,98,115,99,114,105,112,116,32,116,121,112,101,32,39,94,84,39,46,10,0,0,0,0,105,110,116,101,103,101,114,32,109,105,110,117,115,32,40,45,41,0,0,0,0,0,0,0,83,104,105,102,116,32,111,110,32,97,110,32,101,109,112,116,121,32,108,105,115,116,46,10,0,0,0,0,0,0,0,0,85,110,101,120,112,101,99,116,101,100,32,116,111,107,101,110,32,37,115,46,10,0,0,0,101,120,99,101,112,116,0,0,97,32,100,111,117,98,108,101,0,0,0,0,0,0,0,0,73,110,100,101,120,32,37,100,32,105,115,32,111,117,116,32,111,102,32,114,97,110,103,101,32,102,111,114,32,94,84,46,10,0,0,0,0,0,0,0,84,97,103,115,32,110,111,116,32,97,108,108,111,119,101,100,32,105,110,32,105,110,99,108,117,100,101,100,32,102,105,108,101,115,46,10,0,0,0,0,85,110,116,101,114,109,105,110,97,116,101,100,32,98,108,111,99,107,40,115,41,32,97,116,32,101,110,100,32,111,102,32,112,97,114,115,105,110,103,46,10,0,0,0,0,0,0,0,112,111,112,32,116,114,121,0,97,110,32,105,110,116,101,103,101,114,0,0,0,0,0,0,116,117,112,108,101,32,115,117,98,115,99,114,105,112,116,115,32,109,117,115,116,32,98,101,32,105,110,116,101,103,101,114,32,108,105,116,101,114,97,108,115,46,10,0,0,0,0,0,32,32,32,32,102,114,111,109,32,37,115,58,37,100,58,32,105,110,32,37,115,37,115,37,115,10,0,0,0,0,0,0,65,116,116,101,109,112,116,32,116,111,32,100,105,118,105,100,101,32,98,121,32,122,101,114,111,46,10,0,0,0,0,0,39,99,111,110,116,105,110,117,101,39,32,117,115,101,100,32,111,117,116,115,105,100,101,32,111,102,32,97,32,108,111,111,112,46,10,0,0,0,0,0,112,117,115,104,32,116,114,121,0,0,0,0,0,0,0,0,97,32,98,121,116,101,115,116,114,105,110,103,0,0,0,0,104,97,115,104,32,105,110,100,101,120,32,115,104,111,117,108,100,32,98,101,32,116,121,112,101,32,39,94,84,39,44,32,110,111,116,32,116,121,112,101,32,39,94,84,39,46,10,0,116,111,95,100,0,0,0,0,32,32,32,32,102,114,111,109,32,91,67,93,58,32,105,110,32,37,115,37,115,37,115,10,0,0,0,0,0,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,41,58,105,110,116,101,103,101,114,0,0,0,0,0,0,40,98,111,111,108,101,97,110,41,58,115,116,114,105,110,103,0,0,0,0,0,0,0,0,115,101,116,32,112,114,111,112,101,114,116,121,0,0,0,0,97,32,115,116,114,105,110,103,0,0,0,0,0,0,0,0,37,115,32,105,110,100,101,120,32,105,115,32,110,111,116,32,97,110,32,105,110,116,101,103,101,114,46,10,0,0,0,0,58,58,0,0,0,0,0,0,91,65,93,40,102,105,108,101,44,32,65,41,0,0,0,0,103,101,116,32,112,114,111,112,101,114,116,121,0,0,0,0,97,32,112,114,111,112,101,114,116,121,32,110,97,109,101,0,116,114,121,0,0,0,0,0,67,97,110,110,111,116,32,99,97,115,116,32,116,121,112,101,32,39,94,84,39,32,116,111,32,116,121,112,101,32,39,94,84,39,46,10,0,0,0,0,101,110,99,111,100,101,0,0,66,97,100,84,121,112,101,99,97,115,116,69,114,114,111,114,0,0,0,0,0,0,0,0,84,114,97,99,101,98,97,99,107,58,10,0,0,0,0,0,103,101,116,32,114,101,97,100,111,110,108,121,0,0,0,0,97,32,108,97,98,101,108,0,73,110,118,97,108,105,100,32,117,115,101,32,111,102,32,117,110,105,110,105,116,105,97,108,105,122,101,100,32,112,114,111,112,101,114,116,121,32,39,64,37,115,39,46,10,0,0,0,32,32,32,32,102,114,111,109,32,37,115,58,37,100,10,0,67,97,110,110,111,116,32,115,112,108,105,116,32,98,121,32,101,109,112,116,121,32,115,116,114,105,110,103,46,10,0,0,73,110,100,101,120,69,114,114,111,114,0,0,0,0,0,0,115,101,116,32,103,108,111,98,97,108,0,0,0,0,0,0,61,62,0,0,0,0,0,0,37,115,58,58,37,115,32,105,115,32,109,97,114,107,101,100,32,37,115,44,32,97,110,100,32,110,111,116,32,97,118,97,105,108,97,98,108,101,32,104,101,114,101,46,10,0,0,0,58,32,37,115,0,0,0,0,103,101,116,32,103,108,111,98,97,108,0,0,0,0,0,0,93,0,0,0,0,0,0,0,112,114,111,116,101,99,116,101,100,0,0,0,0,0,0,0,37,115,58,58,0,0,0,0,115,116,100,101,114,114,0,0,115,101,116,32,105,116,101,109,0,0,0,0,0,0,0,0,93,62,0,0,0,0,0,0,112,114,105,118,97,116,101,0,44,32,0,0,0,0,0,0,85,110,101,120,112,101,99,116,101,100,32,116,111,107,101,110,32,39,37,115,39,46,10,0,73,110,100,101,120,32,37,100,32,105,115,32,116,111,111,32,108,97,114,103,101,32,102,111,114,32,108,105,115,116,32,40,109,97,120,105,109,117,109,58,32,37,100,41,10,0,0,0,103,101,116,32,105,116,101,109,0,0,0,0,0,0,0,0,60,91,0,0,0,0,0,0,85,115,101,32,64,60,110,97,109,101,62,32,116,111,32,103,101,116,47,115,101,116,32,112,114,111,112,101,114,116,105,101,115,44,32,110,111,116,32,115,101,108,102,46,60,110,97,109,101,62,46,10,0,0,0,0,95,95,109,97,105,110,95,95,0,0,0,0,0,0,0,0,105,110,116,101,103,101,114,32,97,100,100,32,40,43,41,0,67,97,110,110,111,116,32,105,110,102,101,114,32,116,121,112,101,32,111,102,32,39,37,115,39,46,10,0,0,0,0,0,32,0,0,0,0,0,0,0,73,110,100,101,120,32,37,100,32,105,115,32,116,111,111,32,115,109,97,108,108,32,102,111,114,32,108,105,115,116,32,40,109,105,110,105,109,117,109,58,32,37,100,41,10,0,0,0,82,101,112,101,97,116,32,99,111,117,110,116,32,109,117,115,116,32,98,101,32,62,61,32,48,32,40,37,100,32,103,105,118,101,110,41,46,10,0,0,102,111,114,32,115,101,116,117,112,0,0,0,0,0,0,0,97,32,108,97,109,98,100,97,0,0,0,0,0,0,0,0,67,108,97,115,115,32,37,115,32,104,97,115,32,110,111,32,109,101,116,104,111,100,32,111,114,32,112,114,111,112,101,114,116,121,32,110,97,109,101,100,32,37,115,46,10,0,0,0,70,111,117,110,100,32,63,62,32,98,117,116,32,110,111,116,32,101,120,112,101,99,116,105,110,103,32,116,97,103,115,46,10,0,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,39,37,115,39,44,32,110,111,116,32,37,115,46,10,0,10,0,0,0,0,0,0,0,91,65,93,40,108,105,115,116,91,65,93,41,0,0,0,0,102,111,114,32,40,105,110,116,101,103,101,114,32,114,97,110,103,101,41,0,0,0,0,0,123,0,0,0,0,0,0,0,67,97,110,110,111,116,32,97,110,111,110,121,109,111,117,115,108,121,32,99,97,108,108,32,114,101,115,117,108,116,105,110,103,32,116,121,112,101,32,39,94,84,39,46,10,0,0,0,76,97,109,98,100,97,32,101,120,112,101,99,116,101,100,32,37,100,32,97,114,103,115,44,32,98,117,116,32,103,111,116,32,48,46,10,0,0,0,0,99,111,110,99,97,116,0,0,99,108,101,97,114,0,0,0,37,37,102,32,105,115,32,110,111,116,32,118,97,108,105,100,32,102,111,114,32,116,121,112,101,32,94,84,46,10,0,0,39,98,114,101,97,107,39,32,117,115,101,100,32,111,117,116,115,105,100,101,32,111,102,32,97,32,108,111,111,112,46,10,0,0,0,0,0,0,0,0,116,121,112,101,99,97,115,116,0,0,0,0,0,0,0,0,61,61,0,0,0,0,0,0,65,114,103,117,109,101,110,116,32,35,37,100,32,116,111,32,37,115,37,115,37,115,32,105,115,32,105,110,118,97,108,105,100,58,10,69,120,112,101,99,116,101,100,32,84,121,112,101,58,32,94,84,10,82,101,99,101,105,118,101,100,32,84,121,112,101,58,32,94,84,10,0,40,105,110,116,101,103,101,114,41,58,115,116,114,105,110,103,0,0,0,0,0,0,0,0,40,108,97,109,98,100,97,41,0,0,0,0,0,0,0,0,101,110,100,115,119,105,116,104,0,0,0,0,0,0,0,0,115,105,122,101,0,0,0,0,91,65,93,40,108,105,115,116,91,65,93,44,32,102,117,110,99,116,105,111,110,40,65,32,61,62,32,98,111,111,108,101,97,110,41,41,58,105,110,116,101,103,101,114,0,0,0,0,116,111,95,115,0,0,0,0,37,108,108,100,0,0,0,0,98,117,105,108,100,32,101,110,117,109,0,0,0,0,0,0,61,0,0,0,0,0,0,0,40,97,110,111,110,121,109,111,117,115,41,0,0,0,0,0,91,108,97,109,98,100,97,93,0,0,0,0,0,0,0,0,40,115,116,114,105,110,103,44,32,115,116,114,105,110,103,41,58,105,110,116,101,103,101,114,0,0,0,0,0,0,0,0,99,111,117,110,116,0,0,0,119,114,105,116,101,0,0,0,98,117,105,108,100,32,104,97,115,104,0,0,0,0,0,0,62,62,61,0,0,0,0,0,102,111,114,0,0,0,0,0,46,0,0,0,0,0,0,0,98,121,116,101,115,116,114,105,110,103,0,0,0,0,0,0,108,105,115,116,91,115,116,114,105,110,103,93,0,0,0,0,102,105,110,100,0,0,0,0,91,65,93,40,108,105,115,116,91,65,93,44,32,105,110,116,101,103,101,114,41,0,0,0,98,117,105,108,100,32,108,105,115,116,47,116,117,112,108,101,0,0,0,0,0,0,0,0,62,62,0,0,0,0,0,0,58,58,0,0,0,0,0,0,47,117,115,114,47,108,111,99,97,108,47,108,105,98,47,108,105,108,121,47,48,95,49,53,47,59,0,0,0,0,0,0,40,115,116,114,105,110,103,44,32,97,110,121,46,46,46,41,58,115,116,114,105,110,103,0,38,97,109,112,59,0,0,0,100,101,108,101,116,101,95,97,116,0,0,0,0,0,0,0,68,105,118,105,115,105,111,110,66,121,90,101,114,111,69,114,114,111,114,0,0,0,0,0,117,110,97,114,121,32,109,105,110,117,115,32,40,45,120,41,0,0,0,0,0,0,0,0,62,61,0,0,0,0,0,0,87,114,111,110,103,32,110,117,109,98,101,114,32,111,102,32,97,114,103,117,109,101,110,116,115,32,116,111,32,37,115,37,115,37,115,32,40,37,115,32,102,111,114,32,37,115,37,115,37,115,41,46,10,0,0,0,73,110,102,105,110,105,116,101,32,108,111,111,112,32,105,110,32,99,111,109,112,97,114,105,115,111,110,46,10,0,0,0,47,117,115,114,47,108,111,99,97,108,47,115,104,97,114,101,47,108,105,108,121,47,48,95,49,53,47,59,47,117,115,114,47,108,111,99,97,108,47,108,105,98,47,108,105,108,121,47,48,95,49,53,47,59,46,47,59,0,0,0,0,0,0,0,102,111,114,109,97,116,0,0,91,65,93,40,108,105,115,116,91,65,93,44,32,102,117,110,99,116,105,111,110,40,65,41,41,58,108,105,115,116,91,65,93,0,0,0,0,0,0,0,117,110,97,114,121,32,110,111,116,32,40,33,120,41,0,0,62,0,0,0,0,0,0,0,46,46,0,0,0,0,0,0,102,97,108,115,101,0,0,0,91,98,117,105,108,116,105,110,93,0,0,0,0,0,0,0,104,116,109,108,101,110,99,111,100,101,0,0,0,0,0,0,101,97,99,104,0,0,0,0,102,105,108,101,0,0,0,0,114,101,116,117,114,110,32,40,110,111,32,118,97,108,117,101,41,0,0,0,0,0,0,0,60,60,61,0,0,0,0,0,43,0,0,0,0,0,0,0,91,0,0,0,0,0,0,0,105,115,97,108,112,104,97,0,91,65,93,40,108,105,115,116,91,65,93,44,32,102,117,110,99,116,105,111,110,40,105,110,116,101,103,101,114,41,41,58,108,105,115,116,91,65,93,0,37,48,51,100,0,0,0,0,114,101,116,117,114,110,32,118,97,108,117,101,0,0,0,0,60,60,0,0,0,0,0,0,37,100,0,0,0,0,0,0,98,111,120,32,97,115,115,105,103,110,0,0,0,0,0,0,95,95,102,117,110,99,116,105,111,110,95,95,0,0,0,0,108,105,115,116,91,115,116,114,105,110,103,93,0,0,0,0,105,115,100,105,103,105,116,0,105,115,97,108,110,117,109,0,101,97,99,104,95,105,110,100,101,120,0,0,0,0,0,0,67,97,110,110,111,116,32,100,101,108,101,116,101,32,102,114,111,109,32,97,110,32,101,109,112,116,121,32,108,105,115,116,46,10,0,0,0,0,0,0,116,114,117,101,0,0,0,0,102,117,110,99,116,105,111,110,32,99,97,108,108,0,0,0,60,61,0,0,0,0,0,0,110,111,110,101,0,0,0,0,39,46,46,39,32,105,115,32,110,111,116,32,97,32,118,97,108,105,100,32,116,111,107,101,110,32,40,101,120,112,101,99,116,101,100,32,49,32,111,114,32,51,32,100,111,116,115,41,46,10,0,0,0,0,0,0,99,111,110,116,105,110,117,101,0,0,0,0,0,0,0,0,38,103,116,59,0,0,0,0,91,65,93,40,105,110,116,101,103,101,114,44,32,65,41,58,108,105,115,116,91,65,93,0,102,117,110,99,116,105,111,110,0,0,0,0,0,0,0,0,102,97,108,115,101,0,0,0,106,117,109,112,32,105,102,0,60,0,0,0,0,0,0,0,112,114,111,116,101,99,116,101,100,0,0,0,0,0,0,0,40,115,116,114,105,110,103,41,58,98,111,111,108,101,97,110,0,0,0,0,0,0,0,0,102,105,108,108,0,0,0,0,97,110,121,0,0,0,0,0,60,37,115,37,115,37,115,32,97,116,32,37,112,62,0,0,37,37,100,32,105,115,32,110,111,116,32,118,97,108,105,100,32,102,111,114,32,116,121,112,101,32,94,84,46,10,0,0,40,102,111,114,32,116,101,109,112,41,0,0,0,0,0,0,106,117,109,112,0,0,0,0,45,61,0,0,0,0,0,0,86,97,114,105,97,110,116,32,37,115,32,115,104,111,117,108,100,32,110,111,116,32,103,101,116,32,97,114,103,115,46,10,0,0,0,0,0,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,41,0,0,0,0,0,0,116,111,95,115,0,0,0,0,73,110,102,105,110,105,116,101,32,108,111,111,112,32,105,110,32,99,111,109,112,97,114,105,115,111,110,46,10,0,0,0,95,95,108,105,110,101,95,95,0,0,0,0,0,0,0,0,105,115,115,112,97,99,101,0,104,97,115,104,0,0,0,0,98,111,111,108,101,97,110,0,116,117,112,108,101,0,0,0,91,65,93,40,108,105,115,116,91,65,93,44,32,65,44,32,102,117,110,99,116,105,111,110,40,65,44,32,65,32,61,62,32,65,41,41,58,65,0,0,40,0,0,0,0,0,0,0,103,114,101,97,116,101,114,32,101,113,117,97,108,32,40,62,61,41,0,0,0,0,0,0,45,0,0,0,0,0,0,0,94,84,32,105,115,32,110,111,116,32,97,32,118,97,108,105,100,32,99,111,110,100,105,116,105,111,110,32,116,121,112,101,46,10,0,0,0,0,0,0,99,108,101,97,114,0,0,0,95,95,102,105,108,101,95,95,0,0,0,0,0,0,0,0,108,115,116,114,105,112,0,0,102,105,108,101,0,0,0,0,42,0,0,0,0,0,0,0,102,111,108,100,0,0,0,0,60,37,115,32,102,105,108,101,32,97,116,32,37,112,62,0,103,114,101,97,116,101,114,32,40,62,41,0,0,0,0,0,43,61,0,0,0,0,0,0,118,97,114,0,0,0,0,0,73,110,118,97,108,105,100,32,99,108,97,115,115,32,39,37,115,39,32,103,105,118,101,110,32,116,111,32,114,97,105,115,101,46,10,0,0,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,44,32,65,41,0,0,0,73,110,118,97,108,105,100,32,117,116,102,45,56,32,115,101,113,117,101,110,99,101,32,102,111,117,110,100,32,105,110,32,98,117,102,102,101,114,46,10,0,0,0,0,0,0,0,0,112,114,105,118,97,116,101,0,108,111,119,101,114,0,0,0,91,65,93,40,108,105,115,116,91,65,93,44,32,105,110,116,101,103,101,114,44,32,65,41,0,0,0,0,0,0,0,0,99,108,111,115,101,100,0,0,108,101,115,115,32,101,113,117,97,108,32,40,60,61,41,0,43,0,0,0,0,0,0,0,69,120,99,101,112,116,105,111,110,0,0,0,0,0,0,0,100,101,108,101,116,101,0,0,105,109,112,111,114,116,0,0,114,115,116,114,105,112,0,0,40,100,111,117,98,108,101,41,58,105,110,116,101,103,101,114,0,0,0,0,0,0,0,0,73,110,102,105,110,105,116,101,32,108,111,111,112,32,105,110,32,99,111,109,112,97,114,105,115,111,110,46,10,0,0,0,63,0,0,0,0,0,0,0,105,110,115,101,114,116,0,0,111,112,101,110,0,0,0,0,83,121,110,116,97,120,69,114,114,111,114,0,0,0,0,0,108,101,115,115,32,40,60,41,0,0,0,0,0,0,0,0,47,61,0,0,0,0,0,0,39,114,97,105,115,101,39,32,101,120,112,114,101,115,115,105,111,110,32,104,97,115,32,110,111,32,118,97,108,117,101,46,10,0,0,0,0,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,44,32,102,117,110,99,116,105,111,110,40,65,44,32,66,41,41,0,0,0,0,0,0,101,120,99,101,112,116,0,0,40,115,116,114,105,110,103,44,32,115,116,114,105,110,103,41,58,98,111,111,108,101,97,110,0,0,0,0,0,0,0,0,73,79,69,114,114,111,114,0,91,65,44,66,93,40,108,105,115,116,91,65,93,44,32,102,117,110,99,116,105,111,110,40,65,32,61,62,32,66,41,41,58,108,105,115,116,91,66,93,0,0,0,0,0,0,0,0,93,62,0,0,0,0,0,0,110,111,116,32,101,113,117,97,108,32,40,33,61,41,0,0,47,0,0,0,0,0,0,0,114,101,116,117,114,110,32,101,120,112,101,99,116,101,100,32,116,121,112,101,32,39,94,84,39,32,98,117,116,32,103,111,116,32,116,121,112,101,32,39,94,84,39,46,10,0,0,0,101,97,99,104,95,112,97,105,114,0,0,0,0,0,0,0,77,111,100,101,32,109,117,115,116,32,115,116,97,114,116,32,119,105,116,104,32,111,110,101,32,111,102,32,39,97,114,119,39,44,32,98,117,116,32,103,111,116,32,39,37,99,39,46,10,0,0,0,0,0,0,0,114,101,116,117,114,110,0,0,115,116,97,114,116,115,119,105,116,104,0,0,0,0,0,0,110,97,110,0,0,0,0,0,70,111,114,109,97,116,69,114,114,111,114,0,0,0,0,0,109,97,112,0,0,0,0,0,60,91,0,0,0,0,0,0,115,116,100,111,117,116,0,0,105,115,32,101,113,117,97,108,32,40,61,61,41,0,0,0,42,61,0,0,0,0,0,0,39,114,101,116,117,114,110,39,32,101,120,112,114,101,115,115,105,111,110,32,104,97,115,32,110,111,32,118,97,108,117,101,46,10,0,0,0,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,44,32,65,41,58,98,111,111,108,101,97,110,0,0,0,37,103,0,0,0,0,0,0,100,101,102,105,110,101,0,0,40,115,116,114,105,110,103,44,32,42,115,116,114,105,110,103,41,58,108,105,115,116,91,115,116,114,105,110,103,93,0,0,75,101,121,69,114,114,111,114,0,0,0,0,0,0,0,0,60,37,115,102,117,110,99,116,105,111,110,32,37,115,37,115,37,115,62,0,0,0,0,0,112,111,112,0,0,0,0,0,100,111,117,98,108,101,32,100,105,118,105,100,101,32,40,47,41,0,0,0,0,0,0,0,42,0,0,0,0,0,0,0,39,114,101,116,117,114,110,39,32,117,115,101,100,32,111,117,116,115,105,100,101,32,111,102,32,97,32,102,117,110,99,116,105,111,110,46,10,0,0,0,104,97,115,95,107,101,121,0,97,115,115,105,103,110,0,0,99,108,97,115,115,0,0,0,97,114,103,118,0,0,0,0,115,112,108,105,116,0,0,0,82,117,110,116,105,109,101,69,114,114,111,114,0,0,0,0,58,58,0,0,0,0,0,0,112,117,115,104,0,0,0,0,80,111,112,32,102,114,111,109,32,97,110,32,101,109,112,116,121,32,108,105,115,116,46,10,0,0,0,0,0,0,0,0,100,111,117,98,108,101,32,109,117,108,116,105,112,108,121,32,40,42,41,0,0,0,0,0,37,61,0,0,0,0,0,0,67,111,110,100,105,116,105,111,110,97,108,32,101,120,112,114,101,115,115,105,111,110,32,104,97,115,32,110,111,32,118,97,108,117,101,46,10,0,0,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,41,58,108,105,115,116,91,65,93,0,0,0,0,0,0,98,114,101,97,107,0,0,0,40,115,116,114,105,110,103,44,32,115,116,114,105,110,103,41,58,115,116,114,105,110,103,0,115,116,114,105,112,0,0,0,86,97,108,117,101,69,114,114,111,114,0,0,0,0,0,0,98,117,105,108,116,45,105,110,32,0,0,0,0,0,0,0,114,101,106,101,99,116,0,0,100,111,117,98,108,101,32,109,105,110,117,115,32,40,45,41,0,0,0,0,0,0,0,0,37,0,0,0,0,0,0,0,37,115,58,37,115,32,102,114,111,109,32,37,115,37,115,37,115,0,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,116,121,112,101,32,39,105,110,116,101,103,101,114,39,44,32,98,117,116,32,103,111,116,32,116,121,112,101,32,39,94,84,39,46,10,0,0,0,0,107,101,121,115,0,0,0,0,70,105,108,101,32,110,111,116,32,111,112,101,110,32,102,111,114,32,114,101,97,100,105,110,103,46,10,0,0,0,0,0,109,97,116,99,104,0,0,0,38,108,116,59,0,0,0,0,105,102,0,0,0,0,0,0,66,97,100,84,121,112,101,99,97,115,116,69,114,114,111,114,0,0,0,0,0,0,0,0,91,65,93,40,108,105,115,116,91,65,93,44,32,102,117,110,99,116,105,111,110,40,65,32,61,62,32,98,111,111,108,101,97,110,41,41,58,108,105,115,116,91,65,93,0,0,0,0,99,108,97,115,115,32,69,120,99,101,112,116,105,111,110,40,109,101,115,115,97,103,101,58,32,115,116,114,105,110,103,41,32,123,10,32,32,32,32,118,97,114,32,64,109,101,115,115,97,103,101,32,61,32,109,101,115,115,97,103,101,10,32,32,32,32,118,97,114,32,64,116,114,97,99,101,98,97,99,107,58,32,108,105,115,116,91,115,116,114,105,110,103,93,32,61,32,91,93,10,125,10,99,108,97,115,115,32,84,97,105,110,116,101,100,91,65,93,40,118,97,108,117,101,58,32,65,41,32,123,10,32,32,32,32,112,114,105,118,97,116,101,32,118,97,114,32,64,118,97,108,117,101,32,61,32,118,97,108,117,101,10,32,32,32,32,100,101,102,105,110,101,32,115,97,110,105,116,105,122,101,91,65,44,32,66,93,40,102,58,32,102,117,110,99,116,105,111,110,40,65,32,61,62,32,66,41,41,58,66,32,123,10,32,32,32,32,32,32,32,32,32,114,101,116,117,114,110,32,102,40,64,118,97,108,117,101,41,10,32,32,32,32,125,10,125,10,0,40,102,111,114,32,115,116,101,112,41,0,0,0,0,0,0,100,111,117,98,108,101,32,97,100,100,32,40,43,41,0,0,91,97,112,105,93,0,0,0,78,111,116,32,101,110,111,117,103,104,32,97,114,103,115,32,102,111,114,32,112,114,105,110,116,102,109,116,46,10,0,0,33,61,0,0,0,0,0,0,91,100,121,110,97,108,111,97,100,93,0,0,0,0,0,0,58,58,0,0,0,0,0,0,99,108,97,115,115,32,37,115,40,109,115,103,58,32,115,116,114,105,110,103,41,32,60,32,69,120,99,101,112,116,105,111,110,40,109,115,103,41,32,123,32,125,10,0,0,0,0,0,77,97,116,99,104,32,101,120,112,114,101,115,115,105,111,110,32,105,115,32,110,111,116,32,97,110,32,101,110,117,109,32,118,97,108,117,101,46,10,0,91,65,44,32,66,93,40,104,97,115,104,91,65,44,32,66,93,44,32,65,44,32,66,41,58,66,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,39,119,104,105,108,101,39,44,32,110,111,116,32,39,37,115,39,46,10,0,0,0,0,105,110,116,101,103,101,114,0,70,111,114,32,114,97,110,103,101,32,118,97,108,117,101,32,101,120,112,114,101,115,115,105,111,110,32,99,111,110,116,97,105,110,115,32,97,110,32,97,115,115,105,103,110,109,101,110,116,46,0,0,0,0,0,0,70,105,108,101,32,110,111,116,32,111,112,101,110,32,102,111,114,32,119,114,105,116,105,110,103,46,10,0,0,0,0,0,102,97,108,115,101,0,0,0,40,102,111,114,32,115,116,101,112,41,0,0,0,0,0,0,69,120,112,101,99,116,101,100,32,39,98,121,39,44,32,110,111,116,32,39,37,115,39,46,10,0,0,0,0,0,0,0,40,115,116,114,105,110,103,41,58,105,110,116,101,103,101,114,0,0,0,0,0,0,0,0,67,97,110,110,111,116,32,114,101,109,111,118,101,32,107,101,121,32,102,114,111,109,32,104,97,115,104,32,100,117,114,105,110,103,32,105,116,101,114,97,116,105,111,110,46,10,0,0,98,121,0,0,0,0,0,0,116,114,117,101,0,0,0,0,40,102,111,114,32,101,110,100,41,0,0,0,0,0,0,0,73,110,100,101,120,69,114,114,111,114,0,0,0,0,0,0,91,46,46,46,93,0,0,0,40,102,111,114,32,115,116,97,114,116,41,0,0,0,0,0,115,101,108,101,99,116,0,0,98,105,116,119,105,115,101,32,120,111,114,32,40,97,32,94,32,98,41,0,0,0,0,0,69,120,112,101,99,116,101,100,32,39,105,110,39,44,32,110,111,116,32,39,37,115,39,46,10,0,0,0,0,0,0,0,33,0,0,0,0,0,0,0,105,110,0,0,0,0,0,0,76,111,111,112,32,118,97,114,32,109,117,115,116,32,98,101,32,116,121,112,101,32,105,110,116,101,103,101,114,44,32,110,111,116,32,116,121,112,101,32,39,94,84,39,46,10,0,0,101,114,114,111,114,0,0,0,77,97,116,99,104,32,101,120,112,114,101,115,115,105,111,110,32,104,97,115,32,110,111,32,118,97,108,117,101,46,10,0,103,101,116,0,0,0,0,0,65,108,114,101,97,100,121,32,104,97,118,101,32,97,32,99,97,115,101,32,102,111,114,32,118,97,114,105,97,110,116,32,37,115,46,10,0,0,0,0,37,115,32,105,115,32,110,111,116,32,97,32,109,101,109,98,101,114,32,111,102,32,101,110,117,109,32,37,115,46,10,0,73,79,32,111,112,101,114,97,116,105,111,110,32,111,110,32,99,108,111,115,101,100,32,102,105,108,101,46,10,0,0,0,114,97,105,115,101,0,0,0,39,99,97,115,101,39,32,110,111,116,32,97,108,108,111,119,101,100,32,111,117,116,115,105,100,101,32,111,102,32,39,109,97,116,99,104,39,46,10,0,86,97,114,105,97,110,116,32,116,121,112,101,115,32,99,97,110,110,111,116,32,104,97,118,101,32,100,101,102,97,117,108,116,32,118,97,108,117,101,115,46,10,0,0,0,0,0,0,116,111,95,105,0,0,0,0,69,109,112,116,121,32,40,41,32,110,111,116,32,110,101,101,100,101,100,32,102,111,114,32,97,32,118,97,114,105,97,110,116,46,10,0,0,0,0,0,69,114,114,110,111,32,37,100,58,32,94,82,32,40,37,115,41,46,10,0,0,0,0,0,69,120,112,101,99,116,101,100,32,39,125,39,32,111,114,32,39,100,101,102,105,110,101,39,44,32,110,111,116,32,39,37,115,39,46,10,0,0,0,0,68,105,118,105,115,105,111,110,66,121,90,101,114,111,69,114,114,111,114,0,0,0,0,0,124,32,32,32,32,0,0,0,65,110,32,101,110,117,109,32,109,117,115,116,32,104,97,118,101,32,97,116,32,108,101,97,115,116,32,116,119,111,32,118,97,114,105,97,110,116,115,46,10,0,0,0,0,0,0,0,91,65,93,40,108,105,115,116,91,65,93,41,58,105,110,116,101,103,101,114,0,0,0,0,98,105,116,119,105,115,101,32,111,114,32,40,97,32,124,32,98,41,0,0,0,0,0,0,65,32,99,108,97,115,115,32,119,105,116,104,32,116,104,101,32,110,97,109,101,32,39,37,115,39,32,97,108,114,101,97,100,121,32,101,120,105,115,116,115,46,10,0,0,0,0,0,94,0,0,0,0,0,0,0,67,97,110,110,111,116,32,100,101,102,105,110,101,32,97,110,32,101,110,117,109,32,104,101,114,101,46,10,0,0,0,0,91,67,93,0,0,0,0,0,100,111,0,0,0,0,0,0,39,114,97,105,115,101,39,32,110,111,116,32,97,108,108,111,119,101,100,32,105,110,32,97,32,108,97,109,98,100,97,46,10,0,0,0,0,0,0,0,39,101,120,99,101,112,116,39,32,111,117,116,115,105,100,101,32,39,116,114,121,39,46,10,0,0,0,0,0,0,0,0,91,65,44,32,66,44,32,67,93,40,104,97,115,104,91,65,44,32,66,93,44,32,102,117,110,99,116,105,111,110,40,66,32,61,62,32,67,41,41,58,32,104,97,115,104,91,65,44,32,67,93,0,0,0,0,0,77,97,116,99,104,32,98,108,111,99,107,32,99,97,110,110,111,116,32,98,101,32,105,110,32,97,32,115,105,110,103,108,101,45,108,105,110,101,32,98,108,111,99,107,46,10,0,0,69,110,99,111,100,101,32,111,112,116,105,111,110,32,115,104,111,117,108,100,32,98,101,32,101,105,116,104,101,114,32,39,105,103,110,111,114,101,39,32,111,114,32,39,101,114,114,111,114,39,46,10,0,0,0,0,39,98,114,101,97,107,39,32,110,111,116,32,97,116,32,116,104,101,32,101,110,100,32,111,102,32,97,32,109,117,108,116,105,45,108,105,110,101,32,98,108,111,99,107,46,10,0,0,40,102,105,108,101,41,0,0,119,104,105,108,101,0,0,0,67,108,97,115,115,32,39,37,115,39,32,104,97,115,32,97,108,114,101,97,100,121,32,98], "i8", ALLOC_NONE, Runtime.GLOBAL_BASE+10240);
/* memory initializer */ allocate([101,101,110,32,100,101,99,108,97,114,101,100,46,10,0,0,67,97,110,110,111,116,32,100,101,99,108,97,114,101,32,97,32,99,108,97,115,115,32,104,101,114,101,46,10,0,0,0,116,114,105,109,0,0,0,0,39,37,115,39,32,105,115,32,110,111,116,32,97,32,118,97,108,105,100,32,99,108,97,115,115,32,110,97,109,101,32,40,116,111,111,32,115,104,111,114,116,41,46,10,0,0,0,0,69,109,112,116,121,32,40,41,32,110,111,116,32,110,101,101,100,101,100,32,102,111,114,32,97,32,99,108,97,115,115,46,10,0,0,0,0,0,0,0,58,108,105,115,116,91,115,116,114,105,110,103,93,0,0,0,37,112,0,0,0,0,0,0,69,109,112,116,121,32,40,41,32,110,111,116,32,110,101,101,100,101,100,32,104,101,114,101,32,102,111,114,32,105,110,104,101,114,105,116,101,100,32,110,101,119,46,10,0,0,0,0,115,105,122,101,0,0,0,0,98,105,116,119,105,115,101,32,97,110,100,32,40,97,32,38,32,98,41,0,0,0,0,0,110,101,119,0,0,0,0,0,91,0,0,0,0,0,0,0,39,37,115,39,32,99,97,110,110,111,116,32,98,101,32,105,110,104,101,114,105,116,101,100,32,102,114,111,109,46,10,0,37,100,58,0,0,0,0,0,65,32,99,108,97,115,115,32,99,97,110,110,111,116,32,105,110,104,101,114,105,116,32,102,114,111,109,32,105,116,115,101,108,102,33,10,0,0,0,0,39,101,120,99,101,112,116,39,32,99,108,97,117,115,101,32,105,115,32,117,110,114,101,97,99,104,97,98,108,101,46,10,0,0,0,0,0,0,0,0,109,97,112,95,118,97,108,117,101,115,0,0,0,0,0,0,67,97,110,110,111,116,32,100,101,102,105,110,101,32,97,32,99,108,97,115,115,32,104,101,114,101,46,10,0,0,0,0,83,116,97,116,101,109,101,110,116,40,115,41,32,97,102,116,101,114,32,39,37,115,39,32,119,105,108,108,32,110,111,116,32,101,120,101,99,117,116,101,46,10,0,0,0,0,0,0,99,108,111,115,101,0,0,0,101,110,117,109,0,0,0,0,39,114,101,116,117,114,110,39,32,110,111,116,32,97,108,108,111,119,101,100,32,105,110,32,97,32,108,97,109,98,100,97,46,10,0,0,0,0,0,0,39,114,101,116,117,114,110,39,32,110,111,116,32,97,108,108,111,119,101,100,32,105,110,32,97,32,99,108,97,115,115,32,99,111,110,115,116,114,117,99,116,111,114,46,10,0,0,0,40,115,116,114,105,110,103,41,58,115,116,114,105,110,103,0,116,111,95,105,0,0,0,0,100,111,117,98,108,101,0,0], "i8", ALLOC_NONE, Runtime.GLOBAL_BASE+20480);



var tempDoublePtr = Runtime.alignMemory(allocate(12, "i8", ALLOC_STATIC), 8);

assert(tempDoublePtr % 8 == 0);

function copyTempFloat(ptr) { // functions, because inlining this code increases code size too much

  HEAP8[tempDoublePtr] = HEAP8[ptr];

  HEAP8[tempDoublePtr+1] = HEAP8[ptr+1];

  HEAP8[tempDoublePtr+2] = HEAP8[ptr+2];

  HEAP8[tempDoublePtr+3] = HEAP8[ptr+3];

}

function copyTempDouble(ptr) {

  HEAP8[tempDoublePtr] = HEAP8[ptr];

  HEAP8[tempDoublePtr+1] = HEAP8[ptr+1];

  HEAP8[tempDoublePtr+2] = HEAP8[ptr+2];

  HEAP8[tempDoublePtr+3] = HEAP8[ptr+3];

  HEAP8[tempDoublePtr+4] = HEAP8[ptr+4];

  HEAP8[tempDoublePtr+5] = HEAP8[ptr+5];

  HEAP8[tempDoublePtr+6] = HEAP8[ptr+6];

  HEAP8[tempDoublePtr+7] = HEAP8[ptr+7];

}


  
  function _strncmp(px, py, n) {
      var i = 0;
      while (i < n) {
        var x = HEAPU8[(((px)+(i))|0)];
        var y = HEAPU8[(((py)+(i))|0)];
        if (x == y && x == 0) return 0;
        if (x == 0) return -1;
        if (y == 0) return 1;
        if (x == y) {
          i ++;
          continue;
        } else {
          return x > y ? 1 : -1;
        }
      }
      return 0;
    }function _strcmp(px, py) {
      return _strncmp(px, py, TOTAL_MEMORY);
    }

  
   
  Module["_memcpy"] = _memcpy;var _llvm_memcpy_p0i8_p0i8_i32=_memcpy;

  function _isalpha(chr) {
      return (chr >= 97 && chr <= 122) ||
             (chr >= 65 && chr <= 90);
    }

  function _isspace(chr) {
      return (chr == 32) || (chr >= 9 && chr <= 13);
    }

  function _isalnum(chr) {
      return (chr >= 48 && chr <= 57) ||
             (chr >= 97 && chr <= 122) ||
             (chr >= 65 && chr <= 90);
    }

   
  Module["_strcpy"] = _strcpy;

  
   
  Module["_strlen"] = _strlen; 
  Module["_strcat"] = _strcat;


  function _isupper(chr) {
      return chr >= 65 && chr <= 90;
    }

   
  Module["_tolower"] = _tolower;

   
  Module["_strncpy"] = _strncpy;

  function _islower(chr) {
      return chr >= 97 && chr <= 122;
    }

  function _toupper(chr) {
      if (chr >= 97 && chr <= 122) {
        return chr - 97 + 65;
      } else {
        return chr;
      }
    }

  var _llvm_va_start=undefined;

  function _llvm_va_end() {}

  
  
  
  
  
  var ERRNO_CODES={EPERM:1,ENOENT:2,ESRCH:3,EINTR:4,EIO:5,ENXIO:6,E2BIG:7,ENOEXEC:8,EBADF:9,ECHILD:10,EAGAIN:11,EWOULDBLOCK:11,ENOMEM:12,EACCES:13,EFAULT:14,ENOTBLK:15,EBUSY:16,EEXIST:17,EXDEV:18,ENODEV:19,ENOTDIR:20,EISDIR:21,EINVAL:22,ENFILE:23,EMFILE:24,ENOTTY:25,ETXTBSY:26,EFBIG:27,ENOSPC:28,ESPIPE:29,EROFS:30,EMLINK:31,EPIPE:32,EDOM:33,ERANGE:34,ENOMSG:42,EIDRM:43,ECHRNG:44,EL2NSYNC:45,EL3HLT:46,EL3RST:47,ELNRNG:48,EUNATCH:49,ENOCSI:50,EL2HLT:51,EDEADLK:35,ENOLCK:37,EBADE:52,EBADR:53,EXFULL:54,ENOANO:55,EBADRQC:56,EBADSLT:57,EDEADLOCK:35,EBFONT:59,ENOSTR:60,ENODATA:61,ETIME:62,ENOSR:63,ENONET:64,ENOPKG:65,EREMOTE:66,ENOLINK:67,EADV:68,ESRMNT:69,ECOMM:70,EPROTO:71,EMULTIHOP:72,EDOTDOT:73,EBADMSG:74,ENOTUNIQ:76,EBADFD:77,EREMCHG:78,ELIBACC:79,ELIBBAD:80,ELIBSCN:81,ELIBMAX:82,ELIBEXEC:83,ENOSYS:38,ENOTEMPTY:39,ENAMETOOLONG:36,ELOOP:40,EOPNOTSUPP:95,EPFNOSUPPORT:96,ECONNRESET:104,ENOBUFS:105,EAFNOSUPPORT:97,EPROTOTYPE:91,ENOTSOCK:88,ENOPROTOOPT:92,ESHUTDOWN:108,ECONNREFUSED:111,EADDRINUSE:98,ECONNABORTED:103,ENETUNREACH:101,ENETDOWN:100,ETIMEDOUT:110,EHOSTDOWN:112,EHOSTUNREACH:113,EINPROGRESS:115,EALREADY:114,EDESTADDRREQ:89,EMSGSIZE:90,EPROTONOSUPPORT:93,ESOCKTNOSUPPORT:94,EADDRNOTAVAIL:99,ENETRESET:102,EISCONN:106,ENOTCONN:107,ETOOMANYREFS:109,EUSERS:87,EDQUOT:122,ESTALE:116,ENOTSUP:95,ENOMEDIUM:123,EILSEQ:84,EOVERFLOW:75,ECANCELED:125,ENOTRECOVERABLE:131,EOWNERDEAD:130,ESTRPIPE:86};
  
  var ERRNO_MESSAGES={0:"Success",1:"Not super-user",2:"No such file or directory",3:"No such process",4:"Interrupted system call",5:"I/O error",6:"No such device or address",7:"Arg list too long",8:"Exec format error",9:"Bad file number",10:"No children",11:"No more processes",12:"Not enough core",13:"Permission denied",14:"Bad address",15:"Block device required",16:"Mount device busy",17:"File exists",18:"Cross-device link",19:"No such device",20:"Not a directory",21:"Is a directory",22:"Invalid argument",23:"Too many open files in system",24:"Too many open files",25:"Not a typewriter",26:"Text file busy",27:"File too large",28:"No space left on device",29:"Illegal seek",30:"Read only file system",31:"Too many links",32:"Broken pipe",33:"Math arg out of domain of func",34:"Math result not representable",35:"File locking deadlock error",36:"File or path name too long",37:"No record locks available",38:"Function not implemented",39:"Directory not empty",40:"Too many symbolic links",42:"No message of desired type",43:"Identifier removed",44:"Channel number out of range",45:"Level 2 not synchronized",46:"Level 3 halted",47:"Level 3 reset",48:"Link number out of range",49:"Protocol driver not attached",50:"No CSI structure available",51:"Level 2 halted",52:"Invalid exchange",53:"Invalid request descriptor",54:"Exchange full",55:"No anode",56:"Invalid request code",57:"Invalid slot",59:"Bad font file fmt",60:"Device not a stream",61:"No data (for no delay io)",62:"Timer expired",63:"Out of streams resources",64:"Machine is not on the network",65:"Package not installed",66:"The object is remote",67:"The link has been severed",68:"Advertise error",69:"Srmount error",70:"Communication error on send",71:"Protocol error",72:"Multihop attempted",73:"Cross mount point (not really error)",74:"Trying to read unreadable message",75:"Value too large for defined data type",76:"Given log. name not unique",77:"f.d. invalid for this operation",78:"Remote address changed",79:"Can   access a needed shared lib",80:"Accessing a corrupted shared lib",81:".lib section in a.out corrupted",82:"Attempting to link in too many libs",83:"Attempting to exec a shared library",84:"Illegal byte sequence",86:"Streams pipe error",87:"Too many users",88:"Socket operation on non-socket",89:"Destination address required",90:"Message too long",91:"Protocol wrong type for socket",92:"Protocol not available",93:"Unknown protocol",94:"Socket type not supported",95:"Not supported",96:"Protocol family not supported",97:"Address family not supported by protocol family",98:"Address already in use",99:"Address not available",100:"Network interface is not configured",101:"Network is unreachable",102:"Connection reset by network",103:"Connection aborted",104:"Connection reset by peer",105:"No buffer space available",106:"Socket is already connected",107:"Socket is not connected",108:"Can't send after socket shutdown",109:"Too many references",110:"Connection timed out",111:"Connection refused",112:"Host is down",113:"Host is unreachable",114:"Socket already connected",115:"Connection already in progress",116:"Stale file handle",122:"Quota exceeded",123:"No medium (in tape drive)",125:"Operation canceled",130:"Previous owner died",131:"State not recoverable"};
  
  
  var ___errno_state=0;function ___setErrNo(value) {
      // For convenient setting and returning of errno.
      HEAP32[((___errno_state)>>2)]=value;
      return value;
    }
  
  var PATH={splitPath:function (filename) {
        var splitPathRe = /^(\/?|)([\s\S]*?)((?:\.{1,2}|[^\/]+?|)(\.[^.\/]*|))(?:[\/]*)$/;
        return splitPathRe.exec(filename).slice(1);
      },normalizeArray:function (parts, allowAboveRoot) {
        // if the path tries to go above the root, `up` ends up > 0
        var up = 0;
        for (var i = parts.length - 1; i >= 0; i--) {
          var last = parts[i];
          if (last === '.') {
            parts.splice(i, 1);
          } else if (last === '..') {
            parts.splice(i, 1);
            up++;
          } else if (up) {
            parts.splice(i, 1);
            up--;
          }
        }
        // if the path is allowed to go above the root, restore leading ..s
        if (allowAboveRoot) {
          for (; up--; up) {
            parts.unshift('..');
          }
        }
        return parts;
      },normalize:function (path) {
        var isAbsolute = path.charAt(0) === '/',
            trailingSlash = path.substr(-1) === '/';
        // Normalize the path
        path = PATH.normalizeArray(path.split('/').filter(function(p) {
          return !!p;
        }), !isAbsolute).join('/');
        if (!path && !isAbsolute) {
          path = '.';
        }
        if (path && trailingSlash) {
          path += '/';
        }
        return (isAbsolute ? '/' : '') + path;
      },dirname:function (path) {
        var result = PATH.splitPath(path),
            root = result[0],
            dir = result[1];
        if (!root && !dir) {
          // No dirname whatsoever
          return '.';
        }
        if (dir) {
          // It has a dirname, strip trailing slash
          dir = dir.substr(0, dir.length - 1);
        }
        return root + dir;
      },basename:function (path) {
        // EMSCRIPTEN return '/'' for '/', not an empty string
        if (path === '/') return '/';
        var lastSlash = path.lastIndexOf('/');
        if (lastSlash === -1) return path;
        return path.substr(lastSlash+1);
      },extname:function (path) {
        return PATH.splitPath(path)[3];
      },join:function () {
        var paths = Array.prototype.slice.call(arguments, 0);
        return PATH.normalize(paths.join('/'));
      },join2:function (l, r) {
        return PATH.normalize(l + '/' + r);
      },resolve:function () {
        var resolvedPath = '',
          resolvedAbsolute = false;
        for (var i = arguments.length - 1; i >= -1 && !resolvedAbsolute; i--) {
          var path = (i >= 0) ? arguments[i] : FS.cwd();
          // Skip empty and invalid entries
          if (typeof path !== 'string') {
            throw new TypeError('Arguments to path.resolve must be strings');
          } else if (!path) {
            continue;
          }
          resolvedPath = path + '/' + resolvedPath;
          resolvedAbsolute = path.charAt(0) === '/';
        }
        // At this point the path should be resolved to a full absolute path, but
        // handle relative paths to be safe (might happen when process.cwd() fails)
        resolvedPath = PATH.normalizeArray(resolvedPath.split('/').filter(function(p) {
          return !!p;
        }), !resolvedAbsolute).join('/');
        return ((resolvedAbsolute ? '/' : '') + resolvedPath) || '.';
      },relative:function (from, to) {
        from = PATH.resolve(from).substr(1);
        to = PATH.resolve(to).substr(1);
        function trim(arr) {
          var start = 0;
          for (; start < arr.length; start++) {
            if (arr[start] !== '') break;
          }
          var end = arr.length - 1;
          for (; end >= 0; end--) {
            if (arr[end] !== '') break;
          }
          if (start > end) return [];
          return arr.slice(start, end - start + 1);
        }
        var fromParts = trim(from.split('/'));
        var toParts = trim(to.split('/'));
        var length = Math.min(fromParts.length, toParts.length);
        var samePartsLength = length;
        for (var i = 0; i < length; i++) {
          if (fromParts[i] !== toParts[i]) {
            samePartsLength = i;
            break;
          }
        }
        var outputParts = [];
        for (var i = samePartsLength; i < fromParts.length; i++) {
          outputParts.push('..');
        }
        outputParts = outputParts.concat(toParts.slice(samePartsLength));
        return outputParts.join('/');
      }};
  
  var TTY={ttys:[],init:function () {
        // https://github.com/kripken/emscripten/pull/1555
        // if (ENVIRONMENT_IS_NODE) {
        //   // currently, FS.init does not distinguish if process.stdin is a file or TTY
        //   // device, it always assumes it's a TTY device. because of this, we're forcing
        //   // process.stdin to UTF8 encoding to at least make stdin reading compatible
        //   // with text files until FS.init can be refactored.
        //   process['stdin']['setEncoding']('utf8');
        // }
      },shutdown:function () {
        // https://github.com/kripken/emscripten/pull/1555
        // if (ENVIRONMENT_IS_NODE) {
        //   // inolen: any idea as to why node -e 'process.stdin.read()' wouldn't exit immediately (with process.stdin being a tty)?
        //   // isaacs: because now it's reading from the stream, you've expressed interest in it, so that read() kicks off a _read() which creates a ReadReq operation
        //   // inolen: I thought read() in that case was a synchronous operation that just grabbed some amount of buffered data if it exists?
        //   // isaacs: it is. but it also triggers a _read() call, which calls readStart() on the handle
        //   // isaacs: do process.stdin.pause() and i'd think it'd probably close the pending call
        //   process['stdin']['pause']();
        // }
      },register:function (dev, ops) {
        TTY.ttys[dev] = { input: [], output: [], ops: ops };
        FS.registerDevice(dev, TTY.stream_ops);
      },stream_ops:{open:function (stream) {
          var tty = TTY.ttys[stream.node.rdev];
          if (!tty) {
            throw new FS.ErrnoError(ERRNO_CODES.ENODEV);
          }
          stream.tty = tty;
          stream.seekable = false;
        },close:function (stream) {
          // flush any pending line data
          if (stream.tty.output.length) {
            stream.tty.ops.put_char(stream.tty, 10);
          }
        },read:function (stream, buffer, offset, length, pos /* ignored */) {
          if (!stream.tty || !stream.tty.ops.get_char) {
            throw new FS.ErrnoError(ERRNO_CODES.ENXIO);
          }
          var bytesRead = 0;
          for (var i = 0; i < length; i++) {
            var result;
            try {
              result = stream.tty.ops.get_char(stream.tty);
            } catch (e) {
              throw new FS.ErrnoError(ERRNO_CODES.EIO);
            }
            if (result === undefined && bytesRead === 0) {
              throw new FS.ErrnoError(ERRNO_CODES.EAGAIN);
            }
            if (result === null || result === undefined) break;
            bytesRead++;
            buffer[offset+i] = result;
          }
          if (bytesRead) {
            stream.node.timestamp = Date.now();
          }
          return bytesRead;
        },write:function (stream, buffer, offset, length, pos) {
          if (!stream.tty || !stream.tty.ops.put_char) {
            throw new FS.ErrnoError(ERRNO_CODES.ENXIO);
          }
          for (var i = 0; i < length; i++) {
            try {
              stream.tty.ops.put_char(stream.tty, buffer[offset+i]);
            } catch (e) {
              throw new FS.ErrnoError(ERRNO_CODES.EIO);
            }
          }
          if (length) {
            stream.node.timestamp = Date.now();
          }
          return i;
        }},default_tty_ops:{get_char:function (tty) {
          if (!tty.input.length) {
            var result = null;
            if (ENVIRONMENT_IS_NODE) {
              result = process['stdin']['read']();
              if (!result) {
                if (process['stdin']['_readableState'] && process['stdin']['_readableState']['ended']) {
                  return null;  // EOF
                }
                return undefined;  // no data available
              }
            } else if (typeof window != 'undefined' &&
              typeof window.prompt == 'function') {
              // Browser.
              result = window.prompt('Input: ');  // returns null on cancel
              if (result !== null) {
                result += '\n';
              }
            } else if (typeof readline == 'function') {
              // Command line.
              result = readline();
              if (result !== null) {
                result += '\n';
              }
            }
            if (!result) {
              return null;
            }
            tty.input = intArrayFromString(result, true);
          }
          return tty.input.shift();
        },put_char:function (tty, val) {
          if (val === null || val === 10) {
            Module['print'](tty.output.join(''));
            tty.output = [];
          } else {
            tty.output.push(TTY.utf8.processCChar(val));
          }
        }},default_tty1_ops:{put_char:function (tty, val) {
          if (val === null || val === 10) {
            Module['printErr'](tty.output.join(''));
            tty.output = [];
          } else {
            tty.output.push(TTY.utf8.processCChar(val));
          }
        }}};
  
  var MEMFS={ops_table:null,CONTENT_OWNING:1,CONTENT_FLEXIBLE:2,CONTENT_FIXED:3,mount:function (mount) {
        return MEMFS.createNode(null, '/', 16384 | 0777, 0);
      },createNode:function (parent, name, mode, dev) {
        if (FS.isBlkdev(mode) || FS.isFIFO(mode)) {
          // no supported
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        if (!MEMFS.ops_table) {
          MEMFS.ops_table = {
            dir: {
              node: {
                getattr: MEMFS.node_ops.getattr,
                setattr: MEMFS.node_ops.setattr,
                lookup: MEMFS.node_ops.lookup,
                mknod: MEMFS.node_ops.mknod,
                mknod: MEMFS.node_ops.mknod,
                rename: MEMFS.node_ops.rename,
                unlink: MEMFS.node_ops.unlink,
                rmdir: MEMFS.node_ops.rmdir,
                readdir: MEMFS.node_ops.readdir,
                symlink: MEMFS.node_ops.symlink
              },
              stream: {
                llseek: MEMFS.stream_ops.llseek
              }
            },
            file: {
              node: {
                getattr: MEMFS.node_ops.getattr,
                setattr: MEMFS.node_ops.setattr
              },
              stream: {
                llseek: MEMFS.stream_ops.llseek,
                read: MEMFS.stream_ops.read,
                write: MEMFS.stream_ops.write,
                allocate: MEMFS.stream_ops.allocate,
                mmap: MEMFS.stream_ops.mmap
              }
            },
            link: {
              node: {
                getattr: MEMFS.node_ops.getattr,
                setattr: MEMFS.node_ops.setattr,
                readlink: MEMFS.node_ops.readlink
              },
              stream: {}
            },
            chrdev: {
              node: {
                getattr: MEMFS.node_ops.getattr,
                setattr: MEMFS.node_ops.setattr
              },
              stream: FS.chrdev_stream_ops
            },
          };
        }
        var node = FS.createNode(parent, name, mode, dev);
        if (FS.isDir(node.mode)) {
          node.node_ops = MEMFS.ops_table.dir.node;
          node.stream_ops = MEMFS.ops_table.dir.stream;
          node.contents = {};
        } else if (FS.isFile(node.mode)) {
          node.node_ops = MEMFS.ops_table.file.node;
          node.stream_ops = MEMFS.ops_table.file.stream;
          node.contents = [];
          node.contentMode = MEMFS.CONTENT_FLEXIBLE;
        } else if (FS.isLink(node.mode)) {
          node.node_ops = MEMFS.ops_table.link.node;
          node.stream_ops = MEMFS.ops_table.link.stream;
        } else if (FS.isChrdev(node.mode)) {
          node.node_ops = MEMFS.ops_table.chrdev.node;
          node.stream_ops = MEMFS.ops_table.chrdev.stream;
        }
        node.timestamp = Date.now();
        // add the new node to the parent
        if (parent) {
          parent.contents[name] = node;
        }
        return node;
      },ensureFlexible:function (node) {
        if (node.contentMode !== MEMFS.CONTENT_FLEXIBLE) {
          var contents = node.contents;
          node.contents = Array.prototype.slice.call(contents);
          node.contentMode = MEMFS.CONTENT_FLEXIBLE;
        }
      },node_ops:{getattr:function (node) {
          var attr = {};
          // device numbers reuse inode numbers.
          attr.dev = FS.isChrdev(node.mode) ? node.id : 1;
          attr.ino = node.id;
          attr.mode = node.mode;
          attr.nlink = 1;
          attr.uid = 0;
          attr.gid = 0;
          attr.rdev = node.rdev;
          if (FS.isDir(node.mode)) {
            attr.size = 4096;
          } else if (FS.isFile(node.mode)) {
            attr.size = node.contents.length;
          } else if (FS.isLink(node.mode)) {
            attr.size = node.link.length;
          } else {
            attr.size = 0;
          }
          attr.atime = new Date(node.timestamp);
          attr.mtime = new Date(node.timestamp);
          attr.ctime = new Date(node.timestamp);
          // NOTE: In our implementation, st_blocks = Math.ceil(st_size/st_blksize),
          //       but this is not required by the standard.
          attr.blksize = 4096;
          attr.blocks = Math.ceil(attr.size / attr.blksize);
          return attr;
        },setattr:function (node, attr) {
          if (attr.mode !== undefined) {
            node.mode = attr.mode;
          }
          if (attr.timestamp !== undefined) {
            node.timestamp = attr.timestamp;
          }
          if (attr.size !== undefined) {
            MEMFS.ensureFlexible(node);
            var contents = node.contents;
            if (attr.size < contents.length) contents.length = attr.size;
            else while (attr.size > contents.length) contents.push(0);
          }
        },lookup:function (parent, name) {
          throw FS.genericErrors[ERRNO_CODES.ENOENT];
        },mknod:function (parent, name, mode, dev) {
          return MEMFS.createNode(parent, name, mode, dev);
        },rename:function (old_node, new_dir, new_name) {
          // if we're overwriting a directory at new_name, make sure it's empty.
          if (FS.isDir(old_node.mode)) {
            var new_node;
            try {
              new_node = FS.lookupNode(new_dir, new_name);
            } catch (e) {
            }
            if (new_node) {
              for (var i in new_node.contents) {
                throw new FS.ErrnoError(ERRNO_CODES.ENOTEMPTY);
              }
            }
          }
          // do the internal rewiring
          delete old_node.parent.contents[old_node.name];
          old_node.name = new_name;
          new_dir.contents[new_name] = old_node;
          old_node.parent = new_dir;
        },unlink:function (parent, name) {
          delete parent.contents[name];
        },rmdir:function (parent, name) {
          var node = FS.lookupNode(parent, name);
          for (var i in node.contents) {
            throw new FS.ErrnoError(ERRNO_CODES.ENOTEMPTY);
          }
          delete parent.contents[name];
        },readdir:function (node) {
          var entries = ['.', '..']
          for (var key in node.contents) {
            if (!node.contents.hasOwnProperty(key)) {
              continue;
            }
            entries.push(key);
          }
          return entries;
        },symlink:function (parent, newname, oldpath) {
          var node = MEMFS.createNode(parent, newname, 0777 | 40960, 0);
          node.link = oldpath;
          return node;
        },readlink:function (node) {
          if (!FS.isLink(node.mode)) {
            throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
          }
          return node.link;
        }},stream_ops:{read:function (stream, buffer, offset, length, position) {
          var contents = stream.node.contents;
          if (position >= contents.length)
            return 0;
          var size = Math.min(contents.length - position, length);
          assert(size >= 0);
          if (size > 8 && contents.subarray) { // non-trivial, and typed array
            buffer.set(contents.subarray(position, position + size), offset);
          } else
          {
            for (var i = 0; i < size; i++) {
              buffer[offset + i] = contents[position + i];
            }
          }
          return size;
        },write:function (stream, buffer, offset, length, position, canOwn) {
          var node = stream.node;
          node.timestamp = Date.now();
          var contents = node.contents;
          if (length && contents.length === 0 && position === 0 && buffer.subarray) {
            // just replace it with the new data
            if (canOwn && offset === 0) {
              node.contents = buffer; // this could be a subarray of Emscripten HEAP, or allocated from some other source.
              node.contentMode = (buffer.buffer === HEAP8.buffer) ? MEMFS.CONTENT_OWNING : MEMFS.CONTENT_FIXED;
            } else {
              node.contents = new Uint8Array(buffer.subarray(offset, offset+length));
              node.contentMode = MEMFS.CONTENT_FIXED;
            }
            return length;
          }
          MEMFS.ensureFlexible(node);
          var contents = node.contents;
          while (contents.length < position) contents.push(0);
          for (var i = 0; i < length; i++) {
            contents[position + i] = buffer[offset + i];
          }
          return length;
        },llseek:function (stream, offset, whence) {
          var position = offset;
          if (whence === 1) {  // SEEK_CUR.
            position += stream.position;
          } else if (whence === 2) {  // SEEK_END.
            if (FS.isFile(stream.node.mode)) {
              position += stream.node.contents.length;
            }
          }
          if (position < 0) {
            throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
          }
          stream.ungotten = [];
          stream.position = position;
          return position;
        },allocate:function (stream, offset, length) {
          MEMFS.ensureFlexible(stream.node);
          var contents = stream.node.contents;
          var limit = offset + length;
          while (limit > contents.length) contents.push(0);
        },mmap:function (stream, buffer, offset, length, position, prot, flags) {
          if (!FS.isFile(stream.node.mode)) {
            throw new FS.ErrnoError(ERRNO_CODES.ENODEV);
          }
          var ptr;
          var allocated;
          var contents = stream.node.contents;
          // Only make a new copy when MAP_PRIVATE is specified.
          if ( !(flags & 2) &&
                (contents.buffer === buffer || contents.buffer === buffer.buffer) ) {
            // We can't emulate MAP_SHARED when the file is not backed by the buffer
            // we're mapping to (e.g. the HEAP buffer).
            allocated = false;
            ptr = contents.byteOffset;
          } else {
            // Try to avoid unnecessary slices.
            if (position > 0 || position + length < contents.length) {
              if (contents.subarray) {
                contents = contents.subarray(position, position + length);
              } else {
                contents = Array.prototype.slice.call(contents, position, position + length);
              }
            }
            allocated = true;
            ptr = _malloc(length);
            if (!ptr) {
              throw new FS.ErrnoError(ERRNO_CODES.ENOMEM);
            }
            buffer.set(contents, ptr);
          }
          return { ptr: ptr, allocated: allocated };
        }}};
  
  var IDBFS={dbs:{},indexedDB:function () {
        return window.indexedDB || window.mozIndexedDB || window.webkitIndexedDB || window.msIndexedDB;
      },DB_VERSION:20,DB_STORE_NAME:"FILE_DATA",mount:function (mount) {
        return MEMFS.mount.apply(null, arguments);
      },syncfs:function (mount, populate, callback) {
        IDBFS.getLocalSet(mount, function(err, local) {
          if (err) return callback(err);
  
          IDBFS.getRemoteSet(mount, function(err, remote) {
            if (err) return callback(err);
  
            var src = populate ? remote : local;
            var dst = populate ? local : remote;
  
            IDBFS.reconcile(src, dst, callback);
          });
        });
      },reconcile:function (src, dst, callback) {
        var total = 0;
  
        var create = {};
        for (var key in src.files) {
          if (!src.files.hasOwnProperty(key)) continue;
          var e = src.files[key];
          var e2 = dst.files[key];
          if (!e2 || e.timestamp > e2.timestamp) {
            create[key] = e;
            total++;
          }
        }
  
        var remove = {};
        for (var key in dst.files) {
          if (!dst.files.hasOwnProperty(key)) continue;
          var e = dst.files[key];
          var e2 = src.files[key];
          if (!e2) {
            remove[key] = e;
            total++;
          }
        }
  
        if (!total) {
          // early out
          return callback(null);
        }
  
        var completed = 0;
        function done(err) {
          if (err) return callback(err);
          if (++completed >= total) {
            return callback(null);
          }
        };
  
        // create a single transaction to handle and IDB reads / writes we'll need to do
        var db = src.type === 'remote' ? src.db : dst.db;
        var transaction = db.transaction([IDBFS.DB_STORE_NAME], 'readwrite');
        transaction.onerror = function transaction_onerror() { callback(this.error); };
        var store = transaction.objectStore(IDBFS.DB_STORE_NAME);
  
        for (var path in create) {
          if (!create.hasOwnProperty(path)) continue;
          var entry = create[path];
  
          if (dst.type === 'local') {
            // save file to local
            try {
              if (FS.isDir(entry.mode)) {
                FS.mkdir(path, entry.mode);
              } else if (FS.isFile(entry.mode)) {
                var stream = FS.open(path, 'w+', 0666);
                FS.write(stream, entry.contents, 0, entry.contents.length, 0, true /* canOwn */);
                FS.close(stream);
              }
              done(null);
            } catch (e) {
              return done(e);
            }
          } else {
            // save file to IDB
            var req = store.put(entry, path);
            req.onsuccess = function req_onsuccess() { done(null); };
            req.onerror = function req_onerror() { done(this.error); };
          }
        }
  
        for (var path in remove) {
          if (!remove.hasOwnProperty(path)) continue;
          var entry = remove[path];
  
          if (dst.type === 'local') {
            // delete file from local
            try {
              if (FS.isDir(entry.mode)) {
                // TODO recursive delete?
                FS.rmdir(path);
              } else if (FS.isFile(entry.mode)) {
                FS.unlink(path);
              }
              done(null);
            } catch (e) {
              return done(e);
            }
          } else {
            // delete file from IDB
            var req = store.delete(path);
            req.onsuccess = function req_onsuccess() { done(null); };
            req.onerror = function req_onerror() { done(this.error); };
          }
        }
      },getLocalSet:function (mount, callback) {
        var files = {};
  
        function isRealDir(p) {
          return p !== '.' && p !== '..';
        };
        function toAbsolute(root) {
          return function(p) {
            return PATH.join2(root, p);
          }
        };
  
        var check = FS.readdir(mount.mountpoint)
          .filter(isRealDir)
          .map(toAbsolute(mount.mountpoint));
  
        while (check.length) {
          var path = check.pop();
          var stat, node;
  
          try {
            var lookup = FS.lookupPath(path);
            node = lookup.node;
            stat = FS.stat(path);
          } catch (e) {
            return callback(e);
          }
  
          if (FS.isDir(stat.mode)) {
            check.push.apply(check, FS.readdir(path)
              .filter(isRealDir)
              .map(toAbsolute(path)));
  
            files[path] = { mode: stat.mode, timestamp: stat.mtime };
          } else if (FS.isFile(stat.mode)) {
            files[path] = { contents: node.contents, mode: stat.mode, timestamp: stat.mtime };
          } else {
            return callback(new Error('node type not supported'));
          }
        }
  
        return callback(null, { type: 'local', files: files });
      },getDB:function (name, callback) {
        // look it up in the cache
        var db = IDBFS.dbs[name];
        if (db) {
          return callback(null, db);
        }
        var req;
        try {
          req = IDBFS.indexedDB().open(name, IDBFS.DB_VERSION);
        } catch (e) {
          return onerror(e);
        }
        req.onupgradeneeded = function req_onupgradeneeded() {
          db = req.result;
          db.createObjectStore(IDBFS.DB_STORE_NAME);
        };
        req.onsuccess = function req_onsuccess() {
          db = req.result;
          // add to the cache
          IDBFS.dbs[name] = db;
          callback(null, db);
        };
        req.onerror = function req_onerror() {
          callback(this.error);
        };
      },getRemoteSet:function (mount, callback) {
        var files = {};
  
        IDBFS.getDB(mount.mountpoint, function(err, db) {
          if (err) return callback(err);
  
          var transaction = db.transaction([IDBFS.DB_STORE_NAME], 'readonly');
          transaction.onerror = function transaction_onerror() { callback(this.error); };
  
          var store = transaction.objectStore(IDBFS.DB_STORE_NAME);
          store.openCursor().onsuccess = function store_openCursor_onsuccess(event) {
            var cursor = event.target.result;
            if (!cursor) {
              return callback(null, { type: 'remote', db: db, files: files });
            }
  
            files[cursor.key] = cursor.value;
            cursor.continue();
          };
        });
      }};
  
  var NODEFS={isWindows:false,staticInit:function () {
        NODEFS.isWindows = !!process.platform.match(/^win/);
      },mount:function (mount) {
        assert(ENVIRONMENT_IS_NODE);
        return NODEFS.createNode(null, '/', NODEFS.getMode(mount.opts.root), 0);
      },createNode:function (parent, name, mode, dev) {
        if (!FS.isDir(mode) && !FS.isFile(mode) && !FS.isLink(mode)) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        var node = FS.createNode(parent, name, mode);
        node.node_ops = NODEFS.node_ops;
        node.stream_ops = NODEFS.stream_ops;
        return node;
      },getMode:function (path) {
        var stat;
        try {
          stat = fs.lstatSync(path);
          if (NODEFS.isWindows) {
            // On Windows, directories return permission bits 'rw-rw-rw-', even though they have 'rwxrwxrwx', so 
            // propagate write bits to execute bits.
            stat.mode = stat.mode | ((stat.mode & 146) >> 1);
          }
        } catch (e) {
          if (!e.code) throw e;
          throw new FS.ErrnoError(ERRNO_CODES[e.code]);
        }
        return stat.mode;
      },realPath:function (node) {
        var parts = [];
        while (node.parent !== node) {
          parts.push(node.name);
          node = node.parent;
        }
        parts.push(node.mount.opts.root);
        parts.reverse();
        return PATH.join.apply(null, parts);
      },flagsToPermissionStringMap:{0:"r",1:"r+",2:"r+",64:"r",65:"r+",66:"r+",129:"rx+",193:"rx+",514:"w+",577:"w",578:"w+",705:"wx",706:"wx+",1024:"a",1025:"a",1026:"a+",1089:"a",1090:"a+",1153:"ax",1154:"ax+",1217:"ax",1218:"ax+",4096:"rs",4098:"rs+"},flagsToPermissionString:function (flags) {
        if (flags in NODEFS.flagsToPermissionStringMap) {
          return NODEFS.flagsToPermissionStringMap[flags];
        } else {
          return flags;
        }
      },node_ops:{getattr:function (node) {
          var path = NODEFS.realPath(node);
          var stat;
          try {
            stat = fs.lstatSync(path);
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
          // node.js v0.10.20 doesn't report blksize and blocks on Windows. Fake them with default blksize of 4096.
          // See http://support.microsoft.com/kb/140365
          if (NODEFS.isWindows && !stat.blksize) {
            stat.blksize = 4096;
          }
          if (NODEFS.isWindows && !stat.blocks) {
            stat.blocks = (stat.size+stat.blksize-1)/stat.blksize|0;
          }
          return {
            dev: stat.dev,
            ino: stat.ino,
            mode: stat.mode,
            nlink: stat.nlink,
            uid: stat.uid,
            gid: stat.gid,
            rdev: stat.rdev,
            size: stat.size,
            atime: stat.atime,
            mtime: stat.mtime,
            ctime: stat.ctime,
            blksize: stat.blksize,
            blocks: stat.blocks
          };
        },setattr:function (node, attr) {
          var path = NODEFS.realPath(node);
          try {
            if (attr.mode !== undefined) {
              fs.chmodSync(path, attr.mode);
              // update the common node structure mode as well
              node.mode = attr.mode;
            }
            if (attr.timestamp !== undefined) {
              var date = new Date(attr.timestamp);
              fs.utimesSync(path, date, date);
            }
            if (attr.size !== undefined) {
              fs.truncateSync(path, attr.size);
            }
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },lookup:function (parent, name) {
          var path = PATH.join2(NODEFS.realPath(parent), name);
          var mode = NODEFS.getMode(path);
          return NODEFS.createNode(parent, name, mode);
        },mknod:function (parent, name, mode, dev) {
          var node = NODEFS.createNode(parent, name, mode, dev);
          // create the backing node for this in the fs root as well
          var path = NODEFS.realPath(node);
          try {
            if (FS.isDir(node.mode)) {
              fs.mkdirSync(path, node.mode);
            } else {
              fs.writeFileSync(path, '', { mode: node.mode });
            }
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
          return node;
        },rename:function (oldNode, newDir, newName) {
          var oldPath = NODEFS.realPath(oldNode);
          var newPath = PATH.join2(NODEFS.realPath(newDir), newName);
          try {
            fs.renameSync(oldPath, newPath);
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },unlink:function (parent, name) {
          var path = PATH.join2(NODEFS.realPath(parent), name);
          try {
            fs.unlinkSync(path);
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },rmdir:function (parent, name) {
          var path = PATH.join2(NODEFS.realPath(parent), name);
          try {
            fs.rmdirSync(path);
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },readdir:function (node) {
          var path = NODEFS.realPath(node);
          try {
            return fs.readdirSync(path);
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },symlink:function (parent, newName, oldPath) {
          var newPath = PATH.join2(NODEFS.realPath(parent), newName);
          try {
            fs.symlinkSync(oldPath, newPath);
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },readlink:function (node) {
          var path = NODEFS.realPath(node);
          try {
            return fs.readlinkSync(path);
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        }},stream_ops:{open:function (stream) {
          var path = NODEFS.realPath(stream.node);
          try {
            if (FS.isFile(stream.node.mode)) {
              stream.nfd = fs.openSync(path, NODEFS.flagsToPermissionString(stream.flags));
            }
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },close:function (stream) {
          try {
            if (FS.isFile(stream.node.mode) && stream.nfd) {
              fs.closeSync(stream.nfd);
            }
          } catch (e) {
            if (!e.code) throw e;
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
        },read:function (stream, buffer, offset, length, position) {
          // FIXME this is terrible.
          var nbuffer = new Buffer(length);
          var res;
          try {
            res = fs.readSync(stream.nfd, nbuffer, 0, length, position);
          } catch (e) {
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
          if (res > 0) {
            for (var i = 0; i < res; i++) {
              buffer[offset + i] = nbuffer[i];
            }
          }
          return res;
        },write:function (stream, buffer, offset, length, position) {
          // FIXME this is terrible.
          var nbuffer = new Buffer(buffer.subarray(offset, offset + length));
          var res;
          try {
            res = fs.writeSync(stream.nfd, nbuffer, 0, length, position);
          } catch (e) {
            throw new FS.ErrnoError(ERRNO_CODES[e.code]);
          }
          return res;
        },llseek:function (stream, offset, whence) {
          var position = offset;
          if (whence === 1) {  // SEEK_CUR.
            position += stream.position;
          } else if (whence === 2) {  // SEEK_END.
            if (FS.isFile(stream.node.mode)) {
              try {
                var stat = fs.fstatSync(stream.nfd);
                position += stat.size;
              } catch (e) {
                throw new FS.ErrnoError(ERRNO_CODES[e.code]);
              }
            }
          }
  
          if (position < 0) {
            throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
          }
  
          stream.position = position;
          return position;
        }}};
  
  var _stdin=allocate(1, "i32*", ALLOC_STATIC);
  
  var _stdout=allocate(1, "i32*", ALLOC_STATIC);
  
  var _stderr=allocate(1, "i32*", ALLOC_STATIC);
  
  function _fflush(stream) {
      // int fflush(FILE *stream);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fflush.html
      // we don't currently perform any user-space buffering of data
    }var FS={root:null,mounts:[],devices:[null],streams:[null],nextInode:1,nameTable:null,currentPath:"/",initialized:false,ignorePermissions:true,ErrnoError:null,genericErrors:{},handleFSError:function (e) {
        if (!(e instanceof FS.ErrnoError)) throw e + ' : ' + stackTrace();
        return ___setErrNo(e.errno);
      },lookupPath:function (path, opts) {
        path = PATH.resolve(FS.cwd(), path);
        opts = opts || { recurse_count: 0 };
  
        if (opts.recurse_count > 8) {  // max recursive lookup of 8
          throw new FS.ErrnoError(ERRNO_CODES.ELOOP);
        }
  
        // split the path
        var parts = PATH.normalizeArray(path.split('/').filter(function(p) {
          return !!p;
        }), false);
  
        // start at the root
        var current = FS.root;
        var current_path = '/';
  
        for (var i = 0; i < parts.length; i++) {
          var islast = (i === parts.length-1);
          if (islast && opts.parent) {
            // stop resolving
            break;
          }
  
          current = FS.lookupNode(current, parts[i]);
          current_path = PATH.join2(current_path, parts[i]);
  
          // jump to the mount's root node if this is a mountpoint
          if (FS.isMountpoint(current)) {
            current = current.mount.root;
          }
  
          // follow symlinks
          // by default, lookupPath will not follow a symlink if it is the final path component.
          // setting opts.follow = true will override this behavior.
          if (!islast || opts.follow) {
            var count = 0;
            while (FS.isLink(current.mode)) {
              var link = FS.readlink(current_path);
              current_path = PATH.resolve(PATH.dirname(current_path), link);
              
              var lookup = FS.lookupPath(current_path, { recurse_count: opts.recurse_count });
              current = lookup.node;
  
              if (count++ > 40) {  // limit max consecutive symlinks to 40 (SYMLOOP_MAX).
                throw new FS.ErrnoError(ERRNO_CODES.ELOOP);
              }
            }
          }
        }
  
        return { path: current_path, node: current };
      },getPath:function (node) {
        var path;
        while (true) {
          if (FS.isRoot(node)) {
            var mount = node.mount.mountpoint;
            if (!path) return mount;
            return mount[mount.length-1] !== '/' ? mount + '/' + path : mount + path;
          }
          path = path ? node.name + '/' + path : node.name;
          node = node.parent;
        }
      },hashName:function (parentid, name) {
        var hash = 0;
  
  
        for (var i = 0; i < name.length; i++) {
          hash = ((hash << 5) - hash + name.charCodeAt(i)) | 0;
        }
        return ((parentid + hash) >>> 0) % FS.nameTable.length;
      },hashAddNode:function (node) {
        var hash = FS.hashName(node.parent.id, node.name);
        node.name_next = FS.nameTable[hash];
        FS.nameTable[hash] = node;
      },hashRemoveNode:function (node) {
        var hash = FS.hashName(node.parent.id, node.name);
        if (FS.nameTable[hash] === node) {
          FS.nameTable[hash] = node.name_next;
        } else {
          var current = FS.nameTable[hash];
          while (current) {
            if (current.name_next === node) {
              current.name_next = node.name_next;
              break;
            }
            current = current.name_next;
          }
        }
      },lookupNode:function (parent, name) {
        var err = FS.mayLookup(parent);
        if (err) {
          throw new FS.ErrnoError(err);
        }
        var hash = FS.hashName(parent.id, name);
        for (var node = FS.nameTable[hash]; node; node = node.name_next) {
          var nodeName = node.name;
          if (node.parent.id === parent.id && nodeName === name) {
            return node;
          }
        }
        // if we failed to find it in the cache, call into the VFS
        return FS.lookup(parent, name);
      },createNode:function (parent, name, mode, rdev) {
        if (!FS.FSNode) {
          FS.FSNode = function(parent, name, mode, rdev) {
            this.id = FS.nextInode++;
            this.name = name;
            this.mode = mode;
            this.node_ops = {};
            this.stream_ops = {};
            this.rdev = rdev;
            this.parent = null;
            this.mount = null;
            if (!parent) {
              parent = this;  // root node sets parent to itself
            }
            this.parent = parent;
            this.mount = parent.mount;
            FS.hashAddNode(this);
          };
  
          // compatibility
          var readMode = 292 | 73;
          var writeMode = 146;
  
          FS.FSNode.prototype = {};
  
          // NOTE we must use Object.defineProperties instead of individual calls to
          // Object.defineProperty in order to make closure compiler happy
          Object.defineProperties(FS.FSNode.prototype, {
            read: {
              get: function() { return (this.mode & readMode) === readMode; },
              set: function(val) { val ? this.mode |= readMode : this.mode &= ~readMode; }
            },
            write: {
              get: function() { return (this.mode & writeMode) === writeMode; },
              set: function(val) { val ? this.mode |= writeMode : this.mode &= ~writeMode; }
            },
            isFolder: {
              get: function() { return FS.isDir(this.mode); },
            },
            isDevice: {
              get: function() { return FS.isChrdev(this.mode); },
            },
          });
        }
        return new FS.FSNode(parent, name, mode, rdev);
      },destroyNode:function (node) {
        FS.hashRemoveNode(node);
      },isRoot:function (node) {
        return node === node.parent;
      },isMountpoint:function (node) {
        return node.mounted;
      },isFile:function (mode) {
        return (mode & 61440) === 32768;
      },isDir:function (mode) {
        return (mode & 61440) === 16384;
      },isLink:function (mode) {
        return (mode & 61440) === 40960;
      },isChrdev:function (mode) {
        return (mode & 61440) === 8192;
      },isBlkdev:function (mode) {
        return (mode & 61440) === 24576;
      },isFIFO:function (mode) {
        return (mode & 61440) === 4096;
      },isSocket:function (mode) {
        return (mode & 49152) === 49152;
      },flagModes:{"r":0,"rs":1052672,"r+":2,"w":577,"wx":705,"xw":705,"w+":578,"wx+":706,"xw+":706,"a":1089,"ax":1217,"xa":1217,"a+":1090,"ax+":1218,"xa+":1218},modeStringToFlags:function (str) {
        var flags = FS.flagModes[str];
        if (typeof flags === 'undefined') {
          throw new Error('Unknown file open mode: ' + str);
        }
        return flags;
      },flagsToPermissionString:function (flag) {
        var accmode = flag & 2097155;
        var perms = ['r', 'w', 'rw'][accmode];
        if ((flag & 512)) {
          perms += 'w';
        }
        return perms;
      },nodePermissions:function (node, perms) {
        if (FS.ignorePermissions) {
          return 0;
        }
        // return 0 if any user, group or owner bits are set.
        if (perms.indexOf('r') !== -1 && !(node.mode & 292)) {
          return ERRNO_CODES.EACCES;
        } else if (perms.indexOf('w') !== -1 && !(node.mode & 146)) {
          return ERRNO_CODES.EACCES;
        } else if (perms.indexOf('x') !== -1 && !(node.mode & 73)) {
          return ERRNO_CODES.EACCES;
        }
        return 0;
      },mayLookup:function (dir) {
        return FS.nodePermissions(dir, 'x');
      },mayCreate:function (dir, name) {
        try {
          var node = FS.lookupNode(dir, name);
          return ERRNO_CODES.EEXIST;
        } catch (e) {
        }
        return FS.nodePermissions(dir, 'wx');
      },mayDelete:function (dir, name, isdir) {
        var node;
        try {
          node = FS.lookupNode(dir, name);
        } catch (e) {
          return e.errno;
        }
        var err = FS.nodePermissions(dir, 'wx');
        if (err) {
          return err;
        }
        if (isdir) {
          if (!FS.isDir(node.mode)) {
            return ERRNO_CODES.ENOTDIR;
          }
          if (FS.isRoot(node) || FS.getPath(node) === FS.cwd()) {
            return ERRNO_CODES.EBUSY;
          }
        } else {
          if (FS.isDir(node.mode)) {
            return ERRNO_CODES.EISDIR;
          }
        }
        return 0;
      },mayOpen:function (node, flags) {
        if (!node) {
          return ERRNO_CODES.ENOENT;
        }
        if (FS.isLink(node.mode)) {
          return ERRNO_CODES.ELOOP;
        } else if (FS.isDir(node.mode)) {
          if ((flags & 2097155) !== 0 ||  // opening for write
              (flags & 512)) {
            return ERRNO_CODES.EISDIR;
          }
        }
        return FS.nodePermissions(node, FS.flagsToPermissionString(flags));
      },MAX_OPEN_FDS:4096,nextfd:function (fd_start, fd_end) {
        fd_start = fd_start || 1;
        fd_end = fd_end || FS.MAX_OPEN_FDS;
        for (var fd = fd_start; fd <= fd_end; fd++) {
          if (!FS.streams[fd]) {
            return fd;
          }
        }
        throw new FS.ErrnoError(ERRNO_CODES.EMFILE);
      },getStream:function (fd) {
        return FS.streams[fd];
      },createStream:function (stream, fd_start, fd_end) {
        if (!FS.FSStream) {
          FS.FSStream = function(){};
          FS.FSStream.prototype = {};
          // compatibility
          Object.defineProperties(FS.FSStream.prototype, {
            object: {
              get: function() { return this.node; },
              set: function(val) { this.node = val; }
            },
            isRead: {
              get: function() { return (this.flags & 2097155) !== 1; }
            },
            isWrite: {
              get: function() { return (this.flags & 2097155) !== 0; }
            },
            isAppend: {
              get: function() { return (this.flags & 1024); }
            }
          });
        }
        if (stream.__proto__) {
          // reuse the object
          stream.__proto__ = FS.FSStream.prototype;
        } else {
          var newStream = new FS.FSStream();
          for (var p in stream) {
            newStream[p] = stream[p];
          }
          stream = newStream;
        }
        var fd = FS.nextfd(fd_start, fd_end);
        stream.fd = fd;
        FS.streams[fd] = stream;
        return stream;
      },closeStream:function (fd) {
        FS.streams[fd] = null;
      },chrdev_stream_ops:{open:function (stream) {
          var device = FS.getDevice(stream.node.rdev);
          // override node's stream ops with the device's
          stream.stream_ops = device.stream_ops;
          // forward the open call
          if (stream.stream_ops.open) {
            stream.stream_ops.open(stream);
          }
        },llseek:function () {
          throw new FS.ErrnoError(ERRNO_CODES.ESPIPE);
        }},major:function (dev) {
        return ((dev) >> 8);
      },minor:function (dev) {
        return ((dev) & 0xff);
      },makedev:function (ma, mi) {
        return ((ma) << 8 | (mi));
      },registerDevice:function (dev, ops) {
        FS.devices[dev] = { stream_ops: ops };
      },getDevice:function (dev) {
        return FS.devices[dev];
      },syncfs:function (populate, callback) {
        if (typeof(populate) === 'function') {
          callback = populate;
          populate = false;
        }
  
        var completed = 0;
        var total = FS.mounts.length;
        function done(err) {
          if (err) {
            return callback(err);
          }
          if (++completed >= total) {
            callback(null);
          }
        };
  
        // sync all mounts
        for (var i = 0; i < FS.mounts.length; i++) {
          var mount = FS.mounts[i];
          if (!mount.type.syncfs) {
            done(null);
            continue;
          }
          mount.type.syncfs(mount, populate, done);
        }
      },mount:function (type, opts, mountpoint) {
        var lookup;
        if (mountpoint) {
          lookup = FS.lookupPath(mountpoint, { follow: false });
          mountpoint = lookup.path;  // use the absolute path
        }
        var mount = {
          type: type,
          opts: opts,
          mountpoint: mountpoint,
          root: null
        };
        // create a root node for the fs
        var root = type.mount(mount);
        root.mount = mount;
        mount.root = root;
        // assign the mount info to the mountpoint's node
        if (lookup) {
          lookup.node.mount = mount;
          lookup.node.mounted = true;
          // compatibility update FS.root if we mount to /
          if (mountpoint === '/') {
            FS.root = mount.root;
          }
        }
        // add to our cached list of mounts
        FS.mounts.push(mount);
        return root;
      },lookup:function (parent, name) {
        return parent.node_ops.lookup(parent, name);
      },mknod:function (path, mode, dev) {
        var lookup = FS.lookupPath(path, { parent: true });
        var parent = lookup.node;
        var name = PATH.basename(path);
        var err = FS.mayCreate(parent, name);
        if (err) {
          throw new FS.ErrnoError(err);
        }
        if (!parent.node_ops.mknod) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        return parent.node_ops.mknod(parent, name, mode, dev);
      },create:function (path, mode) {
        mode = mode !== undefined ? mode : 0666;
        mode &= 4095;
        mode |= 32768;
        return FS.mknod(path, mode, 0);
      },mkdir:function (path, mode) {
        mode = mode !== undefined ? mode : 0777;
        mode &= 511 | 512;
        mode |= 16384;
        return FS.mknod(path, mode, 0);
      },mkdev:function (path, mode, dev) {
        if (typeof(dev) === 'undefined') {
          dev = mode;
          mode = 0666;
        }
        mode |= 8192;
        return FS.mknod(path, mode, dev);
      },symlink:function (oldpath, newpath) {
        var lookup = FS.lookupPath(newpath, { parent: true });
        var parent = lookup.node;
        var newname = PATH.basename(newpath);
        var err = FS.mayCreate(parent, newname);
        if (err) {
          throw new FS.ErrnoError(err);
        }
        if (!parent.node_ops.symlink) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        return parent.node_ops.symlink(parent, newname, oldpath);
      },rename:function (old_path, new_path) {
        var old_dirname = PATH.dirname(old_path);
        var new_dirname = PATH.dirname(new_path);
        var old_name = PATH.basename(old_path);
        var new_name = PATH.basename(new_path);
        // parents must exist
        var lookup, old_dir, new_dir;
        try {
          lookup = FS.lookupPath(old_path, { parent: true });
          old_dir = lookup.node;
          lookup = FS.lookupPath(new_path, { parent: true });
          new_dir = lookup.node;
        } catch (e) {
          throw new FS.ErrnoError(ERRNO_CODES.EBUSY);
        }
        // need to be part of the same mount
        if (old_dir.mount !== new_dir.mount) {
          throw new FS.ErrnoError(ERRNO_CODES.EXDEV);
        }
        // source must exist
        var old_node = FS.lookupNode(old_dir, old_name);
        // old path should not be an ancestor of the new path
        var relative = PATH.relative(old_path, new_dirname);
        if (relative.charAt(0) !== '.') {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        // new path should not be an ancestor of the old path
        relative = PATH.relative(new_path, old_dirname);
        if (relative.charAt(0) !== '.') {
          throw new FS.ErrnoError(ERRNO_CODES.ENOTEMPTY);
        }
        // see if the new path already exists
        var new_node;
        try {
          new_node = FS.lookupNode(new_dir, new_name);
        } catch (e) {
          // not fatal
        }
        // early out if nothing needs to change
        if (old_node === new_node) {
          return;
        }
        // we'll need to delete the old entry
        var isdir = FS.isDir(old_node.mode);
        var err = FS.mayDelete(old_dir, old_name, isdir);
        if (err) {
          throw new FS.ErrnoError(err);
        }
        // need delete permissions if we'll be overwriting.
        // need create permissions if new doesn't already exist.
        err = new_node ?
          FS.mayDelete(new_dir, new_name, isdir) :
          FS.mayCreate(new_dir, new_name);
        if (err) {
          throw new FS.ErrnoError(err);
        }
        if (!old_dir.node_ops.rename) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        if (FS.isMountpoint(old_node) || (new_node && FS.isMountpoint(new_node))) {
          throw new FS.ErrnoError(ERRNO_CODES.EBUSY);
        }
        // if we are going to change the parent, check write permissions
        if (new_dir !== old_dir) {
          err = FS.nodePermissions(old_dir, 'w');
          if (err) {
            throw new FS.ErrnoError(err);
          }
        }
        // remove the node from the lookup hash
        FS.hashRemoveNode(old_node);
        // do the underlying fs rename
        try {
          old_dir.node_ops.rename(old_node, new_dir, new_name);
        } catch (e) {
          throw e;
        } finally {
          // add the node back to the hash (in case node_ops.rename
          // changed its name)
          FS.hashAddNode(old_node);
        }
      },rmdir:function (path) {
        var lookup = FS.lookupPath(path, { parent: true });
        var parent = lookup.node;
        var name = PATH.basename(path);
        var node = FS.lookupNode(parent, name);
        var err = FS.mayDelete(parent, name, true);
        if (err) {
          throw new FS.ErrnoError(err);
        }
        if (!parent.node_ops.rmdir) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        if (FS.isMountpoint(node)) {
          throw new FS.ErrnoError(ERRNO_CODES.EBUSY);
        }
        parent.node_ops.rmdir(parent, name);
        FS.destroyNode(node);
      },readdir:function (path) {
        var lookup = FS.lookupPath(path, { follow: true });
        var node = lookup.node;
        if (!node.node_ops.readdir) {
          throw new FS.ErrnoError(ERRNO_CODES.ENOTDIR);
        }
        return node.node_ops.readdir(node);
      },unlink:function (path) {
        var lookup = FS.lookupPath(path, { parent: true });
        var parent = lookup.node;
        var name = PATH.basename(path);
        var node = FS.lookupNode(parent, name);
        var err = FS.mayDelete(parent, name, false);
        if (err) {
          // POSIX says unlink should set EPERM, not EISDIR
          if (err === ERRNO_CODES.EISDIR) err = ERRNO_CODES.EPERM;
          throw new FS.ErrnoError(err);
        }
        if (!parent.node_ops.unlink) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        if (FS.isMountpoint(node)) {
          throw new FS.ErrnoError(ERRNO_CODES.EBUSY);
        }
        parent.node_ops.unlink(parent, name);
        FS.destroyNode(node);
      },readlink:function (path) {
        var lookup = FS.lookupPath(path, { follow: false });
        var link = lookup.node;
        if (!link.node_ops.readlink) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        return link.node_ops.readlink(link);
      },stat:function (path, dontFollow) {
        var lookup = FS.lookupPath(path, { follow: !dontFollow });
        var node = lookup.node;
        if (!node.node_ops.getattr) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        return node.node_ops.getattr(node);
      },lstat:function (path) {
        return FS.stat(path, true);
      },chmod:function (path, mode, dontFollow) {
        var node;
        if (typeof path === 'string') {
          var lookup = FS.lookupPath(path, { follow: !dontFollow });
          node = lookup.node;
        } else {
          node = path;
        }
        if (!node.node_ops.setattr) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        node.node_ops.setattr(node, {
          mode: (mode & 4095) | (node.mode & ~4095),
          timestamp: Date.now()
        });
      },lchmod:function (path, mode) {
        FS.chmod(path, mode, true);
      },fchmod:function (fd, mode) {
        var stream = FS.getStream(fd);
        if (!stream) {
          throw new FS.ErrnoError(ERRNO_CODES.EBADF);
        }
        FS.chmod(stream.node, mode);
      },chown:function (path, uid, gid, dontFollow) {
        var node;
        if (typeof path === 'string') {
          var lookup = FS.lookupPath(path, { follow: !dontFollow });
          node = lookup.node;
        } else {
          node = path;
        }
        if (!node.node_ops.setattr) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        node.node_ops.setattr(node, {
          timestamp: Date.now()
          // we ignore the uid / gid for now
        });
      },lchown:function (path, uid, gid) {
        FS.chown(path, uid, gid, true);
      },fchown:function (fd, uid, gid) {
        var stream = FS.getStream(fd);
        if (!stream) {
          throw new FS.ErrnoError(ERRNO_CODES.EBADF);
        }
        FS.chown(stream.node, uid, gid);
      },truncate:function (path, len) {
        if (len < 0) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        var node;
        if (typeof path === 'string') {
          var lookup = FS.lookupPath(path, { follow: true });
          node = lookup.node;
        } else {
          node = path;
        }
        if (!node.node_ops.setattr) {
          throw new FS.ErrnoError(ERRNO_CODES.EPERM);
        }
        if (FS.isDir(node.mode)) {
          throw new FS.ErrnoError(ERRNO_CODES.EISDIR);
        }
        if (!FS.isFile(node.mode)) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        var err = FS.nodePermissions(node, 'w');
        if (err) {
          throw new FS.ErrnoError(err);
        }
        node.node_ops.setattr(node, {
          size: len,
          timestamp: Date.now()
        });
      },ftruncate:function (fd, len) {
        var stream = FS.getStream(fd);
        if (!stream) {
          throw new FS.ErrnoError(ERRNO_CODES.EBADF);
        }
        if ((stream.flags & 2097155) === 0) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        FS.truncate(stream.node, len);
      },utime:function (path, atime, mtime) {
        var lookup = FS.lookupPath(path, { follow: true });
        var node = lookup.node;
        node.node_ops.setattr(node, {
          timestamp: Math.max(atime, mtime)
        });
      },open:function (path, flags, mode, fd_start, fd_end) {
        flags = typeof flags === 'string' ? FS.modeStringToFlags(flags) : flags;
        mode = typeof mode === 'undefined' ? 0666 : mode;
        if ((flags & 64)) {
          mode = (mode & 4095) | 32768;
        } else {
          mode = 0;
        }
        var node;
        if (typeof path === 'object') {
          node = path;
        } else {
          path = PATH.normalize(path);
          try {
            var lookup = FS.lookupPath(path, {
              follow: !(flags & 131072)
            });
            node = lookup.node;
          } catch (e) {
            // ignore
          }
        }
        // perhaps we need to create the node
        if ((flags & 64)) {
          if (node) {
            // if O_CREAT and O_EXCL are set, error out if the node already exists
            if ((flags & 128)) {
              throw new FS.ErrnoError(ERRNO_CODES.EEXIST);
            }
          } else {
            // node doesn't exist, try to create it
            node = FS.mknod(path, mode, 0);
          }
        }
        if (!node) {
          throw new FS.ErrnoError(ERRNO_CODES.ENOENT);
        }
        // can't truncate a device
        if (FS.isChrdev(node.mode)) {
          flags &= ~512;
        }
        // check permissions
        var err = FS.mayOpen(node, flags);
        if (err) {
          throw new FS.ErrnoError(err);
        }
        // do truncation if necessary
        if ((flags & 512)) {
          FS.truncate(node, 0);
        }
        // we've already handled these, don't pass down to the underlying vfs
        flags &= ~(128 | 512);
  
        // register the stream with the filesystem
        var stream = FS.createStream({
          node: node,
          path: FS.getPath(node),  // we want the absolute path to the node
          flags: flags,
          seekable: true,
          position: 0,
          stream_ops: node.stream_ops,
          // used by the file family libc calls (fopen, fwrite, ferror, etc.)
          ungotten: [],
          error: false
        }, fd_start, fd_end);
        // call the new stream's open function
        if (stream.stream_ops.open) {
          stream.stream_ops.open(stream);
        }
        if (Module['logReadFiles'] && !(flags & 1)) {
          if (!FS.readFiles) FS.readFiles = {};
          if (!(path in FS.readFiles)) {
            FS.readFiles[path] = 1;
            Module['printErr']('read file: ' + path);
          }
        }
        return stream;
      },close:function (stream) {
        try {
          if (stream.stream_ops.close) {
            stream.stream_ops.close(stream);
          }
        } catch (e) {
          throw e;
        } finally {
          FS.closeStream(stream.fd);
        }
      },llseek:function (stream, offset, whence) {
        if (!stream.seekable || !stream.stream_ops.llseek) {
          throw new FS.ErrnoError(ERRNO_CODES.ESPIPE);
        }
        return stream.stream_ops.llseek(stream, offset, whence);
      },read:function (stream, buffer, offset, length, position) {
        if (length < 0 || position < 0) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        if ((stream.flags & 2097155) === 1) {
          throw new FS.ErrnoError(ERRNO_CODES.EBADF);
        }
        if (FS.isDir(stream.node.mode)) {
          throw new FS.ErrnoError(ERRNO_CODES.EISDIR);
        }
        if (!stream.stream_ops.read) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        var seeking = true;
        if (typeof position === 'undefined') {
          position = stream.position;
          seeking = false;
        } else if (!stream.seekable) {
          throw new FS.ErrnoError(ERRNO_CODES.ESPIPE);
        }
        var bytesRead = stream.stream_ops.read(stream, buffer, offset, length, position);
        if (!seeking) stream.position += bytesRead;
        return bytesRead;
      },write:function (stream, buffer, offset, length, position, canOwn) {
        if (length < 0 || position < 0) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        if ((stream.flags & 2097155) === 0) {
          throw new FS.ErrnoError(ERRNO_CODES.EBADF);
        }
        if (FS.isDir(stream.node.mode)) {
          throw new FS.ErrnoError(ERRNO_CODES.EISDIR);
        }
        if (!stream.stream_ops.write) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        var seeking = true;
        if (typeof position === 'undefined') {
          position = stream.position;
          seeking = false;
        } else if (!stream.seekable) {
          throw new FS.ErrnoError(ERRNO_CODES.ESPIPE);
        }
        if (stream.flags & 1024) {
          // seek to the end before writing in append mode
          FS.llseek(stream, 0, 2);
        }
        var bytesWritten = stream.stream_ops.write(stream, buffer, offset, length, position, canOwn);
        if (!seeking) stream.position += bytesWritten;
        return bytesWritten;
      },allocate:function (stream, offset, length) {
        if (offset < 0 || length <= 0) {
          throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
        }
        if ((stream.flags & 2097155) === 0) {
          throw new FS.ErrnoError(ERRNO_CODES.EBADF);
        }
        if (!FS.isFile(stream.node.mode) && !FS.isDir(node.mode)) {
          throw new FS.ErrnoError(ERRNO_CODES.ENODEV);
        }
        if (!stream.stream_ops.allocate) {
          throw new FS.ErrnoError(ERRNO_CODES.EOPNOTSUPP);
        }
        stream.stream_ops.allocate(stream, offset, length);
      },mmap:function (stream, buffer, offset, length, position, prot, flags) {
        // TODO if PROT is PROT_WRITE, make sure we have write access
        if ((stream.flags & 2097155) === 1) {
          throw new FS.ErrnoError(ERRNO_CODES.EACCES);
        }
        if (!stream.stream_ops.mmap) {
          throw new FS.ErrnoError(ERRNO_CODES.ENODEV);
        }
        return stream.stream_ops.mmap(stream, buffer, offset, length, position, prot, flags);
      },ioctl:function (stream, cmd, arg) {
        if (!stream.stream_ops.ioctl) {
          throw new FS.ErrnoError(ERRNO_CODES.ENOTTY);
        }
        return stream.stream_ops.ioctl(stream, cmd, arg);
      },readFile:function (path, opts) {
        opts = opts || {};
        opts.flags = opts.flags || 'r';
        opts.encoding = opts.encoding || 'binary';
        var ret;
        var stream = FS.open(path, opts.flags);
        var stat = FS.stat(path);
        var length = stat.size;
        var buf = new Uint8Array(length);
        FS.read(stream, buf, 0, length, 0);
        if (opts.encoding === 'utf8') {
          ret = '';
          var utf8 = new Runtime.UTF8Processor();
          for (var i = 0; i < length; i++) {
            ret += utf8.processCChar(buf[i]);
          }
        } else if (opts.encoding === 'binary') {
          ret = buf;
        } else {
          throw new Error('Invalid encoding type "' + opts.encoding + '"');
        }
        FS.close(stream);
        return ret;
      },writeFile:function (path, data, opts) {
        opts = opts || {};
        opts.flags = opts.flags || 'w';
        opts.encoding = opts.encoding || 'utf8';
        var stream = FS.open(path, opts.flags, opts.mode);
        if (opts.encoding === 'utf8') {
          var utf8 = new Runtime.UTF8Processor();
          var buf = new Uint8Array(utf8.processJSString(data));
          FS.write(stream, buf, 0, buf.length, 0);
        } else if (opts.encoding === 'binary') {
          FS.write(stream, data, 0, data.length, 0);
        } else {
          throw new Error('Invalid encoding type "' + opts.encoding + '"');
        }
        FS.close(stream);
      },cwd:function () {
        return FS.currentPath;
      },chdir:function (path) {
        var lookup = FS.lookupPath(path, { follow: true });
        if (!FS.isDir(lookup.node.mode)) {
          throw new FS.ErrnoError(ERRNO_CODES.ENOTDIR);
        }
        var err = FS.nodePermissions(lookup.node, 'x');
        if (err) {
          throw new FS.ErrnoError(err);
        }
        FS.currentPath = lookup.path;
      },createDefaultDirectories:function () {
        FS.mkdir('/tmp');
      },createDefaultDevices:function () {
        // create /dev
        FS.mkdir('/dev');
        // setup /dev/null
        FS.registerDevice(FS.makedev(1, 3), {
          read: function() { return 0; },
          write: function() { return 0; }
        });
        FS.mkdev('/dev/null', FS.makedev(1, 3));
        // setup /dev/tty and /dev/tty1
        // stderr needs to print output using Module['printErr']
        // so we register a second tty just for it.
        TTY.register(FS.makedev(5, 0), TTY.default_tty_ops);
        TTY.register(FS.makedev(6, 0), TTY.default_tty1_ops);
        FS.mkdev('/dev/tty', FS.makedev(5, 0));
        FS.mkdev('/dev/tty1', FS.makedev(6, 0));
        // we're not going to emulate the actual shm device,
        // just create the tmp dirs that reside in it commonly
        FS.mkdir('/dev/shm');
        FS.mkdir('/dev/shm/tmp');
      },createStandardStreams:function () {
        // TODO deprecate the old functionality of a single
        // input / output callback and that utilizes FS.createDevice
        // and instead require a unique set of stream ops
  
        // by default, we symlink the standard streams to the
        // default tty devices. however, if the standard streams
        // have been overwritten we create a unique device for
        // them instead.
        if (Module['stdin']) {
          FS.createDevice('/dev', 'stdin', Module['stdin']);
        } else {
          FS.symlink('/dev/tty', '/dev/stdin');
        }
        if (Module['stdout']) {
          FS.createDevice('/dev', 'stdout', null, Module['stdout']);
        } else {
          FS.symlink('/dev/tty', '/dev/stdout');
        }
        if (Module['stderr']) {
          FS.createDevice('/dev', 'stderr', null, Module['stderr']);
        } else {
          FS.symlink('/dev/tty1', '/dev/stderr');
        }
  
        // open default streams for the stdin, stdout and stderr devices
        var stdin = FS.open('/dev/stdin', 'r');
        HEAP32[((_stdin)>>2)]=stdin.fd;
        assert(stdin.fd === 1, 'invalid handle for stdin (' + stdin.fd + ')');
  
        var stdout = FS.open('/dev/stdout', 'w');
        HEAP32[((_stdout)>>2)]=stdout.fd;
        assert(stdout.fd === 2, 'invalid handle for stdout (' + stdout.fd + ')');
  
        var stderr = FS.open('/dev/stderr', 'w');
        HEAP32[((_stderr)>>2)]=stderr.fd;
        assert(stderr.fd === 3, 'invalid handle for stderr (' + stderr.fd + ')');
      },ensureErrnoError:function () {
        if (FS.ErrnoError) return;
        FS.ErrnoError = function ErrnoError(errno) {
          this.errno = errno;
          for (var key in ERRNO_CODES) {
            if (ERRNO_CODES[key] === errno) {
              this.code = key;
              break;
            }
          }
          this.message = ERRNO_MESSAGES[errno];
        };
        FS.ErrnoError.prototype = new Error();
        FS.ErrnoError.prototype.constructor = FS.ErrnoError;
        // Some errors may happen quite a bit, to avoid overhead we reuse them (and suffer a lack of stack info)
        [ERRNO_CODES.ENOENT].forEach(function(code) {
          FS.genericErrors[code] = new FS.ErrnoError(code);
          FS.genericErrors[code].stack = '<generic error, no stack>';
        });
      },staticInit:function () {
        FS.ensureErrnoError();
  
        FS.nameTable = new Array(4096);
  
        FS.root = FS.createNode(null, '/', 16384 | 0777, 0);
        FS.mount(MEMFS, {}, '/');
  
        FS.createDefaultDirectories();
        FS.createDefaultDevices();
      },init:function (input, output, error) {
        assert(!FS.init.initialized, 'FS.init was previously called. If you want to initialize later with custom parameters, remove any earlier calls (note that one is automatically added to the generated code)');
        FS.init.initialized = true;
  
        FS.ensureErrnoError();
  
        // Allow Module.stdin etc. to provide defaults, if none explicitly passed to us here
        Module['stdin'] = input || Module['stdin'];
        Module['stdout'] = output || Module['stdout'];
        Module['stderr'] = error || Module['stderr'];
  
        FS.createStandardStreams();
      },quit:function () {
        FS.init.initialized = false;
        for (var i = 0; i < FS.streams.length; i++) {
          var stream = FS.streams[i];
          if (!stream) {
            continue;
          }
          FS.close(stream);
        }
      },getMode:function (canRead, canWrite) {
        var mode = 0;
        if (canRead) mode |= 292 | 73;
        if (canWrite) mode |= 146;
        return mode;
      },joinPath:function (parts, forceRelative) {
        var path = PATH.join.apply(null, parts);
        if (forceRelative && path[0] == '/') path = path.substr(1);
        return path;
      },absolutePath:function (relative, base) {
        return PATH.resolve(base, relative);
      },standardizePath:function (path) {
        return PATH.normalize(path);
      },findObject:function (path, dontResolveLastLink) {
        var ret = FS.analyzePath(path, dontResolveLastLink);
        if (ret.exists) {
          return ret.object;
        } else {
          ___setErrNo(ret.error);
          return null;
        }
      },analyzePath:function (path, dontResolveLastLink) {
        // operate from within the context of the symlink's target
        try {
          var lookup = FS.lookupPath(path, { follow: !dontResolveLastLink });
          path = lookup.path;
        } catch (e) {
        }
        var ret = {
          isRoot: false, exists: false, error: 0, name: null, path: null, object: null,
          parentExists: false, parentPath: null, parentObject: null
        };
        try {
          var lookup = FS.lookupPath(path, { parent: true });
          ret.parentExists = true;
          ret.parentPath = lookup.path;
          ret.parentObject = lookup.node;
          ret.name = PATH.basename(path);
          lookup = FS.lookupPath(path, { follow: !dontResolveLastLink });
          ret.exists = true;
          ret.path = lookup.path;
          ret.object = lookup.node;
          ret.name = lookup.node.name;
          ret.isRoot = lookup.path === '/';
        } catch (e) {
          ret.error = e.errno;
        };
        return ret;
      },createFolder:function (parent, name, canRead, canWrite) {
        var path = PATH.join2(typeof parent === 'string' ? parent : FS.getPath(parent), name);
        var mode = FS.getMode(canRead, canWrite);
        return FS.mkdir(path, mode);
      },createPath:function (parent, path, canRead, canWrite) {
        parent = typeof parent === 'string' ? parent : FS.getPath(parent);
        var parts = path.split('/').reverse();
        while (parts.length) {
          var part = parts.pop();
          if (!part) continue;
          var current = PATH.join2(parent, part);
          try {
            FS.mkdir(current);
          } catch (e) {
            // ignore EEXIST
          }
          parent = current;
        }
        return current;
      },createFile:function (parent, name, properties, canRead, canWrite) {
        var path = PATH.join2(typeof parent === 'string' ? parent : FS.getPath(parent), name);
        var mode = FS.getMode(canRead, canWrite);
        return FS.create(path, mode);
      },createDataFile:function (parent, name, data, canRead, canWrite, canOwn) {
        var path = name ? PATH.join2(typeof parent === 'string' ? parent : FS.getPath(parent), name) : parent;
        var mode = FS.getMode(canRead, canWrite);
        var node = FS.create(path, mode);
        if (data) {
          if (typeof data === 'string') {
            var arr = new Array(data.length);
            for (var i = 0, len = data.length; i < len; ++i) arr[i] = data.charCodeAt(i);
            data = arr;
          }
          // make sure we can write to the file
          FS.chmod(node, mode | 146);
          var stream = FS.open(node, 'w');
          FS.write(stream, data, 0, data.length, 0, canOwn);
          FS.close(stream);
          FS.chmod(node, mode);
        }
        return node;
      },createDevice:function (parent, name, input, output) {
        var path = PATH.join2(typeof parent === 'string' ? parent : FS.getPath(parent), name);
        var mode = FS.getMode(!!input, !!output);
        if (!FS.createDevice.major) FS.createDevice.major = 64;
        var dev = FS.makedev(FS.createDevice.major++, 0);
        // Create a fake device that a set of stream ops to emulate
        // the old behavior.
        FS.registerDevice(dev, {
          open: function(stream) {
            stream.seekable = false;
          },
          close: function(stream) {
            // flush any pending line data
            if (output && output.buffer && output.buffer.length) {
              output(10);
            }
          },
          read: function(stream, buffer, offset, length, pos /* ignored */) {
            var bytesRead = 0;
            for (var i = 0; i < length; i++) {
              var result;
              try {
                result = input();
              } catch (e) {
                throw new FS.ErrnoError(ERRNO_CODES.EIO);
              }
              if (result === undefined && bytesRead === 0) {
                throw new FS.ErrnoError(ERRNO_CODES.EAGAIN);
              }
              if (result === null || result === undefined) break;
              bytesRead++;
              buffer[offset+i] = result;
            }
            if (bytesRead) {
              stream.node.timestamp = Date.now();
            }
            return bytesRead;
          },
          write: function(stream, buffer, offset, length, pos) {
            for (var i = 0; i < length; i++) {
              try {
                output(buffer[offset+i]);
              } catch (e) {
                throw new FS.ErrnoError(ERRNO_CODES.EIO);
              }
            }
            if (length) {
              stream.node.timestamp = Date.now();
            }
            return i;
          }
        });
        return FS.mkdev(path, mode, dev);
      },createLink:function (parent, name, target, canRead, canWrite) {
        var path = PATH.join2(typeof parent === 'string' ? parent : FS.getPath(parent), name);
        return FS.symlink(target, path);
      },forceLoadFile:function (obj) {
        if (obj.isDevice || obj.isFolder || obj.link || obj.contents) return true;
        var success = true;
        if (typeof XMLHttpRequest !== 'undefined') {
          throw new Error("Lazy loading should have been performed (contents set) in createLazyFile, but it was not. Lazy loading only works in web workers. Use --embed-file or --preload-file in emcc on the main thread.");
        } else if (Module['read']) {
          // Command-line.
          try {
            // WARNING: Can't read binary files in V8's d8 or tracemonkey's js, as
            //          read() will try to parse UTF8.
            obj.contents = intArrayFromString(Module['read'](obj.url), true);
          } catch (e) {
            success = false;
          }
        } else {
          throw new Error('Cannot load without read() or XMLHttpRequest.');
        }
        if (!success) ___setErrNo(ERRNO_CODES.EIO);
        return success;
      },createLazyFile:function (parent, name, url, canRead, canWrite) {
        if (typeof XMLHttpRequest !== 'undefined') {
          if (!ENVIRONMENT_IS_WORKER) throw 'Cannot do synchronous binary XHRs outside webworkers in modern browsers. Use --embed-file or --preload-file in emcc';
          // Lazy chunked Uint8Array (implements get and length from Uint8Array). Actual getting is abstracted away for eventual reuse.
          function LazyUint8Array() {
            this.lengthKnown = false;
            this.chunks = []; // Loaded chunks. Index is the chunk number
          }
          LazyUint8Array.prototype.get = function LazyUint8Array_get(idx) {
            if (idx > this.length-1 || idx < 0) {
              return undefined;
            }
            var chunkOffset = idx % this.chunkSize;
            var chunkNum = Math.floor(idx / this.chunkSize);
            return this.getter(chunkNum)[chunkOffset];
          }
          LazyUint8Array.prototype.setDataGetter = function LazyUint8Array_setDataGetter(getter) {
            this.getter = getter;
          }
          LazyUint8Array.prototype.cacheLength = function LazyUint8Array_cacheLength() {
              // Find length
              var xhr = new XMLHttpRequest();
              xhr.open('HEAD', url, false);
              xhr.send(null);
              if (!(xhr.status >= 200 && xhr.status < 300 || xhr.status === 304)) throw new Error("Couldn't load " + url + ". Status: " + xhr.status);
              var datalength = Number(xhr.getResponseHeader("Content-length"));
              var header;
              var hasByteServing = (header = xhr.getResponseHeader("Accept-Ranges")) && header === "bytes";
              var chunkSize = 1024*1024; // Chunk size in bytes
  
              if (!hasByteServing) chunkSize = datalength;
  
              // Function to get a range from the remote URL.
              var doXHR = (function(from, to) {
                if (from > to) throw new Error("invalid range (" + from + ", " + to + ") or no bytes requested!");
                if (to > datalength-1) throw new Error("only " + datalength + " bytes available! programmer error!");
  
                // TODO: Use mozResponseArrayBuffer, responseStream, etc. if available.
                var xhr = new XMLHttpRequest();
                xhr.open('GET', url, false);
                if (datalength !== chunkSize) xhr.setRequestHeader("Range", "bytes=" + from + "-" + to);
  
                // Some hints to the browser that we want binary data.
                if (typeof Uint8Array != 'undefined') xhr.responseType = 'arraybuffer';
                if (xhr.overrideMimeType) {
                  xhr.overrideMimeType('text/plain; charset=x-user-defined');
                }
  
                xhr.send(null);
                if (!(xhr.status >= 200 && xhr.status < 300 || xhr.status === 304)) throw new Error("Couldn't load " + url + ". Status: " + xhr.status);
                if (xhr.response !== undefined) {
                  return new Uint8Array(xhr.response || []);
                } else {
                  return intArrayFromString(xhr.responseText || '', true);
                }
              });
              var lazyArray = this;
              lazyArray.setDataGetter(function(chunkNum) {
                var start = chunkNum * chunkSize;
                var end = (chunkNum+1) * chunkSize - 1; // including this byte
                end = Math.min(end, datalength-1); // if datalength-1 is selected, this is the last block
                if (typeof(lazyArray.chunks[chunkNum]) === "undefined") {
                  lazyArray.chunks[chunkNum] = doXHR(start, end);
                }
                if (typeof(lazyArray.chunks[chunkNum]) === "undefined") throw new Error("doXHR failed!");
                return lazyArray.chunks[chunkNum];
              });
  
              this._length = datalength;
              this._chunkSize = chunkSize;
              this.lengthKnown = true;
          }
  
          var lazyArray = new LazyUint8Array();
          Object.defineProperty(lazyArray, "length", {
              get: function() {
                  if(!this.lengthKnown) {
                      this.cacheLength();
                  }
                  return this._length;
              }
          });
          Object.defineProperty(lazyArray, "chunkSize", {
              get: function() {
                  if(!this.lengthKnown) {
                      this.cacheLength();
                  }
                  return this._chunkSize;
              }
          });
  
          var properties = { isDevice: false, contents: lazyArray };
        } else {
          var properties = { isDevice: false, url: url };
        }
  
        var node = FS.createFile(parent, name, properties, canRead, canWrite);
        // This is a total hack, but I want to get this lazy file code out of the
        // core of MEMFS. If we want to keep this lazy file concept I feel it should
        // be its own thin LAZYFS proxying calls to MEMFS.
        if (properties.contents) {
          node.contents = properties.contents;
        } else if (properties.url) {
          node.contents = null;
          node.url = properties.url;
        }
        // override each stream op with one that tries to force load the lazy file first
        var stream_ops = {};
        var keys = Object.keys(node.stream_ops);
        keys.forEach(function(key) {
          var fn = node.stream_ops[key];
          stream_ops[key] = function forceLoadLazyFile() {
            if (!FS.forceLoadFile(node)) {
              throw new FS.ErrnoError(ERRNO_CODES.EIO);
            }
            return fn.apply(null, arguments);
          };
        });
        // use a custom read function
        stream_ops.read = function stream_ops_read(stream, buffer, offset, length, position) {
          if (!FS.forceLoadFile(node)) {
            throw new FS.ErrnoError(ERRNO_CODES.EIO);
          }
          var contents = stream.node.contents;
          if (position >= contents.length)
            return 0;
          var size = Math.min(contents.length - position, length);
          assert(size >= 0);
          if (contents.slice) { // normal array
            for (var i = 0; i < size; i++) {
              buffer[offset + i] = contents[position + i];
            }
          } else {
            for (var i = 0; i < size; i++) { // LazyUint8Array from sync binary XHR
              buffer[offset + i] = contents.get(position + i);
            }
          }
          return size;
        };
        node.stream_ops = stream_ops;
        return node;
      },createPreloadedFile:function (parent, name, url, canRead, canWrite, onload, onerror, dontCreateFile, canOwn) {
        Browser.init();
        // TODO we should allow people to just pass in a complete filename instead
        // of parent and name being that we just join them anyways
        var fullname = name ? PATH.resolve(PATH.join2(parent, name)) : parent;
        function processData(byteArray) {
          function finish(byteArray) {
            if (!dontCreateFile) {
              FS.createDataFile(parent, name, byteArray, canRead, canWrite, canOwn);
            }
            if (onload) onload();
            removeRunDependency('cp ' + fullname);
          }
          var handled = false;
          Module['preloadPlugins'].forEach(function(plugin) {
            if (handled) return;
            if (plugin['canHandle'](fullname)) {
              plugin['handle'](byteArray, fullname, finish, function() {
                if (onerror) onerror();
                removeRunDependency('cp ' + fullname);
              });
              handled = true;
            }
          });
          if (!handled) finish(byteArray);
        }
        addRunDependency('cp ' + fullname);
        if (typeof url == 'string') {
          Browser.asyncLoad(url, function(byteArray) {
            processData(byteArray);
          }, onerror);
        } else {
          processData(url);
        }
      },indexedDB:function () {
        return window.indexedDB || window.mozIndexedDB || window.webkitIndexedDB || window.msIndexedDB;
      },DB_NAME:function () {
        return 'EM_FS_' + window.location.pathname;
      },DB_VERSION:20,DB_STORE_NAME:"FILE_DATA",saveFilesToDB:function (paths, onload, onerror) {
        onload = onload || function(){};
        onerror = onerror || function(){};
        var indexedDB = FS.indexedDB();
        try {
          var openRequest = indexedDB.open(FS.DB_NAME(), FS.DB_VERSION);
        } catch (e) {
          return onerror(e);
        }
        openRequest.onupgradeneeded = function openRequest_onupgradeneeded() {
          console.log('creating db');
          var db = openRequest.result;
          db.createObjectStore(FS.DB_STORE_NAME);
        };
        openRequest.onsuccess = function openRequest_onsuccess() {
          var db = openRequest.result;
          var transaction = db.transaction([FS.DB_STORE_NAME], 'readwrite');
          var files = transaction.objectStore(FS.DB_STORE_NAME);
          var ok = 0, fail = 0, total = paths.length;
          function finish() {
            if (fail == 0) onload(); else onerror();
          }
          paths.forEach(function(path) {
            var putRequest = files.put(FS.analyzePath(path).object.contents, path);
            putRequest.onsuccess = function putRequest_onsuccess() { ok++; if (ok + fail == total) finish() };
            putRequest.onerror = function putRequest_onerror() { fail++; if (ok + fail == total) finish() };
          });
          transaction.onerror = onerror;
        };
        openRequest.onerror = onerror;
      },loadFilesFromDB:function (paths, onload, onerror) {
        onload = onload || function(){};
        onerror = onerror || function(){};
        var indexedDB = FS.indexedDB();
        try {
          var openRequest = indexedDB.open(FS.DB_NAME(), FS.DB_VERSION);
        } catch (e) {
          return onerror(e);
        }
        openRequest.onupgradeneeded = onerror; // no database to load from
        openRequest.onsuccess = function openRequest_onsuccess() {
          var db = openRequest.result;
          try {
            var transaction = db.transaction([FS.DB_STORE_NAME], 'readonly');
          } catch(e) {
            onerror(e);
            return;
          }
          var files = transaction.objectStore(FS.DB_STORE_NAME);
          var ok = 0, fail = 0, total = paths.length;
          function finish() {
            if (fail == 0) onload(); else onerror();
          }
          paths.forEach(function(path) {
            var getRequest = files.get(path);
            getRequest.onsuccess = function getRequest_onsuccess() {
              if (FS.analyzePath(path).exists) {
                FS.unlink(path);
              }
              FS.createDataFile(PATH.dirname(path), PATH.basename(path), getRequest.result, true, true, true);
              ok++;
              if (ok + fail == total) finish();
            };
            getRequest.onerror = function getRequest_onerror() { fail++; if (ok + fail == total) finish() };
          });
          transaction.onerror = onerror;
        };
        openRequest.onerror = onerror;
      }};
  
  
  
  
  var _mkport=undefined;var SOCKFS={mount:function (mount) {
        return FS.createNode(null, '/', 16384 | 0777, 0);
      },createSocket:function (family, type, protocol) {
        var streaming = type == 1;
        if (protocol) {
          assert(streaming == (protocol == 6)); // if SOCK_STREAM, must be tcp
        }
  
        // create our internal socket structure
        var sock = {
          family: family,
          type: type,
          protocol: protocol,
          server: null,
          peers: {},
          pending: [],
          recv_queue: [],
          sock_ops: SOCKFS.websocket_sock_ops
        };
  
        // create the filesystem node to store the socket structure
        var name = SOCKFS.nextname();
        var node = FS.createNode(SOCKFS.root, name, 49152, 0);
        node.sock = sock;
  
        // and the wrapping stream that enables library functions such
        // as read and write to indirectly interact with the socket
        var stream = FS.createStream({
          path: name,
          node: node,
          flags: FS.modeStringToFlags('r+'),
          seekable: false,
          stream_ops: SOCKFS.stream_ops
        });
  
        // map the new stream to the socket structure (sockets have a 1:1
        // relationship with a stream)
        sock.stream = stream;
  
        return sock;
      },getSocket:function (fd) {
        var stream = FS.getStream(fd);
        if (!stream || !FS.isSocket(stream.node.mode)) {
          return null;
        }
        return stream.node.sock;
      },stream_ops:{poll:function (stream) {
          var sock = stream.node.sock;
          return sock.sock_ops.poll(sock);
        },ioctl:function (stream, request, varargs) {
          var sock = stream.node.sock;
          return sock.sock_ops.ioctl(sock, request, varargs);
        },read:function (stream, buffer, offset, length, position /* ignored */) {
          var sock = stream.node.sock;
          var msg = sock.sock_ops.recvmsg(sock, length);
          if (!msg) {
            // socket is closed
            return 0;
          }
          buffer.set(msg.buffer, offset);
          return msg.buffer.length;
        },write:function (stream, buffer, offset, length, position /* ignored */) {
          var sock = stream.node.sock;
          return sock.sock_ops.sendmsg(sock, buffer, offset, length);
        },close:function (stream) {
          var sock = stream.node.sock;
          sock.sock_ops.close(sock);
        }},nextname:function () {
        if (!SOCKFS.nextname.current) {
          SOCKFS.nextname.current = 0;
        }
        return 'socket[' + (SOCKFS.nextname.current++) + ']';
      },websocket_sock_ops:{createPeer:function (sock, addr, port) {
          var ws;
  
          if (typeof addr === 'object') {
            ws = addr;
            addr = null;
            port = null;
          }
  
          if (ws) {
            // for sockets that've already connected (e.g. we're the server)
            // we can inspect the _socket property for the address
            if (ws._socket) {
              addr = ws._socket.remoteAddress;
              port = ws._socket.remotePort;
            }
            // if we're just now initializing a connection to the remote,
            // inspect the url property
            else {
              var result = /ws[s]?:\/\/([^:]+):(\d+)/.exec(ws.url);
              if (!result) {
                throw new Error('WebSocket URL must be in the format ws(s)://address:port');
              }
              addr = result[1];
              port = parseInt(result[2], 10);
            }
          } else {
            // create the actual websocket object and connect
            try {
              var url = 'ws://' + addr + ':' + port;
              // the node ws library API is slightly different than the browser's
              var opts = ENVIRONMENT_IS_NODE ? {headers: {'websocket-protocol': ['binary']}} : ['binary'];
              // If node we use the ws library.
              var WebSocket = ENVIRONMENT_IS_NODE ? require('ws') : window['WebSocket'];
              ws = new WebSocket(url, opts);
              ws.binaryType = 'arraybuffer';
            } catch (e) {
              throw new FS.ErrnoError(ERRNO_CODES.EHOSTUNREACH);
            }
          }
  
  
          var peer = {
            addr: addr,
            port: port,
            socket: ws,
            dgram_send_queue: []
          };
  
          SOCKFS.websocket_sock_ops.addPeer(sock, peer);
          SOCKFS.websocket_sock_ops.handlePeerEvents(sock, peer);
  
          // if this is a bound dgram socket, send the port number first to allow
          // us to override the ephemeral port reported to us by remotePort on the
          // remote end.
          if (sock.type === 2 && typeof sock.sport !== 'undefined') {
            peer.dgram_send_queue.push(new Uint8Array([
                255, 255, 255, 255,
                'p'.charCodeAt(0), 'o'.charCodeAt(0), 'r'.charCodeAt(0), 't'.charCodeAt(0),
                ((sock.sport & 0xff00) >> 8) , (sock.sport & 0xff)
            ]));
          }
  
          return peer;
        },getPeer:function (sock, addr, port) {
          return sock.peers[addr + ':' + port];
        },addPeer:function (sock, peer) {
          sock.peers[peer.addr + ':' + peer.port] = peer;
        },removePeer:function (sock, peer) {
          delete sock.peers[peer.addr + ':' + peer.port];
        },handlePeerEvents:function (sock, peer) {
          var first = true;
  
          var handleOpen = function () {
            try {
              var queued = peer.dgram_send_queue.shift();
              while (queued) {
                peer.socket.send(queued);
                queued = peer.dgram_send_queue.shift();
              }
            } catch (e) {
              // not much we can do here in the way of proper error handling as we've already
              // lied and said this data was sent. shut it down.
              peer.socket.close();
            }
          };
  
          function handleMessage(data) {
            assert(typeof data !== 'string' && data.byteLength !== undefined);  // must receive an ArrayBuffer
            data = new Uint8Array(data);  // make a typed array view on the array buffer
  
  
            // if this is the port message, override the peer's port with it
            var wasfirst = first;
            first = false;
            if (wasfirst &&
                data.length === 10 &&
                data[0] === 255 && data[1] === 255 && data[2] === 255 && data[3] === 255 &&
                data[4] === 'p'.charCodeAt(0) && data[5] === 'o'.charCodeAt(0) && data[6] === 'r'.charCodeAt(0) && data[7] === 't'.charCodeAt(0)) {
              // update the peer's port and it's key in the peer map
              var newport = ((data[8] << 8) | data[9]);
              SOCKFS.websocket_sock_ops.removePeer(sock, peer);
              peer.port = newport;
              SOCKFS.websocket_sock_ops.addPeer(sock, peer);
              return;
            }
  
            sock.recv_queue.push({ addr: peer.addr, port: peer.port, data: data });
          };
  
          if (ENVIRONMENT_IS_NODE) {
            peer.socket.on('open', handleOpen);
            peer.socket.on('message', function(data, flags) {
              if (!flags.binary) {
                return;
              }
              handleMessage((new Uint8Array(data)).buffer);  // copy from node Buffer -> ArrayBuffer
            });
            peer.socket.on('error', function() {
              // don't throw
            });
          } else {
            peer.socket.onopen = handleOpen;
            peer.socket.onmessage = function peer_socket_onmessage(event) {
              handleMessage(event.data);
            };
          }
        },poll:function (sock) {
          if (sock.type === 1 && sock.server) {
            // listen sockets should only say they're available for reading
            // if there are pending clients.
            return sock.pending.length ? (64 | 1) : 0;
          }
  
          var mask = 0;
          var dest = sock.type === 1 ?  // we only care about the socket state for connection-based sockets
            SOCKFS.websocket_sock_ops.getPeer(sock, sock.daddr, sock.dport) :
            null;
  
          if (sock.recv_queue.length ||
              !dest ||  // connection-less sockets are always ready to read
              (dest && dest.socket.readyState === dest.socket.CLOSING) ||
              (dest && dest.socket.readyState === dest.socket.CLOSED)) {  // let recv return 0 once closed
            mask |= (64 | 1);
          }
  
          if (!dest ||  // connection-less sockets are always ready to write
              (dest && dest.socket.readyState === dest.socket.OPEN)) {
            mask |= 4;
          }
  
          if ((dest && dest.socket.readyState === dest.socket.CLOSING) ||
              (dest && dest.socket.readyState === dest.socket.CLOSED)) {
            mask |= 16;
          }
  
          return mask;
        },ioctl:function (sock, request, arg) {
          switch (request) {
            case 21531:
              var bytes = 0;
              if (sock.recv_queue.length) {
                bytes = sock.recv_queue[0].data.length;
              }
              HEAP32[((arg)>>2)]=bytes;
              return 0;
            default:
              return ERRNO_CODES.EINVAL;
          }
        },close:function (sock) {
          // if we've spawned a listen server, close it
          if (sock.server) {
            try {
              sock.server.close();
            } catch (e) {
            }
            sock.server = null;
          }
          // close any peer connections
          var peers = Object.keys(sock.peers);
          for (var i = 0; i < peers.length; i++) {
            var peer = sock.peers[peers[i]];
            try {
              peer.socket.close();
            } catch (e) {
            }
            SOCKFS.websocket_sock_ops.removePeer(sock, peer);
          }
          return 0;
        },bind:function (sock, addr, port) {
          if (typeof sock.saddr !== 'undefined' || typeof sock.sport !== 'undefined') {
            throw new FS.ErrnoError(ERRNO_CODES.EINVAL);  // already bound
          }
          sock.saddr = addr;
          sock.sport = port || _mkport();
          // in order to emulate dgram sockets, we need to launch a listen server when
          // binding on a connection-less socket
          // note: this is only required on the server side
          if (sock.type === 2) {
            // close the existing server if it exists
            if (sock.server) {
              sock.server.close();
              sock.server = null;
            }
            // swallow error operation not supported error that occurs when binding in the
            // browser where this isn't supported
            try {
              sock.sock_ops.listen(sock, 0);
            } catch (e) {
              if (!(e instanceof FS.ErrnoError)) throw e;
              if (e.errno !== ERRNO_CODES.EOPNOTSUPP) throw e;
            }
          }
        },connect:function (sock, addr, port) {
          if (sock.server) {
            throw new FS.ErrnoError(ERRNO_CODS.EOPNOTSUPP);
          }
  
          // TODO autobind
          // if (!sock.addr && sock.type == 2) {
          // }
  
          // early out if we're already connected / in the middle of connecting
          if (typeof sock.daddr !== 'undefined' && typeof sock.dport !== 'undefined') {
            var dest = SOCKFS.websocket_sock_ops.getPeer(sock, sock.daddr, sock.dport);
            if (dest) {
              if (dest.socket.readyState === dest.socket.CONNECTING) {
                throw new FS.ErrnoError(ERRNO_CODES.EALREADY);
              } else {
                throw new FS.ErrnoError(ERRNO_CODES.EISCONN);
              }
            }
          }
  
          // add the socket to our peer list and set our
          // destination address / port to match
          var peer = SOCKFS.websocket_sock_ops.createPeer(sock, addr, port);
          sock.daddr = peer.addr;
          sock.dport = peer.port;
  
          // always "fail" in non-blocking mode
          throw new FS.ErrnoError(ERRNO_CODES.EINPROGRESS);
        },listen:function (sock, backlog) {
          if (!ENVIRONMENT_IS_NODE) {
            throw new FS.ErrnoError(ERRNO_CODES.EOPNOTSUPP);
          }
          if (sock.server) {
             throw new FS.ErrnoError(ERRNO_CODES.EINVAL);  // already listening
          }
          var WebSocketServer = require('ws').Server;
          var host = sock.saddr;
          sock.server = new WebSocketServer({
            host: host,
            port: sock.sport
            // TODO support backlog
          });
  
          sock.server.on('connection', function(ws) {
            if (sock.type === 1) {
              var newsock = SOCKFS.createSocket(sock.family, sock.type, sock.protocol);
  
              // create a peer on the new socket
              var peer = SOCKFS.websocket_sock_ops.createPeer(newsock, ws);
              newsock.daddr = peer.addr;
              newsock.dport = peer.port;
  
              // push to queue for accept to pick up
              sock.pending.push(newsock);
            } else {
              // create a peer on the listen socket so calling sendto
              // with the listen socket and an address will resolve
              // to the correct client
              SOCKFS.websocket_sock_ops.createPeer(sock, ws);
            }
          });
          sock.server.on('closed', function() {
            sock.server = null;
          });
          sock.server.on('error', function() {
            // don't throw
          });
        },accept:function (listensock) {
          if (!listensock.server) {
            throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
          }
          var newsock = listensock.pending.shift();
          newsock.stream.flags = listensock.stream.flags;
          return newsock;
        },getname:function (sock, peer) {
          var addr, port;
          if (peer) {
            if (sock.daddr === undefined || sock.dport === undefined) {
              throw new FS.ErrnoError(ERRNO_CODES.ENOTCONN);
            }
            addr = sock.daddr;
            port = sock.dport;
          } else {
            // TODO saddr and sport will be set for bind()'d UDP sockets, but what
            // should we be returning for TCP sockets that've been connect()'d?
            addr = sock.saddr || 0;
            port = sock.sport || 0;
          }
          return { addr: addr, port: port };
        },sendmsg:function (sock, buffer, offset, length, addr, port) {
          if (sock.type === 2) {
            // connection-less sockets will honor the message address,
            // and otherwise fall back to the bound destination address
            if (addr === undefined || port === undefined) {
              addr = sock.daddr;
              port = sock.dport;
            }
            // if there was no address to fall back to, error out
            if (addr === undefined || port === undefined) {
              throw new FS.ErrnoError(ERRNO_CODES.EDESTADDRREQ);
            }
          } else {
            // connection-based sockets will only use the bound
            addr = sock.daddr;
            port = sock.dport;
          }
  
          // find the peer for the destination address
          var dest = SOCKFS.websocket_sock_ops.getPeer(sock, addr, port);
  
          // early out if not connected with a connection-based socket
          if (sock.type === 1) {
            if (!dest || dest.socket.readyState === dest.socket.CLOSING || dest.socket.readyState === dest.socket.CLOSED) {
              throw new FS.ErrnoError(ERRNO_CODES.ENOTCONN);
            } else if (dest.socket.readyState === dest.socket.CONNECTING) {
              throw new FS.ErrnoError(ERRNO_CODES.EAGAIN);
            }
          }
  
          // create a copy of the incoming data to send, as the WebSocket API
          // doesn't work entirely with an ArrayBufferView, it'll just send
          // the entire underlying buffer
          var data;
          if (buffer instanceof Array || buffer instanceof ArrayBuffer) {
            data = buffer.slice(offset, offset + length);
          } else {  // ArrayBufferView
            data = buffer.buffer.slice(buffer.byteOffset + offset, buffer.byteOffset + offset + length);
          }
  
          // if we're emulating a connection-less dgram socket and don't have
          // a cached connection, queue the buffer to send upon connect and
          // lie, saying the data was sent now.
          if (sock.type === 2) {
            if (!dest || dest.socket.readyState !== dest.socket.OPEN) {
              // if we're not connected, open a new connection
              if (!dest || dest.socket.readyState === dest.socket.CLOSING || dest.socket.readyState === dest.socket.CLOSED) {
                dest = SOCKFS.websocket_sock_ops.createPeer(sock, addr, port);
              }
              dest.dgram_send_queue.push(data);
              return length;
            }
          }
  
          try {
            // send the actual data
            dest.socket.send(data);
            return length;
          } catch (e) {
            throw new FS.ErrnoError(ERRNO_CODES.EINVAL);
          }
        },recvmsg:function (sock, length) {
          // http://pubs.opengroup.org/onlinepubs/7908799/xns/recvmsg.html
          if (sock.type === 1 && sock.server) {
            // tcp servers should not be recv()'ing on the listen socket
            throw new FS.ErrnoError(ERRNO_CODES.ENOTCONN);
          }
  
          var queued = sock.recv_queue.shift();
          if (!queued) {
            if (sock.type === 1) {
              var dest = SOCKFS.websocket_sock_ops.getPeer(sock, sock.daddr, sock.dport);
  
              if (!dest) {
                // if we have a destination address but are not connected, error out
                throw new FS.ErrnoError(ERRNO_CODES.ENOTCONN);
              }
              else if (dest.socket.readyState === dest.socket.CLOSING || dest.socket.readyState === dest.socket.CLOSED) {
                // return null if the socket has closed
                return null;
              }
              else {
                // else, our socket is in a valid state but truly has nothing available
                throw new FS.ErrnoError(ERRNO_CODES.EAGAIN);
              }
            } else {
              throw new FS.ErrnoError(ERRNO_CODES.EAGAIN);
            }
          }
  
          // queued.data will be an ArrayBuffer if it's unadulterated, but if it's
          // requeued TCP data it'll be an ArrayBufferView
          var queuedLength = queued.data.byteLength || queued.data.length;
          var queuedOffset = queued.data.byteOffset || 0;
          var queuedBuffer = queued.data.buffer || queued.data;
          var bytesRead = Math.min(length, queuedLength);
          var res = {
            buffer: new Uint8Array(queuedBuffer, queuedOffset, bytesRead),
            addr: queued.addr,
            port: queued.port
          };
  
  
          // push back any unread data for TCP connections
          if (sock.type === 1 && bytesRead < queuedLength) {
            var bytesRemaining = queuedLength - bytesRead;
            queued.data = new Uint8Array(queuedBuffer, queuedOffset + bytesRead, bytesRemaining);
            sock.recv_queue.unshift(queued);
          }
  
          return res;
        }}};function _send(fd, buf, len, flags) {
      var sock = SOCKFS.getSocket(fd);
      if (!sock) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
      // TODO honor flags
      return _write(fd, buf, len);
    }
  
  function _pwrite(fildes, buf, nbyte, offset) {
      // ssize_t pwrite(int fildes, const void *buf, size_t nbyte, off_t offset);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/write.html
      var stream = FS.getStream(fildes);
      if (!stream) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
      try {
        var slab = HEAP8;
        return FS.write(stream, slab, buf, nbyte, offset);
      } catch (e) {
        FS.handleFSError(e);
        return -1;
      }
    }function _write(fildes, buf, nbyte) {
      // ssize_t write(int fildes, const void *buf, size_t nbyte);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/write.html
      var stream = FS.getStream(fildes);
      if (!stream) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
  
  
      try {
        var slab = HEAP8;
        return FS.write(stream, slab, buf, nbyte);
      } catch (e) {
        FS.handleFSError(e);
        return -1;
      }
    }function _fputc(c, stream) {
      // int fputc(int c, FILE *stream);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fputc.html
      var chr = unSign(c & 0xFF);
      HEAP8[((_fputc.ret)|0)]=chr;
      var ret = _write(stream, _fputc.ret, 1);
      if (ret == -1) {
        var streamObj = FS.getStream(stream);
        if (streamObj) streamObj.error = true;
        return -1;
      } else {
        return chr;
      }
    }function _putchar(c) {
      // int putchar(int c);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/putchar.html
      return _fputc(c, HEAP32[((_stdout)>>2)]);
    } 
  Module["_saveSetjmp"] = _saveSetjmp;
  
   
  Module["_testSetjmp"] = _testSetjmp;var _setjmp=undefined;

  
   
  Module["_memset"] = _memset;var _llvm_memset_p0i8_i32=_memset;

  
  
  
  function __reallyNegative(x) {
      return x < 0 || (x === 0 && (1/x) === -Infinity);
    }function __formatString(format, varargs) {
      var textIndex = format;
      var argIndex = 0;
      function getNextArg(type) {
        // NOTE: Explicitly ignoring type safety. Otherwise this fails:
        //       int x = 4; printf("%c\n", (char)x);
        var ret;
        if (type === 'double') {
          ret = HEAPF64[(((varargs)+(argIndex))>>3)];
        } else if (type == 'i64') {
          ret = [HEAP32[(((varargs)+(argIndex))>>2)],
                 HEAP32[(((varargs)+(argIndex+8))>>2)]];
          argIndex += 8; // each 32-bit chunk is in a 64-bit block
  
        } else {
          type = 'i32'; // varargs are always i32, i64, or double
          ret = HEAP32[(((varargs)+(argIndex))>>2)];
        }
        argIndex += Math.max(Runtime.getNativeFieldSize(type), Runtime.getAlignSize(type, null, true));
        return ret;
      }
  
      var ret = [];
      var curr, next, currArg;
      while(1) {
        var startTextIndex = textIndex;
        curr = HEAP8[(textIndex)];
        if (curr === 0) break;
        next = HEAP8[((textIndex+1)|0)];
        if (curr == 37) {
          // Handle flags.
          var flagAlwaysSigned = false;
          var flagLeftAlign = false;
          var flagAlternative = false;
          var flagZeroPad = false;
          var flagPadSign = false;
          flagsLoop: while (1) {
            switch (next) {
              case 43:
                flagAlwaysSigned = true;
                break;
              case 45:
                flagLeftAlign = true;
                break;
              case 35:
                flagAlternative = true;
                break;
              case 48:
                if (flagZeroPad) {
                  break flagsLoop;
                } else {
                  flagZeroPad = true;
                  break;
                }
              case 32:
                flagPadSign = true;
                break;
              default:
                break flagsLoop;
            }
            textIndex++;
            next = HEAP8[((textIndex+1)|0)];
          }
  
          // Handle width.
          var width = 0;
          if (next == 42) {
            width = getNextArg('i32');
            textIndex++;
            next = HEAP8[((textIndex+1)|0)];
          } else {
            while (next >= 48 && next <= 57) {
              width = width * 10 + (next - 48);
              textIndex++;
              next = HEAP8[((textIndex+1)|0)];
            }
          }
  
          // Handle precision.
          var precisionSet = false, precision = -1;
          if (next == 46) {
            precision = 0;
            precisionSet = true;
            textIndex++;
            next = HEAP8[((textIndex+1)|0)];
            if (next == 42) {
              precision = getNextArg('i32');
              textIndex++;
            } else {
              while(1) {
                var precisionChr = HEAP8[((textIndex+1)|0)];
                if (precisionChr < 48 ||
                    precisionChr > 57) break;
                precision = precision * 10 + (precisionChr - 48);
                textIndex++;
              }
            }
            next = HEAP8[((textIndex+1)|0)];
          }
          if (precision === -1) {
            precision = 6; // Standard default.
            precisionSet = false;
          }
  
          // Handle integer sizes. WARNING: These assume a 32-bit architecture!
          var argSize;
          switch (String.fromCharCode(next)) {
            case 'h':
              var nextNext = HEAP8[((textIndex+2)|0)];
              if (nextNext == 104) {
                textIndex++;
                argSize = 1; // char (actually i32 in varargs)
              } else {
                argSize = 2; // short (actually i32 in varargs)
              }
              break;
            case 'l':
              var nextNext = HEAP8[((textIndex+2)|0)];
              if (nextNext == 108) {
                textIndex++;
                argSize = 8; // long long
              } else {
                argSize = 4; // long
              }
              break;
            case 'L': // long long
            case 'q': // int64_t
            case 'j': // intmax_t
              argSize = 8;
              break;
            case 'z': // size_t
            case 't': // ptrdiff_t
            case 'I': // signed ptrdiff_t or unsigned size_t
              argSize = 4;
              break;
            default:
              argSize = null;
          }
          if (argSize) textIndex++;
          next = HEAP8[((textIndex+1)|0)];
  
          // Handle type specifier.
          switch (String.fromCharCode(next)) {
            case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
              // Integer.
              var signed = next == 100 || next == 105;
              argSize = argSize || 4;
              var currArg = getNextArg('i' + (argSize * 8));
              var origArg = currArg;
              var argText;
              // Flatten i64-1 [low, high] into a (slightly rounded) double
              if (argSize == 8) {
                currArg = Runtime.makeBigInt(currArg[0], currArg[1], next == 117);
              }
              // Truncate to requested size.
              if (argSize <= 4) {
                var limit = Math.pow(256, argSize) - 1;
                currArg = (signed ? reSign : unSign)(currArg & limit, argSize * 8);
              }
              // Format the number.
              var currAbsArg = Math.abs(currArg);
              var prefix = '';
              if (next == 100 || next == 105) {
                if (argSize == 8 && i64Math) argText = i64Math.stringify(origArg[0], origArg[1], null); else
                argText = reSign(currArg, 8 * argSize, 1).toString(10);
              } else if (next == 117) {
                if (argSize == 8 && i64Math) argText = i64Math.stringify(origArg[0], origArg[1], true); else
                argText = unSign(currArg, 8 * argSize, 1).toString(10);
                currArg = Math.abs(currArg);
              } else if (next == 111) {
                argText = (flagAlternative ? '0' : '') + currAbsArg.toString(8);
              } else if (next == 120 || next == 88) {
                prefix = (flagAlternative && currArg != 0) ? '0x' : '';
                if (argSize == 8 && i64Math) {
                  if (origArg[1]) {
                    argText = (origArg[1]>>>0).toString(16);
                    var lower = (origArg[0]>>>0).toString(16);
                    while (lower.length < 8) lower = '0' + lower;
                    argText += lower;
                  } else {
                    argText = (origArg[0]>>>0).toString(16);
                  }
                } else
                if (currArg < 0) {
                  // Represent negative numbers in hex as 2's complement.
                  currArg = -currArg;
                  argText = (currAbsArg - 1).toString(16);
                  var buffer = [];
                  for (var i = 0; i < argText.length; i++) {
                    buffer.push((0xF - parseInt(argText[i], 16)).toString(16));
                  }
                  argText = buffer.join('');
                  while (argText.length < argSize * 2) argText = 'f' + argText;
                } else {
                  argText = currAbsArg.toString(16);
                }
                if (next == 88) {
                  prefix = prefix.toUpperCase();
                  argText = argText.toUpperCase();
                }
              } else if (next == 112) {
                if (currAbsArg === 0) {
                  argText = '(nil)';
                } else {
                  prefix = '0x';
                  argText = currAbsArg.toString(16);
                }
              }
              if (precisionSet) {
                while (argText.length < precision) {
                  argText = '0' + argText;
                }
              }
  
              // Add sign if needed
              if (currArg >= 0) {
                if (flagAlwaysSigned) {
                  prefix = '+' + prefix;
                } else if (flagPadSign) {
                  prefix = ' ' + prefix;
                }
              }
  
              // Move sign to prefix so we zero-pad after the sign
              if (argText.charAt(0) == '-') {
                prefix = '-' + prefix;
                argText = argText.substr(1);
              }
  
              // Add padding.
              while (prefix.length + argText.length < width) {
                if (flagLeftAlign) {
                  argText += ' ';
                } else {
                  if (flagZeroPad) {
                    argText = '0' + argText;
                  } else {
                    prefix = ' ' + prefix;
                  }
                }
              }
  
              // Insert the result into the buffer.
              argText = prefix + argText;
              argText.split('').forEach(function(chr) {
                ret.push(chr.charCodeAt(0));
              });
              break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
              // Float.
              var currArg = getNextArg('double');
              var argText;
              if (isNaN(currArg)) {
                argText = 'nan';
                flagZeroPad = false;
              } else if (!isFinite(currArg)) {
                argText = (currArg < 0 ? '-' : '') + 'inf';
                flagZeroPad = false;
              } else {
                var isGeneral = false;
                var effectivePrecision = Math.min(precision, 20);
  
                // Convert g/G to f/F or e/E, as per:
                // http://pubs.opengroup.org/onlinepubs/9699919799/functions/printf.html
                if (next == 103 || next == 71) {
                  isGeneral = true;
                  precision = precision || 1;
                  var exponent = parseInt(currArg.toExponential(effectivePrecision).split('e')[1], 10);
                  if (precision > exponent && exponent >= -4) {
                    next = ((next == 103) ? 'f' : 'F').charCodeAt(0);
                    precision -= exponent + 1;
                  } else {
                    next = ((next == 103) ? 'e' : 'E').charCodeAt(0);
                    precision--;
                  }
                  effectivePrecision = Math.min(precision, 20);
                }
  
                if (next == 101 || next == 69) {
                  argText = currArg.toExponential(effectivePrecision);
                  // Make sure the exponent has at least 2 digits.
                  if (/[eE][-+]\d$/.test(argText)) {
                    argText = argText.slice(0, -1) + '0' + argText.slice(-1);
                  }
                } else if (next == 102 || next == 70) {
                  argText = currArg.toFixed(effectivePrecision);
                  if (currArg === 0 && __reallyNegative(currArg)) {
                    argText = '-' + argText;
                  }
                }
  
                var parts = argText.split('e');
                if (isGeneral && !flagAlternative) {
                  // Discard trailing zeros and periods.
                  while (parts[0].length > 1 && parts[0].indexOf('.') != -1 &&
                         (parts[0].slice(-1) == '0' || parts[0].slice(-1) == '.')) {
                    parts[0] = parts[0].slice(0, -1);
                  }
                } else {
                  // Make sure we have a period in alternative mode.
                  if (flagAlternative && argText.indexOf('.') == -1) parts[0] += '.';
                  // Zero pad until required precision.
                  while (precision > effectivePrecision++) parts[0] += '0';
                }
                argText = parts[0] + (parts.length > 1 ? 'e' + parts[1] : '');
  
                // Capitalize 'E' if needed.
                if (next == 69) argText = argText.toUpperCase();
  
                // Add sign.
                if (currArg >= 0) {
                  if (flagAlwaysSigned) {
                    argText = '+' + argText;
                  } else if (flagPadSign) {
                    argText = ' ' + argText;
                  }
                }
              }
  
              // Add padding.
              while (argText.length < width) {
                if (flagLeftAlign) {
                  argText += ' ';
                } else {
                  if (flagZeroPad && (argText[0] == '-' || argText[0] == '+')) {
                    argText = argText[0] + '0' + argText.slice(1);
                  } else {
                    argText = (flagZeroPad ? '0' : ' ') + argText;
                  }
                }
              }
  
              // Adjust case.
              if (next < 97) argText = argText.toUpperCase();
  
              // Insert the result into the buffer.
              argText.split('').forEach(function(chr) {
                ret.push(chr.charCodeAt(0));
              });
              break;
            }
            case 's': {
              // String.
              var arg = getNextArg('i8*');
              var argLength = arg ? _strlen(arg) : '(null)'.length;
              if (precisionSet) argLength = Math.min(argLength, precision);
              if (!flagLeftAlign) {
                while (argLength < width--) {
                  ret.push(32);
                }
              }
              if (arg) {
                for (var i = 0; i < argLength; i++) {
                  ret.push(HEAPU8[((arg++)|0)]);
                }
              } else {
                ret = ret.concat(intArrayFromString('(null)'.substr(0, argLength), true));
              }
              if (flagLeftAlign) {
                while (argLength < width--) {
                  ret.push(32);
                }
              }
              break;
            }
            case 'c': {
              // Character.
              if (flagLeftAlign) ret.push(getNextArg('i8'));
              while (--width > 0) {
                ret.push(32);
              }
              if (!flagLeftAlign) ret.push(getNextArg('i8'));
              break;
            }
            case 'n': {
              // Write the length written so far to the next parameter.
              var ptr = getNextArg('i32*');
              HEAP32[((ptr)>>2)]=ret.length;
              break;
            }
            case '%': {
              // Literal percent sign.
              ret.push(curr);
              break;
            }
            default: {
              // Unknown specifiers remain untouched.
              for (var i = startTextIndex; i < textIndex + 2; i++) {
                ret.push(HEAP8[(i)]);
              }
            }
          }
          textIndex += 2;
          // TODO: Support a/A (hex float) and m (last error) specifiers.
          // TODO: Support %1${specifier} for arg selection.
        } else {
          ret.push(curr);
          textIndex += 1;
        }
      }
      return ret;
    }function _snprintf(s, n, format, varargs) {
      // int snprintf(char *restrict s, size_t n, const char *restrict format, ...);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/printf.html
      var result = __formatString(format, varargs);
      var limit = (n === undefined) ? result.length
                                    : Math.min(result.length, Math.max(n - 1, 0));
      if (s < 0) {
        s = -s;
        var buf = _malloc(limit+1);
        HEAP32[((s)>>2)]=buf;
        s = buf;
      }
      for (var i = 0; i < limit; i++) {
        HEAP8[(((s)+(i))|0)]=result[i];
      }
      if (limit < n || (n === undefined)) HEAP8[(((s)+(i))|0)]=0;
      return result.length;
    }function _sprintf(s, format, varargs) {
      // int sprintf(char *restrict s, const char *restrict format, ...);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/printf.html
      return _snprintf(s, undefined, format, varargs);
    }

  var _llvm_memset_p0i8_i64=_memset;

  function _abort() {
      Module['abort']();
    }

  
   
  Module["_memmove"] = _memmove;var _llvm_memmove_p0i8_p0i8_i32=_memmove;

  function _longjmp(env, value) {
      asm['setThrew'](env, value || 1);
      throw 'longjmp';
    }

   
  Module["_memcmp"] = _memcmp;

  function _strchr(ptr, chr) {
      ptr--;
      do {
        ptr++;
        var val = HEAP8[(ptr)];
        if (val == chr) return ptr;
      } while (val);
      return 0;
    }

  
  function _close(fildes) {
      // int close(int fildes);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/close.html
      var stream = FS.getStream(fildes);
      if (!stream) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
      try {
        FS.close(stream);
        return 0;
      } catch (e) {
        FS.handleFSError(e);
        return -1;
      }
    }
  
  function _fsync(fildes) {
      // int fsync(int fildes);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fsync.html
      var stream = FS.getStream(fildes);
      if (stream) {
        // We write directly to the file system, so there's nothing to do here.
        return 0;
      } else {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
    }function _fclose(stream) {
      // int fclose(FILE *stream);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fclose.html
      _fsync(stream);
      return _close(stream);
    }

  function ___errno_location() {
      return ___errno_state;
    }

  
  function _open(path, oflag, varargs) {
      // int open(const char *path, int oflag, ...);
      // http://pubs.opengroup.org/onlinepubs/009695399/functions/open.html
      var mode = HEAP32[((varargs)>>2)];
      path = Pointer_stringify(path);
      try {
        var stream = FS.open(path, oflag, mode);
        return stream.fd;
      } catch (e) {
        FS.handleFSError(e);
        return -1;
      }
    }function _fopen(filename, mode) {
      // FILE *fopen(const char *restrict filename, const char *restrict mode);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fopen.html
      var flags;
      mode = Pointer_stringify(mode);
      if (mode[0] == 'r') {
        if (mode.indexOf('+') != -1) {
          flags = 2;
        } else {
          flags = 0;
        }
      } else if (mode[0] == 'w') {
        if (mode.indexOf('+') != -1) {
          flags = 2;
        } else {
          flags = 1;
        }
        flags |= 64;
        flags |= 512;
      } else if (mode[0] == 'a') {
        if (mode.indexOf('+') != -1) {
          flags = 2;
        } else {
          flags = 1;
        }
        flags |= 64;
        flags |= 1024;
      } else {
        ___setErrNo(ERRNO_CODES.EINVAL);
        return 0;
      }
      var ret = _open(filename, flags, allocate([0x1FF, 0, 0, 0], 'i32', ALLOC_STACK));  // All creation permissions.
      return (ret == -1) ? 0 : ret;
    }


  
  
  
  function _recv(fd, buf, len, flags) {
      var sock = SOCKFS.getSocket(fd);
      if (!sock) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
      // TODO honor flags
      return _read(fd, buf, len);
    }
  
  function _pread(fildes, buf, nbyte, offset) {
      // ssize_t pread(int fildes, void *buf, size_t nbyte, off_t offset);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/read.html
      var stream = FS.getStream(fildes);
      if (!stream) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
      try {
        var slab = HEAP8;
        return FS.read(stream, slab, buf, nbyte, offset);
      } catch (e) {
        FS.handleFSError(e);
        return -1;
      }
    }function _read(fildes, buf, nbyte) {
      // ssize_t read(int fildes, void *buf, size_t nbyte);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/read.html
      var stream = FS.getStream(fildes);
      if (!stream) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return -1;
      }
  
  
      try {
        var slab = HEAP8;
        return FS.read(stream, slab, buf, nbyte);
      } catch (e) {
        FS.handleFSError(e);
        return -1;
      }
    }function _fread(ptr, size, nitems, stream) {
      // size_t fread(void *restrict ptr, size_t size, size_t nitems, FILE *restrict stream);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fread.html
      var bytesToRead = nitems * size;
      if (bytesToRead == 0) {
        return 0;
      }
      var bytesRead = 0;
      var streamObj = FS.getStream(stream);
      if (!streamObj) {
        ___setErrNo(ERRNO_CODES.EBADF);
        return 0;
      }
      while (streamObj.ungotten.length && bytesToRead > 0) {
        HEAP8[((ptr++)|0)]=streamObj.ungotten.pop();
        bytesToRead--;
        bytesRead++;
      }
      var err = _read(stream, ptr, bytesToRead);
      if (err == -1) {
        if (streamObj) streamObj.error = true;
        return 0;
      }
      bytesRead += err;
      if (bytesRead < bytesToRead) streamObj.eof = true;
      return Math.floor(bytesRead / size);
    }function _fgetc(stream) {
      // int fgetc(FILE *stream);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fgetc.html
      var streamObj = FS.getStream(stream);
      if (!streamObj) return -1;
      if (streamObj.eof || streamObj.error) return -1;
      var ret = _fread(_fgetc.ret, 1, 1, stream);
      if (ret == 0) {
        return -1;
      } else if (ret == -1) {
        streamObj.error = true;
        return -1;
      } else {
        return HEAPU8[((_fgetc.ret)|0)];
      }
    }

  function _fputs(s, stream) {
      // int fputs(const char *restrict s, FILE *restrict stream);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/fputs.html
      return _write(stream, s, _strlen(s));
    }



  function _ungetc(c, stream) {
      // int ungetc(int c, FILE *stream);
      // http://pubs.opengroup.org/onlinepubs/000095399/functions/ungetc.html
      stream = FS.getStream(stream);
      if (!stream) {
        return -1;
      }
      if (c === -1) {
        // do nothing for EOF character
        return c;
      }
      c = unSign(c & 0xFF);
      stream.ungotten.push(c);
      stream.eof = false;
      return c;
    }

  
  var DLFCN={error:null,errorMsg:null,loadedLibs:{},loadedLibNames:{}};
  
  
  
  
  var _environ=allocate(1, "i32*", ALLOC_STATIC);var ___environ=_environ;function ___buildEnvironment(env) {
      // WARNING: Arbitrary limit!
      var MAX_ENV_VALUES = 64;
      var TOTAL_ENV_SIZE = 1024;
  
      // Statically allocate memory for the environment.
      var poolPtr;
      var envPtr;
      if (!___buildEnvironment.called) {
        ___buildEnvironment.called = true;
        // Set default values. Use string keys for Closure Compiler compatibility.
        ENV['USER'] = 'root';
        ENV['PATH'] = '/';
        ENV['PWD'] = '/';
        ENV['HOME'] = '/home/emscripten';
        ENV['LANG'] = 'en_US.UTF-8';
        ENV['_'] = './this.program';
        // Allocate memory.
        poolPtr = allocate(TOTAL_ENV_SIZE, 'i8', ALLOC_STATIC);
        envPtr = allocate(MAX_ENV_VALUES * 4,
                          'i8*', ALLOC_STATIC);
        HEAP32[((envPtr)>>2)]=poolPtr;
        HEAP32[((_environ)>>2)]=envPtr;
      } else {
        envPtr = HEAP32[((_environ)>>2)];
        poolPtr = HEAP32[((envPtr)>>2)];
      }
  
      // Collect key=value lines.
      var strings = [];
      var totalSize = 0;
      for (var key in env) {
        if (typeof env[key] === 'string') {
          var line = key + '=' + env[key];
          strings.push(line);
          totalSize += line.length;
        }
      }
      if (totalSize > TOTAL_ENV_SIZE) {
        throw new Error('Environment size exceeded TOTAL_ENV_SIZE!');
      }
  
      // Make new.
      var ptrSize = 4;
      for (var i = 0; i < strings.length; i++) {
        var line = strings[i];
        writeAsciiToMemory(line, poolPtr);
        HEAP32[(((envPtr)+(i * ptrSize))>>2)]=poolPtr;
        poolPtr += line.length + 1;
      }
      HEAP32[(((envPtr)+(strings.length * ptrSize))>>2)]=0;
    }var ENV={};function _dlopen(filename, flag) {
      // void *dlopen(const char *file, int mode);
      // http://pubs.opengroup.org/onlinepubs/009695399/functions/dlopen.html
      filename = filename === 0 ? '__self__' : (ENV['LD_LIBRARY_PATH'] || '/') + Pointer_stringify(filename);
  
      abort('need to build with DLOPEN_SUPPORT=1 to get dlopen support in asm.js');
  
      if (DLFCN.loadedLibNames[filename]) {
        // Already loaded; increment ref count and return.
        var handle = DLFCN.loadedLibNames[filename];
        DLFCN.loadedLibs[handle].refcount++;
        return handle;
      }
  
      if (filename === '__self__') {
        var handle = -1;
        var lib_module = Module;
        var cached_functions = SYMBOL_TABLE;
      } else {
        var target = FS.findObject(filename);
        if (!target || target.isFolder || target.isDevice) {
          DLFCN.errorMsg = 'Could not find dynamic lib: ' + filename;
          return 0;
        } else {
          FS.forceLoadFile(target);
          var lib_data = intArrayToString(target.contents);
        }
  
        try {
          var lib_module = eval(lib_data)(
            DLFCN.functionTable.length,
            Module
          );
        } catch (e) {
          DLFCN.errorMsg = 'Could not evaluate dynamic lib: ' + filename;
          return 0;
        }
  
        // Not all browsers support Object.keys().
        var handle = 1;
        for (var key in DLFCN.loadedLibs) {
          if (DLFCN.loadedLibs.hasOwnProperty(key)) handle++;
        }
  
        // We don't care about RTLD_NOW and RTLD_LAZY.
        if (flag & 256) { // RTLD_GLOBAL
          for (var ident in lib_module) {
            if (lib_module.hasOwnProperty(ident)) {
              Module[ident] = lib_module[ident];
            }
          }
        }
  
        var cached_functions = {};
      }
      DLFCN.loadedLibs[handle] = {
        refcount: 1,
        name: filename,
        module: lib_module,
        cached_functions: cached_functions
      };
      DLFCN.loadedLibNames[filename] = handle;
  
      return handle;
    }

  function _dlsym(handle, symbol) {
      // void *dlsym(void *restrict handle, const char *restrict name);
      // http://pubs.opengroup.org/onlinepubs/009695399/functions/dlsym.html
      symbol = '_' + Pointer_stringify(symbol);
  
      if (!DLFCN.loadedLibs[handle]) {
        DLFCN.errorMsg = 'Tried to dlsym() from an unopened handle: ' + handle;
        return 0;
      } else {
        var lib = DLFCN.loadedLibs[handle];
        // self-dlopen means that lib.module is not a superset of
        // cached_functions, so check the latter first
        if (lib.cached_functions.hasOwnProperty(symbol)) {
          return lib.cached_functions[symbol];
        } else {
          if (!lib.module.hasOwnProperty(symbol)) {
            DLFCN.errorMsg = ('Tried to lookup unknown symbol "' + symbol +
                                   '" in dynamic lib: ' + lib.name);
            return 0;
          } else {
            var result = lib.module[symbol];
            if (typeof result == 'function') {
              result = lib.module.SYMBOL_TABLE[symbol];
              assert(result);
              lib.cached_functions = result;
            }
            return result;
          }
        }
      }
    }

  function _dlclose(handle) {
      // int dlclose(void *handle);
      // http://pubs.opengroup.org/onlinepubs/009695399/functions/dlclose.html
      if (!DLFCN.loadedLibs[handle]) {
        DLFCN.errorMsg = 'Tried to dlclose() unopened handle: ' + handle;
        return 1;
      } else {
        var lib_record = DLFCN.loadedLibs[handle];
        if (--lib_record.refcount == 0) {
          if (lib_record.module.cleanups) {
            lib_record.module.cleanups.forEach(function(cleanup) { cleanup() });
          }
          delete DLFCN.loadedLibNames[lib_record.name];
          delete DLFCN.loadedLibs[handle];
        }
        return 0;
      }
    }

  function _strerror_r(errnum, strerrbuf, buflen) {
      if (errnum in ERRNO_MESSAGES) {
        if (ERRNO_MESSAGES[errnum].length > buflen - 1) {
          return ___setErrNo(ERRNO_CODES.ERANGE);
        } else {
          var msg = ERRNO_MESSAGES[errnum];
          writeAsciiToMemory(msg, strerrbuf);
          return 0;
        }
      } else {
        return ___setErrNo(ERRNO_CODES.EINVAL);
      }
    }

  function _isprint(chr) {
      return 0x1F < chr && chr < 0x7F;
    }

  function _sbrk(bytes) {
      // Implement a Linux-like 'memory area' for our 'process'.
      // Changes the size of the memory area by |bytes|; returns the
      // address of the previous top ('break') of the memory area
      // We control the "dynamic" memory - DYNAMIC_BASE to DYNAMICTOP
      var self = _sbrk;
      if (!self.called) {
        DYNAMICTOP = alignMemoryPage(DYNAMICTOP); // make sure we start out aligned
        self.called = true;
        assert(Runtime.dynamicAlloc);
        self.alloc = Runtime.dynamicAlloc;
        Runtime.dynamicAlloc = function() { abort('cannot dynamically allocate, sbrk now has control') };
      }
      var ret = DYNAMICTOP;
      if (bytes != 0) self.alloc(bytes);
      return ret;  // Previous break location.
    }

  function _sysconf(name) {
      // long sysconf(int name);
      // http://pubs.opengroup.org/onlinepubs/009695399/functions/sysconf.html
      switch(name) {
        case 30: return PAGE_SIZE;
        case 132:
        case 133:
        case 12:
        case 137:
        case 138:
        case 15:
        case 235:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
        case 149:
        case 13:
        case 10:
        case 236:
        case 153:
        case 9:
        case 21:
        case 22:
        case 159:
        case 154:
        case 14:
        case 77:
        case 78:
        case 139:
        case 80:
        case 81:
        case 79:
        case 82:
        case 68:
        case 67:
        case 164:
        case 11:
        case 29:
        case 47:
        case 48:
        case 95:
        case 52:
        case 51:
        case 46:
          return 200809;
        case 27:
        case 246:
        case 127:
        case 128:
        case 23:
        case 24:
        case 160:
        case 161:
        case 181:
        case 182:
        case 242:
        case 183:
        case 184:
        case 243:
        case 244:
        case 245:
        case 165:
        case 178:
        case 179:
        case 49:
        case 50:
        case 168:
        case 169:
        case 175:
        case 170:
        case 171:
        case 172:
        case 97:
        case 76:
        case 32:
        case 173:
        case 35:
          return -1;
        case 176:
        case 177:
        case 7:
        case 155:
        case 8:
        case 157:
        case 125:
        case 126:
        case 92:
        case 93:
        case 129:
        case 130:
        case 131:
        case 94:
        case 91:
          return 1;
        case 74:
        case 60:
        case 69:
        case 70:
        case 4:
          return 1024;
        case 31:
        case 42:
        case 72:
          return 32;
        case 87:
        case 26:
        case 33:
          return 2147483647;
        case 34:
        case 1:
          return 47839;
        case 38:
        case 36:
          return 99;
        case 43:
        case 37:
          return 2048;
        case 0: return 2097152;
        case 3: return 65536;
        case 28: return 32768;
        case 44: return 32767;
        case 75: return 16384;
        case 39: return 1000;
        case 89: return 700;
        case 71: return 256;
        case 40: return 255;
        case 2: return 100;
        case 180: return 64;
        case 25: return 20;
        case 5: return 16;
        case 6: return 6;
        case 73: return 4;
        case 84: return 1;
      }
      ___setErrNo(ERRNO_CODES.EINVAL);
      return -1;
    }

  function _time(ptr) {
      var ret = Math.floor(Date.now()/1000);
      if (ptr) {
        HEAP32[((ptr)>>2)]=ret;
      }
      return ret;
    }

  
  function _copysign(a, b) {
      return __reallyNegative(a) === __reallyNegative(b) ? a : -a;
    }var _copysignl=_copysign;

  
  function _fmod(x, y) {
      return x % y;
    }var _fmodl=_fmod;

  var _fabs=Math_abs;

  function _llvm_lifetime_start() {}

  function _llvm_lifetime_end() {}






  var Browser={mainLoop:{scheduler:null,shouldPause:false,paused:false,queue:[],pause:function () {
          Browser.mainLoop.shouldPause = true;
        },resume:function () {
          if (Browser.mainLoop.paused) {
            Browser.mainLoop.paused = false;
            Browser.mainLoop.scheduler();
          }
          Browser.mainLoop.shouldPause = false;
        },updateStatus:function () {
          if (Module['setStatus']) {
            var message = Module['statusMessage'] || 'Please wait...';
            var remaining = Browser.mainLoop.remainingBlockers;
            var expected = Browser.mainLoop.expectedBlockers;
            if (remaining) {
              if (remaining < expected) {
                Module['setStatus'](message + ' (' + (expected - remaining) + '/' + expected + ')');
              } else {
                Module['setStatus'](message);
              }
            } else {
              Module['setStatus']('');
            }
          }
        }},isFullScreen:false,pointerLock:false,moduleContextCreatedCallbacks:[],workers:[],init:function () {
        if (!Module["preloadPlugins"]) Module["preloadPlugins"] = []; // needs to exist even in workers
  
        if (Browser.initted || ENVIRONMENT_IS_WORKER) return;
        Browser.initted = true;
  
        try {
          new Blob();
          Browser.hasBlobConstructor = true;
        } catch(e) {
          Browser.hasBlobConstructor = false;
          console.log("warning: no blob constructor, cannot create blobs with mimetypes");
        }
        Browser.BlobBuilder = typeof MozBlobBuilder != "undefined" ? MozBlobBuilder : (typeof WebKitBlobBuilder != "undefined" ? WebKitBlobBuilder : (!Browser.hasBlobConstructor ? console.log("warning: no BlobBuilder") : null));
        Browser.URLObject = typeof window != "undefined" ? (window.URL ? window.URL : window.webkitURL) : undefined;
        if (!Module.noImageDecoding && typeof Browser.URLObject === 'undefined') {
          console.log("warning: Browser does not support creating object URLs. Built-in browser image decoding will not be available.");
          Module.noImageDecoding = true;
        }
  
        // Support for plugins that can process preloaded files. You can add more of these to
        // your app by creating and appending to Module.preloadPlugins.
        //
        // Each plugin is asked if it can handle a file based on the file's name. If it can,
        // it is given the file's raw data. When it is done, it calls a callback with the file's
        // (possibly modified) data. For example, a plugin might decompress a file, or it
        // might create some side data structure for use later (like an Image element, etc.).
  
        var imagePlugin = {};
        imagePlugin['canHandle'] = function imagePlugin_canHandle(name) {
          return !Module.noImageDecoding && /\.(jpg|jpeg|png|bmp)$/i.test(name);
        };
        imagePlugin['handle'] = function imagePlugin_handle(byteArray, name, onload, onerror) {
          var b = null;
          if (Browser.hasBlobConstructor) {
            try {
              b = new Blob([byteArray], { type: Browser.getMimetype(name) });
              if (b.size !== byteArray.length) { // Safari bug #118630
                // Safari's Blob can only take an ArrayBuffer
                b = new Blob([(new Uint8Array(byteArray)).buffer], { type: Browser.getMimetype(name) });
              }
            } catch(e) {
              Runtime.warnOnce('Blob constructor present but fails: ' + e + '; falling back to blob builder');
            }
          }
          if (!b) {
            var bb = new Browser.BlobBuilder();
            bb.append((new Uint8Array(byteArray)).buffer); // we need to pass a buffer, and must copy the array to get the right data range
            b = bb.getBlob();
          }
          var url = Browser.URLObject.createObjectURL(b);
          var img = new Image();
          img.onload = function img_onload() {
            assert(img.complete, 'Image ' + name + ' could not be decoded');
            var canvas = document.createElement('canvas');
            canvas.width = img.width;
            canvas.height = img.height;
            var ctx = canvas.getContext('2d');
            ctx.drawImage(img, 0, 0);
            Module["preloadedImages"][name] = canvas;
            Browser.URLObject.revokeObjectURL(url);
            if (onload) onload(byteArray);
          };
          img.onerror = function img_onerror(event) {
            console.log('Image ' + url + ' could not be decoded');
            if (onerror) onerror();
          };
          img.src = url;
        };
        Module['preloadPlugins'].push(imagePlugin);
  
        var audioPlugin = {};
        audioPlugin['canHandle'] = function audioPlugin_canHandle(name) {
          return !Module.noAudioDecoding && name.substr(-4) in { '.ogg': 1, '.wav': 1, '.mp3': 1 };
        };
        audioPlugin['handle'] = function audioPlugin_handle(byteArray, name, onload, onerror) {
          var done = false;
          function finish(audio) {
            if (done) return;
            done = true;
            Module["preloadedAudios"][name] = audio;
            if (onload) onload(byteArray);
          }
          function fail() {
            if (done) return;
            done = true;
            Module["preloadedAudios"][name] = new Audio(); // empty shim
            if (onerror) onerror();
          }
          if (Browser.hasBlobConstructor) {
            try {
              var b = new Blob([byteArray], { type: Browser.getMimetype(name) });
            } catch(e) {
              return fail();
            }
            var url = Browser.URLObject.createObjectURL(b); // XXX we never revoke this!
            var audio = new Audio();
            audio.addEventListener('canplaythrough', function() { finish(audio) }, false); // use addEventListener due to chromium bug 124926
            audio.onerror = function audio_onerror(event) {
              if (done) return;
              console.log('warning: browser could not fully decode audio ' + name + ', trying slower base64 approach');
              function encode64(data) {
                var BASE = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
                var PAD = '=';
                var ret = '';
                var leftchar = 0;
                var leftbits = 0;
                for (var i = 0; i < data.length; i++) {
                  leftchar = (leftchar << 8) | data[i];
                  leftbits += 8;
                  while (leftbits >= 6) {
                    var curr = (leftchar >> (leftbits-6)) & 0x3f;
                    leftbits -= 6;
                    ret += BASE[curr];
                  }
                }
                if (leftbits == 2) {
                  ret += BASE[(leftchar&3) << 4];
                  ret += PAD + PAD;
                } else if (leftbits == 4) {
                  ret += BASE[(leftchar&0xf) << 2];
                  ret += PAD;
                }
                return ret;
              }
              audio.src = 'data:audio/x-' + name.substr(-3) + ';base64,' + encode64(byteArray);
              finish(audio); // we don't wait for confirmation this worked - but it's worth trying
            };
            audio.src = url;
            // workaround for chrome bug 124926 - we do not always get oncanplaythrough or onerror
            Browser.safeSetTimeout(function() {
              finish(audio); // try to use it even though it is not necessarily ready to play
            }, 10000);
          } else {
            return fail();
          }
        };
        Module['preloadPlugins'].push(audioPlugin);
  
        // Canvas event setup
  
        var canvas = Module['canvas'];
        canvas.requestPointerLock = canvas['requestPointerLock'] ||
                                    canvas['mozRequestPointerLock'] ||
                                    canvas['webkitRequestPointerLock'];
        canvas.exitPointerLock = document['exitPointerLock'] ||
                                 document['mozExitPointerLock'] ||
                                 document['webkitExitPointerLock'] ||
                                 function(){}; // no-op if function does not exist
        canvas.exitPointerLock = canvas.exitPointerLock.bind(document);
  
        function pointerLockChange() {
          Browser.pointerLock = document['pointerLockElement'] === canvas ||
                                document['mozPointerLockElement'] === canvas ||
                                document['webkitPointerLockElement'] === canvas;
        }
  
        document.addEventListener('pointerlockchange', pointerLockChange, false);
        document.addEventListener('mozpointerlockchange', pointerLockChange, false);
        document.addEventListener('webkitpointerlockchange', pointerLockChange, false);
  
        if (Module['elementPointerLock']) {
          canvas.addEventListener("click", function(ev) {
            if (!Browser.pointerLock && canvas.requestPointerLock) {
              canvas.requestPointerLock();
              ev.preventDefault();
            }
          }, false);
        }
      },createContext:function (canvas, useWebGL, setInModule, webGLContextAttributes) {
        var ctx;
        try {
          if (useWebGL) {
            var contextAttributes = {
              antialias: false,
              alpha: false
            };
  
            if (webGLContextAttributes) {
              for (var attribute in webGLContextAttributes) {
                contextAttributes[attribute] = webGLContextAttributes[attribute];
              }
            }
  
  
            var errorInfo = '?';
            function onContextCreationError(event) {
              errorInfo = event.statusMessage || errorInfo;
            }
            canvas.addEventListener('webglcontextcreationerror', onContextCreationError, false);
            try {
              ['experimental-webgl', 'webgl'].some(function(webglId) {
                return ctx = canvas.getContext(webglId, contextAttributes);
              });
            } finally {
              canvas.removeEventListener('webglcontextcreationerror', onContextCreationError, false);
            }
          } else {
            ctx = canvas.getContext('2d');
          }
          if (!ctx) throw ':(';
        } catch (e) {
          Module.print('Could not create canvas: ' + [errorInfo, e]);
          return null;
        }
        if (useWebGL) {
          // Set the background of the WebGL canvas to black
          canvas.style.backgroundColor = "black";
  
          // Warn on context loss
          canvas.addEventListener('webglcontextlost', function(event) {
            alert('WebGL context lost. You will need to reload the page.');
          }, false);
        }
        if (setInModule) {
          GLctx = Module.ctx = ctx;
          Module.useWebGL = useWebGL;
          Browser.moduleContextCreatedCallbacks.forEach(function(callback) { callback() });
          Browser.init();
        }
        return ctx;
      },destroyContext:function (canvas, useWebGL, setInModule) {},fullScreenHandlersInstalled:false,lockPointer:undefined,resizeCanvas:undefined,requestFullScreen:function (lockPointer, resizeCanvas) {
        Browser.lockPointer = lockPointer;
        Browser.resizeCanvas = resizeCanvas;
        if (typeof Browser.lockPointer === 'undefined') Browser.lockPointer = true;
        if (typeof Browser.resizeCanvas === 'undefined') Browser.resizeCanvas = false;
  
        var canvas = Module['canvas'];
        function fullScreenChange() {
          Browser.isFullScreen = false;
          if ((document['webkitFullScreenElement'] || document['webkitFullscreenElement'] ||
               document['mozFullScreenElement'] || document['mozFullscreenElement'] ||
               document['fullScreenElement'] || document['fullscreenElement']) === canvas) {
            canvas.cancelFullScreen = document['cancelFullScreen'] ||
                                      document['mozCancelFullScreen'] ||
                                      document['webkitCancelFullScreen'];
            canvas.cancelFullScreen = canvas.cancelFullScreen.bind(document);
            if (Browser.lockPointer) canvas.requestPointerLock();
            Browser.isFullScreen = true;
            if (Browser.resizeCanvas) Browser.setFullScreenCanvasSize();
          } else if (Browser.resizeCanvas){
            Browser.setWindowedCanvasSize();
          }
          if (Module['onFullScreen']) Module['onFullScreen'](Browser.isFullScreen);
        }
  
        if (!Browser.fullScreenHandlersInstalled) {
          Browser.fullScreenHandlersInstalled = true;
          document.addEventListener('fullscreenchange', fullScreenChange, false);
          document.addEventListener('mozfullscreenchange', fullScreenChange, false);
          document.addEventListener('webkitfullscreenchange', fullScreenChange, false);
        }
  
        canvas.requestFullScreen = canvas['requestFullScreen'] ||
                                   canvas['mozRequestFullScreen'] ||
                                   (canvas['webkitRequestFullScreen'] ? function() { canvas['webkitRequestFullScreen'](Element['ALLOW_KEYBOARD_INPUT']) } : null);
        canvas.requestFullScreen();
      },requestAnimationFrame:function requestAnimationFrame(func) {
        if (typeof window === 'undefined') { // Provide fallback to setTimeout if window is undefined (e.g. in Node.js)
          setTimeout(func, 1000/60);
        } else {
          if (!window.requestAnimationFrame) {
            window.requestAnimationFrame = window['requestAnimationFrame'] ||
                                           window['mozRequestAnimationFrame'] ||
                                           window['webkitRequestAnimationFrame'] ||
                                           window['msRequestAnimationFrame'] ||
                                           window['oRequestAnimationFrame'] ||
                                           window['setTimeout'];
          }
          window.requestAnimationFrame(func);
        }
      },safeCallback:function (func) {
        return function() {
          if (!ABORT) return func.apply(null, arguments);
        };
      },safeRequestAnimationFrame:function (func) {
        return Browser.requestAnimationFrame(function() {
          if (!ABORT) func();
        });
      },safeSetTimeout:function (func, timeout) {
        return setTimeout(function() {
          if (!ABORT) func();
        }, timeout);
      },safeSetInterval:function (func, timeout) {
        return setInterval(function() {
          if (!ABORT) func();
        }, timeout);
      },getMimetype:function (name) {
        return {
          'jpg': 'image/jpeg',
          'jpeg': 'image/jpeg',
          'png': 'image/png',
          'bmp': 'image/bmp',
          'ogg': 'audio/ogg',
          'wav': 'audio/wav',
          'mp3': 'audio/mpeg'
        }[name.substr(name.lastIndexOf('.')+1)];
      },getUserMedia:function (func) {
        if(!window.getUserMedia) {
          window.getUserMedia = navigator['getUserMedia'] ||
                                navigator['mozGetUserMedia'];
        }
        window.getUserMedia(func);
      },getMovementX:function (event) {
        return event['movementX'] ||
               event['mozMovementX'] ||
               event['webkitMovementX'] ||
               0;
      },getMovementY:function (event) {
        return event['movementY'] ||
               event['mozMovementY'] ||
               event['webkitMovementY'] ||
               0;
      },mouseX:0,mouseY:0,mouseMovementX:0,mouseMovementY:0,calculateMouseEvent:function (event) { // event should be mousemove, mousedown or mouseup
        if (Browser.pointerLock) {
          // When the pointer is locked, calculate the coordinates
          // based on the movement of the mouse.
          // Workaround for Firefox bug 764498
          if (event.type != 'mousemove' &&
              ('mozMovementX' in event)) {
            Browser.mouseMovementX = Browser.mouseMovementY = 0;
          } else {
            Browser.mouseMovementX = Browser.getMovementX(event);
            Browser.mouseMovementY = Browser.getMovementY(event);
          }
          
          // check if SDL is available
          if (typeof SDL != "undefined") {
          	Browser.mouseX = SDL.mouseX + Browser.mouseMovementX;
          	Browser.mouseY = SDL.mouseY + Browser.mouseMovementY;
          } else {
          	// just add the mouse delta to the current absolut mouse position
          	// FIXME: ideally this should be clamped against the canvas size and zero
          	Browser.mouseX += Browser.mouseMovementX;
          	Browser.mouseY += Browser.mouseMovementY;
          }        
        } else {
          // Otherwise, calculate the movement based on the changes
          // in the coordinates.
          var rect = Module["canvas"].getBoundingClientRect();
          var x, y;
          
          // Neither .scrollX or .pageXOffset are defined in a spec, but
          // we prefer .scrollX because it is currently in a spec draft.
          // (see: http://www.w3.org/TR/2013/WD-cssom-view-20131217/)
          var scrollX = ((typeof window.scrollX !== 'undefined') ? window.scrollX : window.pageXOffset);
          var scrollY = ((typeof window.scrollY !== 'undefined') ? window.scrollY : window.pageYOffset);
          if (event.type == 'touchstart' ||
              event.type == 'touchend' ||
              event.type == 'touchmove') {
            var t = event.touches.item(0);
            if (t) {
              x = t.pageX - (scrollX + rect.left);
              y = t.pageY - (scrollY + rect.top);
            } else {
              return;
            }
          } else {
            x = event.pageX - (scrollX + rect.left);
            y = event.pageY - (scrollY + rect.top);
          }
  
          // the canvas might be CSS-scaled compared to its backbuffer;
          // SDL-using content will want mouse coordinates in terms
          // of backbuffer units.
          var cw = Module["canvas"].width;
          var ch = Module["canvas"].height;
          x = x * (cw / rect.width);
          y = y * (ch / rect.height);
  
          Browser.mouseMovementX = x - Browser.mouseX;
          Browser.mouseMovementY = y - Browser.mouseY;
          Browser.mouseX = x;
          Browser.mouseY = y;
        }
      },xhrLoad:function (url, onload, onerror) {
        var xhr = new XMLHttpRequest();
        xhr.open('GET', url, true);
        xhr.responseType = 'arraybuffer';
        xhr.onload = function xhr_onload() {
          if (xhr.status == 200 || (xhr.status == 0 && xhr.response)) { // file URLs can return 0
            onload(xhr.response);
          } else {
            onerror();
          }
        };
        xhr.onerror = onerror;
        xhr.send(null);
      },asyncLoad:function (url, onload, onerror, noRunDep) {
        Browser.xhrLoad(url, function(arrayBuffer) {
          assert(arrayBuffer, 'Loading data file "' + url + '" failed (no arrayBuffer).');
          onload(new Uint8Array(arrayBuffer));
          if (!noRunDep) removeRunDependency('al ' + url);
        }, function(event) {
          if (onerror) {
            onerror();
          } else {
            throw 'Loading data file "' + url + '" failed.';
          }
        });
        if (!noRunDep) addRunDependency('al ' + url);
      },resizeListeners:[],updateResizeListeners:function () {
        var canvas = Module['canvas'];
        Browser.resizeListeners.forEach(function(listener) {
          listener(canvas.width, canvas.height);
        });
      },setCanvasSize:function (width, height, noUpdates) {
        var canvas = Module['canvas'];
        canvas.width = width;
        canvas.height = height;
        if (!noUpdates) Browser.updateResizeListeners();
      },windowedWidth:0,windowedHeight:0,setFullScreenCanvasSize:function () {
        var canvas = Module['canvas'];
        this.windowedWidth = canvas.width;
        this.windowedHeight = canvas.height;
        canvas.width = screen.width;
        canvas.height = screen.height;
        // check if SDL is available   
        if (typeof SDL != "undefined") {
        	var flags = HEAPU32[((SDL.screen+Runtime.QUANTUM_SIZE*0)>>2)];
        	flags = flags | 0x00800000; // set SDL_FULLSCREEN flag
        	HEAP32[((SDL.screen+Runtime.QUANTUM_SIZE*0)>>2)]=flags
        }
        Browser.updateResizeListeners();
      },setWindowedCanvasSize:function () {
        var canvas = Module['canvas'];
        canvas.width = this.windowedWidth;
        canvas.height = this.windowedHeight;
        // check if SDL is available       
        if (typeof SDL != "undefined") {
        	var flags = HEAPU32[((SDL.screen+Runtime.QUANTUM_SIZE*0)>>2)];
        	flags = flags & ~0x00800000; // clear SDL_FULLSCREEN flag
        	HEAP32[((SDL.screen+Runtime.QUANTUM_SIZE*0)>>2)]=flags
        }
        Browser.updateResizeListeners();
      }};
_fputc.ret = allocate([0], "i8", ALLOC_STATIC);
FS.staticInit();__ATINIT__.unshift({ func: function() { if (!Module["noFSInit"] && !FS.init.initialized) FS.init() } });__ATMAIN__.push({ func: function() { FS.ignorePermissions = false } });__ATEXIT__.push({ func: function() { FS.quit() } });Module["FS_createFolder"] = FS.createFolder;Module["FS_createPath"] = FS.createPath;Module["FS_createDataFile"] = FS.createDataFile;Module["FS_createPreloadedFile"] = FS.createPreloadedFile;Module["FS_createLazyFile"] = FS.createLazyFile;Module["FS_createLink"] = FS.createLink;Module["FS_createDevice"] = FS.createDevice;
___errno_state = Runtime.staticAlloc(4); HEAP32[((___errno_state)>>2)]=0;
__ATINIT__.unshift({ func: function() { TTY.init() } });__ATEXIT__.push({ func: function() { TTY.shutdown() } });TTY.utf8 = new Runtime.UTF8Processor();
if (ENVIRONMENT_IS_NODE) { var fs = require("fs"); NODEFS.staticInit(); }
__ATINIT__.push({ func: function() { SOCKFS.root = FS.mount(SOCKFS, {}, null); } });
_fgetc.ret = allocate([0], "i8", ALLOC_STATIC);
___buildEnvironment(ENV);
Module["requestFullScreen"] = function Module_requestFullScreen(lockPointer, resizeCanvas) { Browser.requestFullScreen(lockPointer, resizeCanvas) };
  Module["requestAnimationFrame"] = function Module_requestAnimationFrame(func) { Browser.requestAnimationFrame(func) };
  Module["setCanvasSize"] = function Module_setCanvasSize(width, height, noUpdates) { Browser.setCanvasSize(width, height, noUpdates) };
  Module["pauseMainLoop"] = function Module_pauseMainLoop() { Browser.mainLoop.pause() };
  Module["resumeMainLoop"] = function Module_resumeMainLoop() { Browser.mainLoop.resume() };
  Module["getUserMedia"] = function Module_getUserMedia() { Browser.getUserMedia() }
STACK_BASE = STACKTOP = Runtime.alignMemory(STATICTOP);

staticSealed = true; // seal the static portion of memory

STACK_MAX = STACK_BASE + 5242880;

DYNAMIC_BASE = DYNAMICTOP = Runtime.alignMemory(STACK_MAX);

assert(DYNAMIC_BASE < TOTAL_MEMORY, "TOTAL_MEMORY not big enough for stack");

 var ctlz_i8 = allocate([8,7,6,6,5,5,5,5,4,4,4,4,4,4,4,4,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0], "i8", ALLOC_DYNAMIC);
 var cttz_i8 = allocate([8,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,7,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,6,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,5,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0,4,0,1,0,2,0,1,0,3,0,1,0,2,0,1,0], "i8", ALLOC_DYNAMIC);

var Math_min = Math.min;
function invoke_ii(index,a1) {
  try {
    return Module["dynCall_ii"](index,a1);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_vi(index,a1) {
  try {
    Module["dynCall_vi"](index,a1);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_vii(index,a1,a2) {
  try {
    Module["dynCall_vii"](index,a1,a2);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_iiiiiii(index,a1,a2,a3,a4,a5,a6) {
  try {
    return Module["dynCall_iiiiiii"](index,a1,a2,a3,a4,a5,a6);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_iiii(index,a1,a2,a3) {
  try {
    return Module["dynCall_iiii"](index,a1,a2,a3);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_viii(index,a1,a2,a3) {
  try {
    Module["dynCall_viii"](index,a1,a2,a3);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_v(index) {
  try {
    Module["dynCall_v"](index);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_iiiii(index,a1,a2,a3,a4) {
  try {
    return Module["dynCall_iiiii"](index,a1,a2,a3,a4);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_iii(index,a1,a2) {
  try {
    return Module["dynCall_iii"](index,a1,a2);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function invoke_viiii(index,a1,a2,a3,a4) {
  try {
    Module["dynCall_viiii"](index,a1,a2,a3,a4);
  } catch(e) {
    if (typeof e !== 'number' && e !== 'longjmp') throw e;
    asm["setThrew"](1, 0);
  }
}

function asmPrintInt(x, y) {
  Module.print('int ' + x + ',' + y);// + ' ' + new Error().stack);
}
function asmPrintFloat(x, y) {
  Module.print('float ' + x + ',' + y);// + ' ' + new Error().stack);
}
// EMSCRIPTEN_START_ASM
var asm=(function(global,env,buffer){"use asm";var a=new global.Int8Array(buffer);var b=new global.Int16Array(buffer);var c=new global.Int32Array(buffer);var d=new global.Uint8Array(buffer);var e=new global.Uint16Array(buffer);var f=new global.Uint32Array(buffer);var g=new global.Float32Array(buffer);var h=new global.Float64Array(buffer);var i=env.STACKTOP|0;var j=env.STACK_MAX|0;var k=env.tempDoublePtr|0;var l=env.ABORT|0;var m=env.cttz_i8|0;var n=env.ctlz_i8|0;var o=env._stdin|0;var p=env._stderr|0;var q=env._stdout|0;var r=+env.NaN;var s=+env.Infinity;var t=0;var u=0;var v=0;var w=0;var x=0,y=0,z=0,A=0,B=0.0,C=0,D=0,E=0,F=0.0;var G=0;var H=0;var I=0;var J=0;var K=0;var L=0;var M=0;var N=0;var O=0;var P=0;var Q=global.Math.floor;var R=global.Math.abs;var S=global.Math.sqrt;var T=global.Math.pow;var U=global.Math.cos;var V=global.Math.sin;var W=global.Math.tan;var X=global.Math.acos;var Y=global.Math.asin;var Z=global.Math.atan;var _=global.Math.atan2;var $=global.Math.exp;var aa=global.Math.log;var ba=global.Math.ceil;var ca=global.Math.imul;var da=env.abort;var ea=env.assert;var fa=env.asmPrintInt;var ga=env.asmPrintFloat;var ha=env.min;var ia=env.invoke_ii;var ja=env.invoke_vi;var ka=env.invoke_vii;var la=env.invoke_iiiiiii;var ma=env.invoke_iiii;var na=env.invoke_viii;var oa=env.invoke_v;var pa=env.invoke_iiiii;var qa=env.invoke_iii;var ra=env.invoke_viiii;var sa=env._strncmp;var ta=env._llvm_va_end;var ua=env._dlsym;var va=env._snprintf;var wa=env._fgetc;var xa=env._fclose;var ya=env._isprint;var za=env._abort;var Aa=env._toupper;var Ba=env._pread;var Ca=env._close;var Da=env._fflush;var Ea=env._fopen;var Fa=env._strchr;var Ga=env._fputc;var Ha=env.___buildEnvironment;var Ia=env._sysconf;var Ja=env._isalnum;var Ka=env.___setErrNo;var La=env.__reallyNegative;var Ma=env._send;var Na=env._write;var Oa=env._fputs;var Pa=env._isalpha;var Qa=env._sprintf;var Ra=env._llvm_lifetime_end;var Sa=env._fabs;var Ta=env._isspace;var Ua=env._fread;var Va=env._longjmp;var Wa=env._read;var Xa=env._copysign;var Ya=env.__formatString;var Za=env._ungetc;var _a=env._dlclose;var $a=env._recv;var ab=env._dlopen;var bb=env._pwrite;var cb=env._putchar;var db=env._sbrk;var eb=env._fsync;var fb=env._strerror_r;var gb=env.___errno_location;var hb=env._llvm_lifetime_start;var ib=env._open;var jb=env._fmod;var kb=env._time;var lb=env._islower;var mb=env._isupper;var nb=env._strcmp;var ob=0.0;
// EMSCRIPTEN_START_FUNCS
function zb(a){a=a|0;var b=0;b=i;i=i+a|0;i=i+7&-8;return b|0}function Ab(){return i|0}function Bb(a){a=a|0;i=a}function Cb(a,b){a=a|0;b=b|0;if((t|0)==0){t=a;u=b}}function Db(b){b=b|0;a[k]=a[b];a[k+1|0]=a[b+1|0];a[k+2|0]=a[b+2|0];a[k+3|0]=a[b+3|0]}function Eb(b){b=b|0;a[k]=a[b];a[k+1|0]=a[b+1|0];a[k+2|0]=a[b+2|0];a[k+3|0]=a[b+3|0];a[k+4|0]=a[b+4|0];a[k+5|0]=a[b+5|0];a[k+6|0]=a[b+6|0];a[k+7|0]=a[b+7|0]}function Fb(a){a=a|0;G=a}function Gb(a){a=a|0;H=a}function Hb(a){a=a|0;I=a}function Ib(a){a=a|0;J=a}function Jb(a){a=a|0;K=a}function Kb(a){a=a|0;L=a}function Lb(a){a=a|0;M=a}function Mb(a){a=a|0;N=a}function Nb(a){a=a|0;O=a}function Ob(a){a=a|0;P=a}function Pb(){}function Qb(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;return+h[c+16>>3]==+h[d+16>>3]|0}function Rb(a,b,d){a=a|0;b=b|0;d=d|0;var f=0.0;b=c[a>>2]|0;f=+h[(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16>>3];a=c[b+((e[d>>1]|0)<<2)>>2]|0;c[a>>2]=4194304;d=(F=+f,+R(F)>=1.0?F>0.0?(ha(+Q(F/4294967296.0),4294967295.0)|0)>>>0:~~+ba((F- +(~~F>>>0))/4294967296.0)>>>0:0);b=a+16|0;c[b>>2]=~~+f>>>0;c[b+4>>2]=d;return}function Sb(a){a=a|0;return ei(a,7448)|0}function Tb(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;b=c[d+16>>2]|0;d=c[e+16>>2]|0;do{if((c[b+4>>2]|0)==(c[d+4>>2]|0)){if((b|0)==(d|0)){f=1;return f|0}if((nb(c[b+8>>2]|0,c[d+8>>2]|0)|0)==0){f=1}else{break}return f|0}}while(0);f=0;return f|0}function Ub(a){a=a|0;var b=0,d=0;b=c[a+16>>2]|0;a=c[b+8>>2]|0;if((a|0)==0){d=b;Ln(d);return}Ln(a);d=b;Ln(d);return}function Vb(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+(e[f>>1]<<2)>>2]|0;j=c[h+(e[f+2>>1]<<2)>>2]|0;do{if((c[j>>2]&1048576|0)==0){f=j+16|0;h=c[f>>2]|0;if((c[h+4>>2]|0)==0){break}k=c[h+8>>2]|0;gh(b,4616);h=c[(c[f>>2]|0)+4>>2]|0;if((h|0)==0){i=d;return}else{l=0}while(1){f=l+1|0;if(((a[k+l|0]|0)-48|0)>>>0>=10>>>0){break}if(f>>>0<h>>>0){l=f}else{m=8;break}}if((m|0)==8){i=d;return}h=b+16|0;c[h>>2]=0;c[h+4>>2]=0;i=d;return}}while(0);m=g|0;c[m>>2]=0;c[m+4>>2]=0;gh(b,g);i=d;return}function Wb(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+(e[f>>1]<<2)>>2]|0;j=c[h+(e[f+2>>1]<<2)>>2]|0;do{if((c[j>>2]&1048576|0)==0){f=j+16|0;h=c[f>>2]|0;if((c[h+4>>2]|0)==0){break}k=c[h+8>>2]|0;gh(b,4616);if((c[(c[f>>2]|0)+4>>2]|0)==0){i=d;return}else{l=0}while(1){h=l+1|0;if((Pa(a[k+l|0]|0)|0)==0){break}if(h>>>0<(c[(c[f>>2]|0)+4>>2]|0)>>>0){l=h}else{m=8;break}}if((m|0)==8){i=d;return}f=b+16|0;c[f>>2]=0;c[f+4>>2]=0;i=d;return}}while(0);m=g|0;c[m>>2]=0;c[m+4>>2]=0;gh(b,g);i=d;return}function Xb(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+(e[f>>1]<<2)>>2]|0;j=c[h+(e[f+2>>1]<<2)>>2]|0;do{if((c[j>>2]&1048576|0)==0){f=j+16|0;h=c[f>>2]|0;if((c[h+4>>2]|0)==0){break}k=c[h+8>>2]|0;gh(b,4616);if((c[(c[f>>2]|0)+4>>2]|0)==0){i=d;return}else{l=0}while(1){h=l+1|0;if((Ta(a[k+l|0]|0)|0)==0){break}if(h>>>0<(c[(c[f>>2]|0)+4>>2]|0)>>>0){l=h}else{m=8;break}}if((m|0)==8){i=d;return}f=b+16|0;c[f>>2]=0;c[f+4>>2]=0;i=d;return}}while(0);m=g|0;c[m>>2]=0;c[m+4>>2]=0;gh(b,g);i=d;return}function Yb(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+(e[f>>1]<<2)>>2]|0;j=c[h+(e[f+2>>1]<<2)>>2]|0;do{if((c[j>>2]&1048576|0)==0){f=j+16|0;h=c[f>>2]|0;if((c[h+4>>2]|0)==0){break}k=c[h+8>>2]|0;gh(b,4616);if((c[(c[f>>2]|0)+4>>2]|0)==0){i=d;return}else{l=0}while(1){h=l+1|0;if((Ja(a[k+l|0]|0)|0)==0){break}if(h>>>0<(c[(c[f>>2]|0)+4>>2]|0)>>>0){l=h}else{m=8;break}}if((m|0)==8){i=d;return}f=b+16|0;c[f>>2]=0;c[f+4>>2]=0;i=d;return}}while(0);m=g|0;c[m>>2]=0;c[m+4>>2]=0;gh(b,g);i=d;return}function Zb(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;a=c[g+((e[d>>1]|0)<<2)>>2]|0;h=c[(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;j=c[(c[g+((e[d+4>>1]|0)<<2)>>2]|0)+16>>2]|0;d=nc((c[h+4>>2]|0)+1+(c[j+4>>2]|0)|0)|0;g=c[d+8>>2]|0;Zn(g|0,c[h+8>>2]|0)|0;$n(g|0,c[j+8>>2]|0)|0;c[f>>2]=d;gh(a,f);i=b;return}function _b(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;d=i;i=i+16|0;g=d|0;h=d+8|0;j=c[b>>2]|0;b=c[j+(e[f>>1]<<2)>>2]|0;k=c[(c[j+(e[f+2>>1]<<2)>>2]|0)+16>>2]|0;l=c[k+8>>2]|0;m=c[(c[j+(e[f+4>>1]<<2)>>2]|0)+16>>2]|0;f=c[m+8>>2]|0;j=c[k+4>>2]|0;k=c[m+4>>2]|0;if((k|0)>(j|0)){m=g|0;c[m>>2]=0;c[m+4>>2]=0;gh(b,g);i=d;return}else{n=k;o=j}while(1){j=n-1|0;k=o-1|0;if((j|0)<=0){p=0;q=1;break}if((a[l+k|0]|0)==(a[f+j|0]|0)){n=j;o=k}else{p=0;q=0;break}}o=h|0;c[o>>2]=q;c[o+4>>2]=p;gh(b,h);i=d;return}function $b(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;d=i;i=i+16|0;g=d|0;h=d+8|0;j=c[b>>2]|0;b=c[j+(e[f>>1]<<2)>>2]|0;k=c[(c[j+(e[f+2>>1]<<2)>>2]|0)+16>>2]|0;l=c[k+8>>2]|0;m=c[k+4>>2]|0;k=c[(c[j+(e[f+4>>1]<<2)>>2]|0)+16>>2]|0;f=c[k+8>>2]|0;j=c[k+4>>2]|0;if((m|0)<(j|0)){gh(b,4624);i=d;return}if((j|0)==0){k=g|0;c[k>>2]=0;c[k+4>>2]=0;gh(b,g);i=d;return}g=m-j|0;m=a[f]|0;a:do{if((g|0)<0){n=-1}else{if((j|0)>1){o=0}else{k=0;while(1){if((a[l+k|0]|0)==m<<24>>24){n=k;break a}if((k|0)<(g|0)){k=k+1|0}else{n=-1;break a}}}while(1){b:do{if((a[l+o|0]|0)==m<<24>>24){k=1;p=o;while(1){q=p+1|0;r=k+1|0;if((a[l+q|0]|0)!=(a[f+k|0]|0)){break b}if((r|0)<(j|0)){k=r;p=q}else{n=o;break a}}}}while(0);if((o|0)<(g|0)){o=o+1|0}else{n=-1;break}}}}while(0);o=h|0;c[o>>2]=n;c[o+4>>2]=(n|0)<0|0?-1:0;gh(b,h);i=d;return}function ac(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0;b=i;i=i+8|0;f=b|0;zc(a,d);g=c[c[a+100>>2]>>2]|0;h=c[(c[a>>2]|0)+((e[d>>1]|0)<<2)>>2]|0;d=nc((_n(g|0)|0)+1|0)|0;Zn(c[d+8>>2]|0,g|0)|0;c[f>>2]=d;gh(h,f);i=b;return}function bc(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;j=c[h+((e[f+2>>1]|0)<<2)>>2]|0;k=c[h+((e[f>>1]|0)<<2)>>2]|0;f=c[b+100>>2]|0;qn(f);b=j+16|0;j=c[(c[b>>2]|0)+8>>2]|0;h=j;l=0;m=j;while(1){n=a[m]|0;if((n<<24>>24|0)==0){break}else if((n<<24>>24|0)==60){o=m-h|0;ln(f,j,l,o);jn(f,18560);p=o+1|0}else if((n<<24>>24|0)==62){o=m-h|0;ln(f,j,l,o);jn(f,16520);p=o+1|0}else if((n<<24>>24|0)==38){n=m-h|0;ln(f,j,l,n);jn(f,15784);p=n+1|0}else{p=l}l=p;m=m+1|0}if((l|0)==0){c[g>>2]=c[b>>2];gh(k,g);i=d;return}else{ln(f,j,l,m-h|0);h=f|0;f=nc((_n(c[h>>2]|0)|0)+1|0)|0;Zn(c[f+8>>2]|0,c[h>>2]|0)|0;c[g>>2]=f;gh(k,g);i=d;return}}function cc(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+(e[f+2>>1]<<2)>>2]|0;j=c[h+(e[f>>1]<<2)>>2]|0;k=b+16|0;l=c[(c[k>>2]|0)+4>>2]|0;do{if((l|0)!=0){m=c[(c[h+(e[f+4>>1]<<2)>>2]|0)+16>>2]|0;n=c[m+4>>2]|0;if((n|0)==0){break}o=c[m+8>>2]|0;m=_n(o|0)|0;a:do{if((m|0)>0){p=0;while(1){q=p+1|0;if((a[o+p|0]|0)<0){break}if((q|0)<(m|0)){p=q}else{r=7;break a}}s=pc(b,n,o)|0}else{r=7}}while(0);if((r|0)==7){s=oc(b,n,o)|0}m=nc(1-s+l|0)|0;Zn(c[m+8>>2]|0,(c[(c[k>>2]|0)+8>>2]|0)+s|0)|0;c[g>>2]=m;gh(j,g);i=d;return}}while(0);fh(j,b);i=d;return}function dc(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+((e[f>>1]|0)<<2)>>2]|0;j=(c[h+((e[f+2>>1]|0)<<2)>>2]|0)+16|0;f=nc((c[(c[j>>2]|0)+4>>2]|0)+1|0)|0;h=c[f+8>>2]|0;k=c[j>>2]|0;j=c[k+8>>2]|0;l=c[k+4>>2]|0;if((l|0)>0){m=0}else{n=h+l|0;a[n]=0;o=g;c[o>>2]=f;gh(b,g);i=d;return}do{k=a[j+m|0]|0;p=k<<24>>24;if((mb(p|0)|0)==0){a[h+m|0]=k}else{a[h+m|0]=ao(p|0)|0}m=m+1|0;}while((m|0)<(l|0));n=h+l|0;a[n]=0;o=g;c[o>>2]=f;gh(b,g);i=d;return}function ec(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+(e[f+2>>1]<<2)>>2]|0;j=c[h+(e[f>>1]<<2)>>2]|0;k=b+16|0;do{if((c[(c[k>>2]|0)+4>>2]|0)!=0){l=c[(c[h+(e[f+4>>1]<<2)>>2]|0)+16>>2]|0;m=c[l+4>>2]|0;if((m|0)==0){break}n=c[l+8>>2]|0;l=_n(n|0)|0;a:do{if((l|0)>0){o=0;while(1){p=o+1|0;if((a[n+o|0]|0)<0){break}if((p|0)<(l|0)){o=p}else{q=7;break a}}r=rc(b,m,n)|0}else{q=7}}while(0);if((q|0)==7){r=qc(b,m,n)|0}l=nc(r+1|0)|0;o=l+8|0;bo(c[o>>2]|0,c[(c[k>>2]|0)+8>>2]|0,r|0)|0;a[(c[o>>2]|0)+r|0]=0;c[g>>2]=l;gh(j,g);i=d;return}}while(0);fh(j,b);i=d;return}function fc(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;d=i;i=i+16|0;g=d|0;h=d+8|0;j=c[b>>2]|0;b=c[j+(e[f>>1]<<2)>>2]|0;k=c[(c[j+(e[f+2>>1]<<2)>>2]|0)+16>>2]|0;l=c[k+8>>2]|0;m=c[(c[j+(e[f+4>>1]<<2)>>2]|0)+16>>2]|0;f=c[m+8>>2]|0;j=c[m+4>>2]|0;if((c[k+4>>2]|0)>>>0<j>>>0){k=g|0;c[k>>2]=0;c[k+4>>2]=0;gh(b,g);i=d;return}a:do{if((j|0)>0){g=0;while(1){k=g+1|0;if((a[l+g|0]|0)!=(a[f+g|0]|0)){n=0;o=0;break a}if((k|0)<(j|0)){g=k}else{n=0;o=1;break}}}else{n=0;o=1}}while(0);j=h|0;c[j>>2]=o;c[j+4>>2]=n;gh(b,h);i=d;return}function gc(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+((e[f+2>>1]|0)<<2)>>2]|0;j=c[h+((e[f>>1]|0)<<2)>>2]|0;k=b+16|0;l=c[(c[k>>2]|0)+4>>2]|0;do{if((l|0)!=0){m=c[(c[h+((e[f+4>>1]|0)<<2)>>2]|0)+16>>2]|0;n=c[m+4>>2]|0;if((n|0)==0){break}o=c[m+8>>2]|0;m=oc(b,n,o)|0;if((m|0)==(l|0)){p=l}else{p=rc(b,n,o)|0}o=p-m|0;n=nc(o+1|0)|0;q=c[n+8>>2]|0;bo(q|0,(c[(c[k>>2]|0)+8>>2]|0)+m|0,o|0)|0;a[q+o|0]=0;c[g>>2]=n;gh(j,g);i=d;return}}while(0);fh(j,b);i=d;return}function hc(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=i;i=i+24|0;g=f|0;h=f+16|0;j=c[a>>2]|0;k=c[(c[j+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;do{if(b<<16>>16==2){l=c[(c[j+((e[d+4>>1]|0)<<2)>>2]|0)+16>>2]|0;m=c[j+((e[d>>1]|0)<<2)>>2]|0;if((c[l+4>>2]|0)!=0){n=l;o=m;break}Ae(c[a+116>>2]|0,5,14344,(p=i,i=i+1|0,i=i+7&-8,c[p>>2]=0,p)|0);i=p;n=l;o=m}else{c[g+8>>2]=14784;c[g+4>>2]=1;n=g;o=c[j+((e[d>>1]|0)<<2)>>2]|0}}while(0);d=Xd()|0;sc(c[(c[(c[a+112>>2]|0)+52>>2]|0)+24>>2]|0,c[k+8>>2]|0,c[n+8>>2]|0,d);c[h>>2]=d;gh(o,h);i=f;return}function ic(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,H=0,I=0,J=0,K=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;j=c[h+((e[f>>1]|0)<<2)>>2]|0;k=f+2|0;f=c[(c[(c[h+((e[k>>1]|0)<<2)>>2]|0)+16>>2]|0)+8>>2]|0;l=a[f]|0;if((l<<24>>24|0)==45){m=1;n=f+1|0}else if((l<<24>>24|0)==43){m=0;n=f+1|0}else{m=0;n=f}f=n;while(1){o=a[f]|0;if(o<<24>>24==48){f=f+1|0}else{break}}do{if((o-48&255)>>>0<10>>>0){n=f;l=0;p=0;q=0;r=o;do{s=so(p,l,10,0)|0;t=(r<<24>>24)-48|0;p=io(t,(t|0)<0|0?-1:0,s,G)|0;l=G;n=n+1|0;q=q+1|0;r=a[n]|0;}while((r-48&255)>>>0<10>>>0&(q|0)!=20);s=io(m,0,-1,2147483647)|0;t=G;if(l>>>0>t>>>0|l>>>0==t>>>0&p>>>0>s>>>0){Ae(c[b+116>>2]|0,5,13088,(u=i,i=i+1|0,i=i+7&-8,c[u>>2]=0,u)|0);i=u;v=a[n]|0}else{v=r}if(v<<24>>24!=0|(q|0)==0){w=l;x=p;break}else{y=l;z=p}A=(m|0)==0;B=0;C=0;D=jo(B,C,z,y)|0;E=G;F=A?z:D;H=A?y:E;I=g|0;J=I|0;c[J>>2]=F;K=I+4|0;c[K>>2]=H;gh(j,g);i=d;return}else{w=0;x=0}}while(0);Ae(c[b+116>>2]|0,5,11848,(u=i,i=i+8|0,c[u>>2]=c[(c[(c[h+((e[k>>1]|0)<<2)>>2]|0)+16>>2]|0)+8>>2],u)|0);i=u;y=w;z=x;A=(m|0)==0;B=0;C=0;D=jo(B,C,z,y)|0;E=G;F=A?z:D;H=A?y:E;I=g|0;J=I|0;c[J>>2]=F;K=I+4|0;c[K>>2]=H;gh(j,g);i=d;return}function jc(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0;d=i;i=i+16|0;g=d+8|0;h=c[b>>2]|0;b=c[h+((e[f+2>>1]|0)<<2)>>2]|0;j=c[h+((e[f>>1]|0)<<2)>>2]|0;f=d|0;a[f]=a[4608]|0;a[f+1|0]=a[4609]|0;a[f+2|0]=a[4610]|0;a[f+3|0]=a[4611]|0;a[f+4|0]=a[4612]|0;h=_n(f|0)|0;k=oc(b,h,f)|0;l=(qc(b,h,f)|0)-k|0;f=nc(l+1|0)|0;h=c[f+8>>2]|0;bo(h|0,(c[(c[b+16>>2]|0)+8>>2]|0)+k|0,l|0)|0;a[h+l|0]=0;c[g>>2]=f;gh(j,g);i=d;return}function kc(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;b=c[h+((e[f>>1]|0)<<2)>>2]|0;j=(c[h+((e[f+2>>1]|0)<<2)>>2]|0)+16|0;f=nc((c[(c[j>>2]|0)+4>>2]|0)+1|0)|0;h=c[f+8>>2]|0;k=c[j>>2]|0;j=c[k+8>>2]|0;l=c[k+4>>2]|0;if((l|0)>0){m=0}else{n=h+l|0;a[n]=0;o=g;c[o>>2]=f;gh(b,g);i=d;return}do{k=a[j+m|0]|0;p=k<<24>>24;if((lb(p|0)|0)==0){a[h+m|0]=k}else{a[h+m|0]=Aa(p|0)|0}m=m+1|0;}while((m|0)<(l|0));n=h+l|0;a[n]=0;o=g;c[o>>2]=f;gh(b,g);i=d;return}function lc(b,e,f,g){b=b|0;e=e|0;f=f|0;g=g|0;var h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0;h=i;i=i+8|0;j=h|0;k=c[e+16>>2]|0;e=c[k+8>>2]|0;l=f+16|0;f=c[l>>2]|0;m=c[l+4>>2]|0;l=f;do{if((l|0)>-1){n=a[4192+(d[e]|0)|0]|0;a:do{if((l|0)==0){o=n;p=e;q=5}else{r=e;s=l;t=n;while(1){if(t<<24>>24==0){u=r;break a}v=r+(t<<24>>24)|0;w=s-1|0;x=a[4192+(d[v]|0)|0]|0;if((w|0)==0){o=x;p=v;q=5;break}else{r=v;s=w;t=x}}}}while(0);if((q|0)==5){if(o<<24>>24==0){u=p}else{y=p;break}}Ae(c[b+116>>2]|0,3,10616,(z=i,i=i+16|0,c[z>>2]=f,c[z+8>>2]=m,z)|0);i=z;y=u}else{n=c[k+4>>2]|0;t=e+n|0;if((n|0)==0){A=t;B=1}else{n=t;t=l;while(1){s=n-1|0;r=((a[4192+(d[s]|0)|0]|0)!=0)+t|0;x=(r|0)!=0;if((e|0)!=(s|0)&x){n=s;t=r}else{A=s;B=x;break}}}if(!B){y=A;break}Ae(c[b+116>>2]|0,3,10616,(z=i,i=i+16|0,c[z>>2]=f,c[z+8>>2]=m,z)|0);i=z;y=A}}while(0);A=a[4192+(d[y]|0)|0]|0;z=nc(A+1|0)|0;m=c[z+8>>2]|0;a[m+A|0]=0;bo(m|0,y|0,A|0)|0;c[j>>2]=z;gh(g,j);i=h;return}function mc(a){a=a|0;return ei(a,800)|0}function nc(a){a=a|0;var b=0;b=Vd(12)|0;c[b+8>>2]=Vd(a)|0;c[b+4>>2]=a-1;c[b>>2]=1;return b|0}function oc(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0;f=c[b+16>>2]|0;b=c[f+8>>2]|0;g=c[f+4>>2]|0;if((d|0)==1){f=a[e]|0;if((g|0)>0){h=0}else{i=0;return i|0}while(1){j=h+1|0;if((a[b+h|0]|0)!=f<<24>>24){i=h;k=10;break}if((j|0)<(g|0)){h=j}else{i=j;k=10;break}}if((k|0)==10){return i|0}}if((g|0)>0&(d|0)>0){l=0}else{i=0;return i|0}a:while(1){h=a[b+l|0]|0;f=0;while(1){j=f+1|0;if(h<<24>>24==(a[e+f|0]|0)){break}if((j|0)<(d|0)){f=j}else{i=l;k=10;break a}}f=l+1|0;if((f|0)<(g|0)){l=f}else{i=f;k=10;break}}if((k|0)==10){return i|0}return 0}function pc(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;f=c[b+16>>2]|0;b=c[f+8>>2]|0;g=c[f+4>>2]|0;f=a[e]|0;h=f&255;if((a[6696+h|0]|0)!=(d|0)){i=0;a:while(1){j=a[b+i|0]|0;k=0;b:while(1){l=a[e+k|0]|0;c:do{if(j<<24>>24==l<<24>>24){m=j&255;n=a[6696+m|0]|0;if((m-194|0)>>>0<51>>>0){o=1}else{break b}while(1){m=o+1|0;if((a[b+(o+i)|0]|0)!=(a[e+(o+k)|0]|0)){break c}if((m|0)<(n|0)){o=m}else{break b}}}}while(0);m=(a[6696+(l&255)|0]|0)+k|0;if((m|0)==(d|0)){p=i;q=17;break a}else{k=m}}k=n+i|0;if((k|0)<(g|0)){i=k}else{p=k;q=17;break}}if((q|0)==17){return p|0}}if((g|0)<=0){p=0;return p|0}if((h-194|0)>>>0<51>>>0){r=0}else{h=0;while(1){if((a[b+h|0]|0)!=f<<24>>24){p=h;q=17;break}i=h+d|0;if((i|0)<(g|0)){h=i}else{p=i;q=17;break}}if((q|0)==17){return p|0}}d:while(1){if((a[b+r|0]|0)==f<<24>>24){s=1}else{p=r;q=17;break}while(1){h=s+1|0;if((a[b+(s+r)|0]|0)!=(a[e+s|0]|0)){p=r;q=17;break d}if((h|0)<(d|0)){s=h}else{break}}h=r+d|0;if((h|0)<(g|0)){r=h}else{p=h;q=17;break}}if((q|0)==17){return p|0}return 0}function qc(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0;f=c[b+16>>2]|0;b=c[f+8>>2]|0;g=c[f+4>>2]|0;a:do{if((d|0)==1){f=a[e]|0;h=g;while(1){i=h-1|0;if((h|0)<=0){j=i;break a}if((a[b+i|0]|0)==f<<24>>24){h=i}else{j=i;break}}}else{h=g-1|0;if((g|0)>0&(d|0)>0){k=h}else{j=h;break}while(1){h=a[b+k|0]|0;f=0;while(1){i=f+1|0;if(h<<24>>24==(a[e+f|0]|0)){break}if((i|0)<(d|0)){f=i}else{j=k;break a}}f=k-1|0;if((k|0)>0){k=f}else{j=f;break}}}}while(0);return j+1|0}function rc(b,e,f){b=b|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0;g=c[b+16>>2]|0;b=c[g+8>>2]|0;h=c[g+4>>2]|0;g=h-1|0;if((h|0)>0){i=g}else{j=g;k=j+1|0;return k|0}a:while(1){g=a[b+i|0]|0;h=i+1|0;l=0;b:while(1){m=d[f+l|0]|0;n=a[6696+m|0]|0;c:do{if(!(g<<24>>24!=(a[f+(l-1+n)|0]|0)|(h|0)<(n|0))){if((m-194|0)>>>0>=51>>>0){break b}o=l-2+n|0;p=1;q=i;while(1){r=q-1|0;if((a[b+r|0]|0)!=(a[f+o|0]|0)){break c}s=p+1|0;if((s|0)<(n|0)){o=o-1|0;p=s;q=r}else{break b}}}}while(0);m=n+l|0;if((m|0)==(e|0)){j=i;t=10;break a}else{l=m}}l=i-n|0;if((l|0)>-1){i=l}else{j=l;t=10;break}}if((t|0)==10){k=j+1|0;return k|0}return 0}function sc(b,d,e,f){b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0;g=i;i=i+8|0;h=g|0;j=a[d]|0;if((a[4192+(j&255)|0]|0)==0){k=0}else{l=a[e]|0;m=0;n=d;o=j;while(1){if(o<<24>>24==l<<24>>24){j=n;p=e;while(1){q=p+1|0;r=a[q]|0;if(r<<24>>24==0){s=j;t=1;break}u=j+1|0;if((a[u]|0)==r<<24>>24){j=u;p=q}else{s=n;t=0;break}}v=s;w=t+m|0;x=a[s]|0}else{v=n;w=m;x=o}p=v+(a[4192+(x&255)|0]|0)|0;j=a[p]|0;if((a[4192+(j&255)|0]|0)==0){k=w;break}else{m=w;n=p;o=j}}}o=k+1|0;k=Vd(o<<2)|0;n=h;w=d;m=d;d=0;while(1){x=a[w]|0;a:do{if(x<<24>>24==(a[e]|0)){v=w;s=e;while(1){t=s+1|0;l=a[t]|0;if(l<<24>>24==0){y=v;z=13;break a}j=v+1|0;if((a[j]|0)==l<<24>>24){v=j;s=t}else{z=12;break}}}else{z=12}}while(0);if((z|0)==12){z=0;if(x<<24>>24==0){y=w;z=13}else{A=m;B=d;C=w}}if((z|0)==13){z=0;s=w-m|0;v=nc(s+1|0)|0;t=c[v+8>>2]|0;a[t+s|0]=0;bo(t|0,m|0,s|0)|0;c[n>>2]=v;c[k+(d<<2)>>2]=eh(0,0,b,h)|0;if((a[y]|0)==0){break}A=y+1|0;B=d+1|0;C=y}w=C+1|0;m=A;d=B}c[f+12>>2]=k;c[f+16>>2]=o;i=g;return}function tc(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=Vd(128)|0;f=e;c[e+120>>2]=c[a+20>>2];b[e+50>>1]=c[a+4>>2];a=Vd(32)|0;g=e+76|0;c[g>>2]=Vd(16)|0;h=e+72|0;c[h>>2]=Vd(2)|0;c[e+44>>2]=0;c[e+116>>2]=d;b[e+48>>1]=0;c[e+52>>2]=0;d=e+64|0;c[d>>2]=0;c[d+4>>2]=0;d=e+84|0;c[d>>2]=0;c[e+80>>2]=0;c[e+112>>2]=0;c[e+40>>2]=0;fo(e|0,0,36)|0;i=e+104|0;c[i>>2]=Vd(12)|0;e=Vd(16)|0;c[c[i>>2]>>2]=e;c[(c[i>>2]|0)+4>>2]=0;c[(c[i>>2]|0)+8>>2]=4;Gc(f);Yn(c[g>>2]|0,4632,16)|0;c[d>>2]=a;c[a+28>>2]=0;c[a+24>>2]=0;b[c[h>>2]>>1]=57;return f|0}function uc(a){a=a|0;var b=0,d=0,e=0,f=0,g=0;b=c[a+4>>2]|0;d=a+84|0;e=c[d>>2]|0;if((e|0)!=0){f=c[e+28>>2]|0;if((f|0)==0){g=e}else{e=f;while(1){c[d>>2]=e;f=c[e+28>>2]|0;if((f|0)==0){g=e;break}else{e=f}}}while(1){e=c[g+24>>2]|0;Ln(g);if((e|0)==0){break}else{g=e}}}g=a+32|0;e=(c[g>>2]|0)-1|0;if((e|0)>-1){d=e;do{e=c[b+(d<<2)>>2]|0;ch(e);Ln(e);d=d-1|0;}while((d|0)>-1)}c[a+24>>2]=0;c[a+28>>2]=0;c[g>>2]=0;Ln(b);b=c[a+8>>2]|0;while(1){g=c[b+36>>2]|0;if((g|0)==0){break}else{b=g}}if((b|0)!=0){g=b;while(1){b=c[g+40>>2]|0;Ln(g);if((b|0)==0){break}else{g=b}}}Hc(a);Ic(a);g=a+104|0;Ln(c[c[g>>2]>>2]|0);Ln(c[g>>2]|0);Ln(c[a+12>>2]|0);Ln(c[a+72>>2]|0);Ln(c[a+76>>2]|0);Ln(a);return}function vc(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0;g=a+48|0;if((e[g>>1]|0)>>>0>=(e[a+50>>1]|0)>>>0){Hc(a)}h=a+20|0;i=c[h>>2]|0;if((i|0)==0){j=Vd(32)|0}else{c[h>>2]=c[i+24>>2];j=i}c[j>>2]=d;c[j+8>>2]=f;c[j+16>>2]=0;d=a+16|0;c[j+24>>2]=c[d>>2];c[d>>2]=j;c[f+8>>2]=j;b[g>>1]=(b[g>>1]|0)+1;return}function wc(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0;b=i;i=i+8|0;f=b|0;g=c[(c[a>>2]|0)+((e[d>>1]|0)<<2)>>2]|0;d=a+8|0;c[d>>2]=c[(c[d>>2]|0)+36>>2];h=a+44|0;c[h>>2]=(c[h>>2]|0)-1;j=Jc(a)|0;c[d>>2]=c[(c[d>>2]|0)+40>>2];c[h>>2]=(c[h>>2]|0)+1;c[f>>2]=j;gh(g,f);i=b;return}function xc(a,b,d){a=a|0;b=b|0;d=d|0;Wm(a,c[(c[a>>2]|0)+((e[d+2>>1]|0)<<2)>>2]|0);return}function yc(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0;d=c[(c[a>>2]|0)+(e[f+2>>1]<<2)>>2]|0;if((b[(c[c[d+8>>2]>>2]|0)+52>>1]|0)==2){Fn(c[a+120>>2]|0,c[(c[d+16>>2]|0)+8>>2]|0);g=a+120|0;h=c[g>>2]|0;Fn(h,15024);return}else{f=c[a+100>>2]|0;qn(f);pn(f,d);Fn(c[a+120>>2]|0,c[f>>2]|0);g=a+120|0;h=c[g>>2]|0;Fn(h,15024);return}}function zc(d,f){d=d|0;f=f|0;var g=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0.0,z=0,A=0,B=0,C=0,D=0;g=i;j=c[d>>2]|0;k=c[d+100>>2]|0;qn(k);l=c[(c[(c[j+((e[f+2>>1]|0)<<2)>>2]|0)+16>>2]|0)+8>>2]|0;m=c[(c[j+((e[f+4>>1]|0)<<2)>>2]|0)+16>>2]|0;f=m+16|0;j=d+116|0;d=m+12|0;m=0;n=0;o=0;p=0;while(1){q=a[l+n|0]|0;if((q<<24>>24|0)==37){if((m|0)==(c[f>>2]|0)){Ae(c[j>>2]|0,8,18936,(r=i,i=i+1|0,i=i+7&-8,c[r>>2]=0,r)|0);i=r}ln(k,l,o,p);s=n+2|0;t=n+1|0;u=c[(c[(c[(c[d>>2]|0)+(m<<2)>>2]|0)+16>>2]|0)+12>>2]|0;v=c[u+8>>2]|0;w=b[(c[v>>2]|0)+52>>1]|0;x=u+16|0;y=+h[x>>3];z=c[x>>2]|0;x=a[l+t|0]|0;do{if((x<<24>>24|0)==100){if(w<<16>>16!=0){Ae(c[j>>2]|0,8,16664,(r=i,i=i+8|0,c[r>>2]=v,r)|0);i=r}nn(k,z)}else if((x<<24>>24|0)==102){if(w<<16>>16!=1){Ae(c[j>>2]|0,8,15184,(r=i,i=i+8|0,c[r>>2]=v,r)|0);i=r}on(k,y)}else if((x<<24>>24|0)==115){if(w<<16>>16==2){jn(k,c[z+8>>2]|0);break}else{pn(k,u);break}}}while(0);A=m+1|0;B=t;C=s;D=s}else if((q<<24>>24|0)==0){break}else{A=m;B=n;C=o;D=p+1|0}m=A;n=B+1|0;o=C;p=D}ln(k,l,o,p);i=g;return}function Ac(a,b,d){a=a|0;b=b|0;d=d|0;zc(a,d);Fn(c[a+120>>2]|0,c[c[a+100>>2]>>2]|0);return}function Bc(a,b,d,f,g,h){a=a|0;b=b|0;d=d|0;f=f|0;g=g|0;h=h|0;var j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0;j=i;i=i+16|0;k=j|0;l=c[f+16>>2]|0;f=a+8|0;m=c[f>>2]|0;n=l+24|0;o=(c[n>>2]|0)==0;p=(d|0)!=0;if(o){q=e[l+48>>1]|0}else{q=g}r=a+24|0;s=c[r>>2]|0;t=q+(p&1)+s|0;if((t+s|0)>>>0>(c[a+28>>2]|0)>>>0){Kc(a,t)}t=a|0;s=c[t>>2]|0;u=c[(c[m+36>>2]|0)+8>>2]|0;m=s+(u<<2)|0;if(p){p=c[m>>2]|0;v=p|0;if((c[v>>2]&7340032|0)==0){ch(p)}c[p+8>>2]=d;c[v>>2]=1048576;w=p}else{w=0}if((c[b>>2]|0)==0){p=c[f>>2]|0;c[p+12>>2]=c[a+72>>2];c[p+16>>2]=0;c[p+4>>2]=c[m>>2];c[p+32>>2]=0;c[p+20>>2]=0;m=p+40|0;v=c[m>>2]|0;if((v|0)==0){Gc(a);c[f>>2]=p;y=c[m>>2]|0}else{y=v}c[y+12>>2]=c[l+28>>2];c[y+16>>2]=0;c[y+8>>2]=q;c[y>>2]=l;c[y+20>>2]=0;c[y+32>>2]=0}y=u+1|0;u=s+(y<<2)|0;v=k|0;m=k;c[m>>2]=h;c[m+4>>2]=0;if((g|0)>0){m=0;while(1){h=(x=c[v+4>>2]|0,c[v+4>>2]=x+8,c[(c[v>>2]|0)+x>>2]|0);k=s+(m+y<<2)|0;p=c[k>>2]|0;if((c[p>>2]&7340032|0)==0){ch(p)}if((c[h>>2]&7340032|0)==0){p=c[h+16>>2]|0;c[p>>2]=(c[p>>2]|0)+1}p=c[k>>2]|0;k=h;c[p>>2]=c[k>>2];c[p+4>>2]=c[k+4>>2];c[p+8>>2]=c[k+8>>2];c[p+12>>2]=c[k+12>>2];c[p+16>>2]=c[k+16>>2];c[p+20>>2]=c[k+20>>2];k=m+1|0;if((k|0)<(g|0)){m=k}else{z=g;break}}}else{z=0}do{if((c[b>>2]|0)==0&o){if((z|0)==(e[l+48>>1]|0|0)){break}Lc(a,l,z)}}while(0);c[t>>2]=u;c[f>>2]=c[(c[f>>2]|0)+40>>2];c[r>>2]=(c[r>>2]|0)+q;c[b>>2]=1;if((c[l+28>>2]|0)==0){c[t>>2]=(c[t>>2]|0)-4;ub[c[n>>2]&255](a,g+1&65535,6560);g=c[(c[f>>2]|0)+36>>2]|0;c[f>>2]=g;c[r>>2]=(c[r>>2]|0)-q;A=g;B=A+36|0;C=c[B>>2]|0;D=C+8|0;E=c[D>>2]|0;F=c[t>>2]|0;G=-E|0;H=F+(G<<2)|0;c[t>>2]=H;i=j;return w|0}else{g=a+44|0;c[g>>2]=(c[g>>2]|0)+1;Cc(a);A=c[f>>2]|0;B=A+36|0;C=c[B>>2]|0;D=C+8|0;E=c[D>>2]|0;F=c[t>>2]|0;G=-E|0;H=F+(G<<2)|0;c[t>>2]=H;i=j;return w|0}return 0}function Cc(a){a=a|0;var d=0,f=0,g=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0,P=0,Q=0,R=0,S=0,T=0,U=0,V=0,W=0,X=0,Y=0,Z=0,_=0,$=0,aa=0.0,ba=0,ca=0,da=0,ea=0.0,fa=0,ga=0,ha=0,la=0,oa=0.0,pa=0,sa=0,ta=0,ua=0,va=0,wa=0,xa=0.0,ya=0,za=0,Aa=0,Ba=0,Ca=0,Da=0,Ea=0.0,Fa=0,Ga=0,Ha=0,Ia=0,Ja=0,Ka=0,La=0.0,Ma=0,Na=0,Oa=0,Pa=0,Qa=0,Ra=0,Sa=0.0,Ta=0,Ua=0,Va=0,Wa=0,Xa=0,Ya=0,Za=0.0,_a=0,$a=0,ab=0,bb=0,cb=0,db=0.0,eb=0,fb=0,gb=0,hb=0,ib=0,jb=0.0,kb=0,lb=0,mb=0,nb=0,ob=0,pb=0,qb=0,rb=0,sb=0,tb=0,ub=0,vb=0,wb=0,xb=0,yb=0,zb=0,Ab=0,Bb=0,Cb=0,Db=0,Eb=0,Fb=0,Gb=0,Hb=0,Ib=0,Jb=0,Kb=0,Lb=0,Mb=0,Nb=0,Ob=0,Pb=0,Qb=0,Rb=0,Sb=0,Tb=0,Ub=0,Vb=0,Wb=0,Xb=0,Yb=0,Zb=0,_b=0,$b=0,ac=0,bc=0,cc=0,dc=0,ec=0;d=i;f=1;g=0;j=i;i=i+168|0;c[j>>2]=0;while(1)switch(f|0){case 1:k=c[a+112>>2]|0;l=c[(c[k+44>>2]|0)+24>>2]|0;m=c[(c[k+48>>2]|0)+24>>2]|0;n=a+8|0;o=c[n>>2]|0;p=o|0;q=c[(c[p>>2]|0)+28>>2]|0;r=a|0;s=c[r>>2]|0;v=a+4|0;w=c[v>>2]|0;x=a+28|0;y=c[x>>2]|0;z=0;A=a+116|0;k=ia(8,c[A>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;B=co(k+8|0,f,j)|0;f=202;break;case 202:if((B|0)==0){C=q;D=w;E=s;F=o;H=0;f=7;break}else{f=2;break};case 2:if((c[(c[p>>2]|0)+28>>2]|0)==0){f=4;break}else{f=3;break};case 3:c[o+20>>2]=e[(c[o+12>>2]|0)+(z+1<<1)>>1]|0;f=4;break;case 4:k=ia(4,a|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;if((k|0)==0){f=5;break}else{f=6;break};case 5:ja(32,c[A>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;C=q;D=w;E=s;F=o;H=0;f=7;break;case 6:k=c[n>>2]|0;I=c[k+12>>2]|0;z=c[k+16>>2]|0;J=c[k+28>>2]|0;K=c[v>>2]|0;L=c[r>>2]|0;c[a+24>>2]=(L-K>>2)+(c[k+8>>2]|0);C=I;D=K;E=L;F=k;H=J;f=7;break;case 7:M=a+24|0;N=a+12|0;O=a+44|0;P=a+84|0;Q=a+104|0;R=a+80|0;S=C;T=D;U=E;V=c[M>>2]|0;W=y;X=F;Y=H;f=8;break;case 8:switch(e[S+(z<<1)>>1]|0){case 0:{f=10;break};case 7:{f=104;break};case 5:{f=107;break};case 40:{f=11;break};case 8:{f=110;break};case 9:{f=111;break};case 10:{f=112;break};case 11:{f=113;break};case 12:{f=114;break};case 16:{f=115;break};case 3:{f=12;break};case 24:{f=126;break};case 4:{f=13;break};case 25:{f=135;break};case 13:{f=14;break};case 28:{f=149;break};case 29:{f=150;break};case 26:{f=151;break};case 38:{f=153;break};case 39:{f=154;break};case 1:{f=155;break};case 2:{f=156;break};case 36:{f=157;break};case 41:{f=158;break};case 37:{f=159;break};case 42:{f=160;break};case 31:{f=161;break};case 30:{f=162;break};case 32:{f=163;break};case 33:{f=164;break};case 54:{f=168;break};case 52:{f=169;break};case 51:{f=173;break};case 47:{f=174;break};case 34:{f=175;break};case 43:{f=180;break};case 44:{f=183;break};case 46:{f=184;break};case 48:{f=185;break};case 49:{f=186;break};case 50:{f=191;break};case 53:{f=195;break};case 55:{f=196;break};case 56:{f=197;break};case 35:{f=198;break};case 14:{f=20;break};case 57:{f=201;break};case 19:{f=26;break};case 20:{f=37;break};case 17:{f=48;break};case 21:{f=61;break};case 22:{f=72;break};case 18:{f=83;break};case 27:{f=9;break};case 23:{f=96;break};case 6:{f=97;break};case 15:{f=98;break};default:{S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break}}break;case 9:Z=X+36|0;f=152;break;case 10:J=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;k=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;c[k>>2]=c[J>>2];L=J+16|0;J=c[L+4>>2]|0;K=k+16|0;c[K>>2]=c[L>>2];c[K+4>>2]=J;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 11:J=c[(c[N>>2]|0)+(e[S+(z+2<<1)>>1]<<2)>>2]|0;K=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;ja(22,K|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;L=J+24|0;J=c[L+4>>2]|0;k=K+16|0;c[k>>2]=c[L>>2];c[k+4>>2]=J;c[K>>2]=2097152;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 12:K=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;J=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;k=io(c[J>>2]|0,c[J+4>>2]|0,c[K>>2]|0,c[K+4>>2]|0)|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=k;c[K+4>>2]=G;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 13:K=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;k=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;J=jo(c[K>>2]|0,c[K+4>>2]|0,c[k>>2]|0,c[k+4>>2]|0)|0;k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=J;c[k+4>>2]=G;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 14:_=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;$=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;if((c[_+8>>2]|0)==(m|0)){f=15;break}else{f=18;break};case 15:aa=+h[_+16>>3];ba=$+16|0;if((c[$+8>>2]|0)==(m|0)){f=16;break}else{f=17;break};case 16:h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=aa+ +h[ba>>3];f=19;break;case 17:k=ba|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=aa+(+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0);f=19;break;case 18:k=_+16|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0+ +h[$+16>>3];f=19;break;case 19:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 20:ca=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;da=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;if((c[ca+8>>2]|0)==(m|0)){f=21;break}else{f=24;break};case 21:ea=+h[ca+16>>3];fa=da+16|0;if((c[da+8>>2]|0)==(m|0)){f=22;break}else{f=23;break};case 22:h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=ea- +h[fa>>3];f=25;break;case 23:k=fa|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=ea-(+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0);f=25;break;case 24:k=ca+16|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0- +h[da+16>>3];f=25;break;case 25:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 26:ga=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;ha=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;la=c[ga+8>>2]|0;if((la|0)==(m|0)){f=27;break}else{f=30;break};case 27:oa=+h[ga+16>>3];pa=ha+16|0;if((c[ha+8>>2]|0)==(m|0)){f=28;break}else{f=29;break};case 28:k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=oa<+h[pa>>3];c[k+4>>2]=0;f=36;break;case 29:k=pa|0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=oa<+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0;c[J+4>>2]=0;f=36;break;case 30:if((la|0)==(l|0)){f=31;break}else{f=34;break};case 31:J=ga+16|0;sa=c[J>>2]|0;ta=c[J+4>>2]|0;if((c[ha+8>>2]|0)==(l|0)){f=32;break}else{f=33;break};case 32:J=ha+16|0;k=c[J+4>>2]|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=((ta|0)<(k|0)|(ta|0)==(k|0)&sa>>>0<(c[J>>2]|0)>>>0)&1;c[K+4>>2]=0;f=36;break;case 33:K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=+(sa>>>0)+ +(ta|0)*4294967296.0<+h[ha+16>>3];c[K+4>>2]=0;f=36;break;case 34:if((b[(c[la>>2]|0)+52>>1]|0)==2){f=35;break}else{f=36;break};case 35:K=qa(4,c[(c[ga+16>>2]|0)+8>>2]|0,c[(c[ha+16>>2]|0)+8>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=(K|0)==-1;c[J+4>>2]=0;f=36;break;case 36:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 37:ua=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;va=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;wa=c[ua+8>>2]|0;if((wa|0)==(m|0)){f=38;break}else{f=41;break};case 38:xa=+h[ua+16>>3];ya=va+16|0;if((c[va+8>>2]|0)==(m|0)){f=39;break}else{f=40;break};case 39:J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=xa<=+h[ya>>3];c[J+4>>2]=0;f=47;break;case 40:J=ya|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=xa<=+((c[J>>2]|0)>>>0)+ +(c[J+4>>2]|0)*4294967296.0;c[K+4>>2]=0;f=47;break;case 41:if((wa|0)==(l|0)){f=42;break}else{f=45;break};case 42:K=ua+16|0;za=c[K>>2]|0;Aa=c[K+4>>2]|0;if((c[va+8>>2]|0)==(l|0)){f=43;break}else{f=44;break};case 43:K=va+16|0;J=c[K+4>>2]|0;k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=(Aa|0)<=(J|0)&((Aa|0)<(J|0)|za>>>0<=(c[K>>2]|0)>>>0)&1;c[k+4>>2]=0;f=47;break;case 44:k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=+(za>>>0)+ +(Aa|0)*4294967296.0<=+h[va+16>>3];c[k+4>>2]=0;f=47;break;case 45:if((b[(c[wa>>2]|0)+52>>1]|0)==2){f=46;break}else{f=47;break};case 46:k=qa(4,c[(c[ua+16>>2]|0)+8>>2]|0,c[(c[va+16>>2]|0)+8>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=(k|0)<1;c[K+4>>2]=0;f=47;break;case 47:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 48:Ba=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;Ca=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;Da=c[Ba+8>>2]|0;if((Da|0)==(m|0)){f=49;break}else{f=52;break};case 49:Ea=+h[Ba+16>>3];Fa=Ca+16|0;if((c[Ca+8>>2]|0)==(m|0)){f=50;break}else{f=51;break};case 50:K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=Ea==+h[Fa>>3];c[K+4>>2]=0;f=60;break;case 51:K=Fa|0;k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=Ea==+((c[K>>2]|0)>>>0)+ +(c[K+4>>2]|0)*4294967296.0;c[k+4>>2]=0;f=60;break;case 52:if((Da|0)==(l|0)){f=53;break}else{f=56;break};case 53:k=Ba+16|0;Ga=c[k>>2]|0;Ha=c[k+4>>2]|0;if((c[Ca+8>>2]|0)==(l|0)){f=54;break}else{f=55;break};case 54:k=Ca+16|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=(Ga|0)==(c[k>>2]|0)&(Ha|0)==(c[k+4>>2]|0)&1;c[K+4>>2]=0;f=60;break;case 55:K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=+(Ga>>>0)+ +(Ha|0)*4294967296.0==+h[Ca+16>>3];c[K+4>>2]=0;f=60;break;case 56:if((b[(c[Da>>2]|0)+52>>1]|0)==2){f=57;break}else{f=58;break};case 57:K=qa(4,c[(c[Ba+16>>2]|0)+8>>2]|0,c[(c[Ca+16>>2]|0)+8>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=(K|0)==0;c[k+4>>2]=0;f=60;break;case 58:if((Da|0)==(c[Ca+8>>2]|0)){f=59;break}else{f=60;break};case 59:k=ma(4,a|0,Ba|0,Ca|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=(k|0)==1;c[K+4>>2]=0;f=60;break;case 60:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 61:Ia=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;Ja=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;Ka=c[Ia+8>>2]|0;if((Ka|0)==(m|0)){f=62;break}else{f=65;break};case 62:La=+h[Ia+16>>3];Ma=Ja+16|0;if((c[Ja+8>>2]|0)==(m|0)){f=63;break}else{f=64;break};case 63:K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=La>+h[Ma>>3];c[K+4>>2]=0;f=71;break;case 64:K=Ma|0;k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=La>+((c[K>>2]|0)>>>0)+ +(c[K+4>>2]|0)*4294967296.0;c[k+4>>2]=0;f=71;break;case 65:if((Ka|0)==(l|0)){f=66;break}else{f=69;break};case 66:k=Ia+16|0;Na=c[k>>2]|0;Oa=c[k+4>>2]|0;if((c[Ja+8>>2]|0)==(l|0)){f=67;break}else{f=68;break};case 67:k=Ja+16|0;K=c[k+4>>2]|0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=((Oa|0)>(K|0)|(Oa|0)==(K|0)&Na>>>0>(c[k>>2]|0)>>>0)&1;c[J+4>>2]=0;f=71;break;case 68:J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=+(Na>>>0)+ +(Oa|0)*4294967296.0>+h[Ja+16>>3];c[J+4>>2]=0;f=71;break;case 69:if((b[(c[Ka>>2]|0)+52>>1]|0)==2){f=70;break}else{f=71;break};case 70:J=qa(4,c[(c[Ia+16>>2]|0)+8>>2]|0,c[(c[Ja+16>>2]|0)+8>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=(J|0)==1;c[k+4>>2]=0;f=71;break;case 71:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 72:Pa=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;Qa=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;Ra=c[Pa+8>>2]|0;if((Ra|0)==(m|0)){f=73;break}else{f=76;break};case 73:Sa=+h[Pa+16>>3];Ta=Qa+16|0;if((c[Qa+8>>2]|0)==(m|0)){f=74;break}else{f=75;break};case 74:k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=Sa>+h[Ta>>3];c[k+4>>2]=0;f=82;break;case 75:k=Ta|0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=Sa>+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0;c[J+4>>2]=0;f=82;break;case 76:if((Ra|0)==(l|0)){f=77;break}else{f=80;break};case 77:J=Pa+16|0;Ua=c[J>>2]|0;Va=c[J+4>>2]|0;if((c[Qa+8>>2]|0)==(l|0)){f=78;break}else{f=79;break};case 78:J=Qa+16|0;k=c[J+4>>2]|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=((Va|0)>(k|0)|(Va|0)==(k|0)&Ua>>>0>(c[J>>2]|0)>>>0)&1;c[K+4>>2]=0;f=82;break;case 79:K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=+(Ua>>>0)+ +(Va|0)*4294967296.0>+h[Qa+16>>3];c[K+4>>2]=0;f=82;break;case 80:if((b[(c[Ra>>2]|0)+52>>1]|0)==2){f=81;break}else{f=82;break};case 81:K=qa(4,c[(c[Pa+16>>2]|0)+8>>2]|0,c[(c[Qa+16>>2]|0)+8>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=K>>>31^1;c[J+4>>2]=0;f=82;break;case 82:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 83:Wa=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;Xa=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;Ya=c[Wa+8>>2]|0;if((Ya|0)==(m|0)){f=84;break}else{f=87;break};case 84:Za=+h[Wa+16>>3];_a=Xa+16|0;if((c[Xa+8>>2]|0)==(m|0)){f=85;break}else{f=86;break};case 85:J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=Za!=+h[_a>>3];c[J+4>>2]=0;f=95;break;case 86:J=_a|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=Za!=+((c[J>>2]|0)>>>0)+ +(c[J+4>>2]|0)*4294967296.0;c[K+4>>2]=0;f=95;break;case 87:if((Ya|0)==(l|0)){f=88;break}else{f=91;break};case 88:K=Wa+16|0;$a=c[K>>2]|0;ab=c[K+4>>2]|0;if((c[Xa+8>>2]|0)==(l|0)){f=89;break}else{f=90;break};case 89:K=Xa+16|0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=(($a|0)!=(c[K>>2]|0)|(ab|0)!=(c[K+4>>2]|0))&1;c[J+4>>2]=0;f=95;break;case 90:J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=+($a>>>0)+ +(ab|0)*4294967296.0!=+h[Xa+16>>3];c[J+4>>2]=0;f=95;break;case 91:if((b[(c[Ya>>2]|0)+52>>1]|0)==2){f=92;break}else{f=93;break};case 92:J=qa(4,c[(c[Wa+16>>2]|0)+8>>2]|0,c[(c[Xa+16>>2]|0)+8>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=(J|0)!=0;c[K+4>>2]=0;f=95;break;case 93:if((Ya|0)==(c[Xa+8>>2]|0)){f=94;break}else{f=95;break};case 94:K=ma(4,a|0,Wa|0,Xa|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=(K|0)!=1;c[J+4>>2]=0;f=95;break;case 95:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 96:z=e[S+(z+1<<1)>>1]|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 97:J=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;K=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;k=so(c[K>>2]|0,c[K+4>>2]|0,c[J>>2]|0,c[J+4>>2]|0)|0;J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=k;c[J+4>>2]=G;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 98:bb=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;cb=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;if((c[bb+8>>2]|0)==(m|0)){f=99;break}else{f=102;break};case 99:db=+h[bb+16>>3];eb=cb+16|0;if((c[cb+8>>2]|0)==(m|0)){f=100;break}else{f=101;break};case 100:h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=db*+h[eb>>3];f=103;break;case 101:J=eb|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=db*(+((c[J>>2]|0)>>>0)+ +(c[J+4>>2]|0)*4294967296.0);f=103;break;case 102:J=bb+16|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=(+((c[J>>2]|0)>>>0)+ +(c[J+4>>2]|0)*4294967296.0)*+h[cb+16>>3];f=103;break;case 103:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 104:J=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;if((c[J>>2]|0)==0&(c[J+4>>2]|0)==0){f=105;break}else{f=106;break};case 105:ra(8,c[A>>2]|0,2,13792,(fb=i,i=i+1|0,i=i+7&-8,c[fb>>2]=0,fb)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=fb;f=106;break;case 106:J=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;k=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;K=qo(c[J>>2]|0,c[J+4>>2]|0,c[k>>2]|0,c[k+4>>2]|0)|0;k=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[k>>2]=K;c[k+4>>2]=G;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 107:k=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;if((c[k>>2]|0)==0&(c[k+4>>2]|0)==0){f=108;break}else{f=109;break};case 108:ra(8,c[A>>2]|0,2,13792,(fb=i,i=i+1|0,i=i+7&-8,c[fb>>2]=0,fb)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=fb;f=109;break;case 109:k=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;K=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;J=ro(c[k>>2]|0,c[k+4>>2]|0,c[K>>2]|0,c[K+4>>2]|0)|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=J;c[K+4>>2]=G;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 110:K=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;J=ko(c[K>>2]|0,c[K+4>>2]|0,c[(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16>>2]|0)|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=J;c[K+4>>2]=G;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 111:K=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;J=mo(c[K>>2]|0,c[K+4>>2]|0,c[(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16>>2]|0)|0;K=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[K>>2]=J;c[K+4>>2]=G;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 112:K=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;J=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;k=c[J+4>>2]&c[K+4>>2];L=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[L>>2]=c[J>>2]&c[K>>2];c[L+4>>2]=k;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 113:k=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;L=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;K=c[L+4>>2]|c[k+4>>2];J=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[J>>2]=c[L>>2]|c[k>>2];c[J+4>>2]=K;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 114:K=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;J=(c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0)+16|0;k=c[J+4>>2]^c[K+4>>2];L=(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16|0;c[L>>2]=c[J>>2]^c[K>>2];c[L+4>>2]=k;c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 115:gb=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;k=b[(c[c[gb+8>>2]>>2]|0)+52>>1]|0;if((k<<16>>16|0)==0){f=116;break}else if((k<<16>>16|0)==1){f=118;break}else{f=120;break};case 116:k=gb+16|0;if((c[k>>2]|0)==0&(c[k+4>>2]|0)==0){f=117;break}else{f=120;break};case 117:ra(8,c[A>>2]|0,2,13792,(fb=i,i=i+1|0,i=i+7&-8,c[fb>>2]=0,fb)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=fb;f=120;break;case 118:if(+h[gb+16>>3]==0.0){f=119;break}else{f=120;break};case 119:ra(8,c[A>>2]|0,2,13792,(fb=i,i=i+1|0,i=i+7&-8,c[fb>>2]=0,fb)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=fb;f=120;break;case 120:hb=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;ib=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;if((c[hb+8>>2]|0)==(m|0)){f=121;break}else{f=124;break};case 121:jb=+h[hb+16>>3];kb=ib+16|0;if((c[ib+8>>2]|0)==(m|0)){f=122;break}else{f=123;break};case 122:h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=jb/+h[kb>>3];f=125;break;case 123:k=kb|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=jb/(+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0);f=125;break;case 124:k=hb+16|0;h[(c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0)+16>>3]=(+((c[k>>2]|0)>>>0)+ +(c[k+4>>2]|0)*4294967296.0)/+h[ib+16>>3];f=125;break;case 125:c[c[U+(e[S+(z+4<<1)>>1]<<2)>>2]>>2]=4194304;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 126:lb=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;mb=b[(c[c[lb+8>>2]>>2]|0)+52>>1]|0;if((mb&-5)<<16>>16==0){f=127;break}else{f=128;break};case 127:k=lb+16|0;nb=(c[k>>2]|0)==0&(c[k+4>>2]|0)==0&1;f=132;break;case 128:if((mb<<16>>16|0)==1){f=129;break}else if((mb<<16>>16|0)==2){f=130;break}else if((mb<<16>>16|0)==7){f=131;break}else{nb=1;f=132;break};case 129:nb=+h[lb+16>>3]==0.0|0;f=132;break;case 130:nb=(c[(c[lb+16>>2]|0)+4>>2]|0)==0|0;f=132;break;case 131:nb=(c[(c[lb+16>>2]|0)+16>>2]|0)==0|0;f=132;break;case 132:ob=z;if((nb|0)==(e[S+(z+1<<1)>>1]|0)){f=134;break}else{f=133;break};case 133:z=e[S+(ob+3<<1)>>1]|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 134:z=ob+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 135:if((c[O>>2]|0)>>>0>100>>>0){f=136;break}else{f=137;break};case 136:ra(8,c[A>>2]|0,6,12424,(fb=i,i=i+1|0,i=i+7&-8,c[fb>>2]=0,fb)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=fb;f=137;break;case 137:pb=X+40|0;if((c[pb>>2]|0)==0){f=138;break}else{f=139;break};case 138:ja(6,a|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;f=139;break;case 139:qb=e[S+(z+3<<1)>>1]|0;if((b[S+(z+2<<1)>>1]|0)==1){f=140;break}else{f=141;break};case 140:rb=(c[(c[N>>2]|0)+(qb<<2)>>2]|0)+24|0;f=142;break;case 141:rb=(c[U+(qb<<2)>>2]|0)+16|0;f=142;break;case 142:sb=c[rb>>2]|0;tb=b[S+(z+4<<1)>>1]|0;c[X+20>>2]=e[S+(z+1<<1)>>1]|0;ub=(tb&65535)+6|0;c[X+16>>2]=ub+z;c[X+28>>2]=Y;vb=sb+28|0;if((c[vb>>2]|0)==0){f=146;break}else{f=143;break};case 143:wb=sb+48|0;xb=(e[wb>>1]|0)+V|0;if((xb|0)>(W|0)){f=144;break}else{yb=T;zb=U;Ab=W;f=145;break};case 144:ka(10,a|0,xb|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;yb=c[v>>2]|0;zb=c[r>>2]|0;Ab=c[x>>2]|0;f=145;break;case 145:na(112,a|0,sb|0,S+(z<<1)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;k=c[M>>2]|0;c[X+4>>2]=c[zb+(e[S+(z+5<<1)>>1]<<2)>>2];L=zb+(c[X+8>>2]<<2)|0;c[r>>2]=L;K=c[pb>>2]|0;c[n>>2]=K;c[K>>2]=sb;c[K+8>>2]=e[wb>>1]|0;c[K+12>>2]=c[vb>>2];c[K+28>>2]=0;c[O>>2]=(c[O>>2]|0)+1;z=0;S=c[vb>>2]|0;T=yb;U=L;V=k;W=Ab;X=K;Y=0;f=8;break;case 146:K=c[sb+24>>2]|0;Bb=c[pb>>2]|0;c[n>>2]=Bb;c[Bb>>2]=sb;c[Bb+20>>2]=-1;c[Bb+12>>2]=0;c[Bb+32>>2]=0;c[Bb+28>>2]=0;c[Bb+8>>2]=1;c[O>>2]=(c[O>>2]|0)+1;c[M>>2]=(c[M>>2]|0)+1;na(K|0,a|0,tb|0,S+(z+5<<1)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;Cb=c[x>>2]|0;if((Cb|0)==(W|0)){Db=T;Eb=U;Fb=W;f=148;break}else{f=147;break};case 147:Db=c[v>>2]|0;Eb=c[r>>2]|0;Fb=Cb;f=148;break;case 148:c[M>>2]=(c[M>>2]|0)-1;K=c[Bb+36>>2]|0;c[n>>2]=K;z=ub+z|0;c[O>>2]=(c[O>>2]|0)-1;S=S;T=Db;U=Eb;V=V;W=Fb;X=K;Y=Y;f=8;break;case 149:K=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;k=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;c[k>>2]=4194304;L=K+16|0;K=k+16|0;c[K>>2]=(c[L>>2]|0)==0&(c[L+4>>2]|0)==0&1;c[K+4>>2]=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 150:K=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;L=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;c[L>>2]=4194304;k=K+16|0;K=jo(0,0,c[k>>2]|0,c[k+4>>2]|0)|0;k=L+16|0;c[k>>2]=K;c[k+4>>2]=G;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 151:k=X+36|0;ka(60,c[(c[k>>2]|0)+4>>2]|0,c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;Z=k;f=152;break;case 152:c[X+32>>2]=0;k=c[Z>>2]|0;c[n>>2]=k;c[O>>2]=(c[O>>2]|0)-1;K=V-(c[(c[k+40>>2]|0)+8>>2]|0)|0;c[M>>2]=K;L=U+(-(c[k+8>>2]|0)<<2)|0;c[r>>2]=L;z=c[k+16>>2]|0;S=c[k+12>>2]|0;T=T;U=L;V=K;W=W;X=k;Y=c[k+28>>2]|0;f=8;break;case 153:ka(60,c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0,c[T+(e[S+(z+2<<1)>>1]<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 154:ka(60,c[T+(e[S+(z+3<<1)>>1]<<2)>>2]|0,c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 155:ka(60,c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0,c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 156:na(38,a|0,c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0,c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 157:na(26,a|0,S|0,z|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 158:na(58,c[r>>2]|0,S|0,z|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 159:na(74,a|0,S|0,z|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 160:na(56,c[r>>2]|0,S|0,z|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 161:na(16,a|0,S|0,z|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=(e[S+(z+2<<1)>>1]|0)+4+z|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 162:ka(4,a|0,S+(z<<1)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=(e[S+(z+2<<1)>>1]|0)+4+z|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 163:ka(18,a|0,S+(z<<1)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=(e[S+(z+3<<1)>>1]|0)+5+z|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 164:Gb=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;Hb=Gb+8|0;Ib=c[(c[(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16>>2]|0)+12>>2]|0;Jb=Ib+8|0;if((c[Hb>>2]|0)==(c[Jb>>2]|0)){f=165;break}else{f=166;break};case 165:ka(60,Gb|0,Ib|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;f=167;break;case 166:c[X+20>>2]=e[(c[X+12>>2]|0)+(z+1<<1)>>1]|0;k=c[Hb>>2]|0;ra(8,c[A>>2]|0,4,11288,(fb=i,i=i+16|0,c[fb>>2]=c[Jb>>2],c[fb+8>>2]=k,fb)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=fb;f=167;break;case 167:z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 168:k=z;ra(4,a|0,b[S+(k+1<<1)>>1]|0,b[S+(k+2<<1)>>1]|0,b[S+(k+3<<1)>>1]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 169:Kb=c[Y+(e[S+(z+2<<1)>>1]<<2)>>2]|0;Lb=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;if((Kb|0)==0){f=170;break}else{f=171;break};case 170:k=ia(12,Lb|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;c[Y+(e[S+(z+2<<1)>>1]<<2)>>2]=k;f=172;break;case 171:ka(60,Kb|0,Lb|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;f=172;break;case 172:z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 173:ka(60,c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0,c[Y+(e[S+(z+2<<1)>>1]<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 174:ra(6,c[r>>2]|0,c[N>>2]|0,S|0,z|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=(e[S+(z+1<<1)>>1]|0)+2+z|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 175:Mb=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;k=c[U+(e[S+(z+4<<1)>>1]<<2)>>2]|0;Nb=(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16|0;K=(c[U+(e[S+(z+5<<1)>>1]<<2)>>2]|0)+16|0;L=c[K>>2]|0;J=c[K+4>>2]|0;Ob=io(L,J,c[Nb>>2]|0,c[Nb+4>>2]|0)|0;Pb=G;K=0;I=k+16|0;Qb=c[I>>2]|0;Rb=c[I+4>>2]|0;if((J|0)>(K|0)|(J|0)==(K|0)&L>>>0>0>>>0){f=176;break}else{f=177;break};case 176:if((Pb|0)>(Rb|0)|(Pb|0)==(Rb|0)&Ob>>>0>Qb>>>0){f=179;break}else{f=178;break};case 177:if((Pb|0)<(Rb|0)|(Pb|0)==(Rb|0)&Ob>>>0<Qb>>>0){f=179;break}else{f=178;break};case 178:L=Mb+16|0;c[L>>2]=Ob;c[L+4>>2]=Pb;c[Nb>>2]=Ob;c[Nb+4>>2]=Pb;z=z+7|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 179:z=e[S+(z+6<<1)>>1]|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 180:L=c[P>>2]|0;if((c[L+24>>2]|0)==0){f=181;break}else{Sb=L;f=182;break};case 181:ja(14,a|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;Sb=c[P>>2]|0;f=182;break;case 182:c[Sb>>2]=X;c[Sb+12>>2]=c[O>>2];c[Sb+8>>2]=z+2;c[Sb+20>>2]=c[c[A>>2]>>2];c[Sb+4>>2]=U-T>>2;c[Sb+16>>2]=c[(c[Q>>2]|0)+4>>2];L=c[P>>2]|0;c[R>>2]=L;c[P>>2]=c[L+24>>2];z=z+3|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 183:L=c[R>>2]|0;c[P>>2]=L;c[R>>2]=c[L+28>>2];z=z+1|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 184:ka(68,c[A>>2]|0,c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+3|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 185:ka(66,a|0,b[S+(z+2<<1)>>1]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+3|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 186:L=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;Tb=b[(c[L+16>>2]|0)+24>>1]|0;Ub=c[c[L+8>>2]>>2]|0;if((b[S+(z+3<<1)>>1]|0)==0){Vb=0;f=190;break}else{f=187;break};case 187:Wb=c[Ub+48>>2]|0;Xb=0;f=189;break;case 188:if((Yb|0)<(e[S+(z+3<<1)>>1]|0)){Xb=Yb;f=189;break}else{Vb=Yb;f=190;break};case 189:Yb=Xb+1|0;if((b[(c[Wb+(Xb<<2)>>2]|0)+66>>1]|0)==Tb<<16>>16){Vb=Xb;f=190;break}else{f=188;break};case 190:z=e[S+(Vb+4+z<<1)>>1]|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 191:Zb=c[(c[(c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0)+16>>2]|0)+12>>2]|0;if((b[S+(z+3<<1)>>1]|0)==0){_b=4;f=194;break}else{$b=0;f=192;break};case 192:ka(60,c[U+(e[S+($b+4+z<<1)>>1]<<2)>>2]|0,c[Zb+($b<<2)>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;L=$b+1|0;if((L|0)<(e[S+(z+3<<1)>>1]|0)){$b=L;f=192;break}else{f=193;break};case 193:_b=$b+5|0;f=194;break;case 194:z=_b+z|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 195:L=z;K=ma(6,a|0,b[S+(L+2<<1)>>1]|0,b[S+(L+3<<1)>>1]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=K;f=8;break;case 196:K=ma(8,a|0,S|0,z|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=z+5|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=K;f=8;break;case 197:K=qa(2,a|0,S+(z<<1)|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;z=(e[S+(z+2<<1)>>1]|0)+4|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=K;f=8;break;case 198:ac=c[U+(e[S+(z+2<<1)>>1]<<2)>>2]|0;bc=c[U+(e[S+(z+3<<1)>>1]<<2)>>2]|0;cc=(c[U+(e[S+(z+5<<1)>>1]<<2)>>2]|0)+16|0;K=c[cc>>2]|0;L=c[cc+4>>2]|0;if((K|0)==0&(L|0)==0){f=199;break}else{dc=L;ec=K;f=200;break};case 199:ra(8,c[A>>2]|0,5,10080,(fb=i,i=i+1|0,i=i+7&-8,c[fb>>2]=0,fb)|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=fb;dc=c[cc+4>>2]|0;ec=c[cc>>2]|0;f=200;break;case 200:K=bc+16|0;L=jo(c[K>>2]|0,c[K+4>>2]|0,ec,dc)|0;K=ac+16|0;c[K>>2]=L;c[K+4>>2]=G;c[ac>>2]=4194304;z=z+7|0;S=S;T=T;U=U;V=V;W=W;X=X;Y=Y;f=8;break;case 201:ja(28,c[A>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,j)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=d;return;case-1:if((g|0)==1){B=u;f=202}t=u=0;break}}function Dc(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=c[a+104>>2]|0;a=(c[d+4>>2]|0)+b|0;b=d+8|0;e=c[b>>2]|0;if(a>>>0>e>>>0){f=e}else{return}while(1){g=f<<1;if(a>>>0>g>>>0){f=g}else{break}}c[b>>2]=g;g=d|0;c[g>>2]=Wd(c[g>>2]|0,f<<3)|0;return}function Ec(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=b[(c[c[d+8>>2]>>2]|0)+52>>1]|0;if((e<<16>>16|0)==2){f=c[d+16>>2]|0;g=Nj(c[f+8>>2]|0,c[f+4>>2]|0,c[a+76>>2]|0)|0;h=G;i=g}else if((e<<16>>16|0)==1){g=Nj(d+16|0,8,c[a+76>>2]|0)|0;h=G;i=g}else if((e<<16>>16|0)==0){e=d+16|0;h=c[e+4>>2]|0;i=c[e>>2]|0}else{h=0;i=0}return(G=h,i)|0}function Fc(a,b){a=a|0;b=b|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;d=c[b+32>>2]|0;c[a+56>>2]=c[(c[b+44>>2]|0)+24>>2];f=d+48|0;g=e[f>>1]|0;do{if(g>>>0>(c[a+28>>2]|0)>>>0){Kc(a,g);h=c[a+4>>2]|0;c[a>>2]=h;i=h}else{h=a|0;if((c[h>>2]|0)==0){Kc(a,1);j=c[a+4>>2]|0;c[h>>2]=j;i=j;break}else{i=c[a+4>>2]|0;break}}}while(0);g=a+64|0;j=c[g>>2]|0;if((j|0)<(e[f>>1]|0|0)){h=d+52|0;k=j;while(1){l=c[i+(k<<2)>>2]|0;m=c[(c[h>>2]|0)+(k<<4)>>2]|0;c[l>>2]=1048576;c[l+8>>2]=m;m=k+1|0;if((m|0)<(e[f>>1]|0|0)){k=m}else{n=m;break}}}else{n=j}c[g>>2]=n;c[g+4>>2]=(n|0)<0|0?-1:0;if((c[(c[a+112>>2]|0)+12>>2]|0)!=0){ed(a)}if((c[a+40>>2]|0)!=(c[b+40>>2]|0)){fd(a)}c[a+24>>2]=e[f>>1]|0;b=c[a+8>>2]|0;c[b>>2]=d;c[b+12>>2]=c[d+28>>2];c[b+8>>2]=e[f>>1]|0;c[b+4>>2]=0;c[b+16>>2]=0;c[b+32>>2]=0;c[a+44>>2]=1;return}function Gc(a){a=a|0;var b=0,d=0,e=0;b=Vd(44)|0;d=b;e=a+8|0;c[b+36>>2]=c[e>>2];c[b+40>>2]=0;c[b+4>>2]=0;b=c[e>>2]|0;if((b|0)==0){c[e>>2]=d;return}c[b+40>>2]=d;c[e>>2]=d;return}function Hc(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0;d=a+52|0;e=(c[d>>2]|0)+1|0;c[d>>2]=e;d=c[a+4>>2]|0;f=a+24|0;g=c[f>>2]|0;if((g|0)!=0){h=0;i=g;while(1){j=c[d+(h<<2)>>2]|0;k=c[j+8>>2]|0;do{if((b[k+14>>1]&2)==0){l=i}else{if((c[j>>2]&1048576|0)!=0){l=i;break}if((c[(c[j+16>>2]|0)+8>>2]|0)==0){l=i;break}rb[c[(c[k>>2]|0)+84>>2]&127](e,j);l=c[f>>2]|0}}while(0);j=h+1|0;if(j>>>0<l>>>0){h=j;i=l}else{break}}}l=a+16|0;i=c[l>>2]|0;if((i|0)!=0){h=i;do{do{if((c[h+16>>2]|0)!=(e|0)){i=h+8|0;if((c[i>>2]|0)==0){break}kh(c[h>>2]|0,i)}}while(0);h=c[h+24>>2]|0;}while((h|0)!=0)}do{if((g|0)!=-1){h=c[f>>2]|0;e=a+32|0;i=c[e>>2]|0;if(h>>>0<i>>>0){m=h;n=i}else{break}while(1){i=c[d+(m<<2)>>2]|0;do{if((b[(c[i+8>>2]|0)+14>>1]&2)==0){o=n}else{h=i|0;j=c[h>>2]|0;if((j&1048576|0)!=0){o=n;break}k=c[(c[i+16>>2]|0)+8>>2]|0;if((k|0)==0){o=n;break}if((c[k+16>>2]|0)!=-1){o=n;break}c[h>>2]=j|1048576;o=c[e>>2]|0}}while(0);i=m+1|0;if(i>>>0<o>>>0){m=i;n=o}else{break}}}}while(0);o=a+20|0;n=c[o>>2]|0;m=c[l>>2]|0;if((m|0)==0){p=0;q=0;r=n;s=a+48|0;b[s>>1]=p;c[l>>2]=q;c[o>>2]=r;return}else{t=m;u=0;v=0;w=n}while(1){n=t+24|0;m=c[n>>2]|0;x=u+1|0;if((c[t+16>>2]|0)==-1){Ln(c[t+8>>2]|0);y=t;z=v;A=w}else{y=w;z=t;A=v}c[n>>2]=A;if((m|0)==0){break}else{t=m;u=x;v=z;w=y}}p=x&65535;q=z;r=y;s=a+48|0;b[s>>1]=p;c[l>>2]=q;c[o>>2]=r;return}function Ic(a){a=a|0;var b=0,d=0,e=0;b=c[a+16>>2]|0;if((b|0)!=0){d=b;while(1){b=c[d+24>>2]|0;Ln(d);if((b|0)==0){break}else{d=b}}}d=c[a+20>>2]|0;if((d|0)==0){return}else{e=d}while(1){d=c[e+24>>2]|0;Ln(e);if((d|0)==0){break}else{e=d}}return}function Jc(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;b=i;i=i+24|0;d=b+16|0;e=c[(c[(c[a+112>>2]|0)+52>>2]|0)+24>>2]|0;f=Xd()|0;g=a+44|0;h=f+12|0;c[h>>2]=Vd(c[g>>2]<<2)|0;j=f+16|0;c[j>>2]=-1;k=c[g>>2]|0;if((k|0)<=0){l=k;c[j>>2]=l;i=b;return f|0}m=b|0;n=d;o=k;k=a+8|0;while(1){a=c[k>>2]|0;p=c[a>>2]|0;fo(m|0,0,16)|0;q=c[p+16>>2]|0;if((c[p+28>>2]|0)==0){r=20152}else{s=c[(c[p+20>>2]|0)+12>>2]|0;Qa(m|0,20784,(t=i,i=i+8|0,c[t>>2]=c[a+20>>2],t)|0)|0;i=t;r=s}s=c[p+12>>2]|0;p=(s|0)==0;u=p?21168:s;s=_n(u|0)|0;v=_n(r|0)|0;w=s+15+v+(_n(m|0)|0)|0;v=Vd(12)|0;s=Vd(w)|0;w=Qa(s|0,18440,(t=i,i=i+40|0,c[t>>2]=r,c[t+8>>2]=m,c[t+16>>2]=u,c[t+24>>2]=p?21168:18992,c[t+32>>2]=q,t)|0)|0;i=t;c[v>>2]=1;c[v+4>>2]=w;c[v+8>>2]=s;c[n>>2]=v;v=eh(0,0,e,d)|0;s=o-1|0;c[(c[h>>2]|0)+(s<<2)>>2]=v;if((s|0)>0){o=s;k=a+36|0}else{break}}l=c[g>>2]|0;c[j>>2]=l;i=b;return f|0}function Kc(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;d=i;i=i+8|0;e=d|0;f=b+2|0;b=c[a+56>>2]|0;g=a+32|0;h=c[g>>2]|0;j=a|0;k=a+4|0;l=c[k>>2]|0;m=(c[j>>2]|0)-l|0;n=(h|0)==0?1:h;while(1){o=n<<1;if((o|0)<(f|0)){n=o}else{break}}f=Wd(l,n<<3)|0;c[k>>2]=f;c[j>>2]=f+(m>>2<<2);if((h|0)>=(o|0)){c[g>>2]=o;p=o-2|0;q=a+28|0;c[q>>2]=p;i=d;return}m=e|0;j=h;do{c[m>>2]=0;c[m+4>>2]=0;c[f+(j<<2)>>2]=eh(5242880,0,b,e)|0;j=j+1|0;}while((j|0)<(o|0));c[g>>2]=o;p=o-2|0;q=a+28|0;c[q>>2]=p;i=d;return}function Lc(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0;if((b[d+46>>1]|0)!=0){gd(a,d,f);return}g=c[a+4>>2]|0;h=c[a+24>>2]|0;a=c[d+52>>2]|0;i=d+48|0;if((e[i>>1]|0)>(f|0)){j=f}else{return}do{f=c[a+(j<<4)>>2]|0;d=c[g+(j+h<<2)>>2]|0;ch(d);c[d>>2]=1048576;c[d+8>>2]=f;j=j+1|0;}while((j|0)<(e[i>>1]|0));return}function Mc(a){a=a|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0;d=a+116|0;f=c[d>>2]|0;g=c[f+8>>2]|0;h=a+80|0;i=c[h>>2]|0;if((i|0)==0){j=0;return j|0}k=c[f>>2]|0;do{if((g|0)==0){l=De(f)|0;m=c[(Ye(c[a+96>>2]|0,0,l)|0)+24>>2]|0;l=c[h>>2]|0;if((l|0)==0){j=0}else{n=l;o=m;break}return j|0}else{n=i;o=g}}while(0);g=a+4|0;i=a+108|0;f=n;a:while(1){if((c[f+20>>2]|0)!=(k|0)){p=6;break}q=f|0;r=c[(c[c[q>>2]>>2]|0)+28>>2]|0;n=c[g>>2]|0;m=c[f+4>>2]|0;s=n+(m<<2)|0;l=b[r+(c[f+8>>2]<<1)>>1]|0;while(1){t=l&65535;if(l<<16>>16==0){break}u=b[r+(t+2<<1)>>1]|0;v=c[n+((e[r+(t+4<<1)>>1]|0)+m<<2)>>2]|0;if((sh(c[i>>2]|0,c[v+8>>2]|0,o)|0)==0){l=u}else{p=10;break a}}l=c[f+28>>2]|0;if((l|0)==0){j=0;p=18;break}else{f=l}}if((p|0)==6){c[h>>2]=f;j=0;return j|0}else if((p|0)==10){i=t+5|0;do{if((b[r+(t+3<<1)>>1]|0)!=0){g=c[(c[d>>2]|0)+12>>2]|0;if((g|0)==0){id(a,o,v);break}else{hd(a,v,g);break}}}while(0);c[(c[d>>2]|0)+12>>2]=0;d=a+8|0;c[d>>2]=c[q>>2];c[a+44>>2]=c[f+12>>2];c[(c[a+104>>2]|0)+4>>2]=c[f+16>>2];c[a>>2]=s;c[(c[d>>2]|0)+16>>2]=i;i=c[f+28>>2]|0;c[h>>2]=i;h=a+84|0;if((i|0)==0){c[h>>2]=f;j=1;return j|0}else{c[h>>2]=i;j=1;return j|0}}else if((p|0)==18){return j|0}return 0}function Nc(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0;e=i;i=i+8|0;f=e|0;g=c[(c[c[b+8>>2]>>2]|0)+88>>2]|0;c[f>>2]=0;h=wb[g&31](a,f,b,d)|0;i=e;return h|0}function Oc(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;g=a+24|0;h=c[g>>2]|0;i=d+48|0;j=b[i>>1]|0;k=(j&65535)+h|0;l=c[a>>2]|0;m=c[a+4>>2]|0;n=f+8|0;if((b[n>>1]|0)==0){o=0;p=j}else{j=0;do{q=c[l+(e[f+(j+6<<1)>>1]<<2)>>2]|0;r=c[m+(j+h<<2)>>2]|0;if((c[q>>2]&7340032|0)==0){s=c[q+16>>2]|0;c[s>>2]=(c[s>>2]|0)+1}if((c[r>>2]&7340032|0)==0){ch(r)}s=r;r=q;c[s>>2]=c[r>>2];c[s+4>>2]=c[r+4>>2];c[s+8>>2]=c[r+8>>2];c[s+12>>2]=c[r+12>>2];c[s+16>>2]=c[r+16>>2];c[s+20>>2]=c[r+20>>2];j=j+1|0;}while((j|0)<(e[n>>1]|0));o=j;p=b[i>>1]|0}if((o|0)==(p&65535|0)){c[g>>2]=k;return}Lc(a,d,o);c[g>>2]=k;return}function Pc(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0;f=i;i=i+8|0;g=f|0;h=c[e+8>>2]|0;do{if((b[(c[h>>2]|0)+52>>1]|0)==6){if((c[d+8>>2]|0)!=(h|0)){break}fh(d,e);i=f;return}}while(0);if((c[e>>2]&7340032|0)==0){h=c[e+16>>2]|0;c[h>>2]=(c[h>>2]|0)+1}h=Zg()|0;j=c[d+8>>2]|0;if((b[j+14>>1]&2)!=0){vc(a,j,h)}j=c[h+12>>2]|0;a=e;c[j>>2]=c[a>>2];c[j+4>>2]=c[a+4>>2];c[j+8>>2]=c[a+8>>2];c[j+12>>2]=c[a+12>>2];c[j+16>>2]=c[a+16>>2];c[j+20>>2]=c[a+20>>2];c[g>>2]=h;gh(d,g);i=f;return}function Qc(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0;g=c[a>>2]|0;h=c[g+((e[d+(f+2<<1)>>1]|0)<<2)>>2]|0;i=c[g+((e[d+(f+3<<1)>>1]|0)<<2)>>2]|0;j=c[g+((e[d+(f+4<<1)>>1]|0)<<2)>>2]|0;d=b[(c[c[h+8>>2]>>2]|0)+52>>1]|0;if((d<<16>>16|0)==2){lc(a,h,i,j);return}else if((d<<16>>16|0)==8){d=Oi(a,c[h+16>>2]|0,i)|0;if((d|0)==0){kd(a,f,i)}fh(j,c[d+12>>2]|0);return}else{d=c[h+16>>2]|0;h=c[i+16>>2]|0;i=c[d+16>>2]|0;do{if((h|0)<0){f=i+h|0;if((f|0)>=0){k=f;break}jd(c[a+116>>2]|0,h);k=f}else{if(h>>>0<i>>>0){k=h;break}jd(c[a+116>>2]|0,h);k=h}}while(0);fh(j,c[(c[d+12>>2]|0)+(k<<2)>>2]|0);return}}function Rc(a,b,d){a=a|0;b=b|0;d=d|0;fh(c[a+((e[b+(d+4<<1)>>1]|0)<<2)>>2]|0,c[(c[(c[(c[a+((e[b+(d+2<<1)>>1]|0)<<2)>>2]|0)+16>>2]|0)+12>>2]|0)+((e[b+(d+3<<1)>>1]|0)<<2)>>2]|0);return}function Sc(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0;g=c[a>>2]|0;h=c[g+(e[d+(f+2<<1)>>1]<<2)>>2]|0;i=c[g+(e[d+(f+3<<1)>>1]<<2)>>2]|0;j=c[g+(e[d+(f+4<<1)>>1]<<2)>>2]|0;f=h+16|0;if((b[(c[c[h+8>>2]>>2]|0)+52>>1]|0)==8){Qi(a,c[f>>2]|0,i,j);return}h=c[f>>2]|0;f=c[i+16>>2]|0;i=c[h+16>>2]|0;do{if((f|0)<0){d=i+f|0;if((d|0)>=0){k=d;break}jd(c[a+116>>2]|0,f);k=d}else{if(f>>>0<i>>>0){k=f;break}jd(c[a+116>>2]|0,f);k=f}}while(0);fh(c[(c[h+12>>2]|0)+(k<<2)>>2]|0,j);return}function Tc(a,b,d){a=a|0;b=b|0;d=d|0;fh(c[(c[(c[(c[a+((e[b+(d+2<<1)>>1]|0)<<2)>>2]|0)+16>>2]|0)+12>>2]|0)+((e[b+(d+3<<1)>>1]|0)<<2)>>2]|0,c[a+((e[b+(d+4<<1)>>1]|0)<<2)>>2]|0);return}function Uc(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0;g=c[a>>2]|0;h=b[d+(f+2<<1)>>1]|0;i=h&65535;j=f+3|0;f=c[g+((e[d+(i+j<<1)>>1]|0)<<2)>>2]|0;k=Ni()|0;l=c[f+8>>2]|0;if((b[l+14>>1]&2)!=0){vc(a,l,k)}ch(f);c[f+16>>2]=k;c[f>>2]=0;if(h<<16>>16==0){return}else{m=0}do{h=m+j|0;Qi(a,k,c[g+((e[d+(h<<1)>>1]|0)<<2)>>2]|0,c[g+((e[d+(h+1<<1)>>1]|0)<<2)>>2]|0);m=m+2|0;}while((m|0)<(i|0));return}function Vc(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=i;i=i+8|0;g=f|0;h=c[a>>2]|0;j=b[d+4>>1]|0;k=j&65535;l=c[h+((e[d+(k+3<<1)>>1]|0)<<2)>>2]|0;m=Xd()|0;n=Vd(k<<2)|0;c[m+16>>2]=k;c[m+12>>2]=n;o=c[l+8>>2]|0;if((b[o+14>>1]&2)!=0){vc(a,o,m)}c[g>>2]=m;gh(l,g);if(j<<16>>16==0){i=f;return}else{p=0}do{c[n+(p<<2)>>2]=hh(c[h+((e[d+(p+3<<1)>>1]|0)<<2)>>2]|0)|0;p=p+1|0;}while((p|0)<(k|0));i=f;return}function Wc(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;f=i;i=i+8|0;g=f|0;h=c[a>>2]|0;j=b[d+4>>1]|0;k=b[d+6>>1]|0;l=k&65535;m=c[h+((e[d+(l+4<<1)>>1]|0)<<2)>>2]|0;n=m+8|0;o=e[(c[c[n>>2]>>2]|0)+66>>1]|0;p=ih()|0;q=Vd(o<<2)|0;c[p+16>>2]=l;c[p+12>>2]=q;b[p+24>>1]=j;j=c[n>>2]|0;if((b[j+14>>1]&2)!=0){vc(a,j,p)}c[g>>2]=p;gh(m,g);if(k<<16>>16==0){i=f;return}else{r=0}do{c[q+(r<<2)>>2]=hh(c[h+((e[d+(r+4<<1)>>1]|0)<<2)>>2]|0)|0;r=r+1|0;}while((r|0)<(l|0));i=f;return}function Xc(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0;f=i;i=i+8|0;g=f|0;h=c[a>>2]|0;j=c[h+((b&65535)<<2)>>2]|0;b=c[(c[a+12>>2]|0)+((d&65535)<<2)>>2]|0;d=c[h+((e&65535)<<2)>>2]|0;e=rj(c[b+24>>2]|0)|0;h=e|0;c[h>>2]=1;vc(a,c[b+8>>2]|0,e);b=c[j+16>>2]|0;ld(e,c[b+36>>2]|0,c[b+40>>2]|0);c[g>>2]=e;gh(d,g);c[h>>2]=(c[h>>2]|0)+1;i=f;return}function Yc(a){a=a|0;var b=0,d=0,e=0;b=Vd(24)|0;d=b;e=a;c[b>>2]=c[e>>2];c[b+4>>2]=c[e+4>>2];c[b+8>>2]=c[e+8>>2];c[b+12>>2]=c[e+12>>2];c[b+16>>2]=c[e+16>>2];c[b+20>>2]=c[e+20>>2];c[b+4>>2]=1;if((c[a>>2]&7340032|0)!=0){return d|0}b=c[a+16>>2]|0;c[b>>2]=(c[b>>2]|0)+1;return d|0}function Zc(a,d,f,g){a=a|0;d=d|0;f=f|0;g=g|0;var h=0,i=0,j=0,k=0,l=0,m=0,n=0;h=g+1|0;g=b[f+(h<<1)>>1]|0;i=h+(g&65535)|0;h=(g&65535)>>>1;g=h&65535;j=i-g|0;if(h<<16>>16==0){return}else{k=i}while(1){i=c[a+((e[f+(k<<1)>>1]|0)<<2)>>2]|0;h=i|0;if((c[h>>2]&1048576|0)==0){l=4;break}m=c[d+((e[f+(k-g<<1)>>1]|0)<<2)>>2]|0;c[h>>2]=c[m>>2];h=m+24|0;m=c[h+4>>2]|0;n=i+16|0;c[n>>2]=c[h>>2];c[n+4>>2]=m;m=k-1|0;if((m|0)>(j|0)){k=m}else{l=4;break}}if((l|0)==4){return}}function _c(a){a=a|0;var b=0,d=0;b=Vd(32)|0;d=a+84|0;c[(c[d>>2]|0)+24>>2]=b;c[b+24>>2]=0;c[b+28>>2]=c[d>>2];return}function $c(a,b){a=a|0;b=b|0;Ce(a,b,c[(c[(c[c[(c[b+16>>2]|0)+12>>2]>>2]|0)+16>>2]|0)+8>>2]|0);return}function ad(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0;f=i;i=i+8|0;g=f|0;h=c[(c[a>>2]|0)+((d&65535)<<2)>>2]|0;d=h+8|0;j=c[c[d>>2]>>2]|0;k=c[j+60>>2]|0;l=a+8|0;m=(c[(c[l>>2]|0)+36>>2]|0)+32|0;n=c[m>>2]|0;do{if((n|0)!=0){if((e[(c[c[n+24>>2]>>2]|0)+52>>1]|0)>>>0<=(e[j+52>>1]|0)>>>0){break}c[h+16>>2]=n;o=n|0;c[o>>2]=(c[o>>2]|0)+1;c[h>>2]=0;c[(c[l>>2]|0)+32>>2]=c[m>>2];i=f;return}}while(0);m=Vd(28)|0;n=m;o=Vd(k<<2)|0;p=m+16|0;c[p>>2]=-1;c[m>>2]=1;q=m+12|0;c[q>>2]=o;c[m+8>>2]=0;c[m+20>>2]=0;c[m+24>>2]=c[d>>2];o=c[d>>2]|0;if((b[o+14>>1]&2)!=0){vc(a,o,m)}ch(h);c[h+16>>2]=n;c[h>>2]=0;if((k|0)>0){h=g|0;m=a+108|0;a=j;o=j+44|0;j=k;while(1){r=j-1|0;s=c[o>>2]|0;if((s|0)==0){t=a;while(1){u=c[t+36>>2]|0;v=c[u+44>>2]|0;if((v|0)==0){t=u}else{w=u;x=v;break}}}else{w=a;x=s}t=c[x+8>>2]|0;if((b[t+14>>1]&16)==0){y=t}else{y=uh(c[m>>2]|0,c[d>>2]|0,t)|0}c[h>>2]=0;c[h+4>>2]=0;t=eh(1048576,0,y,g)|0;c[(c[q>>2]|0)+(r<<2)>>2]=t;if((r|0)>0){a=w;o=x+36|0;j=r}else{break}}}c[p>>2]=k;c[(c[l>>2]|0)+32>>2]=n;i=f;return}function bd(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,j=0;e=i;i=i+8|0;f=e|0;g=b&65535;h=c[(c[a>>2]|0)+((d&65535)<<2)>>2]|0;d=rj(c[c[a+8>>2]>>2]|0)|0;vc(a,c[h+8>>2]|0,d);a=Vd(g<<2)|0;j=a;if(b<<16>>16!=0){fo(a|0,0,((b&65535)>>>0>1>>>0?g<<2:4)|0)|0}c[d+36>>2]=g;c[d+40>>2]=j;c[d>>2]=1;c[f>>2]=d;gh(h,f);i=e;return j|0}function cd(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0;f=i;i=i+8|0;g=f|0;h=a|0;Rc(c[h>>2]|0,b,d);a=c[(c[h>>2]|0)+((e[b+(d+4<<1)>>1]|0)<<2)>>2]|0;d=c[a+16>>2]|0;b=rj(d)|0;c[b>>2]=1;ld(b,c[d+36>>2]|0,c[d+40>>2]|0);c[g>>2]=b;gh(a,g);i=f;return c[b+40>>2]|0}function dd(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;f=i;i=i+8|0;g=f|0;h=c[c[a+8>>2]>>2]|0;j=h+40|0;k=c[j>>2]|0;l=b[d+4>>1]|0;m=l&65535;n=d+6|0;o=e[n>>1]|0;if(l<<16>>16==0){p=o}else{l=0;q=n;n=o;while(1){o=c[k+(n<<2)>>2]|0;if((o|0)!=0){r=o+4|0;s=(c[r>>2]|0)-1|0;c[r>>2]=s;if((s|0)==0){ch(o);Ln(o)}c[k+((e[q>>1]|0)<<2)>>2]=0}o=l+1|0;s=d+(l+4<<1)|0;r=e[s>>1]|0;if((o|0)<(m|0)){l=o;q=s;n=r}else{p=r;break}}}n=c[(c[a>>2]|0)+(p<<2)>>2]|0;c[g>>2]=h;gh(n,g);g=h|0;c[g>>2]=(c[g>>2]|0)+1;i=f;return c[j>>2]|0}function ed(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0;b=a+112|0;d=c[b>>2]|0;e=c[d+12>>2]|0;f=c[a+4>>2]|0;if((e|0)==0){g=d;h=g+12|0;c[h>>2]=0;return}else{i=e}while(1){e=c[f+(c[i+12>>2]<<2)>>2]|0;d=i+8|0;c[e+8>>2]=c[d>>2];a=i+24|0;j=c[a+4>>2]|0;k=e+16|0;c[k>>2]=c[a>>2];c[k+4>>2]=j;c[e>>2]=c[c[c[d>>2]>>2]>>2]&4194304;d=c[i+32>>2]|0;Ln(i);if((d|0)==0){break}else{i=d}}g=c[b>>2]|0;h=g+12|0;c[h>>2]=0;return}function fd(a){a=a|0;var b=0,d=0,e=0,f=0,g=0;b=c[a+112>>2]|0;d=a+40|0;e=b+40|0;f=c[e>>2]|0;if((c[d>>2]|0)==(f|0)){return}g=a+12|0;a=Wd(c[g>>2]|0,f<<2)|0;f=c[d>>2]|0;md(a,c[b+4>>2]|0,f);md(a,c[b+8>>2]|0,f);c[d>>2]=c[e>>2];c[g>>2]=a;return}function gd(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0;g=a+108|0;h=Ah(c[g>>2]|0)|0;i=c[a+4>>2]|0;j=c[a+24>>2]|0;k=c[d+52>>2]|0;if((f|0)>0){l=0;while(1){ph(c[g>>2]|0,c[k+(l<<4)>>2]|0,c[(c[i+(l+j<<2)>>2]|0)+8>>2]|0);m=l+1|0;if((m|0)<(f|0)){l=m}else{n=f;break}}}else{n=0}f=b[d+44>>1]|0;do{if(f<<16>>16!=0){l=c[a+92>>2]|0;m=c[c[l+4>>2]>>2]|0;o=c[c[l>>2]>>2]|0;l=(f&65535)-1|0;p=c[m+(l<<2)>>2]|0;if((p|0)==0){break}q=d+40|0;r=l;l=p;do{ph(c[g>>2]|0,l,c[(c[(c[q>>2]|0)+((e[o+(r<<1)>>1]|0)<<2)>>2]|0)+8>>2]|0);r=r+1|0;l=c[m+(r<<2)>>2]|0;}while((l|0)!=0)}}while(0);f=e[d+48>>1]|0;if((n|0)<(f|0)){s=n}else{t=c[g>>2]|0;Bh(t,h);return}do{n=c[k+(s<<4)>>2]|0;if((b[n+14>>1]&16)==0){u=n}else{u=oh(c[g>>2]|0,n)|0}n=c[i+(s+j<<2)>>2]|0;ch(n);c[n>>2]=c[c[u>>2]>>2]&4194304|1048576;c[n+8>>2]=u;s=s+1|0;}while((s|0)<(f|0));t=c[g>>2]|0;Bh(t,h);return}function hd(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0;e=i;i=i+8|0;f=e|0;fh(b,d);d=Jc(a)|0;a=c[b+16>>2]|0;c[f>>2]=d;gh(c[(c[a+12>>2]|0)+4>>2]|0,f);i=e;return}function id(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0;e=i;i=i+8|0;f=e|0;g=Vd(28)|0;h=g+12|0;c[h>>2]=Vd(8)|0;j=g+16|0;c[j>>2]=-1;c[g+20>>2]=0;c[g>>2]=1;c[g+8>>2]=0;c[g+24>>2]=b;b=a+116|0;k=nm(c[a+112>>2]|0,c[c[(c[b>>2]|0)+4>>2]>>2]|0)|0;qn(c[(c[b>>2]|0)+4>>2]|0);c[c[h>>2]>>2]=k;c[j>>2]=1;k=nd(a)|0;c[(c[h>>2]|0)+4>>2]=k;c[j>>2]=2;c[f>>2]=g;gh(d,f);i=e;return}function jd(a,b){a=a|0;b=b|0;var d=0;d=i;Ae(a,3,9048,(a=i,i=i+8|0,c[a>>2]=b,a)|0);i=a;i=d;return}function kd(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0;f=i;g=c[a+8>>2]|0;c[g+20>>2]=e[(c[g+12>>2]|0)+(b+1<<1)>>1]|0;Ae(c[a+116>>2]|0,7,8072,(a=i,i=i+8|0,c[a>>2]=d,a)|0);i=a;i=f;return}function ld(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0;e=Vd(b<<2)|0;if((b|0)>0){f=0;do{g=c[d+(f<<2)>>2]|0;if((g|0)!=0){h=g+4|0;c[h>>2]=(c[h>>2]|0)+1}c[e+(f<<2)>>2]=g;f=f+1|0;}while((f|0)<(b|0))}c[a+40>>2]=e;c[a+36>>2]=b;return}function md(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0;if((b|0)==0){return}else{e=b}while(1){b=c[e+12>>2]|0;if(b>>>0<d>>>0){f=4;break}c[a+(b<<2)>>2]=e;b=c[e+32>>2]|0;if((b|0)==0){f=4;break}else{e=b}}if((f|0)==4){return}}function nd(a){a=a|0;var b=0,d=0,e=0,f=0;b=i;i=i+8|0;d=b|0;e=Jc(a)|0;f=c[a+88>>2]|0;c[d>>2]=e;e=eh(0,0,f,d)|0;i=b;return e|0}function od(){var a=0,b=0,d=0,e=0;a=Vd(44)|0;fo(a|0,0,40)|0;b=Vd(44)|0;c[b+40>>2]=0;d=Vd(44)|0;c[d+40>>2]=b;b=Vd(44)|0;c[b+40>>2]=d;d=Vd(44)|0;e=d;c[d+40>>2]=b;b=a;c[a>>2]=e;c[a+4>>2]=e;c[a+8>>2]=e;c[a+20>>2]=He()|0;Nd(b);return b|0}function pd(a){a=a|0;var b=0,d=0,e=0,f=0,g=0;b=c[a>>2]|0;if((b|0)!=0){d=b;while(1){b=c[d+40>>2]|0;Ln(d);if((b|0)==0){break}else{d=b}}}d=c[a+24>>2]|0;if((d|0)!=0){b=d;while(1){d=c[b+16>>2]|0;if((d|0)==0){e=b;break}else{b=d}}while(1){b=c[e+12>>2]|0;Ln(e);if((b|0)==0){break}else{e=b}}}e=c[a+36>>2]|0;if((e|0)!=0){b=e;while(1){e=c[b+24>>2]|0;if((e|0)==0){f=b;break}else{b=e}}while(1){b=c[f+20>>2]|0;Ln(f);if((b|0)==0){break}else{f=b}}}f=c[a+20>>2]|0;if((f|0)==0){g=a;Ln(g);return}Ie(f);g=a;Ln(g);return}function qd(a){a=a|0;c[a+12>>2]=0;c[a+16>>2]=0;Le(c[a+20>>2]|0,c[a+28>>2]|0);c[a+8>>2]=c[a+4>>2];return}function rd(a){a=a|0;var b=0;b=a+12|0;Od(c[(c[a+24>>2]|0)+8>>2]|0,c[b>>2]|0);c[b>>2]=0;c[a+16>>2]=0;return}function sd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|d&65535;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];c[f+16>>2]=0;c[f+32>>2]=0;b[f+14>>1]=0;c[f+24>>2]=0;b[f+12>>1]=1;c[f>>2]=0;Qd(a,f);d=a+24|0;i=c[d>>2]|0;if((c[i+8>>2]|0)==0){j=i}else{e=c[i+12>>2]|0;if((e|0)==0){Nd(a);k=c[(c[d>>2]|0)+12>>2]|0}else{k=e}c[d>>2]=k;j=k}k=a+12|0;c[j+4>>2]=c[k>>2];d=a+16|0;c[j>>2]=c[d>>2];c[j+8>>2]=f;f=a+32|0;c[f>>2]=(c[f>>2]|0)+1;c[k>>2]=0;c[d>>2]=0;return}function td(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0;b=a+24|0;d=c[b>>2]|0;e=a+12|0;Od(c[d+8>>2]|0,c[e>>2]|0);c[e>>2]=c[d+4>>2];c[a+16>>2]=c[d>>2];d=c[b>>2]|0;e=c[d+16>>2]|0;if((e|0)==0){c[d+8>>2]=0;f=a+32|0;g=c[f>>2]|0;h=g-1|0;c[f>>2]=h;return}else{c[b>>2]=e;f=a+32|0;g=c[f>>2]|0;h=g-1|0;c[f>>2]=h;return}}function ud(a){a=a|0;return c[(c[a+24>>2]|0)+8>>2]|0}function vd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|22;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;e=f+32|0;c[e>>2]=0;h=Rd(d)|0;c[f+28>>2]=h;c[i>>2]=d<<16|22;d=f+20|0;c[d>>2]=0;c[f+24>>2]=0;i=a+16|0;g=c[i>>2]|0;j=c[g+4>>2]&65535;if(j>>>0<22>>>0){k=a+12|0;if((c[k>>2]|0)==(g|0)){c[k>>2]=f}c[g+32>>2]=f;c[d>>2]=g;c[i>>2]=f;return}if((j|0)!=22){return}if((h|0)>(c[g+28>>2]|0)|(h|0)==0){j=g+24|0;c[d>>2]=c[j>>2];c[j>>2]=f;c[e>>2]=g;c[i>>2]=f;return}else{l=g}while(1){m=l+32|0;n=c[m>>2]|0;if((n|0)==0){o=16;break}if((h|0)>(c[n+28>>2]|0)){o=12;break}else{l=n}}if((o|0)==12){h=n+20|0;if((c[h>>2]|0)==(l|0)){c[h>>2]=f}else{c[n+24>>2]=f}c[e>>2]=c[g+32>>2]}else if((o|0)==16){c[m>>2]=f;c[a+12>>2]=f}c[d>>2]=l;c[i>>2]=f;return}function wd(a,b){a=a|0;b=b|0;sd(a,11);Sd(a,b);rd(a);return}function xd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|9;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f+20>>2]=0;c[f+28>>2]=Rd(d)|0;c[i>>2]=d<<16|9;Qd(a,f);return}function yd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|5;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f>>2]=d;c[f+20>>2]=d;Qd(a,f);return}function zd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|7;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f>>2]=d;c[f+20>>2]=d;Qd(a,f);return}function Ad(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|21;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f+20>>2]=d;Qd(a,f);return}function Bd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|6;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f>>2]=d;c[f+20>>2]=d;Qd(a,f);return}function Cd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|18;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f>>2]=d;c[f+20>>2]=d;Qd(a,f);return}function Dd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|19;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f>>2]=d;c[f+20>>2]=d;Qd(a,f);return}function Ed(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|17;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f>>2]=d;c[f+20>>2]=d;Qd(a,f);return}function Fd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|16;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f>>2]=d;c[f+20>>2]=d;Qd(a,f);return}function Gd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|8;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];i=f+16|0;c[i>>2]=0;c[f+32>>2]=0;b[f+14>>1]=0;c[f+24>>2]=0;b[f+12>>1]=1;e=f|0;c[e>>2]=0;c[i>>2]=Je(c[a+20>>2]|0,d)|0;c[e>>2]=0;Qd(a,f);return}function Hd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|13;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f+20>>2]=d;Qd(a,f);return}function Id(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|14;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f+20>>2]=d;Qd(a,f);return}function Jd(a){a=a|0;var d=0,e=0,f=0,g=0,h=0;d=a+8|0;e=c[d>>2]|0;f=e+40|0;g=c[f>>2]|0;if((g|0)==0){Pd(a);h=c[f>>2]|0}else{h=g}c[d>>2]=h;h=e+4|0;c[h>>2]=c[h>>2]&-65536|20;c[e+36>>2]=0;c[e+8>>2]=c[c[a+40>>2]>>2];b[e+12>>1]=1;c[e+16>>2]=0;c[e+32>>2]=0;Qd(a,e);return}function Kd(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0;f=a+8|0;g=c[f>>2]|0;h=g+40|0;i=c[h>>2]|0;if((i|0)==0){Pd(a);j=c[h>>2]|0}else{j=i}c[f>>2]=j;j=g+4|0;c[j>>2]=c[j>>2]&-65536|15;c[g+36>>2]=0;j=g+8|0;c[j>>2]=c[c[a+40>>2]>>2];b[g+12>>1]=1;f=g+16|0;c[f>>2]=0;c[g+32>>2]=0;c[f>>2]=Je(c[a+20>>2]|0,e)|0;c[j>>2]=d;Qd(a,g);return}function Ld(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=a+36|0;e=c[d>>2]|0;do{if((e|0)==0){f=4}else{g=c[e+20>>2]|0;h=(b[e+6>>1]|0)==0;if((g|0)==0){if(h){i=e;break}else{f=4;break}}else{i=h?e:g;break}}}while(0);if((f|0)==4){f=Vd(28)|0;e=f;g=c[d>>2]|0;if((g|0)==0){j=0}else{c[g+20>>2]=e;j=c[d>>2]|0}c[f+24>>2]=j;c[f+20>>2]=0;i=e}e=a+12|0;c[i+12>>2]=c[e>>2];f=a+16|0;c[i+16>>2]=c[f>>2];j=a+32|0;c[i+8>>2]=c[j>>2];g=a+28|0;b[i+4>>1]=c[g>>2];h=a+4|0;c[i>>2]=c[h>>2];b[i+6>>1]=1;c[f>>2]=0;c[e>>2]=0;c[g>>2]=c[(c[a+20>>2]|0)+4>>2];c[j>>2]=0;c[h>>2]=c[a+8>>2];c[d>>2]=i;return}function Md(a){a=a|0;var d=0;d=c[a+36>>2]|0;c[a+12>>2]=c[d+12>>2];c[a+16>>2]=c[d+16>>2];c[a+28>>2]=e[d+4>>1]|0;c[a+32>>2]=c[d+8>>2];c[a+4>>2]=c[d>>2];b[d+6>>1]=0;return}function Nd(a){a=a|0;var b=0,d=0,e=0;b=Vd(20)|0;d=b;e=a+24|0;a=c[e>>2]|0;if((a|0)==0){c[e>>2]=d;c[b+16>>2]=0;fo(b|0,0,16)|0;return}else{c[a+12>>2]=d;c[b+16>>2]=c[e>>2];fo(b|0,0,16)|0;return}}function Od(a,d){a=a|0;d=d|0;var e=0,f=0,g=0;if((d|0)==0){return}e=a+24|0;f=c[e>>2]|0;if((f|0)==0){c[e>>2]=d}else{e=f;do{g=e+36|0;e=c[g>>2]|0;}while((e|0)!=0);c[g>>2]=d}c[d+32>>2]=a;c[d+36>>2]=0;d=a+14|0;b[d>>1]=(b[d>>1]|0)+1;return}function Pd(a){a=a|0;var b=0;b=Vd(44)|0;c[b+40>>2]=0;c[(c[a+8>>2]|0)+40>>2]=b;return}function Qd(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=a+16|0;e=c[d>>2]|0;if((e|0)==0){c[a+12>>2]=b;c[d>>2]=b;return}d=c[e+4>>2]&65535;if((d|0)==9){Td(a,e,b);return}else if((d|0)==22){d=e+24|0;f=c[d>>2]|0;if((f|0)==0){c[d>>2]=b;c[b+32>>2]=e;return}if((c[f+4>>2]&65535|0)==9){Td(a,f,b);return}else{Ud(a,f,b);c[d>>2]=b;c[b+32>>2]=e;return}}else{Ud(a,e,b);return}}function Rd(a){a=a|0;var b=0;switch(a|0){case 19:{b=1;break};case 20:{b=5;break};case 13:{b=8;break};case 11:case 12:{b=9;break};case 18:{b=2;break};case 14:{b=6;break};case 2:case 7:{b=3;break};case 21:case 26:case 25:case 22:case 23:case 27:case 28:{b=0;break};case 0:case 1:{b=10;break};case 3:case 5:case 4:case 6:{b=4;break};case 9:case 10:case 8:{b=11;break};case 16:case 17:{b=12;break};case 15:{b=7;break};default:{b=-1}}return b|0}function Sd(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=a+8|0;f=c[e>>2]|0;g=f+40|0;h=c[g>>2]|0;if((h|0)==0){Pd(a);i=c[g>>2]|0}else{i=h}c[e>>2]=i;i=f+4|0;c[i>>2]=c[i>>2]&-65536|10;c[f+36>>2]=0;c[f+8>>2]=c[c[a+40>>2]>>2];b[f+12>>1]=1;c[f+16>>2]=0;c[f+32>>2]=0;c[f+28>>2]=d;Qd(a,f);return}function Td(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0;e=b;f=c[b+4>>2]|0;while(1){b=e+20|0;g=c[b>>2]|0;if((f&65535|0)!=9){h=6;break}if((g|0)==0){h=4;break}i=c[g+4>>2]|0;if((i&65535|0)==9){e=g;f=i}else{j=b;h=8;break}}if((h|0)==4){k=e+20|0;h=7}else if((h|0)==6){f=e+20|0;if((g|0)==0){k=f;h=7}else{j=f;h=8}}if((h|0)==7){c[k>>2]=d;l=d+32|0;c[l>>2]=e;return}else if((h|0)==8){Ud(a,g,d);c[j>>2]=d;l=d+32|0;c[l>>2]=e;return}}function Ud(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0;f=a+16|0;do{if((c[f>>2]|0)==(d|0)){c[f>>2]=e;g=a+12|0;if((c[g>>2]|0)!=(d|0)){break}c[g>>2]=e}}while(0);c[d+32>>2]=e;c[e+24>>2]=d;b[e+14>>1]=1;c[e+36>>2]=0;return}function Vd(a){a=a|0;var b=0;b=Kn(a)|0;if((b|0)==0){za();return 0}else{return b|0}return 0}function Wd(a,b){a=a|0;b=b|0;return Mn(a,b)|0}function Xd(){var a=0;a=Vd(24)|0;c[a>>2]=1;c[a+8>>2]=0;c[a+12>>2]=0;c[a+16>>2]=-1;c[a+20>>2]=0;c[a+4>>2]=0;return a|0}function Yd(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=i;if((c[b>>2]|0)==100){Ae(c[a+116>>2]|0,6,12280,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}g=d+16|0;h=c[g>>2]|0;j=c[h+16>>2]|0;k=c[e+16>>2]|0;if((j|0)!=(c[k+16>>2]|0)){l=0;i=f;return l|0}e=c[(c[c[c[(c[d+8>>2]|0)+4>>2]>>2]>>2]|0)+88>>2]|0;d=c[h+12>>2]|0;h=c[k+12>>2]|0;if((j|0)==0){l=1;i=f;return l|0}j=0;k=c[b>>2]|0;while(1){c[b>>2]=k+1;m=(wb[e&31](a,b,c[d+(j<<2)>>2]|0,c[h+(j<<2)>>2]|0)|0)==0;n=(c[b>>2]|0)-1|0;c[b>>2]=n;o=j+1|0;if(m){l=0;p=8;break}if(o>>>0<(c[(c[g>>2]|0)+16>>2]|0)>>>0){j=o;k=n}else{l=1;p=8;break}}if((p|0)==8){i=f;return l|0}return 0}function Zd(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0;d=c[b+16>>2]|0;e=c[d+8>>2]|0;if((e|0)==0){return}f=e+16|0;if((c[f>>2]|0)==(a|0)){return}c[f>>2]=a;f=c[(c[c[c[(c[b+8>>2]|0)+4>>2]>>2]>>2]|0)+84>>2]|0;if((f|0)==0){return}b=d+16|0;e=c[b>>2]|0;if((e|0)==0){return}g=d+12|0;d=0;h=e;while(1){e=c[(c[g>>2]|0)+(d<<2)>>2]|0;if((c[e>>2]&1048576|0)==0){rb[f&127](a,e);i=c[b>>2]|0}else{i=h}e=d+1|0;if(e>>>0<i>>>0){d=e;h=i}else{break}}return}function _d(a){a=a|0;var d=0,e=0,f=0,g=0,h=0;d=c[a+8>>2]|0;e=c[a+16>>2]|0;a=c[e+8>>2]|0;if((a|0)!=0){c[a+8>>2]=0}a=e+16|0;f=(c[a>>2]|0)==0;do{if((b[(c[c[c[d+4>>2]>>2]>>2]|0)+56>>1]|0)==0){if(f){break}g=e+12|0;h=0;do{Ln(c[(c[g>>2]|0)+(h<<2)>>2]|0);h=h+1|0;}while(h>>>0<(c[a>>2]|0)>>>0)}else{if(f){break}h=e+12|0;g=0;do{ch(c[(c[h>>2]|0)+(g<<2)>>2]|0);Ln(c[(c[h>>2]|0)+(g<<2)>>2]|0);g=g+1|0;}while(g>>>0<(c[a>>2]|0)>>>0)}}while(0);Ln(c[e+12>>2]|0);Ln(e);return}function $d(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;e=i;i=i+8|0;f=e|0;g=c[d+8>>2]|0;do{if((g|0)==0){h=0;j=c[c[a+4>>2]>>2]|0}else{k=g+16|0;if((c[k>>2]|0)==-1){i=e;return}if((c[g+8>>2]|0)==0){i=e;return}else{l=c[c[a+4>>2]>>2]|0;c[k>>2]=-1;h=1;j=l;break}}}while(0);a=d+16|0;g=(c[a>>2]|0)==0;do{if((b[(c[j>>2]|0)+56>>1]|0)==0){if(g){break}l=d+12|0;k=0;do{Ln(c[(c[l>>2]|0)+(k<<2)>>2]|0);k=k+1|0;}while(k>>>0<(c[a>>2]|0)>>>0)}else{if(g){break}k=d+12|0;l=f|0;m=0;do{n=c[(c[k>>2]|0)+(m<<2)>>2]|0;do{if((c[n>>2]&7340032|0)==0){o=n+16|0;p=c[o>>2]|0;q=c[o+4>>2]|0;c[l>>2]=p;c[l+4>>2]=q;q=p|0;p=c[q>>2]|0;if((p|0)==1){kh(j,f);break}else{c[q>>2]=p-1;break}}}while(0);Ln(n);m=m+1|0;}while(m>>>0<(c[a>>2]|0)>>>0)}}while(0);Ln(c[d+12>>2]|0);if((h|0)!=0){i=e;return}Ln(d);i=e;return}function ae(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;a=c[g+((e[d>>1]|0)<<2)>>2]|0;h=f|0;c[h>>2]=c[(c[(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0)+16>>2];c[h+4>>2]=0;gh(a,f);i=b;return}function be(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0;b=c[a>>2]|0;a=c[(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;f=c[b+((e[d+4>>1]|0)<<2)>>2]|0;d=a+4|0;if((c[d>>2]|0)==0){re(a)}b=a+16|0;g=c[b>>2]|0;h=hh(f)|0;c[(c[a+12>>2]|0)+(g<<2)>>2]=h;c[b>>2]=(c[b>>2]|0)+1;c[d>>2]=(c[d>>2]|0)-1;return}function ce(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0;b=i;f=c[a>>2]|0;g=c[(c[f+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;h=c[f+((e[d>>1]|0)<<2)>>2]|0;d=g+16|0;f=c[d>>2]|0;if((f|0)==0){Ae(c[a+116>>2]|0,3,18192,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a;j=c[d>>2]|0}else{j=f}f=g+12|0;gh(h,(c[(c[f>>2]|0)+(j-1<<2)>>2]|0)+16|0);Ln(c[(c[f>>2]|0)+((c[d>>2]|0)-1<<2)>>2]|0);c[d>>2]=(c[d>>2]|0)-1;d=g+4|0;c[d>>2]=(c[d>>2]|0)+1;i=b;return}function de(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0;b=c[a>>2]|0;f=c[(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;g=(c[b+((e[d+4>>1]|0)<<2)>>2]|0)+16|0;h=c[b+((e[d+6>>1]|0)<<2)>>2]|0;d=se(a,f,c[g>>2]|0,c[g+4>>2]|0)|0;g=G;a=f+4|0;if((c[a>>2]|0)==0){re(f)}b=f+16|0;i=c[b>>2]|0;j=0;if((i|0)==(d|0)&(j|0)==(g|0)){k=d;l=f+12|0;m=hh(h)|0;n=c[l>>2]|0;o=n+(k<<2)|0;c[o>>2]=m;p=c[b>>2]|0;q=p+1|0;c[b>>2]=q;r=c[a>>2]|0;s=r-1|0;c[a>>2]=s;return}else{t=f+12|0;f=c[t>>2]|0;u=d;v=jo(i,j,d,g)|0;go(f+(u+1<<2)|0,f+(u<<2)|0,v<<2|0>>>30|0)|0;k=u;l=t;m=hh(h)|0;n=c[l>>2]|0;o=n+(k<<2)|0;c[o>>2]=m;p=c[b>>2]|0;q=p+1|0;c[b>>2]=q;r=c[a>>2]|0;s=r-1|0;c[a>>2]=s;return}}function ee(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;b=i;f=c[a>>2]|0;g=c[(c[f+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;h=(c[f+((e[d+4>>1]|0)<<2)>>2]|0)+16|0;d=c[h>>2]|0;f=c[h+4>>2]|0;h=g+16|0;if((c[h>>2]|0)==0){Ae(c[a+116>>2]|0,3,16368,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}j=se(a,g,d,f)|0;f=G;d=g+4|0;if((c[d>>2]|0)==0){re(g)}a=j;k=g+12|0;g=c[(c[k>>2]|0)+(a<<2)>>2]|0;ch(g);Ln(g);g=c[h>>2]|0;l=g;m=0;if((l|0)==(j|0)&(m|0)==(f|0)){n=g;o=n-1|0;c[h>>2]=o;p=c[d>>2]|0;q=p+1|0;c[d>>2]=q;i=b;return}g=c[k>>2]|0;k=jo(l,m,j,f)|0;go(g+(a<<2)|0,g+(a+1<<2)|0,k<<2|0>>>30|0)|0;n=c[h>>2]|0;o=n-1|0;c[h>>2]=o;p=c[d>>2]|0;q=p+1|0;c[d>>2]=q;i=b;return}function fe(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0;b=c[(c[(c[a>>2]|0)+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;d=b+16|0;if((c[d>>2]|0)==0){f=0;g=b+4|0;h=c[g>>2]|0;i=h+f|0;c[g>>2]=i;c[d>>2]=0;return}a=b+12|0;j=0;while(1){ch(c[(c[a>>2]|0)+(j<<2)>>2]|0);Ln(c[(c[a>>2]|0)+(j<<2)>>2]|0);k=j+1|0;l=c[d>>2]|0;if(k>>>0<l>>>0){j=k}else{f=l;break}}g=b+4|0;h=c[g>>2]|0;i=h+f|0;c[g>>2]=i;c[d>>2]=0;return}function ge(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;h=c[g+((e[d+2>>1]|0)<<2)>>2]|0;j=c[g+((e[d+4>>1]|0)<<2)>>2]|0;k=c[h+16>>2]|0;l=c[c[(c[h+8>>2]|0)+4>>2]>>2]|0;m=c[g+((e[d>>1]|0)<<2)>>2]|0;c[f>>2]=0;d=k+16|0;if((c[d>>2]|0)==0){fh(m,h);i=b;return}g=k+12|0;k=0;do{Bc(a,f,l,j,1,(n=i,i=i+8|0,c[n>>2]=c[(c[g>>2]|0)+(k<<2)>>2],n)|0)|0;i=n;k=k+1|0;}while(k>>>0<(c[d>>2]|0)>>>0);fh(m,h);i=b;return}function he(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;b=i;i=i+32|0;f=b|0;g=b+24|0;h=c[a>>2]|0;j=c[h+((e[d+2>>1]|0)<<2)>>2]|0;k=c[h+((e[d+4>>1]|0)<<2)>>2]|0;l=c[j+16>>2]|0;m=c[h+((e[d>>1]|0)<<2)>>2]|0;d=f+16|0;c[d>>2]=0;c[d+4>>2]=0;c[f+8>>2]=c[(c[(c[k+8>>2]|0)+4>>2]|0)+4>>2];c[f>>2]=4194304;c[g>>2]=0;h=l+16|0;if((c[h>>2]|0)==0){fh(m,j);i=b;return}else{n=0}do{Bc(a,g,0,k,1,(l=i,i=i+8|0,c[l>>2]=f,l)|0)|0;i=l;n=n+1|0;l=io(c[d>>2]|0,c[d+4>>2]|0,1,0)|0;c[d>>2]=l;c[d+4>>2]=G;}while(n>>>0<(c[h>>2]|0)>>>0);fh(m,j);i=b;return}function ie(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0;d=i;i=i+8|0;g=d|0;h=c[a>>2]|0;j=c[(c[h+((e[f+2>>1]|0)<<2)>>2]|0)+16>>2]|0;if((j|0)<0){Ae(c[a+116>>2]|0,5,14840,(k=i,i=i+8|0,c[k>>2]=j,k)|0);i=k}k=c[h+((e[f+4>>1]|0)<<2)>>2]|0;l=c[h+((e[f>>1]|0)<<2)>>2]|0;f=Xd()|0;h=c[l+8>>2]|0;if((b[h+14>>1]&2)!=0){vc(a,h,f)}c[g>>2]=f;gh(l,g);g=Vd(j<<2)|0;c[f+12>>2]=g;if((j|0)>0){m=0}else{n=f+16|0;c[n>>2]=j;i=d;return}do{c[g+(m<<2)>>2]=hh(k)|0;m=m+1|0;}while((m|0)<(j|0));n=f+16|0;c[n>>2]=j;i=d;return}function je(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0;b=i;i=i+16|0;f=b|0;g=b+8|0;h=c[a>>2]|0;j=c[h+((e[d>>1]|0)<<2)>>2]|0;k=c[(c[h+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;l=c[h+((e[d+4>>1]|0)<<2)>>2]|0;d=c[c[(c[l+8>>2]|0)+4>>2]>>2]|0;c[f>>2]=0;h=k+16|0;if((c[h>>2]|0)==0){m=0;n=0;o=g|0;p=o|0;c[p>>2]=n;q=o+4|0;c[q>>2]=m;gh(j,g);i=b;return}r=k+12|0;k=0;s=0;do{t=Bc(a,f,d,l,1,(u=i,i=i+8|0,c[u>>2]=c[(c[r>>2]|0)+(k<<2)>>2],u)|0)|0;i=u;u=t+16|0;s=((c[u>>2]|0)==1&(c[u+4>>2]|0)==0&1)+s|0;k=k+1|0;}while(k>>>0<(c[h>>2]|0)>>>0);m=(s|0)<0|0?-1:0;n=s;o=g|0;p=o|0;c[p>>2]=n;q=o+4|0;c[q>>2]=m;gh(j,g);i=b;return}function ke(a,c,d){a=a|0;c=c|0;d=d|0;te(a,b[d>>1]|0,b[d+2>>1]|0,b[d+4>>1]|0,1);return}function le(a,c,d){a=a|0;c=c|0;d=d|0;te(a,b[d>>1]|0,b[d+2>>1]|0,b[d+4>>1]|0,0);return}function me(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;h=c[g+((e[d>>1]|0)<<2)>>2]|0;j=c[(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;k=c[g+((e[d+4>>1]|0)<<2)>>2]|0;d=c[c[(c[k+8>>2]|0)+4>>2]>>2]|0;g=c[a+104>>2]|0;l=g+4|0;m=c[l>>2]|0;c[f>>2]=0;n=j+16|0;Dc(a,c[n>>2]|0);if((c[n>>2]|0)==0){ue(a,m,h);i=b;return}o=j+12|0;j=g|0;g=0;do{p=Bc(a,f,d,k,1,(q=i,i=i+8|0,c[q>>2]=c[(c[o>>2]|0)+(g<<2)>>2],q)|0)|0;i=q;q=hh(p)|0;c[(c[j>>2]|0)+(c[l>>2]<<2)>>2]=q;c[l>>2]=(c[l>>2]|0)+1;g=g+1|0;}while(g>>>0<(c[n>>2]|0)>>>0);ue(a,m,h);i=b;return}function ne(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0;b=i;f=c[a>>2]|0;g=c[(c[f+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;h=c[f+((e[d>>1]|0)<<2)>>2]|0;d=g+16|0;if((c[d>>2]|0)==0){Ae(c[a+116>>2]|0,3,13480,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a}a=g+12|0;gh(h,(c[c[a>>2]>>2]|0)+16|0);Ln(c[c[a>>2]>>2]|0);h=c[d>>2]|0;if((h|0)==1){j=0;c[d>>2]=j;k=g+4|0;l=c[k>>2]|0;m=l+1|0;c[k>>2]=m;i=b;return}f=c[a>>2]|0;go(f|0,f+4|0,(h<<2)-4|0)|0;j=(c[d>>2]|0)-1|0;c[d>>2]=j;k=g+4|0;l=c[k>>2]|0;m=l+1|0;c[k>>2]=m;i=b;return}function oe(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0;b=c[a>>2]|0;a=c[(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;f=c[b+((e[d+4>>1]|0)<<2)>>2]|0;d=a+4|0;if((c[d>>2]|0)==0){re(a)}b=a+16|0;g=c[b>>2]|0;h=a+12|0;if((g|0)!=0){a=c[h>>2]|0;go(a+4|0,a|0,g<<2|0)|0}g=hh(f)|0;c[c[h>>2]>>2]=g;c[b>>2]=(c[b>>2]|0)+1;c[d>>2]=(c[d>>2]|0)-1;return}function pe(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;h=c[g+((e[d>>1]|0)<<2)>>2]|0;j=c[(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;k=c[g+((e[d+4>>1]|0)<<2)>>2]|0;l=c[g+((e[d+6>>1]|0)<<2)>>2]|0;d=c[k+8>>2]|0;c[f>>2]=0;g=j+16|0;if((c[g>>2]|0)==0){m=k;fh(h,m);i=b;return}n=j+12|0;j=k;k=0;while(1){o=c[(c[n>>2]|0)+(k<<2)>>2]|0;p=Bc(a,f,d,l,2,(q=i,i=i+16|0,c[q>>2]=j,c[q+8>>2]=o,q)|0)|0;i=q;q=k+1|0;if(q>>>0<(c[g>>2]|0)>>>0){j=p;k=q}else{m=p;break}}fh(h,m);i=b;return}function qe(a){a=a|0;return ei(a,4568)|0}function re(a){a=a|0;var b=0,d=0,e=0;b=c[a+16>>2]|0;d=(b+8|0)>>>2;e=a+12|0;c[e>>2]=Wd(c[e>>2]|0,d+b<<2)|0;c[a+4>>2]=d;return}function se(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;f=i;g=0;if((e|0)<(g|0)|(e|0)==(g|0)&d>>>0<0>>>0){g=jo(0,0,d,e)|0;h=G;j=b+16|0;k=c[j>>2]|0;l=k;m=0;if(m>>>0<h>>>0|m>>>0==h>>>0&l>>>0<g>>>0){g=c[a+116>>2]|0;h=jo(0,0,l,m)|0;m=G;Ae(g,3,14792,(n=i,i=i+32|0,c[n>>2]=d,c[n+8>>2]=e,c[n+16>>2]=h,c[n+24>>2]=m,n)|0);i=n;o=c[j>>2]|0}else{o=k}k=io(o,0,d,e)|0;p=G;q=k;i=f;return(G=p,q)|0}else{k=c[b+16>>2]|0;b=0;if(!((b|0)<(e|0)|(b|0)==(e|0)&k>>>0<d>>>0)){p=e;q=d;i=f;return(G=p,q)|0}Ae(c[a+116>>2]|0,3,14592,(n=i,i=i+24|0,c[n>>2]=d,c[n+8>>2]=e,c[n+16>>2]=k,n)|0);i=n;p=e;q=d;i=f;return(G=p,q)|0}return 0}function te(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;g=i;i=i+8|0;h=g|0;j=c[a>>2]|0;k=c[j+((b&65535)<<2)>>2]|0;b=c[(c[j+((d&65535)<<2)>>2]|0)+16>>2]|0;d=c[j+((e&65535)<<2)>>2]|0;e=c[c[(c[d+8>>2]|0)+4>>2]>>2]|0;j=c[a+104>>2]|0;l=j+4|0;m=c[l>>2]|0;c[h>>2]=0;n=b+16|0;Dc(a,c[n>>2]|0);if((c[n>>2]|0)==0){ue(a,m,k);i=g;return}o=b+12|0;b=f;p=(f|0)<0|0?-1:0;f=j|0;j=0;do{q=Bc(a,h,e,d,1,(r=i,i=i+8|0,c[r>>2]=c[(c[o>>2]|0)+(j<<2)>>2],r)|0)|0;i=r;r=q+16|0;if((c[r>>2]|0)==(b|0)&(c[r+4>>2]|0)==(p|0)){r=hh(c[(c[o>>2]|0)+(j<<2)>>2]|0)|0;c[(c[f>>2]|0)+(c[l>>2]<<2)>>2]=r;c[l>>2]=(c[l>>2]|0)+1}j=j+1|0;}while(j>>>0<(c[n>>2]|0)>>>0);ue(a,m,k);i=g;return}function ue(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=i;i=i+8|0;g=f|0;h=c[a+104>>2]|0;j=Xd()|0;k=h+4|0;l=(c[k>>2]|0)-d|0;m=c[e+8>>2]|0;if((b[m+14>>1]&2)!=0){vc(a,m,j)}c[j+16>>2]=l;m=Vd(l<<2)|0;a=j+12|0;c[a>>2]=m;if((l|0)<=0){c[k>>2]=d;n=g;c[n>>2]=j;gh(e,g);i=f;return}o=h|0;h=0;p=m;while(1){c[p+(h<<2)>>2]=c[(c[o>>2]|0)+(h+d<<2)>>2];m=h+1|0;if((m|0)>=(l|0)){break}h=m;p=c[a>>2]|0}c[k>>2]=d;n=g;c[n>>2]=j;gh(e,g);i=f;return}function ve(){var a=0,b=0;a=Vd(24)|0;b=Kn(164)|0;c[b>>2]=0;c[b+4>>2]=0;c[a+4>>2]=gn()|0;c[a>>2]=b;c[a+16>>2]=0;c[a+8>>2]=0;c[a+12>>2]=0;return a|0}function we(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0;b=c[a+4>>2]|0;if((b|0)!=0){hn(b)}b=a|0;d=c[b>>2]|0;e=c[d>>2]|0;do{if((e|0)==0){if((d|0)!=0){f=d;break}g=a;Ln(g);return}else{h=e;while(1){c[b>>2]=h;i=c[h>>2]|0;if((i|0)==0){f=h;break}else{h=i}}}}while(0);while(1){e=c[f+4>>2]|0;Ln(f);c[b>>2]=e;if((e|0)==0){break}else{f=e}}g=a;Ln(g);return}function xe(a){a=a|0;var b=0,d=0,e=0;b=a|0;a=c[(c[b>>2]|0)+4>>2]|0;if((a|0)!=0){d=a;c[b>>2]=d;return d|0}a=Vd(164)|0;e=a;c[a>>2]=c[b>>2];c[(c[b>>2]|0)+4>>2]=e;c[a+4>>2]=0;d=e;c[b>>2]=d;return d|0}function ye(a){a=a|0;var b=0;b=a|0;c[b>>2]=c[c[b>>2]>>2];return}function ze(a){a=a|0;var b=0;b=a|0;a=c[c[b>>2]>>2]|0;c[b>>2]=a;Va(a+8|0,1)}function Ae(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0;g=i;i=i+16|0;h=g|0;g=a+4|0;qn(c[g>>2]|0);c[a+8>>2]=0;j=h;c[j>>2]=f;c[j+4>>2]=0;sn(c[g>>2]|0,e,h|0);b[a+20>>1]=d;Va((c[a>>2]|0)+8|0,1)}function Be(a,d){a=a|0;d=d|0;b[a+20>>1]=d;Va((c[a>>2]|0)+8|0,1)}function Ce(a,b,d){a=a|0;b=b|0;d=d|0;c[a+12>>2]=b;c[a+8>>2]=c[b+8>>2];b=a+4|0;qn(c[b>>2]|0);jn(c[b>>2]|0,d);Va((c[a>>2]|0)+8|0,1)}function De(a){a=a|0;var d=0,e=0;d=c[a+8>>2]|0;if((d|0)==0){e=4648+(b[a+20>>1]<<2)|0}else{e=(c[d>>2]|0)+8|0}return c[e>>2]|0}function Ee(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;b=c[d+16>>2]|0;d=c[b+4>>2]|0;a=c[e+16>>2]|0;do{if((d|0)==(c[a+4>>2]|0)){if((b|0)==(a|0)){f=1;return f|0}if((ho(c[b+8>>2]|0,c[a+8>>2]|0,d|0)|0)==0){f=1}else{break}return f|0}}while(0);f=0;return f|0}function Fe(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0;f=i;i=i+8|0;g=f|0;h=c[a>>2]|0;j=c[(c[h+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;if(b<<16>>16==2){k=c[(c[(c[h+((e[d+4>>1]|0)<<2)>>2]|0)+16>>2]|0)+8>>2]|0}else{k=19576}b=c[h+((e[d>>1]|0)<<2)>>2]|0;if((nb(k|0,19576)|0)!=0){Ae(c[a+116>>2]|0,5,20344,(l=i,i=i+1|0,i=i+7&-8,c[l>>2]=0,l)|0);i=l}k=c[j+8>>2]|0;d=c[j+4>>2]|0;if((Ji(k,d)|0)==0){Ae(c[a+116>>2]|0,5,17168,(l=i,i=i+1|0,i=i+7&-8,c[l>>2]=0,l)|0);i=l}l=Vd(12)|0;a=Vd(d+1|0)|0;Zn(a|0,k|0)|0;c[l>>2]=1;c[l+8>>2]=a;c[l+4>>2]=d;c[g>>2]=l;gh(b,g);i=f;return}function Ge(a){a=a|0;return ei(a,7712)|0}function He(){var a=0;a=Vd(12)|0;c[a>>2]=Vd(64)|0;c[a+4>>2]=0;c[a+8>>2]=63;return a|0}function Ie(a){a=a|0;if((a|0)!=0){Ln(c[a>>2]|0)}Ln(a);return}function Je(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0;d=a+4|0;e=c[d>>2]|0;f=e+1+(_n(b|0)|0)|0;g=a+8|0;h=c[g>>2]|0;if(h>>>0<f>>>0){i=h}else{j=c[a>>2]|0;k=e;l=j+k|0;m=Zn(l|0,b|0)|0;c[d>>2]=f;return e|0}do{i=i<<1;}while(i>>>0<f>>>0);c[g>>2]=i;g=a|0;a=Wd(c[g>>2]|0,i)|0;c[g>>2]=a;j=a;k=c[d>>2]|0;l=j+k|0;m=Zn(l|0,b|0)|0;c[d>>2]=f;return e|0}function Ke(a,b){a=a|0;b=b|0;return(c[a>>2]|0)+b|0}function Le(a,b){a=a|0;b=b|0;c[a+4>>2]=b;return}function Me(a){a=a|0;var b=0;b=Vd(12)|0;c[b>>2]=Vd(a<<1)|0;c[b+4>>2]=0;c[b+8>>2]=a;return b|0}function Ne(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0;e=a+4|0;f=c[e>>2]|0;g=f+1|0;h=a+8|0;if((g|0)==(c[h>>2]|0)){c[h>>2]=g<<1;h=a|0;i=Wd(c[h>>2]|0,g<<2)|0;c[h>>2]=i;j=c[e>>2]|0;k=i}else{j=f;k=c[a>>2]|0}b[k+(j<<1)>>1]=d;c[e>>2]=(c[e>>2]|0)+1;return}function Oe(a){a=a|0;var d=0,e=0,f=0;d=a+4|0;e=(c[d>>2]|0)-1|0;f=b[(c[a>>2]|0)+(e<<1)>>1]|0;c[d>>2]=e;return f|0}function Pe(a){a=a|0;var b=0;b=Vd(12)|0;c[b>>2]=Vd(a<<2)|0;c[b+4>>2]=0;c[b+8>>2]=a;return b|0}function Qe(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=a+4|0;e=c[d>>2]|0;f=e+1|0;g=a+8|0;if((f|0)==(c[g>>2]|0)){c[g>>2]=f<<1;g=a|0;h=Wd(c[g>>2]|0,f<<3)|0;c[g>>2]=h;i=c[d>>2]|0;j=h}else{i=e;j=c[a>>2]|0}c[j+(i<<2)>>2]=b;c[d>>2]=(c[d>>2]|0)+1;return}function Re(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0;f=a+4|0;g=c[f>>2]|0;h=g+1|0;i=a+8|0;if((h|0)==(c[i>>2]|0)){c[i>>2]=h<<1;i=a|0;j=Wd(c[i>>2]|0,h<<2)|0;c[i>>2]=j;k=c[f>>2]|0;l=j}else{k=g;l=c[a>>2]|0}go(l+(d+1<<1)|0,l+(d<<1)|0,k-d<<1|0)|0;c[f>>2]=(c[f>>2]|0)+1;b[(c[a>>2]|0)+(d<<1)>>1]=e;return}function Se(a){a=a|0;Ln(c[a>>2]|0);Ln(a);return}function Te(){var a=0;a=Vd(24)|0;c[a>>2]=1;c[a+4>>2]=100;fo(a+8|0,0,16)|0;return a|0}function Ue(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0;d=Vd(76)|0;e=d;c[d+72>>2]=c[a+20>>2];c[d+8>>2]=0;c[d+12>>2]=0;f=Cf(e,21136,16104)|0;g=ve()|0;b[d+20>>1]=0;b[d+22>>1]=4;b[d+26>>1]=1;c[d+28>>2]=0;h=d+64|0;c[h>>2]=g;c[d+16>>2]=Vd(8)|0;i=d+40|0;c[i>>2]=od()|0;j=Rh(f)|0;f=d+52|0;c[f>>2]=j;k=d+48|0;c[k>>2]=Oj(j,g)|0;j=d+44|0;c[j>>2]=om(a,g)|0;l=d+56|0;c[l>>2]=tc(a,g)|0;c[d+32>>2]=gn()|0;c[d+68>>2]=a;c[d+60>>2]=c[(c[k>>2]|0)+124>>2];c[(c[l>>2]|0)+112>>2]=c[f>>2];c[(c[l>>2]|0)+108>>2]=c[(c[k>>2]|0)+120>>2];c[(c[l>>2]|0)+100>>2]=c[(c[h>>2]|0)+4>>2];c[(c[l>>2]|0)+96>>2]=e;c[(c[l>>2]|0)+92>>2]=c[(c[k>>2]|0)+64>>2];c[(c[f>>2]|0)+96>>2]=(c[j>>2]|0)+16;c[(c[i>>2]|0)+40>>2]=(c[j>>2]|0)+16;c[(c[k>>2]|0)+108>>2]=(c[j>>2]|0)+16;c[(c[k>>2]|0)+132>>2]=c[f>>2];c[(c[k>>2]|0)+116>>2]=c[(c[i>>2]|0)+20>>2];c[(c[k>>2]|0)+128>>2]=e;c[(c[j>>2]|0)+56>>2]=c[f>>2];c[(c[j>>2]|0)+60>>2]=c[(c[i>>2]|0)+20>>2];c[d>>2]=Df(15952)|0;c[d+4>>2]=Df(15728)|0;Qj(c[k>>2]|0);c[(c[l>>2]|0)+124>>2]=c[c[f>>2]>>2];c[d+36>>2]=c[(c[(c[l>>2]|0)+124>>2]|0)+8>>2];Tm(e,a);Ef(e);a=Ff(e,15640)|0;c[(c[l>>2]|0)+88>>2]=a;b[d+24>>1]=0;return e|0}function Ve(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0;we(c[a+64>>2]|0);pd(c[a+40>>2]|0);uc(c[a+56>>2]|0);Th(c[a+52>>2]|0);pm(c[a+44>>2]|0);Pj(c[a+48>>2]|0);Gf(c[a>>2]|0);Gf(c[a+4>>2]|0);Ln(c[a+16>>2]|0);b=c[a+12>>2]|0;if((b|0)==0){d=a+32|0;e=c[d>>2]|0;hn(e);f=a+60|0;g=c[f>>2]|0;Hj(g);h=a;Ln(h);return}else{i=b}while(1){b=c[i+40>>2]|0;j=c[i+16>>2]|0;if((j|0)!=0){k=j;while(1){j=c[k+8>>2]|0;Ln(c[k+4>>2]|0);Ln(k);if((j|0)==0){break}else{k=j}}}k=c[i+28>>2]|0;if((k|0)!=0){Vm(k)}Ln(c[i+12>>2]|0);Ln(c[i+8>>2]|0);Ln(i);if((b|0)==0){break}else{i=b}}d=a+32|0;e=c[d>>2]|0;hn(e);f=a+60|0;g=c[f>>2]|0;Hj(g);h=a;Ln(h);return}function We(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Cf(a,b,16104)|0;c[f+32>>2]=d;c[f+36>>2]=e;return}function Xe(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0;e=ji(b,d)|0;if((e|0)!=0){f=e;return f|0}e=$h(c[a+52>>2]|0,0,d)|0;do{if((e|0)!=0){if((c[e+40>>2]|0)!=(b|0)){break}f=e;return f|0}}while(0);e=hi(b,d)|0;if((e|0)==0){f=Hf(a,b,d)|0;return f|0}else{f=e;return f|0}return 0}function Ye(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;e=c[a+52>>2]|0;if((b|0)==0){f=c[e+16>>2]|0}else{f=b}b=gi(e,f,d)|0;if((b|0)!=0){g=b;return g|0}g=If(a,f,d)|0;return g|0}function Ze(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0;g=i;h=c[a+44>>2]|0;j=a+60|0;k=c[(c[j>>2]|0)+4>>2]|0;wm(h,15520,1,e);c[h+16>>2]=d;d=a+48|0;e=Sj(c[d>>2]|0,c[a+36>>2]|0,15376)|0;ck(c[d>>2]|0,14);sm(h);yj(c[j>>2]|0,0);l=(f|0)!=0;do{if(l){m=f+8|0;n=c[m>>2]|0;if(n>>>0<=1>>>0){o=13;break}p=h+44|0;q=a+64|0;if((c[p>>2]|0)==46){Ae(c[q>>2]|0,1,15128,(r=i,i=i+8|0,c[r>>2]=n-1,r)|0);i=r;s=c[m>>2]|0}else{s=n}n=s-1|0;m=f+4|0;t=h+12|0;u=0;v=2;while(1){sm(h);if((c[p>>2]|0)!=34){w=c[q>>2]|0;x=ym(34)|0;y=ym(c[p>>2]|0)|0;Ae(w,1,15e3,(r=i,i=i+16|0,c[r>>2]=x,c[r+8>>2]=y,r)|0);i=r}y=u+1|0;x=c[(c[m>>2]|0)+(y<<2)>>2]|0;if((b[x+14>>1]&64)!=0){Ae(c[q>>2]|0,1,14752,(r=i,i=i+8|0,c[r>>2]=c[t>>2],r)|0);i=r}yj(c[j>>2]|0,x);Jf(a,x)|0;x=(y|0)==(n|0)?45:v;if((c[p>>2]|0)!=(x|0)){w=c[q>>2]|0;z=ym(x)|0;A=ym(c[p>>2]|0)|0;Ae(w,1,15e3,(r=i,i=i+16|0,c[r>>2]=z,c[r+8>>2]=A,r)|0);i=r}if((x|0)==45){break}else{u=y;v=x}}B=u+2|0}else{o=13}}while(0);do{if((o|0)==13){s=h+44|0;v=c[s>>2]|0;if((v|0)==46){B=1;break}else if((v|0)!=45){Ae(c[a+64>>2]|0,1,14568,(r=i,i=i+8|0,c[r>>2]=v,r)|0);i=r;B=1;break}sm(h);if((c[s>>2]|0)==45){B=1;break}v=c[a+64>>2]|0;p=ym(45)|0;q=ym(c[s>>2]|0)|0;Ae(v,1,15e3,(r=i,i=i+16|0,c[r>>2]=p,c[r+8>>2]=q,r)|0);i=r;B=1}}while(0);o=a+40|0;Ld(c[o>>2]|0);q=Kf(a,f)|0;Md(c[o>>2]|0);o=h+44|0;if((c[o>>2]|0)!=3){p=c[a+64>>2]|0;v=ym(3)|0;s=ym(c[o>>2]|0)|0;Ae(p,1,15e3,(r=i,i=i+16|0,c[r>>2]=v,c[r+8>>2]=s,r)|0);i=r}sm(h);zj(c[j>>2]|0,k,q);if(!l){C=0;D=c[j>>2]|0;E=a+52|0;F=c[E>>2]|0;G=F+68|0;H=c[G>>2]|0;I=Cj(D,C,H,B)|0;J=e+8|0;c[J>>2]=I;K=c[d>>2]|0;dk(K);qm(h);i=g;return e|0}if((b[(c[f>>2]|0)+52>>1]|0)!=5){C=0;D=c[j>>2]|0;E=a+52|0;F=c[E>>2]|0;G=F+68|0;H=c[G>>2]|0;I=Cj(D,C,H,B)|0;J=e+8|0;c[J>>2]=I;K=c[d>>2]|0;dk(K);qm(h);i=g;return e|0}C=b[f+14>>1]&1;D=c[j>>2]|0;E=a+52|0;F=c[E>>2]|0;G=F+68|0;H=c[G>>2]|0;I=Cj(D,C,H,B)|0;J=e+8|0;c[J>>2]=I;K=c[d>>2]|0;dk(K);qm(h);i=g;return e|0}function _e(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0;f=1;g=0;h=i;i=i+168|0;c[h>>2]=0;while(1)switch(f|0){case 1:j=co((c[c[a+64>>2]>>2]|0)+8|0,f,h)|0;f=4;break;case 4:if((j|0)==0){f=2;break}else{k=0;f=3;break};case 2:l=a+44|0;ra(2,c[l>>2]|0,b|0,d|0,e|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return 0}t=u=0;ja(2,a|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return 0}t=u=0;ja(10,c[l>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return 0}t=u=0;k=1;f=3;break;case 3:return k|0;case-1:if((g|0)==1){j=u;f=4}t=u=0;break}return 0}function $e(d){d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0;e=i;f=c[d+64>>2]|0;g=c[d+32>>2]|0;qn(g);h=c[f+8>>2]|0;do{if((h|0)!=0){j=c[(c[(c[h>>2]|0)+72>>2]|0)+8>>2]|0;if((a[j]|0)==0){break}tn(g,14512,(k=i,i=i+8|0,c[k>>2]=j,k)|0);i=k}}while(0);jn(g,De(f)|0);h=c[c[f+4>>2]>>2]|0;if((a[h]|0)==0){mn(g,10)}else{tn(g,14464,(k=i,i=i+8|0,c[k>>2]=h,k)|0);i=k}if((b[d+24>>1]|0)!=0){h=c[(c[d+56>>2]|0)+8>>2]|0;jn(g,14240);if((h|0)==0){l=g|0;m=c[l>>2]|0;i=e;return m|0}else{n=h}do{h=c[n>>2]|0;j=c[h+12>>2]|0;o=(j|0)==0;p=o?21136:j;j=o?21136:14104;if((c[h+28>>2]|0)==0){o=c[h+16>>2]|0;tn(g,13952,(k=i,i=i+24|0,c[k>>2]=p,c[k+8>>2]=j,c[k+16>>2]=o,k)|0);i=k}else{o=c[n+20>>2]|0;q=c[h+16>>2]|0;tn(g,13760,(k=i,i=i+40|0,c[k>>2]=c[(c[h+20>>2]|0)+12>>2],c[k+8>>2]=o,c[k+16>>2]=p,c[k+24>>2]=j,c[k+32>>2]=q,k)|0);i=k}n=c[n+36>>2]|0;}while((n|0)!=0);l=g|0;m=c[l>>2]|0;i=e;return m|0}n=c[d+44>>2]|0;d=c[n>>2]|0;q=c[f+16>>2]|0;if((q|0)==0){r=c[n+16>>2]|0}else{r=q}q=d|0;n=c[q>>2]|0;if((nb(n|0,15520)|0)==0){f=d;while(1){j=c[f+48>>2]|0;p=j|0;o=c[p>>2]|0;if((nb(o|0,15520)|0)==0){f=j}else{s=j;t=p;u=o;break}}}else{s=d;t=q;u=n}if((nb(u|0,16104)|0)==0){l=g|0;m=c[l>>2]|0;i=e;return m|0}c[s+32>>2]=r;tn(g,14328,(k=i,i=i+16|0,c[k>>2]=c[t>>2],c[k+8>>2]=r,k)|0);i=k;l=g|0;m=c[l>>2]|0;i=e;return m|0}function af(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0;d=i;e=a+44|0;f=c[e>>2]|0;g=a+48|0;ck(c[g>>2]|0,0);Mf(a);mk(c[g>>2]|0,c[a+40>>2]|0);h=f+44|0;if((c[h>>2]|0)!=41){j=c[a+64>>2]|0;k=ym(41)|0;l=ym(c[h>>2]|0)|0;Ae(j,1,15e3,(j=i,i=i+16|0,c[j>>2]=k,c[j+8>>2]=l,j)|0);i=j}sm(f);a:do{if((c[h>>2]|0)==28){Nf(a,b)}else{Of(a,0);j=f+12|0;do{if((c[h>>2]|0)!=34){break a}l=Pf(c[j>>2]|0)|0;if(!((l|0)==8|(l|0)==6)){break a}sm(c[e>>2]|0);rb[c[5696+(l<<2)>>2]&127](a,0)}while((l|0)!=6)}}while(0);dk(c[g>>2]|0);i=d;return}function bf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0;d=i;e=c[a+44>>2]|0;f=a+48|0;ck(c[f>>2]|0,5);g=e+44|0;if((c[g>>2]|0)!=41){h=c[a+64>>2]|0;j=ym(41)|0;k=ym(c[g>>2]|0)|0;Ae(h,1,15e3,(l=i,i=i+16|0,c[l>>2]=j,c[l+8>>2]=k,l)|0);i=l}sm(e);if((c[g>>2]|0)==28){Nf(a,b)}else{Of(a,0)}if((c[g>>2]|0)!=34){b=c[a+64>>2]|0;k=ym(34)|0;j=ym(c[g>>2]|0)|0;Ae(b,1,15e3,(l=i,i=i+16|0,c[l>>2]=k,c[l+8>>2]=j,l)|0);i=l}j=c[e+12>>2]|0;if((nb(j|0,20456)|0)!=0){Ae(c[a+64>>2]|0,1,19120,(l=i,i=i+8|0,c[l>>2]=j,l)|0);i=l}sm(e);ai(c[a+52>>2]|0,c[(c[(c[f>>2]|0)+96>>2]|0)+4>>2]|0);Mf(a);mk(c[f>>2]|0,c[a+40>>2]|0);dk(c[f>>2]|0);i=d;return}function cf(a,b){a=a|0;b=b|0;Qf(a,0);return}function df(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;e=i;f=a+44|0;g=c[f>>2]|0;h=g+44|0;if((c[h>>2]|0)!=34){j=c[a+64>>2]|0;k=ym(34)|0;l=ym(c[h>>2]|0)|0;Ae(j,1,15e3,(m=i,i=i+16|0,c[m>>2]=k,c[m+8>>2]=l,m)|0);i=m}l=a+48|0;ck(c[l>>2]|0,6);k=a+52|0;j=g+12|0;n=$h(c[k>>2]|0,0,c[j>>2]|0)|0;do{if((n|0)==0){o=Rj(c[l>>2]|0,c[(c[(c[k>>2]|0)+44>>2]|0)+24>>2]|0,c[j>>2]|0)|0}else{p=c[n+8>>2]|0;if((b[(c[p>>2]|0)+52>>1]|0)==0){o=n;break}Ae(c[a+64>>2]|0,1,19528,(m=i,i=i+8|0,c[m>>2]=p,m)|0);i=m;o=n}}while(0);sm(g);if((c[h>>2]|0)!=34){n=c[a+64>>2]|0;k=ym(34)|0;p=ym(c[h>>2]|0)|0;Ae(n,1,15e3,(m=i,i=i+16|0,c[m>>2]=k,c[m+8>>2]=p,m)|0);i=m}p=c[j>>2]|0;if((nb(p|0,19520)|0)!=0){Ae(c[a+64>>2]|0,1,19480,(m=i,i=i+8|0,c[m>>2]=p,m)|0);i=m}sm(g);p=Rf(a,19432)|0;if((c[h>>2]|0)!=48){k=c[a+64>>2]|0;n=ym(48)|0;q=ym(c[h>>2]|0)|0;Ae(k,1,15e3,(m=i,i=i+16|0,c[m>>2]=n,c[m+8>>2]=q,m)|0);i=m}sm(g);q=Rf(a,19392)|0;if((c[h>>2]|0)==34){n=c[j>>2]|0;if((nb(n|0,19376)|0)!=0){Ae(c[a+64>>2]|0,1,19272,(m=i,i=i+8|0,c[m>>2]=n,m)|0);i=m}sm(g);r=Rf(a,19256)|0}else{r=0}Xj(c[l>>2]|0,o,p,q,r,c[(c[f>>2]|0)+16>>2]|0);if((c[h>>2]|0)!=41){f=c[a+64>>2]|0;r=ym(41)|0;q=ym(c[h>>2]|0)|0;Ae(f,1,15e3,(m=i,i=i+16|0,c[m>>2]=r,c[m+8>>2]=q,m)|0);i=m}sm(g);if((c[h>>2]|0)==28){Nf(a,d);s=c[l>>2]|0;dk(s);i=e;return}else{Of(a,0);s=c[l>>2]|0;dk(s);i=e;return}}function ef(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;e=i;f=a+44|0;g=c[f>>2]|0;h=a+48|0;ck(c[h>>2]|0,7);_j(c[h>>2]|0,c[(c[f>>2]|0)+16>>2]|0);j=g+44|0;if((c[j>>2]|0)!=41){k=c[a+64>>2]|0;l=ym(41)|0;m=ym(c[j>>2]|0)|0;Ae(k,1,15e3,(k=i,i=i+16|0,c[k>>2]=l,c[k+8>>2]=m,k)|0);i=k}sm(g);if((c[j>>2]|0)==28){Nf(a,d);n=c[h>>2]|0;dk(n);i=e;return}Of(a,0);d=g+12|0;while(1){if((c[j>>2]|0)!=34){o=9;break}if((nb(17536,c[d>>2]|0)|0)!=0){o=9;break}sm(c[f>>2]|0);uf(a,0);if((b[(c[(c[h>>2]|0)+96>>2]|0)+22>>1]|0)==9){o=9;break}}if((o|0)==9){n=c[h>>2]|0;dk(n);i=e;return}}function ff(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;d=i;e=a+48|0;f=c[(c[e>>2]|0)+96>>2]|0;if((b[f+22>>1]|0)!=10){Ae(c[a+64>>2]|0,1,19736,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}h=c[(c[f+44>>2]|0)+8>>2]|0;f=c[h>>2]|0;j=c[a+44>>2]|0;k=j+44|0;if((c[k>>2]|0)!=34){l=c[a+64>>2]|0;m=ym(34)|0;n=ym(c[k>>2]|0)|0;Ae(l,1,15e3,(g=i,i=i+16|0,c[g>>2]=m,c[g+8>>2]=n,g)|0);i=g}n=b[f+64>>1]|0;if(n<<16>>16==0){o=0;p=0;q=0}else{m=c[j+12>>2]|0;l=c[f+48>>2]|0;r=n&65535;s=0;while(1){t=c[l+(s<<2)>>2]|0;u=s+1|0;if((nb(m|0,c[t+8>>2]|0)|0)==0){v=t;w=s;break}if((u|0)<(r|0)){s=u}else{v=0;w=u;break}}o=v;p=w;q=n&65535}if((p|0)==(q|0)){q=c[f+8>>2]|0;Ae(c[a+64>>2]|0,1,19664,(g=i,i=i+16|0,c[g>>2]=c[j+12>>2],c[g+8>>2]=q,g)|0);i=g}if((gk(c[e>>2]|0,p)|0)==0){Ae(c[a+64>>2]|0,1,19624,(g=i,i=i+8|0,c[g>>2]=c[j+12>>2],g)|0);i=g}p=c[o+68>>2]|0;o=c[(c[e>>2]|0)+120>>2]|0;q=p+8|0;if((c[q>>2]|0)!=0){sm(j);if((c[k>>2]|0)!=0){f=c[a+64>>2]|0;n=ym(0)|0;w=ym(c[k>>2]|0)|0;Ae(f,1,15e3,(g=i,i=i+16|0,c[g>>2]=n,c[g+8>>2]=w,g)|0);i=g}sm(j);if((c[k>>2]|0)!=34){w=c[a+64>>2]|0;n=ym(34)|0;f=ym(c[k>>2]|0)|0;Ae(w,1,15e3,(g=i,i=i+16|0,c[g>>2]=n,c[g+8>>2]=f,g)|0);i=g}if((c[q>>2]|0)>>>0>1>>>0){f=p+4|0;n=a+64|0;w=1;do{Jf(a,uh(o,h,c[(c[f>>2]|0)+(w<<2)>>2]|0)|0)|0;do{if((w|0)!=((c[q>>2]|0)-1|0)){if((c[k>>2]|0)!=2){v=c[n>>2]|0;s=ym(2)|0;r=ym(c[k>>2]|0)|0;Ae(v,1,15e3,(g=i,i=i+16|0,c[g>>2]=s,c[g+8>>2]=r,g)|0);i=g}sm(j);if((c[k>>2]|0)==34){break}r=c[n>>2]|0;s=ym(34)|0;v=ym(c[k>>2]|0)|0;Ae(r,1,15e3,(g=i,i=i+16|0,c[g>>2]=s,c[g+8>>2]=v,g)|0);i=g}}while(0);w=w+1|0;}while(w>>>0<(c[q>>2]|0)>>>0)}if((c[k>>2]|0)!=1){q=c[a+64>>2]|0;w=ym(1)|0;n=ym(c[k>>2]|0)|0;Ae(q,1,15e3,(g=i,i=i+16|0,c[g>>2]=w,c[g+8>>2]=n,g)|0);i=g}fk(c[e>>2]|0,p)}sm(j);if((c[k>>2]|0)==41){sm(j);i=d;return}p=c[a+64>>2]|0;a=ym(41)|0;e=ym(c[k>>2]|0)|0;Ae(p,1,15e3,(g=i,i=i+16|0,c[g>>2]=a,c[g+8>>2]=e,g)|0);i=g;sm(j);i=d;return}function gf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0;d=i;e=c[a+44>>2]|0;ek(c[a+48>>2]|0,2);f=e+44|0;if((c[f>>2]|0)==41){sm(e);Of(a,b);i=d;return}g=c[a+64>>2]|0;h=ym(41)|0;j=ym(c[f>>2]|0)|0;Ae(g,1,15e3,(g=i,i=i+16|0,c[g>>2]=h,c[g+8>>2]=j,g)|0);i=g;sm(e);Of(a,b);i=d;return}function hf(a,b){a=a|0;b=b|0;Sf(a,7);return}function jf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0;d=i;e=c[a+44>>2]|0;f=a+48|0;ek(c[f>>2]|0,1);Mf(a);mk(c[f>>2]|0,c[a+40>>2]|0);f=e+44|0;if((c[f>>2]|0)==41){sm(e);Of(a,b);i=d;return}g=c[a+64>>2]|0;h=ym(41)|0;j=ym(c[f>>2]|0)|0;Ae(g,1,15e3,(g=i,i=i+16|0,c[g>>2]=h,c[g+8>>2]=j,g)|0);i=g;sm(e);Of(a,b);i=d;return}function kf(a,b){a=a|0;b=b|0;Sf(a,9);return}function lf(d,f){d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0;f=i;g=d+48|0;h=c[(c[g>>2]|0)+96>>2]|0;do{if((b[h+22>>1]|0)!=15){if((c[h+60>>2]|0)==0){break}Ae(c[d+64>>2]|0,1,20120,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}}while(0);h=c[d+44>>2]|0;k=h+44|0;if((c[k>>2]|0)!=34){l=c[d+64>>2]|0;m=ym(34)|0;n=ym(c[k>>2]|0)|0;Ae(l,1,15e3,(j=i,i=i+16|0,c[j>>2]=m,c[j+8>>2]=n,j)|0);i=j}n=h+12|0;Tf(d,c[n>>2]|0);m=d+52|0;l=fi(c[m>>2]|0,c[n>>2]|0)|0;sm(h);o=e[(c[(c[g>>2]|0)+96>>2]|0)+14>>1]|0;if((c[k>>2]|0)==4){p=Uf(d)|0}else{p=0}si(c[m>>2]|0,l,p);ck(c[g>>2]|0,11);q=Vf(d,l,p)|0;p=d+28|0;r=c[p>>2]|0;c[p>>2]=q;s=d+64|0;if((c[k>>2]|0)!=28){t=c[s>>2]|0;u=ym(28)|0;v=ym(c[k>>2]|0)|0;Ae(t,1,15e3,(j=i,i=i+16|0,c[j>>2]=u,c[j+8>>2]=v,j)|0);i=j}sm(h);v=c[k>>2]|0;u=(v|0)==42;t=u&1;w=d+60|0;x=1;y=v;a:while(1){if(u){if((y|0)!=42){v=c[s>>2]|0;z=ym(42)|0;A=ym(c[k>>2]|0)|0;Ae(v,1,15e3,(j=i,i=i+16|0,c[j>>2]=z,c[j+8>>2]=A,j)|0);i=j}sm(h);B=c[k>>2]|0}else{B=y}if((B|0)!=34){A=c[s>>2]|0;z=ym(34)|0;v=ym(c[k>>2]|0)|0;Ae(A,1,15e3,(j=i,i=i+16|0,c[j>>2]=z,c[j+8>>2]=v,j)|0);i=j}v=gi(c[m>>2]|0,0,c[n>>2]|0)|0;if((v|0)!=0){Ae(c[s>>2]|0,1,20064,(j=i,i=i+8|0,c[j>>2]=c[v+8>>2],j)|0);i=j}v=mi(c[m>>2]|0,l,c[n>>2]|0)|0;sm(h);if((c[k>>2]|0)==0){C=Wf(d,v)|0}else{C=Gj(c[w>>2]|0,v)|0}c[v+68>>2]=C;v=c[k>>2]|0;do{if((v|0)==34){z=c[n>>2]|0;if((a[z]|0)!=100){break}if((Pf(z)|0)==17){break a}}else if((v|0)==3){break a}}while(0);x=x+1|0;y=v}if((x|0)<2){Ae(c[s>>2]|0,1,19968,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}pi(c[m>>2]|0,l,t,q);if((c[k>>2]|0)!=34){D=c[g>>2]|0;dk(D);c[p>>2]=r;E=c[m>>2]|0;si(E,0,o);sm(h);i=f;return}while(1){sm(h);sf(d,1);q=c[k>>2]|0;if((q|0)==34){if((Pf(c[n>>2]|0)|0)==17){continue}}else if((q|0)==3){break}t=c[s>>2]|0;l=ym(q)|0;Ae(t,1,19896,(j=i,i=i+8|0,c[j>>2]=l,j)|0);i=j}D=c[g>>2]|0;dk(D);c[p>>2]=r;E=c[m>>2]|0;si(E,0,o);sm(h);i=f;return}function mf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0;d=i;e=c[a+44>>2]|0;f=a+48|0;ck(c[f>>2]|0,4);Mf(a);mk(c[f>>2]|0,c[a+40>>2]|0);g=e+44|0;if((c[g>>2]|0)!=41){h=c[a+64>>2]|0;j=ym(41)|0;k=ym(c[g>>2]|0)|0;Ae(h,1,15e3,(h=i,i=i+16|0,c[h>>2]=j,c[h+8>>2]=k,h)|0);i=h}sm(e);if((c[g>>2]|0)==28){Nf(a,b);l=c[f>>2]|0;dk(l);i=d;return}else{Of(a,0);l=c[f>>2]|0;dk(l);i=d;return}}function nf(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0;e=i;f=a+48|0;if((b[(c[(c[f>>2]|0)+92>>2]|0)+22>>1]|0)==14){Ae(c[a+64>>2]|0,1,20168,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}Mf(a);g=a+40|0;qk(c[f>>2]|0,c[(c[g>>2]|0)+12>>2]|0);if((d|0)==0){h=c[g>>2]|0;qd(h);i=e;return}Xf(a,19728);h=c[g>>2]|0;qd(h);i=e;return}function of(a,b){a=a|0;b=b|0;Sf(a,13);return}function pf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;d=i;if((b|0)==0){Ae(c[a+64>>2]|0,1,20296,(e=i,i=i+1|0,i=i+7&-8,c[e>>2]=0,e)|0);i=e}f=c[a+44>>2]|0;g=a+48|0;ck(c[g>>2]|0,10);Mf(a);hk(c[g>>2]|0,c[a+40>>2]|0);h=f+44|0;if((c[h>>2]|0)!=41){j=c[a+64>>2]|0;k=ym(41)|0;l=ym(c[h>>2]|0)|0;Ae(j,1,15e3,(e=i,i=i+16|0,c[e>>2]=k,c[e+8>>2]=l,e)|0);i=e}sm(f);if((c[h>>2]|0)==28){Nf(a,b);m=c[g>>2]|0;dk(m);i=d;return}f=c[a+64>>2]|0;l=ym(28)|0;k=ym(c[h>>2]|0)|0;Ae(f,1,15e3,(e=i,i=i+16|0,c[e>>2]=l,c[e+8>>2]=k,e)|0);i=e;Nf(a,b);m=c[g>>2]|0;dk(m);i=d;return}function qf(a,b){a=a|0;b=b|0;var d=0;d=i;Yj(c[a+48>>2]|0);if((b|0)==0){i=d;return}if((c[(c[a+44>>2]|0)+44>>2]|0)==3){i=d;return}Ae(c[a+64>>2]|0,1,20400,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a;i=d;return}function rf(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;d=i;e=c[(c[a+48>>2]|0)+96>>2]|0;do{if((b[e+22>>1]|0)!=15){if((c[e+60>>2]|0)==0){break}Ae(c[a+64>>2]|0,1,20888,(f=i,i=i+1|0,i=i+7&-8,c[f>>2]=0,f)|0);i=f}}while(0);e=c[a+44>>2]|0;g=e+44|0;if((c[g>>2]|0)==34){h=e+12|0;j=c[h>>2]|0;Tf(a,j);Yf(a);i=d;return}k=c[a+64>>2]|0;l=ym(34)|0;m=ym(c[g>>2]|0)|0;Ae(k,1,15e3,(f=i,i=i+16|0,c[f>>2]=l,c[f+8>>2]=m,f)|0);i=f;h=e+12|0;j=c[h>>2]|0;Tf(a,j);Yf(a);i=d;return}function sf(a,b){a=a|0;b=b|0;Zf(a,0);return}function tf(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0;e=i;f=a+48|0;g=b[(c[(c[f>>2]|0)+92>>2]|0)+22>>1]|0;if((g<<16>>16|0)==14){Ae(c[a+64>>2]|0,1,20984,(h=i,i=i+1|0,i=i+7&-8,c[h>>2]=0,h)|0);i=h}else if((g<<16>>16|0)==13){Ae(c[a+64>>2]|0,1,21024,(h=i,i=i+1|0,i=i+7&-8,c[h>>2]=0,h)|0);i=h}h=c[f>>2]|0;do{if((c[h+72>>2]|0)==0){ok(h,0)}else{Mf(a);g=a+40|0;j=c[(c[g>>2]|0)+12>>2]|0;ok(c[f>>2]|0,j);if((j|0)==0){break}qd(c[g>>2]|0)}}while(0);if((d|0)==0){i=e;return}Xf(a,17784);i=e;return}function uf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;d=i;e=a+44|0;f=c[e>>2]|0;g=_f(a)|0;h=a+52|0;j=c[(gi(c[h>>2]|0,0,7968)|0)+24>>2]|0;do{if((g|0)==(j|0)){k=9}else{l=g|0;if((Dh(c[j>>2]|0,c[l>>2]|0)|0)!=0){k=8;break}Ae(c[a+64>>2]|0,1,7904,(m=i,i=i+8|0,c[m>>2]=c[(c[l>>2]|0)+8>>2],m)|0);i=m;k=8}}while(0);j=a+48|0;ek(c[j>>2]|0,k);k=f+44|0;l=c[k>>2]|0;if((l|0)==34){if((nb(c[(c[e>>2]|0)+12>>2]|0,8368)|0)!=0){Ae(c[a+64>>2]|0,1,7872,(m=i,i=i+8|0,c[m>>2]=c[f+12>>2],m)|0);i=m}sm(f);if((c[k>>2]|0)!=34){e=c[a+64>>2]|0;n=ym(34)|0;o=ym(c[k>>2]|0)|0;Ae(e,1,15e3,(m=i,i=i+16|0,c[m>>2]=n,c[m+8>>2]=o,m)|0);i=m}o=f+12|0;n=$h(c[h>>2]|0,0,c[o>>2]|0)|0;if((n|0)!=0){Ae(c[a+64>>2]|0,1,10240,(m=i,i=i+8|0,c[m>>2]=c[n+20>>2],m)|0);i=m}n=Rj(c[j>>2]|0,g,c[o>>2]|0)|0;sm(f);p=n;q=c[k>>2]|0}else{p=0;q=l}if((q|0)!=41){q=c[a+64>>2]|0;l=ym(41)|0;n=ym(c[k>>2]|0)|0;Ae(q,1,15e3,(m=i,i=i+16|0,c[m>>2]=l,c[m+8>>2]=n,m)|0);i=m}$j(c[j>>2]|0,g,p,c[f+16>>2]|0);sm(f);if((c[k>>2]|0)==3){i=d;return}Of(a,b);i=d;return}function vf(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0;d=i;e=a+48|0;f=c[(c[e>>2]|0)+96>>2]|0;do{if((b[f+22>>1]|0)!=15){if((c[f+60>>2]|0)==0){break}Ae(c[a+64>>2]|0,1,8952,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}}while(0);f=a+44|0;h=c[f>>2]|0;j=a+52|0;k=c[j>>2]|0;l=k+20|0;m=c[l>>2]|0;n=h+44|0;o=a+36|0;p=a+64|0;q=h+12|0;while(1){if((c[n>>2]|0)!=34){r=c[p>>2]|0;s=ym(34)|0;t=ym(c[n>>2]|0)|0;Ae(r,1,15e3,(g=i,i=i+16|0,c[g>>2]=s,c[g+8>>2]=t,g)|0);i=g}t=c[(c[f>>2]|0)+12>>2]|0;if((ri(k,c[l>>2]|0,t)|0)!=0){Ae(c[p>>2]|0,1,8832,(g=i,i=i+8|0,c[g>>2]=t,g)|0);i=g}s=qi(k,t)|0;if((s|0)==0){r=$f(a,t)|0;t=c[r+28>>2]|0;if((t|0)==0){u=Sj(c[e>>2]|0,c[o>>2]|0,8720)|0;ck(c[e>>2]|0,15);sm(h);Of(a,1);if((c[n>>2]|0)==3){Ae(c[p>>2]|0,1,8600,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}v=c[e>>2]|0;if((b[(c[v+96>>2]|0)+22>>1]|0)==15){w=v}else{Ae(c[p>>2]|0,1,8480,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g;w=c[e>>2]|0}dk(w);qm(c[f>>2]|0);Vj(c[e>>2]|0,u)}else{c[r+32>>2]=c[t+4>>2]}c[(c[j>>2]|0)+20>>2]=m;x=r}else{x=s}sm(c[f>>2]|0);do{if((c[n>>2]|0)==34){if((nb(c[q>>2]|0,8368)|0)!=0){y=23;break}sm(h);if((c[n>>2]|0)!=34){s=c[p>>2]|0;r=ym(34)|0;t=ym(c[n>>2]|0)|0;Ae(s,1,15e3,(g=i,i=i+16|0,c[g>>2]=r,c[g+8>>2]=t,g)|0);i=g}ag(m,x,c[q>>2]|0);sm(h)}else{y=23}}while(0);if((y|0)==23){y=0;ag(m,x,0)}if((c[n>>2]|0)!=2){break}sm(c[f>>2]|0)}i=d;return}function wf(a,b){a=a|0;b=b|0;bg(a,17216,4096);return}function xf(a,b){a=a|0;b=b|0;Sf(a,22);return}function yf(a,b){a=a|0;b=b|0;Sf(a,23);return}function zf(a,b){a=a|0;b=b|0;bg(a,16592,8192);return}function Af(a,b){a=a|0;b=b|0;var d=0;d=i;Zj(c[a+48>>2]|0);if((b|0)==0){i=d;return}if((c[(c[a+44>>2]|0)+44>>2]|0)==3){i=d;return}Ae(c[a+64>>2]|0,1,11184,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a;i=d;return}function Bf(a,b){a=a|0;b=b|0;Sf(a,26);return}function Cf(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0;e=Vd(48)|0;f=e;g=a+8|0;h=c[g>>2]|0;if((h|0)==0){c[a+12>>2]=f}else{c[h+40>>2]=f}c[g>>2]=f;g=Vd((_n(b|0)|0)+1|0)|0;c[e+8>>2]=g;Zn(g|0,b|0)|0;b=Vd((_n(d|0)|0)+1|0)|0;c[e+12>>2]=b;Zn(b|0,d|0)|0;d=e;fo(e+16|0,0,28)|0;c[d>>2]=64;c[d+4>>2]=0;return f|0}function Df(a){a=a|0;var b=0,c=0,d=0,e=0,f=0,g=0;b=Fa(a|0,59)|0;if((b|0)==0){c=0;return c|0}else{d=0;e=a;f=b}while(1){b=cg(d,e,f-e|0)|0;a=f+1|0;g=Fa(a|0,59)|0;if((g|0)==0){c=b;break}else{d=b;e=a;f=g}}return c|0}function Ef(a){a=a|0;var b=0;b=c[a+44>>2]|0;vm(b,16104,1,18648);sm(b);Of(a,1);qm(b);return}function Ff(a,b){a=a|0;b=b|0;var d=0;d=a+44|0;wm(c[d>>2]|0,18928,1,b);sm(c[d>>2]|0);b=_f(a)|0;qm(c[d>>2]|0);return b|0}function Gf(a){a=a|0;var b=0;if((a|0)==0){return}else{b=a}while(1){a=c[b+4>>2]|0;Ln(c[b>>2]|0);Ln(b);if((a|0)==0){break}else{b=a}}return}function Hf(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0;e=c[a+52>>2]|0;f=e+20|0;g=c[f>>2]|0;h=b;i=dg(h,d)|0;if((i|0)==0){j=0;return j|0}c[f>>2]=b;d=i+8|0;k=c[d>>2]|0;l=c[d+4>>2]|0;d=3;m=0;n=0;o=0;p=1;q=0;if((k|0)==2&(l|0)==0){r=Ff(a,c[i+16>>2]|0)|0;s=Uj(c[a+48>>2]|0,b,r,c[i+4>>2]|0)|0;rb[c[b+36>>2]&127](a,s);t=s}else if((k|0)==(p|0)&(l|0)==(q|0)){t=eg(a,h,i)|0}else if((k|0)==(n|0)&(l|0)==(o|0)){t=ei(e,i)|0}else if((k|0)==(d|0)&(l|0)==(m|0)){t=If(a,b,c[i+4>>2]|0)|0}else{t=0}c[f>>2]=g;j=t;return j|0}function If(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0;e=i;f=(c[a+52>>2]|0)+20|0;g=c[f>>2]|0;c[f>>2]=b;b=a+32|0;qn(c[b>>2]|0);tn(c[b>>2]|0,19e3,(h=i,i=i+8|0,c[h>>2]=d,h)|0);i=h;h=a+44|0;vm(c[h>>2]|0,18976,1,c[c[b>>2]>>2]|0);sm(c[h>>2]|0);sm(c[h>>2]|0);b=c[a+40>>2]|0;Ld(b);Yf(a);a=c[(c[f>>2]|0)+20>>2]|0;Md(b);qm(c[h>>2]|0);c[f>>2]=g;i=e;return a|0}function Jf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;e=c[a+44>>2]|0;f=e+12|0;if(($h(c[a+52>>2]|0,0,c[f>>2]|0)|0)!=0){Ae(c[a+64>>2]|0,1,10240,(g=i,i=i+8|0,c[g>>2]=c[f>>2],g)|0);i=g}g=Rj(c[a+48>>2]|0,b,c[f>>2]|0)|0;sm(e);i=d;return g|0}function Kf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;d=a+48|0;e=c[(c[d>>2]|0)+104>>2]|0;f=c[a+44>>2]|0;sm(f);g=f+44|0;h=f+12|0;f=a+40|0;i=-1;a:while(1){j=i;k=c[g>>2]|0;while(1){if((k|0)==34){l=Pf(c[h>>2]|0)|0}else{l=j}if((l|0)==-1){break}if((fg(l)|0)!=0){break}Of(a,0);m=c[g>>2]|0;if((m|0)==3){n=0;break a}else{j=-1;k=m}}Mf(a);o=c[d>>2]|0;p=c[f>>2]|0;if((c[g>>2]|0)==3){q=9;break}kk(o,p);i=l}do{if((q|0)==9){nk(o,p,b);l=c[c[(c[f>>2]|0)+12>>2]>>2]|0;if((l|0)==0){n=0;break}n=c[l+8>>2]|0}}while(0);c[(c[d>>2]|0)+104>>2]=e;return n|0}function Lf(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0;d=i;e=a+26|0;f=a+44|0;if((b[e>>1]|0)==0){g=a+52|0}else{h=Cf(a,21136,c[c[c[f>>2]>>2]>>2]|0)|0;j=a+52|0;c[(c[(c[j>>2]|0)+32>>2]|0)+20>>2]=h;c[(c[j>>2]|0)+20>>2]=h;b[e>>1]=0;g=j}j=c[f>>2]|0;sm(j);e=j+44|0;h=a+64|0;k=a+48|0;l=a+12|0;m=a+56|0;n=a+24|0;o=a+40|0;a:while(1){p=c[e>>2]|0;switch(p|0){case 34:{Of(a,1);continue a;break};case 3:{dk(c[k>>2]|0);sm(j);continue a;break};case 51:case 52:{q=c[k>>2]|0;if((c[(c[q+96>>2]|0)+60>>2]|0)==0){r=q}else{Ae(c[h>>2]|0,1,13640,(s=i,i=i+1|0,i=i+7&-8,c[s>>2]=0,s)|0);i=s;r=c[k>>2]|0}sk(r,c[l>>2]|0);c[(c[m>>2]|0)+92>>2]=c[(c[k>>2]|0)+64>>2];Fc(c[m>>2]|0,c[g>>2]|0);b[n>>1]=1;Cc(c[m>>2]|0);b[n>>1]=0;rk(c[k>>2]|0);if((c[e>>2]|0)!=51){t=15;break a}xm(c[f>>2]|0);if((c[e>>2]|0)==52){t=15;break a}sm(j);continue a;break};case 38:case 39:case 36:case 0:case 4:case 37:case 30:{Mf(a);kk(c[k>>2]|0,c[o>>2]|0);continue a;break};default:{q=c[h>>2]|0;u=ym(p)|0;Ae(q,1,13512,(s=i,i=i+8|0,c[s>>2]=u,s)|0);i=s;continue a}}}if((t|0)==15){i=d;return}}function Mf(a){a=a|0;gg(a,1);return}function Nf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0;d=i;e=c[a+44>>2]|0;if((b|0)==0){Ae(c[a+64>>2]|0,1,10736,(f=i,i=i+1|0,i=i+7&-8,c[f>>2]=0,f)|0);i=f}sm(e);b=e+44|0;if((c[b>>2]|0)==3){sm(e);i=d;return}Of(a,1);if((c[b>>2]|0)==3){sm(e);i=d;return}g=c[a+64>>2]|0;a=ym(3)|0;h=ym(c[b>>2]|0)|0;Ae(g,1,15e3,(f=i,i=i+16|0,c[f>>2]=a,c[f+8>>2]=h,f)|0);i=f;sm(e);i=d;return}function Of(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;d=i;e=c[a+44>>2]|0;f=e+44|0;g=e+12|0;h=a+48|0;j=a+40|0;k=(b|0)==0;a:do{if(k){l=c[f>>2]|0;switch(l|0){case 34:{break};case 39:case 38:case 37:case 36:case 35:case 30:case 4:case 0:{Mf(a);kk(c[h>>2]|0,c[j>>2]|0);i=d;return};default:{m=l;break a}}l=Pf(c[g>>2]|0)|0;if((l|0)==-1){Mf(a);kk(c[h>>2]|0,c[j>>2]|0);i=d;return}else{sm(e);rb[c[5696+(l<<2)>>2]&127](a,0);i=d;return}}else{b:while(1){n=c[f>>2]|0;switch(n|0){case 34:{break};case 39:case 38:case 37:case 36:case 35:case 30:case 4:case 0:{Mf(a);kk(c[h>>2]|0,c[j>>2]|0);continue b;break};default:{break b}}l=Pf(c[g>>2]|0)|0;if((l|0)==-1){Mf(a);kk(c[h>>2]|0,c[j>>2]|0);continue}else{sm(e);rb[c[5696+(l<<2)>>2]&127](a,b);continue}}if(k){m=n;break}i=d;return}}while(0);n=c[a+64>>2]|0;a=ym(m)|0;Ae(n,1,11344,(n=i,i=i+8|0,c[n>>2]=a,n)|0);i=n;i=d;return}function Pf(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0;b=hg(a)|0;d=G;e=0;while(1){f=4696+(e<<4)|0;g=c[f>>2]|0;h=c[f+4>>2]|0;if((g|0)==(b|0)&(h|0)==(d|0)){if((nb(c[4688+(e<<4)>>2]|0,a|0)|0)==0){i=e;j=5;break}}f=e+1|0;if(h>>>0<=d>>>0&(h>>>0<d>>>0|g>>>0<=b>>>0)&(f|0)<27){e=f}else{i=-1;j=5;break}}if((j|0)==5){return i|0}return 0}function Qf(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0;e=i;f=a+44|0;g=c[f>>2]|0;h=d|256;d=a+48|0;j=(b[(c[(c[d>>2]|0)+96>>2]|0)+22>>1]|0)==13;k=j?35:34;l=j?34:35;m=g+44|0;n=a+64|0;o=a+40|0;p=a+28|0;a:while(1){q=c[m>>2]|0;if((q|0)==(l|0)){ig(c[(c[f>>2]|0)+44>>2]|0,c[n>>2]|0);r=c[m>>2]|0}else{r=q}if((r|0)==(k|0)){s=k}else{q=c[n>>2]|0;t=ym(k)|0;u=ym(c[m>>2]|0)|0;Ae(q,1,15e3,(v=i,i=i+16|0,c[v>>2]=t,c[v+8>>2]=u,v)|0);i=v;s=c[m>>2]|0}do{if((s|0)==34){u=Jf(a,0)|0;t=u;q=u|0;w=c[q+4>>2]|0;c[q>>2]=c[q>>2]|256;c[q+4>>2]=w;w=c[o>>2]|0;if((b[(c[d>>2]|0)+102>>1]|0)==1){zd(w,u);x=t;break}else{yd(w,u);x=t;break}}else{t=jg(a,h)|0;Hd(c[o>>2]|0,t);x=t}}while(0);t=c[m>>2]|0;if((t|0)==41){sm(g);c[x+8>>2]=_f(a)|0;y=c[m>>2]|0}else{y=t}if((y|0)!=26){Ae(c[n>>2]|0,1,9624,(v=i,i=i+1|0,i=i+7&-8,c[v>>2]=0,v)|0);i=v}vd(c[o>>2]|0,21);sm(g);Mf(a);kk(c[d>>2]|0,c[o>>2]|0);do{if(j){if((b[(c[x+8>>2]|0)+14>>1]&2)==0){break}Bj(c[c[p>>2]>>2]|0)}}while(0);t=c[m>>2]|0;switch(t|0){case 2:{break};case 52:case 51:case 35:case 34:case 3:{break a;break};default:{u=c[n>>2]|0;w=ym(t)|0;Ae(u,1,9552,(v=i,i=i+8|0,c[v>>2]=w,v)|0);i=v}}sm(g)}i=e;return}function Rf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;e=c[a+40>>2]|0;Mf(a);f=c[(c[e+12>>2]|0)+4>>2]|0;if((f&65535|0)==22&f>>>0>1376255>>>0){Ae(c[a+64>>2]|0,1,19160,(f=i,i=i+1|0,i=i+7&-8,c[f>>2]=0,f)|0);i=f}f=a+48|0;g=Rj(c[f>>2]|0,c[(c[(c[a+52>>2]|0)+44>>2]|0)+24>>2]|0,b)|0;lk(c[f>>2]|0,e,g);i=d;return g|0}function Sf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=kg(a,b)|0;b=d|0;e=a+40|0;f=c[e>>2]|0;if((c[b>>2]&1|0)==0&(c[b+4>>2]&0|0)==0){Jd(f)}else{Fd(f,d)}gg(a,2);kk(c[a+48>>2]|0,c[e>>2]|0);return}function Tf(d,e){d=d|0;e=e|0;var f=0,g=0,h=0;f=i;if((a[e+1|0]|0)==0){Ae(c[d+64>>2]|0,1,20544,(g=i,i=i+8|0,c[g>>2]=e,g)|0);i=g}h=c[(c[d+48>>2]|0)+96>>2]|0;do{if((b[h+22>>1]|0)!=15){if((c[h+60>>2]|0)==0){break}Ae(c[d+64>>2]|0,1,20504,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}}while(0);if((gi(c[d+52>>2]|0,0,e)|0)==0){i=f;return}Ae(c[d+64>>2]|0,1,20464,(g=i,i=i+8|0,c[g>>2]=e,g)|0);i=g;i=f;return}function Uf(d){d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;e=i;i=i+8|0;f=e|0;b[f>>1]=65;g=f;h=c[d+44>>2]|0;j=h+44|0;k=h+12|0;l=d+64|0;m=65;while(1){sm(h);if((c[j>>2]|0)!=34){n=c[l>>2]|0;o=ym(34)|0;p=ym(c[j>>2]|0)|0;Ae(n,1,15e3,(q=i,i=i+16|0,c[q>>2]=o,c[q+8>>2]=p,q)|0);i=q}p=c[k>>2]|0;if((a[p]|0)==m<<24>>24){if((a[p+1|0]|0)!=0){r=6}}else{r=6}if((r|0)==6){r=0;a[g]=m;Ae(c[l>>2]|0,1,9776,(q=i,i=i+16|0,c[q>>2]=f,c[q+8>>2]=p,q)|0);i=q}s=m+1&255;sm(h);p=c[j>>2]|0;if((p|0)==2){m=s;continue}else if((p|0)==32){break}o=c[l>>2]|0;n=ym(p)|0;Ae(o,1,13048,(q=i,i=i+8|0,c[q>>2]=n,q)|0);i=q;m=s}sm(h);h=(s<<24>>24)-65|0;Ch(c[(c[d+48>>2]|0)+120>>2]|0,h);i=e;return h|0}function Vf(d,e,f){d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0;g=i;i=i+8|0;h=g|0;if((f|0)==0){j=Gj(c[d+60>>2]|0,e)|0;k=e+68|0;c[k>>2]=j;i=g;return j|0}b[h>>1]=65;l=d+52|0;m=h;h=d+60|0;d=f;do{f=gi(c[l>>2]|0,0,m)|0;yj(c[h>>2]|0,c[f+24>>2]|0);n=(a[m]|0)+1&255;a[m]=n;d=d-1|0;}while((d|0)!=0);j=Cj(c[h>>2]|0,0,e,(n<<24>>24)-65|0)|0;k=e+68|0;c[k>>2]=j;i=g;return j|0}function Wf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;d=i;i=i+8|0;e=d|0;f=c[a+44>>2]|0;sm(f);g=f+44|0;h=a+64|0;if((c[g>>2]|0)==1){Ae(c[h>>2]|0,1,19832,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}k=a+60|0;l=c[k>>2]|0;m=c[l+4>>2]|0;c[e>>2]=0;yj(l,0);l=2;while(1){n=c[k>>2]|0;yj(n,lg(a,e)|0);o=c[e>>2]|0;if((o&32|0)!=0){Ae(c[h>>2]|0,1,19776,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}n=c[g>>2]|0;if((n|0)==1){break}else if((n|0)==2){sm(f)}else{p=c[h>>2]|0;q=ym(n)|0;Ae(p,1,10352,(j=i,i=i+8|0,c[j>>2]=q,j)|0);i=j}l=l+1|0}sm(f);f=Dj(c[k>>2]|0,b,m,l)|0;zj(c[k>>2]|0,m,f);f=Cj(c[k>>2]|0,o,c[(c[a+52>>2]|0)+68>>2]|0,l)|0;i=d;return f|0}function Xf(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;e=c[a+44>>2]|0;f=c[e+44>>2]|0;if((f|0)==52|(f|0)==51|(f|0)==3){i=d;return}else if((f|0)==34){g=2}do{if((g|0)==2){f=Pf(c[e+12>>2]|0)|0;if(!((f|0)==19|(f|0)==8|(f|0)==6|(f|0)==5)){break}i=d;return}}while(0);Ae(c[a+64>>2]|0,1,20920,(a=i,i=i+8|0,c[a>>2]=b,a)|0);i=a;i=d;return}function Yf(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0;b=i;d=c[a+44>>2]|0;e=a+52|0;f=fi(c[e>>2]|0,c[d+12>>2]|0)|0;g=a+28|0;h=c[g>>2]|0;mg(a,f);j=d+44|0;d=c[j>>2]|0;if((d|0)==18){ng(a,f);k=c[j>>2]|0}else{k=d}if((k|0)!=28){k=c[a+64>>2]|0;d=ym(28)|0;l=ym(c[j>>2]|0)|0;Ae(k,1,15e3,(k=i,i=i+16|0,c[k>>2]=d,c[k+8>>2]=l,k)|0);i=k}Nf(a,1);li(c[e>>2]|0,f);c[g>>2]=h;dk(c[a+48>>2]|0);i=b;return}function Zf(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0;e=i;f=a+48|0;g=c[(c[f>>2]|0)+96>>2]|0;h=b[g+22>>1]|0;do{if(!((h<<16>>16|0)==15|(h<<16>>16|0)==12|(h<<16>>16|0)==13|(h<<16>>16|0)==11)){if((c[g+60>>2]|0)==0){break}Ae(c[a+64>>2]|0,1,10848,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}}while(0);g=c[a+44>>2]|0;og(a,d);d=g+44|0;if((c[d>>2]|0)!=28){g=c[a+64>>2]|0;h=ym(28)|0;k=ym(c[d>>2]|0)|0;Ae(g,1,15e3,(j=i,i=i+16|0,c[j>>2]=h,c[j+8>>2]=k,j)|0);i=j}Nf(a,1);dk(c[f>>2]|0);j=b[(c[(c[f>>2]|0)+96>>2]|0)+22>>1]|0;if(!((j<<16>>16|0)==13|(j<<16>>16|0)==11)){i=e;return}j=c[a+52>>2]|0;ii(j,c[c[a+28>>2]>>2]|0,c[(c[j+20>>2]|0)+24>>2]|0);i=e;return}function _f(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;d=i;i=i+8|0;e=d|0;f=c[a+44>>2]|0;g=pg(a)|0;h=g|0;if(!((c[h>>2]&4096|0)==0&(c[h+4>>2]&0|0)==0)){Ae(c[a+64>>2]|0,1,13160,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}if((b[g+58>>1]|0)==0){k=c[g+24>>2]|0;sm(f);i=d;return k|0}h=(b[g+52>>1]|0)==5;sm(f);l=f+44|0;m=c[l>>2]|0;if(!h){h=a+64|0;if((m|0)!=4){n=c[h>>2]|0;o=ym(4)|0;p=ym(c[l>>2]|0)|0;Ae(n,1,15e3,(j=i,i=i+16|0,c[j>>2]=o,c[j+8>>2]=p,j)|0);i=j}p=a+60|0;o=1;while(1){sm(f);n=c[p>>2]|0;yj(n,_f(a)|0);n=c[l>>2]|0;if((n|0)==32){break}else if((n|0)!=2){q=c[h>>2]|0;r=ym(n)|0;Ae(q,1,13048,(j=i,i=i+8|0,c[j>>2]=r,j)|0);i=j}o=o+1|0}h=Cj(c[p>>2]|0,0,g,o)|0;qg(a,h);k=h;sm(f);i=d;return k|0}if((m|0)!=0){m=c[a+64>>2]|0;h=ym(0)|0;o=ym(c[l>>2]|0)|0;Ae(m,1,15e3,(j=i,i=i+16|0,c[j>>2]=h,c[j+8>>2]=o,j)|0);i=j}sm(f);c[e>>2]=0;o=a+60|0;h=c[o>>2]|0;m=c[h+4>>2]|0;yj(h,0);h=c[l>>2]|0;do{if((h|0)==33|(h|0)==1){s=1;t=h}else{p=c[o>>2]|0;yj(p,lg(a,e)|0);p=c[l>>2]|0;if((p|0)==2){u=1}else{s=2;t=p;break}while(1){sm(f);p=c[o>>2]|0;yj(p,lg(a,e)|0);v=c[l>>2]|0;if((v|0)==2){u=u+1|0}else{break}}s=u+2|0;t=v}}while(0);if((t|0)==33){sm(f);v=c[o>>2]|0;zj(v,m,_f(a)|0);w=c[l>>2]|0}else{w=t}if((w|0)!=1){w=c[a+64>>2]|0;a=ym(1)|0;t=ym(c[l>>2]|0)|0;Ae(w,1,15e3,(j=i,i=i+16|0,c[j>>2]=a,c[j+8>>2]=t,j)|0);i=j}k=Cj(c[o>>2]|0,c[e>>2]|0,g,s)|0;sm(f);i=d;return k|0}function $f(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0;d=i;e=a|0;f=sg(a,c[e>>2]|0,b,8344,2)|0;do{if((f|0)==0){g=sg(a,c[e>>2]|0,b,8232,10)|0;if((g|0)!=0){h=g;break}g=a+32|0;j=c[g>>2]|0;qn(j);tn(j,8176,(k=i,i=i+8|0,c[k>>2]=b,k)|0);i=k;tn(j,8080,(k=i,i=i+8|0,c[k>>2]=b,k)|0);i=k;ug(j,c[e>>2]|0,b,8344);ug(j,c[a+4>>2]|0,b,8232);Ae(c[a+64>>2]|0,1,c[c[g>>2]>>2]|0,(k=i,i=i+1|0,i=i+7&-8,c[k>>2]=0,k)|0);i=k;h=0}else{h=f}}while(0);c[(c[a+52>>2]|0)+20>>2]=h;i=d;return h|0}function ag(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;e=Vd(12)|0;if((d|0)==0){f=0}else{g=Vd((_n(d|0)|0)+1|0)|0;Zn(g|0,d|0)|0;f=g}c[e>>2]=b;b=a+16|0;c[e+8>>2]=c[b>>2];c[e+4>>2]=f;c[b>>2]=e;return}function bg(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;f=i;if((b[(c[(c[a+48>>2]|0)+96>>2]|0)+22>>1]|0)!=13){Ae(c[a+64>>2]|0,1,11072,(g=i,i=i+8|0,c[g>>2]=d,g)|0);i=g}d=a+44|0;h=c[d>>2]|0;j=h+44|0;if((c[j>>2]|0)==34){k=h}else{l=c[a+64>>2]|0;m=ym(34)|0;n=ym(c[j>>2]|0)|0;Ae(l,1,15e3,(g=i,i=i+16|0,c[g>>2]=m,c[g+8>>2]=n,g)|0);i=g;k=c[d>>2]|0}d=Pf(c[k+12>>2]|0)|0;if((d|0)==2){sm(h);Qf(a,e);i=f;return}else if((d|0)==17){sm(h);Zf(a,e);i=f;return}else{Ae(c[a+64>>2]|0,1,10944,(g=i,i=i+8|0,c[g>>2]=c[h+12>>2],g)|0);i=g;i=f;return}}function cg(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0;f=Vd(8)|0;g=Vd(e+1|0)|0;bo(g|0,d|0,e|0)|0;a[g+e|0]=0;c[f>>2]=g;c[f+4>>2]=b;return f|0}function dg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0;d=a|0;e=c[((c[d>>2]&64|0)==0&(c[d+4>>2]&0|0)==0?a+80|0:a+32|0)>>2]|0;if((e|0)==0){f=0;g=f;return g|0}else{h=e}while(1){if((nb(c[h+4>>2]|0,b|0)|0)==0){f=h;i=4;break}e=c[h>>2]|0;if((e|0)==0){f=0;i=4;break}else{h=e}}if((i|0)==4){g=f;return g|0}return 0}function eg(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0;f=i;i=i+8|0;g=f|0;h=a+44|0;j=c[h>>2]|0;k=a+52|0;l=c[k>>2]|0;m=c[l+20>>2]|0;n=a+48|0;o=e[(c[(c[n>>2]|0)+96>>2]|0)+14>>1]|0;vm(j,16104,1,c[d+16>>2]|0);sm(j);p=b|0;if((c[p>>2]&64|0)==0&(c[p+4>>2]&0|0)==0){c[(c[k>>2]|0)+20>>2]=c[b+72>>2]}else{c[(c[k>>2]|0)+20>>2]=b}if((c[(c[h>>2]|0)+44>>2]|0)==4){q=Uf(a)|0}else{q=0}si(l,0,q);q=a+60|0;h=c[q>>2]|0;p=c[h+4>>2]|0;c[g>>2]=0;yj(h,0);h=j+44|0;r=c[h>>2]|0;if((r|0)==0){sm(j);s=a+64|0;t=2;while(1){u=c[q>>2]|0;yj(u,lg(a,g)|0);u=c[h>>2]|0;if((u|0)==2){sm(j)}else if((u|0)==1){break}else{v=c[s>>2]|0;w=ym(u)|0;Ae(v,1,10352,(v=i,i=i+8|0,c[v>>2]=w,v)|0);i=v}t=t+1|0}sm(j);x=t;y=c[h>>2]|0}else{x=1;y=r}if((y|0)==41){sm(j);y=c[q>>2]|0;zj(y,p,_f(a)|0)}a=Cj(c[q>>2]|0,c[g>>2]|0,c[(c[k>>2]|0)+68>>2]|0,x)|0;x=Tj(c[n>>2]|0,c[d+20>>2]|0,b,a,c[d+4>>2]|0)|0;si(l,0,o);c[(c[k>>2]|0)+20>>2]=m;qm(j);i=f;return x|0}function fg(a){a=a|0;var b=0;switch(a|0){case 7:case 13:case 22:case 26:case 23:case 9:{b=1;break};default:{b=0}}return b|0}function gg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;d=i;i=i+8|0;e=d|0;f=a+44|0;g=c[f>>2]|0;h=(b|0)==4;c[e>>2]=h?3:b;b=g+44|0;j=a+40|0;k=a+64|0;a:while(1){l=c[b>>2]|0;m=c[1312+(l*12|0)>>2]|0;b:do{if((l|0)==34){if((c[e>>2]|0)!=2){vg(a,e);n=51;break}if((c[(c[j>>2]|0)+32>>2]|0)==0){n=5;break a}c[e>>2]=6;n=52}else{if((m|0)!=-1){if((c[e>>2]|0)==2){vd(c[j>>2]|0,m);c[e>>2]=1;n=53;break}if((l|0)==16){wg(a,e);n=51;break}else{c[e>>2]=6;n=52;break}}switch(l|0){case 29:{if((c[e>>2]|0)==2){sd(c[j>>2]|0,0)}o=c[f>>2]|0;Kd(c[j>>2]|0,c[o+40>>2]|0,c[o+32>>2]|0);if((c[e>>2]|0)==2){td(c[j>>2]|0)}c[e>>2]=2;n=53;break b;break};case 30:{if((c[e>>2]|0)==2){n=27;break a}sd(c[j>>2]|0,12);c[e>>2]=3;n=53;break b;break};case 35:{if((c[e>>2]|0)==2){n=24;break a}xg(a,e);n=51;break b;break};case 4:{o=c[e>>2]|0;if((o&-3|0)==1){sd(c[j>>2]|0,2);c[e>>2]=3;n=53;break b}if((o|0)!=2){n=51;break b}sd(c[j>>2]|0,1);c[e>>2]=1;n=53;break b;break};case 38:case 39:case 36:case 37:{zg(a,e);n=51;break b;break};case 40:{Ag(a,e);n=51;break b;break};case 16:case 6:{wg(a,e);n=51;break b;break};case 0:{o=c[e>>2]|0;if((o&-3|0)==1){sd(c[j>>2]|0,4);c[e>>2]=1;n=53;break b}if((o|0)!=2){n=51;break b}sd(c[j>>2]|0,0);c[e>>2]=3;n=53;break b;break};case 1:case 32:case 31:{do{if((c[e>>2]|0)!=1){if((c[(c[j>>2]|0)+32>>2]|0)==0){break}yg(a);td(c[j>>2]|0);do{if(h){if((c[b>>2]|0)!=1){break}if((c[(c[j>>2]|0)+32>>2]|0)==0){n=36;break a}}}while(0);c[e>>2]=2;n=53;break b}}while(0);c[e>>2]=6;n=52;break b;break};default:{if((c[1308+(l*12|0)>>2]|0)!=0){if((c[(c[j>>2]|0)+32>>2]|0)==0&(c[e>>2]|0)==2){n=47;break a}}if((l|0)==2|(l|0)==33){Bg(a,e);n=51;break b}else{c[e>>2]=6;n=52;break b}}}}}while(0);if((n|0)==51){n=0;l=c[e>>2]|0;if((l|0)==6){n=52}else if((l|0)==5){n=54;break}else{n=53}}if((n|0)==52){n=0;l=c[k>>2]|0;m=ym(c[b>>2]|0)|0;Ae(l,1,14568,(l=i,i=i+8|0,c[l>>2]=m,l)|0);i=l;continue}else if((n|0)==53){n=0;sm(g);continue}}if((n|0)==5){c[e>>2]=5;i=d;return}else if((n|0)==24){c[e>>2]=5;i=d;return}else if((n|0)==27){c[e>>2]=5;i=d;return}else if((n|0)==36){c[e>>2]=5;i=d;return}else if((n|0)==47){c[e>>2]=5;i=d;return}else if((n|0)==54){i=d;return}}function hg(b){b=b|0;var c=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;c=a[b]|0;if(c<<24>>24==0){d=0;e=0;return(G=d,e)|0}else{f=b;g=0;h=0;i=0;j=0;k=c}while(1){c=ko(k<<24>>24|0,(k<<24>>24<0|0?-1:0)|0,h|0)|0;b=c|j;c=G|i;l=f+1|0;m=g+1|0;n=a[l]|0;if(n<<24>>24!=0&(m|0)!=8){f=l;g=m;h=h+8|0;i=c;j=b;k=n}else{d=c;e=b;break}}return(G=d,e)|0}function ig(a,b){a=a|0;b=b|0;var d=0;d=i;Ae(b,1,(a|0)==34?9232:9088,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a;i=d;return}function jg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;d=i;e=a+44|0;f=c[(c[e>>2]|0)+12>>2]|0;g=c[c[a+28>>2]>>2]|0;if((ji(g,f)|0)!=0){h=c[g+8>>2]|0;Ae(c[a+64>>2]|0,1,9480,(j=i,i=i+16|0,c[j>>2]=f,c[j+8>>2]=h,j)|0);i=j}if((hi(g,f)|0)==0){k=a+52|0;l=c[k>>2]|0;m=b&-257;n=ki(l,g,0,f,m)|0;o=c[e>>2]|0;sm(o);i=d;return n|0}Ae(c[a+64>>2]|0,1,9360,(j=i,i=i+16|0,c[j>>2]=c[g+8>>2],c[j+8>>2]=f,j)|0);i=j;k=a+52|0;l=c[k>>2]|0;m=b&-257;n=ki(l,g,0,f,m)|0;o=c[e>>2]|0;sm(o);i=d;return n|0}function kg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0;d=i;e=c[a+52>>2]|0;switch(b|0){case 22:{b=c[a+44>>2]|0;while(1){f=c[b>>2]|0;g=c[f>>2]|0;if((nb(g|0,15520)|0)==0){b=f+48|0}else{break}}h=Xh(e,g)|0;i=d;return h|0};case 23:{h=Vh(e,c[(c[a+44>>2]|0)+16>>2]|0,0)|0;i=d;return h|0};case 26:{h=Xh(e,c[(c[(c[a+48>>2]|0)+68>>2]|0)+20>>2]|0)|0;i=d;return h|0};case 7:{h=Uh(e,1,0)|0;i=d;return h|0};case 9:{if((c[a+28>>2]|0)==0){Ae(c[a+64>>2]|0,1,11624,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}h=c[(c[(c[a+48>>2]|0)+96>>2]|0)+52>>2]|0;i=d;return h|0};case 13:{h=Uh(e,0,0)|0;i=d;return h|0};default:{h=0;i=d;return h|0}}return 0}function lg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0;d=i;e=c[a+44>>2]|0;f=e+44|0;g=c[b>>2]|0;do{if((c[f>>2]|0)==10){c[b>>2]=g|32;sm(e)}else{if((g&32|0)==0){break}Ae(c[a+64>>2]|0,1,12920,(h=i,i=i+1|0,i=i+7&-8,c[h>>2]=0,h)|0);i=h}}while(0);g=_f(a)|0;if((c[b>>2]&32|0)!=0){j=c[g>>2]|0;if((c[j>>2]&512|0)==0&(c[j+4>>2]&0|0)==0){Ae(c[a+64>>2]|0,1,12704,(h=i,i=i+8|0,c[h>>2]=g,h)|0);i=h}k=Cg(a,c[(c[a+52>>2]|0)+84>>2]|0,g)|0;i=d;return k|0}if((c[f>>2]|0)!=48){k=g;i=d;return k|0}j=Cg(a,c[(c[a+52>>2]|0)+72>>2]|0,g)|0;sm(e);e=c[f>>2]|0;if(!((e|0)==33|(e|0)==1)){Ae(c[a+64>>2]|0,1,12608,(h=i,i=i+1|0,i=i+7&-8,c[h>>2]=0,h)|0);i=h}c[b>>2]=c[b>>2]|1;k=j;i=d;return k|0}function mg(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;f=i;i=i+8|0;g=f|0;h=c[a+44>>2]|0;j=a+52|0;k=c[j>>2]|0;l=a+48|0;m=Sj(c[l>>2]|0,c[a+36>>2]|0,20736)|0;sm(h);n=h+44|0;if((c[n>>2]|0)==4){o=Uf(a)|0}else{o=e[(c[(c[l>>2]|0)+96>>2]|0)+14>>1]|0}si(k,d,o);ck(c[l>>2]|0,13);k=Vf(a,d,o)|0;d=a+28|0;c[d>>2]=k;c[g>>2]=0;p=a+60|0;yj(c[p>>2]|0,k);if((c[n>>2]|0)==0){sm(h);k=a+64|0;if((c[n>>2]|0)==1){Ae(c[k>>2]|0,1,20592,(q=i,i=i+1|0,i=i+7&-8,c[q>>2]=0,q)|0);i=q;r=2}else{r=2}while(1){s=c[p>>2]|0;yj(s,Dg(a,g)|0);s=c[n>>2]|0;if((s|0)==2){sm(h)}else if((s|0)==1){break}else{t=c[k>>2]|0;u=ym(s)|0;Ae(t,1,10352,(q=i,i=i+8|0,c[q>>2]=u,q)|0);i=q}r=r+1|0}sm(h);v=r;w=c[g>>2]|0}else{v=1;w=0}g=Cj(c[p>>2]|0,w,c[(c[j>>2]|0)+68>>2]|0,v)|0;c[m+8>>2]=g;pk(c[l>>2]|0,c[d>>2]|0,o,c[c[g+4>>2]>>2]|0);g=a+20|0;o=b[g>>1]|0;if(o<<16>>16==0){i=f;return}Wj(c[l>>2]|0,c[a+16>>2]|0,o);b[g>>1]=0;i=f;return}function ng(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;e=i;f=c[a+44>>2]|0;sm(f);g=f+44|0;if((c[g>>2]|0)!=34){h=c[a+64>>2]|0;j=ym(34)|0;k=ym(c[g>>2]|0)|0;Ae(h,1,15e3,(l=i,i=i+16|0,c[l>>2]=j,c[l+8>>2]=k,l)|0);i=l}k=pg(a)|0;do{if((k|0)==0){Ae(c[a+64>>2]|0,1,12240,(l=i,i=i+8|0,c[l>>2]=c[f+12>>2],l)|0);i=l}else{if((k|0)==(d|0)){Ae(c[a+64>>2]|0,1,20792,(l=i,i=i+1|0,i=i+7&-8,c[l>>2]=0,l)|0);i=l;break}if((b[k+54>>1]|0)==0){j=k|0;if((c[j>>2]&5120|0)==0&(c[j+4>>2]&0|0)==0){break}}Ae(c[a+64>>2]|0,1,20752,(l=i,i=i+8|0,c[l>>2]=c[k+8>>2],l)|0);i=l}}while(0);j=hi(k,20736)|0;h=a+40|0;m=c[h>>2]|0;sd(m,0);Ed(m,j);rd(m);sm(f);if((c[g>>2]|0)!=0){td(c[h>>2]|0);n=a+48|0;o=c[n>>2]|0;kk(o,m);ti(k,d);i=e;return}sm(f);if((c[g>>2]|0)==1){Ae(c[a+64>>2]|0,1,20656,(l=i,i=i+1|0,i=i+7&-8,c[l>>2]=0,l)|0);i=l}gg(a,4);sm(f);n=a+48|0;o=c[n>>2]|0;kk(o,m);ti(k,d);i=e;return}function og(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0;f=i;i=i+8|0;g=f|0;h=c[a+44>>2]|0;j=h+44|0;if((c[j>>2]|0)!=34){k=c[a+64>>2]|0;l=ym(34)|0;m=ym(c[j>>2]|0)|0;Ae(k,1,15e3,(n=i,i=i+16|0,c[n>>2]=l,c[n+8>>2]=m,n)|0);i=n}m=h+12|0;Eg(a,c[m>>2]|0);l=a+48|0;k=Sj(c[l>>2]|0,c[a+36>>2]|0,c[m>>2]|0)|0;c[g>>2]=0;m=a+60|0;o=c[m>>2]|0;p=c[o+4>>2]|0;yj(o,0);sm(h);if((c[j>>2]|0)==4){q=Uf(a)|0}else{q=e[(c[(c[l>>2]|0)+96>>2]|0)+14>>1]|0}o=a+52|0;si(c[o>>2]|0,0,q);ck(c[l>>2]|0,12);r=a+28|0;s=c[r>>2]|0;if((s|0)==0){t=0}else{yj(c[m>>2]|0,s);s=Rj(c[l>>2]|0,c[r>>2]|0,10608)|0;c[k+40>>2]=c[c[r>>2]>>2];r=k|0;u=c[r+4>>2]|((d|0)<0|0?-1:0);c[r>>2]=c[r>>2]|d;c[r+4>>2]=u;c[(c[(c[l>>2]|0)+96>>2]|0)+52>>2]=s;t=1}s=c[j>>2]|0;if((s|0)==0){sm(h);u=c[j>>2]|0;if((u|0)==1){Ae(c[a+64>>2]|0,1,10480,(n=i,i=i+1|0,i=i+7&-8,c[n>>2]=0,n)|0);i=n;v=t;w=11}else{x=t;y=u}while(1){if((w|0)==11){w=0;x=v;y=c[j>>2]|0}if((y|0)!=34){u=c[a+64>>2]|0;r=ym(34)|0;d=ym(c[j>>2]|0)|0;Ae(u,1,15e3,(n=i,i=i+16|0,c[n>>2]=r,c[n+8>>2]=d,n)|0);i=n}d=c[m>>2]|0;yj(d,Dg(a,g)|0);z=x+1|0;d=c[j>>2]|0;if((d|0)==2){sm(h);v=z;w=11;continue}else if((d|0)==1){break}else{r=c[a+64>>2]|0;u=ym(d)|0;Ae(r,1,10352,(n=i,i=i+8|0,c[n>>2]=u,n)|0);i=n;v=z;w=11;continue}}sm(h);A=z;B=c[j>>2]|0}else{A=t;B=s}if((B|0)==41){sm(h);h=c[m>>2]|0;zj(h,p,_f(a)|0);C=c[j>>2]|0}else{C=B}if((C|0)!=28){C=c[a+64>>2]|0;B=ym(28)|0;p=ym(c[j>>2]|0)|0;Ae(C,1,15e3,(n=i,i=i+16|0,c[n>>2]=B,c[n+8>>2]=p,n)|0);i=n}n=Cj(c[m>>2]|0,c[g>>2]|0,c[(c[o>>2]|0)+68>>2]|0,A+1|0)|0;c[k+8>>2]=n;pk(c[l>>2]|0,0,q,c[c[n+4>>2]>>2]|0);n=a+20|0;q=b[n>>1]|0;if(q<<16>>16==0){i=f;return}Wj(c[l>>2]|0,c[a+16>>2]|0,q);b[n>>1]=0;i=f;return}



function pg(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;b=i;d=a+52|0;e=c[d>>2]|0;f=c[a+44>>2]|0;g=f+44|0;if((c[g>>2]|0)!=34){h=c[a+64>>2]|0;j=ym(34)|0;k=ym(c[g>>2]|0)|0;Ae(h,1,15e3,(l=i,i=i+16|0,c[l>>2]=j,c[l+8>>2]=k,l)|0);i=l}k=Fg(a)|0;j=f+12|0;f=gi(e,k,c[j>>2]|0)|0;if((f|0)!=0){m=f;i=b;return m|0}if((k|0)==0){n=c[e+16>>2]|0}else{n=k}k=c[j>>2]|0;j=dg(n,k)|0;if((j|0)==0){f=dg(c[e+20>>2]|0,k)|0;if((f|0)!=0){o=f;p=8}}else{o=j;p=8}do{if((p|0)==8){j=o+8|0;f=c[j>>2]|0;e=c[j+4>>2]|0;j=0;h=0;if((f|0)==3&(e|0)==0){m=If(a,n,k)|0;i=b;return m|0}else if((f|0)==(j|0)&(e|0)==(h|0)){m=ei(c[d>>2]|0,o)|0;i=b;return m|0}else{break}}}while(0);Ae(c[a+64>>2]|0,1,12240,(l=i,i=i+8|0,c[l>>2]=k,l)|0);i=l;m=0;i=b;return m|0}function qg(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;e=i;f=c[d+8>>2]|0;g=d|0;h=c[g>>2]|0;j=b[h+58>>1]|0;k=j<<16>>16;if((f|0)==(k|0)|j<<16>>16==-1){l=h}else{Ae(c[a+64>>2]|0,1,12464,(m=i,i=i+24|0,c[m>>2]=c[h+8>>2],c[m+8>>2]=k,c[m+16>>2]=f,m)|0);i=m;l=c[g>>2]|0}if((l|0)!=(c[(c[a+52>>2]|0)+76>>2]|0)){i=e;return}l=c[c[d+4>>2]>>2]|0;d=c[l>>2]|0;g=d|0;if(!((c[g>>2]&256|0)==0&(c[g+4>>2]&0|0)==0)){i=e;return}if((b[d+52>>1]|0)==12){i=e;return}Ae(c[a+64>>2]|0,1,12360,(m=i,i=i+8|0,c[m>>2]=l,m)|0);i=m;i=e;return}function rg(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;if((um(c[a+44>>2]|0,c[c[a+32>>2]>>2]|0)|0)==0){e=0;return e|0}e=Cf(a,b,d)|0;return e|0}function sg(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0;g=i;h=c[a+32>>2]|0;if((b|0)==0){j=0;i=g;return j|0}k=h|0;l=b;while(1){qn(h);tn(h,8024,(b=i,i=i+24|0,c[b>>2]=c[l>>2],c[b+8>>2]=d,c[b+16>>2]=e,b)|0);i=b;b=tb[f&15](a,d,c[k>>2]|0)|0;if((b|0)!=0){j=b;m=5;break}b=c[l+4>>2]|0;if((b|0)==0){j=0;m=5;break}else{l=b}}if((m|0)==5){i=g;return j|0}return 0}function tg(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;e=Um(d)|0;if((e|0)==0){f=0;return f|0}g=Cf(a,b,d)|0;c[g+28>>2]=e;f=g;return f|0}function ug(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0;f=i;if((b|0)==0){i=f;return}else{g=b}do{tn(a,8048,(b=i,i=i+24|0,c[b>>2]=c[g>>2],c[b+8>>2]=d,c[b+16>>2]=e,b)|0);i=b;g=c[g+4>>2]|0;}while((g|0)!=0);i=f;return}function vg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;d=i;e=a+52|0;f=c[e>>2]|0;g=c[a+44>>2]|0;h=Fg(a)|0;j=g+12|0;g=$h(f,h,c[j>>2]|0)|0;if((g|0)!=0){Gg(a,g,b);i=d;return}g=(h|0)==0;do{if(g){k=Pf(c[j>>2]|0)|0;if((k|0)==-1){break}l=kg(a,k)|0;if((l|0)==0){break}k=l|0;m=c[a+40>>2]|0;if((c[k>>2]&1|0)==0&(c[k+4>>2]&0|0)==0){Jd(m)}else{Fd(m,l)}c[b>>2]=2;i=d;return}}while(0);l=gi(c[e>>2]|0,h,c[j>>2]|0)|0;if((l|0)!=0){Hg(a,l,b);i=d;return}if(g){do{if((c[a+28>>2]|0)!=0){g=hi(c[(c[(c[e>>2]|0)+20>>2]|0)+20>>2]|0,c[j>>2]|0)|0;if((g|0)==0){break}Cd(c[a+40>>2]|0,g);c[b>>2]=2;i=d;return}}while(0);n=c[f+16>>2]|0}else{n=h}h=Hf(a,n,c[j>>2]|0)|0;if((h|0)==0){Ae(c[a+64>>2]|0,1,11816,(n=i,i=i+8|0,c[n>>2]=c[j>>2],n)|0);i=n;i=d;return}else{Ig(a,h,b);i=d;return}}function wg(a,b){a=a|0;b=b|0;var d=0,e=0;do{if((c[b>>2]|0)==2){d=6}else{e=c[(c[a+44>>2]|0)+44>>2]|0;if((e|0)==16){xd(c[a+40>>2]|0,17);d=1;break}else if((e|0)==6){xd(c[a+40>>2]|0,16);d=1;break}else{d=1;break}}}while(0);c[b>>2]=d;return}function xg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0;d=i;e=a+28|0;f=c[e>>2]|0;if((f|0)==0){Ae(c[a+64>>2]|0,1,12016,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g;h=c[e>>2]|0}else{h=f}f=c[(c[a+44>>2]|0)+12>>2]|0;e=c[h>>2]|0;h=ji(e,f)|0;if((h|0)!=0){j=a+40|0;k=c[j>>2]|0;Hd(k,h);c[b>>2]=2;i=d;return}l=c[e+8>>2]|0;Ae(c[a+64>>2]|0,1,11944,(g=i,i=i+16|0,c[g>>2]=f,c[g+8>>2]=l,g)|0);i=g;j=a+40|0;k=c[j>>2]|0;Hd(k,h);c[b>>2]=2;i=d;return}function yg(a){a=a|0;var b=0,d=0,e=0,f=0,g=0;b=i;d=c[(c[a+44>>2]|0)+44>>2]|0;e=c[(ud(c[a+40>>2]|0)|0)+4>>2]|0;f=e&65535;if((e&65531|0)==0|(f|0)==11){g=1}else{g=(f|0)==12?31:32}if((d|0)==(g|0)){i=b;return}f=c[a+64>>2]|0;a=ym(g)|0;g=ym(d)|0;Ae(f,1,12144,(f=i,i=i+16|0,c[f>>2]=a,c[f+8>>2]=g,f)|0);i=f;i=b;return}function zg(a,b){a=a|0;b=b|0;var d=0;d=c[a+44>>2]|0;if((c[b>>2]|0)!=2){Fd(c[a+40>>2]|0,c[d+52>>2]|0);c[b>>2]=2;return}if((c[d+44>>2]&-2|0)!=38){c[b>>2]=6;return}if((Jg(a)|0)!=0){return}c[b>>2]=5;return}function Ag(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=a+44|0;e=c[d>>2]|0;sm(e);f=c[e+44>>2]|0;if((f|0)==47){sm(e);e=_f(a)|0;g=a+40|0;wd(c[g>>2]|0,e);td(c[g>>2]|0);c[b>>2]=2;return}else if((f|0)==34){Gd(c[a+40>>2]|0,c[(c[d>>2]|0)+12>>2]|0);c[b>>2]=2;return}else{c[b>>2]=2;return}}function Bg(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;e=i;f=c[a+44>>2]|0;g=a+40|0;h=c[g>>2]|0;if((c[h+16>>2]|0)==0){Ae(c[a+64>>2]|0,1,13376,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j;k=c[g>>2]|0}else{k=h}h=ud(k)|0;k=c[f+44>>2]|0;do{if((k|0)==2){if((c[h+4>>2]&65535|0)!=3){break}if((b[h+14>>1]&1)!=0){break}Ae(c[a+64>>2]|0,1,13248,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}else if((k|0)==33){f=h+4|0;l=c[f>>2]|0;m=l&65535;if((m|0)==3){if((b[h+14>>1]&1)==0){break}}else if((m|0)==2){if((b[h+14>>1]|0)==0){c[f>>2]=l&-65536|3;break}else{l=c[a+64>>2]|0;f=ym(33)|0;Ae(l,1,14568,(j=i,i=i+8|0,c[j>>2]=f,j)|0);i=j;break}}f=c[a+64>>2]|0;l=ym(33)|0;Ae(f,1,14568,(j=i,i=i+8|0,c[j>>2]=l,j)|0);i=j}}while(0);rd(c[g>>2]|0);c[d>>2]=1;i=e;return}function Cg(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;e=a+60|0;yj(c[e>>2]|0,d);return Cj(c[e>>2]|0,0,b,1)|0}function Dg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0;d=i;e=c[a+44>>2]|0;f=e+12|0;if(($h(c[a+52>>2]|0,0,c[f>>2]|0)|0)!=0){Ae(c[a+64>>2]|0,1,10240,(g=i,i=i+8|0,c[g>>2]=c[f>>2],g)|0);i=g}h=Rj(c[a+48>>2]|0,0,c[f>>2]|0)|0;sm(e);f=e+44|0;if((c[f>>2]|0)!=41){j=c[a+64>>2]|0;k=ym(41)|0;l=ym(c[f>>2]|0)|0;Ae(j,1,15e3,(g=i,i=i+16|0,c[g>>2]=k,c[g+8>>2]=l,g)|0);i=g}sm(e);e=lg(a,b)|0;if((c[b>>2]&32|0)==0){c[h+8>>2]=e;i=d;return e|0}else{c[h+8>>2]=c[c[e+4>>2]>>2];Kg(a,h);i=d;return e|0}return 0}function Eg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;if(($h(c[a+52>>2]|0,0,b)|0)!=0){Ae(c[a+64>>2]|0,1,10240,(e=i,i=i+8|0,c[e>>2]=b,e)|0);i=e}f=c[a+28>>2]|0;if((f|0)==0){i=d;return}g=c[f>>2]|0;if((ji(g,b)|0)==0){i=d;return}Ae(c[a+64>>2]|0,1,9688,(e=i,i=i+16|0,c[e>>2]=c[g+8>>2],c[e+8>>2]=b,e)|0);i=e;i=d;return}function Fg(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;b=i;d=c[a+52>>2]|0;e=c[a+44>>2]|0;f=e+12|0;g=ri(d,0,c[f>>2]|0)|0;if((g|0)==0){h=0;i=b;return h|0}j=e+44|0;k=a+64|0;a=g;while(1){sm(e);if((c[j>>2]|0)!=42){g=c[k>>2]|0;l=ym(42)|0;m=ym(c[j>>2]|0)|0;Ae(g,1,15e3,(n=i,i=i+16|0,c[n>>2]=l,c[n+8>>2]=m,n)|0);i=n}sm(e);if((c[j>>2]|0)!=34){m=c[k>>2]|0;l=ym(34)|0;g=ym(c[j>>2]|0)|0;Ae(m,1,15e3,(n=i,i=i+16|0,c[n>>2]=l,c[n+8>>2]=g,n)|0);i=n}g=ri(d,a,c[f>>2]|0)|0;if((g|0)==0){h=a;break}else{a=g}}i=b;return h|0}function Gg(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0;f=i;g=b|0;h=c[g>>2]|0;j=c[g+4>>2]|0;if(!((h&256|0)==0&(j&0|0)==0)){Ae(c[a+64>>2]|0,1,11512,(g=i,i=i+8|0,c[g>>2]=c[b+20>>2],g)|0);i=g;c[d>>2]=2;i=f;return}if(!((h&65536|0)==0&(j&0|0)==0)){Lg(a,b);c[d>>2]=2;i=f;return}j=c[b+32>>2]|0;if((j|0)==1){zd(c[a+40>>2]|0,b);c[d>>2]=2;i=f;return}h=c[a+40>>2]|0;if((j|0)==(e[(c[a+48>>2]|0)+102>>1]|0|0)){yd(h,b);c[d>>2]=2;i=f;return}else{Ad(h,b);c[d>>2]=2;i=f;return}}function Hg(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;e=b|0;if((c[e>>2]&4096|0)==0&(c[e+4>>2]&0|0)==0){Mg(a,b);c[d>>2]=2;return}else{Id(c[a+40>>2]|0,b);c[d>>2]=2;return}}function Ig(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0;e=c[a+40>>2]|0;f=b|0;g=c[f>>2]|0;h=c[f+4>>2]|0;if((g&2|0)==0&(h&0|0)==0){Hg(a,b,d);return}a=b;if((g&65536|0)==0&(h&0|0)==0){zd(e,a)}else{Bd(e,a)}c[d>>2]=2;return}function Jg(b){b=b|0;var d=0,f=0,g=0,h=0;d=c[b+44>>2]|0;f=a[(c[d+8>>2]|0)+(e[d+20>>1]|0)|0]|0;if(!((f<<24>>24|0)==45|(f<<24>>24|0)==43)){g=0;return g|0}h=b+40|0;vd(c[h>>2]|0,f<<24>>24==45|0);rm(d);Fd(c[h>>2]|0,c[d+52>>2]|0);g=1;return g|0}function Kg(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0;f=i;g=c[a+44>>2]|0;h=g+44|0;if((c[h>>2]|0)!=26){j=c[a+64>>2]|0;k=ym(26)|0;l=ym(c[h>>2]|0)|0;Ae(j,1,15e3,(j=i,i=i+16|0,c[j>>2]=k,c[j+8>>2]=l,j)|0);i=j}j=a+20|0;if(((e[j>>1]|0)+1|0)>>>0>=(e[a+22>>1]|0)>>>0){Ng(a)}l=c[(Og(a,c[c[d+8>>2]>>2]|0)|0)+12>>2]&65535;k=a+16|0;b[(c[k>>2]|0)+((e[j>>1]|0)<<1)>>1]=l;b[(c[k>>2]|0)+((e[j>>1]|0)+1<<1)>>1]=c[d+12>>2];b[j>>1]=(b[j>>1]|0)+2;sm(g);i=f;return}function Lg(a,b){a=a|0;b=b|0;var d=0,e=0;d=c[b+40>>2]|0;do{if((d|0)!=0){e=c[a+28>>2]|0;if((e|0)==0){break}if((Dh(d,c[e>>2]|0)|0)==0){break}Cd(c[a+40>>2]|0,b);return}}while(0);Bd(c[a+40>>2]|0,b);return}function Mg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0;d=i;e=c[a+44>>2]|0;sm(e);f=e+44|0;if((c[f>>2]|0)!=42){g=c[a+64>>2]|0;h=ym(42)|0;j=ym(c[f>>2]|0)|0;Ae(g,1,15e3,(k=i,i=i+16|0,c[k>>2]=h,c[k+8>>2]=j,k)|0);i=k}sm(e);if((c[f>>2]|0)!=34){j=c[a+64>>2]|0;h=ym(34)|0;g=ym(c[f>>2]|0)|0;Ae(j,1,15e3,(k=i,i=i+16|0,c[k>>2]=h,c[k+8>>2]=g,k)|0);i=k}g=e+12|0;e=Xe(a,b,c[g>>2]|0)|0;do{if((e|0)!=0){h=e|0;j=c[h>>2]|0;f=c[h+4>>2]|0;if((j&2|0)==0&(f&0|0)==0){if(!((j&32|0)==0&(f&0|0)==0)){break}}else{if(!((c[e+40>>2]|0)==(b|0)&((j&32|0)==0&(f&0|0)==0))){break}}Dd(c[a+40>>2]|0,e);i=d;return}}while(0);e=b|0;do{if(!((c[e>>2]&1024|0)==0&(c[e+4>>2]&0|0)==0)){f=ni(b,c[g>>2]|0)|0;if((f|0)==0){break}Id(c[a+40>>2]|0,f);i=d;return}}while(0);e=c[g>>2]|0;Ae(c[a+64>>2]|0,1,11768,(k=i,i=i+16|0,c[k>>2]=c[b+8>>2],c[k+8>>2]=e,k)|0);i=k;i=d;return}function Ng(a){a=a|0;var d=0,e=0;d=a+22|0;e=b[d>>1]<<1;b[d>>1]=e;d=a+16|0;c[d>>2]=Wd(c[d>>2]|0,(e&65535)<<1)|0;return}function Og(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;d=i;e=c[a+44>>2]|0;f=c[a+52>>2]|0;do{if((c[f+44>>2]|0)==(b|0)){g=38}else{if((c[f+48>>2]|0)==(b|0)){g=39;break}if((c[f+52>>2]|0)==(b|0)){g=36;break}g=(c[f+56>>2]|0)==(b|0)?37:34}}while(0);sm(e);h=e+44|0;if((c[h>>2]|0)!=(g|0)){j=c[a+64>>2]|0;k=ym(g)|0;l=ym(c[h>>2]|0)|0;Ae(j,1,15e3,(m=i,i=i+16|0,c[m>>2]=k,c[m+8>>2]=l,m)|0);i=m}if((c[f+60>>2]|0)==(b|0)){l=c[e+12>>2]|0;k=Pf(l)|0;if(!((k|0)==13|(k|0)==7)){Ae(c[a+64>>2]|0,1,10112,(m=i,i=i+8|0,c[m>>2]=l,m)|0);i=m}n=Uh(f,(k|0)==7|0,0)|0;i=d;return n|0}if((g|0)!=34){n=c[e+52>>2]|0;i=d;return n|0}g=e+12|0;e=ni(b,c[g>>2]|0)|0;if((e|0)==0){k=c[g>>2]|0;Ae(c[a+64>>2]|0,1,1e4,(m=i,i=i+16|0,c[m>>2]=c[b+8>>2],c[m+8>>2]=k,m)|0);i=m}k=e+80|0;e=c[k>>2]|0;if((e|0)!=0){n=e;i=d;return n|0}Ae(c[a+64>>2]|0,1,9904,(m=i,i=i+1|0,i=i+7&-8,c[m>>2]=0,m)|0);i=m;n=c[k>>2]|0;i=d;return n|0}function Pg(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=Vd(20)|0;c[d>>2]=1;c[d+16>>2]=a;c[d+4>>2]=(a|0)!=0;if(b<<24>>24==119){e=1;f=0}else{e=b<<24>>24!=114|0;f=1}c[d+8>>2]=f;c[d+12>>2]=e;return d|0}function Qg(a){a=a|0;var b=0,d=0;b=c[a+16>>2]|0;if((c[b+4>>2]|0)==0){d=b;Ln(d);return}xa(c[b+16>>2]|0)|0;d=b;Ln(d);return}function Rg(a,b,d){a=a|0;b=b|0;d=d|0;b=c[(c[(c[a>>2]|0)+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;d=b+16|0;a=c[d>>2]|0;if((a|0)==0){return}xa(a|0)|0;c[d>>2]=0;c[b+4>>2]=0;return}function Sg(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;j=c[(c[(c[h+((e[f+2>>1]|0)<<2)>>2]|0)+16>>2]|0)+8>>2]|0;k=c[(c[(c[h+((e[f+4>>1]|0)<<2)>>2]|0)+16>>2]|0)+8>>2]|0;l=c[h+((e[f>>1]|0)<<2)>>2]|0;f=gb()|0;c[f>>2]=0;h=a[k]|0;if(!((h<<24>>24|0)==97|(h<<24>>24|0)==119|(h<<24>>24|0)==114)){Ae(c[b+116>>2]|0,9,17728,(m=i,i=i+8|0,c[m>>2]=h<<24>>24,m)|0);i=m}n=Ea(j|0,k|0)|0;if((n|0)!=0){o=Pg(n,h)|0;p=g;c[p>>2]=o;q=o+16|0;c[q>>2]=n;r=o+4|0;c[r>>2]=1;s=o|0;c[s>>2]=1;gh(l,g);i=d;return}k=c[f>>2]|0;Ae(c[b+116>>2]|0,9,19872,(m=i,i=i+24|0,c[m>>2]=k,c[m+8>>2]=k,c[m+16>>2]=j,m)|0);i=m;o=Pg(n,h)|0;p=g;c[p>>2]=o;q=o+16|0;c[q>>2]=n;r=o+4|0;c[r>>2]=1;s=o|0;c[s>>2]=1;gh(l,g);i=d;return}function Tg(a,b,d){a=a|0;b=b|0;d=d|0;Ug(a,0,d);Ga(10,c[(c[(c[(c[a>>2]|0)+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0)+16>>2]|0)|0;return}function Ug(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0;d=c[a>>2]|0;g=c[(c[d+(e[f+2>>1]<<2)>>2]|0)+16>>2]|0;h=c[d+(e[f+4>>1]<<2)>>2]|0;Xg(a,g);if((b[(c[c[h+8>>2]>>2]|0)+52>>1]|0)==2){Oa(c[(c[h+16>>2]|0)+8>>2]|0,c[g+16>>2]|0)|0;return}else{f=c[a+100>>2]|0;qn(f);pn(f,h);Oa(c[f>>2]|0,c[g+16>>2]|0)|0;return}}function Vg(b,d,f){b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0;d=i;i=i+8|0;g=d|0;h=c[b>>2]|0;j=c[(c[h+((e[f+2>>1]|0)<<2)>>2]|0)+16>>2]|0;k=c[h+((e[f>>1]|0)<<2)>>2]|0;f=c[b+100>>2]|0;qn(f);h=f+8|0;l=c[h>>2]|0;m=f|0;n=c[m>>2]|0;Yg(b,j);b=c[j+16>>2]|0;j=wa(b|0)|0;a:do{if((j|0)==-1){o=0;p=n;q=3}else{r=0;s=n;t=l-1|0;u=j;while(1){a[s+r|0]=u;if((u|0)==10){break}if((r|0)==(t|0)){un(f);v=(c[h>>2]|0)-1|0;w=c[m>>2]|0}else{v=t;w=s}x=r+1|0;y=wa(b|0)|0;if((y|0)==-1){o=x;p=w;q=3;break a}else{r=x;s=w;t=v;u=y}}u=r+1|0;a[s+u|0]=0;z=u;A=s}}while(0);if((q|0)==3){a[p+o|0]=0;z=o;A=p}p=Vd(12)|0;o=z+1|0;q=Vd(o)|0;Yn(q|0,A|0,o)|0;c[p+8>>2]=q;c[p>>2]=1;c[p+4>>2]=z;qn(f);c[g>>2]=p;gh(k,g);i=d;return}function Wg(a){a=a|0;return ei(a,7024)|0}function Xg(a,b){a=a|0;b=b|0;var d=0,e=0;d=i;if((c[b+16>>2]|0)==0){Ae(c[a+116>>2]|0,9,19696,(e=i,i=i+1|0,i=i+7&-8,c[e>>2]=0,e)|0);i=e}if((c[b+12>>2]|0)!=0){i=d;return}Ae(c[a+116>>2]|0,9,19216,(e=i,i=i+1|0,i=i+7&-8,c[e>>2]=0,e)|0);i=e;i=d;return}function Yg(a,b){a=a|0;b=b|0;var d=0,e=0;d=i;if((c[b+16>>2]|0)==0){Ae(c[a+116>>2]|0,9,19696,(e=i,i=i+1|0,i=i+7&-8,c[e>>2]=0,e)|0);i=e}if((c[b+8>>2]|0)!=0){i=d;return}Ae(c[a+116>>2]|0,9,18520,(e=i,i=i+1|0,i=i+7&-8,c[e>>2]=0,e)|0);i=e;i=d;return}function Zg(){var a=0,b=0,d=0,e=0;a=i;i=i+8|0;b=a|0;d=Vd(16)|0;e=b|0;c[e>>2]=0;c[e+4>>2]=0;c[d+12>>2]=eh(1048576,0,0,b)|0;c[d+8>>2]=0;c[d>>2]=1;i=a;return d|0}function _g(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0;f=i;if((c[b>>2]|0)==100){Ae(c[a+116>>2]|0,6,17360,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}g=c[(c[d+16>>2]|0)+12>>2]|0;d=c[(c[e+16>>2]|0)+12>>2]|0;e=c[g+8>>2]|0;if((e|0)!=(c[d+8>>2]|0)){h=0;i=f;return h|0}j=c[(c[e>>2]|0)+88>>2]|0;c[b>>2]=(c[b>>2]|0)+1;e=wb[j&31](a,b,g,d)|0;c[b>>2]=(c[b>>2]|0)-1;h=e;i=f;return h|0}function $g(a,b){a=a|0;b=b|0;var d=0,e=0;d=c[b+16>>2]|0;b=c[d+8>>2]|0;if((b|0)==0){return}e=b+16|0;if((c[e>>2]|0)==(a|0)){return}c[e>>2]=a;e=c[d+12>>2]|0;d=c[(c[c[e+8>>2]>>2]|0)+84>>2]|0;if((d|0)==0){return}rb[d&127](a,e);return}function ah(a){a=a|0;var b=0;b=c[a+16>>2]|0;a=c[b+8>>2]|0;if((a|0)!=0){c[a+8>>2]=0}a=b+12|0;ch(c[a>>2]|0);Ln(c[a>>2]|0);Ln(b);return}function bh(a){a=a|0;var b=0,d=0,e=0,f=0;b=c[a+8>>2]|0;if((c[b+8>>2]|0)==0){return}d=b+16|0;if((c[d>>2]|0)==-1){return}c[d>>2]=-1;d=a+12|0;a=c[d>>2]|0;do{if((c[a>>2]&7340032|0)==0){b=a+16|0;e=c[b>>2]|0;f=c[e>>2]|0;if((f|0)==1){kh(c[a+8>>2]|0,b);break}else{c[e>>2]=f-1;break}}}while(0);Ln(c[d>>2]|0);return}function ch(a){a=a|0;var b=0,d=0;if((c[a>>2]&7340032|0)!=0){return}b=a+16|0;d=c[b>>2]|0;c[d>>2]=(c[d>>2]|0)-1;if((c[c[b>>2]>>2]|0)!=0){return}qb[c[(c[c[a+8>>2]>>2]|0)+28>>2]&63](a);return}function dh(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=i;i=i+24|0;e=b;b=i;i=i+8|0;c[b>>2]=c[e>>2];c[b+4>>2]=c[e+4>>2];e=d|0;c[e+8>>2]=a;c[e>>2]=c[c[a>>2]>>2]&4194304;a=b|0;b=c[a+4>>2]|0;f=e+16|0;c[f>>2]=c[a>>2];c[f+4>>2]=b;ch(e);i=d;return}function eh(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;b=i;f=e;e=i;i=i+8|0;c[e>>2]=c[f>>2];c[e+4>>2]=c[f+4>>2];f=Vd(24)|0;c[f>>2]=a;c[f+8>>2]=d;d=e|0;e=f+16|0;a=c[d+4>>2]|0;c[e>>2]=c[d>>2];c[e+4>>2]=a;i=b;return f|0}function fh(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=b|0;if((c[d>>2]&7340032|0)==0){e=c[b+16>>2]|0;c[e>>2]=(c[e>>2]|0)+1}e=a|0;if((c[e>>2]&7340032|0)==0){ch(a)}f=b+16|0;b=c[f+4>>2]|0;g=a+16|0;c[g>>2]=c[f>>2];c[g+4>>2]=b;c[e>>2]=c[d>>2];return}function gh(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;e=b;b=i;i=i+8|0;c[b>>2]=c[e>>2];c[b+4>>2]=c[e+4>>2];e=a|0;if((c[e>>2]&7340032|0)==0){ch(a)}f=b|0;b=c[f+4>>2]|0;g=a+16|0;c[g>>2]=c[f>>2];c[g+4>>2]=b;c[e>>2]=c[c[c[a+8>>2]>>2]>>2]&4194304;i=d;return}function hh(a){a=a|0;var b=0,d=0,e=0,f=0,g=0;b=a|0;d=c[b>>2]|0;e=a+16|0;if((d&7340032|0)==0){f=c[e>>2]|0;c[f>>2]=(c[f>>2]|0)+1;g=c[b>>2]|0}else{g=d}return eh(g,0,c[a+8>>2]|0,e)|0}function ih(){var a=0;a=Vd(28)|0;c[a>>2]=1;c[a+8>>2]=0;c[a+12>>2]=0;c[a+16>>2]=-1;c[a+20>>2]=0;c[a+24>>2]=0;return a|0}function jh(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;return(c[d+16>>2]|0)==(c[e+16>>2]|0)|0}function kh(a,d){a=a|0;d=d|0;var e=0,f=0;e=i;f=d;d=i;i=i+8|0;c[d>>2]=c[f>>2];c[d+4>>2]=c[f+4>>2];f=b[(c[a>>2]|0)+52>>1]|0;if((f<<16>>16|0)==6){bh(c[d>>2]|0);i=e;return}else if((f<<16>>16|0)==7){$d(a,c[d>>2]|0);i=e;return}else if((f<<16>>16|0)==8){Ui(a,c[d>>2]|0);i=e;return}else{if(f<<16>>16==9|(f&65535)>>>0>13>>>0){Qh(a,c[d>>2]|0);i=e;return}if(f<<16>>16==5){uj(a,c[d>>2]|0);i=e;return}else{dh(a,d);i=e;return}}}function lh(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0;f=Vd(24)|0;g=Vd(16)|0;c[f+20>>2]=a;c[f>>2]=g;b[f+4>>1]=0;b[f+6>>1]=4;b[f+10>>1]=0;b[f+8>>1]=0;c[f+12>>2]=d;c[f+16>>2]=e;return f|0}function mh(a){a=a|0;if((a|0)!=0){Ln(c[a>>2]|0)}Ln(a);return}function nh(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;if((d|0)==0){g=0;return g|0}h=d+4|0;if((c[h>>2]|0)!=0){i=a+20|0;j=d+8|0;wj(c[i>>2]|0,c[j>>2]|0);k=c[h>>2]|0;h=c[i>>2]|0;if((c[j>>2]|0)==0){l=0;m=h}else{n=0;o=h;while(1){xj(o,nh(a,c[k+(n<<2)>>2]|0,f)|0);h=n+1|0;p=c[i>>2]|0;if(h>>>0<(c[j>>2]|0)>>>0){n=h;o=p}else{l=h;m=p;break}}}g=Cj(m,e[d+14>>1]|0,c[d>>2]|0,l)|0;return g|0}if((b[(c[d>>2]|0)+52>>1]|0)!=12){g=d;return g|0}l=(c[a>>2]|0)+((e[d+12>>1]|0)+(e[a+4>>1]|0)<<2)|0;a=c[l>>2]|0;do{if((a|0)!=0){if((b[(c[a>>2]|0)+52>>1]|0)==13){break}else{g=a}return g|0}}while(0);c[l>>2]=f;g=f;return g|0}function oh(a,b){a=a|0;b=b|0;return nh(a,b,c[a+12>>2]|0)|0}function ph(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0;if((d|0)==0){return}if((b[d+14>>1]&16)==0){return}if((b[(c[d>>2]|0)+52>>1]|0)==12){c[(c[a>>2]|0)+((e[d+12>>1]|0)+(e[a+4>>1]|0)<<2)>>2]=f;return}g=d+8|0;if((c[g>>2]|0)==0){return}h=d+4|0;d=f+4|0;f=0;do{ph(a,c[(c[h>>2]|0)+(f<<2)>>2]|0,c[(c[d>>2]|0)+(f<<2)>>2]|0);f=f+1|0;}while(f>>>0<(c[g>>2]|0)>>>0);return}function qh(a,b,c){a=a|0;b=b|0;c=c|0;return Eh(a,b,c,0)|0}function rh(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0;e=a+20|0;f=c[(c[e>>2]|0)+4>>2]|0;g=(Eh(a,b,d,19)|0)==0;d=c[e>>2]|0;if(g){c[d+4>>2]=f;h=0;return h|0}else{h=Aj(d)|0;return h|0}return 0}function sh(a,b,c){a=a|0;b=b|0;c=c|0;return Eh(a,b,c,3)|0}function th(a,b){a=a|0;b=b|0;return c[(c[a>>2]|0)+((e[b+12>>1]|0)+(e[a+4>>1]|0)<<2)>>2]|0}function uh(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;g=a+4|0;h=b[g>>1]|0;i=(h&65535)+1+(e[a+8>>1]|0)|0;j=d+8|0;k=c[j>>2]|0;if((i+k|0)>>>0<(e[a+6>>1]|0)>>>0){l=k}else{Fh(a);l=c[j>>2]|0}if((l|0)==0){m=i&65535;b[g>>1]=m;n=oh(a,f)|0;b[g>>1]=h;return n|0}l=d+4|0;d=a|0;k=0;do{c[(c[d>>2]|0)+(k+i<<2)>>2]=c[(c[l>>2]|0)+(k<<2)>>2];k=k+1|0;}while(k>>>0<(c[j>>2]|0)>>>0);m=i&65535;b[g>>1]=m;n=oh(a,f)|0;b[g>>1]=h;return n|0}function vh(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,i=0;f=c[b+8>>2]|0;if((f|0)<=0){return}g=(c[c[(c[(c[b>>2]|0)+68>>2]|0)+4>>2]>>2]|0)+4|0;b=d+4|0;d=a+4|0;h=a|0;a=0;do{i=e[(c[c[g>>2]>>2]|0)+12>>1]|0;c[(c[h>>2]|0)+((e[d>>1]|0)+i<<2)>>2]=c[(c[b>>2]|0)+(i<<2)>>2];a=a+1|0;}while((a|0)<(f|0));return}function wh(a,d){a=a|0;d=d|0;var f=0,g=0,h=0;f=e[a+4>>1]|0;g=b[a+8>>1]|0;h=(g&65535)+f|0;if(g<<16>>16==0){return}g=a|0;a=f;f=d;while(1){d=(c[g>>2]|0)+(a<<2)|0;if((c[d>>2]|0)==0){c[d>>2]=f}d=a+1|0;if((d|0)<(h|0)){a=d;f=c[f+16>>2]|0}else{break}}return}function xh(a){a=a|0;var d=0,f=0,g=0,h=0;d=e[a+4>>1]|0;f=b[a+8>>1]|0;g=(f&65535)+d|0;if(f<<16>>16==0){return}f=a|0;h=a+16|0;a=d;do{d=(c[f>>2]|0)+(a<<2)|0;if((c[d>>2]|0)==0){c[d>>2]=c[h>>2]}a=a+1|0;}while((a|0)<(g|0));return}function yh(a){a=a|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0;d=e[a+4>>1]|0;f=b[a+8>>1]|0;g=(f&65535)+d|0;if(f<<16>>16==0){h=0;return h|0}f=c[a>>2]|0;i=a+16|0;a=d;d=0;while(1){j=c[f+(a<<2)>>2]|0;if((j|0)==0){k=5}else{if((j|0)==(c[i>>2]|0)){k=5}else{l=d}}if((k|0)==5){k=0;l=d+1|0}j=a+1|0;if((j|0)<(g|0)){a=j;d=l}else{h=l;break}}return h|0}function zh(a){a=a|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;d=e[a+4>>1]|0;f=b[a+8>>1]|0;g=(f&65535)+d|0;h=c[a+16>>2]|0;if(f<<16>>16==0){return}f=a|0;i=a+20|0;j=a+12|0;a=d;do{d=c[(c[f>>2]|0)+(a<<2)>>2]|0;do{if(!((d|0)==0|(d|0)==(h|0))){if((b[d+14>>1]&64)==0){break}k=d+8|0;if((c[k>>2]|0)==0){l=0}else{m=d+4|0;n=0;while(1){o=c[(c[m>>2]|0)+(n<<2)>>2]|0;p=c[i>>2]|0;if((b[o+14>>1]&64)==0){yj(p,o)}else{yj(p,c[j>>2]|0)}p=n+1|0;if(p>>>0<(c[k>>2]|0)>>>0){n=p}else{l=p;break}}}n=Cj(c[i>>2]|0,0,c[d>>2]|0,l)|0;c[(c[f>>2]|0)+(a<<2)>>2]=n}}while(0);a=a+1|0;}while((a|0)<(g|0));return}function Ah(a){a=a|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;d=a+8|0;f=b[d>>1]|0;g=f&65535;h=a+4|0;i=b[h>>1]|0;j=a+10|0;k=b[j>>1]|0;if(((i&65535)+g+(k&65535)|0)<(e[a+6>>1]|0|0)){l=f;m=i;n=k}else{Fh(a);l=b[d>>1]|0;m=b[h>>1]|0;n=b[j>>1]|0}k=m+l&65535;b[h>>1]=k;b[d>>1]=n;if(n<<16>>16==0){return g|0}n=a|0;a=0;d=k;while(1){c[(c[n>>2]|0)+((d&65535)+a<<2)>>2]=0;k=a+1|0;if((k|0)>=(e[j>>1]|0|0)){break}a=k;d=b[h>>1]|0}return g|0}function Bh(a,c){a=a|0;c=c|0;var d=0;d=a+4|0;b[d>>1]=(e[d>>1]|0)-c;b[a+8>>1]=c;return}function Ch(a,c){a=a|0;c=c|0;var d=0;d=a+10|0;if((e[d>>1]|0|0)>=(c|0)){return}b[d>>1]=c;return}function Dh(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;a:do{if((a|0)==(b|0)){d=1}else{e=b;while(1){if((e|0)==0){d=0;break a}f=c[e+36>>2]|0;if((f|0)==(a|0)){d=1;break}else{e=f}}}}while(0);return d|0}function Eh(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0;if((d|0)==0|(e|0)==0){g=(d|0)==(e|0);h=g&1;if(!g){i=h;return i|0}yj(c[a+20>>2]|0,d);i=h;return i|0}h=c[d>>2]|0;g=b[h+52>>1]|0;if(g<<16>>16==13){if((f&16|0)==0){i=1;return i|0}yj(c[a+20>>2]|0,e);i=1;return i|0}j=c[e>>2]|0;k=b[j+52>>1]|0;if(k<<16>>16==13){if((f&16|0)==0){i=1;return i|0}yj(c[a+20>>2]|0,d);i=1;return i|0}if(g<<16>>16==12){i=Hh(a,d,e,f)|0;return i|0}l=h|0;do{if(!((c[l>>2]&1024|0)==0&(c[l+4>>2]&0|0)==0)){m=j|0;if((c[m>>2]&4096|0)==0&(c[m+4>>2]&0|0)==0){break}if((c[j+36>>2]|0)!=(h|0)){break}i=Gh(a,d,e,f)|0;return i|0}}while(0);if(g<<16>>16==5&k<<16>>16==5){i=Ih(a,d,e,f)|0;return i|0}else{i=Jh(a,d,e,f)|0;return i|0}return 0}function Fh(a){a=a|0;var d=0,e=0;d=a+6|0;e=b[d>>1]<<1;b[d>>1]=e;d=a|0;c[d>>2]=Wd(c[d>>2]|0,(e&65535)<<2)|0;return}function Gh(a,b,d,f){a=a|0;b=b|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;g=c[(c[d>>2]|0)+68>>2]|0;a:do{if((c[g+8>>2]|0)==0){h=f;i=1}else{j=f&17;k=c[c[g+4>>2]>>2]|0;l=k+8|0;if((c[l>>2]|0)==0){h=j;i=1;break}m=k+4|0;k=b+4|0;n=d+4|0;o=0;while(1){p=Eh(a,c[(c[k>>2]|0)+((e[(c[(c[m>>2]|0)+(o<<2)>>2]|0)+12>>1]|0)<<2)>>2]|0,c[(c[n>>2]|0)+(o<<2)>>2]|0,j)|0;q=o+1|0;if((p|0)==0){r=0;break}if(q>>>0<(c[l>>2]|0)>>>0){o=q}else{h=j;i=p;break a}}return r|0}}while(0);if((i|0)==0|(h&16|0)==0){r=i;return r|0}yj(c[a+20>>2]|0,b);r=i;return r|0}function Hh(a,d,f,g){a=a|0;d=d|0;f=f|0;g=g|0;var h=0,i=0,j=0,k=0,l=0;if((g&1|0)!=0){h=(d|0)==(f|0);i=h&1;if((g&16|0)==0|h^1){j=i;return j|0}yj(c[a+20>>2]|0,d);j=i;return j|0}i=(e[d+12>>1]|0)+(e[a+4>>1]|0)|0;d=a|0;h=(c[d>>2]|0)+(i<<2)|0;k=c[h>>2]|0;do{if((k|0)!=0){if((k|0)==(c[a+16>>2]|0)){break}if((k|0)==(f|0)){j=1;return j|0}if((b[k+14>>1]&64)==0){j=Eh(a,k,f,g|1)|0;return j|0}l=rh(a,k,f)|0;if((l|0)==0){j=0;return j|0}c[(c[d>>2]|0)+(i<<2)>>2]=l;j=1;return j|0}}while(0);c[h>>2]=f;j=1;return j|0}function Ih(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;g=f&17;h=d+4|0;i=e+4|0;j=(Eh(a,c[c[h>>2]>>2]|0,c[c[i>>2]>>2]|0,g|2)|0)!=0|0;k=d+8|0;l=c[k>>2]|0;m=l>>>0>(c[e+8>>2]|0)>>>0?0:j;if((m|0)==0){n=0;return n|0}j=g|4;a:do{if(l>>>0>1>>>0){g=1;while(1){o=c[(c[h>>2]|0)+(g<<2)>>2]|0;p=c[(c[i>>2]|0)+(g<<2)>>2]|0;do{if((b[(c[p>>2]|0)+52>>1]|0)==10){if((b[(c[o>>2]|0)+52>>1]|0)==10){q=p;break}q=c[c[p+4>>2]>>2]|0}else{q=p}}while(0);p=g+1|0;if((Eh(a,o,q,j)|0)==0){n=0;break}r=c[k>>2]|0;if(p>>>0<r>>>0){g=p}else{s=r;break a}}return n|0}else{s=l}}while(0);if((f&16|0)==0){n=m;return n|0}Kh(a,d,e,s);n=m;return n|0}function Jh(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;f=i;i=i+8|0;g=f|0;do{if((e&2|0)==0){if((e&4|0)==0){h=Mh(c[b>>2]|0,c[b+8>>2]|0,c[d>>2]|0,g)|0;break}else{h=Lh(c[d>>2]|0,c[d+8>>2]|0,c[b>>2]|0,g)|0;break}}else{h=Lh(c[b>>2]|0,c[b+8>>2]|0,c[d>>2]|0,g)|0}}while(0);j=c[g>>2]|0;a:do{if((h|0)!=0&(j|0)!=0){g=e&17;k=c[b+4>>2]|0;l=c[d+4>>2]|0;if((j|0)>0){m=0}else{n=g;o=1;break}while(1){p=m+1|0;if((Eh(a,c[k+(m<<2)>>2]|0,c[l+(m<<2)>>2]|0,g)|0)==0){q=0;break}if((p|0)<(j|0)){m=p}else{n=g;o=1;break a}}i=f;return q|0}else{n=e;o=h}}while(0);if((o|0)==0|(n&16|0)==0){q=o;i=f;return q|0}Kh(a,b,d,j);q=o;i=f;return q|0}function Kh(a,d,f,g){a=a|0;d=d|0;f=f|0;g=g|0;var h=0,i=0,j=0;h=c[d>>2]|0;i=c[f>>2]|0;j=(e[h+52>>1]|0)>>>0<(e[i+52>>1]|0)>>>0?h:i;if((g|0)==0){yj(c[a+20>>2]|0,c[j+24>>2]|0);return}else{i=c[a+20>>2]|0;yj(i,Cj(i,b[d+14>>1]&1&(e[f+14>>1]|0),j,g)|0);return}}function Lh(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Dh(a,d)|0;c[e>>2]=b;return f|0}function Mh(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;c[e>>2]=b;return(a|0)==(d|0)|0}function Nh(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=i;if((c[b>>2]|0)==100){Ae(c[a+116>>2]|0,6,16800,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}g=d+16|0;h=c[g>>2]|0;j=c[h+16>>2]|0;k=c[e+16>>2]|0;if((j|0)!=(c[k+16>>2]|0)){l=0;i=f;return l|0}e=c[h+12>>2]|0;h=c[k+12>>2]|0;k=d+8|0;if((j|0)==0){l=1;i=f;return l|0}j=0;d=c[b>>2]|0;while(1){m=c[(c[c[(c[(c[k>>2]|0)+4>>2]|0)+(j<<2)>>2]>>2]|0)+88>>2]|0;c[b>>2]=d+1;n=(wb[m&31](a,b,c[e+(j<<2)>>2]|0,c[h+(j<<2)>>2]|0)|0)==0;m=(c[b>>2]|0)-1|0;c[b>>2]=m;o=j+1|0;if(n){l=0;p=8;break}if(o>>>0<(c[(c[g>>2]|0)+16>>2]|0)>>>0){j=o;d=m}else{l=1;p=8;break}}if((p|0)==8){i=f;return l|0}return 0}function Oh(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0;d=c[b+16>>2]|0;b=c[d+8>>2]|0;if((b|0)==0){return}e=b+16|0;if((c[e>>2]|0)==(a|0)){return}c[e>>2]=a;e=d+16|0;b=c[e>>2]|0;if((b|0)==0){return}f=d+12|0;d=0;g=b;while(1){b=c[(c[f>>2]|0)+(d<<2)>>2]|0;h=c[(c[c[b+8>>2]>>2]|0)+84>>2]|0;do{if((h|0)==0){i=g}else{if((c[b>>2]&1048576|0)!=0){i=g;break}rb[h&127](a,b);i=c[e>>2]|0}}while(0);b=d+1|0;if(b>>>0<i>>>0){d=b;g=i}else{break}}return}function Ph(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0;b=c[a+16>>2]|0;a=c[b+8>>2]|0;if((a|0)!=0){c[a+8>>2]=0}a=b+16|0;d=b+12|0;e=c[d>>2]|0;if((c[a>>2]|0)==0){f=e;g=f;Ln(g);h=b;Ln(h);return}else{i=0;j=e}while(1){e=c[j+(i<<2)>>2]|0;ch(e);Ln(e);e=i+1|0;k=c[d>>2]|0;if(e>>>0<(c[a>>2]|0)>>>0){i=e;j=k}else{f=k;break}}g=f;Ln(g);h=b;Ln(h);return}function Qh(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;a=i;i=i+8|0;d=a|0;e=c[b+8>>2]|0;do{if((e|0)==0){f=0}else{g=e+16|0;if((c[g>>2]|0)==-1){i=a;return}if((c[e+8>>2]|0)==0){i=a;return}else{c[g>>2]=-1;f=1;break}}}while(0);e=b+16|0;g=b+12|0;h=c[g>>2]|0;if((c[e>>2]|0)==0){j=h}else{k=d|0;l=0;m=h;while(1){h=c[m+(l<<2)>>2]|0;do{if((c[h>>2]&7340032|0)==0){n=h+16|0;o=c[n>>2]|0;p=c[n+4>>2]|0;c[k>>2]=o;c[k+4>>2]=p;p=o|0;o=c[p>>2]|0;if((o|0)==1){kh(c[h+8>>2]|0,d);break}else{c[p>>2]=o-1;break}}}while(0);Ln(h);o=l+1|0;p=c[g>>2]|0;if(o>>>0<(c[e>>2]|0)>>>0){l=o;m=p}else{j=p;break}}}Ln(j);if((f|0)!=0){i=a;return}Ln(b);i=a;return}function Rh(a){a=a|0;var b=0,d=0;b=Vd(100)|0;d=b;c[b+32>>2]=0;c[b+40>>2]=0;c[b+36>>2]=0;c[b+24>>2]=0;fo(b|0,0,16)|0;c[b+16>>2]=a;c[b+20>>2]=a;c[b+88>>2]=0;c[b+28>>2]=0;Dn(d,a);return d|0}function Sh(a,b){a=a|0;b=b|0;var d=0;if((b|0)==0){return}else{d=b}while(1){b=c[d+44>>2]|0;Ln(c[d+20>>2]|0);Ln(d);if((b|0)==0){break}else{d=b}}return}function Th(a){a=a|0;var b=0,d=0;ui(c[a+4>>2]|0);ui(c[a+8>>2]|0);vi(c[a+28>>2]|0);Sh(0,c[a+24>>2]|0);b=c[a+16>>2]|0;if((b|0)!=0){d=b;do{vi(c[d+20>>2]|0);Sh(0,c[d+24>>2]|0);d=c[d+40>>2]|0;}while((d|0)!=0)}d=c[a+32>>2]|0;Ln(c[d+52>>2]|0);Ln(d);Ln(a);return}function Uh(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=c[(c[a+60>>2]|0)+24>>2]|0;f=c[a+4>>2]|0;a:do{if((f|0)!=0){g=f;while(1){if((c[g+8>>2]|0)==(e|0)){h=g+24|0;if((c[h>>2]|0)==(b|0)&(c[h+4>>2]|0)==(d|0)){break}}h=c[g+32>>2]|0;if((h|0)==0){break a}else{g=h}}if((g|0)==0){break}else{i=g}return i|0}}while(0);f=wi(a,e)|0;e=f+24|0;c[e>>2]=b;c[e+4>>2]=d;d=f|0;e=c[d+4>>2]|0;c[d>>2]=c[d>>2]|4194304;c[d+4>>2]=e;i=f;return i|0}function Vh(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=c[(c[a+44>>2]|0)+24>>2]|0;f=c[a+4>>2]|0;a:do{if((f|0)!=0){g=f;while(1){if((c[g+8>>2]|0)==(e|0)){h=g+24|0;if((c[h>>2]|0)==(b|0)&(c[h+4>>2]|0)==(d|0)){break}}h=c[g+32>>2]|0;if((h|0)==0){break a}else{g=h}}if((g|0)==0){break}else{i=g}return i|0}}while(0);f=wi(a,e)|0;e=f+24|0;c[e>>2]=b;c[e+4>>2]=d;d=f|0;e=c[d+4>>2]|0;c[d>>2]=c[d>>2]|4194304;c[d+4>>2]=e;i=f;return i|0}function Wh(a,b){a=a|0;b=+b;var d=0,e=0,f=0,g=0,i=0;d=c[(c[a+48>>2]|0)+24>>2]|0;e=c[a+4>>2]|0;a:do{if((e|0)!=0){f=e;while(1){if((c[f+8>>2]|0)==(d|0)){if(+h[f+24>>3]==b){break}}g=c[f+32>>2]|0;if((g|0)==0){break a}else{f=g}}if((f|0)==0){break}else{i=f}return i|0}}while(0);e=wi(a,d)|0;h[e+24>>3]=b;d=e|0;a=c[d+4>>2]|0;c[d>>2]=c[d>>2]|4194304;c[d+4>>2]=a;i=e;return i|0}function Xh(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=_n(d|0)|0;f=c[a+4>>2]|0;a:do{if((f|0)!=0){g=f;b:while(1){do{if((b[(c[c[g+8>>2]>>2]|0)+52>>1]|0)==2){h=c[g+24>>2]|0;if((c[h+4>>2]|0)!=(e|0)){break}if((nb(c[h+8>>2]|0,d|0)|0)==0){break b}}}while(0);h=c[g+32>>2]|0;if((h|0)==0){break a}else{g=h}}if((g|0)==0){break}else{i=g}return i|0}}while(0);f=c[a+52>>2]|0;h=Vd(e+1|0)|0;j=Vd(12)|0;Zn(h|0,d|0)|0;c[j+8>>2]=h;c[j+4>>2]=e;c[j>>2]=1;e=wi(a,c[f+24>>2]|0)|0;c[e+24>>2]=j;i=e;return i|0}function Yh(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0;f=c[a+4>>2]|0;a:do{if((f|0)!=0){g=f;b:while(1){do{if((b[(c[c[g+8>>2]>>2]|0)+52>>1]|0)==3){h=c[g+24>>2]|0;if((c[h+4>>2]|0)!=(e|0)){break}if((ho(c[h+8>>2]|0,d|0,e|0)|0)==0){break b}}}while(0);h=c[g+32>>2]|0;if((h|0)==0){break a}else{g=h}}if((g|0)==0){break}else{i=g}return i|0}}while(0);f=c[a+56>>2]|0;h=Vd(e)|0;j=Vd(12)|0;Yn(h|0,d|0,e)|0;c[j+8>>2]=h;c[j+4>>2]=e;c[j>>2]=1;e=wi(a,c[f+24>>2]|0)|0;c[e+24>>2]=j;i=e;return i|0}function Zh(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;e=Vd(48)|0;f=Vd((_n(d|0)|0)+1|0)|0;c[e+20>>2]=f;g=e;c[g>>2]=2;c[g+4>>2]=0;Zn(f|0,d|0)|0;c[e+16>>2]=c[c[a+96>>2]>>2];a=xi(d)|0;d=e+24|0;c[d>>2]=a;c[d+4>>2]=G;c[e+8>>2]=b;c[e+44>>2]=0;c[e+40>>2]=0;return e|0}function _h(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;e=Zh(a,b,d)|0;d=a+20|0;c[e+44>>2]=c[(c[d>>2]|0)+24>>2];c[(c[d>>2]|0)+24>>2]=e;return e|0}function $h(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;e=xi(d)|0;f=G;if((b|0)!=0){g=yi(c[b+24>>2]|0,d,e,f)|0;return g|0}b=yi(c[(c[a+16>>2]|0)+24>>2]|0,d,e,f)|0;if((b|0)!=0){g=b;return g|0}g=yi(c[(c[a+20>>2]|0)+24>>2]|0,d,e,f)|0;return g|0}function ai(a,b){a=a|0;b=b|0;var d=0,e=0;d=c[(c[a+20>>2]|0)+24>>2]|0;if((d|0)==(b|0)){return}else{e=d}do{d=e|0;a=c[d+4>>2]|0;c[d>>2]=c[d>>2]|16384;c[d+4>>2]=a;e=c[e+44>>2]|0;}while((e|0)!=(b|0));return}function bi(a,b,d){a=a|0;b=b|0;d=d|0;zi(a,b,d,c[a+16>>2]|0);return}function ci(a,b,d){a=a|0;b=b|0;d=d|0;zi(a,b,d,c[a+20>>2]|0);return}function di(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;e=Vd(40)|0;c[e+8>>2]=c[b+8>>2];f=d+16|0;d=e+24|0;g=c[f+4>>2]|0;c[d>>2]=c[f>>2];c[d+4>>2]=g;c[e+12>>2]=c[b+12>>2];b=e;c[b>>2]=1;c[b+4>>2]=0;b=a+12|0;c[e+32>>2]=c[b>>2];c[b>>2]=e;return}function ei(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=fi(a,c[d+4>>2]|0)|0;f=d+18|0;if((b[f>>1]|0)==0){g=Ai(e)|0;h=e+24|0;c[h>>2]=g;c[e+76>>2]=g;i=g;j=h}else{i=0;j=e+24|0}c[j>>2]=i;b[e+54>>1]=1;b[e+58>>1]=b[f>>1]|0;c[e+84>>2]=c[d+28>>2];f=c[d+20>>2]|0;i=0;j=e|0;c[j>>2]=f;c[j+4>>2]=i;h=b[d+16>>1]|0;b[e+56>>1]=h;c[e+88>>2]=c[d+32>>2];c[e+28>>2]=c[d+36>>2];c[e+72>>2]=c[a+20>>2];c[e+80>>2]=c[d+24>>2];if(h<<16>>16!=0){return e|0}c[j>>2]=f|4194304;c[j+4>>2]=i;return e|0}function fi(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0;e=Vd(96)|0;f=e;g=Vd((_n(d|0)|0)+1|0)|0;Zn(g|0,d|0)|0;h=e;c[h>>2]=65536;c[h+4>>2]=0;b[e+56>>1]=1;b[e+54>>1]=0;c[e+24>>2]=0;c[e+36>>2]=0;h=xi(d)|0;d=e+16|0;c[d>>2]=h;c[d+4>>2]=G;c[e+8>>2]=g;b[e+58>>1]=0;c[e+44>>2]=0;c[e+60>>2]=0;c[e+80>>2]=0;c[e+32>>2]=0;c[e+48>>2]=0;c[e+84>>2]=0;c[e+88>>2]=0;c[e+28>>2]=0;g=a+20|0;c[e+72>>2]=c[g>>2];c[e+76>>2]=0;d=a+36|0;b[e+52>>1]=c[d>>2];c[d>>2]=(c[d>>2]|0)+1;c[e+40>>2]=c[(c[g>>2]|0)+20>>2];c[(c[g>>2]|0)+20>>2]=f;return f|0}function gi(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0;f=xi(e)|0;g=G;if((d|0)!=0){h=Bi(c[d+20>>2]|0,e,f,g)|0;return h|0}if((a[e+1|0]|0)==0){d=c[b+88>>2]|0;i=Ci(d,a[e]|0)|0;if((i|0)==0){h=0;return h|0}c[d+24>>2]=i;h=d;return h|0}else{d=Bi(c[(c[b+16>>2]|0)+20>>2]|0,e,f,g)|0;if((d|0)!=0){h=d;return h|0}h=Bi(c[(c[b+20>>2]|0)+20>>2]|0,e,f,g)|0;return h|0}return 0}function hi(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=xi(b)|0;e=G;f=a;a:while(1){a=c[f+32>>2]|0;b:do{if((a|0)!=0){g=a;while(1){h=g+24|0;if((c[h>>2]|0)==(d|0)&(c[h+4>>2]|0)==(e|0)){if((nb(c[g+20>>2]|0,b|0)|0)==0){break}}h=c[g+44>>2]|0;if((h|0)==0){break b}else{g=h}}if((g|0)!=0){i=g;j=8;break a}}}while(0);a=c[f+36>>2]|0;if((a|0)==0){i=0;j=8;break}else{f=a}}if((j|0)==8){return i|0}return 0}function ii(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;e=(c[a+20>>2]|0)+24|0;a=d+44|0;if((c[e>>2]|0)==(d|0)){c[e>>2]=c[a>>2]}e=b+32|0;c[a>>2]=c[e>>2];c[e>>2]=d;return}function ji(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=a;a:while(1){a=c[d+44>>2]|0;b:do{if((a|0)!=0){e=xi(b)|0;f=G;g=a;while(1){h=g+24|0;if((c[h>>2]|0)==(e|0)&(c[h+4>>2]|0)==(f|0)){if((nb(c[g+20>>2]|0,b|0)|0)==0){break}}h=c[g+36>>2]|0;if((h|0)==0){break b}else{g=h}}if((g|0)!=0){i=g;j=9;break a}}}while(0);a=c[d+36>>2]|0;if((a|0)==0){i=0;j=9;break}else{d=a}}if((j|0)==9){return i|0}return 0}function ki(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0;a=Vd(40)|0;g=a;h=Vd((_n(e|0)|0)+1|0)|0;Zn(h|0,e|0)|0;e=f|32;f=a;c[f>>2]=e;c[f+4>>2]=(e|0)<0|0?-1:0;c[a+20>>2]=h;c[a+8>>2]=d;d=xi(h)|0;h=a+24|0;c[h>>2]=d;c[h+4>>2]=G;h=a+36|0;c[h>>2]=0;d=b+60|0;c[a+12>>2]=c[d>>2];c[a+32>>2]=b;c[d>>2]=(c[d>>2]|0)+1;d=b+44|0;c[h>>2]=c[d>>2];c[d>>2]=g;return g|0}function li(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;e=d|0;f=c[e>>2]|0;g=c[e+4>>2]|0;if(!((f&1024|0)==0&(g&0|0)==0)){h=a+64|0;c[d+84>>2]=c[(c[h>>2]|0)+84>>2];c[d+88>>2]=c[(c[h>>2]|0)+88>>2];c[d+28>>2]=c[(c[h>>2]|0)+28>>2];i=-65537;j=-1;k=f&i;l=g&j;m=e|0;c[m>>2]=k;n=e+4|0;c[n>>2]=l;return}do{if((f&16384|0)==0&(g&0|0)==0){if((b[d+58>>1]|0)!=0){o=5;break}p=a+80|0}else{o=5}}while(0);if((o|0)==5){o=a+80|0;c[d+84>>2]=c[(c[o>>2]|0)+84>>2];p=o}c[d+28>>2]=c[(c[p>>2]|0)+28>>2];c[d+88>>2]=c[(c[p>>2]|0)+88>>2];i=-65537;j=-1;k=f&i;l=g&j;m=e|0;c[m>>2]=k;n=e+4|0;c[n>>2]=l;return}function mi(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;e=fi(a,d)|0;d=e|0;a=c[d+4>>2]|0;c[d>>2]=c[d>>2]|4112;c[d+4>>2]=a;c[e+36>>2]=b;return e|0}function ni(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0;e=xi(d)|0;f=G;g=b[a+64>>1]|0;if(g<<16>>16==0){h=0;return h|0}i=c[a+48>>2]|0;a=g&65535;g=0;j=0;while(1){k=c[i+(j<<2)>>2]|0;l=k+16|0;if((c[l>>2]|0)==(e|0)&(c[l+4>>2]|0)==(f|0)){l=(nb(c[k+8>>2]|0,d|0)|0)==0;m=l?k:g}else{m=g}k=j+1|0;if((k|0)<(a|0)){g=m;j=k}else{h=m;break}}return h|0}function oi(a,d){a=a|0;d=d|0;var e=0,f=0;e=c[(c[d+36>>2]|0)+68>>2]|0;f=ih()|0;b[f+24>>1]=b[d+66>>1]|0;c[f+16>>2]=0;d=Di(a,e)|0;c[d+24>>2]=f;f=d|0;e=c[f+4>>2]|0;c[f>>2]=c[f>>2]|2097152;c[f+4>>2]=e;return d|0}function pi(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0;g=a+20|0;h=c[(c[g>>2]|0)+20>>2]|0;do{if((h|0)==(d|0)){i=0;j=Vd(0)|0;k=5}else{l=0;m=h;while(1){n=l+1|0;o=c[m+40>>2]|0;if((o|0)==(d|0)){break}else{l=n;m=o}}m=Vd(n<<2)|0;if((l|0)<=-1){i=n&65535;j=m;k=5;break}o=d|0;p=0;q=(c[g>>2]|0)+20|0;while(1){r=c[q>>2]|0;s=l-p|0;c[m+(s<<2)>>2]=r;b[r+66>>1]=s;if((c[r+68>>2]|0)==(c[r+24>>2]|0)){s=oi(a,r)|0;t=c[o+4>>2]|0;c[o>>2]=c[o>>2]|512;c[o+4>>2]=t;u=s}else{u=0}c[r+80>>2]=u;s=p+1|0;if((s|0)<(n|0)){p=s;q=r+40|0}else{break}}v=o;w=n&65535;x=m}}while(0);if((k|0)==5){v=d|0;w=i;x=j}c[d+68>>2]=f;c[d+48>>2]=x;b[d+64>>1]=w;w=c[v>>2]&-66561;x=c[v+4>>2]|0;c[v>>2]=w|1024;c[v+4>>2]=x;f=a+80|0;c[d+84>>2]=c[(c[f>>2]|0)+84>>2];c[d+88>>2]=c[(c[f>>2]|0)+88>>2];c[d+28>>2]=c[(c[f>>2]|0)+28>>2];b[d+66>>1]=Ei(d)|0;if((e|0)==0){return}c[v>>2]=w|9216;c[v+4>>2]=x;c[(c[g>>2]|0)+20>>2]=d;return}function qi(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=c[a+16>>2]|0;a:do{if((d|0)==0){e=0}else{a=d;while(1){if((nb(c[a+8>>2]|0,b|0)|0)==0){e=a;break a}f=c[a+40>>2]|0;if((f|0)==0){e=0;break}else{a=f}}}}while(0);return e|0}function ri(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;if((b|0)!=0){e=Fi(b,d)|0;return e|0}b=Fi(c[a+20>>2]|0,d)|0;if((b|0)!=0){e=b;return e|0}e=Fi(c[a+16>>2]|0,d)|0;return e|0}function si(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;f=a+88|0;a=c[(c[f>>2]|0)+76>>2]|0;if((e|0)==0){g=a}else{h=1;i=e;j=a;while(1){a=j+14|0;b[a>>1]=b[a>>1]&-5;a=i-1|0;k=j+16|0;l=c[k>>2]|0;if((l|0)==0&(a|0)!=0){m=Ai(c[f>>2]|0)|0;b[m+14>>1]=16;b[m+12>>1]=h;c[k>>2]=m;n=m}else{n=l}if((a|0)==0){g=n;break}else{h=h+1|0;i=a;j=n}}}if((g|0)!=0){n=g;do{g=n+14|0;b[g>>1]=b[g>>1]|4;n=c[n+16>>2]|0;}while((n|0)!=0)}if((d|0)==0){return}b[d+58>>1]=e;return}function ti(a,b){a=a|0;b=b|0;c[b+36>>2]=a;c[b+60>>2]=c[a+60>>2];return}function ui(a){a=a|0;var b=0;if((a|0)==0){return}else{b=a}while(1){a=c[b+32>>2]|0;dh(c[b+8>>2]|0,b+24|0);Ln(b);if((a|0)==0){break}else{b=a}}return}function vi(a){a=a|0;var d=0,f=0,g=0,h=0;if((a|0)==0){return}else{d=a}while(1){a=c[d+44>>2]|0;if((a|0)!=0){Gi(a)}a=c[d+32>>2]|0;if((a|0)!=0){Sh(0,a)}a=c[d+76>>2]|0;if((a|0)!=0){f=a;while(1){a=c[f+16>>2]|0;Ln(c[f+4>>2]|0);Ln(f);if((a|0)==0){break}else{f=a}}}Ln(c[d+8>>2]|0);f=d|0;do{if(!((c[f>>2]&8192|0)==0&(c[f+4>>2]&0|0)==0)){a=d+64|0;if((b[a>>1]|0)==0){break}g=d+48|0;h=0;do{Ln(c[(c[(c[g>>2]|0)+(h<<2)>>2]|0)+8>>2]|0);Ln(c[(c[g>>2]|0)+(h<<2)>>2]|0);h=h+1|0;}while((h|0)<(e[a>>1]|0))}}while(0);Ln(c[d+48>>2]|0);f=c[d+40>>2]|0;Ln(d);if((f|0)==0){break}else{d=f}}return}function wi(a,b){a=a|0;b=b|0;return Di(a,b)|0}function xi(b){b=b|0;var c=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;c=a[b]|0;if(c<<24>>24==0){d=0;e=0;return(G=d,e)|0}else{f=b;g=0;h=0;i=0;j=0;k=c}while(1){c=ko(k<<24>>24|0,(k<<24>>24<0|0?-1:0)|0,h|0)|0;b=c|j;c=G|i;l=f+1|0;m=g+1|0;n=a[l]|0;if(n<<24>>24!=0&(m|0)!=8){f=l;g=m;h=h+8|0;i=c;j=b;k=n}else{d=c;e=b;break}}return(G=d,e)|0}function yi(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0;if((a|0)==0){f=0;return f|0}else{g=a}a:while(1){a=g+24|0;do{if((c[a>>2]|0)==(d|0)&(c[a+4>>2]|0)==(e|0)){h=g|0;if(!((c[h>>2]&16384|0)==0&(c[h+4>>2]&0|0)==0)){break}if((nb(c[g+20>>2]|0,b|0)|0)==0){f=g;i=6;break a}}}while(0);a=c[g+44>>2]|0;if((a|0)==0){f=0;i=6;break}else{g=a}}if((i|0)==6){return f|0}return 0}function zi(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Vd(40)|0;c[d+4>>2]=c[b+16>>2];c[d+20>>2]=e;c[f+8>>2]=c[b+8>>2];c[f+24>>2]=d;c[f+12>>2]=c[b+12>>2];b=f;c[b>>2]=1;c[b+4>>2]=0;b=a+8|0;c[f+32>>2]=c[b>>2];c[b>>2]=f;return}function Ai(a){a=a|0;var b=0;b=Vd(20)|0;c[b>>2]=a;fo(b+4|0,0,16)|0;return b|0}function Bi(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0;if((a|0)==0){f=0;return f|0}else{g=a}while(1){a=g+16|0;if((c[a>>2]|0)==(d|0)&(c[a+4>>2]|0)==(e|0)){if((nb(c[g+8>>2]|0,b|0)|0)==0){f=g;h=5;break}}a=c[g+40>>2]|0;if((a|0)==0){f=0;h=5;break}else{g=a}}if((h|0)==5){return f|0}return 0}function Ci(a,d){a=a|0;d=d|0;var f=0,g=0,h=0;f=(d<<24>>24)-65|0;d=c[a+76>>2]|0;if((d|0)==0){return 0}else{g=d}while(1){if((e[g+12>>1]|0|0)==(f|0)){h=4;break}d=c[g+16>>2]|0;if((d|0)==0){h=5;break}else{g=d}}if((h|0)==4){return((b[g+14>>1]&4)==0?g:0)|0}else if((h|0)==5){return 0}return 0}function Di(a,b){a=a|0;b=b|0;var d=0,e=0;d=Vd(40)|0;e=d;c[d+8>>2]=b;b=d;c[b>>2]=1;c[b+4>>2]=0;b=a+40|0;c[d+12>>2]=c[b>>2];c[b>>2]=(c[b>>2]|0)+1;b=a+4|0;c[d+32>>2]=c[b>>2];c[b>>2]=e;return e|0}function Ei(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=b[a+64>>1]|0;if(d<<16>>16==0){e=0;return e|0}f=c[a+48>>2]|0;a=d&65535;d=0;g=0;while(1){h=c[(c[f+(g<<2)>>2]|0)+68>>2]|0;if((b[(c[h>>2]|0)+52>>1]|0)==5){i=(c[h+8>>2]|0)+65535|0;j=(i&65535)>>>0>(d&65535)>>>0?i&65535:d}else{j=d}i=g+1|0;if((i|0)<(a|0)){d=j;g=i}else{e=j;break}}return e|0}function Fi(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=c[a+16>>2]|0;if((d|0)==0){e=0;return e|0}else{f=d}while(1){d=c[f+4>>2]|0;a=c[f>>2]|0;if((d|0)==0){if((nb(c[a+8>>2]|0,b|0)|0)==0){e=a;g=6;break}}else{if((nb(d|0,b|0)|0)==0){e=a;g=6;break}}a=c[f+8>>2]|0;if((a|0)==0){e=0;g=6;break}else{f=a}}if((g|0)==6){return e|0}return 0}function Gi(a){a=a|0;var b=0;if((a|0)==0){return}else{b=a}while(1){a=c[b+36>>2]|0;Ln(c[b+20>>2]|0);Ln(b);if((a|0)==0){break}else{b=a}}return}function Hi(a,b,e){a=a|0;b=b|0;e=e|0;var f=0,g=0;f=d[24+e|0]|0;if((c[a>>2]|0)==0){g=255>>>(f>>>0)&e}else{g=c[b>>2]<<6|e&63}c[b>>2]=g;g=d[24+((f|256)+(c[a>>2]<<4))|0]|0;c[a>>2]=g;return g|0}function Ii(b){b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0;d=i;i=i+16|0;e=d|0;f=d+8|0;c[f>>2]=0;g=a[b]|0;a:do{if(g<<24>>24!=0){h=b;j=g;do{h=h+1|0;if((Hi(f,e,j&255)|0)==1){break a}j=a[h]|0;}while(j<<24>>24!=0)}}while(0);i=d;return(c[f>>2]|0)==0|0}function Ji(b,d){b=b|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;e=i;i=i+16|0;f=e|0;g=e+8|0;h=b+d|0;c[g>>2]=0;d=a[b]|0;do{if(d<<24>>24==0){j=b}else{k=b;l=d;while(1){m=k+1|0;if((Hi(g,f,l&255)|0)==1){n=k;break}o=a[m]|0;if(o<<24>>24==0){n=m;break}else{k=m;l=o}}if((c[g>>2]|0)==0){j=n;break}else{p=0}i=e;return p|0}}while(0);p=(j|0)==(h|0)|0;i=e;return p|0}function Ki(a,b,d){a=a|0;b=b|0;d=d|0;var f=0;b=c[a>>2]|0;a=c[b+((e[d>>1]|0)<<2)>>2]|0;f=(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16|0;d=c[f+4>>2]|0;b=a+16|0;c[b>>2]=c[f>>2];c[b+4>>2]=d;c[a>>2]=4194304;return}function Li(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;a=(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16|0;h=c[g+((e[d>>1]|0)<<2)>>2]|0;d=(c[a>>2]|0)==0&(c[a+4>>2]|0)==0?16096:19384;a=Vd(12)|0;g=Vd((_n(d|0)|0)+1|0)|0;c[a+8>>2]=g;Zn(g|0,d|0)|0;c[a>>2]=1;c[a+4>>2]=_n(d|0)|0;c[f>>2]=a;gh(h,f);i=b;return}function Mi(a){a=a|0;return ei(a,7752)|0}function Ni(){var a=0;a=Vd(24)|0;c[a+8>>2]=0;c[a>>2]=1;c[a+4>>2]=0;c[a+12>>2]=0;c[a+16>>2]=0;c[a+20>>2]=0;return a|0}function Oi(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,i=0,j=0,k=0,l=0,m=0.0,n=0,o=0,p=0,q=0;f=Ec(a,e)|0;a=G;g=e+16|0;i=c[g>>2]|0;j=c[g+4>>2]|0;k=b[(c[c[e+8>>2]>>2]|0)+52>>1]|0;e=c[d+20>>2]|0;if((e|0)==0){l=0;return l|0}m=+h[g>>3];g=i;d=g+4|0;n=g+8|0;if((k<<16>>16|0)==0){o=e;while(1){p=o|0;if((c[p>>2]|0)==(f|0)&(c[p+4>>2]|0)==(a|0)){p=(c[o+8>>2]|0)+16|0;if((c[p>>2]|0)==(i|0)&(c[p+4>>2]|0)==(j|0)){l=o;q=14;break}}p=c[o+16>>2]|0;if((p|0)==0){l=0;q=14;break}else{o=p}}if((q|0)==14){return l|0}}else if((k<<16>>16|0)==1){o=e;while(1){j=o|0;if((c[j>>2]|0)==(f|0)&(c[j+4>>2]|0)==(a|0)){if(+h[(c[o+8>>2]|0)+16>>3]==m){l=o;q=14;break}}j=c[o+16>>2]|0;if((j|0)==0){l=0;q=14;break}else{o=j}}if((q|0)==14){return l|0}}else{o=e;a:while(1){e=o|0;do{if((c[e>>2]|0)==(f|0)&(c[e+4>>2]|0)==(a|0)&k<<16>>16==2){j=c[(c[o+8>>2]|0)+16>>2]|0;if((j|0)==(g|0)){l=o;q=14;break a}if((c[j+4>>2]|0)!=(c[d>>2]|0)){break}if((nb(c[j+8>>2]|0,c[n>>2]|0)|0)==0){l=o;q=14;break a}}}while(0);e=c[o+16>>2]|0;if((e|0)==0){l=0;q=14;break}else{o=e}}if((q|0)==14){return l|0}}return 0}function Pi(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;var e=0;e=hh(c)|0;fj(a,b,e,hh(d)|0);return}function Qi(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Oi(a,b,d)|0;if((f|0)==0){Pi(a,b,d,e);return}else{fh(c[f+12>>2]|0,e);return}}function Ri(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;f=i;if((c[b>>2]|0)==100){Ae(c[a+116>>2]|0,6,15920,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}g=c[d+16>>2]|0;h=c[e+16>>2]|0;if((c[g+16>>2]|0)!=(c[h+16>>2]|0)){j=0;i=f;return j|0}e=c[(c[d+8>>2]|0)+4>>2]|0;d=c[(c[c[e>>2]>>2]|0)+88>>2]|0;k=c[(c[c[e+4>>2]>>2]|0)+88>>2]|0;e=c[g+20>>2]|0;if((e|0)==0){j=1;i=f;return j|0}g=c[h+20>>2]|0;h=e;e=c[b>>2]|0;a:while(1){l=e+1|0;c[b>>2]=l;if((g|0)==0){m=l;break}l=h|0;n=h+8|0;o=g;while(1){p=o|0;if((c[l>>2]|0)==(c[p>>2]|0)&(c[l+4>>2]|0)==(c[p+4>>2]|0)){if((wb[d&31](a,b,c[n>>2]|0,c[o+8>>2]|0)|0)!=0){break}}p=c[o+16>>2]|0;if((p|0)==0){q=9;break a}else{o=p}}n=wb[k&31](a,b,c[h+12>>2]|0,c[o+12>>2]|0)|0;if((o|0)==(g|0)){r=c[g+16>>2]|0}else{r=g}l=(c[b>>2]|0)-1|0;c[b>>2]=l;if((n|0)==0){j=0;q=17;break}p=c[h+16>>2]|0;if((p|0)==0){j=n;q=17;break}else{g=r;h=p;e=l}}if((q|0)==9){m=c[b>>2]|0}else if((q|0)==17){i=f;return j|0}c[b>>2]=m-1;j=0;i=f;return j|0}function Si(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=c[b+16>>2]|0;e=c[d+8>>2]|0;if((e|0)==0){return}f=e+16|0;if((c[f>>2]|0)==(a|0)){return}c[f>>2]=a;f=c[(c[c[(c[(c[b+8>>2]|0)+4>>2]|0)+4>>2]>>2]|0)+84>>2]|0;b=c[d+20>>2]|0;if((b|0)==0){return}else{g=b}do{b=c[g+12>>2]|0;if((c[b>>2]&1048576|0)==0){rb[f&127](a,b)}g=c[g+16>>2]|0;}while((g|0)!=0);return}function Ti(a){a=a|0;var b=0;b=c[a+16>>2]|0;a=c[b+8>>2]|0;if((a|0)!=0){c[a+8>>2]=0}gj(c[b+20>>2]|0);Ln(b);return}function Ui(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0;d=i;i=i+16|0;e=d|0;f=d+8|0;g=c[b+8>>2]|0;do{if((g|0)==0){h=c[a+4>>2]|0;j=0;k=c[h>>2]|0;l=c[h+4>>2]|0}else{h=g+16|0;if((c[h>>2]|0)==-1){i=d;return}if((c[g+8>>2]|0)==0){i=d;return}else{m=c[a+4>>2]|0;n=c[m+4>>2]|0;o=c[m>>2]|0;c[h>>2]=-1;j=1;k=o;l=n;break}}}while(0);a=c[b+20>>2]|0;if((a|0)!=0){g=e|0;n=f|0;o=a;while(1){a=o+12|0;h=c[a>>2]|0;m=o+8|0;p=c[m>>2]|0;q=c[o+16>>2]|0;do{if((c[p>>2]&7340032|0)==0){r=p+16|0;s=c[r>>2]|0;t=c[r+4>>2]|0;c[g>>2]=s;c[g+4>>2]=t;t=s|0;s=c[t>>2]|0;if((s|0)==1){kh(k,e);break}else{c[t>>2]=s-1;break}}}while(0);do{if((c[h>>2]&7340032|0)==0){p=h+16|0;s=c[p>>2]|0;t=c[p+4>>2]|0;c[n>>2]=s;c[n+4>>2]=t;t=s|0;s=c[t>>2]|0;if((s|0)==1){kh(l,f);break}else{c[t>>2]=s-1;break}}}while(0);Ln(c[m>>2]|0);Ln(c[a>>2]|0);Ln(o);if((q|0)==0){break}else{o=q}}}if((j|0)!=0){i=d;return}Ln(b);i=d;return}function Vi(a,b,d){a=a|0;b=b|0;d=d|0;var f=0;b=i;f=c[(c[(c[a>>2]|0)+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;if((c[f+4>>2]|0)!=0){Ae(c[a+116>>2]|0,6,19328,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a}a=f+20|0;gj(c[a>>2]|0);c[a>>2]=0;c[f+16>>2]=0;i=b;return}function Wi(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,i=0;b=c[a>>2]|0;f=c[b+((e[d+6>>1]|0)<<2)>>2]|0;g=c[b+((e[d>>1]|0)<<2)>>2]|0;h=Oi(a,c[(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0,c[b+((e[d+4>>1]|0)<<2)>>2]|0)|0;if((h|0)==0){i=f;fh(g,i);return}i=c[h+12>>2]|0;fh(g,i);return}function Xi(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0;b=c[a>>2]|0;a=c[(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;f=c[b+((e[d>>1]|0)<<2)>>2]|0;d=c[a+16>>2]|0;b=Xd()|0;c[b+16>>2]=d;g=b+12|0;c[g>>2]=Vd(d<<2)|0;d=c[a+20>>2]|0;if((d|0)==0){ch(f);h=f+16|0;i=h;c[i>>2]=b;j=f|0;c[j>>2]=0;return}else{k=0;l=d}while(1){d=hh(c[l+8>>2]|0)|0;c[(c[g>>2]|0)+(k<<2)>>2]=d;d=c[l+16>>2]|0;if((d|0)==0){break}else{k=k+1|0;l=d}}ch(f);h=f+16|0;i=h;c[i>>2]=b;j=f|0;c[j>>2]=0;return}function Yi(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0;b=c[a>>2]|0;f=c[(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;g=c[b+((e[d+4>>1]|0)<<2)>>2]|0;hj(a,c[f+4>>2]|0);d=Oi(a,f,g)|0;if((d|0)==0){return}g=d+16|0;a=c[g>>2]|0;b=d+20|0;if((a|0)!=0){c[a+20>>2]=c[b>>2]}a=c[b>>2]|0;if((a|0)!=0){c[a+16>>2]=c[g>>2]}a=f+20|0;if((d|0)==(c[a>>2]|0)){c[a>>2]=c[g>>2]}ij(d);d=f+16|0;c[d>>2]=(c[d>>2]|0)-1;return}function Zi(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;b=i;i=i+8|0;f=1;g=0;h=i;i=i+168|0;c[h>>2]=0;while(1)switch(f|0){case 1:j=b|0;k=c[a>>2]|0;l=c[(c[k+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;m=c[k+((e[d+4>>1]|0)<<2)>>2]|0;n=c[l+20>>2]|0;c[j>>2]=0;o=l+4|0;c[o>>2]=(c[o>>2]|0)+1;p=a+116|0;l=ia(8,c[p>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;q=co(l+8|0,f,h)|0;f=7;break;case 7:if((q|0)==0){f=2;break}else{f=5;break};case 2:if((n|0)==0){f=4;break}else{r=n;f=3;break};case 3:l=c[r+12>>2]|0;la(2,a|0,j|0,0,m|0,2,(k=i,i=i+16|0,c[k>>2]=c[r+8>>2],c[k+8>>2]=l,k)|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=k;k=c[r+16>>2]|0;if((k|0)==0){f=4;break}else{r=k;f=3;break};case 4:c[o>>2]=(c[o>>2]|0)-1;ja(28,c[p>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;f=6;break;case 5:c[o>>2]=(c[o>>2]|0)-1;ja(32,c[p>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;f=6;break;case 6:i=b;return;case-1:if((g|0)==1){q=u;f=7}t=u=0;break}}function _i(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;h=f|0;c[h>>2]=(Oi(a,c[(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0,c[g+((e[d+4>>1]|0)<<2)>>2]|0)|0)!=0;c[h+4>>2]=0;gh(c[g+((e[d>>1]|0)<<2)>>2]|0,f);i=b;return}function $i(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,v=0,w=0,x=0,y=0,z=0;b=i;i=i+8|0;f=1;g=0;h=i;i=i+168|0;c[h>>2]=0;while(1)switch(f|0){case 1:j=b|0;k=c[a>>2]|0;l=c[(c[k+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0;m=c[k+((e[d+4>>1]|0)<<2)>>2]|0;n=c[k+((e[d>>1]|0)<<2)>>2]|0;o=c[c[(c[m+8>>2]|0)+4>>2]>>2]|0;p=c[l+20>>2]|0;q=c[a+104>>2]|0;c[j>>2]=0;r=q+4|0;s=c[r>>2]|0;ka(76,a|0,c[l+16>>2]<<1|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;v=l+4|0;c[v>>2]=(c[v>>2]|0)+1;w=a+116|0;l=ia(8,c[w>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;x=co(l+8|0,f,h)|0;f=8;break;case 8:if((x|0)==0){f=2;break}else{f=6;break};case 2:if((p|0)==0){f=5;break}else{f=3;break};case 3:y=q|0;z=p;f=4;break;case 4:l=la(2,a|0,j|0,o|0,m|0,1,(k=i,i=i+8|0,c[k>>2]=c[z+12>>2],k)|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;i=k;k=ia(10,c[z+8>>2]|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;c[(c[y>>2]|0)+(c[r>>2]<<2)>>2]=k;k=ia(10,l|0)|0;if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;c[(c[y>>2]|0)+((c[r>>2]|0)+1<<2)>>2]=k;c[r>>2]=(c[r>>2]|0)+2;k=c[z+16>>2]|0;if((k|0)==0){f=5;break}else{z=k;f=4;break};case 5:na(46,a|0,s|0,n|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;c[v>>2]=(c[v>>2]|0)-1;ja(28,c[w>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;f=7;break;case 6:c[v>>2]=(c[v>>2]|0)-1;ja(32,c[w>>2]|0);if((t|0)!=0&(u|0)!=0){g=eo(c[t>>2]|0,h)|0;if((g|0)>0){f=-1;break}else return}t=u=0;f=7;break;case 7:i=b;return;case-1:if((g|0)==1){x=u;f=8}t=u=0;break}}function aj(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;d=i;i=i+8|0;g=d|0;h=c[a>>2]|0;j=c[(c[h+((e[f+2>>1]|0)<<2)>>2]|0)+16>>2]|0;k=c[(c[h+((e[f+4>>1]|0)<<2)>>2]|0)+16>>2]|0;l=c[h+((e[f>>1]|0)<<2)>>2]|0;f=Ni()|0;h=c[l+8>>2]|0;if((b[h+14>>1]&2)!=0){vc(a,h,f)}h=c[j+20>>2]|0;if((h|0)!=0){j=h;do{Pi(a,f,c[j+8>>2]|0,c[j+12>>2]|0);j=c[j+16>>2]|0;}while((j|0)!=0)}j=k+16|0;h=c[j>>2]|0;if((h|0)==0){m=g;c[m>>2]=f;gh(l,g);i=d;return}n=k+12|0;k=0;o=h;while(1){h=c[(c[(c[(c[n>>2]|0)+(k<<2)>>2]|0)+16>>2]|0)+20>>2]|0;if((h|0)==0){p=o}else{q=h;do{Qi(a,f,c[q+8>>2]|0,c[q+12>>2]|0);q=c[q+16>>2]|0;}while((q|0)!=0);p=c[j>>2]|0}q=k+1|0;if(q>>>0<p>>>0){k=q;o=p}else{break}}m=g;c[m>>2]=f;gh(l,g);i=d;return}function bj(a,c,d){a=a|0;c=c|0;d=d|0;kj(a,b[d>>1]|0,b[d+2>>1]|0,b[d+4>>1]|0,0);return}function cj(a,c,d){a=a|0;c=c|0;d=d|0;kj(a,b[d>>1]|0,b[d+2>>1]|0,b[d+4>>1]|0,1);return}function dj(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0;b=i;i=i+8|0;f=b|0;g=c[a>>2]|0;a=f|0;c[a>>2]=c[(c[(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16>>2]|0)+16>>2];c[a+4>>2]=0;gh(c[g+((e[d>>1]|0)<<2)>>2]|0,f);i=b;return}function ej(a){a=a|0;return ei(a,5632)|0}function fj(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0;hj(a,c[b+4>>2]|0);f=Vd(24)|0;g=f;h=Ec(a,d)|0;a=f;c[a>>2]=h;c[a+4>>2]=G;c[f+8>>2]=d;c[f+12>>2]=e;e=b+20|0;d=c[e>>2]|0;if((d|0)!=0){c[d+20>>2]=g}c[f+20>>2]=0;c[f+16>>2]=c[e>>2];c[e>>2]=g;g=b+16|0;c[g>>2]=(c[g>>2]|0)+1;return}function gj(a){a=a|0;var b=0;if((a|0)==0){return}else{b=a}while(1){a=c[b+16>>2]|0;ij(b);if((a|0)==0){break}else{b=a}}return}function hj(a,b){a=a|0;b=b|0;var d=0;d=i;if((b|0)==0){i=d;return}Ae(c[a+116>>2]|0,6,19328,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a;i=d;return}function ij(a){a=a|0;var b=0;b=a+8|0;ch(c[b>>2]|0);Ln(c[b>>2]|0);b=a+12|0;ch(c[b>>2]|0);Ln(c[b>>2]|0);Ln(a);return}function jj(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0;f=i;i=i+8|0;g=f|0;h=a+104|0;j=c[(c[h>>2]|0)+4>>2]|0;k=Ni()|0;l=c[c[h>>2]>>2]|0;m=c[e+8>>2]|0;if((b[m+14>>1]&2)!=0){vc(a,m,k)}if((j|0)>(d|0)){m=d;do{fj(a,k,c[l+(m<<2)>>2]|0,c[l+(m+1<<2)>>2]|0);m=m+2|0;}while((m|0)<(j|0))}c[(c[h>>2]|0)+4>>2]=d;c[g>>2]=k;gh(e,g);i=f;return}function kj(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0;g=i;i=i+8|0;h=1;j=0;k=i;i=i+168|0;c[k>>2]=0;while(1)switch(h|0){case 1:l=g|0;m=c[a>>2]|0;n=c[(c[m+((d&65535)<<2)>>2]|0)+16>>2]|0;o=c[m+((e&65535)<<2)>>2]|0;p=c[m+((b&65535)<<2)>>2]|0;q=c[c[(c[o+8>>2]|0)+4>>2]>>2]|0;r=c[n+20>>2]|0;s=c[a+104>>2]|0;c[l>>2]=0;v=s+4|0;w=c[v>>2]|0;ka(76,a|0,c[n+16>>2]<<1|0);if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;x=n+4|0;c[x>>2]=(c[x>>2]|0)+1;y=a+116|0;n=ia(8,c[y>>2]|0)|0;if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;z=co(n+8|0,h,k)|0;h=10;break;case 10:if((z|0)==0){h=2;break}else{h=8;break};case 2:if((r|0)==0){h=7;break}else{h=3;break};case 3:A=f;B=(f|0)<0|0?-1:0;C=s|0;D=r;h=4;break;case 4:E=c[D+8>>2]|0;F=c[D+12>>2]|0;n=la(2,a|0,l|0,q|0,o|0,2,(m=i,i=i+16|0,c[m>>2]=E,c[m+8>>2]=F,m)|0)|0;if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;i=m;m=n+16|0;if((c[m>>2]|0)==(A|0)&(c[m+4>>2]|0)==(B|0)){h=5;break}else{h=6;break};case 5:m=ia(10,E|0)|0;if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;c[(c[C>>2]|0)+(c[v>>2]<<2)>>2]=m;m=ia(10,F|0)|0;if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;c[(c[C>>2]|0)+((c[v>>2]|0)+1<<2)>>2]=m;c[v>>2]=(c[v>>2]|0)+2;h=6;break;case 6:m=c[D+16>>2]|0;if((m|0)==0){h=7;break}else{D=m;h=4;break};case 7:na(46,a|0,w|0,p|0);if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;c[x>>2]=(c[x>>2]|0)-1;ja(28,c[y>>2]|0);if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;h=9;break;case 8:c[x>>2]=(c[x>>2]|0)-1;ja(32,c[y>>2]|0);if((t|0)!=0&(u|0)!=0){j=eo(c[t>>2]|0,k)|0;if((j|0)>0){h=-1;break}else return}t=u=0;h=9;break;case 9:i=g;return;case-1:if((j|0)==1){z=u;h=10}t=u=0;break}}function lj(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;b=d+16|0;d=e+16|0;return(c[b>>2]|0)==(c[d>>2]|0)&(c[b+4>>2]|0)==(c[d+4>>2]|0)&1|0}function mj(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0;b=c[a>>2]|0;a=(c[b+((e[d+2>>1]|0)<<2)>>2]|0)+16|0;f=c[a>>2]|0;g=c[a+4>>2]|0;a=c[b+((e[d>>1]|0)<<2)>>2]|0;c[a>>2]=4194304;h[a+16>>3]=+(f>>>0)+ +(g|0)*4294967296.0;return}function nj(a,b,d){a=a|0;b=b|0;d=d|0;var f=0,g=0,h=0,j=0;b=i;i=i+40|0;f=b+32|0;g=c[a>>2]|0;a=(c[g+((e[d+2>>1]|0)<<2)>>2]|0)+16|0;h=c[a+4>>2]|0;j=c[g+((e[d>>1]|0)<<2)>>2]|0;d=b|0;va(d|0,32,15472,(g=i,i=i+16|0,c[g>>2]=c[a>>2],c[g+8>>2]=h,g)|0)|0;i=g;g=Vd(12)|0;h=Vd((_n(d|0)|0)+1|0)|0;Zn(h|0,d|0)|0;c[g+8>>2]=h;c[g+4>>2]=_n(d|0)|0;c[g>>2]=1;c[f>>2]=g;gh(j,f);i=b;return}function oj(a){a=a|0;return ei(a,5272)|0}function pj(a,d,e){a=a|0;d=d|0;e=e|0;var f=0;f=Vd(56)|0;c[f>>2]=1;c[f+12>>2]=d;c[f+16>>2]=e;c[f+24>>2]=a;c[f+28>>2]=0;c[f+36>>2]=0;c[f+40>>2]=0;c[f+8>>2]=0;c[f+52>>2]=0;b[f+48>>1]=-1;b[f+44>>1]=0;b[f+46>>1]=0;return f|0}function qj(a,d){a=a|0;d=d|0;var e=0;e=Vd(56)|0;c[e>>2]=1;c[e+12>>2]=a;c[e+16>>2]=d;c[e+24>>2]=0;c[e+28>>2]=0;c[e+36>>2]=0;c[e+40>>2]=0;c[e+8>>2]=0;c[e+52>>2]=0;b[e+48>>1]=-1;b[e+44>>1]=0;b[e+46>>1]=0;return e|0}function rj(a){a=a|0;var b=0;b=Vd(56)|0;Yn(b|0,a|0,56)|0;return b|0}function sj(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=c[b+16>>2]|0;b=c[d+8>>2]|0;if((b|0)==0){return}e=b+16|0;if((c[e>>2]|0)==(a|0)){return}c[e>>2]=a;e=c[d+40>>2]|0;b=c[d+36>>2]|0;if((b|0)>0){f=0}else{return}do{d=c[e+(f<<2)>>2]|0;do{if((d|0)!=0){if((c[d>>2]&7340032|0)!=0){break}g=c[(c[c[d+8>>2]>>2]|0)+84>>2]|0;if((g|0)==0){break}rb[g&127](a,d)}}while(0);f=f+1|0;}while((f|0)<(b|0));return}function tj(a){a=a|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;d=c[a+16>>2]|0;a=d+40|0;f=c[a>>2]|0;if((f|0)==0){g=d+52|0;h=c[g>>2]|0;do{if((h|0)==0){i=0}else{j=d+48|0;if((b[j>>1]|0)==0){i=h;break}else{k=0;l=h}while(1){Ln(c[l+(k<<4)+4>>2]|0);m=k+1|0;n=c[g>>2]|0;if((m|0)<(e[j>>1]|0)){k=m;l=n}else{i=n;break}}}}while(0);Ln(i);Ln(c[d+28>>2]|0);o=d;Ln(o);return}i=c[d+8>>2]|0;if((i|0)==0){p=f}else{c[i+8>>2]=0;p=c[a>>2]|0}a=c[d+36>>2]|0;if((a|0)>0){i=0;do{f=c[p+(i<<2)>>2]|0;do{if((f|0)!=0){l=f+4|0;k=(c[l>>2]|0)-1|0;c[l>>2]=k;if((k|0)!=0){break}ch(f);Ln(f)}}while(0);i=i+1|0;}while((i|0)<(a|0))}Ln(p);o=d;Ln(o);return}function uj(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;a=i;i=i+8|0;d=a|0;e=c[b+8>>2]|0;do{if((e|0)==0){f=0}else{g=e+16|0;if((c[g>>2]|0)==-1){i=a;return}if((c[e+8>>2]|0)==0){i=a;return}else{c[g>>2]=-1;f=1;break}}}while(0);e=c[b+40>>2]|0;g=c[b+36>>2]|0;if((g|0)>0){h=d|0;j=0;do{k=c[e+(j<<2)>>2]|0;do{if((k|0)!=0){l=k+4|0;m=(c[l>>2]|0)-1|0;c[l>>2]=m;if((m|0)!=0){break}do{if((c[k>>2]&7340032|0)==0){m=k+16|0;l=c[m>>2]|0;n=c[m+4>>2]|0;c[h>>2]=l;c[h+4>>2]=n;n=l|0;l=c[n>>2]|0;if((l|0)==1){kh(c[k+8>>2]|0,d);break}else{c[n>>2]=l-1;break}}}while(0);Ln(k)}}while(0);j=j+1|0;}while((j|0)<(g|0))}Ln(e);if((f|0)!=0){i=a;return}Ln(b);i=a;return}function vj(){var a=0;a=Vd(16)|0;c[a>>2]=Vd(16)|0;c[a+4>>2]=0;c[a+8>>2]=4;return a|0}function wj(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=(c[a+4>>2]|0)+b|0;b=a+8|0;e=c[b>>2]|0;if(d>>>0>e>>>0){f=e}else{return}while(1){g=f<<1;if(d>>>0>g>>>0){f=g}else{break}}c[b>>2]=g;g=a|0;c[g>>2]=Wd(c[g>>2]|0,f<<3)|0;return}function xj(a,b){a=a|0;b=b|0;var d=0;d=a+4|0;c[(c[a>>2]|0)+(c[d>>2]<<2)>>2]=b;c[d>>2]=(c[d>>2]|0)+1;return}function yj(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=a+4|0;e=c[d>>2]|0;f=e+1|0;g=a+8|0;if((f|0)==(c[g>>2]|0)){c[g>>2]=f<<1;g=a|0;h=Wd(c[g>>2]|0,f<<3)|0;c[g>>2]=h;i=c[d>>2]|0;j=h}else{i=e;j=c[a>>2]|0}c[j+(i<<2)>>2]=b;c[d>>2]=(c[d>>2]|0)+1;return}function zj(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=a+8|0;f=c[e>>2]|0;if(f>>>0>b>>>0){g=c[a>>2]|0;h=g+(b<<2)|0;c[h>>2]=d;return}else{i=f}while(1){j=i<<1;if(j>>>0>b>>>0){break}else{i=j}}c[e>>2]=j;j=a|0;a=Wd(c[j>>2]|0,i<<3)|0;c[j>>2]=a;g=a;h=g+(b<<2)|0;c[h>>2]=d;return}function Aj(a){a=a|0;var b=0,d=0;b=a+4|0;d=(c[b>>2]|0)-1|0;c[b>>2]=d;return c[(c[a>>2]|0)+(d<<2)>>2]|0}function Bj(a){a=a|0;var d=0,e=0;d=c[a+76>>2]|0;if((d|0)!=0){e=d;do{d=e+14|0;b[d>>1]=b[d>>1]|2;e=c[e+16>>2]|0;}while((e|0)!=0)}e=a|0;a=c[e+4>>2]|0;c[e>>2]=c[e>>2]|16384;c[e+4>>2]=a;return}function Cj(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0;g=i;i=i+24|0;h=g|0;c[h>>2]=e;b[h+12>>1]=0;e=a+4|0;j=c[e>>2]|0;c[h+4>>2]=(c[a>>2]|0)+(j-f<<2);c[h+8>>2]=f;b[h+14>>1]=d;c[h+16>>2]=0;d=Ij(h)|0;if((d|0)!=0){k=d;l=j;m=l-f|0;c[e>>2]=m;i=g;return k|0}j=Jj(h)|0;k=j;l=c[e>>2]|0;m=l-f|0;c[e>>2]=m;i=g;return k|0}function Dj(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=i;i=i+136|0;g=f|0;h=f+8|0;c[g>>2]=-1;fo(h|0,0,128)|0;do{if((d|0)<(e|0)){j=h|0;k=a|0;l=d;do{Kj(j,c[(c[k>>2]|0)+(l<<2)>>2]|0,g);l=l+1|0;}while((l|0)<(e|0));l=c[g>>2]|0;k=l+1|0;c[g>>2]=k;if((l|0)>-1){m=0;n=0}else{o=0;break}while(1){l=c[h+(n<<2)>>2]|0;if((l|0)==0){p=m}else{yj(a,l);p=m+1|0}l=n+1|0;if((l|0)<(k|0)){m=p;n=l}else{o=p;break}}}else{c[g>>2]=0;o=0}}while(0);g=Cj(a,0,b,o)|0;i=f;return g|0}function Ej(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=c[a+12>>2]|0;f=d+8|0;if((c[f>>2]|0)==0){g=0}else{h=d+4|0;i=0;while(1){j=c[(c[h>>2]|0)+(i<<2)>>2]|0;if((b[j+14>>1]&64)==0){yj(a,j)}else{yj(a,e)}j=i+1|0;if(j>>>0<(c[f>>2]|0)>>>0){i=j}else{g=j;break}}}return Cj(a,0,c[d>>2]|0,g)|0}function Fj(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;f=d|0;g=c[f>>2]|0;h=c[g+36>>2]|0;i=b[h+58>>1]|0;j=i<<16>>16;if(i<<16>>16==0){k=c[h+24>>2]|0;return k|0}l=c[a+4>>2]|0;if(i<<16>>16>0){i=a+12|0;m=0;do{yj(a,c[i>>2]|0);m=m+1|0;}while((m|0)<(j|0));n=c[f>>2]|0}else{n=g}g=c[n+68>>2]|0;do{if((c[g+8>>2]|0)!=0){n=c[c[g+4>>2]>>2]|0;f=n+8|0;if((c[f>>2]|0)==0){break}m=n+4|0;n=d+4|0;i=0;do{zj(a,(e[(c[(c[m>>2]|0)+(i<<2)>>2]|0)+12>>1]|0)+l|0,c[(c[n>>2]|0)+(i<<2)>>2]|0);i=i+1|0;}while(i>>>0<(c[f>>2]|0)>>>0)}}while(0);k=Cj(a,0,h,j)|0;return k|0}function Gj(a,b){a=a|0;b=b|0;a=Lj(b)|0;c[b+24>>2]=a;c[b+76>>2]=a;return a|0}function Hj(a){a=a|0;Ln(c[a>>2]|0);Ln(a);return}function Ij(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0;d=c[(c[a>>2]|0)+76>>2]|0;if((d|0)==0){e=0;return e|0}f=a+8|0;g=a+14|0;h=a+4|0;a=d;a:while(1){d=c[a+4>>2]|0;b:do{if((d|0)!=0){i=c[a+8>>2]|0;if((i|0)!=(c[f>>2]|0)){break}if(((b[g>>1]^b[a+14>>1])&-83)<<16>>16!=0){break}if((i|0)==0){e=a;j=11;break a}k=c[h>>2]|0;l=0;while(1){m=l+1|0;if((c[d+(l<<2)>>2]|0)!=(c[k+(l<<2)>>2]|0)){break b}if(m>>>0<i>>>0){l=m}else{e=a;j=11;break a}}}}while(0);d=c[a+16>>2]|0;if((d|0)==0){e=0;j=11;break}else{a=d}}if((j|0)==11){return e|0}return 0}function Jj(a){a=a|0;var b=0,d=0,e=0,f=0,g=0;b=a|0;d=Lj(c[b>>2]|0)|0;e=c[a+8>>2]|0;f=d;g=a;c[f>>2]=c[g>>2];c[f+4>>2]=c[g+4>>2];c[f+8>>2]=c[g+8>>2];c[f+12>>2]=c[g+12>>2];c[f+16>>2]=c[g+16>>2];if((e|0)==0){c[d+4>>2]=0;c[(c[b>>2]|0)+24>>2]=d}else{b=e<<2;g=Vd(b)|0;Yn(g|0,c[a+4>>2]|0,b)|0;c[d+4>>2]=g}c[d+8>>2]=e;e=(c[d>>2]|0)+76|0;c[d+16>>2]=c[e>>2];c[e>>2]=d;Mj(d);return d|0}function Kj(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0;if((d|0)==0){return}g=d+4|0;h=c[g>>2]|0;if((h|0)!=0){i=d+8|0;if((c[i>>2]|0)==0){return}else{j=0;k=h}while(1){Kj(a,c[k+(j<<2)>>2]|0,f);h=j+1|0;if(h>>>0>=(c[i>>2]|0)>>>0){break}j=h;k=c[g>>2]|0}return}if((b[(c[d>>2]|0)+52>>1]|0)!=12){return}g=e[d+12>>1]|0;if((g|0)>(c[f>>2]|0)){c[f>>2]=g}c[a+(g<<2)>>2]=d;return}function Lj(a){a=a|0;var b=0;b=Vd(20)|0;c[b>>2]=a;fo(b+4|0,0,16)|0;return b|0}function Mj(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0;d=c[a+4>>2]|0;do{if((d|0)==0){e=0}else{f=c[a+8>>2]|0;if((f|0)==0){e=0;break}g=a+14|0;h=0;i=0;while(1){j=c[d+(h<<2)>>2]|0;if((j|0)==0){k=i}else{b[g>>1]=b[g>>1]|b[j+14>>1]&82;k=c[c[j>>2]>>2]|i}j=h+1|0;if(j>>>0<f>>>0){h=j;i=k}else{break}}if((k&65536|0)==0){e=k;break}if((b[(c[a>>2]|0)+52>>1]|0)==5){e=k;break}if((f|0)==0){l=0}else{i=0;h=0;while(1){g=c[d+(i<<2)>>2]|0;if((g|0)==0){m=h}else{j=c[g>>2]|0;g=j|0;m=(c[g>>2]&65536|0)==0&(c[g+4>>2]&0|0)==0?h:j}j=i+1|0;if(j>>>0<f>>>0){i=j;h=m}else{l=m;break}}}Bj(l);e=k}}while(0);k=c[a>>2]|0;if(((c[k>>2]|e)&16384|0)==0&(c[k+4>>2]&0|0)==0){return}k=a+14|0;b[k>>1]=b[k>>1]|2;return}function Nj(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0,P=0,Q=0,R=0,S=0,T=0,U=0,V=0,W=0,X=0,Y=0,Z=0,_=0,$=0,aa=0,ba=0,ca=0,da=0,ea=0,fa=0,ga=0,ha=0,ia=0,ja=0,ka=0,la=0,ma=0,na=0,oa=0,pa=0,qa=0,ra=0,sa=0,ta=0,ua=0,va=0;f=e;g=c[f>>2]|0;h=c[f+4>>2]|0;f=e+8|0;e=c[f>>2]|0;i=c[f+4>>2]|0;f=0<<24|0>>>8;j=d<<24|0>>>8;k=b;l=g^1886610805;m=h^1936682341;n=e^1852075885;o=i^1685025377;p=g^1852142177;g=h^1819895653;h=e^2037671283;e=i^1952801890;if(d>>>0>7>>>0){i=d-8|0;q=i&-8;r=b+(q+8)|0;b=m;s=l;t=o;u=n;v=g;w=p;x=e;y=h;z=k;A=d;while(1){B=c[z>>2]|0;C=c[z+4>>2]|0;D=A-8|0;E=B^y;F=C^x;H=io(u,t,s,b)|0;I=G;J=io(E,F,w,v)|0;K=G;L=(u<<13|0>>>19|(t>>>19|0<<13))^H;M=(t<<13|u>>>19|(0>>>19|0<<13))^I;N=(E<<16|0>>>16|(F>>>16|0<<16))^J;O=(F<<16|E>>>16|(0>>>16|0<<16))^K;E=io(J,K,L,M)|0;K=G;J=io(N,O,I|0,H|0)|0;H=G;I=E^(L<<17|0>>>15|(M>>>15|0<<17));F=K^(M<<17|L>>>15|(0>>>15|0<<17));L=(N<<21|0>>>11|(O>>>11|0<<21))^J;M=(O<<21|N>>>11|(0>>>11|0<<21))^H;N=io(J,H,I,F)|0;H=G;J=io(L,M,K|0,E|0)|0;E=G;K=(I<<13|0>>>19|(F>>>19|0<<13))^N;O=(F<<13|I>>>19|(0>>>19|0<<13))^H;I=(L<<16|0>>>16|(M>>>16|0<<16))^J;F=(M<<16|L>>>16|(0>>>16|0<<16))^E;L=io(J,E,K,O)|0;E=G;J=io(I,F,H|0,N|0)|0;N=G;P=(K<<17|0>>>15|(O>>>15|0<<17))^L;Q=(O<<17|K>>>15|(0>>>15|0<<17))^E;R=(I<<21|0>>>11|(F>>>11|0<<21))^J;S=(F<<21|I>>>11|(0>>>11|0<<21))^N;T=E|0;U=L|0;V=J^B;W=N^C;if(D>>>0>7>>>0){b=W;s=V;t=Q;u=P;v=U;w=T;x=S;y=R;z=z+8|0;A=D}else{break}}X=W;Y=V;Z=Q;_=P;$=U;aa=T;ba=S;ca=R;da=r;ea=i-q|0}else{X=m;Y=l;Z=o;_=n;$=g;aa=p;ba=e;ca=h;da=k;ea=d}d=da;switch(ea|0){case 2:{fa=0;ga=11;break};case 1:{ha=0;ia=0;ga=12;break};case 5:{ja=0;ka=0;ga=8;break};case 3:{fa=a[d+2|0]|0;ga=11;break};case 4:{la=0;ma=0;na=0;ga=9;break};case 7:{oa=a[d+6|0]|0;ga=7;break};case 6:{oa=0;ga=7;break};default:{pa=0;qa=0;ra=0;sa=0;ta=0;ua=0;va=0}}if((ga|0)==7){ja=a[d+5|0]|0;ka=oa;ga=8}else if((ga|0)==11){ha=a[d+1|0]|0;ia=fa;ga=12}if((ga|0)==8){la=a[d+4|0]|0;ma=ja;na=ka;ga=9}else if((ga|0)==12){pa=0;qa=0;ra=0;sa=0;ta=a[d]|0;ua=ha;va=ia}if((ga|0)==9){ga=c[da>>2]|0;pa=la;qa=ma;ra=na;sa=ga&-16777216;ta=ga&255;ua=ga>>>8&255;va=ga>>>16&255}ga=0<<16|0>>>16|f|(0<<8|0>>>24)|(sa|(va&255)<<16|(ua&255)<<8|ta&255)|0;ta=(ra&255)<<16|0>>>16|j|((qa&255)<<8|0>>>24)|pa&255;pa=ga^ca;ca=ta^ba;ba=io(_,Z,Y,X)|0;X=G;Y=io(pa,ca,aa,$)|0;$=G;aa=(_<<13|0>>>19|(Z>>>19|0<<13))^ba;qa=(Z<<13|_>>>19|(0>>>19|0<<13))^X;_=(pa<<16|0>>>16|(ca>>>16|0<<16))^Y;Z=(ca<<16|pa>>>16|(0>>>16|0<<16))^$;pa=io(Y,$,aa,qa)|0;$=G;Y=io(_,Z,X|0,ba|0)|0;ba=G;X=pa^(aa<<17|0>>>15|(qa>>>15|0<<17));ca=$^(qa<<17|aa>>>15|(0>>>15|0<<17));aa=(_<<21|0>>>11|(Z>>>11|0<<21))^Y;qa=(Z<<21|_>>>11|(0>>>11|0<<21))^ba;_=io(Y,ba,X,ca)|0;ba=G;Y=io(aa,qa,$|0,pa|0)|0;pa=G;$=(X<<13|0>>>19|(ca>>>19|0<<13))^_;Z=(ca<<13|X>>>19|(0>>>19|0<<13))^ba;X=(aa<<16|0>>>16|(qa>>>16|0<<16))^Y;ca=(qa<<16|aa>>>16|(0>>>16|0<<16))^pa;aa=io(Y,pa,$,Z)|0;pa=G;Y=io(X,ca,ba|0,_|0)|0;_=G;ba=($<<17|0>>>15|(Z>>>15|0<<17))^aa;qa=(Z<<17|$>>>15|(0>>>15|0<<17))^pa;$=(X<<21|0>>>11|(ca>>>11|0<<21))^Y;Z=(ca<<21|X>>>11|(0>>>11|0<<21))^_;X=io(Y^ga,_^ta,ba,qa)|0;ta=G;_=io($,Z,pa^255,aa^0)|0;aa=G;pa=X^(ba<<13|0>>>19|(qa>>>19|0<<13));ga=ta^(qa<<13|ba>>>19|(0>>>19|0<<13));ba=($<<16|0>>>16|(Z>>>16|0<<16))^_;qa=(Z<<16|$>>>16|(0>>>16|0<<16))^aa;$=io(_,aa,pa,ga)|0;aa=G;_=io(ba,qa,ta|0,X|0)|0;X=G;ta=(pa<<17|0>>>15|(ga>>>15|0<<17))^$;Z=(ga<<17|pa>>>15|(0>>>15|0<<17))^aa;pa=(ba<<21|0>>>11|(qa>>>11|0<<21))^_;ga=(qa<<21|ba>>>11|(0>>>11|0<<21))^X;ba=io(_,X,ta,Z)|0;X=G;_=io(pa,ga,aa|0,$|0)|0;$=G;aa=(ta<<13|0>>>19|(Z>>>19|0<<13))^ba;qa=(Z<<13|ta>>>19|(0>>>19|0<<13))^X;ta=(pa<<16|0>>>16|(ga>>>16|0<<16))^_;Z=(ga<<16|pa>>>16|(0>>>16|0<<16))^$;pa=io(_,$,aa,qa)|0;$=G;_=io(ta,Z,X|0,ba|0)|0;ba=G;X=(aa<<17|0>>>15|(qa>>>15|0<<17))^pa;ga=(qa<<17|aa>>>15|(0>>>15|0<<17))^$;aa=(ta<<21|0>>>11|(Z>>>11|0<<21))^_;qa=(Z<<21|ta>>>11|(0>>>11|0<<21))^ba;ta=io(_,ba,X,ga)|0;ba=G;_=io(aa,qa,$|0,pa|0)|0;pa=G;$=(X<<13|0>>>19|(ga>>>19|0<<13))^ta;Z=(ga<<13|X>>>19|(0>>>19|0<<13))^ba;X=(aa<<16|0>>>16|(qa>>>16|0<<16))^_;ga=(qa<<16|aa>>>16|(0>>>16|0<<16))^pa;aa=io(_,pa,$,Z)|0;pa=G;_=io(X,ga,ba|0,ta|0)|0;ta=G;ba=($<<17|0>>>15|(Z>>>15|0<<17))^aa;qa=(Z<<17|$>>>15|(0>>>15|0<<17))^pa;$=(X<<21|0>>>11|(ga>>>11|0<<21))^_;Z=(ga<<21|X>>>11|(0>>>11|0<<21))^ta;X=io(_,ta,ba,qa)|0;ta=G;_=io($,Z,pa|0,aa|0)|0;aa=G;pa=(ba<<13|0>>>19|(qa>>>19|0<<13))^X;X=(qa<<13|ba>>>19|(0>>>19|0<<13))^ta;ta=($<<16|0>>>16|(Z>>>16|0<<16))^_;ba=(Z<<16|$>>>16|(0>>>16|0<<16))^aa;$=io(_,aa,pa,X)|0;aa=G;return(G=(X<<17|pa>>>15|(0>>>15|0<<17))^aa^$^(ba<<21|ta>>>11|(0>>>11|0<<21)),(pa<<17|0>>>15|(X>>>15|0<<17))^$^aa^(ta<<21|0>>>11|(ba>>>11|0<<21)))|0}function Oj(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=Vd(136)|0;f=e;c[e>>2]=Me(4)|0;c[e+4>>2]=Vd(16)|0;g=vj()|0;h=e+124|0;c[h>>2]=g;i=a+64|0;c[e+120>>2]=lh(g,c[(c[i>>2]|0)+24>>2]|0,c[(c[a+92>>2]|0)+24>>2]|0)|0;c[e+16>>2]=Vd(64)|0;c[e+20>>2]=Vd(16)|0;c[e+24>>2]=0;a=e+32|0;c[a>>2]=0;c[a+4>>2]=0;c[e+64>>2]=0;c[(c[h>>2]|0)+12>>2]=c[(c[i>>2]|0)+24>>2];c[e+8>>2]=Vd(32)|0;c[e+12>>2]=0;c[e+40>>2]=0;c[e+44>>2]=32;b[e+48>>1]=0;b[e+50>>1]=8;b[e+52>>1]=0;b[e+54>>1]=4;b[e+60>>1]=0;b[e+62>>1]=4;c[e+96>>2]=0;c[e+80>>2]=0;c[e+76>>2]=0;c[e+84>>2]=0;b[e+102>>1]=0;c[e+112>>2]=d;c[e+104>>2]=1;tk(f);return f|0}function Pj(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;b=c[a+96>>2]|0;while(1){if((b|0)==0){break}d=c[b+60>>2]|0;if((d|0)==0){e=b;f=4;break}else{b=d}}if((f|0)==4){while(1){f=0;b=c[e+56>>2]|0;Ln(e);if((b|0)==0){break}else{e=b;f=4}}}f=c[a+76>>2]|0;if((f|0)!=0){e=f;while(1){f=c[e+20>>2]|0;Ln(e);if((f|0)==0){break}else{e=f}}}e=c[a+12>>2]|0;if((e|0)!=0){f=e;while(1){e=c[f>>2]|0;if((e|0)==0){g=f;break}else{f=e}}while(1){f=c[g+4>>2]|0;Ln(g);if((f|0)==0){break}else{g=f}}}Ln(c[a+24>>2]|0);Ln(c[a+20>>2]|0);Ln(c[a+8>>2]|0);mh(c[a+120>>2]|0);Ln(c[a+4>>2]|0);g=a+64|0;f=c[g>>2]|0;if((f|0)==0){h=a|0;i=c[h>>2]|0;j=i;Se(j);k=a+16|0;l=c[k>>2]|0;m=l;Ln(m);n=a;Ln(n);return}Se(c[f+4>>2]|0);Se(c[c[g>>2]>>2]|0);Ln(c[g>>2]|0);h=a|0;i=c[h>>2]|0;j=i;Se(j);k=a+16|0;l=c[k>>2]|0;m=l;Ln(m);n=a;Ln(n);return}function Qj(d){d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;uk(d);e=d+124|0;yj(c[e>>2]|0,0);f=d+132|0;g=Cj(c[e>>2]|0,0,c[(c[f>>2]|0)+68>>2]|0,1)|0;e=_h(c[f>>2]|0,g,14720)|0;c[e+12>>2]=0;c[e+32>>2]=1;g=e|0;h=c[g+4>>2]|0;c[g>>2]=c[g>>2]|65536;c[g+4>>2]=h;h=(c[f>>2]|0)+40|0;c[h>>2]=(c[h>>2]|0)+1;h=Vd(64)|0;g=h;i=qj(0,c[e+20>>2]|0)|0;c[c[f>>2]>>2]=e;c[(c[f>>2]|0)+32>>2]=i;j=i|0;c[j>>2]=(c[j>>2]|0)+1;ci(c[f>>2]|0,e,i);c[h+60>>2]=0;c[h+56>>2]=0;b[h+22>>1]=12;c[h+8>>2]=e;c[h>>2]=c[d+76>>2];c[h+48>>2]=0;b[h+14>>1]=0;c[h+52>>2]=0;c[h+24>>2]=0;c[h+28>>2]=0;c[h+36>>2]=0;b[h+18>>1]=-1;a[h+20|0]=0;c[d+68>>2]=e;c[d+96>>2]=g;e=d+102|0;b[e>>1]=(b[e>>1]|0)+1;c[d+88>>2]=g;c[d+92>>2]=g;return}function Rj(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0;f=_h(c[a+132>>2]|0,d,e)|0;e=a+102|0;if((b[e>>1]|0)==1){d=a+88|0;c[f+12>>2]=c[(c[d>>2]|0)+36>>2];g=(c[d>>2]|0)+36|0;c[g>>2]=(c[g>>2]|0)+1;h=b[e>>1]|0;i=h&65535;j=f+32|0;c[j>>2]=i;return f|0}else{g=a+92|0;c[f+12>>2]=c[(c[g>>2]|0)+36>>2];a=(c[g>>2]|0)+36|0;c[a>>2]=(c[a>>2]|0)+1;h=b[e>>1]|0;i=h&65535;j=f+32|0;c[j>>2]=i;return f|0}return 0}function Sj(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;e=a+132|0;a=_h(c[e>>2]|0,b,d)|0;c[a+12>>2]=c[(c[e>>2]|0)+40>>2];d=(c[e>>2]|0)+40|0;c[d>>2]=(c[d>>2]|0)+1;c[a+32>>2]=1;d=a|0;e=c[d+4>>2]|0;c[d>>2]=c[d>>2]|65536;c[d+4>>2]=e;return a|0}function Tj(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0;g=a+132|0;a=Zh(c[g>>2]|0,e,f)|0;c[a+32>>2]=1;e=a|0;h=c[e+4>>2]|0;c[e>>2]=c[e>>2]|65536;c[e+4>>2]=h;c[a+12>>2]=c[(c[g>>2]|0)+40>>2];h=(c[g>>2]|0)+40|0;c[h>>2]=(c[h>>2]|0)+1;h=d|0;if((c[h>>2]&64|0)==0&(c[h+4>>2]&0|0)==0){h=d+32|0;c[a+44>>2]=c[h>>2];c[h>>2]=a;c[a+40>>2]=d;i=pj(b,c[d+8>>2]|0,f)|0;j=c[g>>2]|0;bi(j,a,i);return a|0}else{h=d+24|0;c[a+44>>2]=c[h>>2];c[h>>2]=a;i=pj(b,0,f)|0;j=c[g>>2]|0;bi(j,a,i);return a|0}return 0}function Uj(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Zh(c[a+132>>2]|0,d,e)|0;e=a+88|0;c[f+12>>2]=c[(c[e>>2]|0)+36>>2];a=(c[e>>2]|0)+36|0;c[a>>2]=(c[a>>2]|0)+1;c[f+32>>2]=1;a=b+24|0;c[f+44>>2]=c[a>>2];c[a>>2]=f;return f|0}function Vj(a,d){a=a|0;d=d|0;var e=0,f=0;vk(a,6);e=a+40|0;f=a+16|0;b[(c[f>>2]|0)+(c[e>>2]<<1)>>1]=25;b[(c[f>>2]|0)+((c[e>>2]|0)+1<<1)>>1]=c[c[a+108>>2]>>2];b[(c[f>>2]|0)+((c[e>>2]|0)+2<<1)>>1]=1;b[(c[f>>2]|0)+((c[e>>2]|0)+3<<1)>>1]=c[d+12>>2];b[(c[f>>2]|0)+((c[e>>2]|0)+4<<1)>>1]=0;b[(c[f>>2]|0)+((c[e>>2]|0)+5<<1)>>1]=0;c[e>>2]=(c[e>>2]|0)+6;return}function Wj(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0;f=e&65535;vk(a,f+2|0);g=a+40|0;h=a+16|0;b[(c[h>>2]|0)+(c[g>>2]<<1)>>1]=47;b[(c[h>>2]|0)+((c[g>>2]|0)+1<<1)>>1]=e;a=(c[g>>2]|0)+2|0;c[g>>2]=a;if(e<<16>>16==0){return}else{i=0;j=a}do{b[(c[h>>2]|0)+(j<<1)>>1]=b[d+(i<<1)>>1]|0;j=(c[g>>2]|0)+1|0;c[g>>2]=j;i=i+2|0;}while((i|0)<(f|0));if((e&65535)>>>0>1>>>0){k=1;l=j}else{return}do{b[(c[h>>2]|0)+(l<<1)>>1]=b[d+(k<<1)>>1]|0;l=(c[g>>2]|0)+1|0;c[g>>2]=l;k=k+2|0;}while((k|0)<(f|0));return}function Xj(a,d,e,f,g,h){a=a|0;d=d|0;e=e|0;f=f|0;g=g|0;h=h|0;var i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0;i=c[a+132>>2]|0;j=c[i+44>>2]|0;if((g|0)==0){k=Vh(i,1,0)|0;i=Rj(a,c[j+24>>2]|0,18896)|0;wk(a,40,h&65535,c[k+12>>2]&65535,c[i+12>>2]&65535);l=i}else{l=g}if((c[d+32>>2]|0)==1){m=Rj(a,c[j+24>>2]|0,16696)|0}else{m=d}j=(m|0)!=(d|0);vk(a,(j&1)<<3|16);g=a+40|0;i=a+16|0;b[(c[i>>2]|0)+(c[g>>2]<<1)>>1]=35;k=h&65535;b[(c[i>>2]|0)+((c[g>>2]|0)+1<<1)>>1]=k;h=m+12|0;b[(c[i>>2]|0)+((c[g>>2]|0)+2<<1)>>1]=c[h>>2];n=e+12|0;b[(c[i>>2]|0)+((c[g>>2]|0)+3<<1)>>1]=c[n>>2];e=f+12|0;b[(c[i>>2]|0)+((c[g>>2]|0)+4<<1)>>1]=c[e>>2];f=l+12|0;b[(c[i>>2]|0)+((c[g>>2]|0)+5<<1)>>1]=c[f>>2];b[(c[i>>2]|0)+((c[g>>2]|0)+6<<1)>>1]=0;l=c[g>>2]|0;if(j){b[(c[i>>2]|0)+(l+7<<1)>>1]=39;b[(c[i>>2]|0)+((c[g>>2]|0)+8<<1)>>1]=k;b[(c[i>>2]|0)+((c[g>>2]|0)+9<<1)>>1]=c[h>>2];b[(c[i>>2]|0)+((c[g>>2]|0)+10<<1)>>1]=c[d+12>>2];o=(c[g>>2]|0)+4|0;c[g>>2]=o;p=o}else{p=l}b[(c[a+96>>2]|0)+18>>1]=p+7;b[(c[i>>2]|0)+((c[g>>2]|0)+7<<1)>>1]=34;b[(c[i>>2]|0)+((c[g>>2]|0)+8<<1)>>1]=k;b[(c[i>>2]|0)+((c[g>>2]|0)+9<<1)>>1]=c[h>>2];b[(c[i>>2]|0)+((c[g>>2]|0)+10<<1)>>1]=c[n>>2];b[(c[i>>2]|0)+((c[g>>2]|0)+11<<1)>>1]=c[e>>2];b[(c[i>>2]|0)+((c[g>>2]|0)+12<<1)>>1]=c[f>>2];b[(c[i>>2]|0)+((c[g>>2]|0)+13<<1)>>1]=0;f=c[g>>2]|0;if(!j){q=f;r=q+14|0;c[g>>2]=r;s=(m|0)==(d|0);t=s?1:5;u=a|0;v=c[u>>2]|0;w=r-t|0;x=w&65535;Ne(v,x);return}b[(c[i>>2]|0)+(f+14<<1)>>1]=39;b[(c[i>>2]|0)+((c[g>>2]|0)+15<<1)>>1]=k;b[(c[i>>2]|0)+((c[g>>2]|0)+16<<1)>>1]=c[h>>2];b[(c[i>>2]|0)+((c[g>>2]|0)+17<<1)>>1]=c[d+12>>2];i=(c[g>>2]|0)+4|0;c[g>>2]=i;q=i;r=q+14|0;c[g>>2]=r;s=(m|0)==(d|0);t=s?1:5;u=a|0;v=c[u>>2]|0;w=r-t|0;x=w&65535;Ne(v,x);return}function Yj(a){a=a|0;var d=0,e=0,f=0;d=i;e=a+96|0;if((b[(c[e>>2]|0)+18>>1]|0)==-1){Ae(c[a+112>>2]|0,1,15216,(f=i,i=i+1|0,i=i+7&-8,c[f>>2]=0,f)|0);i=f}f=xk(a)|0;yk(a,f);zk(a,23,0);Ak(c[a>>2]|0,c[e>>2]|0,f,(c[a+40>>2]|0)+65535&65535);i=d;return}function Zj(a){a=a|0;var d=0,e=0,f=0;d=i;e=a+96|0;if((b[(c[e>>2]|0)+18>>1]|0)==-1){Ae(c[a+112>>2]|0,1,13824,(f=i,i=i+1|0,i=i+7&-8,c[f>>2]=0,f)|0);i=f}yk(a,xk(a)|0);zk(a,23,b[(c[e>>2]|0)+18>>1]|0);i=d;return}function _j(a,b){a=a|0;b=b|0;Bk(a,43,b&65535,0);Ne(c[a>>2]|0,(c[a+40>>2]|0)+65535&65535);return}function $j(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;if((d|0)==0){f=Ck(a,b)|0}else{f=d}Dk(a,45,e&65535,0,(d|0)!=0|0,c[f+12>>2]&65535);Ne(c[a>>2]|0,(c[a+40>>2]|0)+65533&65535);return}function ak(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,i=0,j=0;f=a|0;g=c[f>>2]|0;h=e[(c[a+96>>2]|0)+12>>1]|0;i=(c[g+4>>2]|0)-1|0;if((i|0)<(h|0)){return}j=d&65535;d=a+16|0;a=i;i=g;while(1){g=Oe(i)|0;if(g<<16>>16!=-1){b[(c[d>>2]|0)+((g&65535)<<1)>>1]=j}g=a-1|0;if((g|0)<(h|0)){break}a=g;i=c[f>>2]|0}return}function bk(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=a+92|0;e=c[(c[d>>2]|0)+36>>2]|0;do{f=Ck(a,b)|0;}while((c[(c[d>>2]|0)+36>>2]|0)==(e|0));return f|0}function ck(d,f){d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;g=d+96|0;h=c[(c[g>>2]|0)+56>>2]|0;if((h|0)==0){i=Vd(64)|0;j=i;c[(c[g>>2]|0)+56>>2]=j;c[i+60>>2]=c[g>>2];c[i+56>>2]=0;k=j}else{k=h}b[k+22>>1]=f;h=d+132|0;c[k+4>>2]=c[(c[(c[h>>2]|0)+20>>2]|0)+24>>2];j=c[(c[g>>2]|0)+48>>2]|0;i=k+48|0;c[i>>2]=j;l=k+52|0;c[l>>2]=c[(c[g>>2]|0)+52>>2];b[k+14>>1]=0;b[k+12>>1]=c[(c[d>>2]|0)+4>>2];c[k+40>>2]=-1;m=k+18|0;b[m>>1]=b[(c[g>>2]|0)+18>>1]|0;a[k+20|0]=0;if(f>>>0<12>>>0){c[k>>2]=c[c[g>>2]>>2];c[k+28>>2]=c[(c[g>>2]|0)+28>>2];a[k+21|0]=1;if((f|0)==6|(f|0)==5|(f|0)==4){b[m>>1]=c[d+40>>2];c[g>>2]=k;return}else if((f|0)==11){c[i>>2]=c[(c[(c[h>>2]|0)+20>>2]|0)+20>>2];b[m>>1]=-1;c[g>>2]=k;return}else{c[g>>2]=k;return}}n=c[(c[h>>2]|0)+20>>2]|0;h=c[n+24>>2]|0;if((f|0)==13){o=c[n+20>>2]|0;c[i>>2]=o;p=o}else{p=j}c[h+40>>2]=p;p=d+102|0;do{if((e[p>>1]|0)>>>0>1>>>0){if((b[(c[g>>2]|0)+22>>1]|0)==13){break}j=h|0;o=c[j+4>>2]|0;c[j>>2]=c[j>>2]|131072;c[j+4>>2]=o}}while(0);c[k+36>>2]=0;if((f|0)==14){c[l>>2]=0;q=14}else if((f|0)!=15){q=14}if((q|0)==14){b[p>>1]=(b[p>>1]|0)+1}c[d+92>>2]=k;c[k>>2]=c[d+80>>2];c[k+8>>2]=h;p=d+40|0;c[k+24>>2]=c[p>>2];c[k+28>>2]=c[p>>2];b[m>>1]=-1;c[d+68>>2]=h;c[g>>2]=k;return}function dk(d){d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;f=i;g=d+96|0;h=c[g>>2]|0;if((c[h+60>>2]|0)==0){Ae(c[d+112>>2]|0,1,12520,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j;k=c[g>>2]|0}else{k=h}h=b[k+22>>1]|0;do{if((h&-3)<<16>>16==4){zk(d,23,(e[k+18>>1]|0)-(c[k+28>>2]|0)&65535);l=9}else{if(h<<16>>16==10){Ek(d);b[d+60>>1]=b[(c[g>>2]|0)+16>>1]|0;l=10;break}if((h-7&65535)>>>0>=3>>>0){l=9;break}j=(c[d>>2]|0)+4|0;c[j>>2]=(c[j>>2]|0)-1;l=9}}while(0);if((l|0)==9){if((h<<16>>16|0)==10|(h<<16>>16|0)==9|(h<<16>>16|0)==2){l=10}}do{if((l|0)==10){if((a[k+21|0]|0)==0){break}j=c[k+40>>2]|0;if((j|0)!=(c[d+40>>2]|0)){break}c[(c[(c[g>>2]|0)+60>>2]|0)+40>>2]=j}}while(0);if((h&65535)>>>0<12>>>0){h=c[k+4>>2]|0;ak(d,(c[d+40>>2]|0)-(c[k+28>>2]|0)|0);ai(c[d+132>>2]|0,h);m=c[g>>2]|0;n=m+60|0;o=c[n>>2]|0;c[g>>2]=o;i=f;return}else{Fk(d,k);m=c[g>>2]|0;n=m+60|0;o=c[n>>2]|0;c[g>>2]=o;i=f;return}}function ek(d,e){d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;f=i;g=d+96|0;h=c[g>>2]|0;j=b[h+22>>1]|0;k=d+40|0;if((c[h+40>>2]|0)!=(c[k>>2]|0)){a[h+21|0]=0}do{if((e-1|0)>>>0<2>>>0){h=(e|0)==1?11376:10168;if((j&65535)>>>0<=1>>>0){break}l=d+112|0;Ae(c[l>>2]|0,1,9144,(m=i,i=i+8|0,c[m>>2]=h,m)|0);i=m;if(j<<16>>16!=2){break}Ae(c[l>>2]|0,1,8104,(m=i,i=i+8|0,c[m>>2]=h,m)|0);i=m}else{if((e&-2|0)!=8){break}if(j<<16>>16==9){Ae(c[d+112>>2]|0,1,20832,(m=i,i=i+1|0,i=i+7&-8,c[m>>2]=0,m)|0);i=m;break}if((j-7&65535)>>>0>1>>>0){Ae(c[d+112>>2]|0,1,20208,(m=i,i=i+1|0,i=i+7&-8,c[m>>2]=0,m)|0);i=m}if(j<<16>>16!=7){break}Gk(d,44)}}while(0);j=c[(c[g>>2]|0)+4>>2]|0;m=c[d+132>>2]|0;if((j|0)!=(c[(c[m+20>>2]|0)+24>>2]|0)){ai(m,j)}zk(d,23,0);j=(c[k>>2]|0)+65535|0;m=d|0;h=Oe(c[m>>2]|0)|0;if(h<<16>>16==-1){n=c[m>>2]|0;o=j&65535;Ne(n,o);p=c[g>>2]|0;q=p+22|0;r=q;s=e&65535;b[r>>1]=s;i=f;return}b[(c[d+16>>2]|0)+((h&65535)<<1)>>1]=(c[k>>2]|0)-(c[(c[g>>2]|0)+28>>2]|0);n=c[m>>2]|0;o=j&65535;Ne(n,o);p=c[g>>2]|0;q=p+22|0;r=q;s=e&65535;b[r>>1]=s;i=f;return}function fk(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=c[d+8>>2]|0;d=e+3|0;vk(a,d);f=a+40|0;g=a+16|0;b[(c[g>>2]|0)+(c[f>>2]<<1)>>1]=50;b[(c[g>>2]|0)+((c[f>>2]|0)+1<<1)>>1]=c[c[a+108>>2]>>2];b[(c[g>>2]|0)+((c[f>>2]|0)+2<<1)>>1]=c[(c[(c[a+96>>2]|0)+44>>2]|0)+12>>2];b[(c[g>>2]|0)+((c[f>>2]|0)+3<<1)>>1]=e+65535;h=e-2|0;if((h|0)<=-1){i=c[f>>2]|0;j=i+d|0;c[f>>2]=j;return}e=h;h=(c[(c[a+132>>2]|0)+20>>2]|0)+24|0;while(1){a=c[h>>2]|0;b[(c[g>>2]|0)+(e+4+(c[f>>2]|0)<<1)>>1]=c[a+12>>2];if((e|0)>0){e=e-1|0;h=a+44|0}else{break}}i=c[f>>2]|0;j=i+d|0;c[f>>2]=j;return}function gk(d,e){d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;f=d+96|0;g=c[f>>2]|0;h=b[g+16>>1]|0;i=h&65535;j=b[d+60>>1]|0;a:do{if((h&65535)>>>0<(j&65535)>>>0){k=c[d+4>>2]|0;l=i;while(1){m=l+1|0;if((c[k+(l<<2)>>2]|0)==1){break}if((m|0)<(j&65535|0)){l=m}else{n=5;break a}}l=d+40|0;if((c[g+40>>2]|0)==(c[l>>2]|0)){o=1;p=l;break}a[g+21|0]=0;o=1;p=l}else{n=5}}while(0);if((n|0)==5){o=0;p=d+40|0}n=(c[d+4>>2]|0)+(i+e<<2)|0;if((c[n>>2]|0)!=0){q=0;return q|0}c[n>>2]=1;if(o){zk(d,23,0);Ne(c[d>>2]|0,(c[p>>2]|0)+65535&65535)}o=c[f>>2]|0;b[(c[d+16>>2]|0)+((c[o+32>>2]|0)+e<<1)>>1]=(c[p>>2]|0)-(c[o+28>>2]|0);o=c[(c[f>>2]|0)+4>>2]|0;f=c[d+132>>2]|0;if((o|0)==(c[(c[f+20>>2]|0)+24>>2]|0)){q=1;return q|0}ai(f,o);q=1;return q|0}function hk(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0;f=i;g=c[d+12>>2]|0;h=c[a+96>>2]|0;Hk(a,g,0,19584);j=g|0;g=c[c[(c[j>>2]|0)+8>>2]>>2]|0;k=g|0;if((c[k>>2]&1024|0)==0&(c[k+4>>2]&0|0)==0){l=3}else{if((b[g+52>>1]|0)==6){l=3}else{m=g}}if((l|0)==3){Ae(c[a+112>>2]|0,1,19048,(l=i,i=i+1|0,i=i+7&-8,c[l>>2]=0,l)|0);i=l;m=c[c[(c[j>>2]|0)+8>>2]>>2]|0}l=b[m+64>>1]|0;m=l&65535;g=a+60|0;k=b[g>>1]|0;if(((k&65535)+m|0)>(e[a+62>>1]|0)){Ik(a);n=b[g>>1]|0}else{n=k}b[h+16>>1]=n;n=l<<16>>16==0;k=b[g>>1]|0;if(n){o=k}else{p=a+4|0;q=0;r=k;while(1){c[(c[p>>2]|0)+((r&65535)+q<<2)>>2]=0;k=q+1|0;s=b[g>>1]|0;if((k|0)<(m|0)){q=k;r=s}else{o=s;break}}}b[g>>1]=o+l;o=a+40|0;c[h+32>>2]=(c[o>>2]|0)+4;c[h+44>>2]=c[j>>2];vk(a,m+4|0);h=a+16|0;b[(c[h>>2]|0)+(c[o>>2]<<1)>>1]=49;b[(c[h>>2]|0)+((c[o>>2]|0)+1<<1)>>1]=c[c[a+108>>2]>>2];b[(c[h>>2]|0)+((c[o>>2]|0)+2<<1)>>1]=c[(c[j>>2]|0)+12>>2];b[(c[h>>2]|0)+((c[o>>2]|0)+3<<1)>>1]=l;if(n){t=4;u=c[o>>2]|0;v=t+u|0;c[o>>2]=v;qd(d);i=f;return}n=(l&65535)>>>0>1>>>0;l=0;do{b[(c[h>>2]|0)+(l+4+(c[o>>2]|0)<<1)>>1]=0;l=l+1|0;}while((l|0)<(m|0));t=n?m+4|0:5;u=c[o>>2]|0;v=t+u|0;c[o>>2]=v;qd(d);i=f;return}function ik(d,e){d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;f=c[e+20>>2]|0;g=b[d+52>>1]|0;if(g<<16>>16==0){h=0;i=0}else{j=c[d+20>>2]|0;k=g&65535;l=0;while(1){m=l+1|0;if((c[j+(l<<2)>>2]|0)==(f|0)){n=l;break}if((m|0)<(k|0)){l=m}else{n=m;break}}h=n;i=g&65535}if((h|0)==(i|0)){Jk(d,f)}a[(c[d+92>>2]|0)+20|0]=1;i=Ck(d,c[f+8>>2]|0)|0;wk(d,51,c[e+8>>2]&65535,h&65535,c[i+12>>2]&65535);c[e>>2]=i;return}function jk(a,b){a=a|0;b=b|0;c[b>>2]=c[(c[a+96>>2]|0)+52>>2];return}function kk(a,b){a=a|0;b=b|0;var d=0;Kk(a,c[b+12>>2]|0,0);d=a+104|0;c[d>>2]=(c[d>>2]|0)+1;qd(b);return}function lk(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0;f=i;g=c[d+12>>2]|0;Kk(a,g,0);h=a+104|0;c[h>>2]=(c[h>>2]|0)+1;h=g|0;j=c[h>>2]|0;k=c[j+8>>2]|0;if((b[(c[k>>2]|0)+52>>1]|0)==0){l=j}else{Ae(c[a+112>>2]|0,1,18464,(j=i,i=i+8|0,c[j>>2]=k,j)|0);i=j;l=c[h>>2]|0}wk(a,0,c[g+8>>2]&65535,c[l+12>>2]&65535,c[e+12>>2]&65535);qd(d);i=f;return}function mk(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=c[d+12>>2]|0;f=a+96|0;g=c[f>>2]|0;h=b[g+22>>1]|0;i=e|0;do{if((c[e+4>>2]&65535|0)==16){if((Lk(c[i>>2]|0)|0)==0){break}if(h<<16>>16==5){zk(a,23,b[g+18>>1]|0);qd(d);return}else{Ne(c[a>>2]|0,-1);qd(d);return}}}while(0);Hk(a,e,0,18256);Mk(a,c[(c[i>>2]|0)+8>>2]|0);e=c[(c[i>>2]|0)+12>>2]|0;if(h<<16>>16==5){wk(a,24,1,e&65535,b[(c[f>>2]|0)+18>>1]|0);qd(d);return}else{Nk(a,e,0);qd(d);return}}function nk(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;if((d|0)==0){e=1;f=0}else{g=c[c[d+4>>2]>>2]|0;e=(g|0)!=0;f=g}g=b+12|0;Kk(a,c[g>>2]|0,f);f=c[g>>2]|0;g=f|0;b=c[g>>2]|0;if(e&(b|0)!=0){Bk(a,26,c[f+8>>2]&65535,c[b+12>>2]&65535);return}if(e){return}c[g>>2]=0;return}function ok(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0;e=i;if((b[a+102>>1]|0)==1){Ae(c[a+112>>2]|0,1,18080,(f=i,i=i+1|0,i=i+7&-8,c[f>>2]=0,f)|0);i=f}if((d|0)==0){yk(a,c[a+92>>2]|0);zk(a,27,c[c[a+108>>2]>>2]&65535);i=e;return}g=c[a+72>>2]|0;Hk(a,d,g,17880);h=d|0;do{if((c[(c[h>>2]|0)+8>>2]|0)!=(g|0)){if((Ok(a,g,d)|0)!=0){break}j=a+112|0;c[(c[j>>2]|0)+16>>2]=c[d+8>>2];k=c[(c[h>>2]|0)+8>>2]|0;Ae(c[j>>2]|0,1,17664,(f=i,i=i+16|0,c[f>>2]=g,c[f+8>>2]=k,f)|0);i=f}}while(0);yk(a,c[a+92>>2]|0);Bk(a,26,c[d+8>>2]&65535,c[(c[h>>2]|0)+12>>2]&65535);c[(c[a+96>>2]|0)+40>>2]=c[a+40>>2];i=e;return}function pk(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;c[a+72>>2]=f;f=a+96|0;b[(c[f>>2]|0)+14>>1]=e;if((d|0)==0){return}e=Ck(a,d)|0;c[(c[f>>2]|0)+52>>2]=e;Bk(a,48,c[c[a+108>>2]>>2]&65535,c[e+12>>2]&65535);return}function qk(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;Hk(a,b,0,17456);e=b|0;f=c[c[(c[e>>2]|0)+8>>2]>>2]|0;if((Dh(gi(c[a+132>>2]|0,0,17296)|0,f)|0)==0){Ae(c[a+112>>2]|0,1,17104,(g=i,i=i+8|0,c[g>>2]=c[f+8>>2],g)|0);i=g}Bk(a,46,c[b+8>>2]&65535,c[(c[e>>2]|0)+12>>2]&65535);c[(c[a+96>>2]|0)+40>>2]=c[a+40>>2];i=d;return}function rk(a){a=a|0;c[a+40>>2]=0;return}function sk(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=a+132|0;f=c[(c[e>>2]|0)+32>>2]|0;g=c[(c[a+88>>2]|0)+36>>2]|0;h=f+52|0;i=Wd(c[h>>2]|0,g<<4)|0;if((d|0)!=0){j=d;do{Pk(i,c[j+24>>2]|0,0);j=c[j+40>>2]|0;}while((j|0)!=0)}Pk(i,c[(c[(c[e>>2]|0)+20>>2]|0)+24>>2]|0,0);Qk(i,c[c[a+96>>2]>>2]|0);vk(a,16);Gk(a,57);c[f+28>>2]=c[a+16>>2];c[f+32>>2]=c[a+40>>2];c[h>>2]=i;b[f+48>>1]=g;return}function tk(a){a=a|0;var b=0,d=0,e=0,f=0;b=Vd(32)|0;d=b;e=a+12|0;a=c[e>>2]|0;if((a|0)==0){f=0}else{c[a+4>>2]=d;f=c[e>>2]|0}c[b>>2]=f;c[b+4>>2]=0;c[b+8>>2]=0;c[b+16>>2]=0;c[e>>2]=d;return}function uk(a){a=a|0;var b=0,d=0,e=0,f=0,g=0;b=Vd(24)|0;d=b;c[b+8>>2]=0;c[b+20>>2]=0;c[b+16>>2]=0;e=b;c[e>>2]=0;c[e+4>>2]=0;e=a+76|0;if((c[e>>2]|0)==0){c[e>>2]=d;f=a+84|0;c[f>>2]=d;g=a+80|0;c[g>>2]=d;return}else{e=a+84|0;c[(c[e>>2]|0)+20>>2]=d;f=e;c[f>>2]=d;g=a+80|0;c[g>>2]=d;return}}function vk(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=(c[a+40>>2]|0)+b|0;b=a+44|0;e=c[b>>2]|0;if(d>>>0>e>>>0){f=e}else{return}while(1){g=f<<1;if(d>>>0>g>>>0){f=g}else{break}}c[b>>2]=g;g=a+16|0;c[g>>2]=Wd(c[g>>2]|0,f<<2)|0;return}function wk(a,d,e,f,g){a=a|0;d=d|0;e=e|0;f=f|0;g=g|0;var h=0,i=0,j=0;h=a+40|0;i=c[h>>2]|0;if((i+4|0)>>>0>(c[a+44>>2]|0)>>>0){Rk(a);j=c[h>>2]|0}else{j=i}i=a+16|0;b[(c[i>>2]|0)+(j<<1)>>1]=d;b[(c[i>>2]|0)+((c[h>>2]|0)+1<<1)>>1]=e;b[(c[i>>2]|0)+((c[h>>2]|0)+2<<1)>>1]=f;b[(c[i>>2]|0)+((c[h>>2]|0)+3<<1)>>1]=g;c[h>>2]=(c[h>>2]|0)+4;return}function xk(a){a=a|0;var d=0,e=0,f=0,g=0;d=c[a+96>>2]|0;if((d|0)==0){e=0;return e|0}else{f=d}while(1){d=b[f+22>>1]|0;if((d-4&65535)>>>0<3>>>0){e=f;g=5;break}if((d&65535)>>>0>11>>>0){e=0;g=5;break}d=c[f+60>>2]|0;if((d|0)==0){e=0;g=5;break}else{f=d}}if((g|0)==5){return e|0}return 0}function yk(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=c[a+96>>2]|0;if((e|0)==(d|0)){return}else{f=0;g=e}do{f=((b[g+22>>1]|0)==7)+f|0;g=c[g+60>>2]|0;}while((g|0)!=(d|0));if((f|0)==0){return}vk(a,f);d=a+40|0;g=c[d>>2]|0;if((f|0)<0){h=g}else{e=a+16|0;a=0;i=g;while(1){b[(c[e>>2]|0)+(i+a<<1)>>1]=44;g=c[d>>2]|0;if((a|0)<(f|0)){a=a+1|0;i=g}else{h=g;break}}}c[d>>2]=h+f;return}function zk(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0;f=a+40|0;g=c[f>>2]|0;if((g+2|0)>>>0>(c[a+44>>2]|0)>>>0){Rk(a);h=c[f>>2]|0}else{h=g}g=a+16|0;b[(c[g>>2]|0)+(h<<1)>>1]=d;b[(c[g>>2]|0)+((c[f>>2]|0)+1<<1)>>1]=e;c[f>>2]=(c[f>>2]|0)+2;return}function Ak(a,d,f,g){a=a|0;d=d|0;f=f|0;g=g|0;var h=0;if((d|0)==(f|0)){Ne(a,g);return}d=f+56|0;Re(a,e[(c[d>>2]|0)+12>>1]|0,g);g=c[d>>2]|0;if((g|0)==0){return}else{h=g}do{g=h+12|0;b[g>>1]=(b[g>>1]|0)+1;h=c[h+56>>2]|0;}while((h|0)!=0);return}function Bk(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0;g=a+40|0;h=c[g>>2]|0;if((h+3|0)>>>0>(c[a+44>>2]|0)>>>0){Rk(a);i=c[g>>2]|0}else{i=h}h=a+16|0;b[(c[h>>2]|0)+(i<<1)>>1]=d;b[(c[h>>2]|0)+((c[g>>2]|0)+1<<1)>>1]=e;b[(c[h>>2]|0)+((c[g>>2]|0)+2<<1)>>1]=f;c[g>>2]=(c[g>>2]|0)+3;return}function Ck(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0;d=c[a+104>>2]|0;e=c[c[a+96>>2]>>2]|0;a:do{if((e|0)==0){f=0}else{g=e;while(1){h=g+8|0;i=c[h>>2]|0;if((i|0)==0){break}if((i|0)==(b|0)){j=g+16|0;if((c[j>>2]|0)!=(d|0)){k=7;break}}i=c[g+20>>2]|0;if((i|0)==0){f=0;break a}else{g=i}}if((k|0)==7){c[j>>2]=d;f=g;break}c[h>>2]=b;i=a+92|0;c[g+12>>2]=c[(c[i>>2]|0)+36>>2];l=(c[i>>2]|0)+36|0;c[l>>2]=(c[l>>2]|0)+1;l=c[g+20>>2]|0;if((l|0)==0){f=g;break}c[a+80>>2]=l;f=g}}while(0);c[f+16>>2]=d;if((c[f+20>>2]|0)==0){uk(a)}a=f|0;d=c[a+4>>2]|0;c[a>>2]=c[a>>2]&-513;c[a+4>>2]=d;return f|0}function Dk(a,d,e,f,g,h){a=a|0;d=d|0;e=e|0;f=f|0;g=g|0;h=h|0;var i=0,j=0,k=0;i=a+40|0;j=c[i>>2]|0;if((j+5|0)>>>0>(c[a+44>>2]|0)>>>0){Rk(a);k=c[i>>2]|0}else{k=j}j=a+16|0;b[(c[j>>2]|0)+(k<<1)>>1]=d;b[(c[j>>2]|0)+((c[i>>2]|0)+1<<1)>>1]=e;b[(c[j>>2]|0)+((c[i>>2]|0)+2<<1)>>1]=f;b[(c[j>>2]|0)+((c[i>>2]|0)+3<<1)>>1]=g;b[(c[j>>2]|0)+((c[i>>2]|0)+4<<1)>>1]=h;c[i>>2]=(c[i>>2]|0)+5;return}function Ek(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;d=i;e=c[a+96>>2]|0;f=a+112|0;g=c[(c[f>>2]|0)+4>>2]|0;h=b[e+16>>1]|0;j=a+60|0;k=b[j>>1]|0;if((h&65535)>>>0>=(k&65535)>>>0){i=d;return}l=a+4|0;a=(c[c[(c[e+44>>2]|0)+8>>2]>>2]|0)+48|0;e=h&65535;h=0;m=k;while(1){if((c[(c[l>>2]|0)+(e<<2)>>2]|0)==0){if((h|0)==0){jn(g,8408);n=1}else{n=h}tn(g,8360,(k=i,i=i+8|0,c[k>>2]=c[(c[(c[a>>2]|0)+(e<<2)>>2]|0)+8>>2],k)|0);i=k;o=n;p=b[j>>1]|0}else{o=h;p=m}k=e+1|0;if((k|0)<(p&65535|0)){e=k;h=o;m=p}else{break}}if((o|0)==0){i=d;return}Be(c[f>>2]|0,1);i=d;return}function Fk(d,f){d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;g=i;h=f+22|0;j=b[h>>1]|0;if((j<<16>>16|0)==13){Bk(d,26,c[c[d+108>>2]>>2]&65535,c[(c[f+52>>2]|0)+12>>2]&65535)}else if((j<<16>>16|0)==14){j=c[c[(c[(c[d+68>>2]|0)+8>>2]|0)+4>>2]>>2]|0;c[d+72>>2]=j;k=j;l=5}else{k=c[d+72>>2]|0;l=5}do{if((l|0)==5){if((k|0)==0){zk(d,27,c[c[d+108>>2]>>2]&65535);break}if((b[h>>1]|0)!=12){break}if((c[f+40>>2]|0)==(c[d+40>>2]|0)){break}Ae(c[d+112>>2]|0,1,8288,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}}while(0);Sk(d,f);k=f+60|0;l=k;do{m=c[l>>2]|0;l=m+60|0}while((e[m+22>>1]|0)>>>0<12>>>0);j=c[m+8>>2]|0;n=c[d+96>>2]|0;o=b[n+22>>1]|0;if((o<<16>>16|0)==13){p=c[n+48>>2]|0;n=f+8|0;q=d+132|0;c[(c[(c[q>>2]|0)+20>>2]|0)+24>>2]=c[n>>2];ii(c[q>>2]|0,p,c[n>>2]|0)}else if((o<<16>>16|0)!=15){c[(c[(c[d+132>>2]|0)+20>>2]|0)+24>>2]=c[f+8>>2]}do{if((b[(c[k>>2]|0)+14>>1]|0)!=(b[f+14>>1]|0)){if((b[h>>1]|0)==14){break}si(c[d+132>>2]|0,0,e[m+14>>1]|0)}}while(0);c[d+68>>2]=j;c[d+72>>2]=c[c[(c[j+8>>2]|0)+4>>2]>>2];c[d+40>>2]=c[f+24>>2];c[d+92>>2]=m;if((b[h>>1]|0)==15){i=g;return}h=d+102|0;b[h>>1]=(b[h>>1]|0)-1;if((a[f+20|0]|0)!=1){i=g;return}if((c[l>>2]|0)==0){i=g;return}a[m+20|0]=1;i=g;return}function Gk(a,d){a=a|0;d=d|0;var e=0,f=0,g=0;e=a+40|0;f=c[e>>2]|0;if((f+1|0)>>>0>(c[a+44>>2]|0)>>>0){Rk(a);g=c[e>>2]|0}else{g=f}b[(c[a+16>>2]|0)+(g<<1)>>1]=d;c[e>>2]=(c[e>>2]|0)+1;return}function Hk(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=i;Kk(a,b,d);d=a+104|0;c[d>>2]=(c[d>>2]|0)+1;if((c[b>>2]|0)!=0){i=f;return}Ae(c[a+112>>2]|0,1,e,(e=i,i=i+1|0,i=i+7&-8,c[e>>2]=0,e)|0);i=e;i=f;return}function Ik(a){a=a|0;var d=0,e=0;d=a+62|0;e=b[d>>1]<<1;b[d>>1]=e;d=a+4|0;c[d>>2]=Wd(c[d>>2]|0,(e&65535)<<2)|0;return}function Jk(a,d){a=a|0;d=d|0;var e=0,f=0,g=0;e=i;f=c[a+92>>2]|0;do{if((b[f+22>>1]|0)==12){if((b[(c[f+60>>2]|0)+22>>1]|0)!=12){break}if((b[(c[d+8>>2]|0)+14>>1]&16)==0){break}Ae(c[a+112>>2]|0,1,8536,(g=i,i=i+1|0,i=i+7&-8,c[g>>2]=0,g)|0);i=g}}while(0);Tk(a,d);i=e;return}function Kk(a,b,d){a=a|0;b=b|0;d=d|0;Uk(a,b,d);if((c[b>>2]|0)!=0){return}if((c[b+16>>2]|0)==0){return}Vk(a,b,d);return}function Lk(a){a=a|0;var d=0,e=0,f=0,g=0;d=a|0;if((c[d>>2]&1|0)==0&(c[d+4>>2]&0|0)==0){e=1;return e|0}d=c[c[a+8>>2]>>2]|0;f=b[d+52>>1]|0;do{if((f<<16>>16|0)==4){g=a+24|0;if((c[g>>2]|0)==0&(c[g+4>>2]|0)==0){e=0}else{break}return e|0}else if((f<<16>>16|0)==2){if((c[(c[a+24>>2]|0)+4>>2]|0)==0){e=0}else{break}return e|0}else if((f<<16>>16|0)==1){if(+h[a+24>>3]==0.0){e=0}else{break}return e|0}else if((f<<16>>16|0)==0){g=a+24|0;if((c[g>>2]|0)==0&(c[g+4>>2]|0)==0){e=0}else{break}return e|0}}while(0);e=d|0;return((c[e>>2]|0)>>>12|c[e+4>>2]<<20)&1^1|0}function Mk(a,d){a=a|0;d=d|0;var e=0;e=i;switch(b[(c[d>>2]|0)+52>>1]|0){case 7:case 4:case 2:case 1:case 0:{i=e;return};default:{}}Ae(c[a+112>>2]|0,1,16960,(a=i,i=i+8|0,c[a>>2]=d,a)|0);i=a;i=e;return}function Nk(a,b,d){a=a|0;b=b|0;d=d|0;wk(a,24,d&65535,b&65535,0);Ne(c[a>>2]|0,(c[a+40>>2]|0)+65535&65535);return}function Ok(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0;f=b[(c[d>>2]|0)+52>>1]|0;if((f<<16>>16|0)==6){g=1;return g|0}else if((f<<16>>16|0)!=12){h=2}do{if((h|0)==2){if((sh(c[a+120>>2]|0,d,c[(c[e>>2]|0)+8>>2]|0)|0)==0){break}else{g=1}return g|0}}while(0);g=0;return g|0}function Pk(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;if((b|0)==(d|0)){return}else{e=b}do{b=e|0;if((c[b>>2]&65536|0)==0&(c[b+4>>2]&0|0)==0){b=e+12|0;c[a+(c[b>>2]<<4)>>2]=c[e+8>>2];c[a+(c[b>>2]<<4)+4>>2]=c[e+20>>2];c[a+(c[b>>2]<<4)+8>>2]=c[e+16>>2]}e=c[e+44>>2]|0;}while((e|0)!=(d|0));return}function Qk(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;if((b|0)==0){return}else{d=b}while(1){b=c[d+8>>2]|0;if((b|0)==0){e=4;break}f=d+12|0;c[a+(c[f>>2]<<4)>>2]=b;c[a+(c[f>>2]<<4)+4>>2]=0;c[a+(c[f>>2]<<4)+8>>2]=-1;f=c[d+20>>2]|0;if((f|0)==0){e=4;break}else{d=f}}if((e|0)==4){return}}function Rk(a){a=a|0;var b=0,d=0;b=a+44|0;d=c[b>>2]|0;c[b>>2]=d<<1;b=a+16|0;c[b>>2]=Wd(c[b>>2]|0,d<<2)|0;return}function Sk(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0;f=Wk(a,d)|0;g=c[(c[a+92>>2]|0)+36>>2]|0;h=d|0;i=Vd(g<<4)|0;j=c[d+8>>2]|0;d=a+102|0;if((b[d>>1]|0)==1){k=c[j+44>>2]|0}else{Pk(i,c[(c[(c[a+132>>2]|0)+20>>2]|0)+24>>2]|0,j);k=j}Qk(i,c[h>>2]|0);do{if((e[d>>1]|0)>>>0>1>>>0){j=a+132|0;l=c[(c[(c[j>>2]|0)+20>>2]|0)+24>>2]|0;if((l|0)==(k|0)){break}else{m=l}while(1){l=m+44|0;n=c[l>>2]|0;o=m|0;if((c[o>>2]&65536|0)==0&(c[o+4>>2]&0|0)==0){Ln(m)}else{c[l>>2]=c[(c[j>>2]|0)+24>>2];c[(c[j>>2]|0)+24>>2]=m}if((n|0)==(k|0)){break}else{m=n}}}}while(0);m=c[h>>2]|0;if((m|0)==0){p=0;q=a+80|0;c[q>>2]=p;r=f+52|0;c[r>>2]=i;s=g&65535;t=f+48|0;b[t>>1]=s;return}else{u=m}do{c[u+8>>2]=0;u=c[u+20>>2]|0;}while((u|0)!=0);p=c[h>>2]|0;q=a+80|0;c[q>>2]=p;r=f+52|0;c[r>>2]=i;s=g&65535;t=f+48|0;b[t>>1]=s;return}function Tk(d,e){d=d|0;e=e|0;var f=0,g=0,h=0;f=d+52|0;g=b[f>>1]|0;if(g<<16>>16==(b[d+54>>1]|0)){Xk(d);h=b[f>>1]|0}else{h=g}c[(c[d+20>>2]|0)+((h&65535)<<2)>>2]=e;b[f>>1]=(b[f>>1]|0)+1;f=e|0;e=c[f+4>>2]|0;c[f>>2]=c[f>>2]|1024;c[f+4>>2]=e;a[(c[d+92>>2]|0)+20|0]=1;return}function Uk(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0;f=c[d+4>>2]|0;a:do{switch(f&65535|0){case 22:{if(f>>>0<=1376255>>>0){g=f>>>16;if((g|0)==20){fl(a,d,e);break a}else if((g|0)==19|(g|0)==18){el(a,d);break a}else{g=d+20|0;h=c[g>>2]|0;if((c[h+4>>2]&65535|0)!=5){Kk(a,h,0)}h=c[d+24>>2]|0;if((c[h+4>>2]&65535|0)!=5){Kk(a,h,c[(c[c[g>>2]>>2]|0)+8>>2]|0)}gl(a,d);break a}}g=c[(c[d+20>>2]|0)+4>>2]|0;do{if((g&65533|0)==5){_k(a,d)}else{h=g&65535;if((h|0)==1){$k(a,d);break}else if((h|0)==13){bl(a,d);break}else if((h|0)==21){cl(a,d);break}else if((h|0)==8){al(a,d);break}else{_k(a,d);break}}}while(0);dl(a,d);break};case 0:{Zk(a,d,e);break};case 7:case 16:case 6:case 19:case 18:case 17:{Yk(a,d);break};case 9:{g=c[d+20>>2]|0;if((c[g+4>>2]&65535|0)!=5){Kk(a,g,e)}hl(a,d);break};case 2:{il(a,d,e);break};case 3:{jl(a,d,e);break};case 12:{kl(a,d,e);break};case 1:{ll(a,d);break};case 11:{ml(a,d);break};case 8:{nl(a,d);break};case 13:{ol(a,d);break};case 14:{pl(a,d,e);break};case 4:{g=c[d+24>>2]|0;Uk(a,g,e);h=c[g>>2]|0;if((h|0)==0){c[d+16>>2]=c[g+16>>2];break a}else{c[d>>2]=h;b[d+12>>1]=b[g+12>>1]|0;break a}break};case 15:{ql(a,d,e);break};case 20:{jk(a,d);break};case 21:{ik(a,d);break};default:{}}}while(0);if((e|0)==0){return}if((b[(c[e>>2]|0)+52>>1]|0)!=6){return}f=c[d>>2]|0;do{if((f|0)!=0){if((c[f+8>>2]|0)!=(e|0)){break}return}}while(0);rl(a,d);return}function Vk(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0;f=c[d+28>>2]|0;g=b[f+14>>1]|0;if((g&64)==0){h=f;i=g}else{g=rh(c[a+120>>2]|0,f,e)|0;e=(g|0)==0?f:g;h=e;i=b[e+14>>1]|0}if((i&64)==0){j=h}else{j=Ej(c[a+124>>2]|0,h)|0}h=Ck(a,j)|0;b[(c[a+16>>2]|0)+(c[d+16>>2]<<1)>>1]=c[h+12>>2];c[d>>2]=h;return}function Wk(d,e){d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;f=i;i=i+16|0;g=f|0;h=f+8|0;j=c[e+48>>2]|0;if((j|0)==0){k=0}else{k=c[j+8>>2]|0}j=c[e+8>>2]|0;l=qj(k,c[j+20>>2]|0)|0;k=j+8|0;if((b[(c[k>>2]|0)+14>>1]&16)!=0){b[l+46>>1]=1}if((b[e+22>>1]|0)==14){sl(d,c[k>>2]|0,l)}ci(c[d+132>>2]|0,j,l);if((a[e+20|0]|0)==0){e=c[(c[d+96>>2]|0)+24>>2]|0;c[g>>2]=e;j=(c[d+40>>2]|0)-e|0;c[h>>2]=j;m=j;n=e}else{tl(d,l,g,h);m=c[h>>2]|0;n=c[g>>2]|0}g=m<<1;h=Vd(g+2|0)|0;Yn(h|0,(c[d+16>>2]|0)+(n<<1)|0,g)|0;c[l+28>>2]=h;c[l+32>>2]=m-1;i=f;return l|0}function Xk(a){a=a|0;var d=0,e=0;d=a+54|0;e=b[d>>1]<<1;b[d>>1]=e;d=a+20|0;c[d>>2]=Wd(c[d>>2]|0,(e&65535)<<2)|0;return}function Yk(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=c[b+4>>2]&65535;if((d|0)==7){e=38}else if((d|0)==19){ul(a,c[b+20>>2]|0);f=3}else{f=3}if((f|0)==3){e=40}f=b+20|0;d=Ck(a,c[(c[f>>2]|0)+8>>2]|0)|0;if((e|0)!=38){g=d|0;h=c[g+4>>2]|0;c[g>>2]=c[g>>2]|512;c[g+4>>2]=h}h=c[f>>2]|0;f=h|0;if((c[f>>2]&131072|0)==0&(c[f+4>>2]&0|0)==0){wk(a,e&65535,c[b+8>>2]&65535,c[h+12>>2]&65535,c[d+12>>2]&65535);i=d;j=b|0;c[j>>2]=i;return}else{vl(a,c[h+12>>2]|0,c[d+12>>2]|0);i=d;j=b|0;c[j>>2]=i;return}}function Zk(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;if((c[(c[b+24>>2]|0)+4>>2]&65535|0)==14){pl(a,b,d);return}else{e=wl(a,b)|0;xl(a,e,d);yl(a,e);zl(a,e);return}}function _k(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;f=i;g=d+20|0;h=c[g>>2]|0;j=c[h+4>>2]&65535;if((j|0)==7|(j|0)==5){k=h}else{h=a+112|0;c[(c[h>>2]|0)+16>>2]=c[d+8>>2];j=c[h>>2]|0;h=Al((c[d+4>>2]|0)>>>16)|0;Ae(j,1,8904,(j=i,i=i+8|0,c[j>>2]=h,j)|0);i=j;k=c[g>>2]|0}j=d+24|0;Kk(a,c[j>>2]|0,c[(c[k>>2]|0)+8>>2]|0);k=c[c[g>>2]>>2]|0;if((c[k+8>>2]|0)==0){h=Bl(a,c[(c[c[j>>2]>>2]|0)+8>>2]|0)|0;c[(c[c[g>>2]>>2]|0)+8>>2]=h;l=c[c[g>>2]>>2]|0}else{l=k}k=l|0;l=c[k+4>>2]|0;c[k>>2]=c[k>>2]&-257;c[k+4>>2]=l;l=c[c[g>>2]>>2]|0;k=c[j>>2]|0;h=c[k>>2]|0;m=l+8|0;n=c[m>>2]|0;o=b[(c[n>>2]|0)+52>>1]|0;p=h+8|0;do{if((n|0)!=(c[p>>2]|0)){if((Ok(a,n,k)|0)!=0){break}Cl(a,c[d+8>>2]|0,c[m>>2]|0,c[p>>2]|0)}}while(0);p=(o&65535)>>>0>1>>>0|0;if((c[d+4>>2]|0)>>>0>1441791>>>0){o=c[g>>2]|0;if((c[o+4>>2]&65535|0)==7){Kk(a,o,0)}Dl(a,d);q=c[d>>2]|0}else{q=h}if((El(d)|0)==0){wk(a,(c[(c[g>>2]|0)+4>>2]&65535|0)==7?39:p,c[d+8>>2]&65535,c[q+12>>2]&65535,c[l+12>>2]&65535);r=d|0;c[r>>2]=q;i=f;return}else{b[(c[a+16>>2]|0)+((c[a+40>>2]|0)-(e[(c[j>>2]|0)+12>>1]|0)<<1)>>1]=c[l+12>>2];r=d|0;c[r>>2]=q;i=f;return}}function $k(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0;e=i;f=d+20|0;g=c[f>>2]|0;h=c[g+24>>2]|0;j=c[h+36>>2]|0;k=Fl(a,g)|0;g=d+24|0;l=c[g>>2]|0;if((c[l+4>>2]&65535|0)==5){m=l}else{Kk(a,l,k);m=c[g>>2]|0}k=c[m>>2]|0;do{if((c[h+4>>2]&65535|0)!=5){Kk(a,h,0);m=c[h>>2]|0;if((c[m>>2]&512|0)==0&(c[m+4>>2]&0|0)==0){break}m=a+112|0;c[(c[m>>2]|0)+16>>2]=c[d+8>>2];l=c[m>>2]|0;m=Al((c[d+4>>2]|0)>>>16)|0;Ae(l,1,8904,(n=i,i=i+8|0,c[n>>2]=m,n)|0);i=n}}while(0);if((c[j+4>>2]&65535|0)!=5){Kk(a,j,0)}Gl(a,h,j);m=h|0;h=c[(c[m>>2]|0)+8>>2]|0;if((b[(c[h>>2]|0)+52>>1]|0)==2){Ae(c[a+112>>2]|0,1,8768,(n=i,i=i+1|0,i=i+7&-8,c[n>>2]=0,n)|0);i=n;o=c[(c[m>>2]|0)+8>>2]|0}else{o=h}h=Hl(o,j)|0;if((Ok(a,h,c[g>>2]|0)|0)==0){o=d+8|0;c[(c[a+112>>2]|0)+16>>2]=c[o>>2];Cl(a,c[o>>2]|0,h,c[k+8>>2]|0)}if((c[d+4>>2]|0)>>>0>1441791>>>0){k=Ck(a,h)|0;h=d+8|0;o=j|0;Dk(a,36,c[h>>2]&65535,c[(c[m>>2]|0)+12>>2]&65535,c[(c[o>>2]|0)+12>>2]&65535,c[k+12>>2]&65535);c[c[f>>2]>>2]=k;Dl(a,d);p=d;q=h;r=o;s=p|0;t=c[s>>2]|0;u=c[q>>2]|0;v=u&65535;w=c[m>>2]|0;x=w+12|0;y=c[x>>2]|0;z=y&65535;A=c[r>>2]|0;B=A+12|0;C=c[B>>2]|0;D=C&65535;E=t+12|0;F=c[E>>2]|0;G=F&65535;Dk(a,37,v,z,D,G);H=d|0;c[H>>2]=t;i=e;return}else{p=c[g>>2]|0;q=d+8|0;r=j|0;s=p|0;t=c[s>>2]|0;u=c[q>>2]|0;v=u&65535;w=c[m>>2]|0;x=w+12|0;y=c[x>>2]|0;z=y&65535;A=c[r>>2]|0;B=A+12|0;C=c[B>>2]|0;D=C&65535;E=t+12|0;F=c[E>>2]|0;G=F&65535;Dk(a,37,v,z,D,G);H=d|0;c[H>>2]=t;i=e;return}}function al(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;d=i;e=b+20|0;Il(a,c[e>>2]|0);ul(a,c[(c[e>>2]|0)+20>>2]|0);f=c[e>>2]|0;g=c[f+20>>2]|0;if((c[g>>2]&32|0)==0&(c[g+4>>2]&0|0)==0){g=a+112|0;c[(c[g>>2]|0)+16>>2]=c[b+8>>2];h=c[g>>2]|0;g=Al((c[b+4>>2]|0)>>>16)|0;Ae(h,1,8904,(h=i,i=i+8|0,c[h>>2]=g,h)|0);i=h;j=c[e>>2]|0}else{j=f}f=Jl(a,j)|0;j=b+24|0;Kk(a,c[j>>2]|0,f);h=c[j>>2]|0;j=c[h>>2]|0;g=c[j+8>>2]|0;do{if((f|0)!=(g|0)){if((Ok(a,f,h)|0)!=0){break}k=b+8|0;c[(c[a+112>>2]|0)+16>>2]=c[k>>2];Cl(a,c[k>>2]|0,f,g)}}while(0);if((c[b+4>>2]|0)>>>0>1441791>>>0){Kl(a,c[e>>2]|0);Dl(a,b);g=b|0;l=c[g>>2]|0;m=g}else{l=j;m=b|0}j=c[e>>2]|0;Dk(a,42,c[b+8>>2]&65535,c[(c[c[j+24>>2]>>2]|0)+12>>2]&65535,c[(c[j+20>>2]|0)+12>>2]&65535,c[l+12>>2]&65535);c[m>>2]=l;i=d;return}function bl(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;if((b[(c[a+92>>2]|0)+22>>1]|0)==14){Ll(a)}e=d+20|0;ul(a,c[(c[e>>2]|0)+20>>2]|0);f=c[(c[(c[e>>2]|0)+20>>2]|0)+8>>2]|0;g=d+24|0;Kk(a,c[g>>2]|0,f);h=c[g>>2]|0;i=c[(c[h>>2]|0)+8>>2]|0;if((f|0)==0){j=Bl(a,i)|0;c[(c[(c[e>>2]|0)+20>>2]|0)+8>>2]=j;k=c[(c[e>>2]|0)+20>>2]|0;l=c[k+4>>2]|0;c[k>>2]=c[k>>2]&-257;c[k+4>>2]=l;l=c[g>>2]|0;m=j;n=j;o=l;p=c[(c[l>>2]|0)+8>>2]|0}else{m=i;n=f;o=h;p=i}do{if((n|0)!=(p|0)){if((Ok(a,n,o)|0)!=0){break}i=d+8|0;c[(c[a+112>>2]|0)+16>>2]=c[i>>2];Cl(a,c[i>>2]|0,n,m)}}while(0);m=c[g>>2]|0;if((c[d+4>>2]|0)>>>0>1441791>>>0){Kk(a,c[e>>2]|0,0);Dl(a,d);q=d}else{q=m}m=c[q>>2]|0;Dk(a,42,c[d+8>>2]&65535,c[(c[(c[a+96>>2]|0)+52>>2]|0)+12>>2]&65535,c[(c[(c[e>>2]|0)+20>>2]|0)+12>>2]&65535,c[m+12>>2]&65535);c[d>>2]=m;return}function cl(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0;e=d+24|0;Kk(a,c[e>>2]|0,0);f=d+20|0;g=c[(c[f>>2]|0)+20>>2]|0;h=Ml(a,g)|0;if((h|0)==-1){Jk(a,g);i=(b[a+52>>1]|0)-1&65535}else{i=h&65535}if((c[d+4>>2]|0)>>>0>1441791>>>0){h=Ck(a,c[(c[(c[f>>2]|0)+20>>2]|0)+8>>2]|0)|0;g=d+8|0;wk(a,51,c[g>>2]&65535,i,c[h+12>>2]&65535);c[c[f>>2]>>2]=h;Dl(a,d);j=d;k=g}else{j=c[e>>2]|0;k=d+8|0}wk(a,52,c[k>>2]&65535,i,c[(c[j>>2]|0)+12>>2]&65535);c[d>>2]=c[c[e>>2]>>2];return}function dl(a,b){a=a|0;b=b|0;var d=0,e=0;d=i;e=c[b+32>>2]|0;if((e|0)==0){c[b>>2]=0;c[b+16>>2]=0;i=d;return}b=c[e+4>>2]|0;if(!((b&65535|0)!=22|b>>>0<1376256>>>0)){i=d;return}Ae(c[a+112>>2]|0,1,9304,(a=i,i=i+1|0,i=i+7&-8,c[a>>2]=0,a)|0);i=a;i=d;return}function el(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;e=d+4|0;f=(c[e>>2]|0)>>>16;g=(f|0)==19|0;h=c[d+32>>2]|0;if((h|0)==0){i=3}else{j=c[h+4>>2]|0;if((j&65535|0)==22&(j>>>16|0)==(f|0)){k=0}else{i=3}}if((i|0)==3){ck(a,3);k=1}f=d+20|0;j=c[f>>2]|0;h=c[j+4>>2]|0;if((h&65535|0)==5){l=j;m=h}else{Kk(a,j,0);j=c[f>>2]|0;l=j;m=c[j+4>>2]|0}if((m&65535|0)==22){if((c[e>>2]^m)>>>0>65535>>>0){i=8}}else{i=8}if((i|0)==8){Nk(a,c[(c[l>>2]|0)+12>>2]|0,g)}l=d+24|0;i=c[l>>2]|0;if((c[i+4>>2]&65535|0)==5){n=i}else{Kk(a,i,0);n=c[l>>2]|0}Nk(a,c[(c[n>>2]|0)+12>>2]|0,g);if((k|0)==1){k=c[a+132>>2]|0;g=Ck(a,c[(c[k+44>>2]|0)+24>>2]|0)|0;n=Vh(k,(c[e>>2]&-65536|0)==1179648|0,0)|0;l=Vh(k,(c[e>>2]&-65536|0)==1245184|0,0)|0;e=d+8|0;k=g+12|0;wk(a,40,c[e>>2]&65535,c[n+12>>2]&65535,c[k>>2]&65535);zk(a,23,0);n=a+40|0;i=(c[n>>2]|0)-1|0;dk(a);wk(a,40,c[e>>2]&65535,c[l+12>>2]&65535,c[k>>2]&65535);b[(c[a+16>>2]|0)+(i<<1)>>1]=(c[n>>2]|0)-(c[(c[a+96>>2]|0)+28>>2]|0);c[d>>2]=g;return}else{c[d>>2]=0;return}}function fl(a,d,e){a=a|0;d=d|0;e=e|0;c[(c[d+24>>2]|0)+36>>2]=c[d+20>>2];b[d+14>>1]=2;Zk(a,d,e);return}function gl(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0;f=i;g=d+20|0;h=c[(c[c[g>>2]>>2]|0)+8>>2]|0;j=c[h>>2]|0;k=d+24|0;l=c[(c[c[k>>2]>>2]|0)+8>>2]|0;m=c[l>>2]|0;n=j+52|0;o=b[n>>1]|0;p=o&65535;do{if((o&65535)>>>0<3>>>0){q=b[m+52>>1]|0;if((q&65535)>>>0>=3>>>0){r=3;break}s=d+4|0;t=c[s>>2]|0;u=c[5904+((t>>>16)*36|0)+(p*12|0)+((q&65535)<<2)>>2]|0;if((u|0)==-1){v=s;r=7}else{w=u&65535;x=t}}else{r=3}}while(0);do{if((r|0)==3){p=d+4|0;if((h|0)!=(l|0)){v=p;r=7;break}o=c[p>>2]|0;t=o>>>16;if((t|0)==2){w=17;x=o;break}else if((t|0)!=7){v=p;r=7;break}w=18;x=o}}while(0);if((r|0)==7){r=a+112|0;c[(c[r>>2]|0)+16>>2]=c[d+8>>2];l=c[r>>2]|0;r=c[(c[c[g>>2]>>2]|0)+8>>2]|0;h=Al((c[v>>2]|0)>>>16)|0;o=c[(c[c[k>>2]>>2]|0)+8>>2]|0;Ae(l,1,9432,(l=i,i=i+24|0,c[l>>2]=r,c[l+8>>2]=h,c[l+16>>2]=o,l)|0);i=l;w=-1;x=c[v>>2]|0}switch(x>>>16|0){case 0:case 1:case 9:case 10:{y=(e[n>>1]|0)>>>0<(e[m+52>>1]|0)>>>0?m:j;break};case 2:case 3:case 4:case 5:case 6:case 7:{y=c[(c[a+132>>2]|0)+60>>2]|0;break};default:{y=c[(c[a+132>>2]|0)+44>>2]|0}}j=Ck(a,c[y+24>>2]|0)|0;y=j|0;m=c[y+4>>2]|0;c[y>>2]=c[y>>2]|512;c[y+4>>2]=m;Dk(a,w,c[d+8>>2]&65535,c[(c[c[g>>2]>>2]|0)+12>>2]&65535,c[(c[c[k>>2]>>2]|0)+12>>2]&65535,c[j+12>>2]&65535);c[d>>2]=j;i=f;return}function hl(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0;d=b+20|0;e=c[c[(c[c[d>>2]>>2]|0)+8>>2]>>2]|0;f=(c[b+4>>2]|0)>>>16;g=c[a+132>>2]|0;h=(f|0)==16;do{if((e|0)==(c[g+60>>2]|0)&h){i=28}else{if((e|0)!=(c[g+44>>2]|0)){i=-1;break}if((f|0)==17){i=29;break}i=h?28:-1}}while(0);h=Ck(a,c[e+24>>2]|0)|0;e=h|0;f=c[e+4>>2]|0;c[e>>2]=c[e>>2]|512;c[e+4>>2]=f;wk(a,i,c[b+8>>2]&65535,c[(c[c[d>>2]>>2]|0)+12>>2]&65535,c[h+12>>2]&65535);c[b>>2]=h;return}function il(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;g=i;i=i+8|0;h=g|0;j=d+14|0;if((b[j>>1]|0)==0){Nl(a,d,f);i=g;return}b[h>>1]=0;do{if((f|0)==0){k=6}else{if((b[(c[f>>2]|0)+52>>1]|0)!=7){k=6;break}l=c[c[f+4>>2]>>2]|0;if((l|0)==0){k=6}else{m=l}}}while(0);if((k|0)==6){m=c[(c[a+120>>2]|0)+16>>2]|0}k=d+24|0;f=c[k>>2]|0;do{if((f|0)==0){n=m}else{l=a+120|0;o=m;p=f;while(1){q=Ol(a,p,o,h)|0;r=rh(c[l>>2]|0,o,q)|0;if((r|0)==0){Pl(a,c[p+8>>2]|0,o,q,12688)}q=c[p+36>>2]|0;if((q|0)==0){break}else{o=r;p=q}}if((b[h>>1]|0)==0){n=r;break}if((b[r+14>>1]&64)==0){s=r}else{s=Ej(c[a+124>>2]|0,r)|0}Ql(a,c[k>>2]|0,s,0);n=s}}while(0);s=a+124|0;yj(c[s>>2]|0,n);n=Ck(a,Cj(c[s>>2]|0,0,c[(c[a+132>>2]|0)+72>>2]|0,1)|0)|0;Rl(a,30,c[k>>2]|0,c[d+8>>2]|0,e[j>>1]|0,c[n+12>>2]|0);c[d>>2]=n;i=g;return}function jl(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0;g=i;i=i+8|0;h=g|0;j=a+132|0;k=c[(c[(c[j>>2]|0)+92>>2]|0)+24>>2]|0;b[h>>1]=0;do{if((f|0)==0){l=k;m=k}else{if((b[(c[f>>2]|0)+52>>1]|0)!=8){l=k;m=k;break}n=c[f+4>>2]|0;o=c[n>>2]|0;p=c[n+4>>2]|0;l=(o|0)==0?k:o;m=(p|0)==0?k:p}}while(0);k=d+24|0;f=c[k>>2]|0;do{if((f|0)==0){q=m;r=l}else{p=a+120|0;o=m;n=l;s=f;while(1){t=s+36|0;u=c[t>>2]|0;v=Ol(a,s,n,h)|0;w=rh(c[p>>2]|0,n,v)|0;if((w|0)==0){Pl(a,c[s+8>>2]|0,n,v,13232);x=n}else{Sl(a,d,w);x=w}w=Ol(a,u,o,h)|0;v=rh(c[p>>2]|0,o,w)|0;if((v|0)==0){Pl(a,c[u+8>>2]|0,o,w,13144);y=o}else{y=v}v=c[(c[t>>2]|0)+36>>2]|0;if((v|0)==0){break}else{o=y;n=x;s=v}}if((b[h>>1]|0)==0){q=y;r=x;break}if((b[y+14>>1]&64)==0){z=y}else{z=Ej(c[a+124>>2]|0,y)|0}Ql(a,c[k>>2]|0,z,1);q=z;r=x}}while(0);x=c[(c[j>>2]|0)+76>>2]|0;j=a+124|0;yj(c[j>>2]|0,r);yj(c[j>>2]|0,q);q=Ck(a,Cj(c[j>>2]|0,0,x,2)|0)|0;Rl(a,31,c[k>>2]|0,c[d+8>>2]|0,e[d+14>>1]|0,c[q+12>>2]|0);c[d>>2]=q;i=g;return}function kl(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;g=i;h=d+14|0;if((b[h>>1]|0)==0){Ae(c[a+112>>2]|0,1,13336,(j=i,i=i+1|0,i=i+7&-8,c[j>>2]=0,j)|0);i=j}if((f|0)==0){k=0}else{k=(b[(c[f>>2]|0)+52>>1]|0)==9?f:0}f=d+24|0;j=c[f>>2]|0;a:do{if((j|0)!=0){l=k+4|0;if((k|0)==0){m=j;while(1){Kk(a,m,0);m=c[m+36>>2]|0;if((m|0)==0){break a}}}else{n=0;o=j}while(1){m=c[(c[l>>2]|0)+(n<<2)>>2]|0;Kk(a,o,m);do{if((m|0)!=0){if((m|0)==(c[(c[o>>2]|0)+8>>2]|0)){break}Ok(a,m,o)|0}}while(0);m=c[o+36>>2]|0;if((m|0)==0){break}else{n=n+1|0;o=m}}}}while(0);o=a+124|0;n=c[o>>2]|0;if((b[h>>1]|0)==0){p=0;q=n}else{j=0;k=f;l=n;while(1){n=c[k>>2]|0;yj(l,c[(c[n>>2]|0)+8>>2]|0);m=j+1|0;r=c[o>>2]|0;if((m|0)<(e[h>>1]|0)){j=m;k=n+36|0;l=r}else{p=m;q=r;break}}}l=Ck(a,Cj(q,0,c[(c[a+132>>2]|0)+80>>2]|0,p)|0)|0;Rl(a,30,c[f>>2]|0,c[d+8>>2]|0,e[h>>1]|0,c[l+12>>2]|0);c[d>>2]=l;i=g;return}function ll(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0;d=c[b+24>>2]|0;e=c[d+36>>2]|0;if((c[d+4>>2]&65535|0)!=5){Kk(a,d,0)}if((c[e+4>>2]&65535|0)!=5){Kk(a,e,0)}Gl(a,d,e);f=d|0;d=Ck(a,Hl(c[(c[f>>2]|0)+8>>2]|0,e)|0)|0;Dk(a,36,c[b+8>>2]&65535,c[(c[f>>2]|0)+12>>2]&65535,c[(c[e>>2]|0)+12>>2]&65535,c[d+12>>2]&65535);e=c[f>>2]|0;if((c[e>>2]&512|0)==0&(c[e+4>>2]&0|0)==0){g=d;h=b|0;c[h>>2]=g;return}e=d|0;f=c[e+4>>2]|0;c[e>>2]=c[e>>2]|512;c[e+4>>2]=f;g=d;h=b|0;c[h>>2]=g;return}function ml(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;e=i;f=c[d+24>>2]|0;g=c[(c[f+36>>2]|0)+28>>2]|0;Kk(a,f,g);h=f|0;j=c[h>>2]|0;k=c[j+8>>2]|0;do{if((g|0)==(k|0)){l=j}else{if((sh(c[a+120>>2]|0,g,k)|0)!=0){l=c[h>>2]|0;break}if((b[(c[g>>2]|0)+52>>1]|0)==6){rl(a,f);c[d>>2]=c[h>>2];i=e;return}if((b[(c[k>>2]|0)+52>>1]|0)==6){m=Ck(a,g)|0;wk(a,33,c[d+8>>2]&65535,c[(c[h>>2]|0)+12>>2]&65535,c[m+12>>2]&65535);c[d>>2]=m;i=e;return}else{m=a+112|0;c[(c[m>>2]|0)+16>>2]=c[d+8>>2];Ae(c[m>>2]|0,1,14168,(m=i,i=i+16|0,c[m>>2]=k,c[m+8>>2]=g,m)|0);i=m;i=e;return}}}while(0);c[d>>2]=l;i=e;return}function nl(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;Il(a,b);d=b+20|0;e=c[d>>2]|0;f=e|0;if((c[f>>2]&32|0)==0&(c[f+4>>2]&0|0)==0){f=Ck(a,c[e+8>>2]|0)|0;wk(a,40,c[b+8>>2]&65535,c[(c[d>>2]|0)+12>>2]&65535,c[f+12>>2]&65535);c[b>>2]=f;return}else{Kl(a,b);return}}function ol(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0;e=i;f=d+20|0;ul(a,c[f>>2]|0);if((b[(c[a+92>>2]|0)+22>>1]|0)==14){Ll(a)}g=f;f=c[(c[g>>2]|0)+8>>2]|0;h=d+8|0;if((f|0)==0){j=a+112|0;c[(c[j>>2]|0)+16>>2]=c[h>>2];Ae(c[j>>2]|0,1,14280,(j=i,i=i+8|0,c[j>>2]=c[(c[g>>2]|0)+20>>2],j)|0);i=j;k=c[(c[g>>2]|0)+8>>2]|0}else{k=f}f=Ck(a,k)|0;Dk(a,41,c[h>>2]&65535,c[(c[(c[a+96>>2]|0)+52>>2]|0)+12>>2]&65535,c[(c[g>>2]|0)+12>>2]&65535,c[f+12>>2]&65535);c[d>>2]=f;i=e;return}function pl(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0;g=i;h=c[d+4>>2]&65535;if((h|0)==0|(h|0)==22){c[d>>2]=0;h=c[(c[d+24>>2]|0)+20>>2]|0;j=h+68|0;if((c[(c[j>>2]|0)+8>>2]|0)==1){Ae(c[a+112>>2]|0,1,16728,(k=i,i=i+8|0,c[k>>2]=c[h+8>>2],k)|0);i=k}k=wl(a,d)|0;xl(a,k,f);l=a+120|0;m=c[l>>2]|0;n=nh(m,c[(c[h+36>>2]|0)+68>>2]|0,c[m+16>>2]|0)|0;m=c[c[(c[j>>2]|0)+4>>2]>>2]|0;if((b[m+14>>1]&16)!=0){oh(c[l>>2]|0,m)|0}Tl(a,k,e[h+66>>1]|0);zl(a,k);o=n;p=a+40|0;q=c[p>>2]|0;r=q-1|0;s=d+16|0;c[s>>2]=r;t=d+28|0;c[t>>2]=o;u=d|0;c[u>>2]=0;i=g;return}n=d+20|0;k=c[n>>2]|0;h=c[k+68>>2]|0;if((c[h+8>>2]|0)!=0){Ul(a,d,h,-1)}wk(a,40,c[d+8>>2]&65535,c[(c[k+80>>2]|0)+12>>2]&65535,0);h=k+36|0;if((b[(c[h>>2]|0)+58>>1]|0)==0){o=c[(c[(c[n>>2]|0)+36>>2]|0)+68>>2]|0;p=a+40|0;q=c[p>>2]|0;r=q-1|0;s=d+16|0;c[s>>2]=r;t=d+28|0;c[t>>2]=o;u=d|0;c[u>>2]=0;i=g;return}n=a+120|0;k=Ah(c[n>>2]|0)|0;m=c[h>>2]|0;h=c[m+68>>2]|0;do{if((f|0)!=0){if((c[f>>2]|0)!=(m|0)){break}qh(c[n>>2]|0,h,f)|0}}while(0);f=c[n>>2]|0;m=nh(f,h,c[f+16>>2]|0)|0;Bh(c[n>>2]|0,k);o=m;p=a+40|0;q=c[p>>2]|0;r=q-1|0;s=d+16|0;c[s>>2]=r;t=d+28|0;c[t>>2]=o;u=d|0;c[u>>2]=0;i=g;return}function ql(d,e,f){d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0,k=0;g=Ke(c[d+116>>2]|0,c[e+16>>2]|0)|0;if((f|0)==0){h=0}else{h=(b[(c[f>>2]|0)+52>>1]|0)==5?f:0}f=e+8|0;i=Ze(c[d+128>>2]|0,c[f>>2]|0,g,h)|0;h=Ck(d,c[i+8>>2]|0)|0;if((a[(c[d+92>>2]|0)+20|0]|0)==0){wk(d,40,c[f>>2]&65535,c[i+12>>2]&65535,c[h+12>>2]&65535);j=h;k=e|0;c[k>>2]=j;return}else{vl(d,c[i+12>>2]|0,c[h+12>>2]|0);j=h;k=e|0;c[k>>2]=j;return}}function rl(a,b){a=a|0;b=b|0;var d=0,e=0;d=b|0;if((c[d>>2]|0)==0){Vk(a,b,0)}e=Ck(a,c[(c[a+124>>2]|0)+12>>2]|0)|0;wk(a,2,c[b+8>>2]&65535,c[(c[d>>2]|0)+12>>2]&65535,c[e+12>>2]&65535);c[d>>2]=e;return}function sl(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0;f=a+120|0;g=Ah(c[f>>2]|0)|0;if((b[d+14>>1]&16)!=0){qh(c[f>>2]|0,d,d)|0}d=a+64|0;h=c[d>>2]|0;if((h|0)==0){i=0}else{i=c[(c[h+4>>2]|0)+4>>2]|0}h=c[a+92>>2]|0;j=c[(c[f>>2]|0)+16>>2]|0;k=c[(c[(c[a+132>>2]|0)+20>>2]|0)+24>>2]|0;l=h+4|0;if((k|0)!=(c[l>>2]|0)){m=k;do{k=m+8|0;n=c[k>>2]|0;do{if((b[n+14>>1]&16)!=0){if((b[(nh(c[f>>2]|0,n,j)|0)+14>>1]&64)==0){break}Vl(a,c[k>>2]|0)}}while(0);m=c[m+44>>2]|0;}while((m|0)!=(c[l>>2]|0))}l=c[h>>2]|0;if((l|0)!=0){h=l;do{l=h+8|0;m=c[l>>2]|0;do{if((m|0)!=0){if((b[m+14>>1]&16)==0){break}if((b[(nh(c[f>>2]|0,m,j)|0)+14>>1]&64)==0){break}Vl(a,c[l>>2]|0)}}while(0);h=c[h+20>>2]|0;}while((h|0)!=0)}h=c[d>>2]|0;if((h|0)==0){o=c[f>>2]|0;Bh(o,g);return}a=c[h+4>>2]|0;if((c[a+4>>2]|0)==(i|0)){o=c[f>>2]|0;Bh(o,g);return}Qe(a,0);Ne(c[c[d>>2]>>2]|0,0);b[e+44>>1]=i+1;o=c[f>>2]|0;Bh(o,g);return}function tl(a,d,f,g){a=a|0;d=d|0;f=f|0;g=g|0;var h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0;h=a+96|0;i=c[(c[h>>2]|0)+24>>2]|0;j=a+40|0;k=c[j>>2]|0;c[f>>2]=k;l=c[j>>2]|0;m=bk(a,c[(c[(c[h>>2]|0)+8>>2]|0)+8>>2]|0)|0;n=Wl(a)|0;o=a+102|0;a:do{if((b[o>>1]|0)==2){p=m+12|0;wk(a,53,c[d+4>>2]&65535,b[a+52>>1]|0,c[p>>2]&65535);if((b[(c[h>>2]|0)+22>>1]|0)!=13){q=i;break}r=c[a+16>>2]|0;s=b[r+(i+1<<1)>>1]|0;t=b[r+(i+2<<1)>>1]|0;Bk(a,48,s,t);r=i+3|0;if((n|0)!=-1){wk(a,52,s,n&65535,t);c[(c[a+20>>2]|0)+(n<<2)>>2]=0}u=ji(c[(c[h>>2]|0)+48>>2]|0,8216)|0;if((u|0)==0){q=r;break}Dk(a,42,s,t,c[u+12>>2]&65535,c[p>>2]&65535);q=r}else{r=c[h>>2]|0;p=c[r+60>>2]|0;do{if((p|0)!=0){if((b[p+22>>1]|0)!=13){break}if((b[r+22>>1]|0)==14){wk(a,56,c[d+4>>2]&65535,0,c[m+12>>2]&65535);u=c[(c[h>>2]|0)+52>>2]|0;if((u|0)==0){q=i;break a}wk(a,51,c[c[a+108>>2]>>2]&65535,n&65535,c[u+12>>2]&65535);q=i;break a}u=c[r+48>>2]|0;t=ji(u,8216)|0;s=c[u+36>>2]|0;do{if((t|0)==0){v=13}else{if((s|0)==0){w=t;break}if((c[t+12>>2]|0)>>>0>(c[s+60>>2]|0)>>>0){w=t}else{v=13}}}while(0);if((v|0)==13){t=ki(c[a+132>>2]|0,u,c[m+8>>2]|0,8216,0)|0;Bj(u);w=t}Dk(a,55,c[d+4>>2]&65535,c[(c[(c[h>>2]|0)+52>>2]|0)+12>>2]&65535,c[w+12>>2]&65535,c[m+12>>2]&65535);q=i;break a}}while(0);zk(a,56,c[d+4>>2]&65535);Xl(a);Gk(a,c[m+12>>2]&65535);q=i}}while(0);Yl(a);Zl(a);if((b[o>>1]|0)==2){b[a+52>>1]=0}o=c[h>>2]|0;if((e[o+12>>1]|0)==(c[(c[a>>2]|0)+4>>2]|0)){x=o;y=c[j>>2]|0;z=x+24|0;A=c[z>>2]|0;B=q-l|0;C=B+y|0;D=C-A|0;_l(a,q,k,D);E=c[j>>2]|0;F=c[f>>2]|0;G=E-F|0;c[g>>2]=G;return}ak(a,c[m+12>>2]|0);x=c[h>>2]|0;y=c[j>>2]|0;z=x+24|0;A=c[z>>2]|0;B=q-l|0;C=B+y|0;D=C-A|0;_l(a,q,k,D);E=c[j>>2]|0;F=c[f>>2]|0;G=E-F|0;c[g>>2]=G;return}function ul(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0;d=i;e=b|0;f=c[e>>2]|0;g=c[e+4>>2]|0;if((f&12288|0)==0&(g&0|0)==0){i=d;return}e=c[(c[a+96>>2]|0)+48>>2]|0;h=f&4096;if((f&32|0)==0&(g&0|0)==0){j=b+40|0;k=b+20|0}else{j=b+32|0;k=b+20|0}b=c[j>>2]|0;j=c[k>>2]|0;k=(h|0)!=0;do{if((e|0)==(b|0)|k^1){if((h|0)!=0){i=d;return}if((e|0)==0){break}if((Dh(b,e)|0)==0){break}i=d;return}}while(0);Ae(c[a+112>>2]|0,1,14416,(a=i,i=i+24|0,c[a>>2]=c[b+8>>2],c[a+8>>2]=j,c[a+16>>2]=k?14552:14496,a)|0);i=a;i=d;return}function vl(b,d,e){b=b|0;d=d|0;e=e|0;wk(b,54,0,d&65535,e&65535);e=b+92|0;Ak(c[b>>2]|0,c[b+96>>2]|0,c[e>>2]|0,(c[b+40>>2]|0)+65533&65535);a[(c[e>>2]|0)+20|0]=1;return}function wl(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;e=i;f=a+12|0;g=c[f>>2]|0;h=g+4|0;j=c[h>>2]|0;if((j|0)==0){tk(a);k=c[h>>2]|0}else{k=j}c[f>>2]=k;c[g+12>>2]=d;b[g+26>>1]=0;b[g+28>>1]=0;k=d+24|0;f=c[k>>2]|0;j=c[f+4>>2]&65535;a:do{switch(j|0){case 18:case 17:case 6:{h=c[f+20>>2]|0;l=h|0;if((c[l>>2]&131072|0)==0&(c[l+4>>2]&0|0)==0){m=h;n=11;break a}l=Ck(a,c[h+8>>2]|0)|0;vl(a,c[(c[(c[k>>2]|0)+20>>2]|0)+12>>2]|0,c[l+12>>2]|0);m=l;n=11;break};case 19:{ul(a,c[f+20>>2]|0);m=c[(c[k>>2]|0)+20>>2]|0;n=11;break};case 8:{Il(a,f);l=c[f+20>>2]|0;h=l|0;if((c[h>>2]&32|0)==0&(c[h+4>>2]&0|0)==0){m=l;n=11;break a}Kl(a,f);m=c[f>>2]|0;n=11;break};case 14:{l=c[f+20>>2]|0;h=l;o=c[l+68>>2]|0;if((o|0)==0){m=h;n=11}else{p=o;q=h}break};default:{Kk(a,f,0);m=c[c[k>>2]>>2]|0;n=11}}}while(0);if((n|0)==11){p=c[m+8>>2]|0;q=m}if((b[(c[p>>2]|0)+52>>1]|0)!=5&(j|0)!=14){j=a+112|0;c[(c[j>>2]|0)+16>>2]=c[d+8>>2];Ae(c[j>>2]|0,1,15080,(j=i,i=i+8|0,c[j>>2]=p,j)|0);i=j}c[g+8>>2]=q;c[g+16>>2]=p;b[g+30>>1]=Ah(c[a+120>>2]|0)|0;if((b[p+14>>1]&1)==0){c[g+20>>2]=0;b[g+24>>1]=-1;i=e;return g|0}else{a=(c[p+8>>2]|0)-1|0;c[g+20>>2]=c[c[(c[(c[p+4>>2]|0)+(a<<2)>>2]|0)+4>>2]>>2];b[g+24>>1]=a;i=e;return g|0}return 0}function xl(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0;g=c[d+12>>2]|0;$l(a,d,f,(e[g+14>>1]|0)-1|0);f=c[(c[g+24>>2]|0)+36>>2]|0;if((f|0)!=0){g=f;do{am(a,d,g);g=c[g+36>>2]|0;}while((g|0)!=0)}g=a+120|0;zh(c[g>>2]|0);if((b[d+28>>1]|0)!=0){bm(a,d)}f=d+16|0;h=c[f>>2]|0;if((b[h+14>>1]&1)==0){return}i=c[h+8>>2]|0;j=c[(c[h+4>>2]|0)+(i-1<<2)>>2]|0;if((b[j+14>>1]&16)==0){k=j;l=i}else{i=oh(c[g>>2]|0,j)|0;k=i;l=c[(c[f>>2]|0)+8>>2]|0}cm(a,d,k,l+65534&65535);return}function yl(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;e=i;f=c[d+8>>2]|0;g=c[d+12>>2]|0;h=g+8|0;j=f|0;k=d+26|0;Dk(a,25,c[h>>2]&65535,((c[j>>2]|0)>>>16|c[j+4>>2]<<16)&1,c[f+12>>2]&65535,b[k>>1]|0);Gk(a,0);f=c[c[(c[d+16>>2]|0)+4>>2]>>2]|0;if((f|0)!=0){if((b[f+14>>1]&16)==0){l=f}else{l=oh(c[a+120>>2]|0,f)|0}f=Ck(a,l)|0;l=f|0;d=c[l+4>>2]|0;c[l>>2]=c[l>>2]|512;c[l+4>>2]=d;c[g>>2]=f;b[(c[a+16>>2]|0)+((c[a+40>>2]|0)-1<<1)>>1]=c[f+12>>2];m=b[k>>1]|0;dm(a,m,0);n=b[k>>1]|0;o=n+1&65535;p=g+12|0;b[p>>1]=o;i=e;return}if((c[g+32>>2]|0)==0){c[g>>2]=0}else{f=a+112|0;c[(c[f>>2]|0)+16>>2]=c[h>>2];Ae(c[f>>2]|0,1,8656,(f=i,i=i+8|0,c[f>>2]=21144,f)|0);i=f}b[(c[a+16>>2]|0)+((c[a+40>>2]|0)-1<<1)>>1]=0;m=b[k>>1]|0;dm(a,m,0);n=b[k>>1]|0;o=n+1&65535;p=g+12|0;b[p>>1]=o;i=e;return}function zl(a,d){a=a|0;d=d|0;var f=0;Bh(c[a+120>>2]|0,e[d+30>>1]|0);f=a+48|0;b[f>>1]=(b[f>>1]|0)-(b[d+26>>1]|0);c[a+12>>2]=d;return}function Al(a){a=a|0;return c[1984+(a<<2)>>2]|0}function Bl(a,b){a=a|0;b=b|0;var d=0,e=0;d=c[b>>2]|0;if((c[d>>2]&4096|0)==0&(c[d+4>>2]&0|0)==0){e=b;return e|0}e=Fj(c[a+124>>2]|0,b)|0;return e|0}function Cl(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0;f=i;g=a+112|0;c[(c[g>>2]|0)+16>>2]=b;Ae(c[g>>2]|0,1,9008,(g=i,i=i+16|0,c[g>>2]=e,c[g+8>>2]=d,g)|0);i=g;i=f;return}function Dl(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0;d=i;e=b+4|0;f=c[e>>2]|0;g=f>>>16;switch(g|0){case 26:{h=655360;j=f;break};case 27:{h=720896;j=f;break};case 24:{h=524288;j=f;break};case 28:{h=786432;j=f;break};case 23:{h=65536;j=f;break};case 22:{h=0;j=f;break};case 25:{h=589824;j=f;break};default:{f=c[a+112>>2]|0;k=Al(g)|0;Ae(f,1,9192,(f=i,i=i+8|0,c[f>>2]=k,f)|0);i=f;h=-65536;j=c[e>>2]|0}}c[e>>2]=j&65535|h;gl(a,b);c[e>>2]=c[e>>2]&65535|g<<16;i=d;return}function El(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0;d=c[a+20>>2]|0;if((c[d+4>>2]&65535|0)==7){e=0;return e|0}else{f=a}while(1){g=c[f+24>>2]|0;h=c[g+4>>2]|0;i=h&65535;if((i|0)==5){e=0;j=8;break}else if((i|0)==4){f=g}else{break}}if((j|0)==8){return e|0}j=c[a+32>>2]|0;do{if((j|0)!=0){if((c[j+4>>2]&65535|0)==22){e=0}else{break}return e|0}}while(0);if((i|0)==22&h>>>0>1376255>>>0){e=0;return e|0}if((b[(c[c[(c[d>>2]|0)+8>>2]>>2]|0)+52>>1]|0)==6){return(b[(c[c[(c[g>>2]|0)+8>>2]>>2]|0)+52>>1]|0)==6|0}else{e=1;return e|0}return 0}function Fl(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=c[d+4>>2]&65535;do{if((e|0)==1){f=c[d+24>>2]|0;g=c[f+36>>2]|0;h=Fl(a,f)|0;if((h|0)==0){i=0;break}f=b[(c[h>>2]|0)+52>>1]|0;if((f<<16>>16|0)==9){if((c[g+4>>2]&65535|0)!=16){i=0;break}j=c[g+20>>2]|0;if((b[(c[c[j+8>>2]>>2]|0)+52>>1]|0)!=0){i=0;break}g=c[j+24>>2]|0;if((g|0)<0){i=0;break}if(g>>>0>(c[h+8>>2]|0)>>>0){i=0;break}i=c[(c[h+4>>2]|0)+(g<<2)>>2]|0;break}else if((f<<16>>16|0)==8){i=c[(c[h+4>>2]|0)+4>>2]|0;break}else if((f<<16>>16|0)==7){i=c[c[h+4>>2]>>2]|0;break}else{i=h;break}}else if((e|0)==7|(e|0)==5){i=c[(c[d+20>>2]|0)+8>>2]|0}else if((e|0)==8){h=Fl(a,c[d+24>>2]|0)|0;if((h|0)==0){i=0;break}f=Ke(c[a+116>>2]|0,c[d+16>>2]|0)|0;g=ji(c[h>>2]|0,f)|0;if((g|0)==0){i=0;break}f=c[g+8>>2]|0;if((b[f+14>>1]&16)==0){i=f;break}i=uh(c[a+120>>2]|0,h,f)|0}else{i=0}}while(0);return i|0}function Gl(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;f=i;g=d|0;h=c[(c[g>>2]|0)+8>>2]|0;j=b[(c[h>>2]|0)+52>>1]|0;if((j<<16>>16|0)==9){if((b[(c[c[(c[e>>2]|0)+8>>2]>>2]|0)+52>>1]|0)==0){if((c[e+4>>2]&65535|0)==16){k=h}else{l=8}}else{l=8}if((l|0)==8){l=a+112|0;c[(c[l>>2]|0)+16>>2]=c[d+8>>2];Ae(c[l>>2]|0,1,13712,(m=i,i=i+8|0,c[m>>2]=21144,m)|0);i=m;k=c[(c[g>>2]|0)+8>>2]|0}l=c[(c[e+20>>2]|0)+24>>2]|0;do{if((l|0)>=0){if(l>>>0>=(c[k+8>>2]|0)>>>0){break}i=f;return}}while(0);n=a+112|0;c[(c[n>>2]|0)+16>>2]=c[d+8>>2];Ae(c[n>>2]|0,1,13560,(m=i,i=i+16|0,c[m>>2]=l,c[m+8>>2]=k,m)|0);i=m;i=f;return}else if((j<<16>>16|0)==8){k=c[c[h+4>>2]>>2]|0;h=c[(c[e>>2]|0)+8>>2]|0;if((k|0)==(h|0)){i=f;return}l=a+112|0;c[(c[l>>2]|0)+16>>2]=c[d+8>>2];Ae(c[l>>2]|0,1,13896,(m=i,i=i+16|0,c[m>>2]=k,c[m+8>>2]=h,m)|0);i=m;i=f;return}else if((j<<16>>16|0)==7|(j<<16>>16|0)==2){if((b[(c[c[(c[e>>2]|0)+8>>2]>>2]|0)+52>>1]|0)==0){i=f;return}e=a+112|0;c[(c[e>>2]|0)+16>>2]=c[d+8>>2];Ae(c[e>>2]|0,1,14072,(m=i,i=i+8|0,c[m>>2]=c[(c[c[(c[g>>2]|0)+8>>2]>>2]|0)+8>>2],m)|0);i=m;i=f;return}else{e=a+112|0;c[(c[e>>2]|0)+16>>2]=c[d+8>>2];Ae(c[e>>2]|0,1,13424,(m=i,i=i+8|0,c[m>>2]=c[(c[g>>2]|0)+8>>2],m)|0);i=m;i=f;return}}function Hl(a,d){a=a|0;d=d|0;var e=0,f=0;e=b[(c[a>>2]|0)+52>>1]|0;if((e<<16>>16|0)==8){f=c[(c[a+4>>2]|0)+4>>2]|0;return f|0}else if((e<<16>>16|0)==9){f=c[(c[a+4>>2]|0)+(c[(c[d+20>>2]|0)+24>>2]<<2)>>2]|0;return f|0}else if((e<<16>>16|0)==2){f=a;return f|0}else if((e<<16>>16|0)==7){f=c[c[a+4>>2]>>2]|0;return f|0}else{f=0;return f|0}return 0}function Il(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0;e=i;f=d+24|0;do{if((b[(c[a+92>>2]|0)+22>>1]|0)==14){if((c[(c[f>>2]|0)+4>>2]&65535|0)!=20){break}Ll(a)}}while(0);g=c[f>>2]|0;if((c[g+4>>2]&65535|0)==5){h=g}else{Kk(a,g,0);h=c[f>>2]|0}g=c[c[(c[h>>2]|0)+8>>2]>>2]|0;h=g|0;if((c[h>>2]&4096|0)==0&(c[h+4>>2]&0|0)==0){j=g}else{j=c[g+36>>2]|0}g=Ke(c[a+116>>2]|0,c[d+16>>2]|0)|0;h=Xe(c[a+128>>2]|0,j,g)|0;if((h|0)==0){Ae(c[a+112>>2]|0,1,14912,(k=i,i=i+16|0,c[k>>2]=c[j+8>>2],c[k+8>>2]=g,k)|0);i=k;l=h;ul(a,l);i=e;return}g=h|0;do{if(!((c[g>>2]&32|0)==0&(c[g+4>>2]&0|0)==0)){if((c[(c[f>>2]|0)+4>>2]&65535|0)!=20){break}Ae(c[a+112>>2]|0,1,14664,(k=i,i=i+1|0,i=i+7&-8,c[k>>2]=0,k)|0);i=k;l=h;ul(a,l);i=e;return}}while(0);c[d+20>>2]=h;l=h;ul(a,l);i=e;return}function Jl(a,d){a=a|0;d=d|0;var e=0,f=0;e=c[(c[d+20>>2]|0)+8>>2]|0;if((b[e+14>>1]&16)==0){f=e;return f|0}f=uh(c[a+120>>2]|0,c[(c[c[d+24>>2]>>2]|0)+8>>2]|0,e)|0;return f|0}function Kl(a,b){a=a|0;b=b|0;var d=0,e=0;d=c[b+20>>2]|0;e=Ck(a,Jl(a,b)|0)|0;Dk(a,41,c[b+8>>2]&65535,c[(c[c[b+24>>2]>>2]|0)+12>>2]&65535,c[d+12>>2]&65535,c[e+12>>2]&65535);c[b>>2]=e;return}function Ll(d){d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0;e=d+96|0;f=e;while(1){g=c[f>>2]|0;if((b[g+22>>1]|0)==13){break}else{f=g+60|0}}f=c[g+52>>2]|0;g=f;if((Ml(d,g)|0)==-1){Tk(d,g)}if((c[(c[e>>2]|0)+52>>2]|0)!=0){h=d+92|0;i=c[h>>2]|0;j=i+20|0;a[j]=1;return}g=Ck(d,c[f+8>>2]|0)|0;c[(c[e>>2]|0)+52>>2]=g;h=d+92|0;i=c[h>>2]|0;j=i+20|0;a[j]=1;return}function Ml(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=b[a+52>>1]|0;if(e<<16>>16==0){f=-1;return f|0}g=c[a+20>>2]|0;a=e&65535;e=0;while(1){h=e+1|0;if((c[g+(e<<2)>>2]|0)==(d|0)){f=e;i=5;break}if((h|0)<(a|0)){e=h}else{f=-1;i=5;break}}if((i|0)==5){return f|0}return 0}function Nl(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=a+132|0;g=c[(c[(c[f>>2]|0)+64>>2]|0)+24>>2]|0;do{if((e|0)==0){h=8}else{i=b[(c[e>>2]|0)+52>>1]|0;if((i<<16>>16|0)==7){j=c[c[e+4>>2]>>2]|0;if((b[(c[j>>2]|0)+52>>1]|0)==13){h=8;break}else{k=j;h=9;break}}else if((i<<16>>16|0)!=8){h=8;break}i=c[e+4>>2]|0;j=c[i>>2]|0;l=c[i+4>>2]|0;Sl(a,d,j);if((l|0)==0){h=5}else{if((b[(c[l>>2]|0)+52>>1]|0)==13){h=5}else{m=l}}if((h|0)==5){m=g}l=a+124|0;yj(c[l>>2]|0,j);yj(c[l>>2]|0,m);n=(c[f>>2]|0)+76|0;o=2;p=31}}while(0);if((h|0)==8){k=g;h=9}if((h|0)==9){yj(c[a+124>>2]|0,k);n=(c[f>>2]|0)+72|0;o=1;p=30}f=Ck(a,Cj(c[a+124>>2]|0,0,c[n>>2]|0,o)|0)|0;Rl(a,p,c[d+24>>2]|0,c[d+8>>2]|0,0,c[f+12>>2]|0);c[d>>2]=f;return}function Ol(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0;Uk(a,d,e);e=c[d+4>>2]|0;a=e&65535;do{if((a|0)==0){g=5}else if((a|0)==22){if((e&-65536|0)!=1310720){g=7;break}if((c[(c[d+24>>2]|0)+4>>2]&65535|0)==14){g=6;break}if((a|0)==0){g=5}else{g=7}}else if((a|0)==14){g=6}else{g=7}}while(0);if((g|0)==5){if((c[(c[d+24>>2]|0)+4>>2]&65535|0)==14){g=6}else{g=7}}if((g|0)==6){a=c[d+28>>2]|0;b[f>>1]=1;h=a;return h|0}else if((g|0)==7){h=c[(c[d>>2]|0)+8>>2]|0;return h|0}return 0}function Pl(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0;g=i;h=a+112|0;c[(c[h>>2]|0)+16>>2]=b;Ae(c[h>>2]|0,1,12800,(h=i,i=i+24|0,c[h>>2]=f,c[h+8>>2]=d,c[h+16>>2]=e,h)|0);i=h;i=g;return}function Ql(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0;f=(e|0)!=0;if(f){g=c[b+36>>2]|0}else{g=b}if((g|0)==0){return}if(f){h=g}else{f=g;do{if((c[f>>2]|0)==0){Vk(a,f,d)}f=c[f+36>>2]|0;}while((f|0)!=0);return}while(1){if((c[h>>2]|0)==0){Vk(a,h,d)}f=c[h+36>>2]|0;if((f|0)==0){i=12;break}g=c[f+36>>2]|0;if((g|0)==0){i=12;break}else{h=g}}if((i|0)==12){return}}function Rl(a,d,e,f,g,h){a=a|0;d=d|0;e=e|0;f=f|0;g=g|0;h=h|0;var i=0,j=0,k=0,l=0;i=g+4|0;vk(a,i);j=a+40|0;k=a+16|0;b[(c[k>>2]|0)+(c[j>>2]<<1)>>1]=d;b[(c[k>>2]|0)+((c[j>>2]|0)+1<<1)>>1]=f;b[(c[k>>2]|0)+((c[j>>2]|0)+2<<1)>>1]=g;if((e|0)==0){l=3}else{g=3;f=e;while(1){b[(c[k>>2]|0)+((c[j>>2]|0)+g<<1)>>1]=c[(c[f>>2]|0)+12>>2];e=c[f+36>>2]|0;d=g+1|0;if((e|0)==0){l=d;break}else{g=d;f=e}}}b[(c[k>>2]|0)+((c[j>>2]|0)+l<<1)>>1]=h;c[j>>2]=(c[j>>2]|0)+i;return}function Sl(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0;f=i;if((e|0)==0){g=3}else{h=c[e>>2]|0;if((b[h+52>>1]|0)==13){g=3}else{j=e;k=h;g=5}}do{if((g|0)==3){h=c[(c[(c[a+132>>2]|0)+64>>2]|0)+24>>2]|0;if((h|0)==0){l=0;break}j=h;k=c[h>>2]|0;g=5}}while(0);do{if((g|0)==5){h=k|0;if((c[h>>2]&256|0)==0&(c[h+4>>2]&0|0)==0){l=j;break}i=f;return}}while(0);j=a+112|0;c[(c[j>>2]|0)+16>>2]=c[d+8>>2];Ae(c[j>>2]|0,1,13008,(j=i,i=i+8|0,c[j>>2]=l,j)|0);i=j;i=f;return}function Tl(a,d,e){a=a|0;d=d|0;e=e|0;var f=0;f=d+26|0;wk(a,32,c[(c[d+12>>2]|0)+8>>2]&65535,e&65535,b[f>>1]|0);dm(a,b[f>>1]|0,0);Gk(a,0);return}function Ul(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;f=i;i=i+64|0;g=f|0;h=f+8|0;j=f+16|0;k=f+24|0;l=f+32|0;m=f+48|0;n=f+56|0;em(d,g,h);d=(e|0)==-1;o=c[g>>2]|0;g=c[h>>2]|0;if(!(d|o>>>0>e>>>0|g>>>0<e>>>0)){i=f;return}c[m>>2]=0;c[m+4>>2]=0;c[n>>2]=0;c[n+4>>2]=0;h=f+40|0;if(d){bo(h|0,16440,8)|0}else{va(h|0,8,16280,(p=i,i=i+8|0,c[p>>2]=e,p)|0)|0;i=p}va(m|0,8,16280,(p=i,i=i+8|0,c[p>>2]=o,p)|0)|0;i=p;do{if((o|0)==(g|0)){q=21144}else{if((g|0)==-1){q=16184;break}va(n|0,8,16280,(p=i,i=i+8|0,c[p>>2]=g,p)|0)|0;i=p;q=16088}}while(0);fm(b,j,k,l);g=a+112|0;c[(c[g>>2]|0)+16>>2]=c[b+8>>2];b=c[k>>2]|0;k=c[l>>2]|0;Ae(c[g>>2]|0,1,15864,(p=i,i=i+56|0,c[p>>2]=c[j>>2],c[p+8>>2]=b,c[p+16>>2]=k,c[p+24>>2]=h,c[p+32>>2]=m,c[p+40>>2]=q,c[p+48>>2]=n,p)|0);i=p;i=f;return}function Vl(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0;f=a+120|0;g=yh(c[f>>2]|0)|0;h=a+52|0;if((b[h>>1]|0)==0){return}i=a+20|0;j=a+64|0;a=0;a:while(1){k=c[(c[i>>2]|0)+(a<<2)>>2]|0;do{if((k|0)!=0){l=k+8|0;m=c[l>>2]|0;if((b[m+14>>1]&16)==0){break}qh(c[f>>2]|0,m,m)|0;if((g|0)==(yh(c[f>>2]|0)|0)){break}m=c[j>>2]|0;if((m|0)==0){c[j>>2]=Vd(8)|0;n=Me(4)|0;c[c[j>>2]>>2]=n;n=Pe(4)|0;c[(c[j>>2]|0)+4>>2]=n;o=c[j>>2]|0}else{o=m}Ne(c[o>>2]|0,a&65535);Qe(c[(c[j>>2]|0)+4>>2]|0,c[l>>2]|0);if((b[(oh(c[f>>2]|0,d)|0)+14>>1]&64)==0){p=10;break a}}}while(0);k=a+1|0;if((k|0)<(e[h>>1]|0)){a=k}else{p=10;break}}if((p|0)==10){return}}function Wl(a){a=a|0;var d=0,e=0,f=0,g=0,h=0,i=0;d=b[a+52>>1]|0;if(d<<16>>16==0){e=-1;return e|0}f=c[a+20>>2]|0;a=0;while(1){g=c[f+(a<<2)>>2]|0;if((g|0)!=0){h=g|0;if((c[h>>2]&2|0)==0&(c[h+4>>2]&0|0)==0){e=a;i=6;break}}h=a+1|0;if((h|0)<(d&65535|0)){a=h}else{e=-1;i=6;break}}if((i|0)==6){return e|0}return 0}function Xl(a){a=a|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0;d=c[a+40>>2]|0;Gk(a,0);f=a+52|0;g=b[f>>1]|0;if(g<<16>>16==0){h=0;i=a+16|0;j=c[i>>2]|0;k=j+(d<<1)|0;b[k>>1]=h;return}l=a+20|0;m=a+102|0;n=0;o=0;p=g;while(1){g=c[(c[l>>2]|0)+(n<<2)>>2]|0;do{if((g|0)==0){q=o;r=p}else{s=g|0;if((c[s>>2]&2|0)==0&(c[s+4>>2]&0|0)==0){q=o;r=p;break}if((c[g+32>>2]|0)!=(e[m>>1]|0|0)){q=o;r=p;break}Gk(a,n&65535);q=o+1|0;r=b[f>>1]|0}}while(0);g=n+1|0;if((g|0)<(r&65535|0)){n=g;o=q;p=r}else{break}}h=q&65535;i=a+16|0;j=c[i>>2]|0;k=j+(d<<1)|0;b[k>>1]=h;return}function Yl(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0;b=c[(c[a+96>>2]|0)+8>>2]|0;d=c[b+8>>2]|0;e=(c[d+8>>2]|0)-1|0;if((e|0)==0){return}f=c[a+132>>2]|0;g=c[f+84>>2]|0;h=c[d+4>>2]|0;d=c[(c[f+20>>2]|0)+24>>2]|0;if((d|0)==(b|0)){return}f=b+16|0;i=d;do{d=i|0;do{if(!((c[d>>2]&1024|0)==0&(c[d+4>>2]&0|0)==0)){j=c[i+12>>2]|0;if(j>>>0>=e>>>0){break}if((c[c[h+(j+1<<2)>>2]>>2]|0)==(g|0)){break}k=c[f>>2]&65535;wk(a,52,k,(Ml(a,i)|0)&65535,j&65535)}}while(0);i=c[i+44>>2]|0;}while((i|0)!=(b|0));return}function Zl(a){a=a|0;var d=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;d=a+32|0;f=c[d+4>>2]|0;g=a+92|0;h=c[(c[g>>2]|0)+36>>2]|0;i=0;j=a+24|0;k=c[j>>2]|0;if(f>>>0<i>>>0|f>>>0==i>>>0&(c[d>>2]|0)>>>0<h>>>0){i=Wd(k,h<<1)|0;c[j>>2]=i;j=(c[g>>2]|0)+36|0;c[d>>2]=c[j>>2];c[d+4>>2]=0;l=i;m=c[j>>2]|0}else{l=k;m=h}h=a+24|0;fo(l|0,-1|0,m<<1|0)|0;m=a+52|0;l=b[m>>1]|0;if(l<<16>>16==0){return}k=a+20|0;j=a+102|0;a=0;i=l;while(1){l=c[(c[k>>2]|0)+(a<<2)>>2]|0;do{if((l|0)==0){n=i}else{d=l|0;if((c[d>>2]&2|0)==0&(c[d+4>>2]&0|0)==0){n=i;break}if((c[l+32>>2]|0)!=(e[j>>1]|0|0)){n=i;break}b[(c[h>>2]|0)+(c[l+12>>2]<<1)>>1]=a;c[(c[k>>2]|0)+(a<<2)>>2]=0;n=b[m>>1]|0}}while(0);l=a+1|0;if((l|0)<(n&65535|0)){a=l;i=n}else{break}}return}function _l(a,d,f,g){a=a|0;d=d|0;f=f|0;g=g|0;var h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0,P=0,Q=0,R=0,S=0,T=0,U=0,V=0,W=0,X=0,Y=0,Z=0,_=0,$=0,aa=0,ba=0,ca=0,da=0,ea=0,fa=0,ga=0,ha=0,ia=0,ja=0,ka=0,la=0,ma=0,na=0;h=c[a+24>>2]|0;i=a|0;j=c[i>>2]|0;k=c[j+4>>2]|0;if((d|0)>(f|0)){l=j;m=l+4|0;c[m>>2]=k;return}n=a+16|0;o=a+40|0;p=0;q=0;r=0;s=0;t=0;u=g;g=d;d=j;j=k;while(1){v=b[(c[n>>2]|0)+(g<<1)>>1]|0;w=v&65535;if((k|0)!=(j|0)){x=g+u&65535;y=k;z=d;while(1){A=c[z>>2]|0;if((g|0)==(e[A+(y+1<<1)>>1]|0)){b[(c[n>>2]|0)+(e[A+(y<<1)>>1]<<1)>>1]=x;B=c[i>>2]|0}else{B=z}A=y+2|0;if((A|0)==(c[B+4>>2]|0)){break}else{y=A;z=B}}}z=c[2108+(w<<5)>>2]|0;a:do{if((z|0)<1){C=p;D=q;E=1;F=r;G=0;H=s;I=-1;J=t;K=-1;L=u}else{y=v<<16>>16==54;x=v<<16>>16==45;A=v<<16>>16!=54;M=p;N=q;O=1;P=r;Q=0;R=s;S=-1;T=t;U=-1;V=u;while(1){W=O+1|0;X=c[2104+(w<<5)+(W<<2)>>2]|0;b:do{if((X|0)==0){Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=b[(c[n>>2]|0)+(O+g+Q<<1)>>1]|0;da=N;ea=M}else{if((X&-17|0)==1){if(!y){fa=15}}else{if((X|0)==15&(N|0)==0&A){fa=15}}if((fa|0)==15){fa=0;ga=b[(c[n>>2]|0)+(O+g+Q<<1)>>1]|0;ha=b[h+((ga&65535)<<1)>>1]|0;if(ha<<16>>16==-1){Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=N;ea=M;break}wk(a,51,P,ha,ga);Y=V+4|0;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=N;ea=M;break}switch(X|0){case 9:{if((M|0)<=0){Y=V;Z=U;_=T;$=S;aa=R;ba=-1;ca=P;da=N;ea=M;break b}ga=O+g|0;ha=0;ia=V;while(1){ja=b[(c[n>>2]|0)+(ga+ha<<1)>>1]|0;ka=b[h+((ja&65535)<<1)>>1]|0;if(ka<<16>>16==-1){la=ia}else{wk(a,51,P,ka,ja);la=ia+4|0}ja=ha+1|0;if((ja|0)<(M|0)){ha=ja;ia=la}else{break}}Y=la;Z=U;_=T;$=S;aa=R;ba=M-1|0;ca=P;da=N;ea=M;break b;break};case 2:{if((b[(c[n>>2]|0)+(O+g+Q<<1)>>1]|0)==-1){Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=N;ea=M;break b}ia=Q+O|0;Y=V;Z=U;_=T;$=ia;aa=ia+1|0;ba=Q;ca=P;da=N;ea=M;break b;break};case 8:{Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=N;ea=e[(c[n>>2]|0)+(O+g+Q<<1)>>1]|0;break b;break};case 14:{Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=e[(c[n>>2]|0)+(O+g+Q<<1)>>1]|0;ea=M;break b;break};case 19:{ia=b[(c[n>>2]|0)+(O+g+Q<<1)>>1]|0;ha=ia&65535;ga=W+Q|0;Y=V;Z=U;_=T;$=((ia&65535)>>>1&65535)+ga|0;aa=ha+ga|0;ba=ha+Q|0;ca=P;da=N;ea=ha;break b;break};case 3:{C=M;D=N;E=O;F=P;G=Q;H=R;I=S;J=T;K=U;L=V;break a;break};case 18:{ha=Q+O|0;Y=V;Z=U;_=T;$=ha;aa=ha+M|0;ba=M-1+Q|0;ca=P;da=N;ea=M;break b;break};case 4:{if(x){Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=N;ea=M;break b}if((b[(c[n>>2]|0)+(O+g+Q<<1)>>1]|0)==0){Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=N;ea=M;break b}ha=Q+O|0;Y=V;Z=ha;_=ha+1|0;$=S;aa=R;ba=Q;ca=P;da=N;ea=M;break b;break};case 16:{ha=Q+O|0;Y=V;Z=ha;_=ha+M|0;$=S;aa=R;ba=M-1+Q|0;ca=P;da=N;ea=M;break b;break};default:{Y=V;Z=U;_=T;$=S;aa=R;ba=Q;ca=P;da=N;ea=M;break b}}}}while(0);if((O|0)<(z|0)){M=ea;N=da;O=W;P=ca;Q=ba;R=aa;S=$;T=_;U=Z;V=Y}else{C=ea;D=da;E=W;F=ca;G=ba;H=aa;I=$;J=_;K=Z;L=Y;break}}}}while(0);z=G+E|0;vk(a,z);w=c[n>>2]|0;Yn(w+(c[o>>2]<<1)|0,w+(g<<1)|0,z<<1)|0;if((K|0)!=-1&(K|0)<(J|0)){w=K;do{v=(c[o>>2]|0)+w|0;V=(c[n>>2]|0)+(v<<1)|0;U=e[V>>1]|0;if((U|0)>(g|0)){Ne(c[i>>2]|0,v&65535);Ne(c[i>>2]|0,b[(c[n>>2]|0)+((c[o>>2]|0)+w<<1)>>1]|0)}else{b[V>>1]=U+L}w=w+1|0;}while((w|0)<(J|0))}c[o>>2]=(c[o>>2]|0)+z;if((I|0)!=-1&(I|0)<(H|0)){w=I;U=L;while(1){V=b[(c[n>>2]|0)+(w+g<<1)>>1]|0;do{if(V<<16>>16==-1){ma=U}else{v=b[h+((V&65535)<<1)>>1]|0;if(v<<16>>16==-1){ma=U;break}wk(a,52,F,v,V);ma=U+4|0}}while(0);V=w+1|0;if((V|0)<(H|0)){w=V;U=ma}else{na=ma;break}}}else{na=L}U=z+g|0;w=c[i>>2]|0;if((U|0)>(f|0)){l=w;break}p=C;q=D;r=F;s=H;t=J;u=na;g=U;d=w;j=c[w+4>>2]|0}m=l+4|0;c[m>>2]=k;return}function $l(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0;g=c[d+12>>2]|0;h=c[(c[g+24>>2]|0)+4>>2]&65535;i=d+16|0;Ul(a,g,c[i>>2]|0,(((h|0)==8|(h|0)==18)&1)+f|0);if((h|0)==18|(h|0)==8){gm(a,d)}d=c[i>>2]|0;if((b[d+14>>1]&16)==0){return}if((h|0)==21|(h|0)==17|(h|0)==5){wh(c[a+120>>2]|0,c[(c[(c[a+132>>2]|0)+88>>2]|0)+76>>2]|0);return}h=c[c[d+4>>2]>>2]|0;if(!((h|0)!=0&(e|0)!=0)){return}d=c[e>>2]|0;i=c[h>>2]|0;if((b[d+52>>1]|0)==(b[i+52>>1]|0)){qh(c[a+120>>2]|0,h,e)|0;return}f=d|0;if((c[f>>2]&1024|0)==0&(c[f+4>>2]&0|0)==0){return}if((c[i+36>>2]|0)!=(d|0)){return}vh(c[a+120>>2]|0,h,e);return}function am(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0;g=hm(d,e[d+26>>1]|0)|0;if((b[(c[g>>2]|0)+52>>1]|0)==10){h=c[c[g+4>>2]>>2]|0}else{h=g}if((b[h+14>>1]&16)==0){i=h}else{g=c[a+120>>2]|0;i=nh(g,h,c[g+16>>2]|0)|0}g=Ol(a,f,i,d+28|0)|0;i=a+120|0;if((b[(c[h>>2]|0)+52>>1]|0)==12){j=th(c[i>>2]|0,h)|0}else{j=h}do{if((qh(c[i>>2]|0,h,g)|0)==0){if((Ok(a,j,f)|0)!=0){break}jm(a,d,g,h);return}}while(0);im(a,d,c[f>>2]|0);return}function bm(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0;d=c[(c[b+12>>2]|0)+24>>2]|0;e=c[d+4>>2]&65535;do{if((e|0)==8){if((c[d>>2]|0)!=0){f=1;break}km(a,b,c[d+24>>2]|0,0);f=1}else if((e|0)==18){f=1}else{f=0}}while(0);e=c[d+36>>2]|0;if((e|0)==0){return}else{g=f;h=e}while(1){if((c[h>>2]|0)==0){km(a,b,h,g)}e=c[h+36>>2]|0;if((e|0)==0){break}else{g=g+1|0;h=e}}return}function cm(a,d,f,g){a=a|0;d=d|0;f=f|0;g=g|0;var h=0,i=0;h=Ck(a,f)|0;f=d+26|0;i=(e[f>>1]|0)-(g&65535)|0;Bk(a,30,c[(c[d+12>>2]|0)+8>>2]&65535,i&65535);dm(a,b[f>>1]|0,g);Gk(a,c[h+12>>2]&65535);g=a+48|0;b[g>>1]=(e[g>>1]|0)-i;b[f>>1]=(e[f>>1]|0)-i;im(a,d,h);return}function dm(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0;f=b[a+48>>1]|0;g=d&65535;d=e&65535;e=g-d|0;vk(a,e);if((e|0)<=0){h=a+40|0;i=c[h>>2]|0;j=i+e|0;c[h>>2]=j;return}k=d-g+(f&65535)|0;f=a+8|0;g=a+40|0;d=a+16|0;a=0;while(1){b[(c[d>>2]|0)+((c[g>>2]|0)+a<<1)>>1]=c[(c[(c[f>>2]|0)+(k+a<<2)>>2]|0)+12>>2];l=a+1|0;if((l|0)<(e|0)){a=l}else{h=g;break}}i=c[h>>2]|0;j=i+e|0;c[h>>2]=j;return}function em(a,d,f){a=a|0;d=d|0;f=f|0;var g=0,h=0,i=0,j=0;g=a+8|0;h=(c[g>>2]|0)-1|0;c[d>>2]=h;c[f>>2]=h;h=e[a+14>>1]|0;if((h&32|0)==0){if((h&1|0)==0){return}c[f>>2]=-1;c[d>>2]=(c[d>>2]|0)-1;return}f=c[g>>2]|0;a:do{if(f>>>0>1>>>0){g=c[a+4>>2]|0;h=1;while(1){i=h+1|0;if((b[(c[c[g+(h<<2)>>2]>>2]|0)+52>>1]|0)==10){j=h;break a}if(i>>>0<f>>>0){h=i}else{j=i;break}}}else{j=1}}while(0);c[d>>2]=j-1;return}function fm(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0;c[b>>2]=21144;c[d>>2]=21144;f=c[a+4>>2]&65535;if((f|0)==14){g=a}else if((f|0)==22){g=c[a+24>>2]|0}else{g=c[a+24>>2]|0}a=c[g+20>>2]|0;g=c[a>>2]|0;if((g&2|0)!=0){f=c[a+40>>2]|0;if((f|0)!=0){c[b>>2]=c[f+8>>2];c[d>>2]=15720}c[e>>2]=c[a+20>>2];return}if((g&16|0)!=0){c[e>>2]=c[a+8>>2];return}if((g&32|0)==0){c[e>>2]=15504;return}else{c[b>>2]=c[(c[a+32>>2]|0)+8>>2];c[d>>2]=15616;c[e>>2]=c[a+20>>2];return}}



function gm(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0;e=c[(c[d+12>>2]|0)+24>>2]|0;do{if((c[e+4>>2]&65535|0)==18){f=c[(c[a+96>>2]|0)+52>>2]|0;g=c[f+8>>2]|0;im(a,d,f);h=g}else{g=c[e+24>>2]|0;im(a,d,c[g>>2]|0);f=c[(c[g>>2]|0)+8>>2]|0;g=c[f>>2]|0;if((c[g>>2]&4096|0)==0&(c[g+4>>2]&0|0)==0){h=f;break}b[d+28>>1]=1;h=f}}while(0);e=c[a+120>>2]|0;qh(e,hm(d,0)|0,h)|0;return}function hm(a,d){a=a|0;d=d|0;var f=0,g=0;f=d+1|0;if((e[a+24>>1]|0)<=(f|0)){g=c[a+20>>2]|0;return g|0}d=c[(c[(c[a+16>>2]|0)+4>>2]|0)+(f<<2)>>2]|0;if((b[(c[d>>2]|0)+52>>1]|0)!=10){g=d;return g|0}g=c[c[d+4>>2]>>2]|0;return g|0}function im(a,d,e){a=a|0;d=d|0;e=e|0;var f=0,g=0,h=0;f=a+48|0;g=b[f>>1]|0;if(g<<16>>16==(b[a+50>>1]|0)){lm(a);h=b[f>>1]|0}else{h=g}c[(c[a+8>>2]|0)+((h&65535)<<2)>>2]=e;b[f>>1]=(b[f>>1]|0)+1;f=d+26|0;b[f>>1]=(b[f>>1]|0)+1;return}function jm(a,b,d,f){a=a|0;b=b|0;d=d|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;g=i;i=i+24|0;h=g|0;j=g+8|0;k=g+16|0;l=b+12|0;fm(c[l>>2]|0,h,j,k);m=a+112|0;n=c[m>>2]|0;o=c[n+4>>2]|0;c[n+16>>2]=c[(c[l>>2]|0)+8>>2];l=a+120|0;xh(c[l>>2]|0);a=c[l>>2]|0;l=(e[b+26>>1]|0)+1|0;b=c[h>>2]|0;h=c[j>>2]|0;j=c[k>>2]|0;k=nh(a,f,c[a+16>>2]|0)|0;tn(o,15280,(o=i,i=i+48|0,c[o>>2]=l,c[o+8>>2]=b,c[o+16>>2]=h,c[o+24>>2]=j,c[o+32>>2]=k,c[o+40>>2]=d,o)|0);i=o;Be(c[m>>2]|0,1);i=g;return}function km(a,b,d,f){a=a|0;b=b|0;d=d|0;f=f|0;var g=0,h=0,i=0;g=e[a+48>>1]|0;h=e[b+26>>1]|0;i=c[a+120>>2]|0;Vk(a,d,oh(i,hm(b,f)|0)|0);c[(c[a+8>>2]|0)+(g+f-h<<2)>>2]=c[d>>2];return}function lm(a){a=a|0;var d=0,e=0;d=a+50|0;e=b[d>>1]<<1;b[d>>1]=e;d=a+8|0;c[d>>2]=Wd(c[d>>2]|0,(e&65535)<<2)|0;return}function mm(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;i=i+8|0;e=d|0;f=Vd(12)|0;g=_n(b|0)|0;c[f>>2]=1;c[f+8>>2]=b;c[f+4>>2]=g;g=c[(c[a+52>>2]|0)+24>>2]|0;c[e>>2]=f;f=eh(0,0,g,e)|0;i=d;return f|0}function nm(a,b){a=a|0;b=b|0;var c=0;c=Vd((_n(b|0)|0)+1|0)|0;Zn(c|0,b|0)|0;return mm(a,c)|0}function om(d,e){d=d|0;e=e|0;var f=0;f=Vd(72)|0;c[f+68>>2]=c[d+20>>2];b[f+20>>1]=0;c[f>>2]=0;c[f+64>>2]=e;c[f+8>>2]=Vd(128)|0;c[f+12>>2]=Vd(128)|0;e=f+4|0;c[e>>2]=0;c[f+52>>2]=0;c[f+32>>2]=0;c[f+36>>2]=0;d=Vd(256)|0;b[f+28>>1]=0;b[f+26>>1]=128;b[f+30>>1]=128;c[f+16>>2]=0;fo(d|0,29,256)|0;fo(d+97|0,14,26)|0;fo(d+65|0,14,26)|0;fo(d+48|0,16,10)|0;fo(d+194|0,14,51)|0;a[d+66|0]=28;a[d+95|0]=14;a[d+40|0]=0;a[d+41|0]=1;a[d+34|0]=15;a[d+64|0]=24;a[d+63|0]=27;a[d+35|0]=22;a[d+61|0]=20;a[d+46|0]=23;a[d+44|0]=2;a[d+43|0]=12;a[d+45|0]=13;a[d+123|0]=18;a[d+125|0]=3;a[d+60|0]=11;a[d+62|0]=10;a[d+58|0]=17;a[d+33|0]=6;a[d+42|0]=8;a[d+47|0]=9;a[d+38|0]=25;a[d+37|0]=7;a[d+124|0]=26;a[d+91|0]=4;a[d+93|0]=19;a[d+10|0]=21;c[f+44>>2]=50;c[e>>2]=d;return f|0}function pm(a){a=a|0;var b=0,d=0,e=0;b=c[a>>2]|0;if((b|0)!=0){d=b;while(1){b=c[d+48>>2]|0;if((b|0)==0){e=d;break}else{d=b}}while(1){if((c[e+40>>2]|0)!=0){qb[c[e+12>>2]&63](e)}d=c[e+52>>2]|0;Ln(c[e+20>>2]|0);Ln(c[e>>2]|0);Ln(e);if((d|0)==0){break}else{e=d}}}Ln(c[a+32>>2]|0);Ln(c[a+8>>2]|0);Ln(c[a+4>>2]|0);Ln(c[a+12>>2]|0);Ln(a);return}function qm(d){d=d|0;var e=0,f=0,g=0,h=0,i=0;e=d|0;f=c[e>>2]|0;qb[c[f+12>>2]&63](f);c[f+40>>2]=0;g=c[f+48>>2]|0;if((g|0)==0){c[d+16>>2]=0;return}f=d+8|0;Zn(c[f>>2]|0,c[g+20>>2]|0)|0;c[d+16>>2]=c[g+32>>2];h=b[g+24>>1]|0;b[d+28>>1]=h;b[d+24>>1]=b[g+28>>1]|0;c[e>>2]=g;c[d+52>>2]=c[g+16>>2];e=b[g+30>>1]|0;c[d+44>>2]=e&65535;if(e<<16>>16!=34){return}e=h&65535;h=c[f>>2]|0;f=e;while(1){g=f-1|0;if((a[5352+(a[h+g|0]|0)|0]|0)==0){i=f;break}if((g|0)==0){i=0;break}else{f=g}}f=d+12|0;d=e-i|0;bo(c[f>>2]|0,h+i|0,d|0)|0;a[(c[f>>2]|0)+d|0]=0;return}function rm(a){a=a|0;b[a+28>>1]=(b[a+20>>1]|0)+1;sm(a);return}function sm(f){f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0;g=i;i=i+32|0;h=g|0;j=g+8|0;k=g+16|0;l=g+24|0;m=f+28|0;n=e[m>>1]|0;c[h>>2]=n;o=c[f+4>>2]|0;p=f+8|0;q=f|0;r=n;a:while(1){s=c[p>>2]|0;n=s+r|0;t=n;while(1){u=a[t]|0;if(!((u<<24>>24|0)==32|(u<<24>>24|0)==9)){break}t=t+1|0}v=r+(t-n)|0;c[h>>2]=v;w=a[t]|0;x=a[o+(w&255)|0]|0;y=x<<24>>24;if(x<<24>>24==14){z=6;break}if(x<<24>>24<6){z=10;break}if((x<<24>>24|0)==15){z=21;break}else if((x<<24>>24|0)==28){z=22;break}else if((x<<24>>24|0)==21){u=c[q>>2]|0;if((pb[c[u+8>>2]&15](u)|0)!=1){z=14;break}c[h>>2]=0;r=0;continue}else if((x<<24>>24|0)!=22){z=24;break}do{if(w<<24>>24==35){if((a[t+1|0]|0)!=91){break}zm(f,h);r=c[h>>2]|0;continue a}}while(0);n=c[q>>2]|0;if((pb[c[n+8>>2]&15](n)|0)!=1){z=20;break}c[h>>2]=0;r=0}b:do{if((z|0)==10){c[h>>2]=v+1;c[j>>2]=y}else if((z|0)==14){c[j>>2]=52;c[h>>2]=0}else if((z|0)==20){c[j>>2]=52;c[h>>2]=0}else if((z|0)==21){Am(f,h,t,k);c[j>>2]=36}else if((z|0)==22){r=t+1|0;if((a[r]|0)!=34){z=6;break}c[h>>2]=v+1;Am(f,h,r,l);c[j>>2]=37}else if((z|0)==24){if(x<<24>>24<10){r=v+1|0;c[h>>2]=r;if((a[s+r|0]|0)==61){c[h>>2]=v+2;c[j>>2]=c[5824+(y-6<<2)>>2];break}else{c[j>>2]=c[5808+(y-6<<2)>>2];break}}switch(x<<24>>24){case 12:{r=a[t+1|0]|0;if((a[o+(r&255)|0]|0)==16){Bm(f,h,j,t);break b}if(r<<24>>24==61){c[h>>2]=v+2;c[j>>2]=15;break b}else{c[h>>2]=v+1;c[j>>2]=14;break b}break};case 18:{c[h>>2]=v+1;if((a[t+1|0]|0)==124){Cm(f,h);c[j>>2]=29;break b}else{c[j>>2]=28;break b}break};case 17:{c[h>>2]=v+1;if((a[t+1|0]|0)==58){c[h>>2]=v+2;c[j>>2]=42;break b}else{c[j>>2]=41;break b}break};case 25:{c[h>>2]=v+1;if((a[t+1|0]|0)==38){c[h>>2]=v+2;c[j>>2]=44;break b}else{c[j>>2]=43;break b}break};case 26:{c[h>>2]=v+1;r=a[t+1|0]|0;if((r<<24>>24|0)==124){c[h>>2]=v+2;c[j>>2]=46;break b}else if((r<<24>>24|0)==62){c[h>>2]=v+2;c[j>>2]=49;break b}else{c[j>>2]=45;break b}break};case 13:{r=a[t+1|0]|0;if((a[o+(r&255)|0]|0)==16){Bm(f,h,j,t);break b}if(r<<24>>24==61){c[h>>2]=v+2;c[j>>2]=17;break b}else{c[h>>2]=v+1;c[j>>2]=16;break b}break};case 16:{Bm(f,h,j,t);break b;break};case 23:{r=t+1|0;if((a[o+(d[r]|0)|0]|0)==16){Bm(f,h,j,t);break b}c[h>>2]=v+1;if((a[r]|0)!=46){c[j>>2]=40;break b}c[h>>2]=v+2;if((a[t+2|0]|0)==46){c[h>>2]=v+3;c[j>>2]=48;break b}else{Ae(c[f+64>>2]|0,1,16448,(A=i,i=i+1|0,i=i+7&-8,c[A>>2]=0,A)|0);i=A;break b}break};default:{if((x&-2)<<24>>24==10){r=x<<24>>24==10;c[h>>2]=v+1;p=r?22:18;c[j>>2]=p;n=a[t+1|0]|0;if(n<<24>>24==61){c[j>>2]=p|1;c[h>>2]=v+2;break b}if(n<<24>>24!=(a[t]|0)){if(!(n<<24>>24==91&(r^1))){break b}c[h>>2]=v+2;c[j>>2]=30;break b}c[h>>2]=v+2;if((a[t+2|0]|0)==61){c[h>>2]=v+3;c[j>>2]=p+3;break b}else{c[j>>2]=p+2;break b}}if((x<<24>>24|0)==20){p=v+1|0;c[h>>2]=p;r=a[s+p|0]|0;if((r<<24>>24|0)==61){c[j>>2]=27;c[h>>2]=v+2;break b}else if((r<<24>>24|0)==62){c[j>>2]=33;c[h>>2]=v+2;break b}else{c[j>>2]=26;break b}}else if((x<<24>>24|0)==19){c[h>>2]=v+1;if((a[t+1|0]|0)==62){c[h>>2]=v+2;c[j>>2]=31;break b}else{c[j>>2]=32;break b}}else if((x<<24>>24|0)==24){r=t+1|0;c[h>>2]=v+1;p=a[r]|0;if(p<<24>>24==40){c[h>>2]=v+2;c[j>>2]=47;break b}if((a[o+(p&255)|0]|0)!=14){c[j>>2]=50;break b}n=c[f+12>>2]|0;u=0;B=r;r=p;do{a[n+u|0]=r;u=u+1|0;B=B+1|0;r=a[B]|0;}while((a[5352+(r&255)|0]|0)!=0);c[h>>2]=(c[h>>2]|0)+u;a[n+u|0]=0;c[j>>2]=35;break b}else if((x<<24>>24|0)==27){c[h>>2]=v+1;if((a[t+1|0]|0)!=62){c[j>>2]=50;break b}if((c[f+48>>2]|0)==1){Ae(c[f+64>>2]|0,1,14960,(A=i,i=i+1|0,i=i+7&-8,c[A>>2]=0,A)|0);i=A}if((c[(c[q>>2]|0)+48>>2]|0)!=0){Ae(c[f+64>>2]|0,1,13600,(A=i,i=i+1|0,i=i+7&-8,c[A>>2]=0,A)|0);i=A}c[h>>2]=(c[h>>2]|0)+1;c[j>>2]=51;break b}else{c[j>>2]=50;break b}}}}}while(0);if((z|0)==6){z=c[f+12>>2]|0;A=t;t=0;q=w;do{a[z+t|0]=q;t=t+1|0;A=A+1|0;q=a[A]|0;}while((a[5352+(q&255)|0]|0)!=0);c[h>>2]=(c[h>>2]|0)+t;a[z+t|0]=0;c[j>>2]=34}b[m>>1]=c[h>>2];c[f+44>>2]=c[j>>2];i=g;return}function tm(a){a=a|0;var d=0,e=0,f=0,g=0,h=0;d=a+26|0;e=b[d>>1]|0;f=(e&65535)<<1;g=a+30|0;if((b[g>>1]|0)==e<<16>>16){e=a+12|0;c[e>>2]=Wd(c[e>>2]|0,f)|0;e=f&65535;b[g>>1]=e;h=e}else{h=f&65535}e=a+8|0;c[e>>2]=Wd(c[e>>2]|0,f)|0;b[d>>1]=h;return}function um(a,b){a=a|0;b=b|0;var c=0,d=0;c=Ea(b|0,12776)|0;if((c|0)==0){d=0;return d|0}Dm(a,1,c,b);d=1;return d|0}function vm(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Em(a,b)|0;c[f+40>>2]=e;c[f+8>>2]=6;c[f+12>>2]=30;Hm(a,f,d);return}function wm(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Em(a,b)|0;b=Vd((_n(e|0)|0)+1|0)|0;Zn(b|0,e|0)|0;c[f+40>>2]=b;c[f+44>>2]=b;c[f+8>>2]=6;c[f+12>>2]=8;Hm(a,f,d);return}function xm(d){d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;f=c[d+68>>2]|0;g=d+28|0;h=e[g>>1]|0;i=d+8|0;j=c[i>>2]|0;k=d+24|0;l=d+12|0;m=d+26|0;n=d|0;o=j+h|0;p=0;q=h;h=j;a:while(1){j=a[o]|0;r=q+1|0;do{if(j<<24>>24==60){if((q+5|0)>(e[k>>1]|0|0)){break}if((sa(h+r|0,12352,5)|0)==0){s=5;break a}}}while(0);a[(c[l>>2]|0)+p|0]=j;t=p+1|0;if((t|0)==((e[m>>1]|0)-1|0)){a[(c[l>>2]|0)+t|0]=0;Fn(f,c[l>>2]|0);u=0}else{u=t}if(j<<24>>24==10){t=c[n>>2]|0;if((pb[c[t+8>>2]&15](t)|0)==0){s=12;break}else{v=0}}else{v=r}t=c[i>>2]|0;o=t+v|0;p=u;q=v;h=t}if((s|0)==5){if((p|0)!=0){a[(c[l>>2]|0)+p|0]=0;Fn(f,c[l>>2]|0)}w=q+6&65535;b[g>>1]=w;return}else if((s|0)==12){if((u|0)!=0){a[(c[l>>2]|0)+u|0]=0;Fn(f,c[l>>2]|0)}c[d+44>>2]=52;w=0;b[g>>1]=w;return}}function ym(a){a=a|0;var b=0;if(a>>>0<53>>>0){b=c[488+(a<<2)>>2]|0}else{b=0}return b|0}function zm(b,d){b=b|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;e=i;f=c[d>>2]|0;g=b+8|0;h=c[b+16>>2]|0;j=b|0;k=b+64|0;b=(c[g>>2]|0)+(f+2)|0;l=f+3|0;a:while(1){f=a[b]|0;do{if((f<<24>>24|0)==93){if((a[b+1|0]|0)==35){break a}}else if((f<<24>>24|0)==10){m=c[j>>2]|0;if((pb[c[m+8>>2]&15](m)|0)==1){b=c[g>>2]|0;l=0;continue a}else{Ae(c[k>>2]|0,1,10544,(m=i,i=i+8|0,c[m>>2]=h,m)|0);i=m;break}}}while(0);b=b+1|0;l=l+1|0}c[d>>2]=l+1;i=e;return}function Am(d,f,g,h){d=d|0;f=f|0;g=g|0;h=h|0;var j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0;j=i;i=i+8|0;k=j|0;l=d+8|0;m=c[l>>2]|0;n=d+12|0;o=c[n>>2]|0;if((c[f>>2]|0)==0){p=0}else{p=(a[g-1|0]|0)==66|0}do{if((a[g+1|0]|0)==34){q=g+2|0;if((a[q]|0)!=34){r=6;break}c[h>>2]=1;s=q;t=c[d+16>>2]|0}else{r=6}}while(0);if((r|0)==6){c[h>>2]=0;s=g;t=0}g=d+30|0;q=(p|0)==0;p=d+64|0;u=d|0;v=s+1|0;s=o;o=0;w=m;a:while(1){b:do{if(q){m=v;x=o;while(1){y=e[g>>1]|0;if((x|0)>=(y|0)){z=y<<1;c[n>>2]=Wd(c[n>>2]|0,z)|0;b[g>>1]=z}z=a[m]|0;do{if((z<<24>>24|0)==34){y=c[h>>2]|0;if((y|0)==0){A=x;B=m;C=0;break a}D=m+1|0;if((a[D]|0)!=34){E=D;r=19;break}if((a[m+2|0]|0)==34){A=x;B=m;C=y;break a}else{E=D;r=19}}else if((z<<24>>24|0)==10){F=x;break b}else if((z<<24>>24|0)==92){c[k>>2]=2;D=Jm(d,m,k)|0;if(D<<24>>24<1){Ae(c[p>>2]|0,1,10896,(G=i,i=i+1|0,i=i+7&-8,c[G>>2]=0,G)|0);i=G}a[s+x|0]=D;H=m+(c[k>>2]|0)|0}else{E=m+1|0;r=19}}while(0);if((r|0)==19){r=0;a[s+x|0]=z;H=E}m=H;x=x+1|0}}else{x=v;m=o;while(1){D=e[g>>1]|0;if((m|0)>=(D|0)){y=D<<1;c[n>>2]=Wd(c[n>>2]|0,y)|0;b[g>>1]=y}y=a[x]|0;do{if((y<<24>>24|0)==92){c[k>>2]=2;a[s+m|0]=Jm(d,x,k)|0;I=x+(c[k>>2]|0)|0}else if((y<<24>>24|0)==34){D=c[h>>2]|0;if((D|0)==0){A=m;B=x;C=0;break a}J=x+1|0;if((a[J]|0)!=34){K=J;r=36;break}if((a[x+2|0]|0)==34){A=m;B=x;C=D;break a}else{K=J;r=36}}else if((y<<24>>24|0)==10){F=m;break b}else{K=x+1|0;r=36}}while(0);if((r|0)==36){r=0;a[s+m|0]=y;I=K}x=I;m=m+1|0}}}while(0);do{if((c[h>>2]|0)==0){Ae(c[p>>2]|0,1,10792,(G=i,i=i+1|0,i=i+7&-8,c[G>>2]=0,G)|0);i=G}else{m=c[u>>2]|0;if((pb[c[m+8>>2]&15](m)|0)!=0){break}Ae(c[p>>2]|0,1,10672,(G=i,i=i+8|0,c[G>>2]=t,G)|0);i=G}}while(0);m=c[n>>2]|0;x=c[l>>2]|0;a[m+F|0]=a[x]|0;v=x;s=m;o=F+1|0;w=x}F=(C|0)==0?B+1|0:B+3|0;if(q){a[s+A|0]=0;c[f>>2]=F-w;c[d+52>>2]=Xh(c[d+56>>2]|0,s)|0;i=j;return}else{c[f>>2]=F-w;c[d+52>>2]=Yh(c[d+56>>2]|0,s,A)|0;i=j;return}}function Bm(d,e,f,g){d=d|0;e=e|0;f=f|0;g=g|0;var h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0.0,u=0,v=0,w=0,x=0,y=0;h=i;i=i+16|0;j=h|0;k=h+8|0;l=c[e>>2]|0;c[k>>2]=l;b[d+20>>1]=l;c[j>>2]=1;m=a[g]|0;if((m<<24>>24|0)==43){n=l+1|0;c[k>>2]=n;o=g+1|0;p=0;q=n}else if((m<<24>>24|0)==45){m=l+1|0;c[k>>2]=m;o=g+1|0;p=1;q=m}else{o=g;p=0;q=l}do{if((a[o]|0)==48){c[k>>2]=q+1;g=o+1|0;m=a[g]|0;if((m<<24>>24|0)==98){n=Km(k,g)|0;r=G;s=n;break}else if((m<<24>>24|0)==120){n=Mm(k,g)|0;r=G;s=n;break}else if((m<<24>>24|0)==99){m=Lm(k,g)|0;r=G;s=m;break}else{m=Nm(d,k,j,g)|0;r=G;s=m;break}}else{m=Nm(d,k,j,o)|0;r=G;s=m}}while(0);if((c[j>>2]|0)==0){j=c[k>>2]|0;o=j-l|0;q=d+12|0;bo(c[q>>2]|0,(c[d+8>>2]|0)+l|0,o|0)|0;a[(c[q>>2]|0)+o|0]=0;o=gb()|0;c[o>>2]=0;t=+Un(c[q>>2]|0,0);if((c[o>>2]|0)==34){Ae(c[d+64>>2]|0,1,11248,(u=i,i=i+1|0,i=i+7&-8,c[u>>2]=0,u)|0);i=u}c[d+52>>2]=Wh(c[d+56>>2]|0,t)|0;v=39;w=j;c[f>>2]=v;c[e>>2]=w;i=h;return}do{if((p|0)==0){j=-1;if((r|0)>(j|0)|(r|0)==(j|0)&s>>>0>-1>>>0){x=r;y=s;break}Ae(c[d+64>>2]|0,1,11408,(u=i,i=i+1|0,i=i+7&-8,c[u>>2]=0,u)|0);i=u;x=0;y=0}else{j=-2147483648;if(r>>>0<j>>>0|r>>>0==j>>>0&s>>>0<1>>>0){j=jo(0,0,s,r)|0;x=G;y=j;break}else{Ae(c[d+64>>2]|0,1,11408,(u=i,i=i+1|0,i=i+7&-8,c[u>>2]=0,u)|0);i=u;x=0;y=0;break}}}while(0);c[d+52>>2]=Vh(c[d+56>>2]|0,y,x)|0;v=38;w=c[k>>2]|0;c[f>>2]=v;c[e>>2]=w;i=h;return}function Cm(b,d){b=b|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0;e=i;i=i+16|0;f=e|0;g=e+8|0;h=b+8|0;j=c[h>>2]|0;k=b+16|0;l=b+40|0;c[l>>2]=c[k>>2];m=b+32|0;n=c[m>>2]|0;if((n|0)==0){o=Vd(64)|0;c[m>>2]=o;c[b+36>>2]=64;p=o;q=62}else{p=n;q=(c[b+36>>2]|0)-2|0}n=c[d>>2]|0;o=b+36|0;c[f>>2]=n;r=c[b>>2]|0;s=b+12|0;t=r+8|0;u=b+64|0;v=p;p=j+n|0;n=0;j=q;q=1;while(1){w=v;x=p;y=n;z=j;a:while(1){if((y|0)==(z|0)){Om(b,c[o>>2]<<1);A=c[m>>2]|0;B=(c[o>>2]|0)-2|0}else{A=w;B=z}C=a[x]|0;switch(C<<24>>24){case 35:{D=9;break};case 123:{D=19;break a;break};case 125:{D=20;break a;break};case 10:{break};case 34:{Am(b,f,x,g);E=(c[h>>2]|0)+(c[f>>2]|0)|0;F=(c[g>>2]|0)!=0?11800:11720;G=_n(c[s>>2]|0)|0;Om(b,G+7|0);H=c[m>>2]|0;I=G+1+((_n(F|0)|0)<<1)|0;J=c[s>>2]|0;va(H+y|0,I|0,11592,(K=i,i=i+24|0,c[K>>2]=F,c[K+8>>2]=J,c[K+16>>2]=F,K)|0)|0;i=K;w=H;x=E;y=G+y+((_n(F|0)|0)<<1)|0;z=B;continue a;break};default:{L=q;M=C;break a}}do{if((D|0)==9){D=0;F=a[x+1|0]|0;if(F<<24>>24==35){L=q;M=35;break a}if((a[x+2|0]|0)!=35){break}if(F<<24>>24!=91){L=q;M=35;break a}F=c[k>>2]|0;zm(b,f);G=c[k>>2]|0;if((G|0)==(F|0)){N=A;O=y}else{E=G-F|0;F=E+y|0;Om(b,F);G=c[m>>2]|0;fo(G+y|0,10,E|0)|0;N=G;O=F}w=N;x=(c[h>>2]|0)+(c[f>>2]|0)|0;y=O;z=B;continue a}}while(0);if((pb[c[t>>2]&15](r)|0)==0){Ae(c[u>>2]|0,1,11888,(K=i,i=i+8|0,c[K>>2]=c[l>>2],K)|0);i=K}c[f>>2]=0;a[A+y|0]=10;w=A;x=c[h>>2]|0;y=y+1|0;z=B}if((D|0)==19){D=0;L=q+1|0;M=123}else if((D|0)==20){D=0;if((q|0)==1){break}L=q-1|0;M=C}a[A+y|0]=M;c[f>>2]=(c[f>>2]|0)+1;v=A;p=x+1|0;n=y+1|0;j=B;q=L}a[A+y|0]=125;a[A+(y+1)|0]=0;c[d>>2]=(c[f>>2]|0)+1;i=e;return}function Dm(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0;f=Em(a,e)|0;c[f+8>>2]=2;c[f+12>>2]=16;c[f+40>>2]=d;Hm(a,f,b);return}function Em(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0;f=a|0;g=c[f>>2]|0;do{if((g|0)==0){h=4}else{if((c[g+40>>2]|0)==0){i=g;break}if((c[g+52>>2]|0)==0){h=4;break}i=c[g+52>>2]|0}}while(0);if((h|0)==4){g=Vd(56)|0;j=g;k=c[f>>2]|0;if((k|0)==0){c[f>>2]=j;c[g+48>>2]=0}else{c[k+52>>2]=j;c[g+48>>2]=c[f>>2]}c[g+40>>2]=0;c[g+44>>2]=0;c[g>>2]=0;c[g+52>>2]=0;fo(g+20|0,0,10)|0;c[g+4>>2]=a;i=j}j=i|0;g=c[j>>2]|0;if((g|0)==0){h=11}else{if((nb(g|0,d|0)|0)!=0){h=11}}if((h|0)==11){h=Wd(g,(_n(d|0)|0)+1|0)|0;c[j>>2]=h;Zn(h|0,d|0)|0}d=c[i+48>>2]|0;if((d|0)==0){l=a+28|0;b[l>>1]=0;c[f>>2]=i;return i|0}h=d+20|0;j=c[h>>2]|0;do{if((j|0)==0){m=Vd(e[a+26>>1]|0)|0;n=d+26|0}else{g=d+26|0;k=b[a+26>>1]|0;if((e[g>>1]|0)>>>0>=(k&65535)>>>0){m=j;n=g;break}m=Wd(j,k&65535)|0;n=g}}while(0);Zn(m|0,c[a+8>>2]|0)|0;c[h>>2]=m;m=a+16|0;c[d+32>>2]=c[m>>2];h=a+28|0;b[d+24>>1]=b[h>>1]|0;b[n>>1]=b[a+26>>1]|0;b[d+28>>1]=b[a+24>>1]|0;b[d+30>>1]=c[a+44>>2];c[d+16>>2]=c[a+52>>2];c[m>>2]=0;l=h;b[l>>1]=0;c[f>>2]=i;return i|0}function Fm(d){d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;f=i;g=c[d+4>>2]|0;h=g+8|0;j=d+40|0;d=g+26|0;k=c[j>>2]|0;l=c[h>>2]|0;m=e[d>>1]|0;n=0;o=0;while(1){if((o+2|0)==(m|0)){tm(g);p=c[h>>2]|0;q=e[d>>1]|0}else{p=l;q=m}r=a[k]|0;s=p+o|0;if(r<<24>>24==0){t=5;break}a[s]=r;r=a[k]|0;if((r<<24>>24|0)==13|(r<<24>>24|0)==10){t=7;break}k=k+1|0;l=p;m=q;n=r<<24>>24<0?1:n;o=o+1|0}if((t|0)==5){a[s]=10;a[(c[h>>2]|0)+o|0]=10;q=o+1|0;a[(c[h>>2]|0)+q|0]=0;b[g+24>>1]=q;q=(o|0)!=0|0;h=g+16|0;c[h>>2]=(c[h>>2]|0)+q;u=k;v=q}else if((t|0)==7){t=g+24|0;b[t>>1]=o;q=g+16|0;c[q>>2]=(c[q>>2]|0)+1;do{if((a[k]|0)==13){a[s]=10;q=k+1|0;if((a[q]|0)!=10){w=q;break}b[t>>1]=(b[t>>1]|0)+1;w=k+2|0}else{w=k+1|0}}while(0);a[p+(o+1)|0]=0;u=w;v=1}if((n|0)==0){c[j>>2]=u;i=f;return v|0}if((Ii(p)|0)!=0){c[j>>2]=u;i=f;return v|0}Ae(c[g+64>>2]|0,0,10184,(p=i,i=i+8|0,c[p>>2]=c[g+16>>2],p)|0);i=p;c[j>>2]=u;i=f;return v|0}function Gm(a){a=a|0;return}function Hm(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;e=i;if((c[b+48>>2]|0)!=0){pb[c[b+8>>2]&15](b)|0;i=e;return}c[a+48>>2]=d;pb[c[b+8>>2]&15](b)|0;if((d|0)!=0){i=e;return}if((sa(c[a+8>>2]|0,10440,5)|0)!=0){Ae(c[a+64>>2]|0,0,10296,(d=i,i=i+1|0,i=i+7&-8,c[d>>2]=0,d)|0);i=d}xm(a);i=e;return}function Im(a){a=a|0;Ln(c[a+44>>2]|0);return}function Jm(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;f=i;g=d+1|0;d=a[g]|0;a:do{switch(d<<24>>24){case 34:{h=d;break};case 98:{h=8;break};case 92:{h=d;break};case 116:{h=9;break};case 97:{h=7;break};case 114:{h=13;break};case 39:{h=d;break};case 110:{h=10;break};default:{if((d-48&255)>>>0<10>>>0){j=g;k=0;l=0;m=d}else{Ae(c[b+64>>2]|0,1,10896,(n=i,i=i+1|0,i=i+7&-8,c[n>>2]=0,n)|0);i=n;h=0;break a}while(1){n=l&255;if((m-48&255)>>>0>9>>>0){o=k;p=n;break}q=(l*10|0)-48+(m<<24>>24)|0;if((q|0)>255){o=k;p=n;break}n=k+1|0;r=j+1|0;if((n|0)>=3){o=n;p=q&255;break}j=r;k=n;l=q;m=a[r]|0}c[e>>2]=o-1+(c[e>>2]|0);h=p}}}while(0);i=f;return h|0}function Km(b,d){b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;e=c[b>>2]|0;f=d;do{e=e+1|0;f=f+1|0;g=a[f]|0;}while(g<<24>>24==48);if((g&-2)<<24>>24==48){h=0;i=0;j=0;k=f;l=e;m=g}else{n=0;o=0;p=e;c[b>>2]=p;return(G=n,o)|0}while(1){e=j+1|0;g=io(i<<1|0>>>31,h<<1|i>>>31,-48,-1)|0;f=io(g,G,m<<24>>24,m<<24>>24<0|0?-1:0)|0;g=G;d=k+1|0;q=l+1|0;r=a[d]|0;if((r&-2)<<24>>24!=48|(e|0)==65){n=g;o=f;p=q;break}else{h=g;i=f;j=e;k=d;l=q;m=r}}c[b>>2]=p;return(G=n,o)|0}function Lm(b,d){b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;e=c[b>>2]|0;f=d;do{e=e+1|0;f=f+1|0;g=a[f]|0;}while(g<<24>>24==48);if((g&-8)<<24>>24==48){h=0;i=0;j=0;k=f;l=e;m=g}else{n=0;o=0;p=e;c[b>>2]=p;return(G=n,o)|0}while(1){e=j+1|0;g=io(i<<3|0>>>29,h<<3|i>>>29,-48,-1)|0;f=io(g,G,m<<24>>24,m<<24>>24<0|0?-1:0)|0;g=G;d=l+1|0;q=k+1|0;r=a[q]|0;if((r&-8)<<24>>24!=48|(e|0)==23){n=g;o=f;p=d;break}else{h=g;i=f;j=e;k=q;l=d;m=r}}c[b>>2]=p;return(G=n,o)|0}function Mm(b,d){b=b|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0;e=c[b>>2]|0;f=d;while(1){d=e+1|0;g=f+1|0;h=a[g]|0;if(h<<24>>24==48){e=d;f=g}else{i=0;j=0;k=1;l=g;m=d;n=h;break}}a:while(1){do{if((n-48&255)>>>0<10>>>0){o=0;p=48}else{if((n-97&255)>>>0<6>>>0){o=0;p=87;break}if((n-65&255)>>>0<6>>>0){o=0;p=55}else{q=i;r=j;s=m;t=8;break a}}}while(0);f=jo(j<<4|0>>>28,i<<4|j>>>28,p,o)|0;e=io(f,G,n<<24>>24,n<<24>>24<0|0?-1:0)|0;f=G;h=m+1|0;d=l+1|0;if((k|0)==17){q=f;r=e;s=h;t=8;break}i=f;j=e;k=k+1|0;l=d;m=h;n=a[d]|0}if((t|0)==8){c[b>>2]=s;return(G=q,r)|0}return 0}function Nm(b,d,e,f){b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0;g=i;i=i+8|0;h=g|0;j=c[d>>2]|0;c[h>>2]=j;k=a[f]|0;if(k<<24>>24==48){l=f;m=j;while(1){n=m+1|0;c[h>>2]=n;o=l+1|0;p=a[o]|0;if(p<<24>>24==48){l=o;m=n}else{q=0;r=0;s=0;t=o;u=0;v=p;w=n;break}}}else{q=0;r=0;s=0;t=f;u=0;v=k;w=j}a:while(1){do{if((v-48&255)>>>0<10>>>0){if((c[e>>2]|0)==0){x=u;y=s;z=q;A=r;break}j=so(r,q,10,0)|0;k=io(j,G,-48,-1)|0;j=io(k,G,v<<24>>24,v<<24>>24<0|0?-1:0)|0;x=u;y=s+1|0;z=G;A=j}else{if((v<<24>>24|0)==101){B=10;break a}else if((v<<24>>24|0)!=46){C=q;D=r;E=w;break a}if((u|0)==1){C=q;D=r;E=w;break a}if(((a[t+1|0]|0)-48|0)>>>0>=10>>>0){C=q;D=r;E=w;break a}c[e>>2]=0;x=1;y=s;z=q;A=r}}while(0);j=y+1|0;k=w+1|0;c[h>>2]=k;f=t+1|0;if((j|0)==21){C=z;D=A;E=k;break}q=z;r=A;s=j;t=f;u=x;v=a[f]|0;w=k}if((B|0)==10){c[e>>2]=0;Rm(b,h,t);C=q;D=r;E=c[h>>2]|0}c[d>>2]=E;i=g;return(G=C,D)|0}function Om(a,b){a=a|0;b=b|0;var d=0,e=0;d=a+36|0;e=c[d>>2]|0;while(1){if((e|0)<(b|0)){e=e<<1}else{break}}b=a+32|0;c[b>>2]=Wd(c[b>>2]|0,e)|0;c[d>>2]=e;return}function Pm(d){d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0;f=i;g=c[d+4>>2]|0;h=g+8|0;j=c[d+40>>2]|0;d=g+26|0;k=0;l=e[d>>1]|0;m=c[h>>2]|0;n=0;while(1){o=wa(j|0)|0;if((n+2|0)==(l|0)){tm(g);p=e[d>>1]|0;q=c[h>>2]|0}else{p=l;q=m}if((o|0)==-1){r=5;break}s=q+n|0;a[s]=o;t=(o|0)==13;if((o|0)==13|(o|0)==10){r=7;break}k=(o&128)>>>0>127>>>0?1:k;l=p;m=q;n=n+1|0}if((r|0)==5){a[(c[h>>2]|0)+n|0]=10;m=n+1|0;a[(c[h>>2]|0)+m|0]=0;b[g+24>>1]=m;m=(n|0)!=0|0;h=g+16|0;c[h>>2]=(c[h>>2]|0)+m;u=m}else if((r|0)==7){b[g+24>>1]=n;r=g+16|0;c[r>>2]=(c[r>>2]|0)+1;do{if(t){a[s]=10;r=wa(j|0)|0;if((r|0)==10){break}Za(r|0,j|0)|0}}while(0);a[q+(n+1)|0]=0;u=1}if((k|0)==0){i=f;return u|0}if((Ii(q)|0)!=0){i=f;return u|0}Ae(c[g+64>>2]|0,0,10184,(q=i,i=i+8|0,c[q>>2]=c[g+16>>2],q)|0);i=q;i=f;return u|0}function Qm(a){a=a|0;xa(c[a+40>>2]|0)|0;return}function Rm(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;f=i;g=c[d>>2]|0;h=e+1|0;j=a[h]|0;if(j<<24>>24==43){k=3}else{if(j<<24>>24==45){k=3}else{l=g+1|0;m=h;n=j}}if((k|0)==3){k=e+2|0;l=g+2|0;m=k;n=a[k]|0}if((n-48&255)>>>0>9>>>0){Ae(c[b+64>>2]|0,1,11120,(o=i,i=i+1|0,i=i+7&-8,c[o>>2]=0,o)|0);i=o;p=a[m]|0}else{p=n}if((p-48&255)>>>0>=10>>>0){q=l;c[d>>2]=q;i=f;return}p=b+64|0;b=m;m=l;l=1;while(1){if((l|0)>3){Ae(c[p>>2]|0,1,11008,(o=i,i=i+1|0,i=i+7&-8,c[o>>2]=0,o)|0);i=o}n=m+1|0;k=b+1|0;if(((a[k]|0)-48&255)>>>0<10>>>0){b=k;m=n;l=l+1|0}else{q=n;break}}c[d>>2]=q;i=f;return}function Sm(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;d=i;i=i+32|0;e=d|0;f=d+8|0;g=c[a+68>>2]|0;h=c[a+52>>2]|0;a=b+8|0;j=c[c[(c[a>>2]|0)+4>>2]>>2]|0;k=Xd()|0;l=g+8|0;m=Vd(c[l>>2]<<2|0>>>30)|0;c[k+12>>2]=m;c[k+16>>2]=c[l>>2];if((c[l>>2]|0)==0&(c[l+4>>2]|0)==0){n=c[a>>2]|0;o=f+8|0;c[o>>2]=n;p=f|0;c[p>>2]=0;q=f+16|0;r=q;c[r>>2]=k;di(h,b,f);i=d;return}s=e|0;t=0;do{c[s>>2]=0;c[s+4>>2]=0;c[m+(t<<2)>>2]=eh(1048576,0,j,e)|0;t=t+1|0;u=(t|0)<0|0?-1:0;v=c[l>>2]|0;w=c[l+4>>2]|0;}while(u>>>0<w>>>0|u>>>0==w>>>0&t>>>0<v>>>0);if((v|0)==0&(w|0)==0){n=c[a>>2]|0;o=f+8|0;c[o>>2]=n;p=f|0;c[p>>2]=0;q=f+16|0;r=q;c[r>>2]=k;di(h,b,f);i=d;return}w=g+16|0;g=0;do{v=Vd(12)|0;t=Vd((_n(c[(c[w>>2]|0)+(g<<2)>>2]|0)|0)+1|0)|0;Zn(t|0,c[(c[w>>2]|0)+(g<<2)>>2]|0)|0;c[v+4>>2]=_n(c[(c[w>>2]|0)+(g<<2)>>2]|0)|0;c[v>>2]=1;c[v+8>>2]=t;t=m+(g<<2)|0;c[c[t>>2]>>2]=0;c[(c[t>>2]|0)+16>>2]=v;g=g+1|0;v=(g|0)<0|0?-1:0;t=c[l+4>>2]|0;}while(v>>>0<t>>>0|v>>>0==t>>>0&g>>>0<(c[l>>2]|0)>>>0);n=c[a>>2]|0;o=f+8|0;c[o>>2]=n;p=f|0;c[p>>2]=0;q=f+16|0;r=q;c[r>>2]=k;di(h,b,f);i=d;return}function Tm(a,b){a=a|0;b=b|0;We(a,11744,7808,70);return}function Um(a){a=a|0;var b=0,d=0,e=0;b=ab(a|0,1)|0;if((b|0)==0){d=0;return d|0}a=ua(b|0,11688)|0;if((a|0)==0){_a(b|0)|0;d=0;return d|0}else{e=Vd(8)|0;c[e>>2]=b;c[e+4>>2]=a;d=e;return d|0}return 0}function Vm(a){a=a|0;_a(c[a>>2]|0)|0;Ln(a);return}function Wm(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;d=i;i=i+32|0;e=d|0;c[e+20>>2]=0;c[e>>2]=c[(c[a+112>>2]|0)+32>>2];c[e+4>>2]=c[c[a+8>>2]>>2];f=c[(c[a+116>>2]|0)+4>>2]|0;c[e+8>>2]=f;c[e+12>>2]=a;g=c[a+120>>2]|0;c[e+16>>2]=g;qn(f);Fn(g,11984);g=b+16|0;Xm(e,c[b+8>>2]|0,c[g>>2]|0,c[g+4>>2]|0);i=d;return}function Xm(a,d,e,f){a=a|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0;g=i;h=d|0;j=c[h>>2]|0;k=b[j+52>>1]|0;a:do{if((k&65535)>>>0<5>>>0){l=d;m=f;n=e}else{o=a+8|0;p=d;q=h;r=j;s=k;t=e;while(1){if((s-7&65535)>>>0<3>>>0|(s&65535)>>>0>13>>>0){u=5;break}if((s<<16>>16|0)==5){u=14;break}else if((s<<16>>16|0)!=6){v=r|0;if((c[v>>2]&1024|0)==0&(c[v+4>>2]&0|0)==0){u=18;break}}v=c[t+12>>2]|0;tn(c[o>>2]|0,11584,(w=i,i=i+8|0,c[w>>2]=p,w)|0);i=w;Zm(a);x=c[v+8>>2]|0;y=v+16|0;v=c[y>>2]|0;z=x|0;A=c[z>>2]|0;B=b[A+52>>1]|0;if((B&65535)>>>0<5>>>0){l=x;m=c[y+4>>2]|0;n=v;break a}else{p=x;q=z;r=A;s=B;t=v}}if((u|0)==5){tn(c[o>>2]|0,11792,(w=i,i=i+8|0,c[w>>2]=p,w)|0);i=w;Zm(a);r=a+20|0;c[r>>2]=(c[r>>2]|0)+1;do{if(s<<16>>16==8){_m(a,t)}else{if((s&65535)>>>0<14>>>0){$m(a,t);break}v=c[q>>2]|0;if((c[v>>2]&1024|0)==0&(c[v+4>>2]&0|0)==0){an(a,p,t);break}else{$m(a,t);break}}}while(0);c[r>>2]=(c[r>>2]|0)-1;i=g;return}else if((u|0)==14){q=t;s=c[q+16>>2]|0;tn(c[o>>2]|0,11712,(w=i,i=i+16|0,c[w>>2]=p,c[w+8>>2]=s,w)|0);i=w;Zm(a);if((c[q+24>>2]|0)!=0){i=g;return}s=a+4|0;v=c[s>>2]|0;c[s>>2]=q;bn(a);c[s>>2]=v;i=g;return}else if((u|0)==18){i=g;return}}}while(0);Ym(a,l,n,m);Fn(c[a+16>>2]|0,11880);i=g;return}function Ym(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0;f=i;i=i+24|0;g=f|0;c[g>>2]=0;c[g+4>>2]=0;c[g+8>>2]=b;b=g+16|0;c[b>>2]=d;c[b+4>>2]=e;pn(c[a+8>>2]|0,g);Zm(a);i=f;return}function Zm(a){a=a|0;var b=0;b=a+8|0;Fn(c[a+16>>2]|0,c[c[b>>2]>>2]|0);qn(c[b>>2]|0);return}function _m(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0;d=i;e=c[a+8>>2]|0;f=c[a+20>>2]|0;g=b+12|0;if((c[g>>2]|0)!=0){tn(e,8888,(h=i,i=i+8|0,c[h>>2]=f-1,h)|0);i=h;Zm(a);i=d;return}c[g>>2]=1;j=b+20|0;b=c[j>>2]|0;a:do{if((b|0)!=0){k=f-1|0;l=b;m=b;while(1){if((l|0)!=(m|0)){tn(e,11240,(h=i,i=i+8|0,c[h>>2]=k,h)|0);i=h}n=c[l+8>>2]|0;tn(e,8200,(h=i,i=i+16|0,c[h>>2]=k,c[h+8>>2]=n,h)|0);i=h;Zm(a);n=c[l+12>>2]|0;o=n+16|0;Xm(a,c[n+8>>2]|0,c[o>>2]|0,c[o+4>>2]|0);o=c[l+16>>2]|0;if((o|0)==0){break a}l=o;m=c[j>>2]|0}}}while(0);c[g>>2]=0;i=d;return}function $m(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;d=i;e=c[a+8>>2]|0;f=c[a+20>>2]|0;g=b+20|0;if((c[g>>2]|0)!=0){tn(e,8888,(h=i,i=i+8|0,c[h>>2]=f-1,h)|0);i=h;Zm(a);i=d;return}c[g>>2]=1;j=b+16|0;if((c[j>>2]|0)!=0){k=f-1|0;f=b+12|0;b=0;do{if((b|0)!=0){tn(e,11240,(h=i,i=i+8|0,c[h>>2]=k,h)|0);i=h}tn(e,8264,(h=i,i=i+16|0,c[h>>2]=k,c[h+8>>2]=b,h)|0);i=h;Zm(a);l=c[(c[f>>2]|0)+(b<<2)>>2]|0;m=l+16|0;Xm(a,c[l+8>>2]|0,c[m>>2]|0,c[m+4>>2]|0);b=b+1|0;}while(b>>>0<(c[j>>2]|0)>>>0)}c[g>>2]=0;i=d;return}function an(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0;e=i;f=d+20|0;if((c[f>>2]|0)==0){c[f>>2]=1;cn(a,c[b>>2]|0,d,0);c[f>>2]=0;i=e;return}else{tn(c[a+8>>2]|0,8888,(f=i,i=i+8|0,c[f>>2]=(c[a+20>>2]|0)-1,f)|0);i=f;Zm(a);i=e;return}}function bn(d){d=d|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0,P=0,Q=0,R=0,S=0,T=0,U=0;f=i;i=i+8|0;g=f|0;h=c[d+8>>2]|0;j=c[d+16>>2]|0;k=d+4|0;l=c[k>>2]|0;m=c[l+28>>2]|0;n=c[l+32>>2]|0;do{if((n|0)==0){l=g|0;a[l]=37;o=0;p=l;q=6}else{l=n;r=0;while(1){s=r+1|0;if((l+9|0)>>>0<19>>>0){break}else{l=(l|0)/10|0;r=s}}l=g|0;a[l]=37;if((r|0)<=8){o=s;p=l;q=6;break}a[g+1|0]=((s|0)/10|0)+48;a[g+2|0]=((s|0)%10|0)+48;a[g+3|0]=100;a[g+4|0]=0;t=l}}while(0);if((q|0)==6){a[g+1|0]=o+48;a[g+2|0]=100;a[g+3|0]=0;t=p}p=d+20|0;g=c[p>>2]|0;o=g+1|0;c[p>>2]=o;if((n|0)<=0){i=f;return}p=d+12|0;q=-1;s=0;while(1){l=b[m+(s<<1)>>1]|0;u=l&65535;v=c[3960+(u<<2)>>2]|0;w=c[2108+(u<<5)>>2]|0;do{if((w|0)==0){x=m+(s+1<<1)|0;y=e[x>>1]|0;if((y|0)==(q|0)){z=q;break}tn(h,11384,(A=i,i=i+16|0,c[A>>2]=g,c[A+8>>2]=y,A)|0);i=A;z=e[x>>1]|0}else{z=q}}while(0);if((s|0)!=0){tn(h,11240,(A=i,i=i+8|0,c[A>>2]=o,A)|0);i=A}tn(h,11104,(A=i,i=i+8|0,c[A>>2]=o,A)|0);i=A;tn(h,t,(A=i,i=i+8|0,c[A>>2]=s,A)|0);i=A;tn(h,11e3,(A=i,i=i+8|0,c[A>>2]=v,A)|0);i=A;Zm(d);if((l<<16>>16|0)==54|(l<<16>>16|0)==23){Fn(j,11880)}a:do{if((w|0)<1){B=s;C=1}else{r=0;x=0;y=s;D=1;E=0;F=0;b:while(1){G=D+1|0;c:do{switch(c[2104+(u<<5)+(G<<2)>>2]|0){case 3:{break b;break};case 12:{dn(d,3,e[m+(D+y<<1)>>1]|0);H=F;I=E;J=y;K=x;L=r;break};case 13:{dn(d,5,e[m+(D+y<<1)>>1]|0);H=F;I=E;J=y;K=x;L=r;break};case 20:{H=F;I=E;J=x-1+y|0;K=x;L=r;break};case 5:{if((b[m+(D+y<<1)>>1]|0)==0){Fn(j,10888);H=F;I=E;J=y;K=x;L=r;break c}else{Fn(j,10784);H=F;I=E;J=y;K=x;L=r;break c}break};case 2:{if((F|0)!=0){H=0;I=E;J=y;K=x;L=r;break c}dn(d,4,e[m+(D+y<<1)>>1]|0);H=0;I=E;J=y;K=x;L=r;break};case 9:{if((x|0)==0){H=F;I=E;J=y-1|0;K=0;L=r;break c}if((x|0)>0){M=y;N=0;while(1){dn(d,2,e[m+(M+D<<1)>>1]|0);O=N+1|0;if((O|0)<(x|0)){M=M+1|0;N=O}else{break}}P=x+y|0}else{P=y}H=F;I=E;J=P-1|0;K=x;L=r;break};case 10:{N=e[m+(D+y<<1)>>1]|0;tn(h,10520,(A=i,i=i+16|0,c[A>>2]=o,c[A+8>>2]=N,A)|0);i=A;Zm(d);H=F;I=E;J=y;K=x;L=r;break};case 11:{en(d,e[m+(D+y<<1)>>1]|0);H=F;I=E;J=y;K=x;L=r;break};case 17:{N=m+(D+y<<1)|0;dn(d,2,e[N>>1]|0);H=F;I=c[c[(c[(c[k>>2]|0)+52>>2]|0)+(e[N>>1]<<4)>>2]>>2]|0;J=y;K=x;L=r;break};case 18:{if((x|0)>0){N=y;M=0;while(1){dn(d,4,e[m+(N+D<<1)>>1]|0);O=M+1|0;if((O|0)<(x|0)){N=N+1|0;M=O}else{break}}Q=x+y|0}else{Q=y}H=F;I=E;J=Q-1|0;K=x;L=r;break};case 0:{Fn(j,11880);H=F;I=E;J=y;K=x;L=r;break};case 1:{dn(d,2,e[m+(D+y<<1)>>1]|0);H=F;I=E;J=y;K=x;L=r;break};case 14:{H=F;I=E;J=y;K=x;L=e[m+(D+y<<1)>>1]|0;break};case 15:{M=m+(D+y<<1)|0;N=e[M>>1]|0;if((r|0)==1){en(d,N);R=c[(c[(c[(c[p>>2]|0)+12>>2]|0)+(e[M>>1]<<2)>>2]|0)+8>>2]|0}else{M=c[(c[(c[k>>2]|0)+52>>2]|0)+(N<<4)>>2]|0;dn(d,2,N);R=M}H=(c[c[R+4>>2]>>2]|0)==0?1:F;I=E;J=y;K=x;L=r;break};case 4:{M=e[m+(D+y<<1)>>1]|0;tn(h,10648,(A=i,i=i+16|0,c[A>>2]=o,c[A+8>>2]=M,A)|0);i=A;Zm(d);H=F;I=E;J=y;K=x;L=r;break};case 8:{H=F;I=E;J=y;K=e[m+(D+y<<1)>>1]|0;L=r;break};case 16:{if((x|0)>0){M=E+48|0;N=y;O=0;while(1){S=e[m+(N+D<<1)>>1]|0;T=c[(c[(c[M>>2]|0)+(O<<2)>>2]|0)+8>>2]|0;tn(h,10408,(A=i,i=i+24|0,c[A>>2]=o,c[A+8>>2]=S,c[A+16>>2]=T,A)|0);i=A;Zm(d);T=O+1|0;if((T|0)<(x|0)){N=N+1|0;O=T}else{break}}U=x+y|0}else{U=y}H=F;I=E;J=U-1|0;K=x;L=r;break};case 19:{mn(h,10);O=b[m+(D+y<<1)>>1]|0;N=O&65535;M=(O&65535)>>>1;O=M&65535;if(M<<16>>16!=0){M=y+1+D|0;T=0;do{S=M+T|0;en(d,e[m+(S<<1)>>1]|0);dn(d,4,e[m+(S+O<<1)>>1]|0);T=T+1|0;}while((T|0)<(O|0))}H=F;I=E;J=N+y|0;K=N;L=r;break};default:{H=F;I=E;J=y;K=x;L=r}}}while(0);if((D|0)<(w|0)){r=L;x=K;y=J;D=G;E=I;F=H}else{B=J;C=G;break a}}Fn(j,11880);B=y;C=D}}while(0);w=C+B|0;if((w|0)<(n|0)){q=z;s=w}else{break}}i=f;return}function cn(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0;f=i;g=b+36|0;h=c[g>>2]|0;if((h|0)!=0){cn(a,h,d,e+1|0)}h=c[b+44>>2]|0;if((h|0)==0){i=f;return}if((e|0)==0){if((c[g>>2]|0)!=0){j=6}}else{j=6}if((j|0)==6){j=c[b+8>>2]|0;tn(c[a+8>>2]|0,8736,(b=i,i=i+16|0,c[b>>2]=(c[a+20>>2]|0)-1,c[b+8>>2]=j,b)|0);i=b}fn(a,h,d);i=f;return}function dn(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;e=i;f=c[a+8>>2]|0;g=(b&1|0)==0;h=c[(c[(g?a+4|0:a|0)>>2]|0)+52>>2]|0;j=c[h+(d<<4)>>2]|0;k=c[h+(d<<4)+4>>2]|0;l=c[h+(d<<4)+8>>2]|0;if((b&2|0)==0){m=(b&4|0)==0?21128:9528}else{m=9592}tn(f,9272,(b=i,i=i+40|0,c[b>>2]=c[a+20>>2],c[b+8>>2]=m,c[b+16>>2]=j,c[b+24>>2]=g?9672:9752,c[b+32>>2]=d,b)|0);i=b;if((k|0)==0){jn(f,11880);Zm(a);i=e;return}if((l|0)==0){tn(f,8984,(b=i,i=i+8|0,c[b>>2]=k,b)|0);i=b;Zm(a);i=e;return}else{tn(f,9168,(b=i,i=i+16|0,c[b>>2]=k,c[b+8>>2]=l,b)|0);i=b;Zm(a);i=e;return}}function en(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0;e=i;i=i+24|0;f=e|0;g=c[(c[(c[a+12>>2]|0)+12>>2]|0)+(d<<2)>>2]|0;d=g+8|0;h=c[d>>2]|0;if((b[(c[h>>2]|0)+52>>1]|0)!=5){c[f>>2]=0;c[f+4>>2]=0;c[f+8>>2]=c[d>>2];j=g+24|0;k=c[j+4>>2]|0;l=f+16|0;c[l>>2]=c[j>>2];c[l+4>>2]=k;k=c[d>>2]|0;tn(c[a+8>>2]|0,9832,(m=i,i=i+24|0,c[m>>2]=c[a+20>>2],c[m+8>>2]=k,c[m+16>>2]=f,m)|0);i=m;Zm(a);i=e;return}f=c[g+24>>2]|0;g=a+8|0;tn(c[g>>2]|0,10272,(m=i,i=i+16|0,c[m>>2]=c[a+20>>2],c[m+8>>2]=h,m)|0);i=m;h=c[f+12>>2]|0;if((h|0)!=0){tn(c[g>>2]|0,10176,(m=i,i=i+8|0,c[m>>2]=h,m)|0);i=m}h=c[f+4>>2]|0;k=c[g>>2]|0;g=c[f+16>>2]|0;if((h|0)==0){tn(k,9968,(m=i,i=i+8|0,c[m>>2]=g,m)|0);i=m}else{tn(k,10048,(m=i,i=i+16|0,c[m>>2]=g,c[m+8>>2]=h,m)|0);i=m}Zm(a);i=e;return}function fn(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0;e=i;f=c[b+36>>2]|0;if((f|0)!=0){fn(a,f,d)}f=b|0;g=c[f>>2]|0;h=c[f+4>>2]|0;if((g&4096|0)==0&(h&0|0)==0){j=(g&8192|0)==0&(h&0|0)==0?21128:8520}else{j=8640}h=a+8|0;g=a+20|0;f=b+12|0;k=c[f>>2]|0;l=c[b+20>>2]|0;tn(c[h>>2]|0,8384,(b=i,i=i+32|0,c[b>>2]=c[g>>2],c[b+8>>2]=k,c[b+16>>2]=l,c[b+24>>2]=j,b)|0);i=b;Zm(a);c[g>>2]=(c[g>>2]|0)+1;b=c[(c[d+12>>2]|0)+(c[f>>2]<<2)>>2]|0;if((c[b>>2]&1048576|0)==0){f=b+16|0;Xm(a,c[b+8>>2]|0,c[f>>2]|0,c[f+4>>2]|0);m=c[g>>2]|0;n=m-1|0;c[g>>2]=n;i=e;return}else{jn(c[h>>2]|0,8352);Zm(a);m=c[g>>2]|0;n=m-1|0;c[g>>2]=n;i=e;return}}function gn(){var b=0,d=0;b=Vd(12)|0;d=Vd(64)|0;c[b>>2]=d;a[d]=0;c[b+4>>2]=0;c[b+8>>2]=64;return b|0}function hn(a){a=a|0;Ln(c[a>>2]|0);Ln(a);return}function jn(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=_n(b|0)|0;e=a+4|0;f=d+1+(c[e>>2]|0)|0;if(f>>>0>(c[a+8>>2]|0)>>>0){vn(a,f)}$n(c[a>>2]|0,b|0)|0;c[e>>2]=(c[e>>2]|0)+d;return}function kn(a,b){a=a|0;b=b|0;wn(a,0,b,_n(b|0)|0);return}function ln(b,d,e,f){b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0;g=f-e|0;f=b+4|0;h=c[f>>2]|0;i=g+1+h|0;if(i>>>0>(c[b+8>>2]|0)>>>0){vn(b,i);j=c[f>>2]|0}else{j=h}h=b|0;Yn((c[h>>2]|0)+j|0,d+e|0,g)|0;e=(c[f>>2]|0)+g|0;c[f>>2]=e;a[(c[h>>2]|0)+e|0]=0;return}function mn(b,c){b=b|0;c=c|0;var d=0,e=0,f=0;d=i;i=i+8|0;e=d|0;f=e|0;a[f]=c;a[e+1|0]=0;jn(b,f);i=d;return}function nn(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=i;i=i+64|0;e=d|0;Qa(e|0,10400,(f=i,i=i+8|0,c[f>>2]=b,f)|0)|0;i=f;jn(a,e);i=d;return}function on(a,b){a=a|0;b=+b;var c=0,d=0,e=0;c=i;i=i+64|0;d=c|0;Qa(d|0,17952,(e=i,i=i+8|0,h[e>>3]=b,e)|0)|0;i=e;jn(a,d);i=c;return}function pn(a,b){a=a|0;b=b|0;xn(a,0,b);return}function qn(b){b=b|0;c[b+4>>2]=0;a[c[b>>2]|0]=0;return}function rn(a,d){a=a|0;d=d|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0;f=d|0;jn(a,c[(c[f>>2]|0)+8>>2]|0);g=c[f>>2]|0;f=b[g+52>>1]|0;if((f<<16>>16|0)==5){h=d+12|0;if((b[h>>1]|0)==0){jn(a,12136)}else{jn(a,16192);i=((e[h>>1]|0)-1|0)>0;mn(a,65);if(i){i=65;j=0;do{jn(a,14560);j=j+1|0;i=i+1&255;k=(j|0)<((e[h>>1]|0)-1|0);mn(a,i)}while(k)}jn(a,13368)}i=d+8|0;h=c[i>>2]|0;do{if(h>>>0>1>>>0){j=d+4|0;if((h-1|0)>>>0>1>>>0){k=1;while(1){rn(a,c[(c[j>>2]|0)+(k<<2)>>2]|0);jn(a,14560);l=k+1|0;if(l>>>0<((c[i>>2]|0)-1|0)>>>0){k=l}else{m=l;break}}}else{m=1}k=c[(c[j>>2]|0)+(m<<2)>>2]|0;if((b[d+14>>1]&1)==0){rn(a,k);n=j;break}else{rn(a,c[c[k+4>>2]>>2]|0);jn(a,10936);n=j;break}}else{n=d+4|0}}while(0);if((c[c[n>>2]>>2]|0)!=0){jn(a,8712);rn(a,c[c[n>>2]>>2]|0);jn(a,9768);return}jn(a,9768);return}else if((f<<16>>16|0)==12){mn(a,(b[d+12>>1]&255)+65&255);return}else{if((b[g+58>>1]|0)==0){return}g=f<<16>>16==10;if(!g){jn(a,16192)}f=d+8|0;if((c[f>>2]|0)!=0){n=d+4|0;d=0;do{rn(a,c[(c[n>>2]|0)+(d<<2)>>2]|0);m=c[f>>2]|0;if((d|0)==(m-1|0)){o=m}else{jn(a,14560);o=c[f>>2]|0}d=d+1|0;}while(d>>>0<o>>>0)}if(g){return}jn(a,7960);return}}function sn(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0;f=i;i=i+136|0;g=f|0;h=g|0;a[h]=37;j=g+1|0;a[j]=0;k=_n(d|0)|0;if((k|0)<=0){i=f;return}l=g+2|0;m=g+3|0;n=g+4|0;g=f+8|0;o=0;p=0;q=0;while(1){r=a[d+p|0]|0;if((r<<24>>24|0)==37){s=p+1|0;if((s|0)==(k|0)){t=o;u=p;break}if((p|0)!=(o|0)){ln(b,d,o,p)}v=a[d+s|0]|0;do{if((v-48&255)>>>0<10>>>0){a[j]=v;w=p+2|0;y=a[d+w|0]|0;if((y-48&255)>>>0<10>>>0){a[l]=y;a[m]=100;a[n]=0;z=p+3|0;A=z;B=a[d+z|0]|0;C=v;break}else{a[l]=100;a[m]=0;A=w;B=y;C=v;break}}else{A=s;B=v;C=q}}while(0);do{if((B<<24>>24|0)==115){jn(b,(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0));D=C}else if((B<<24>>24|0)==100){v=(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0);if(C<<24>>24==0){nn(b,v);D=0;break}else{va(g|0,128,h|0,(E=i,i=i+8|0,c[E>>2]=v,E)|0)|0;i=E;jn(b,g);a[j]=0;D=0;break}}else if((B<<24>>24|0)==99){mn(b,(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0)&255);D=C}else if((B<<24>>24|0)==112){v=(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0);va(g|0,128,20648,(E=i,i=i+8|0,c[E>>2]=v,E)|0)|0;i=E;jn(b,g);D=C}else{D=C}}while(0);F=A;G=A+1|0;H=D}else if((r<<24>>24|0)==94){if((p|0)!=(o|0)){ln(b,d,o,p)}v=p+1|0;switch(a[d+v|0]|0){case 84:{rn(b,(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0));break};case 73:{yn(b,(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0));break};case 69:{kn(b,(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0));break};case 82:{zn(b,(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0));break};case 86:{pn(b,(x=c[e+4>>2]|0,c[e+4>>2]=x+8,c[(c[e>>2]|0)+x>>2]|0));break};default:{}}F=v;G=p+2|0;H=q}else{F=p;G=o;H=q}v=F+1|0;if((v|0)<(k|0)){o=G;p=v;q=H}else{t=G;u=v;break}}if((u|0)==(t|0)){i=f;return}ln(b,d,t,u);i=f;return}function tn(a,b,d){a=a|0;b=b|0;d=d|0;var e=0,f=0,g=0;e=i;i=i+16|0;f=e|0;g=f;c[g>>2]=d;c[g+4>>2]=0;sn(a,b,f|0);i=e;return}function un(a){a=a|0;vn(a,c[a+8>>2]<<1);return}function vn(a,b){a=a|0;b=b|0;var d=0;d=a|0;c[d>>2]=Wd(c[d>>2]|0,b)|0;c[a+8>>2]=b;return}function wn(b,c,d,e){b=b|0;c=c|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0;do{if((e|0)>0){a:do{if((c|0)==0){f=0;g=0;h=0;while(1){i=a[d+f|0]|0;switch(i<<24>>24){case 8:{j=98;k=12;break};case 39:{j=i;k=12;break};case 7:{j=97;k=12;break};case 9:{j=116;k=12;break};case 34:{j=i;k=12;break};case 13:{j=114;k=12;break};case 10:{j=110;k=12;break};case 92:{j=i;k=12;break};default:{if((ya(i<<24>>24|0)|0)==1|i<<24>>24<0){l=g;m=0}else{j=h;k=12}}}if((k|0)==12){k=0;if((f|0)!=(g|0)){ln(b,d,g,f)}mn(b,92);if(j<<24>>24==0){An(b,i)}else{mn(b,j)}l=f+1|0;m=j}i=f+1|0;if((i|0)<(e|0)){f=i;g=l;h=m}else{n=l;break a}}}else{h=0;g=0;f=0;while(1){i=a[d+h|0]|0;switch(i<<24>>24){case 8:{o=98;k=28;break};case 39:{o=i;k=28;break};case 34:{o=i;k=28;break};case 9:{o=116;k=28;break};case 13:{o=114;k=28;break};case 10:{o=110;k=28;break};case 7:{o=97;k=28;break};case 92:{o=i;k=28;break};default:{if((ya(i<<24>>24|0)|0)==1){p=g;q=0}else{o=f;k=28}}}if((k|0)==28){k=0;if((h|0)!=(g|0)){ln(b,d,g,h)}mn(b,92);if(o<<24>>24==0){An(b,i)}else{mn(b,o)}p=h+1|0;q=o}i=h+1|0;if((i|0)<(e|0)){h=i;g=p;f=q}else{n=p;break a}}}}while(0);if((n|0)==(e|0)){break}ln(b,d,n,e)}}while(0);if((c|0)==0){return}mn(b,0);return}function xn(d,f,g){d=d|0;f=f|0;g=g|0;var j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0;j=i;i=i+16|0;k=j|0;l=c[g+8>>2]|0;if((b[l+14>>1]&2)==0){m=f}else{a:do{if((f|0)!=0){n=g+16|0;o=f;while(1){if((ho(o+8|0,n|0,8)|0)==0){break}o=c[o>>2]|0;if((o|0)==0){break a}}jn(d,19424);i=j;return}}while(0);c[k>>2]=f;f=g+16|0;o=c[f+4>>2]|0;n=k+8|0;c[n>>2]=c[f>>2];c[n+4>>2]=o;m=k}k=c[l>>2]|0;switch(b[k+52>>1]|0){case 4:{Bn(d,c[g+16>>2]|0);i=j;return};case 0:{nn(d,c[g+16>>2]|0);i=j;return};case 1:{on(d,+h[g+16>>3]);i=j;return};case 2:{mn(d,34);jn(d,c[(c[g+16>>2]|0)+8>>2]|0);mn(d,34);i=j;return};case 3:{l=c[g+16>>2]|0;wn(d,1,c[l+8>>2]|0,c[l+4>>2]|0);i=j;return};case 5:{l=c[g+16>>2]|0;o=c[l+12>>2]|0;n=(o|0)==0;f=c[l+16>>2]|0;tn(d,18016,(p=i,i=i+32|0,c[p>>2]=(c[l+28>>2]|0)==0?18384:21160,c[p+8>>2]=n?21160:o,c[p+16>>2]=n?21160:18176,c[p+24>>2]=f,p)|0);i=p;i=j;return};case 6:{xn(d,m,c[(c[g+16>>2]|0)+12>>2]|0);i=j;return};case 7:{Cn(d,m,g,16192,7960);i=j;return};case 9:{Cn(d,m,g,17840,17632);i=j;return};case 8:{f=c[g+16>>2]|0;mn(d,91);n=c[f+20>>2]|0;b:do{if((n|0)!=0){f=n;do{xn(d,m,c[f+8>>2]|0);jn(d,8712);xn(d,m,c[f+12>>2]|0);o=f+16|0;if((c[o>>2]|0)==0){break b}jn(d,14560);f=c[o>>2]|0;}while((f|0)!=0)}}while(0);mn(d,93);i=j;return};case 11:{n=c[g+16>>2]|0;tn(d,17056,(p=i,i=i+16|0,c[p>>2]=(c[n+4>>2]|0)!=0?17408:17264,c[p+8>>2]=n,p)|0);i=p;i=j;return};default:{n=k|0;if(!((c[n>>2]&1024|0)==0&(c[n+4>>2]&0|0)==0)){n=g+16|0;jn(d,c[(c[(c[k+48>>2]|0)+(e[(c[n>>2]|0)+24>>1]<<2)>>2]|0)+8>>2]|0);if((c[(c[n>>2]|0)+16>>2]|0)==0){i=j;return}Cn(d,m,g,16920,9768);i=j;return}m=g+16|0;if((b[k+54>>1]|0)==0){g=c[m>>2]|0;q=c[c[g+24>>2]>>2]|0;r=g}else{q=k;r=c[m>>2]|0}m=c[(c[q+72>>2]|0)+8>>2]|0;k=(a[m]|0)==0;g=c[q+8>>2]|0;tn(d,16648,(p=i,i=i+32|0,c[p>>2]=k?21160:m,c[p+8>>2]=k?21160:18176,c[p+16>>2]=g,c[p+24>>2]=r,p)|0);i=p;i=j;return}}}function yn(a,b){a=a|0;b=b|0;var c=0;if((b|0)>0){c=0}else{return}do{jn(a,19960);c=c+1|0;}while((c|0)<(b|0));return}function zn(a,b){a=a|0;b=b|0;var c=0,d=0;c=i;i=i+128|0;d=c|0;fb(b|0,d|0,128)|0;jn(a,d);i=c;return}function An(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;d=i;i=i+16|0;e=d|0;Qa(e|0,16248,(f=i,i=i+8|0,c[f>>2]=b&255,f)|0)|0;i=f;jn(a,e);i=d;return}function Bn(a,b){a=a|0;b=b|0;if((b|0)==0){jn(a,16568);return}else{jn(a,16408);return}}function Cn(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0,k=0;g=c[d+16>>2]|0;jn(a,e);e=g+16|0;d=c[e>>2]|0;if((d|0)==1){h=0}else if((d|0)==0){jn(a,f);return}else{i=2}do{if((i|0)==2){d=g+12|0;j=0;do{xn(a,b,c[(c[d>>2]|0)+(j<<2)>>2]|0);jn(a,14560);j=j+1|0;k=c[e>>2]|0;}while(j>>>0<(k-1|0)>>>0);if((j|0)!=(k|0)){h=j;break}jn(a,f);return}}while(0);xn(a,b,c[(c[g+12>>2]|0)+(h<<2)>>2]|0);jn(a,f);return}function Dn(a,d){a=a|0;d=d|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0;e=a+44|0;c[e>>2]=oj(a)|0;f=a+48|0;c[f>>2]=Sb(a)|0;g=a+52|0;c[g>>2]=mc(a)|0;h=a+56|0;c[h>>2]=Ge(a)|0;i=a+60|0;c[i>>2]=Mi(a)|0;j=a+68|0;c[j>>2]=ei(a,6480)|0;k=a+64|0;c[k>>2]=ei(a,7832)|0;c[a+72>>2]=qe(a)|0;c[a+76>>2]=ej(a)|0;c[a+80>>2]=ei(a,424)|0;c[a+84>>2]=ei(a,1944)|0;Wg(a)|0;l=a+88|0;c[l>>2]=ei(a,5864)|0;m=a+92|0;c[m>>2]=ei(a,1168)|0;a=c[e>>2]|0;e=c[a+4>>2]|0;c[a>>2]=c[a>>2]|512;c[a+4>>2]=e;e=c[f>>2]|0;f=c[e+4>>2]|0;c[e>>2]=c[e>>2]|512;c[e+4>>2]=f;f=c[g>>2]|0;g=c[f+4>>2]|0;c[f>>2]=c[f>>2]|512;c[f+4>>2]=g;g=c[h>>2]|0;h=c[g+4>>2]|0;c[g>>2]=c[g>>2]|512;c[g+4>>2]=h;h=c[i>>2]|0;i=c[h+4>>2]|0;c[h>>2]=c[h>>2]|512;c[h+4>>2]=i;i=(c[(c[k>>2]|0)+24>>2]|0)+14|0;b[i>>1]=b[i>>1]|2;i=c[j>>2]|0;j=c[i+4>>2]|0;c[i>>2]=c[i>>2]|16384;c[i+4>>2]=j;j=(c[(c[l>>2]|0)+24>>2]|0)+14|0;b[j>>1]=b[j>>1]|16;j=(c[(c[m>>2]|0)+24>>2]|0)+14|0;b[j>>1]=b[j>>1]|64;c[d+32>>2]=1032;c[d+36>>2]=40;return}function En(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,j=0;d=i;i=i+24|0;e=d|0;f=c[b+20>>2]|0;if((nb(f|0,9824)|0)==0){g=o;h=114}else{j=(nb(f|0,17848)|0)==0;g=j?q:p;h=119}j=Pg(c[g>>2]|0,h)|0;c[e>>2]=0;c[e+8>>2]=c[b+8>>2];c[e+16>>2]=j;di(c[a+52>>2]|0,b,e);i=d;return}function Fn(a,b){a=a|0;b=b|0;Oa(b|0,c[q>>2]|0)|0;return}function Gn(){return Ue(Te()|0)|0}function Hn(a,b){a=a|0;b=b|0;return _e(a,9608,1,b)|0}function In(a){a=a|0;Ve(a);return}function Jn(a){a=a|0;return $e(a)|0}function Kn(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0,P=0,Q=0,R=0,S=0,T=0,U=0,V=0,W=0,X=0,Y=0,Z=0,_=0,$=0,aa=0,ba=0,ca=0,da=0,ea=0,fa=0,ga=0,ha=0,ia=0,ja=0,ka=0,la=0,ma=0,na=0,oa=0,pa=0,qa=0,ra=0,sa=0,ta=0,ua=0,va=0,wa=0,xa=0,ya=0,Aa=0,Ba=0,Ca=0,Da=0,Ea=0,Fa=0,Ga=0,Ha=0,Ja=0,Ka=0,La=0;do{if(a>>>0<245>>>0){if(a>>>0<11>>>0){b=16}else{b=a+11&-8}d=b>>>3;e=c[5294]|0;f=e>>>(d>>>0);if((f&3|0)!=0){g=(f&1^1)+d|0;h=g<<1;i=21216+(h<<2)|0;j=21216+(h+2<<2)|0;h=c[j>>2]|0;k=h+8|0;l=c[k>>2]|0;do{if((i|0)==(l|0)){c[5294]=e&~(1<<g)}else{if(l>>>0<(c[5298]|0)>>>0){za();return 0}m=l+12|0;if((c[m>>2]|0)==(h|0)){c[m>>2]=i;c[j>>2]=l;break}else{za();return 0}}}while(0);l=g<<3;c[h+4>>2]=l|3;j=h+(l|4)|0;c[j>>2]=c[j>>2]|1;n=k;return n|0}if(b>>>0<=(c[5296]|0)>>>0){o=b;break}if((f|0)!=0){j=2<<d;l=f<<d&(j|-j);j=(l&-l)-1|0;l=j>>>12&16;i=j>>>(l>>>0);j=i>>>5&8;m=i>>>(j>>>0);i=m>>>2&4;p=m>>>(i>>>0);m=p>>>1&2;q=p>>>(m>>>0);p=q>>>1&1;r=(j|l|i|m|p)+(q>>>(p>>>0))|0;p=r<<1;q=21216+(p<<2)|0;m=21216+(p+2<<2)|0;p=c[m>>2]|0;i=p+8|0;l=c[i>>2]|0;do{if((q|0)==(l|0)){c[5294]=e&~(1<<r)}else{if(l>>>0<(c[5298]|0)>>>0){za();return 0}j=l+12|0;if((c[j>>2]|0)==(p|0)){c[j>>2]=q;c[m>>2]=l;break}else{za();return 0}}}while(0);l=r<<3;m=l-b|0;c[p+4>>2]=b|3;q=p;e=q+b|0;c[q+(b|4)>>2]=m|1;c[q+l>>2]=m;l=c[5296]|0;if((l|0)!=0){q=c[5299]|0;d=l>>>3;l=d<<1;f=21216+(l<<2)|0;k=c[5294]|0;h=1<<d;do{if((k&h|0)==0){c[5294]=k|h;s=f;t=21216+(l+2<<2)|0}else{d=21216+(l+2<<2)|0;g=c[d>>2]|0;if(g>>>0>=(c[5298]|0)>>>0){s=g;t=d;break}za();return 0}}while(0);c[t>>2]=q;c[s+12>>2]=q;c[q+8>>2]=s;c[q+12>>2]=f}c[5296]=m;c[5299]=e;n=i;return n|0}l=c[5295]|0;if((l|0)==0){o=b;break}h=(l&-l)-1|0;l=h>>>12&16;k=h>>>(l>>>0);h=k>>>5&8;p=k>>>(h>>>0);k=p>>>2&4;r=p>>>(k>>>0);p=r>>>1&2;d=r>>>(p>>>0);r=d>>>1&1;g=c[21480+((h|l|k|p|r)+(d>>>(r>>>0))<<2)>>2]|0;r=g;d=g;p=(c[g+4>>2]&-8)-b|0;while(1){g=c[r+16>>2]|0;if((g|0)==0){k=c[r+20>>2]|0;if((k|0)==0){break}else{u=k}}else{u=g}g=(c[u+4>>2]&-8)-b|0;k=g>>>0<p>>>0;r=u;d=k?u:d;p=k?g:p}r=d;i=c[5298]|0;if(r>>>0<i>>>0){za();return 0}e=r+b|0;m=e;if(r>>>0>=e>>>0){za();return 0}e=c[d+24>>2]|0;f=c[d+12>>2]|0;do{if((f|0)==(d|0)){q=d+20|0;g=c[q>>2]|0;if((g|0)==0){k=d+16|0;l=c[k>>2]|0;if((l|0)==0){v=0;break}else{w=l;x=k}}else{w=g;x=q}while(1){q=w+20|0;g=c[q>>2]|0;if((g|0)!=0){w=g;x=q;continue}q=w+16|0;g=c[q>>2]|0;if((g|0)==0){break}else{w=g;x=q}}if(x>>>0<i>>>0){za();return 0}else{c[x>>2]=0;v=w;break}}else{q=c[d+8>>2]|0;if(q>>>0<i>>>0){za();return 0}g=q+12|0;if((c[g>>2]|0)!=(d|0)){za();return 0}k=f+8|0;if((c[k>>2]|0)==(d|0)){c[g>>2]=f;c[k>>2]=q;v=f;break}else{za();return 0}}}while(0);a:do{if((e|0)!=0){f=d+28|0;i=21480+(c[f>>2]<<2)|0;do{if((d|0)==(c[i>>2]|0)){c[i>>2]=v;if((v|0)!=0){break}c[5295]=c[5295]&~(1<<c[f>>2]);break a}else{if(e>>>0<(c[5298]|0)>>>0){za();return 0}q=e+16|0;if((c[q>>2]|0)==(d|0)){c[q>>2]=v}else{c[e+20>>2]=v}if((v|0)==0){break a}}}while(0);if(v>>>0<(c[5298]|0)>>>0){za();return 0}c[v+24>>2]=e;f=c[d+16>>2]|0;do{if((f|0)!=0){if(f>>>0<(c[5298]|0)>>>0){za();return 0}else{c[v+16>>2]=f;c[f+24>>2]=v;break}}}while(0);f=c[d+20>>2]|0;if((f|0)==0){break}if(f>>>0<(c[5298]|0)>>>0){za();return 0}else{c[v+20>>2]=f;c[f+24>>2]=v;break}}}while(0);if(p>>>0<16>>>0){e=p+b|0;c[d+4>>2]=e|3;f=r+(e+4)|0;c[f>>2]=c[f>>2]|1}else{c[d+4>>2]=b|3;c[r+(b|4)>>2]=p|1;c[r+(p+b)>>2]=p;f=c[5296]|0;if((f|0)!=0){e=c[5299]|0;i=f>>>3;f=i<<1;q=21216+(f<<2)|0;k=c[5294]|0;g=1<<i;do{if((k&g|0)==0){c[5294]=k|g;y=q;z=21216+(f+2<<2)|0}else{i=21216+(f+2<<2)|0;l=c[i>>2]|0;if(l>>>0>=(c[5298]|0)>>>0){y=l;z=i;break}za();return 0}}while(0);c[z>>2]=e;c[y+12>>2]=e;c[e+8>>2]=y;c[e+12>>2]=q}c[5296]=p;c[5299]=m}n=d+8|0;return n|0}else{if(a>>>0>4294967231>>>0){o=-1;break}f=a+11|0;g=f&-8;k=c[5295]|0;if((k|0)==0){o=g;break}r=-g|0;i=f>>>8;do{if((i|0)==0){A=0}else{if(g>>>0>16777215>>>0){A=31;break}f=(i+1048320|0)>>>16&8;l=i<<f;h=(l+520192|0)>>>16&4;j=l<<h;l=(j+245760|0)>>>16&2;B=14-(h|f|l)+(j<<l>>>15)|0;A=g>>>((B+7|0)>>>0)&1|B<<1}}while(0);i=c[21480+(A<<2)>>2]|0;b:do{if((i|0)==0){C=0;D=r;E=0}else{if((A|0)==31){F=0}else{F=25-(A>>>1)|0}d=0;m=r;p=i;q=g<<F;e=0;while(1){B=c[p+4>>2]&-8;l=B-g|0;if(l>>>0<m>>>0){if((B|0)==(g|0)){C=p;D=l;E=p;break b}else{G=p;H=l}}else{G=d;H=m}l=c[p+20>>2]|0;B=c[p+16+(q>>>31<<2)>>2]|0;j=(l|0)==0|(l|0)==(B|0)?e:l;if((B|0)==0){C=G;D=H;E=j;break}else{d=G;m=H;p=B;q=q<<1;e=j}}}}while(0);if((E|0)==0&(C|0)==0){i=2<<A;r=k&(i|-i);if((r|0)==0){o=g;break}i=(r&-r)-1|0;r=i>>>12&16;e=i>>>(r>>>0);i=e>>>5&8;q=e>>>(i>>>0);e=q>>>2&4;p=q>>>(e>>>0);q=p>>>1&2;m=p>>>(q>>>0);p=m>>>1&1;I=c[21480+((i|r|e|q|p)+(m>>>(p>>>0))<<2)>>2]|0}else{I=E}if((I|0)==0){J=D;K=C}else{p=I;m=D;q=C;while(1){e=(c[p+4>>2]&-8)-g|0;r=e>>>0<m>>>0;i=r?e:m;e=r?p:q;r=c[p+16>>2]|0;if((r|0)!=0){p=r;m=i;q=e;continue}r=c[p+20>>2]|0;if((r|0)==0){J=i;K=e;break}else{p=r;m=i;q=e}}}if((K|0)==0){o=g;break}if(J>>>0>=((c[5296]|0)-g|0)>>>0){o=g;break}q=K;m=c[5298]|0;if(q>>>0<m>>>0){za();return 0}p=q+g|0;k=p;if(q>>>0>=p>>>0){za();return 0}e=c[K+24>>2]|0;i=c[K+12>>2]|0;do{if((i|0)==(K|0)){r=K+20|0;d=c[r>>2]|0;if((d|0)==0){j=K+16|0;B=c[j>>2]|0;if((B|0)==0){L=0;break}else{M=B;N=j}}else{M=d;N=r}while(1){r=M+20|0;d=c[r>>2]|0;if((d|0)!=0){M=d;N=r;continue}r=M+16|0;d=c[r>>2]|0;if((d|0)==0){break}else{M=d;N=r}}if(N>>>0<m>>>0){za();return 0}else{c[N>>2]=0;L=M;break}}else{r=c[K+8>>2]|0;if(r>>>0<m>>>0){za();return 0}d=r+12|0;if((c[d>>2]|0)!=(K|0)){za();return 0}j=i+8|0;if((c[j>>2]|0)==(K|0)){c[d>>2]=i;c[j>>2]=r;L=i;break}else{za();return 0}}}while(0);c:do{if((e|0)!=0){i=K+28|0;m=21480+(c[i>>2]<<2)|0;do{if((K|0)==(c[m>>2]|0)){c[m>>2]=L;if((L|0)!=0){break}c[5295]=c[5295]&~(1<<c[i>>2]);break c}else{if(e>>>0<(c[5298]|0)>>>0){za();return 0}r=e+16|0;if((c[r>>2]|0)==(K|0)){c[r>>2]=L}else{c[e+20>>2]=L}if((L|0)==0){break c}}}while(0);if(L>>>0<(c[5298]|0)>>>0){za();return 0}c[L+24>>2]=e;i=c[K+16>>2]|0;do{if((i|0)!=0){if(i>>>0<(c[5298]|0)>>>0){za();return 0}else{c[L+16>>2]=i;c[i+24>>2]=L;break}}}while(0);i=c[K+20>>2]|0;if((i|0)==0){break}if(i>>>0<(c[5298]|0)>>>0){za();return 0}else{c[L+20>>2]=i;c[i+24>>2]=L;break}}}while(0);d:do{if(J>>>0<16>>>0){e=J+g|0;c[K+4>>2]=e|3;i=q+(e+4)|0;c[i>>2]=c[i>>2]|1}else{c[K+4>>2]=g|3;c[q+(g|4)>>2]=J|1;c[q+(J+g)>>2]=J;i=J>>>3;if(J>>>0<256>>>0){e=i<<1;m=21216+(e<<2)|0;r=c[5294]|0;j=1<<i;do{if((r&j|0)==0){c[5294]=r|j;O=m;P=21216+(e+2<<2)|0}else{i=21216+(e+2<<2)|0;d=c[i>>2]|0;if(d>>>0>=(c[5298]|0)>>>0){O=d;P=i;break}za();return 0}}while(0);c[P>>2]=k;c[O+12>>2]=k;c[q+(g+8)>>2]=O;c[q+(g+12)>>2]=m;break}e=p;j=J>>>8;do{if((j|0)==0){Q=0}else{if(J>>>0>16777215>>>0){Q=31;break}r=(j+1048320|0)>>>16&8;i=j<<r;d=(i+520192|0)>>>16&4;B=i<<d;i=(B+245760|0)>>>16&2;l=14-(d|r|i)+(B<<i>>>15)|0;Q=J>>>((l+7|0)>>>0)&1|l<<1}}while(0);j=21480+(Q<<2)|0;c[q+(g+28)>>2]=Q;c[q+(g+20)>>2]=0;c[q+(g+16)>>2]=0;m=c[5295]|0;l=1<<Q;if((m&l|0)==0){c[5295]=m|l;c[j>>2]=e;c[q+(g+24)>>2]=j;c[q+(g+12)>>2]=e;c[q+(g+8)>>2]=e;break}l=c[j>>2]|0;if((Q|0)==31){R=0}else{R=25-(Q>>>1)|0}e:do{if((c[l+4>>2]&-8|0)==(J|0)){S=l}else{j=l;m=J<<R;while(1){T=j+16+(m>>>31<<2)|0;i=c[T>>2]|0;if((i|0)==0){break}if((c[i+4>>2]&-8|0)==(J|0)){S=i;break e}else{j=i;m=m<<1}}if(T>>>0<(c[5298]|0)>>>0){za();return 0}else{c[T>>2]=e;c[q+(g+24)>>2]=j;c[q+(g+12)>>2]=e;c[q+(g+8)>>2]=e;break d}}}while(0);l=S+8|0;m=c[l>>2]|0;i=c[5298]|0;if(S>>>0>=i>>>0&m>>>0>=i>>>0){c[m+12>>2]=e;c[l>>2]=e;c[q+(g+8)>>2]=m;c[q+(g+12)>>2]=S;c[q+(g+24)>>2]=0;break}else{za();return 0}}}while(0);n=K+8|0;return n|0}}while(0);K=c[5296]|0;if(K>>>0>=o>>>0){S=K-o|0;T=c[5299]|0;if(S>>>0>15>>>0){J=T;c[5299]=J+o;c[5296]=S;c[J+(o+4)>>2]=S|1;c[J+K>>2]=S;c[T+4>>2]=o|3}else{c[5296]=0;c[5299]=0;c[T+4>>2]=K|3;S=T+(K+4)|0;c[S>>2]=c[S>>2]|1}n=T+8|0;return n|0}T=c[5297]|0;if(T>>>0>o>>>0){S=T-o|0;c[5297]=S;T=c[5300]|0;K=T;c[5300]=K+o;c[K+(o+4)>>2]=S|1;c[T+4>>2]=o|3;n=T+8|0;return n|0}do{if((c[5276]|0)==0){T=Ia(30)|0;if((T-1&T|0)==0){c[5278]=T;c[5277]=T;c[5279]=-1;c[5280]=-1;c[5281]=0;c[5405]=0;c[5276]=(kb(0)|0)&-16^1431655768;break}else{za();return 0}}}while(0);T=o+48|0;S=c[5278]|0;K=o+47|0;J=S+K|0;R=-S|0;S=J&R;if(S>>>0<=o>>>0){n=0;return n|0}Q=c[5404]|0;do{if((Q|0)!=0){O=c[5402]|0;P=O+S|0;if(P>>>0<=O>>>0|P>>>0>Q>>>0){n=0}else{break}return n|0}}while(0);f:do{if((c[5405]&4|0)==0){Q=c[5300]|0;g:do{if((Q|0)==0){U=181}else{P=Q;O=21624;while(1){V=O|0;L=c[V>>2]|0;if(L>>>0<=P>>>0){W=O+4|0;if((L+(c[W>>2]|0)|0)>>>0>P>>>0){break}}L=c[O+8>>2]|0;if((L|0)==0){U=181;break g}else{O=L}}if((O|0)==0){U=181;break}P=J-(c[5297]|0)&R;if(P>>>0>=2147483647>>>0){X=0;break}e=db(P|0)|0;if((e|0)==((c[V>>2]|0)+(c[W>>2]|0)|0)){Y=e;Z=P;U=190}else{_=P;$=e;U=191}}}while(0);do{if((U|0)==181){Q=db(0)|0;if((Q|0)==-1){X=0;break}e=Q;P=c[5277]|0;L=P-1|0;if((L&e|0)==0){aa=S}else{aa=S-e+(L+e&-P)|0}P=c[5402]|0;e=P+aa|0;if(!(aa>>>0>o>>>0&aa>>>0<2147483647>>>0)){X=0;break}L=c[5404]|0;if((L|0)!=0){if(e>>>0<=P>>>0|e>>>0>L>>>0){X=0;break}}L=db(aa|0)|0;if((L|0)==(Q|0)){Y=Q;Z=aa;U=190}else{_=aa;$=L;U=191}}}while(0);h:do{if((U|0)==190){if((Y|0)==-1){X=Z}else{ba=Z;ca=Y;U=201;break f}}else if((U|0)==191){L=-_|0;do{if(($|0)!=-1&_>>>0<2147483647>>>0&T>>>0>_>>>0){Q=c[5278]|0;e=K-_+Q&-Q;if(e>>>0>=2147483647>>>0){da=_;break}if((db(e|0)|0)==-1){db(L|0)|0;X=0;break h}else{da=e+_|0;break}}else{da=_}}while(0);if(($|0)==-1){X=0}else{ba=da;ca=$;U=201;break f}}}while(0);c[5405]=c[5405]|4;ea=X;U=198}else{ea=0;U=198}}while(0);do{if((U|0)==198){if(S>>>0>=2147483647>>>0){break}X=db(S|0)|0;$=db(0)|0;if(!((X|0)!=-1&($|0)!=-1&X>>>0<$>>>0)){break}da=$-X|0;$=da>>>0>(o+40|0)>>>0;if($){ba=$?da:ea;ca=X;U=201}}}while(0);do{if((U|0)==201){ea=(c[5402]|0)+ba|0;c[5402]=ea;if(ea>>>0>(c[5403]|0)>>>0){c[5403]=ea}ea=c[5300]|0;i:do{if((ea|0)==0){S=c[5298]|0;if((S|0)==0|ca>>>0<S>>>0){c[5298]=ca}c[5406]=ca;c[5407]=ba;c[5409]=0;c[5303]=c[5276];c[5302]=-1;S=0;do{X=S<<1;da=21216+(X<<2)|0;c[21216+(X+3<<2)>>2]=da;c[21216+(X+2<<2)>>2]=da;S=S+1|0;}while(S>>>0<32>>>0);S=ca+8|0;if((S&7|0)==0){fa=0}else{fa=-S&7}S=ba-40-fa|0;c[5300]=ca+fa;c[5297]=S;c[ca+(fa+4)>>2]=S|1;c[ca+(ba-36)>>2]=40;c[5301]=c[5280]}else{S=21624;while(1){ga=c[S>>2]|0;ha=S+4|0;ia=c[ha>>2]|0;if((ca|0)==(ga+ia|0)){U=213;break}da=c[S+8>>2]|0;if((da|0)==0){break}else{S=da}}do{if((U|0)==213){if((c[S+12>>2]&8|0)!=0){break}da=ea;if(!(da>>>0>=ga>>>0&da>>>0<ca>>>0)){break}c[ha>>2]=ia+ba;da=c[5300]|0;X=(c[5297]|0)+ba|0;$=da;_=da+8|0;if((_&7|0)==0){ja=0}else{ja=-_&7}_=X-ja|0;c[5300]=$+ja;c[5297]=_;c[$+(ja+4)>>2]=_|1;c[$+(X+4)>>2]=40;c[5301]=c[5280];break i}}while(0);if(ca>>>0<(c[5298]|0)>>>0){c[5298]=ca}S=ca+ba|0;X=21624;while(1){ka=X|0;if((c[ka>>2]|0)==(S|0)){U=223;break}$=c[X+8>>2]|0;if(($|0)==0){break}else{X=$}}do{if((U|0)==223){if((c[X+12>>2]&8|0)!=0){break}c[ka>>2]=ca;S=X+4|0;c[S>>2]=(c[S>>2]|0)+ba;S=ca+8|0;if((S&7|0)==0){la=0}else{la=-S&7}S=ca+(ba+8)|0;if((S&7|0)==0){ma=0}else{ma=-S&7}S=ca+(ma+ba)|0;$=S;_=la+o|0;da=ca+_|0;K=da;T=S-(ca+la)-o|0;c[ca+(la+4)>>2]=o|3;j:do{if(($|0)==(c[5300]|0)){Y=(c[5297]|0)+T|0;c[5297]=Y;c[5300]=K;c[ca+(_+4)>>2]=Y|1}else{if(($|0)==(c[5299]|0)){Y=(c[5296]|0)+T|0;c[5296]=Y;c[5299]=K;c[ca+(_+4)>>2]=Y|1;c[ca+(Y+_)>>2]=Y;break}Y=ba+4|0;Z=c[ca+(Y+ma)>>2]|0;if((Z&3|0)==1){aa=Z&-8;W=Z>>>3;k:do{if(Z>>>0<256>>>0){V=c[ca+((ma|8)+ba)>>2]|0;R=c[ca+(ba+12+ma)>>2]|0;J=21216+(W<<1<<2)|0;do{if((V|0)!=(J|0)){if(V>>>0<(c[5298]|0)>>>0){za();return 0}if((c[V+12>>2]|0)==($|0)){break}za();return 0}}while(0);if((R|0)==(V|0)){c[5294]=c[5294]&~(1<<W);break}do{if((R|0)==(J|0)){na=R+8|0}else{if(R>>>0<(c[5298]|0)>>>0){za();return 0}L=R+8|0;if((c[L>>2]|0)==($|0)){na=L;break}za();return 0}}while(0);c[V+12>>2]=R;c[na>>2]=V}else{J=S;L=c[ca+((ma|24)+ba)>>2]|0;O=c[ca+(ba+12+ma)>>2]|0;do{if((O|0)==(J|0)){e=ma|16;Q=ca+(Y+e)|0;P=c[Q>>2]|0;if((P|0)==0){M=ca+(e+ba)|0;e=c[M>>2]|0;if((e|0)==0){oa=0;break}else{pa=e;qa=M}}else{pa=P;qa=Q}while(1){Q=pa+20|0;P=c[Q>>2]|0;if((P|0)!=0){pa=P;qa=Q;continue}Q=pa+16|0;P=c[Q>>2]|0;if((P|0)==0){break}else{pa=P;qa=Q}}if(qa>>>0<(c[5298]|0)>>>0){za();return 0}else{c[qa>>2]=0;oa=pa;break}}else{Q=c[ca+((ma|8)+ba)>>2]|0;if(Q>>>0<(c[5298]|0)>>>0){za();return 0}P=Q+12|0;if((c[P>>2]|0)!=(J|0)){za();return 0}M=O+8|0;if((c[M>>2]|0)==(J|0)){c[P>>2]=O;c[M>>2]=Q;oa=O;break}else{za();return 0}}}while(0);if((L|0)==0){break}O=ca+(ba+28+ma)|0;V=21480+(c[O>>2]<<2)|0;do{if((J|0)==(c[V>>2]|0)){c[V>>2]=oa;if((oa|0)!=0){break}c[5295]=c[5295]&~(1<<c[O>>2]);break k}else{if(L>>>0<(c[5298]|0)>>>0){za();return 0}R=L+16|0;if((c[R>>2]|0)==(J|0)){c[R>>2]=oa}else{c[L+20>>2]=oa}if((oa|0)==0){break k}}}while(0);if(oa>>>0<(c[5298]|0)>>>0){za();return 0}c[oa+24>>2]=L;J=ma|16;O=c[ca+(J+ba)>>2]|0;do{if((O|0)!=0){if(O>>>0<(c[5298]|0)>>>0){za();return 0}else{c[oa+16>>2]=O;c[O+24>>2]=oa;break}}}while(0);O=c[ca+(Y+J)>>2]|0;if((O|0)==0){break}if(O>>>0<(c[5298]|0)>>>0){za();return 0}else{c[oa+20>>2]=O;c[O+24>>2]=oa;break}}}while(0);ra=ca+((aa|ma)+ba)|0;sa=aa+T|0}else{ra=$;sa=T}Y=ra+4|0;c[Y>>2]=c[Y>>2]&-2;c[ca+(_+4)>>2]=sa|1;c[ca+(sa+_)>>2]=sa;Y=sa>>>3;if(sa>>>0<256>>>0){W=Y<<1;Z=21216+(W<<2)|0;O=c[5294]|0;L=1<<Y;do{if((O&L|0)==0){c[5294]=O|L;ta=Z;ua=21216+(W+2<<2)|0}else{Y=21216+(W+2<<2)|0;V=c[Y>>2]|0;if(V>>>0>=(c[5298]|0)>>>0){ta=V;ua=Y;break}za();return 0}}while(0);c[ua>>2]=K;c[ta+12>>2]=K;c[ca+(_+8)>>2]=ta;c[ca+(_+12)>>2]=Z;break}W=da;L=sa>>>8;do{if((L|0)==0){va=0}else{if(sa>>>0>16777215>>>0){va=31;break}O=(L+1048320|0)>>>16&8;aa=L<<O;Y=(aa+520192|0)>>>16&4;V=aa<<Y;aa=(V+245760|0)>>>16&2;R=14-(Y|O|aa)+(V<<aa>>>15)|0;va=sa>>>((R+7|0)>>>0)&1|R<<1}}while(0);L=21480+(va<<2)|0;c[ca+(_+28)>>2]=va;c[ca+(_+20)>>2]=0;c[ca+(_+16)>>2]=0;Z=c[5295]|0;R=1<<va;if((Z&R|0)==0){c[5295]=Z|R;c[L>>2]=W;c[ca+(_+24)>>2]=L;c[ca+(_+12)>>2]=W;c[ca+(_+8)>>2]=W;break}R=c[L>>2]|0;if((va|0)==31){wa=0}else{wa=25-(va>>>1)|0}l:do{if((c[R+4>>2]&-8|0)==(sa|0)){xa=R}else{L=R;Z=sa<<wa;while(1){ya=L+16+(Z>>>31<<2)|0;aa=c[ya>>2]|0;if((aa|0)==0){break}if((c[aa+4>>2]&-8|0)==(sa|0)){xa=aa;break l}else{L=aa;Z=Z<<1}}if(ya>>>0<(c[5298]|0)>>>0){za();return 0}else{c[ya>>2]=W;c[ca+(_+24)>>2]=L;c[ca+(_+12)>>2]=W;c[ca+(_+8)>>2]=W;break j}}}while(0);R=xa+8|0;Z=c[R>>2]|0;J=c[5298]|0;if(xa>>>0>=J>>>0&Z>>>0>=J>>>0){c[Z+12>>2]=W;c[R>>2]=W;c[ca+(_+8)>>2]=Z;c[ca+(_+12)>>2]=xa;c[ca+(_+24)>>2]=0;break}else{za();return 0}}}while(0);n=ca+(la|8)|0;return n|0}}while(0);X=ea;_=21624;while(1){Aa=c[_>>2]|0;if(Aa>>>0<=X>>>0){Ba=c[_+4>>2]|0;Ca=Aa+Ba|0;if(Ca>>>0>X>>>0){break}}_=c[_+8>>2]|0}_=Aa+(Ba-39)|0;if((_&7|0)==0){Da=0}else{Da=-_&7}_=Aa+(Ba-47+Da)|0;da=_>>>0<(ea+16|0)>>>0?X:_;_=da+8|0;K=ca+8|0;if((K&7|0)==0){Ea=0}else{Ea=-K&7}K=ba-40-Ea|0;c[5300]=ca+Ea;c[5297]=K;c[ca+(Ea+4)>>2]=K|1;c[ca+(ba-36)>>2]=40;c[5301]=c[5280];c[da+4>>2]=27;c[_>>2]=c[5406];c[_+4>>2]=c[5407];c[_+8>>2]=c[5408];c[_+12>>2]=c[5409];c[5406]=ca;c[5407]=ba;c[5409]=0;c[5408]=_;_=da+28|0;c[_>>2]=7;if((da+32|0)>>>0<Ca>>>0){K=_;while(1){_=K+4|0;c[_>>2]=7;if((K+8|0)>>>0<Ca>>>0){K=_}else{break}}}if((da|0)==(X|0)){break}K=da-ea|0;_=X+(K+4)|0;c[_>>2]=c[_>>2]&-2;c[ea+4>>2]=K|1;c[X+K>>2]=K;_=K>>>3;if(K>>>0<256>>>0){T=_<<1;$=21216+(T<<2)|0;S=c[5294]|0;j=1<<_;do{if((S&j|0)==0){c[5294]=S|j;Fa=$;Ga=21216+(T+2<<2)|0}else{_=21216+(T+2<<2)|0;Z=c[_>>2]|0;if(Z>>>0>=(c[5298]|0)>>>0){Fa=Z;Ga=_;break}za();return 0}}while(0);c[Ga>>2]=ea;c[Fa+12>>2]=ea;c[ea+8>>2]=Fa;c[ea+12>>2]=$;break}T=ea;j=K>>>8;do{if((j|0)==0){Ha=0}else{if(K>>>0>16777215>>>0){Ha=31;break}S=(j+1048320|0)>>>16&8;X=j<<S;da=(X+520192|0)>>>16&4;_=X<<da;X=(_+245760|0)>>>16&2;Z=14-(da|S|X)+(_<<X>>>15)|0;Ha=K>>>((Z+7|0)>>>0)&1|Z<<1}}while(0);j=21480+(Ha<<2)|0;c[ea+28>>2]=Ha;c[ea+20>>2]=0;c[ea+16>>2]=0;$=c[5295]|0;Z=1<<Ha;if(($&Z|0)==0){c[5295]=$|Z;c[j>>2]=T;c[ea+24>>2]=j;c[ea+12>>2]=ea;c[ea+8>>2]=ea;break}Z=c[j>>2]|0;if((Ha|0)==31){Ja=0}else{Ja=25-(Ha>>>1)|0}m:do{if((c[Z+4>>2]&-8|0)==(K|0)){Ka=Z}else{j=Z;$=K<<Ja;while(1){La=j+16+($>>>31<<2)|0;X=c[La>>2]|0;if((X|0)==0){break}if((c[X+4>>2]&-8|0)==(K|0)){Ka=X;break m}else{j=X;$=$<<1}}if(La>>>0<(c[5298]|0)>>>0){za();return 0}else{c[La>>2]=T;c[ea+24>>2]=j;c[ea+12>>2]=ea;c[ea+8>>2]=ea;break i}}}while(0);K=Ka+8|0;Z=c[K>>2]|0;$=c[5298]|0;if(Ka>>>0>=$>>>0&Z>>>0>=$>>>0){c[Z+12>>2]=T;c[K>>2]=T;c[ea+8>>2]=Z;c[ea+12>>2]=Ka;c[ea+24>>2]=0;break}else{za();return 0}}}while(0);ea=c[5297]|0;if(ea>>>0<=o>>>0){break}Z=ea-o|0;c[5297]=Z;ea=c[5300]|0;K=ea;c[5300]=K+o;c[K+(o+4)>>2]=Z|1;c[ea+4>>2]=o|3;n=ea+8|0;return n|0}}while(0);c[(gb()|0)>>2]=12;n=0;return n|0}function Ln(a){a=a|0;var b=0,d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0;if((a|0)==0){return}b=a-8|0;d=b;e=c[5298]|0;if(b>>>0<e>>>0){za()}f=c[a-4>>2]|0;g=f&3;if((g|0)==1){za()}h=f&-8;i=a+(h-8)|0;j=i;a:do{if((f&1|0)==0){k=c[b>>2]|0;if((g|0)==0){return}l=-8-k|0;m=a+l|0;n=m;o=k+h|0;if(m>>>0<e>>>0){za()}if((n|0)==(c[5299]|0)){p=a+(h-4)|0;if((c[p>>2]&3|0)!=3){q=n;r=o;break}c[5296]=o;c[p>>2]=c[p>>2]&-2;c[a+(l+4)>>2]=o|1;c[i>>2]=o;return}p=k>>>3;if(k>>>0<256>>>0){k=c[a+(l+8)>>2]|0;s=c[a+(l+12)>>2]|0;t=21216+(p<<1<<2)|0;do{if((k|0)!=(t|0)){if(k>>>0<e>>>0){za()}if((c[k+12>>2]|0)==(n|0)){break}za()}}while(0);if((s|0)==(k|0)){c[5294]=c[5294]&~(1<<p);q=n;r=o;break}do{if((s|0)==(t|0)){u=s+8|0}else{if(s>>>0<e>>>0){za()}v=s+8|0;if((c[v>>2]|0)==(n|0)){u=v;break}za()}}while(0);c[k+12>>2]=s;c[u>>2]=k;q=n;r=o;break}t=m;p=c[a+(l+24)>>2]|0;v=c[a+(l+12)>>2]|0;do{if((v|0)==(t|0)){w=a+(l+20)|0;x=c[w>>2]|0;if((x|0)==0){y=a+(l+16)|0;z=c[y>>2]|0;if((z|0)==0){A=0;break}else{B=z;C=y}}else{B=x;C=w}while(1){w=B+20|0;x=c[w>>2]|0;if((x|0)!=0){B=x;C=w;continue}w=B+16|0;x=c[w>>2]|0;if((x|0)==0){break}else{B=x;C=w}}if(C>>>0<e>>>0){za()}else{c[C>>2]=0;A=B;break}}else{w=c[a+(l+8)>>2]|0;if(w>>>0<e>>>0){za()}x=w+12|0;if((c[x>>2]|0)!=(t|0)){za()}y=v+8|0;if((c[y>>2]|0)==(t|0)){c[x>>2]=v;c[y>>2]=w;A=v;break}else{za()}}}while(0);if((p|0)==0){q=n;r=o;break}v=a+(l+28)|0;m=21480+(c[v>>2]<<2)|0;do{if((t|0)==(c[m>>2]|0)){c[m>>2]=A;if((A|0)!=0){break}c[5295]=c[5295]&~(1<<c[v>>2]);q=n;r=o;break a}else{if(p>>>0<(c[5298]|0)>>>0){za()}k=p+16|0;if((c[k>>2]|0)==(t|0)){c[k>>2]=A}else{c[p+20>>2]=A}if((A|0)==0){q=n;r=o;break a}}}while(0);if(A>>>0<(c[5298]|0)>>>0){za()}c[A+24>>2]=p;t=c[a+(l+16)>>2]|0;do{if((t|0)!=0){if(t>>>0<(c[5298]|0)>>>0){za()}else{c[A+16>>2]=t;c[t+24>>2]=A;break}}}while(0);t=c[a+(l+20)>>2]|0;if((t|0)==0){q=n;r=o;break}if(t>>>0<(c[5298]|0)>>>0){za()}else{c[A+20>>2]=t;c[t+24>>2]=A;q=n;r=o;break}}else{q=d;r=h}}while(0);d=q;if(d>>>0>=i>>>0){za()}A=a+(h-4)|0;e=c[A>>2]|0;if((e&1|0)==0){za()}do{if((e&2|0)==0){if((j|0)==(c[5300]|0)){B=(c[5297]|0)+r|0;c[5297]=B;c[5300]=q;c[q+4>>2]=B|1;if((q|0)!=(c[5299]|0)){return}c[5299]=0;c[5296]=0;return}if((j|0)==(c[5299]|0)){B=(c[5296]|0)+r|0;c[5296]=B;c[5299]=q;c[q+4>>2]=B|1;c[d+B>>2]=B;return}B=(e&-8)+r|0;C=e>>>3;b:do{if(e>>>0<256>>>0){u=c[a+h>>2]|0;g=c[a+(h|4)>>2]|0;b=21216+(C<<1<<2)|0;do{if((u|0)!=(b|0)){if(u>>>0<(c[5298]|0)>>>0){za()}if((c[u+12>>2]|0)==(j|0)){break}za()}}while(0);if((g|0)==(u|0)){c[5294]=c[5294]&~(1<<C);break}do{if((g|0)==(b|0)){D=g+8|0}else{if(g>>>0<(c[5298]|0)>>>0){za()}f=g+8|0;if((c[f>>2]|0)==(j|0)){D=f;break}za()}}while(0);c[u+12>>2]=g;c[D>>2]=u}else{b=i;f=c[a+(h+16)>>2]|0;t=c[a+(h|4)>>2]|0;do{if((t|0)==(b|0)){p=a+(h+12)|0;v=c[p>>2]|0;if((v|0)==0){m=a+(h+8)|0;k=c[m>>2]|0;if((k|0)==0){E=0;break}else{F=k;G=m}}else{F=v;G=p}while(1){p=F+20|0;v=c[p>>2]|0;if((v|0)!=0){F=v;G=p;continue}p=F+16|0;v=c[p>>2]|0;if((v|0)==0){break}else{F=v;G=p}}if(G>>>0<(c[5298]|0)>>>0){za()}else{c[G>>2]=0;E=F;break}}else{p=c[a+h>>2]|0;if(p>>>0<(c[5298]|0)>>>0){za()}v=p+12|0;if((c[v>>2]|0)!=(b|0)){za()}m=t+8|0;if((c[m>>2]|0)==(b|0)){c[v>>2]=t;c[m>>2]=p;E=t;break}else{za()}}}while(0);if((f|0)==0){break}t=a+(h+20)|0;u=21480+(c[t>>2]<<2)|0;do{if((b|0)==(c[u>>2]|0)){c[u>>2]=E;if((E|0)!=0){break}c[5295]=c[5295]&~(1<<c[t>>2]);break b}else{if(f>>>0<(c[5298]|0)>>>0){za()}g=f+16|0;if((c[g>>2]|0)==(b|0)){c[g>>2]=E}else{c[f+20>>2]=E}if((E|0)==0){break b}}}while(0);if(E>>>0<(c[5298]|0)>>>0){za()}c[E+24>>2]=f;b=c[a+(h+8)>>2]|0;do{if((b|0)!=0){if(b>>>0<(c[5298]|0)>>>0){za()}else{c[E+16>>2]=b;c[b+24>>2]=E;break}}}while(0);b=c[a+(h+12)>>2]|0;if((b|0)==0){break}if(b>>>0<(c[5298]|0)>>>0){za()}else{c[E+20>>2]=b;c[b+24>>2]=E;break}}}while(0);c[q+4>>2]=B|1;c[d+B>>2]=B;if((q|0)!=(c[5299]|0)){H=B;break}c[5296]=B;return}else{c[A>>2]=e&-2;c[q+4>>2]=r|1;c[d+r>>2]=r;H=r}}while(0);r=H>>>3;if(H>>>0<256>>>0){d=r<<1;e=21216+(d<<2)|0;A=c[5294]|0;E=1<<r;do{if((A&E|0)==0){c[5294]=A|E;I=e;J=21216+(d+2<<2)|0}else{r=21216+(d+2<<2)|0;h=c[r>>2]|0;if(h>>>0>=(c[5298]|0)>>>0){I=h;J=r;break}za()}}while(0);c[J>>2]=q;c[I+12>>2]=q;c[q+8>>2]=I;c[q+12>>2]=e;return}e=q;I=H>>>8;do{if((I|0)==0){K=0}else{if(H>>>0>16777215>>>0){K=31;break}J=(I+1048320|0)>>>16&8;d=I<<J;E=(d+520192|0)>>>16&4;A=d<<E;d=(A+245760|0)>>>16&2;r=14-(E|J|d)+(A<<d>>>15)|0;K=H>>>((r+7|0)>>>0)&1|r<<1}}while(0);I=21480+(K<<2)|0;c[q+28>>2]=K;c[q+20>>2]=0;c[q+16>>2]=0;r=c[5295]|0;d=1<<K;c:do{if((r&d|0)==0){c[5295]=r|d;c[I>>2]=e;c[q+24>>2]=I;c[q+12>>2]=q;c[q+8>>2]=q}else{A=c[I>>2]|0;if((K|0)==31){L=0}else{L=25-(K>>>1)|0}d:do{if((c[A+4>>2]&-8|0)==(H|0)){M=A}else{J=A;E=H<<L;while(1){N=J+16+(E>>>31<<2)|0;h=c[N>>2]|0;if((h|0)==0){break}if((c[h+4>>2]&-8|0)==(H|0)){M=h;break d}else{J=h;E=E<<1}}if(N>>>0<(c[5298]|0)>>>0){za()}else{c[N>>2]=e;c[q+24>>2]=J;c[q+12>>2]=q;c[q+8>>2]=q;break c}}}while(0);A=M+8|0;B=c[A>>2]|0;E=c[5298]|0;if(M>>>0>=E>>>0&B>>>0>=E>>>0){c[B+12>>2]=e;c[A>>2]=e;c[q+8>>2]=B;c[q+12>>2]=M;c[q+24>>2]=0;break}else{za()}}}while(0);q=(c[5302]|0)-1|0;c[5302]=q;if((q|0)==0){O=21632}else{return}while(1){q=c[O>>2]|0;if((q|0)==0){break}else{O=q+8|0}}c[5302]=-1;return}function Mn(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0;if((a|0)==0){d=Kn(b)|0;return d|0}if(b>>>0>4294967231>>>0){c[(gb()|0)>>2]=12;d=0;return d|0}if(b>>>0<11>>>0){e=16}else{e=b+11&-8}f=Vn(a-8|0,e)|0;if((f|0)!=0){d=f+8|0;return d|0}f=Kn(b)|0;if((f|0)==0){d=0;return d|0}e=c[a-4>>2]|0;g=(e&-8)-((e&3|0)==0?8:4)|0;Yn(f|0,a|0,g>>>0<b>>>0?g:b)|0;Ln(a);d=f;return d|0}function Nn(b,e,f){b=b|0;e=e|0;f=f|0;var g=0,h=0,j=0,k=0,l=0.0,m=0,n=0,o=0,p=0,q=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,H=0,I=0,J=0,K=0,L=0,M=0,N=0,O=0,P=0,Q=0,S=0,T=0.0,U=0.0,V=0,W=0,X=0,Y=0,Z=0,_=0,$=0,aa=0,ba=0,da=0,ea=0,fa=0,ga=0,ha=0,ia=0.0,ja=0.0,ka=0,la=0,ma=0.0,na=0.0,oa=0,pa=0.0,qa=0,ra=0,sa=0,ta=0,ua=0,va=0,wa=0,xa=0.0,ya=0,za=0.0,Aa=0,Ba=0.0,Ca=0,Da=0,Ea=0,Fa=0,Ga=0.0,Ha=0,Ia=0.0,Ja=0,Ka=0,La=0,Ma=0,Na=0,Oa=0,Pa=0,Qa=0,Ra=0,Sa=0,Ua=0,Va=0,Wa=0,Ya=0,Za=0,_a=0,$a=0,ab=0,bb=0,cb=0,db=0,eb=0,fb=0,hb=0,ib=0,kb=0,lb=0,mb=0,nb=0,ob=0,pb=0,qb=0,rb=0,sb=0,tb=0,ub=0,vb=0,wb=0,xb=0,yb=0,zb=0,Ab=0,Bb=0,Cb=0,Db=0,Eb=0,Fb=0,Gb=0,Hb=0,Ib=0,Jb=0,Kb=0,Lb=0,Mb=0,Nb=0,Ob=0,Pb=0,Qb=0,Rb=0,Sb=0,Tb=0,Ub=0,Vb=0,Wb=0,Xb=0,Yb=0,Zb=0,_b=0,$b=0,ac=0,bc=0,cc=0,dc=0,ec=0,fc=0,gc=0,hc=0,ic=0,jc=0,kc=0,lc=0,mc=0,nc=0,oc=0,pc=0,qc=0,rc=0,sc=0,tc=0,uc=0,vc=0,wc=0,xc=0,yc=0,zc=0,Ac=0,Bc=0,Cc=0,Dc=0,Ec=0,Fc=0,Gc=0,Hc=0,Ic=0,Jc=0,Kc=0,Lc=0.0,Mc=0,Nc=0,Oc=0,Pc=0,Qc=0.0,Rc=0.0,Sc=0.0,Tc=0,Uc=0,Vc=0.0,Wc=0.0,Xc=0.0,Yc=0.0,Zc=0,_c=0,$c=0.0,ad=0,bd=0;g=i;i=i+512|0;h=g|0;if((e|0)==1){j=-1074;k=53}else if((e|0)==2){j=-1074;k=53}else if((e|0)==0){j=-149;k=24}else{l=0.0;i=g;return+l}e=b+4|0;m=b+100|0;do{n=c[e>>2]|0;if(n>>>0<(c[m>>2]|0)>>>0){c[e>>2]=n+1;o=d[n]|0}else{o=Pn(b)|0}}while((Ta(o|0)|0)!=0);do{if((o|0)==45|(o|0)==43){n=1-(((o|0)==45)<<1)|0;p=c[e>>2]|0;if(p>>>0<(c[m>>2]|0)>>>0){c[e>>2]=p+1;q=d[p]|0;t=n;break}else{q=Pn(b)|0;t=n;break}}else{q=o;t=1}}while(0);o=0;n=q;while(1){if((n|32|0)!=(a[10832+o|0]|0)){u=o;v=n;break}do{if(o>>>0<7>>>0){q=c[e>>2]|0;if(q>>>0<(c[m>>2]|0)>>>0){c[e>>2]=q+1;w=d[q]|0;break}else{w=Pn(b)|0;break}}else{w=n}}while(0);q=o+1|0;if(q>>>0<8>>>0){o=q;n=w}else{u=q;v=w;break}}do{if((u|0)==3){x=23}else if((u|0)!=8){w=(f|0)!=0;if(u>>>0>3>>>0&w){if((u|0)==8){break}else{x=23;break}}a:do{if((u|0)==0){n=0;o=v;while(1){if((o|32|0)!=(a[17808+n|0]|0)){y=o;z=n;break a}do{if(n>>>0<2>>>0){q=c[e>>2]|0;if(q>>>0<(c[m>>2]|0)>>>0){c[e>>2]=q+1;A=d[q]|0;break}else{A=Pn(b)|0;break}}else{A=o}}while(0);q=n+1|0;if(q>>>0<3>>>0){n=q;o=A}else{y=A;z=q;break}}}else{y=v;z=u}}while(0);if((z|0)==3){o=c[e>>2]|0;if(o>>>0<(c[m>>2]|0)>>>0){c[e>>2]=o+1;B=d[o]|0}else{B=Pn(b)|0}if((B|0)==40){C=1}else{if((c[m>>2]|0)==0){l=+r;i=g;return+l}c[e>>2]=(c[e>>2]|0)-1;l=+r;i=g;return+l}while(1){o=c[e>>2]|0;if(o>>>0<(c[m>>2]|0)>>>0){c[e>>2]=o+1;D=d[o]|0}else{D=Pn(b)|0}if(!((D-48|0)>>>0<10>>>0|(D-65|0)>>>0<26>>>0)){if(!((D-97|0)>>>0<26>>>0|(D|0)==95)){break}}C=C+1|0}if((D|0)==41){l=+r;i=g;return+l}o=(c[m>>2]|0)==0;if(!o){c[e>>2]=(c[e>>2]|0)-1}if(!w){c[(gb()|0)>>2]=22;On(b,0);l=0.0;i=g;return+l}if((C|0)==0|o){l=+r;i=g;return+l}else{E=C}while(1){o=E-1|0;c[e>>2]=(c[e>>2]|0)-1;if((o|0)==0){l=+r;break}else{E=o}}i=g;return+l}else if((z|0)==0){do{if((y|0)==48){w=c[e>>2]|0;if(w>>>0<(c[m>>2]|0)>>>0){c[e>>2]=w+1;F=d[w]|0}else{F=Pn(b)|0}if((F|32|0)!=120){if((c[m>>2]|0)==0){H=48;break}c[e>>2]=(c[e>>2]|0)-1;H=48;break}w=c[e>>2]|0;if(w>>>0<(c[m>>2]|0)>>>0){c[e>>2]=w+1;I=d[w]|0;J=0}else{I=Pn(b)|0;J=0}while(1){if((I|0)==46){x=70;break}else if((I|0)!=48){K=I;L=0;M=0;N=0;O=0;P=J;Q=0;S=0;T=1.0;U=0.0;V=0;break}w=c[e>>2]|0;if(w>>>0<(c[m>>2]|0)>>>0){c[e>>2]=w+1;I=d[w]|0;J=1;continue}else{I=Pn(b)|0;J=1;continue}}do{if((x|0)==70){w=c[e>>2]|0;if(w>>>0<(c[m>>2]|0)>>>0){c[e>>2]=w+1;W=d[w]|0}else{W=Pn(b)|0}if((W|0)==48){X=0;Y=0}else{K=W;L=0;M=0;N=0;O=0;P=J;Q=1;S=0;T=1.0;U=0.0;V=0;break}while(1){w=c[e>>2]|0;if(w>>>0<(c[m>>2]|0)>>>0){c[e>>2]=w+1;Z=d[w]|0}else{Z=Pn(b)|0}w=io(Y,X,-1,-1)|0;o=G;if((Z|0)==48){X=o;Y=w}else{K=Z;L=0;M=0;N=o;O=w;P=1;Q=1;S=0;T=1.0;U=0.0;V=0;break}}}}while(0);b:while(1){w=K-48|0;do{if(w>>>0<10>>>0){_=w;x=83}else{o=K|32;n=(K|0)==46;if(!((o-97|0)>>>0<6>>>0|n)){$=K;break b}if(n){if((Q|0)==0){aa=L;ba=M;da=L;ea=M;fa=P;ga=1;ha=S;ia=T;ja=U;ka=V;break}else{$=46;break b}}else{_=(K|0)>57?o-87|0:w;x=83;break}}}while(0);if((x|0)==83){x=0;w=0;do{if((L|0)<(w|0)|(L|0)==(w|0)&M>>>0<8>>>0){la=S;ma=T;na=U;oa=_+(V<<4)|0}else{o=0;if((L|0)<(o|0)|(L|0)==(o|0)&M>>>0<14>>>0){pa=T*.0625;la=S;ma=pa;na=U+pa*+(_|0);oa=V;break}if((_|0)==0|(S|0)!=0){la=S;ma=T;na=U;oa=V;break}la=1;ma=T;na=U+T*.5;oa=V}}while(0);w=io(M,L,1,0)|0;aa=G;ba=w;da=N;ea=O;fa=1;ga=Q;ha=la;ia=ma;ja=na;ka=oa}w=c[e>>2]|0;if(w>>>0<(c[m>>2]|0)>>>0){c[e>>2]=w+1;K=d[w]|0;L=aa;M=ba;N=da;O=ea;P=fa;Q=ga;S=ha;T=ia;U=ja;V=ka;continue}else{K=Pn(b)|0;L=aa;M=ba;N=da;O=ea;P=fa;Q=ga;S=ha;T=ia;U=ja;V=ka;continue}}if((P|0)==0){w=(c[m>>2]|0)==0;if(!w){c[e>>2]=(c[e>>2]|0)-1}do{if((f|0)==0){On(b,0)}else{if(w){break}o=c[e>>2]|0;c[e>>2]=o-1;if((Q|0)==0){break}c[e>>2]=o-2}}while(0);l=+(t|0)*0.0;i=g;return+l}w=(Q|0)==0;o=w?M:O;n=w?L:N;w=0;if((L|0)<(w|0)|(L|0)==(w|0)&M>>>0<8>>>0){w=V;q=L;p=M;while(1){qa=w<<4;ra=io(p,q,1,0)|0;sa=G;ta=0;if((sa|0)<(ta|0)|(sa|0)==(ta|0)&ra>>>0<8>>>0){w=qa;q=sa;p=ra}else{ua=qa;break}}}else{ua=V}do{if(($|32|0)==112){p=Xn(b,f)|0;q=G;if(!((p|0)==0&(q|0)==(-2147483648|0))){va=q;wa=p;break}if((f|0)==0){On(b,0);l=0.0;i=g;return+l}else{if((c[m>>2]|0)==0){va=0;wa=0;break}c[e>>2]=(c[e>>2]|0)-1;va=0;wa=0;break}}else{if((c[m>>2]|0)==0){va=0;wa=0;break}c[e>>2]=(c[e>>2]|0)-1;va=0;wa=0}}while(0);p=io(o<<2|0>>>30,n<<2|o>>>30,-32,-1)|0;q=io(p,G,wa,va)|0;p=G;if((ua|0)==0){l=+(t|0)*0.0;i=g;return+l}w=0;if((p|0)>(w|0)|(p|0)==(w|0)&q>>>0>(-j|0)>>>0){c[(gb()|0)>>2]=34;l=+(t|0)*1.7976931348623157e+308*1.7976931348623157e+308;i=g;return+l}w=j-106|0;qa=(w|0)<0|0?-1:0;if((p|0)<(qa|0)|(p|0)==(qa|0)&q>>>0<w>>>0){c[(gb()|0)>>2]=34;l=+(t|0)*2.2250738585072014e-308*2.2250738585072014e-308;i=g;return+l}if((ua|0)>-1){w=ua;pa=U;qa=p;ra=q;while(1){sa=w<<1;if(pa<.5){xa=pa;ya=sa}else{xa=pa+-1.0;ya=sa|1}za=pa+xa;sa=io(ra,qa,-1,-1)|0;ta=G;if((ya|0)>-1){w=ya;pa=za;qa=ta;ra=sa}else{Aa=ya;Ba=za;Ca=ta;Da=sa;break}}}else{Aa=ua;Ba=U;Ca=p;Da=q}ra=0;qa=jo(32,0,j,(j|0)<0|0?-1:0)|0;w=io(Da,Ca,qa,G)|0;qa=G;if((ra|0)>(qa|0)|(ra|0)==(qa|0)&k>>>0>w>>>0){qa=w;if((qa|0)<0){Ea=0;x=126}else{Fa=qa;x=124}}else{Fa=k;x=124}do{if((x|0)==124){if((Fa|0)<53){Ea=Fa;x=126;break}Ga=0.0;Ha=Fa;Ia=+(t|0)}}while(0);if((x|0)==126){pa=+(t|0);Ga=+Xa(+(+Qn(1.0,84-Ea|0)),+pa);Ha=Ea;Ia=pa}q=(Ha|0)<32&Ba!=0.0&(Aa&1|0)==0;pa=Ia*(q?0.0:Ba)+(Ga+Ia*+(((q&1)+Aa|0)>>>0>>>0))-Ga;if(pa==0.0){c[(gb()|0)>>2]=34}l=+Rn(pa,Da);i=g;return+l}else{H=y}}while(0);q=j+k|0;p=-q|0;qa=H;w=0;while(1){if((qa|0)==46){x=137;break}else if((qa|0)!=48){Ja=qa;Ka=0;La=w;Ma=0;Na=0;break}ra=c[e>>2]|0;if(ra>>>0<(c[m>>2]|0)>>>0){c[e>>2]=ra+1;qa=d[ra]|0;w=1;continue}else{qa=Pn(b)|0;w=1;continue}}do{if((x|0)==137){qa=c[e>>2]|0;if(qa>>>0<(c[m>>2]|0)>>>0){c[e>>2]=qa+1;Oa=d[qa]|0}else{Oa=Pn(b)|0}if((Oa|0)==48){Pa=0;Qa=0}else{Ja=Oa;Ka=1;La=w;Ma=0;Na=0;break}while(1){qa=io(Qa,Pa,-1,-1)|0;ra=G;o=c[e>>2]|0;if(o>>>0<(c[m>>2]|0)>>>0){c[e>>2]=o+1;Ra=d[o]|0}else{Ra=Pn(b)|0}if((Ra|0)==48){Pa=ra;Qa=qa}else{Ja=Ra;Ka=1;La=1;Ma=ra;Na=qa;break}}}}while(0);w=h|0;c[w>>2]=0;qa=Ja-48|0;ra=(Ja|0)==46;c:do{if(qa>>>0<10>>>0|ra){o=h+496|0;n=Ma;sa=Na;ta=0;Sa=0;Ua=0;Va=La;Wa=Ka;Ya=0;Za=0;_a=Ja;$a=qa;ab=ra;d:while(1){do{if(ab){if((Wa|0)==0){bb=Za;cb=Ya;db=1;eb=Va;fb=Ua;hb=ta;ib=Sa;kb=ta;lb=Sa}else{break d}}else{mb=io(Sa,ta,1,0)|0;nb=G;ob=(_a|0)!=48;if((Ya|0)>=125){if(!ob){bb=Za;cb=Ya;db=Wa;eb=Va;fb=Ua;hb=nb;ib=mb;kb=n;lb=sa;break}c[o>>2]=c[o>>2]|1;bb=Za;cb=Ya;db=Wa;eb=Va;fb=Ua;hb=nb;ib=mb;kb=n;lb=sa;break}pb=h+(Ya<<2)|0;if((Za|0)==0){qb=$a}else{qb=_a-48+((c[pb>>2]|0)*10|0)|0}c[pb>>2]=qb;pb=Za+1|0;rb=(pb|0)==9;bb=rb?0:pb;cb=(rb&1)+Ya|0;db=Wa;eb=1;fb=ob?mb:Ua;hb=nb;ib=mb;kb=n;lb=sa}}while(0);mb=c[e>>2]|0;if(mb>>>0<(c[m>>2]|0)>>>0){c[e>>2]=mb+1;sb=d[mb]|0}else{sb=Pn(b)|0}mb=sb-48|0;nb=(sb|0)==46;if(mb>>>0<10>>>0|nb){n=kb;sa=lb;ta=hb;Sa=ib;Ua=fb;Va=eb;Wa=db;Ya=cb;Za=bb;_a=sb;$a=mb;ab=nb}else{tb=kb;ub=lb;vb=hb;wb=ib;xb=fb;yb=eb;zb=db;Ab=cb;Bb=bb;Cb=sb;x=160;break c}}Db=(Va|0)!=0;Eb=n;Fb=sa;Gb=ta;Hb=Sa;Ib=Ua;Jb=Ya;Kb=Za;x=168}else{tb=Ma;ub=Na;vb=0;wb=0;xb=0;yb=La;zb=Ka;Ab=0;Bb=0;Cb=Ja;x=160}}while(0);do{if((x|0)==160){ra=(zb|0)==0;qa=ra?wb:ub;ab=ra?vb:tb;ra=(yb|0)!=0;if(!(ra&(Cb|32|0)==101)){if((Cb|0)>-1){Db=ra;Eb=ab;Fb=qa;Gb=vb;Hb=wb;Ib=xb;Jb=Ab;Kb=Bb;x=168;break}else{Lb=ab;Mb=qa;Nb=ra;Ob=vb;Pb=wb;Qb=xb;Rb=Ab;Sb=Bb;x=170;break}}ra=Xn(b,f)|0;$a=G;do{if((ra|0)==0&($a|0)==(-2147483648|0)){if((f|0)==0){On(b,0);l=0.0;i=g;return+l}else{if((c[m>>2]|0)==0){Tb=0;Ub=0;break}c[e>>2]=(c[e>>2]|0)-1;Tb=0;Ub=0;break}}else{Tb=$a;Ub=ra}}while(0);ra=io(Ub,Tb,qa,ab)|0;Vb=G;Wb=ra;Xb=vb;Yb=wb;Zb=xb;_b=Ab;$b=Bb}}while(0);do{if((x|0)==168){if((c[m>>2]|0)==0){Lb=Eb;Mb=Fb;Nb=Db;Ob=Gb;Pb=Hb;Qb=Ib;Rb=Jb;Sb=Kb;x=170;break}c[e>>2]=(c[e>>2]|0)-1;if(Db){Vb=Eb;Wb=Fb;Xb=Gb;Yb=Hb;Zb=Ib;_b=Jb;$b=Kb}else{x=171}}}while(0);if((x|0)==170){if(Nb){Vb=Lb;Wb=Mb;Xb=Ob;Yb=Pb;Zb=Qb;_b=Rb;$b=Sb}else{x=171}}if((x|0)==171){c[(gb()|0)>>2]=22;On(b,0);l=0.0;i=g;return+l}ra=c[w>>2]|0;if((ra|0)==0){l=+(t|0)*0.0;i=g;return+l}$a=0;do{if((Wb|0)==(Yb|0)&(Vb|0)==(Xb|0)&((Xb|0)<($a|0)|(Xb|0)==($a|0)&Yb>>>0<10>>>0)){if(!(k>>>0>30>>>0|(ra>>>(k>>>0)|0)==0)){break}l=+(t|0)*+(ra>>>0>>>0);i=g;return+l}}while(0);ra=(j|0)/-2|0;$a=(ra|0)<0|0?-1:0;if((Vb|0)>($a|0)|(Vb|0)==($a|0)&Wb>>>0>ra>>>0){c[(gb()|0)>>2]=34;l=+(t|0)*1.7976931348623157e+308*1.7976931348623157e+308;i=g;return+l}ra=j-106|0;$a=(ra|0)<0|0?-1:0;if((Vb|0)<($a|0)|(Vb|0)==($a|0)&Wb>>>0<ra>>>0){c[(gb()|0)>>2]=34;l=+(t|0)*2.2250738585072014e-308*2.2250738585072014e-308;i=g;return+l}if(($b|0)==0){ac=_b}else{if(($b|0)<9){ra=h+(_b<<2)|0;$a=$b;Za=c[ra>>2]|0;do{Za=Za*10|0;$a=$a+1|0;}while(($a|0)<9);c[ra>>2]=Za}ac=_b+1|0}$a=Wb;do{if((Zb|0)<9){if(!((Zb|0)<=($a|0)&($a|0)<18)){break}if(($a|0)==9){l=+(t|0)*+((c[w>>2]|0)>>>0>>>0);i=g;return+l}if(($a|0)<9){l=+(t|0)*+((c[w>>2]|0)>>>0>>>0)/+(c[7544+(8-$a<<2)>>2]|0);i=g;return+l}Ya=k+27+($a*-3|0)|0;Ua=c[w>>2]|0;if(!((Ya|0)>30|(Ua>>>(Ya>>>0)|0)==0)){break}l=+(t|0)*+(Ua>>>0>>>0)*+(c[7544+($a-10<<2)>>2]|0);i=g;return+l}}while(0);w=($a|0)%9|0;if((w|0)==0){bc=0;cc=ac;dc=0;ec=$a}else{Za=($a|0)>-1?w:w+9|0;w=c[7544+(8-Za<<2)>>2]|0;do{if((ac|0)==0){fc=0;gc=0;hc=$a}else{ra=1e9/(w|0)|0;Ua=$a;Ya=0;Sa=0;ta=0;while(1){sa=h+(Sa<<2)|0;n=c[sa>>2]|0;Va=((n>>>0)/(w>>>0)|0)+ta|0;c[sa>>2]=Va;ic=ca((n>>>0)%(w>>>0)|0,ra)|0;n=Sa+1|0;if((Sa|0)==(Ya|0)&(Va|0)==0){jc=n&127;kc=Ua-9|0}else{jc=Ya;kc=Ua}if((n|0)==(ac|0)){break}else{Ua=kc;Ya=jc;Sa=n;ta=ic}}if((ic|0)==0){fc=ac;gc=jc;hc=kc;break}c[h+(ac<<2)>>2]=ic;fc=ac+1|0;gc=jc;hc=kc}}while(0);bc=gc;cc=fc;dc=0;ec=9-Za+hc|0}e:while(1){w=h+(bc<<2)|0;if((ec|0)<18){$a=cc;ta=dc;while(1){Sa=0;Ya=$a+127|0;Ua=$a;while(1){ra=Ya&127;ab=h+(ra<<2)|0;qa=c[ab>>2]|0;n=io(qa<<29|0>>>3,0<<29|qa>>>3,Sa,0)|0;qa=G;Va=0;if(qa>>>0>Va>>>0|qa>>>0==Va>>>0&n>>>0>1e9>>>0){Va=to(n,qa,1e9,0)|0;sa=uo(n,qa,1e9,0)|0;lc=Va;mc=sa}else{lc=0;mc=n}c[ab>>2]=mc;ab=(ra|0)==(bc|0);if((ra|0)!=(Ua+127&127|0)|ab){nc=Ua}else{nc=(mc|0)==0?ra:Ua}if(ab){break}else{Sa=lc;Ya=ra-1|0;Ua=nc}}Ua=ta-29|0;if((lc|0)==0){$a=nc;ta=Ua}else{oc=Ua;pc=nc;qc=lc;break}}}else{if((ec|0)==18){rc=cc;sc=dc}else{tc=bc;uc=cc;vc=dc;wc=ec;break}while(1){if((c[w>>2]|0)>>>0>=9007199>>>0){tc=bc;uc=rc;vc=sc;wc=18;break e}ta=0;$a=rc+127|0;Ua=rc;while(1){Ya=$a&127;Sa=h+(Ya<<2)|0;ra=c[Sa>>2]|0;ab=io(ra<<29|0>>>3,0<<29|ra>>>3,ta,0)|0;ra=G;n=0;if(ra>>>0>n>>>0|ra>>>0==n>>>0&ab>>>0>1e9>>>0){n=to(ab,ra,1e9,0)|0;sa=uo(ab,ra,1e9,0)|0;xc=n;yc=sa}else{xc=0;yc=ab}c[Sa>>2]=yc;Sa=(Ya|0)==(bc|0);if((Ya|0)!=(Ua+127&127|0)|Sa){zc=Ua}else{zc=(yc|0)==0?Ya:Ua}if(Sa){break}else{ta=xc;$a=Ya-1|0;Ua=zc}}Ua=sc-29|0;if((xc|0)==0){rc=zc;sc=Ua}else{oc=Ua;pc=zc;qc=xc;break}}}w=bc+127&127;if((w|0)==(pc|0)){Ua=pc+127&127;$a=h+((pc+126&127)<<2)|0;c[$a>>2]=c[$a>>2]|c[h+(Ua<<2)>>2];Ac=Ua}else{Ac=pc}c[h+(w<<2)>>2]=qc;bc=w;cc=Ac;dc=oc;ec=ec+9|0}f:while(1){Bc=uc+1&127;Za=h+((uc+127&127)<<2)|0;w=tc;Ua=vc;$a=wc;while(1){ta=($a|0)==18;Ya=($a|0)>27?9:1;Cc=w;Dc=Ua;while(1){Sa=0;while(1){ab=Sa+Cc&127;if((ab|0)==(uc|0)){Ec=2;break}sa=c[h+(ab<<2)>>2]|0;ab=c[7536+(Sa<<2)>>2]|0;if(sa>>>0<ab>>>0){Ec=2;break}n=Sa+1|0;if(sa>>>0>ab>>>0){Ec=Sa;break}if((n|0)<2){Sa=n}else{Ec=n;break}}if((Ec|0)==2&ta){break f}Fc=Ya+Dc|0;if((Cc|0)==(uc|0)){Cc=uc;Dc=Fc}else{break}}ta=(1<<Ya)-1|0;Sa=1e9>>>(Ya>>>0);Gc=$a;Hc=Cc;n=Cc;Ic=0;do{ab=h+(n<<2)|0;sa=c[ab>>2]|0;ra=(sa>>>(Ya>>>0))+Ic|0;c[ab>>2]=ra;Ic=ca(sa&ta,Sa)|0;sa=(n|0)==(Hc|0)&(ra|0)==0;n=n+1&127;Gc=sa?Gc-9|0:Gc;Hc=sa?n:Hc;}while((n|0)!=(uc|0));if((Ic|0)==0){w=Hc;Ua=Fc;$a=Gc;continue}if((Bc|0)!=(Hc|0)){break}c[Za>>2]=c[Za>>2]|1;w=Hc;Ua=Fc;$a=Gc}c[h+(uc<<2)>>2]=Ic;tc=Hc;uc=Bc;vc=Fc;wc=Gc}$a=Cc&127;if(($a|0)==(uc|0)){c[h+(Bc-1<<2)>>2]=0;Jc=Bc}else{Jc=uc}pa=+((c[h+($a<<2)>>2]|0)>>>0>>>0);$a=Cc+1&127;if(($a|0)==(Jc|0)){Ua=Jc+1&127;c[h+(Ua-1<<2)>>2]=0;Kc=Ua}else{Kc=Jc}za=+(t|0);Lc=za*(pa*1.0e9+ +((c[h+($a<<2)>>2]|0)>>>0>>>0));$a=Dc+53|0;Ua=$a-j|0;if((Ua|0)<(k|0)){if((Ua|0)<0){Mc=1;Nc=0;x=244}else{Oc=Ua;Pc=1;x=243}}else{Oc=k;Pc=0;x=243}if((x|0)==243){if((Oc|0)<53){Mc=Pc;Nc=Oc;x=244}else{Qc=0.0;Rc=0.0;Sc=Lc;Tc=Pc;Uc=Oc}}if((x|0)==244){pa=+Xa(+(+Qn(1.0,105-Nc|0)),+Lc);Vc=+jb(+Lc,+(+Qn(1.0,53-Nc|0)));Qc=pa;Rc=Vc;Sc=pa+(Lc-Vc);Tc=Mc;Uc=Nc}w=Cc+2&127;do{if((w|0)==(Kc|0)){Wc=Rc}else{Za=c[h+(w<<2)>>2]|0;do{if(Za>>>0<5e8>>>0){if((Za|0)==0){if((Cc+3&127|0)==(Kc|0)){Xc=Rc;break}}Xc=za*.25+Rc}else{if(Za>>>0>5e8>>>0){Xc=za*.75+Rc;break}if((Cc+3&127|0)==(Kc|0)){Xc=za*.5+Rc;break}else{Xc=za*.75+Rc;break}}}while(0);if((53-Uc|0)<=1){Wc=Xc;break}if(+jb(+Xc,+1.0)!=0.0){Wc=Xc;break}Wc=Xc+1.0}}while(0);za=Sc+Wc-Qc;do{if(($a&2147483647|0)>(-2-q|0)){if(+R(+za)<9007199254740992.0){Yc=za;Zc=Tc;_c=Dc}else{Yc=za*.5;Zc=(Tc|0)!=0&(Uc|0)==(Ua|0)?0:Tc;_c=Dc+1|0}if((_c+50|0)<=(p|0)){if(!((Zc|0)!=0&Wc!=0.0)){$c=Yc;ad=_c;break}}c[(gb()|0)>>2]=34;$c=Yc;ad=_c}else{$c=za;ad=Dc}}while(0);l=+Rn($c,ad);i=g;return+l}else{if((c[m>>2]|0)!=0){c[e>>2]=(c[e>>2]|0)-1}c[(gb()|0)>>2]=22;On(b,0);l=0.0;i=g;return+l}}}while(0);do{if((x|0)==23){b=(c[m>>2]|0)==0;if(!b){c[e>>2]=(c[e>>2]|0)-1}if(u>>>0<4>>>0|(f|0)==0|b){break}else{bd=u}do{c[e>>2]=(c[e>>2]|0)-1;bd=bd-1|0;}while(bd>>>0>3>>>0)}}while(0);l=+(t|0)*s;i=g;return+l}function On(a,b){a=a|0;b=b|0;var d=0,e=0,f=0;c[a+104>>2]=b;d=c[a+8>>2]|0;e=c[a+4>>2]|0;f=d-e|0;c[a+108>>2]=f;if((b|0)!=0&(f|0)>(b|0)){c[a+100>>2]=e+b;return}else{c[a+100>>2]=d;return}}function Pn(b){b=b|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0;e=b+104|0;f=c[e>>2]|0;if((f|0)==0){g=3}else{if((c[b+108>>2]|0)<(f|0)){g=3}}do{if((g|0)==3){f=Tn(b)|0;if((f|0)<0){break}h=c[e>>2]|0;i=c[b+8>>2]|0;do{if((h|0)==0){g=8}else{j=c[b+4>>2]|0;k=h-(c[b+108>>2]|0)-1|0;if((i-j|0)<=(k|0)){g=8;break}c[b+100>>2]=j+k}}while(0);if((g|0)==8){c[b+100>>2]=i}h=c[b+4>>2]|0;if((i|0)!=0){k=b+108|0;c[k>>2]=i+1-h+(c[k>>2]|0)}k=h-1|0;if((d[k]|0|0)==(f|0)){l=f;return l|0}a[k]=f;l=f;return l|0}}while(0);c[b+100>>2]=0;l=-1;return l|0}function Qn(a,b){a=+a;b=b|0;var d=0.0,e=0,f=0.0,g=0;do{if((b|0)>1023){d=a*8.98846567431158e+307;e=b-1023|0;if((e|0)<=1023){f=d;g=e;break}e=b-2046|0;f=d*8.98846567431158e+307;g=(e|0)>1023?1023:e}else{if((b|0)>=-1022){f=a;g=b;break}d=a*2.2250738585072014e-308;e=b+1022|0;if((e|0)>=-1022){f=d;g=e;break}e=b+2044|0;f=d*2.2250738585072014e-308;g=(e|0)<-1022?-1022:e}}while(0);return+(f*(c[k>>2]=0<<20|0>>>12,c[k+4>>2]=g+1023<<20|0>>>12,+h[k>>3]))}function Rn(a,b){a=+a;b=b|0;return+(+Qn(a,b))}function Sn(b){b=b|0;var d=0,e=0,f=0,g=0,h=0;d=b+74|0;e=a[d]|0;a[d]=e-1&255|e;e=b+20|0;d=b+44|0;if((c[e>>2]|0)>>>0>(c[d>>2]|0)>>>0){tb[c[b+36>>2]&15](b,0,0)|0}c[b+16>>2]=0;c[b+28>>2]=0;c[e>>2]=0;e=b|0;f=c[e>>2]|0;if((f&20|0)==0){g=c[d>>2]|0;c[b+8>>2]=g;c[b+4>>2]=g;h=0;return h|0}if((f&4|0)==0){h=-1;return h|0}c[e>>2]=f|32;h=-1;return h|0}function Tn(a){a=a|0;var b=0,e=0,f=0,g=0;b=i;i=i+8|0;e=b|0;if((c[a+8>>2]|0)==0){if((Sn(a)|0)==0){f=3}else{g=-1}}else{f=3}do{if((f|0)==3){if((tb[c[a+32>>2]&15](a,e,1)|0)!=1){g=-1;break}g=d[e]|0}}while(0);i=b;return g|0}function Un(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0.0,j=0,k=0,l=0,m=0;d=i;i=i+112|0;e=d|0;fo(e|0,0,112)|0;f=e+4|0;c[f>>2]=a;g=e+8|0;c[g>>2]=-1;c[e+44>>2]=a;c[e+76>>2]=-1;On(e,0);h=+Nn(e,1,1);j=(c[f>>2]|0)-(c[g>>2]|0)+(c[e+108>>2]|0)|0;if((b|0)==0){k=112;l=0;i=d;return+h}if((j|0)==0){m=a}else{m=a+j|0}c[b>>2]=m;k=112;l=0;i=d;return+h}function Vn(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0;d=a+4|0;e=c[d>>2]|0;f=e&-8;g=a;h=g+f|0;i=h;j=c[5298]|0;k=e&3;if(!((k|0)!=1&g>>>0>=j>>>0&g>>>0<h>>>0)){za();return 0}l=g+(f|4)|0;m=c[l>>2]|0;if((m&1|0)==0){za();return 0}if((k|0)==0){if(b>>>0<256>>>0){n=0;return n|0}do{if(f>>>0>=(b+4|0)>>>0){if((f-b|0)>>>0>c[5278]<<1>>>0){break}else{n=a}return n|0}}while(0);n=0;return n|0}if(f>>>0>=b>>>0){k=f-b|0;if(k>>>0<=15>>>0){n=a;return n|0}c[d>>2]=e&1|b|2;c[g+(b+4)>>2]=k|3;c[l>>2]=c[l>>2]|1;Wn(g+b|0,k);n=a;return n|0}if((i|0)==(c[5300]|0)){k=(c[5297]|0)+f|0;if(k>>>0<=b>>>0){n=0;return n|0}l=k-b|0;c[d>>2]=e&1|b|2;c[g+(b+4)>>2]=l|1;c[5300]=g+b;c[5297]=l;n=a;return n|0}if((i|0)==(c[5299]|0)){l=(c[5296]|0)+f|0;if(l>>>0<b>>>0){n=0;return n|0}k=l-b|0;if(k>>>0>15>>>0){c[d>>2]=e&1|b|2;c[g+(b+4)>>2]=k|1;c[g+l>>2]=k;o=g+(l+4)|0;c[o>>2]=c[o>>2]&-2;p=g+b|0;q=k}else{c[d>>2]=e&1|l|2;e=g+(l+4)|0;c[e>>2]=c[e>>2]|1;p=0;q=0}c[5296]=q;c[5299]=p;n=a;return n|0}if((m&2|0)!=0){n=0;return n|0}p=(m&-8)+f|0;if(p>>>0<b>>>0){n=0;return n|0}q=p-b|0;e=m>>>3;a:do{if(m>>>0<256>>>0){l=c[g+(f+8)>>2]|0;k=c[g+(f+12)>>2]|0;o=21216+(e<<1<<2)|0;do{if((l|0)!=(o|0)){if(l>>>0<j>>>0){za();return 0}if((c[l+12>>2]|0)==(i|0)){break}za();return 0}}while(0);if((k|0)==(l|0)){c[5294]=c[5294]&~(1<<e);break}do{if((k|0)==(o|0)){r=k+8|0}else{if(k>>>0<j>>>0){za();return 0}s=k+8|0;if((c[s>>2]|0)==(i|0)){r=s;break}za();return 0}}while(0);c[l+12>>2]=k;c[r>>2]=l}else{o=h;s=c[g+(f+24)>>2]|0;t=c[g+(f+12)>>2]|0;do{if((t|0)==(o|0)){u=g+(f+20)|0;v=c[u>>2]|0;if((v|0)==0){w=g+(f+16)|0;x=c[w>>2]|0;if((x|0)==0){y=0;break}else{z=x;A=w}}else{z=v;A=u}while(1){u=z+20|0;v=c[u>>2]|0;if((v|0)!=0){z=v;A=u;continue}u=z+16|0;v=c[u>>2]|0;if((v|0)==0){break}else{z=v;A=u}}if(A>>>0<j>>>0){za();return 0}else{c[A>>2]=0;y=z;break}}else{u=c[g+(f+8)>>2]|0;if(u>>>0<j>>>0){za();return 0}v=u+12|0;if((c[v>>2]|0)!=(o|0)){za();return 0}w=t+8|0;if((c[w>>2]|0)==(o|0)){c[v>>2]=t;c[w>>2]=u;y=t;break}else{za();return 0}}}while(0);if((s|0)==0){break}t=g+(f+28)|0;l=21480+(c[t>>2]<<2)|0;do{if((o|0)==(c[l>>2]|0)){c[l>>2]=y;if((y|0)!=0){break}c[5295]=c[5295]&~(1<<c[t>>2]);break a}else{if(s>>>0<(c[5298]|0)>>>0){za();return 0}k=s+16|0;if((c[k>>2]|0)==(o|0)){c[k>>2]=y}else{c[s+20>>2]=y}if((y|0)==0){break a}}}while(0);if(y>>>0<(c[5298]|0)>>>0){za();return 0}c[y+24>>2]=s;o=c[g+(f+16)>>2]|0;do{if((o|0)!=0){if(o>>>0<(c[5298]|0)>>>0){za();return 0}else{c[y+16>>2]=o;c[o+24>>2]=y;break}}}while(0);o=c[g+(f+20)>>2]|0;if((o|0)==0){break}if(o>>>0<(c[5298]|0)>>>0){za();return 0}else{c[y+20>>2]=o;c[o+24>>2]=y;break}}}while(0);if(q>>>0<16>>>0){c[d>>2]=p|c[d>>2]&1|2;y=g+(p|4)|0;c[y>>2]=c[y>>2]|1;n=a;return n|0}else{c[d>>2]=c[d>>2]&1|b|2;c[g+(b+4)>>2]=q|3;d=g+(p|4)|0;c[d>>2]=c[d>>2]|1;Wn(g+b|0,q);n=a;return n|0}return 0}function Wn(a,b){a=a|0;b=b|0;var d=0,e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,G=0,H=0,I=0,J=0,K=0,L=0;d=a;e=d+b|0;f=e;g=c[a+4>>2]|0;a:do{if((g&1|0)==0){h=c[a>>2]|0;if((g&3|0)==0){return}i=d+(-h|0)|0;j=i;k=h+b|0;l=c[5298]|0;if(i>>>0<l>>>0){za()}if((j|0)==(c[5299]|0)){m=d+(b+4)|0;if((c[m>>2]&3|0)!=3){n=j;o=k;break}c[5296]=k;c[m>>2]=c[m>>2]&-2;c[d+(4-h)>>2]=k|1;c[e>>2]=k;return}m=h>>>3;if(h>>>0<256>>>0){p=c[d+(8-h)>>2]|0;q=c[d+(12-h)>>2]|0;r=21216+(m<<1<<2)|0;do{if((p|0)!=(r|0)){if(p>>>0<l>>>0){za()}if((c[p+12>>2]|0)==(j|0)){break}za()}}while(0);if((q|0)==(p|0)){c[5294]=c[5294]&~(1<<m);n=j;o=k;break}do{if((q|0)==(r|0)){s=q+8|0}else{if(q>>>0<l>>>0){za()}t=q+8|0;if((c[t>>2]|0)==(j|0)){s=t;break}za()}}while(0);c[p+12>>2]=q;c[s>>2]=p;n=j;o=k;break}r=i;m=c[d+(24-h)>>2]|0;t=c[d+(12-h)>>2]|0;do{if((t|0)==(r|0)){u=16-h|0;v=d+(u+4)|0;w=c[v>>2]|0;if((w|0)==0){x=d+u|0;u=c[x>>2]|0;if((u|0)==0){y=0;break}else{z=u;A=x}}else{z=w;A=v}while(1){v=z+20|0;w=c[v>>2]|0;if((w|0)!=0){z=w;A=v;continue}v=z+16|0;w=c[v>>2]|0;if((w|0)==0){break}else{z=w;A=v}}if(A>>>0<l>>>0){za()}else{c[A>>2]=0;y=z;break}}else{v=c[d+(8-h)>>2]|0;if(v>>>0<l>>>0){za()}w=v+12|0;if((c[w>>2]|0)!=(r|0)){za()}x=t+8|0;if((c[x>>2]|0)==(r|0)){c[w>>2]=t;c[x>>2]=v;y=t;break}else{za()}}}while(0);if((m|0)==0){n=j;o=k;break}t=d+(28-h)|0;l=21480+(c[t>>2]<<2)|0;do{if((r|0)==(c[l>>2]|0)){c[l>>2]=y;if((y|0)!=0){break}c[5295]=c[5295]&~(1<<c[t>>2]);n=j;o=k;break a}else{if(m>>>0<(c[5298]|0)>>>0){za()}i=m+16|0;if((c[i>>2]|0)==(r|0)){c[i>>2]=y}else{c[m+20>>2]=y}if((y|0)==0){n=j;o=k;break a}}}while(0);if(y>>>0<(c[5298]|0)>>>0){za()}c[y+24>>2]=m;r=16-h|0;t=c[d+r>>2]|0;do{if((t|0)!=0){if(t>>>0<(c[5298]|0)>>>0){za()}else{c[y+16>>2]=t;c[t+24>>2]=y;break}}}while(0);t=c[d+(r+4)>>2]|0;if((t|0)==0){n=j;o=k;break}if(t>>>0<(c[5298]|0)>>>0){za()}else{c[y+20>>2]=t;c[t+24>>2]=y;n=j;o=k;break}}else{n=a;o=b}}while(0);a=c[5298]|0;if(e>>>0<a>>>0){za()}y=d+(b+4)|0;z=c[y>>2]|0;do{if((z&2|0)==0){if((f|0)==(c[5300]|0)){A=(c[5297]|0)+o|0;c[5297]=A;c[5300]=n;c[n+4>>2]=A|1;if((n|0)!=(c[5299]|0)){return}c[5299]=0;c[5296]=0;return}if((f|0)==(c[5299]|0)){A=(c[5296]|0)+o|0;c[5296]=A;c[5299]=n;c[n+4>>2]=A|1;c[n+A>>2]=A;return}A=(z&-8)+o|0;s=z>>>3;b:do{if(z>>>0<256>>>0){g=c[d+(b+8)>>2]|0;t=c[d+(b+12)>>2]|0;h=21216+(s<<1<<2)|0;do{if((g|0)!=(h|0)){if(g>>>0<a>>>0){za()}if((c[g+12>>2]|0)==(f|0)){break}za()}}while(0);if((t|0)==(g|0)){c[5294]=c[5294]&~(1<<s);break}do{if((t|0)==(h|0)){B=t+8|0}else{if(t>>>0<a>>>0){za()}m=t+8|0;if((c[m>>2]|0)==(f|0)){B=m;break}za()}}while(0);c[g+12>>2]=t;c[B>>2]=g}else{h=e;m=c[d+(b+24)>>2]|0;l=c[d+(b+12)>>2]|0;do{if((l|0)==(h|0)){i=d+(b+20)|0;p=c[i>>2]|0;if((p|0)==0){q=d+(b+16)|0;v=c[q>>2]|0;if((v|0)==0){C=0;break}else{D=v;E=q}}else{D=p;E=i}while(1){i=D+20|0;p=c[i>>2]|0;if((p|0)!=0){D=p;E=i;continue}i=D+16|0;p=c[i>>2]|0;if((p|0)==0){break}else{D=p;E=i}}if(E>>>0<a>>>0){za()}else{c[E>>2]=0;C=D;break}}else{i=c[d+(b+8)>>2]|0;if(i>>>0<a>>>0){za()}p=i+12|0;if((c[p>>2]|0)!=(h|0)){za()}q=l+8|0;if((c[q>>2]|0)==(h|0)){c[p>>2]=l;c[q>>2]=i;C=l;break}else{za()}}}while(0);if((m|0)==0){break}l=d+(b+28)|0;g=21480+(c[l>>2]<<2)|0;do{if((h|0)==(c[g>>2]|0)){c[g>>2]=C;if((C|0)!=0){break}c[5295]=c[5295]&~(1<<c[l>>2]);break b}else{if(m>>>0<(c[5298]|0)>>>0){za()}t=m+16|0;if((c[t>>2]|0)==(h|0)){c[t>>2]=C}else{c[m+20>>2]=C}if((C|0)==0){break b}}}while(0);if(C>>>0<(c[5298]|0)>>>0){za()}c[C+24>>2]=m;h=c[d+(b+16)>>2]|0;do{if((h|0)!=0){if(h>>>0<(c[5298]|0)>>>0){za()}else{c[C+16>>2]=h;c[h+24>>2]=C;break}}}while(0);h=c[d+(b+20)>>2]|0;if((h|0)==0){break}if(h>>>0<(c[5298]|0)>>>0){za()}else{c[C+20>>2]=h;c[h+24>>2]=C;break}}}while(0);c[n+4>>2]=A|1;c[n+A>>2]=A;if((n|0)!=(c[5299]|0)){F=A;break}c[5296]=A;return}else{c[y>>2]=z&-2;c[n+4>>2]=o|1;c[n+o>>2]=o;F=o}}while(0);o=F>>>3;if(F>>>0<256>>>0){z=o<<1;y=21216+(z<<2)|0;C=c[5294]|0;b=1<<o;do{if((C&b|0)==0){c[5294]=C|b;G=y;H=21216+(z+2<<2)|0}else{o=21216+(z+2<<2)|0;d=c[o>>2]|0;if(d>>>0>=(c[5298]|0)>>>0){G=d;H=o;break}za()}}while(0);c[H>>2]=n;c[G+12>>2]=n;c[n+8>>2]=G;c[n+12>>2]=y;return}y=n;G=F>>>8;do{if((G|0)==0){I=0}else{if(F>>>0>16777215>>>0){I=31;break}H=(G+1048320|0)>>>16&8;z=G<<H;b=(z+520192|0)>>>16&4;C=z<<b;z=(C+245760|0)>>>16&2;o=14-(b|H|z)+(C<<z>>>15)|0;I=F>>>((o+7|0)>>>0)&1|o<<1}}while(0);G=21480+(I<<2)|0;c[n+28>>2]=I;c[n+20>>2]=0;c[n+16>>2]=0;o=c[5295]|0;z=1<<I;if((o&z|0)==0){c[5295]=o|z;c[G>>2]=y;c[n+24>>2]=G;c[n+12>>2]=n;c[n+8>>2]=n;return}z=c[G>>2]|0;if((I|0)==31){J=0}else{J=25-(I>>>1)|0}c:do{if((c[z+4>>2]&-8|0)==(F|0)){K=z}else{I=z;G=F<<J;while(1){L=I+16+(G>>>31<<2)|0;o=c[L>>2]|0;if((o|0)==0){break}if((c[o+4>>2]&-8|0)==(F|0)){K=o;break c}else{I=o;G=G<<1}}if(L>>>0<(c[5298]|0)>>>0){za()}c[L>>2]=y;c[n+24>>2]=I;c[n+12>>2]=n;c[n+8>>2]=n;return}}while(0);L=K+8|0;F=c[L>>2]|0;J=c[5298]|0;if(!(K>>>0>=J>>>0&F>>>0>=J>>>0)){za()}c[F+12>>2]=y;c[L>>2]=y;c[n+8>>2]=F;c[n+12>>2]=K;c[n+24>>2]=0;return}function Xn(a,b){a=a|0;b=b|0;var e=0,f=0,g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0;e=a+4|0;f=c[e>>2]|0;g=a+100|0;if(f>>>0<(c[g>>2]|0)>>>0){c[e>>2]=f+1;h=d[f]|0}else{h=Pn(a)|0}do{if((h|0)==45|(h|0)==43){f=c[e>>2]|0;i=(h|0)==45|0;if(f>>>0<(c[g>>2]|0)>>>0){c[e>>2]=f+1;j=d[f]|0}else{j=Pn(a)|0}if(!((j-48|0)>>>0>9>>>0&(b|0)!=0)){k=i;l=j;break}if((c[g>>2]|0)==0){k=i;l=j;break}c[e>>2]=(c[e>>2]|0)-1;k=i;l=j}else{k=0;l=h}}while(0);if((l-48|0)>>>0>9>>>0){if((c[g>>2]|0)==0){m=-2147483648;n=0;return(G=m,n)|0}c[e>>2]=(c[e>>2]|0)-1;m=-2147483648;n=0;return(G=m,n)|0}else{o=l;p=0}while(1){q=o-48+(p*10|0)|0;l=c[e>>2]|0;if(l>>>0<(c[g>>2]|0)>>>0){c[e>>2]=l+1;r=d[l]|0}else{r=Pn(a)|0}if((r-48|0)>>>0<10>>>0&(q|0)<214748364){o=r;p=q}else{break}}p=q;o=(q|0)<0|0?-1:0;if((r-48|0)>>>0<10>>>0){q=r;l=o;h=p;while(1){j=so(h,l,10,0)|0;b=G;i=io(q,(q|0)<0|0?-1:0,-48,-1)|0;f=io(i,G,j,b)|0;b=G;j=c[e>>2]|0;if(j>>>0<(c[g>>2]|0)>>>0){c[e>>2]=j+1;s=d[j]|0}else{s=Pn(a)|0}j=21474836;if((s-48|0)>>>0<10>>>0&((b|0)<(j|0)|(b|0)==(j|0)&f>>>0<2061584302>>>0)){q=s;l=b;h=f}else{t=s;u=b;v=f;break}}}else{t=r;u=o;v=p}if((t-48|0)>>>0<10>>>0){do{t=c[e>>2]|0;if(t>>>0<(c[g>>2]|0)>>>0){c[e>>2]=t+1;w=d[t]|0}else{w=Pn(a)|0}}while((w-48|0)>>>0<10>>>0)}if((c[g>>2]|0)!=0){c[e>>2]=(c[e>>2]|0)-1}e=(k|0)!=0;k=jo(0,0,v,u)|0;m=e?G:u;n=e?k:v;return(G=m,n)|0}function Yn(b,d,e){b=b|0;d=d|0;e=e|0;var f=0;f=b|0;if((b&3)==(d&3)){while(b&3){if((e|0)==0)return f|0;a[b]=a[d]|0;b=b+1|0;d=d+1|0;e=e-1|0}while((e|0)>=4){c[b>>2]=c[d>>2];b=b+4|0;d=d+4|0;e=e-4|0}}while((e|0)>0){a[b]=a[d]|0;b=b+1|0;d=d+1|0;e=e-1|0}return f|0}function Zn(b,c){b=b|0;c=c|0;var d=0;do{a[b+d|0]=a[c+d|0];d=d+1|0}while(a[c+(d-1)|0]|0);return b|0}function _n(b){b=b|0;var c=0;c=b;while(a[c]|0){c=c+1|0}return c-b|0}function $n(b,c){b=b|0;c=c|0;var d=0,e=0;d=b+(_n(b)|0)|0;do{a[d+e|0]=a[c+e|0];e=e+1|0}while(a[c+(e-1)|0]|0);return b|0}function ao(a){a=a|0;if((a|0)<65)return a|0;if((a|0)>90)return a|0;return a-65+97|0}function bo(b,c,d){b=b|0;c=c|0;d=d|0;var e=0,f=0;while((e|0)<(d|0)){a[b+e|0]=f?0:a[c+e|0]|0;f=f?1:(a[c+e|0]|0)==0;e=e+1|0}return b|0}function co(a,b,d){a=a|0;b=b|0;d=d|0;var e=0;v=v+1|0;c[a>>2]=v;while((e|0)<40){if((c[d+(e<<2)>>2]|0)==0){c[d+(e<<2)>>2]=v;c[d+((e<<2)+4)>>2]=b;c[d+((e<<2)+8)>>2]=0;return 0}e=e+2|0}cb(116);cb(111);cb(111);cb(32);cb(109);cb(97);cb(110);cb(121);cb(32);cb(115);cb(101);cb(116);cb(106);cb(109);cb(112);cb(115);cb(32);cb(105);cb(110);cb(32);cb(97);cb(32);cb(102);cb(117);cb(110);cb(99);cb(116);cb(105);cb(111);cb(110);cb(32);cb(99);cb(97);cb(108);cb(108);cb(44);cb(32);cb(98);cb(117);cb(105);cb(108);cb(100);cb(32);cb(119);cb(105);cb(116);cb(104);cb(32);cb(97);cb(32);cb(104);cb(105);cb(103);cb(104);cb(101);cb(114);cb(32);cb(118);cb(97);cb(108);cb(117);cb(101);cb(32);cb(102);cb(111);cb(114);cb(32);cb(77);cb(65);cb(88);cb(95);cb(83);cb(69);cb(84);cb(74);cb(77);cb(80);cb(83);cb(10);da(0);return 0}function eo(a,b){a=a|0;b=b|0;var d=0,e=0;while((d|0)<20){e=c[b+(d<<2)>>2]|0;if((e|0)==0)break;if((e|0)==(a|0)){return c[b+((d<<2)+4)>>2]|0}d=d+2|0}return 0}function fo(b,d,e){b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,i=0;f=b+e|0;if((e|0)>=20){d=d&255;g=b&3;h=d|d<<8|d<<16|d<<24;i=f&~3;if(g){g=b+4-g|0;while((b|0)<(g|0)){a[b]=d;b=b+1|0}}while((b|0)<(i|0)){c[b>>2]=h;b=b+4|0}}while((b|0)<(f|0)){a[b]=d;b=b+1|0}return b-e|0}function go(b,c,d){b=b|0;c=c|0;d=d|0;var e=0;if((c|0)<(b|0)&(b|0)<(c+d|0)){e=b;c=c+d|0;b=b+d|0;while((d|0)>0){b=b-1|0;c=c-1|0;d=d-1|0;a[b]=a[c]|0}b=e}else{Yn(b,c,d)|0}return b|0}function ho(a,b,c){a=a|0;b=b|0;c=c|0;var e=0,f=0,g=0;while((e|0)<(c|0)){f=d[a+e|0]|0;g=d[b+e|0]|0;if((f|0)!=(g|0))return((f|0)>(g|0)?1:-1)|0;e=e+1|0}return 0}function io(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;var e=0;e=a+c>>>0;return(G=b+d+(e>>>0<a>>>0|0)>>>0,e|0)|0}function jo(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;var e=0;e=b-d>>>0;e=b-d-(c>>>0>a>>>0|0)>>>0;return(G=e,a-c>>>0|0)|0}function ko(a,b,c){a=a|0;b=b|0;c=c|0;if((c|0)<32){G=b<<c|(a&(1<<c)-1<<32-c)>>>32-c;return a<<c}G=a<<c-32;return 0}function lo(a,b,c){a=a|0;b=b|0;c=c|0;if((c|0)<32){G=b>>>c;return a>>>c|(b&(1<<c)-1)<<32-c}G=0;return b>>>c-32|0}function mo(a,b,c){a=a|0;b=b|0;c=c|0;if((c|0)<32){G=b>>c;return a>>>c|(b&(1<<c)-1)<<32-c}G=(b|0)<0?-1:0;return b>>c-32|0}function no(b){b=b|0;var c=0;c=a[n+(b>>>24)|0]|0;if((c|0)<8)return c|0;c=a[n+(b>>16&255)|0]|0;if((c|0)<8)return c+8|0;c=a[n+(b>>8&255)|0]|0;if((c|0)<8)return c+16|0;return(a[n+(b&255)|0]|0)+24|0}function oo(b){b=b|0;var c=0;c=a[m+(b&255)|0]|0;if((c|0)<8)return c|0;c=a[m+(b>>8&255)|0]|0;if((c|0)<8)return c+8|0;c=a[m+(b>>16&255)|0]|0;if((c|0)<8)return c+16|0;return(a[m+(b>>>24)|0]|0)+24|0}function po(a,b){a=a|0;b=b|0;var c=0,d=0,e=0,f=0;c=a&65535;d=b&65535;e=ca(d,c)|0;f=a>>>16;a=(e>>>16)+(ca(d,f)|0)|0;d=b>>>16;b=ca(d,c)|0;return(G=(a>>>16)+(ca(d,f)|0)+(((a&65535)+b|0)>>>16)|0,a+b<<16|e&65535|0)|0}function qo(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;var e=0,f=0,g=0,h=0,i=0;e=b>>31|((b|0)<0?-1:0)<<1;f=((b|0)<0?-1:0)>>31|((b|0)<0?-1:0)<<1;g=d>>31|((d|0)<0?-1:0)<<1;h=((d|0)<0?-1:0)>>31|((d|0)<0?-1:0)<<1;i=jo(e^a,f^b,e,f)|0;b=G;a=g^e;e=h^f;f=jo((vo(i,b,jo(g^c,h^d,g,h)|0,G,0)|0)^a,G^e,a,e)|0;return(G=G,f)|0}function ro(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0,h=0,j=0,k=0,l=0,m=0;f=i;i=i+8|0;g=f|0;h=b>>31|((b|0)<0?-1:0)<<1;j=((b|0)<0?-1:0)>>31|((b|0)<0?-1:0)<<1;k=e>>31|((e|0)<0?-1:0)<<1;l=((e|0)<0?-1:0)>>31|((e|0)<0?-1:0)<<1;m=jo(h^a,j^b,h,j)|0;b=G;vo(m,b,jo(k^d,l^e,k,l)|0,G,g)|0;l=jo(c[g>>2]^h,c[g+4>>2]^j,h,j)|0;j=G;i=f;return(G=j,l)|0}function so(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;var e=0,f=0;e=a;a=c;c=po(e,a)|0;f=G;return(G=(ca(b,a)|0)+(ca(d,e)|0)+f|f&0,c|0|0)|0}function to(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;var e=0;e=vo(a,b,c,d,0)|0;return(G=G,e)|0}function uo(a,b,d,e){a=a|0;b=b|0;d=d|0;e=e|0;var f=0,g=0;f=i;i=i+8|0;g=f|0;vo(a,b,d,e,g)|0;i=f;return(G=c[g+4>>2]|0,c[g>>2]|0)|0}function vo(a,b,d,e,f){a=a|0;b=b|0;d=d|0;e=e|0;f=f|0;var g=0,h=0,i=0,j=0,k=0,l=0,m=0,n=0,o=0,p=0,q=0,r=0,s=0,t=0,u=0,v=0,w=0,x=0,y=0,z=0,A=0,B=0,C=0,D=0,E=0,F=0,H=0,I=0,J=0,K=0,L=0,M=0;g=a;h=b;i=h;j=d;k=e;l=k;if((i|0)==0){m=(f|0)!=0;if((l|0)==0){if(m){c[f>>2]=(g>>>0)%(j>>>0);c[f+4>>2]=0}n=0;o=(g>>>0)/(j>>>0)>>>0;return(G=n,o)|0}else{if(!m){n=0;o=0;return(G=n,o)|0}c[f>>2]=a|0;c[f+4>>2]=b&0;n=0;o=0;return(G=n,o)|0}}m=(l|0)==0;do{if((j|0)==0){if(m){if((f|0)!=0){c[f>>2]=(i>>>0)%(j>>>0);c[f+4>>2]=0}n=0;o=(i>>>0)/(j>>>0)>>>0;return(G=n,o)|0}if((g|0)==0){if((f|0)!=0){c[f>>2]=0;c[f+4>>2]=(i>>>0)%(l>>>0)}n=0;o=(i>>>0)/(l>>>0)>>>0;return(G=n,o)|0}p=l-1|0;if((p&l|0)==0){if((f|0)!=0){c[f>>2]=a|0;c[f+4>>2]=p&i|b&0}n=0;o=i>>>((oo(l|0)|0)>>>0);return(G=n,o)|0}p=(no(l|0)|0)-(no(i|0)|0)|0;if(p>>>0<=30){q=p+1|0;r=31-p|0;s=q;t=i<<r|g>>>(q>>>0);u=i>>>(q>>>0);v=0;w=g<<r;break}if((f|0)==0){n=0;o=0;return(G=n,o)|0}c[f>>2]=a|0;c[f+4>>2]=h|b&0;n=0;o=0;return(G=n,o)|0}else{if(!m){r=(no(l|0)|0)-(no(i|0)|0)|0;if(r>>>0<=31){q=r+1|0;p=31-r|0;x=r-31>>31;s=q;t=g>>>(q>>>0)&x|i<<p;u=i>>>(q>>>0)&x;v=0;w=g<<p;break}if((f|0)==0){n=0;o=0;return(G=n,o)|0}c[f>>2]=a|0;c[f+4>>2]=h|b&0;n=0;o=0;return(G=n,o)|0}p=j-1|0;if((p&j|0)!=0){x=(no(j|0)|0)+33-(no(i|0)|0)|0;q=64-x|0;r=32-x|0;y=r>>31;z=x-32|0;A=z>>31;s=x;t=r-1>>31&i>>>(z>>>0)|(i<<r|g>>>(x>>>0))&A;u=A&i>>>(x>>>0);v=g<<q&y;w=(i<<q|g>>>(z>>>0))&y|g<<r&x-33>>31;break}if((f|0)!=0){c[f>>2]=p&g;c[f+4>>2]=0}if((j|0)==1){n=h|b&0;o=a|0|0;return(G=n,o)|0}else{p=oo(j|0)|0;n=i>>>(p>>>0)|0;o=i<<32-p|g>>>(p>>>0)|0;return(G=n,o)|0}}}while(0);if((s|0)==0){B=w;C=v;D=u;E=t;F=0;H=0}else{g=d|0|0;d=k|e&0;e=io(g,d,-1,-1)|0;k=G;i=w;w=v;v=u;u=t;t=s;s=0;while(1){I=w>>>31|i<<1;J=s|w<<1;j=u<<1|i>>>31|0;a=u>>>31|v<<1|0;jo(e,k,j,a)|0;b=G;h=b>>31|((b|0)<0?-1:0)<<1;K=h&1;L=jo(j,a,h&g,(((b|0)<0?-1:0)>>31|((b|0)<0?-1:0)<<1)&d)|0;M=G;b=t-1|0;if((b|0)==0){break}else{i=I;w=J;v=M;u=L;t=b;s=K}}B=I;C=J;D=M;E=L;F=0;H=K}K=C;C=0;if((f|0)!=0){c[f>>2]=E;c[f+4>>2]=D}n=(K|0)>>>31|(B|C)<<1|(C<<1|K>>>31)&0|F;o=(K<<1|0>>>31)&-2|H;return(G=n,o)|0}function wo(a,b){a=a|0;b=b|0;return nb(a|0,b|0)|0}function xo(a,b){a=a|0;b=b|0;return pb[a&15](b|0)|0}function yo(a,b){a=a|0;b=b|0;qb[a&63](b|0)}function zo(a,b,c){a=a|0;b=b|0;c=c|0;rb[a&127](b|0,c|0)}function Ao(a,b,c,d,e,f,g){a=a|0;b=b|0;c=c|0;d=d|0;e=e|0;f=f|0;g=g|0;return sb[a&3](b|0,c|0,d|0,e|0,f|0,g|0)|0}function Bo(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;return tb[a&15](b|0,c|0,d|0)|0}function Co(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;ub[a&255](b|0,c|0,d|0)}function Do(a){a=a|0;vb[a&1]()}function Eo(a,b,c,d,e){a=a|0;b=b|0;c=c|0;d=d|0;e=e|0;return wb[a&31](b|0,c|0,d|0,e|0)|0}function Fo(a,b,c){a=a|0;b=b|0;c=c|0;return xb[a&7](b|0,c|0)|0}function Go(a,b,c,d,e){a=a|0;b=b|0;c=c|0;d=d|0;e=e|0;yb[a&15](b|0,c|0,d|0,e|0)}function Ho(a){a=a|0;da(0);return 0}function Io(a){a=a|0;da(1)}function Jo(a,b){a=a|0;b=b|0;da(2)}function Ko(a,b,c,d,e,f){a=a|0;b=b|0;c=c|0;d=d|0;e=e|0;f=f|0;da(3);return 0}function Lo(a,b,c){a=a|0;b=b|0;c=c|0;da(4);return 0}function Mo(a,b,c){a=a|0;b=b|0;c=c|0;da(5)}function No(){da(6)}function Oo(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;da(7);return 0}function Po(a,b){a=a|0;b=b|0;da(8);return 0}function Qo(a,b,c,d){a=a|0;b=b|0;c=c|0;d=d|0;da(9)}




// EMSCRIPTEN_END_FUNCS
var pb=[Ho,Ho,Pm,Ho,Mc,Ho,Fm,Ho,xe,Ho,hh,Ho,Yc,Ho,Ho,Ho];var qb=[Io,Io,Lf,Io,tj,Io,Gc,Io,Im,Io,qm,Io,_d,Io,_c,Io,Qm,Io,Qg,Io,Ti,Io,ch,Io,Ph,Io,Ub,Io,ye,Io,Gm,Io,ze,Io,ah,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io,Io];var rb=[Jo,Jo,tf,Jo,Vc,Jo,sf,Jo,xf,Jo,Kc,Jo,af,Jo,sj,Jo,Zd,Jo,Wc,Jo,ff,Jo,cf,Jo,pf,Jo,Oh,Jo,Bf,Jo,of,Jo,jf,Jo,ef,Jo,vf,Jo,nf,Jo,En,Jo,yf,Jo,zf,Jo,wf,Jo,uf,Jo,hf,Jo,mf,Jo,kf,Jo,lf,Jo,Af,Jo,fh,Jo,bf,Jo,$g,Jo,ad,Jo,$c,Jo,Sm,Jo,Si,Jo,gf,Jo,Dc,Jo,rf,Jo,df,Jo,qf,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo,Jo];var sb=[Ko,Ko,Bc,Ko];var tb=[Lo,Lo,rg,Lo,Nc,Lo,bd,Lo,cd,Lo,tg,Lo,Lo,Lo,Lo,Lo];var ub=[Mo,Mo,ie,Mo,Zb,Mo,ae,Mo,Xi,Mo,be,Mo,ne,Mo,hc,Mo,Uc,Mo,aj,Mo,fe,Mo,jc,Mo,ic,Mo,Qc,Mo,Rg,Mo,Ki,Mo,Ug,Mo,Li,Mo,Rb,Mo,Pc,Mo,_b,Mo,Wb,Mo,Vb,Mo,jj,Mo,he,Mo,cj,Mo,ec,Mo,le,Mo,Tc,Mo,Rc,Mo,ac,Mo,Sg,Mo,Xb,Mo,Vg,Mo,xc,Mo,$b,Mo,ke,Mo,Sc,Mo,Yb,Mo,_i,Mo,fc,Mo,cc,Mo,bj,Mo,de,Mo,Vi,Mo,Fe,Mo,dc,Mo,bc,Mo,yc,Mo,Ac,Mo,Wi,Mo,oe,Mo,wc,Mo,gc,Mo,ge,Mo,Zi,Mo,Oc,Mo,ee,Mo,dj,Mo,me,Mo,Tg,Mo,pe,Mo,ce,Mo,kc,Mo,$i,Mo,Yi,Mo,mj,Mo,nj,Mo,je,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo,Mo];var vb=[No,No];var wb=[Oo,Oo,_g,Oo,Ee,Oo,Yd,Oo,Nh,Oo,jh,Oo,Tb,Oo,Ri,Oo,Qb,Oo,lj,Oo,Oo,Oo,Oo,Oo,Oo,Oo,Oo,Oo,Oo,Oo,Oo,Oo];var xb=[Po,Po,dd,Po,wo,Po,Po,Po];var yb=[Qo,Qo,vm,Qo,Xc,Qo,Zc,Qo,Ae,Qo,Qo,Qo,Qo,Qo,Qo,Qo];return{_testSetjmp:eo,_saveSetjmp:co,_strcat:$n,_free:Ln,_get_parser:Gn,_memcmp:ho,_strncpy:bo,_memmove:go,_tolower:ao,_strlen:_n,_memset:fo,_malloc:Kn,_memcpy:Yn,_run_parser:Hn,_destroy_parser:In,_realloc:Mn,_get_parser_error:Jn,_strcpy:Zn,runPostSets:Pb,stackAlloc:zb,stackSave:Ab,stackRestore:Bb,setThrew:Cb,setTempRet0:Fb,setTempRet1:Gb,setTempRet2:Hb,setTempRet3:Ib,setTempRet4:Jb,setTempRet5:Kb,setTempRet6:Lb,setTempRet7:Mb,setTempRet8:Nb,setTempRet9:Ob,dynCall_ii:xo,dynCall_vi:yo,dynCall_vii:zo,dynCall_iiiiiii:Ao,dynCall_iiii:Bo,dynCall_viii:Co,dynCall_v:Do,dynCall_iiiii:Eo,dynCall_iii:Fo,dynCall_viiii:Go}})


// EMSCRIPTEN_END_ASM
({ "Math": Math, "Int8Array": Int8Array, "Int16Array": Int16Array, "Int32Array": Int32Array, "Uint8Array": Uint8Array, "Uint16Array": Uint16Array, "Uint32Array": Uint32Array, "Float32Array": Float32Array, "Float64Array": Float64Array }, { "abort": abort, "assert": assert, "asmPrintInt": asmPrintInt, "asmPrintFloat": asmPrintFloat, "min": Math_min, "invoke_ii": invoke_ii, "invoke_vi": invoke_vi, "invoke_vii": invoke_vii, "invoke_iiiiiii": invoke_iiiiiii, "invoke_iiii": invoke_iiii, "invoke_viii": invoke_viii, "invoke_v": invoke_v, "invoke_iiiii": invoke_iiiii, "invoke_iii": invoke_iii, "invoke_viiii": invoke_viiii, "_strncmp": _strncmp, "_llvm_va_end": _llvm_va_end, "_dlsym": _dlsym, "_snprintf": _snprintf, "_fgetc": _fgetc, "_fclose": _fclose, "_isprint": _isprint, "_abort": _abort, "_toupper": _toupper, "_pread": _pread, "_close": _close, "_fflush": _fflush, "_fopen": _fopen, "_strchr": _strchr, "_fputc": _fputc, "___buildEnvironment": ___buildEnvironment, "_sysconf": _sysconf, "_isalnum": _isalnum, "___setErrNo": ___setErrNo, "__reallyNegative": __reallyNegative, "_send": _send, "_write": _write, "_fputs": _fputs, "_isalpha": _isalpha, "_sprintf": _sprintf, "_llvm_lifetime_end": _llvm_lifetime_end, "_fabs": _fabs, "_isspace": _isspace, "_fread": _fread, "_longjmp": _longjmp, "_read": _read, "_copysign": _copysign, "__formatString": __formatString, "_ungetc": _ungetc, "_dlclose": _dlclose, "_recv": _recv, "_dlopen": _dlopen, "_pwrite": _pwrite, "_putchar": _putchar, "_sbrk": _sbrk, "_fsync": _fsync, "_strerror_r": _strerror_r, "___errno_location": ___errno_location, "_llvm_lifetime_start": _llvm_lifetime_start, "_open": _open, "_fmod": _fmod, "_time": _time, "_islower": _islower, "_isupper": _isupper, "_strcmp": _strcmp, "STACKTOP": STACKTOP, "STACK_MAX": STACK_MAX, "tempDoublePtr": tempDoublePtr, "ABORT": ABORT, "cttz_i8": cttz_i8, "ctlz_i8": ctlz_i8, "NaN": NaN, "Infinity": Infinity, "_stdin": _stdin, "_stderr": _stderr, "_stdout": _stdout }, buffer);
var _testSetjmp = Module["_testSetjmp"] = asm["_testSetjmp"];
var _saveSetjmp = Module["_saveSetjmp"] = asm["_saveSetjmp"];
var _strcat = Module["_strcat"] = asm["_strcat"];
var _free = Module["_free"] = asm["_free"];
var _get_parser = Module["_get_parser"] = asm["_get_parser"];
var _memcmp = Module["_memcmp"] = asm["_memcmp"];
var _strncpy = Module["_strncpy"] = asm["_strncpy"];
var _memmove = Module["_memmove"] = asm["_memmove"];
var _tolower = Module["_tolower"] = asm["_tolower"];
var _strlen = Module["_strlen"] = asm["_strlen"];
var _memset = Module["_memset"] = asm["_memset"];
var _malloc = Module["_malloc"] = asm["_malloc"];
var _memcpy = Module["_memcpy"] = asm["_memcpy"];
var _run_parser = Module["_run_parser"] = asm["_run_parser"];
var _destroy_parser = Module["_destroy_parser"] = asm["_destroy_parser"];
var _realloc = Module["_realloc"] = asm["_realloc"];
var _get_parser_error = Module["_get_parser_error"] = asm["_get_parser_error"];
var _strcpy = Module["_strcpy"] = asm["_strcpy"];
var runPostSets = Module["runPostSets"] = asm["runPostSets"];
var dynCall_ii = Module["dynCall_ii"] = asm["dynCall_ii"];
var dynCall_vi = Module["dynCall_vi"] = asm["dynCall_vi"];
var dynCall_vii = Module["dynCall_vii"] = asm["dynCall_vii"];
var dynCall_iiiiiii = Module["dynCall_iiiiiii"] = asm["dynCall_iiiiiii"];
var dynCall_iiii = Module["dynCall_iiii"] = asm["dynCall_iiii"];
var dynCall_viii = Module["dynCall_viii"] = asm["dynCall_viii"];
var dynCall_v = Module["dynCall_v"] = asm["dynCall_v"];
var dynCall_iiiii = Module["dynCall_iiiii"] = asm["dynCall_iiiii"];
var dynCall_iii = Module["dynCall_iii"] = asm["dynCall_iii"];
var dynCall_viiii = Module["dynCall_viiii"] = asm["dynCall_viiii"];

Runtime.stackAlloc = function(size) { return asm['stackAlloc'](size) };
Runtime.stackSave = function() { return asm['stackSave']() };
Runtime.stackRestore = function(top) { asm['stackRestore'](top) };

// TODO: strip out parts of this we do not need

//======= begin closure i64 code =======

// Copyright 2009 The Closure Library Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS-IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/**
 * @fileoverview Defines a Long class for representing a 64-bit two's-complement
 * integer value, which faithfully simulates the behavior of a Java "long". This
 * implementation is derived from LongLib in GWT.
 *
 */

var i64Math = (function() { // Emscripten wrapper
  var goog = { math: {} };


  /**
   * Constructs a 64-bit two's-complement integer, given its low and high 32-bit
   * values as *signed* integers.  See the from* functions below for more
   * convenient ways of constructing Longs.
   *
   * The internal representation of a long is the two given signed, 32-bit values.
   * We use 32-bit pieces because these are the size of integers on which
   * Javascript performs bit-operations.  For operations like addition and
   * multiplication, we split each number into 16-bit pieces, which can easily be
   * multiplied within Javascript's floating-point representation without overflow
   * or change in sign.
   *
   * In the algorithms below, we frequently reduce the negative case to the
   * positive case by negating the input(s) and then post-processing the result.
   * Note that we must ALWAYS check specially whether those values are MIN_VALUE
   * (-2^63) because -MIN_VALUE == MIN_VALUE (since 2^63 cannot be represented as
   * a positive number, it overflows back into a negative).  Not handling this
   * case would often result in infinite recursion.
   *
   * @param {number} low  The low (signed) 32 bits of the long.
   * @param {number} high  The high (signed) 32 bits of the long.
   * @constructor
   */
  goog.math.Long = function(low, high) {
    /**
     * @type {number}
     * @private
     */
    this.low_ = low | 0;  // force into 32 signed bits.

    /**
     * @type {number}
     * @private
     */
    this.high_ = high | 0;  // force into 32 signed bits.
  };


  // NOTE: Common constant values ZERO, ONE, NEG_ONE, etc. are defined below the
  // from* methods on which they depend.


  /**
   * A cache of the Long representations of small integer values.
   * @type {!Object}
   * @private
   */
  goog.math.Long.IntCache_ = {};


  /**
   * Returns a Long representing the given (32-bit) integer value.
   * @param {number} value The 32-bit integer in question.
   * @return {!goog.math.Long} The corresponding Long value.
   */
  goog.math.Long.fromInt = function(value) {
    if (-128 <= value && value < 128) {
      var cachedObj = goog.math.Long.IntCache_[value];
      if (cachedObj) {
        return cachedObj;
      }
    }

    var obj = new goog.math.Long(value | 0, value < 0 ? -1 : 0);
    if (-128 <= value && value < 128) {
      goog.math.Long.IntCache_[value] = obj;
    }
    return obj;
  };


  /**
   * Returns a Long representing the given value, provided that it is a finite
   * number.  Otherwise, zero is returned.
   * @param {number} value The number in question.
   * @return {!goog.math.Long} The corresponding Long value.
   */
  goog.math.Long.fromNumber = function(value) {
    if (isNaN(value) || !isFinite(value)) {
      return goog.math.Long.ZERO;
    } else if (value <= -goog.math.Long.TWO_PWR_63_DBL_) {
      return goog.math.Long.MIN_VALUE;
    } else if (value + 1 >= goog.math.Long.TWO_PWR_63_DBL_) {
      return goog.math.Long.MAX_VALUE;
    } else if (value < 0) {
      return goog.math.Long.fromNumber(-value).negate();
    } else {
      return new goog.math.Long(
          (value % goog.math.Long.TWO_PWR_32_DBL_) | 0,
          (value / goog.math.Long.TWO_PWR_32_DBL_) | 0);
    }
  };


  /**
   * Returns a Long representing the 64-bit integer that comes by concatenating
   * the given high and low bits.  Each is assumed to use 32 bits.
   * @param {number} lowBits The low 32-bits.
   * @param {number} highBits The high 32-bits.
   * @return {!goog.math.Long} The corresponding Long value.
   */
  goog.math.Long.fromBits = function(lowBits, highBits) {
    return new goog.math.Long(lowBits, highBits);
  };


  /**
   * Returns a Long representation of the given string, written using the given
   * radix.
   * @param {string} str The textual representation of the Long.
   * @param {number=} opt_radix The radix in which the text is written.
   * @return {!goog.math.Long} The corresponding Long value.
   */
  goog.math.Long.fromString = function(str, opt_radix) {
    if (str.length == 0) {
      throw Error('number format error: empty string');
    }

    var radix = opt_radix || 10;
    if (radix < 2 || 36 < radix) {
      throw Error('radix out of range: ' + radix);
    }

    if (str.charAt(0) == '-') {
      return goog.math.Long.fromString(str.substring(1), radix).negate();
    } else if (str.indexOf('-') >= 0) {
      throw Error('number format error: interior "-" character: ' + str);
    }

    // Do several (8) digits each time through the loop, so as to
    // minimize the calls to the very expensive emulated div.
    var radixToPower = goog.math.Long.fromNumber(Math.pow(radix, 8));

    var result = goog.math.Long.ZERO;
    for (var i = 0; i < str.length; i += 8) {
      var size = Math.min(8, str.length - i);
      var value = parseInt(str.substring(i, i + size), radix);
      if (size < 8) {
        var power = goog.math.Long.fromNumber(Math.pow(radix, size));
        result = result.multiply(power).add(goog.math.Long.fromNumber(value));
      } else {
        result = result.multiply(radixToPower);
        result = result.add(goog.math.Long.fromNumber(value));
      }
    }
    return result;
  };


  // NOTE: the compiler should inline these constant values below and then remove
  // these variables, so there should be no runtime penalty for these.


  /**
   * Number used repeated below in calculations.  This must appear before the
   * first call to any from* function below.
   * @type {number}
   * @private
   */
  goog.math.Long.TWO_PWR_16_DBL_ = 1 << 16;


  /**
   * @type {number}
   * @private
   */
  goog.math.Long.TWO_PWR_24_DBL_ = 1 << 24;


  /**
   * @type {number}
   * @private
   */
  goog.math.Long.TWO_PWR_32_DBL_ =
      goog.math.Long.TWO_PWR_16_DBL_ * goog.math.Long.TWO_PWR_16_DBL_;


  /**
   * @type {number}
   * @private
   */
  goog.math.Long.TWO_PWR_31_DBL_ =
      goog.math.Long.TWO_PWR_32_DBL_ / 2;


  /**
   * @type {number}
   * @private
   */
  goog.math.Long.TWO_PWR_48_DBL_ =
      goog.math.Long.TWO_PWR_32_DBL_ * goog.math.Long.TWO_PWR_16_DBL_;


  /**
   * @type {number}
   * @private
   */
  goog.math.Long.TWO_PWR_64_DBL_ =
      goog.math.Long.TWO_PWR_32_DBL_ * goog.math.Long.TWO_PWR_32_DBL_;


  /**
   * @type {number}
   * @private
   */
  goog.math.Long.TWO_PWR_63_DBL_ =
      goog.math.Long.TWO_PWR_64_DBL_ / 2;


  /** @type {!goog.math.Long} */
  goog.math.Long.ZERO = goog.math.Long.fromInt(0);


  /** @type {!goog.math.Long} */
  goog.math.Long.ONE = goog.math.Long.fromInt(1);


  /** @type {!goog.math.Long} */
  goog.math.Long.NEG_ONE = goog.math.Long.fromInt(-1);


  /** @type {!goog.math.Long} */
  goog.math.Long.MAX_VALUE =
      goog.math.Long.fromBits(0xFFFFFFFF | 0, 0x7FFFFFFF | 0);


  /** @type {!goog.math.Long} */
  goog.math.Long.MIN_VALUE = goog.math.Long.fromBits(0, 0x80000000 | 0);


  /**
   * @type {!goog.math.Long}
   * @private
   */
  goog.math.Long.TWO_PWR_24_ = goog.math.Long.fromInt(1 << 24);


  /** @return {number} The value, assuming it is a 32-bit integer. */
  goog.math.Long.prototype.toInt = function() {
    return this.low_;
  };


  /** @return {number} The closest floating-point representation to this value. */
  goog.math.Long.prototype.toNumber = function() {
    return this.high_ * goog.math.Long.TWO_PWR_32_DBL_ +
           this.getLowBitsUnsigned();
  };


  /**
   * @param {number=} opt_radix The radix in which the text should be written.
   * @return {string} The textual representation of this value.
   */
  goog.math.Long.prototype.toString = function(opt_radix) {
    var radix = opt_radix || 10;
    if (radix < 2 || 36 < radix) {
      throw Error('radix out of range: ' + radix);
    }

    if (this.isZero()) {
      return '0';
    }

    if (this.isNegative()) {
      if (this.equals(goog.math.Long.MIN_VALUE)) {
        // We need to change the Long value before it can be negated, so we remove
        // the bottom-most digit in this base and then recurse to do the rest.
        var radixLong = goog.math.Long.fromNumber(radix);
        var div = this.div(radixLong);
        var rem = div.multiply(radixLong).subtract(this);
        return div.toString(radix) + rem.toInt().toString(radix);
      } else {
        return '-' + this.negate().toString(radix);
      }
    }

    // Do several (6) digits each time through the loop, so as to
    // minimize the calls to the very expensive emulated div.
    var radixToPower = goog.math.Long.fromNumber(Math.pow(radix, 6));

    var rem = this;
    var result = '';
    while (true) {
      var remDiv = rem.div(radixToPower);
      var intval = rem.subtract(remDiv.multiply(radixToPower)).toInt();
      var digits = intval.toString(radix);

      rem = remDiv;
      if (rem.isZero()) {
        return digits + result;
      } else {
        while (digits.length < 6) {
          digits = '0' + digits;
        }
        result = '' + digits + result;
      }
    }
  };


  /** @return {number} The high 32-bits as a signed value. */
  goog.math.Long.prototype.getHighBits = function() {
    return this.high_;
  };


  /** @return {number} The low 32-bits as a signed value. */
  goog.math.Long.prototype.getLowBits = function() {
    return this.low_;
  };


  /** @return {number} The low 32-bits as an unsigned value. */
  goog.math.Long.prototype.getLowBitsUnsigned = function() {
    return (this.low_ >= 0) ?
        this.low_ : goog.math.Long.TWO_PWR_32_DBL_ + this.low_;
  };


  /**
   * @return {number} Returns the number of bits needed to represent the absolute
   *     value of this Long.
   */
  goog.math.Long.prototype.getNumBitsAbs = function() {
    if (this.isNegative()) {
      if (this.equals(goog.math.Long.MIN_VALUE)) {
        return 64;
      } else {
        return this.negate().getNumBitsAbs();
      }
    } else {
      var val = this.high_ != 0 ? this.high_ : this.low_;
      for (var bit = 31; bit > 0; bit--) {
        if ((val & (1 << bit)) != 0) {
          break;
        }
      }
      return this.high_ != 0 ? bit + 33 : bit + 1;
    }
  };


  /** @return {boolean} Whether this value is zero. */
  goog.math.Long.prototype.isZero = function() {
    return this.high_ == 0 && this.low_ == 0;
  };


  /** @return {boolean} Whether this value is negative. */
  goog.math.Long.prototype.isNegative = function() {
    return this.high_ < 0;
  };


  /** @return {boolean} Whether this value is odd. */
  goog.math.Long.prototype.isOdd = function() {
    return (this.low_ & 1) == 1;
  };


  /**
   * @param {goog.math.Long} other Long to compare against.
   * @return {boolean} Whether this Long equals the other.
   */
  goog.math.Long.prototype.equals = function(other) {
    return (this.high_ == other.high_) && (this.low_ == other.low_);
  };


  /**
   * @param {goog.math.Long} other Long to compare against.
   * @return {boolean} Whether this Long does not equal the other.
   */
  goog.math.Long.prototype.notEquals = function(other) {
    return (this.high_ != other.high_) || (this.low_ != other.low_);
  };


  /**
   * @param {goog.math.Long} other Long to compare against.
   * @return {boolean} Whether this Long is less than the other.
   */
  goog.math.Long.prototype.lessThan = function(other) {
    return this.compare(other) < 0;
  };


  /**
   * @param {goog.math.Long} other Long to compare against.
   * @return {boolean} Whether this Long is less than or equal to the other.
   */
  goog.math.Long.prototype.lessThanOrEqual = function(other) {
    return this.compare(other) <= 0;
  };


  /**
   * @param {goog.math.Long} other Long to compare against.
   * @return {boolean} Whether this Long is greater than the other.
   */
  goog.math.Long.prototype.greaterThan = function(other) {
    return this.compare(other) > 0;
  };


  /**
   * @param {goog.math.Long} other Long to compare against.
   * @return {boolean} Whether this Long is greater than or equal to the other.
   */
  goog.math.Long.prototype.greaterThanOrEqual = function(other) {
    return this.compare(other) >= 0;
  };


  /**
   * Compares this Long with the given one.
   * @param {goog.math.Long} other Long to compare against.
   * @return {number} 0 if they are the same, 1 if the this is greater, and -1
   *     if the given one is greater.
   */
  goog.math.Long.prototype.compare = function(other) {
    if (this.equals(other)) {
      return 0;
    }

    var thisNeg = this.isNegative();
    var otherNeg = other.isNegative();
    if (thisNeg && !otherNeg) {
      return -1;
    }
    if (!thisNeg && otherNeg) {
      return 1;
    }

    // at this point, the signs are the same, so subtraction will not overflow
    if (this.subtract(other).isNegative()) {
      return -1;
    } else {
      return 1;
    }
  };


  /** @return {!goog.math.Long} The negation of this value. */
  goog.math.Long.prototype.negate = function() {
    if (this.equals(goog.math.Long.MIN_VALUE)) {
      return goog.math.Long.MIN_VALUE;
    } else {
      return this.not().add(goog.math.Long.ONE);
    }
  };


  /**
   * Returns the sum of this and the given Long.
   * @param {goog.math.Long} other Long to add to this one.
   * @return {!goog.math.Long} The sum of this and the given Long.
   */
  goog.math.Long.prototype.add = function(other) {
    // Divide each number into 4 chunks of 16 bits, and then sum the chunks.

    var a48 = this.high_ >>> 16;
    var a32 = this.high_ & 0xFFFF;
    var a16 = this.low_ >>> 16;
    var a00 = this.low_ & 0xFFFF;

    var b48 = other.high_ >>> 16;
    var b32 = other.high_ & 0xFFFF;
    var b16 = other.low_ >>> 16;
    var b00 = other.low_ & 0xFFFF;

    var c48 = 0, c32 = 0, c16 = 0, c00 = 0;
    c00 += a00 + b00;
    c16 += c00 >>> 16;
    c00 &= 0xFFFF;
    c16 += a16 + b16;
    c32 += c16 >>> 16;
    c16 &= 0xFFFF;
    c32 += a32 + b32;
    c48 += c32 >>> 16;
    c32 &= 0xFFFF;
    c48 += a48 + b48;
    c48 &= 0xFFFF;
    return goog.math.Long.fromBits((c16 << 16) | c00, (c48 << 16) | c32);
  };


  /**
   * Returns the difference of this and the given Long.
   * @param {goog.math.Long} other Long to subtract from this.
   * @return {!goog.math.Long} The difference of this and the given Long.
   */
  goog.math.Long.prototype.subtract = function(other) {
    return this.add(other.negate());
  };


  /**
   * Returns the product of this and the given long.
   * @param {goog.math.Long} other Long to multiply with this.
   * @return {!goog.math.Long} The product of this and the other.
   */
  goog.math.Long.prototype.multiply = function(other) {
    if (this.isZero()) {
      return goog.math.Long.ZERO;
    } else if (other.isZero()) {
      return goog.math.Long.ZERO;
    }

    if (this.equals(goog.math.Long.MIN_VALUE)) {
      return other.isOdd() ? goog.math.Long.MIN_VALUE : goog.math.Long.ZERO;
    } else if (other.equals(goog.math.Long.MIN_VALUE)) {
      return this.isOdd() ? goog.math.Long.MIN_VALUE : goog.math.Long.ZERO;
    }

    if (this.isNegative()) {
      if (other.isNegative()) {
        return this.negate().multiply(other.negate());
      } else {
        return this.negate().multiply(other).negate();
      }
    } else if (other.isNegative()) {
      return this.multiply(other.negate()).negate();
    }

    // If both longs are small, use float multiplication
    if (this.lessThan(goog.math.Long.TWO_PWR_24_) &&
        other.lessThan(goog.math.Long.TWO_PWR_24_)) {
      return goog.math.Long.fromNumber(this.toNumber() * other.toNumber());
    }

    // Divide each long into 4 chunks of 16 bits, and then add up 4x4 products.
    // We can skip products that would overflow.

    var a48 = this.high_ >>> 16;
    var a32 = this.high_ & 0xFFFF;
    var a16 = this.low_ >>> 16;
    var a00 = this.low_ & 0xFFFF;

    var b48 = other.high_ >>> 16;
    var b32 = other.high_ & 0xFFFF;
    var b16 = other.low_ >>> 16;
    var b00 = other.low_ & 0xFFFF;

    var c48 = 0, c32 = 0, c16 = 0, c00 = 0;
    c00 += a00 * b00;
    c16 += c00 >>> 16;
    c00 &= 0xFFFF;
    c16 += a16 * b00;
    c32 += c16 >>> 16;
    c16 &= 0xFFFF;
    c16 += a00 * b16;
    c32 += c16 >>> 16;
    c16 &= 0xFFFF;
    c32 += a32 * b00;
    c48 += c32 >>> 16;
    c32 &= 0xFFFF;
    c32 += a16 * b16;
    c48 += c32 >>> 16;
    c32 &= 0xFFFF;
    c32 += a00 * b32;
    c48 += c32 >>> 16;
    c32 &= 0xFFFF;
    c48 += a48 * b00 + a32 * b16 + a16 * b32 + a00 * b48;
    c48 &= 0xFFFF;
    return goog.math.Long.fromBits((c16 << 16) | c00, (c48 << 16) | c32);
  };


  /**
   * Returns this Long divided by the given one.
   * @param {goog.math.Long} other Long by which to divide.
   * @return {!goog.math.Long} This Long divided by the given one.
   */
  goog.math.Long.prototype.div = function(other) {
    if (other.isZero()) {
      throw Error('division by zero');
    } else if (this.isZero()) {
      return goog.math.Long.ZERO;
    }

    if (this.equals(goog.math.Long.MIN_VALUE)) {
      if (other.equals(goog.math.Long.ONE) ||
          other.equals(goog.math.Long.NEG_ONE)) {
        return goog.math.Long.MIN_VALUE;  // recall that -MIN_VALUE == MIN_VALUE
      } else if (other.equals(goog.math.Long.MIN_VALUE)) {
        return goog.math.Long.ONE;
      } else {
        // At this point, we have |other| >= 2, so |this/other| < |MIN_VALUE|.
        var halfThis = this.shiftRight(1);
        var approx = halfThis.div(other).shiftLeft(1);
        if (approx.equals(goog.math.Long.ZERO)) {
          return other.isNegative() ? goog.math.Long.ONE : goog.math.Long.NEG_ONE;
        } else {
          var rem = this.subtract(other.multiply(approx));
          var result = approx.add(rem.div(other));
          return result;
        }
      }
    } else if (other.equals(goog.math.Long.MIN_VALUE)) {
      return goog.math.Long.ZERO;
    }

    if (this.isNegative()) {
      if (other.isNegative()) {
        return this.negate().div(other.negate());
      } else {
        return this.negate().div(other).negate();
      }
    } else if (other.isNegative()) {
      return this.div(other.negate()).negate();
    }

    // Repeat the following until the remainder is less than other:  find a
    // floating-point that approximates remainder / other *from below*, add this
    // into the result, and subtract it from the remainder.  It is critical that
    // the approximate value is less than or equal to the real value so that the
    // remainder never becomes negative.
    var res = goog.math.Long.ZERO;
    var rem = this;
    while (rem.greaterThanOrEqual(other)) {
      // Approximate the result of division. This may be a little greater or
      // smaller than the actual value.
      var approx = Math.max(1, Math.floor(rem.toNumber() / other.toNumber()));

      // We will tweak the approximate result by changing it in the 48-th digit or
      // the smallest non-fractional digit, whichever is larger.
      var log2 = Math.ceil(Math.log(approx) / Math.LN2);
      var delta = (log2 <= 48) ? 1 : Math.pow(2, log2 - 48);

      // Decrease the approximation until it is smaller than the remainder.  Note
      // that if it is too large, the product overflows and is negative.
      var approxRes = goog.math.Long.fromNumber(approx);
      var approxRem = approxRes.multiply(other);
      while (approxRem.isNegative() || approxRem.greaterThan(rem)) {
        approx -= delta;
        approxRes = goog.math.Long.fromNumber(approx);
        approxRem = approxRes.multiply(other);
      }

      // We know the answer can't be zero... and actually, zero would cause
      // infinite recursion since we would make no progress.
      if (approxRes.isZero()) {
        approxRes = goog.math.Long.ONE;
      }

      res = res.add(approxRes);
      rem = rem.subtract(approxRem);
    }
    return res;
  };


  /**
   * Returns this Long modulo the given one.
   * @param {goog.math.Long} other Long by which to mod.
   * @return {!goog.math.Long} This Long modulo the given one.
   */
  goog.math.Long.prototype.modulo = function(other) {
    return this.subtract(this.div(other).multiply(other));
  };


  /** @return {!goog.math.Long} The bitwise-NOT of this value. */
  goog.math.Long.prototype.not = function() {
    return goog.math.Long.fromBits(~this.low_, ~this.high_);
  };


  /**
   * Returns the bitwise-AND of this Long and the given one.
   * @param {goog.math.Long} other The Long with which to AND.
   * @return {!goog.math.Long} The bitwise-AND of this and the other.
   */
  goog.math.Long.prototype.and = function(other) {
    return goog.math.Long.fromBits(this.low_ & other.low_,
                                   this.high_ & other.high_);
  };


  /**
   * Returns the bitwise-OR of this Long and the given one.
   * @param {goog.math.Long} other The Long with which to OR.
   * @return {!goog.math.Long} The bitwise-OR of this and the other.
   */
  goog.math.Long.prototype.or = function(other) {
    return goog.math.Long.fromBits(this.low_ | other.low_,
                                   this.high_ | other.high_);
  };


  /**
   * Returns the bitwise-XOR of this Long and the given one.
   * @param {goog.math.Long} other The Long with which to XOR.
   * @return {!goog.math.Long} The bitwise-XOR of this and the other.
   */
  goog.math.Long.prototype.xor = function(other) {
    return goog.math.Long.fromBits(this.low_ ^ other.low_,
                                   this.high_ ^ other.high_);
  };


  /**
   * Returns this Long with bits shifted to the left by the given amount.
   * @param {number} numBits The number of bits by which to shift.
   * @return {!goog.math.Long} This shifted to the left by the given amount.
   */
  goog.math.Long.prototype.shiftLeft = function(numBits) {
    numBits &= 63;
    if (numBits == 0) {
      return this;
    } else {
      var low = this.low_;
      if (numBits < 32) {
        var high = this.high_;
        return goog.math.Long.fromBits(
            low << numBits,
            (high << numBits) | (low >>> (32 - numBits)));
      } else {
        return goog.math.Long.fromBits(0, low << (numBits - 32));
      }
    }
  };


  /**
   * Returns this Long with bits shifted to the right by the given amount.
   * @param {number} numBits The number of bits by which to shift.
   * @return {!goog.math.Long} This shifted to the right by the given amount.
   */
  goog.math.Long.prototype.shiftRight = function(numBits) {
    numBits &= 63;
    if (numBits == 0) {
      return this;
    } else {
      var high = this.high_;
      if (numBits < 32) {
        var low = this.low_;
        return goog.math.Long.fromBits(
            (low >>> numBits) | (high << (32 - numBits)),
            high >> numBits);
      } else {
        return goog.math.Long.fromBits(
            high >> (numBits - 32),
            high >= 0 ? 0 : -1);
      }
    }
  };


  /**
   * Returns this Long with bits shifted to the right by the given amount, with
   * the new top bits matching the current sign bit.
   * @param {number} numBits The number of bits by which to shift.
   * @return {!goog.math.Long} This shifted to the right by the given amount, with
   *     zeros placed into the new leading bits.
   */
  goog.math.Long.prototype.shiftRightUnsigned = function(numBits) {
    numBits &= 63;
    if (numBits == 0) {
      return this;
    } else {
      var high = this.high_;
      if (numBits < 32) {
        var low = this.low_;
        return goog.math.Long.fromBits(
            (low >>> numBits) | (high << (32 - numBits)),
            high >>> numBits);
      } else if (numBits == 32) {
        return goog.math.Long.fromBits(high, 0);
      } else {
        return goog.math.Long.fromBits(high >>> (numBits - 32), 0);
      }
    }
  };

  //======= begin jsbn =======

  var navigator = { appName: 'Modern Browser' }; // polyfill a little

  // Copyright (c) 2005  Tom Wu
  // All Rights Reserved.
  // http://www-cs-students.stanford.edu/~tjw/jsbn/

  /*
   * Copyright (c) 2003-2005  Tom Wu
   * All Rights Reserved.
   *
   * Permission is hereby granted, free of charge, to any person obtaining
   * a copy of this software and associated documentation files (the
   * "Software"), to deal in the Software without restriction, including
   * without limitation the rights to use, copy, modify, merge, publish,
   * distribute, sublicense, and/or sell copies of the Software, and to
   * permit persons to whom the Software is furnished to do so, subject to
   * the following conditions:
   *
   * The above copyright notice and this permission notice shall be
   * included in all copies or substantial portions of the Software.
   *
   * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND, 
   * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY 
   * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.  
   *
   * IN NO EVENT SHALL TOM WU BE LIABLE FOR ANY SPECIAL, INCIDENTAL,
   * INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND, OR ANY DAMAGES WHATSOEVER
   * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER OR NOT ADVISED OF
   * THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF LIABILITY, ARISING OUT
   * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
   *
   * In addition, the following condition applies:
   *
   * All redistributions must retain an intact copy of this copyright notice
   * and disclaimer.
   */

  // Basic JavaScript BN library - subset useful for RSA encryption.

  // Bits per digit
  var dbits;

  // JavaScript engine analysis
  var canary = 0xdeadbeefcafe;
  var j_lm = ((canary&0xffffff)==0xefcafe);

  // (public) Constructor
  function BigInteger(a,b,c) {
    if(a != null)
      if("number" == typeof a) this.fromNumber(a,b,c);
      else if(b == null && "string" != typeof a) this.fromString(a,256);
      else this.fromString(a,b);
  }

  // return new, unset BigInteger
  function nbi() { return new BigInteger(null); }

  // am: Compute w_j += (x*this_i), propagate carries,
  // c is initial carry, returns final carry.
  // c < 3*dvalue, x < 2*dvalue, this_i < dvalue
  // We need to select the fastest one that works in this environment.

  // am1: use a single mult and divide to get the high bits,
  // max digit bits should be 26 because
  // max internal value = 2*dvalue^2-2*dvalue (< 2^53)
  function am1(i,x,w,j,c,n) {
    while(--n >= 0) {
      var v = x*this[i++]+w[j]+c;
      c = Math.floor(v/0x4000000);
      w[j++] = v&0x3ffffff;
    }
    return c;
  }
  // am2 avoids a big mult-and-extract completely.
  // Max digit bits should be <= 30 because we do bitwise ops
  // on values up to 2*hdvalue^2-hdvalue-1 (< 2^31)
  function am2(i,x,w,j,c,n) {
    var xl = x&0x7fff, xh = x>>15;
    while(--n >= 0) {
      var l = this[i]&0x7fff;
      var h = this[i++]>>15;
      var m = xh*l+h*xl;
      l = xl*l+((m&0x7fff)<<15)+w[j]+(c&0x3fffffff);
      c = (l>>>30)+(m>>>15)+xh*h+(c>>>30);
      w[j++] = l&0x3fffffff;
    }
    return c;
  }
  // Alternately, set max digit bits to 28 since some
  // browsers slow down when dealing with 32-bit numbers.
  function am3(i,x,w,j,c,n) {
    var xl = x&0x3fff, xh = x>>14;
    while(--n >= 0) {
      var l = this[i]&0x3fff;
      var h = this[i++]>>14;
      var m = xh*l+h*xl;
      l = xl*l+((m&0x3fff)<<14)+w[j]+c;
      c = (l>>28)+(m>>14)+xh*h;
      w[j++] = l&0xfffffff;
    }
    return c;
  }
  if(j_lm && (navigator.appName == "Microsoft Internet Explorer")) {
    BigInteger.prototype.am = am2;
    dbits = 30;
  }
  else if(j_lm && (navigator.appName != "Netscape")) {
    BigInteger.prototype.am = am1;
    dbits = 26;
  }
  else { // Mozilla/Netscape seems to prefer am3
    BigInteger.prototype.am = am3;
    dbits = 28;
  }

  BigInteger.prototype.DB = dbits;
  BigInteger.prototype.DM = ((1<<dbits)-1);
  BigInteger.prototype.DV = (1<<dbits);

  var BI_FP = 52;
  BigInteger.prototype.FV = Math.pow(2,BI_FP);
  BigInteger.prototype.F1 = BI_FP-dbits;
  BigInteger.prototype.F2 = 2*dbits-BI_FP;

  // Digit conversions
  var BI_RM = "0123456789abcdefghijklmnopqrstuvwxyz";
  var BI_RC = new Array();
  var rr,vv;
  rr = "0".charCodeAt(0);
  for(vv = 0; vv <= 9; ++vv) BI_RC[rr++] = vv;
  rr = "a".charCodeAt(0);
  for(vv = 10; vv < 36; ++vv) BI_RC[rr++] = vv;
  rr = "A".charCodeAt(0);
  for(vv = 10; vv < 36; ++vv) BI_RC[rr++] = vv;

  function int2char(n) { return BI_RM.charAt(n); }
  function intAt(s,i) {
    var c = BI_RC[s.charCodeAt(i)];
    return (c==null)?-1:c;
  }

  // (protected) copy this to r
  function bnpCopyTo(r) {
    for(var i = this.t-1; i >= 0; --i) r[i] = this[i];
    r.t = this.t;
    r.s = this.s;
  }

  // (protected) set from integer value x, -DV <= x < DV
  function bnpFromInt(x) {
    this.t = 1;
    this.s = (x<0)?-1:0;
    if(x > 0) this[0] = x;
    else if(x < -1) this[0] = x+DV;
    else this.t = 0;
  }

  // return bigint initialized to value
  function nbv(i) { var r = nbi(); r.fromInt(i); return r; }

  // (protected) set from string and radix
  function bnpFromString(s,b) {
    var k;
    if(b == 16) k = 4;
    else if(b == 8) k = 3;
    else if(b == 256) k = 8; // byte array
    else if(b == 2) k = 1;
    else if(b == 32) k = 5;
    else if(b == 4) k = 2;
    else { this.fromRadix(s,b); return; }
    this.t = 0;
    this.s = 0;
    var i = s.length, mi = false, sh = 0;
    while(--i >= 0) {
      var x = (k==8)?s[i]&0xff:intAt(s,i);
      if(x < 0) {
        if(s.charAt(i) == "-") mi = true;
        continue;
      }
      mi = false;
      if(sh == 0)
        this[this.t++] = x;
      else if(sh+k > this.DB) {
        this[this.t-1] |= (x&((1<<(this.DB-sh))-1))<<sh;
        this[this.t++] = (x>>(this.DB-sh));
      }
      else
        this[this.t-1] |= x<<sh;
      sh += k;
      if(sh >= this.DB) sh -= this.DB;
    }
    if(k == 8 && (s[0]&0x80) != 0) {
      this.s = -1;
      if(sh > 0) this[this.t-1] |= ((1<<(this.DB-sh))-1)<<sh;
    }
    this.clamp();
    if(mi) BigInteger.ZERO.subTo(this,this);
  }

  // (protected) clamp off excess high words
  function bnpClamp() {
    var c = this.s&this.DM;
    while(this.t > 0 && this[this.t-1] == c) --this.t;
  }

  // (public) return string representation in given radix
  function bnToString(b) {
    if(this.s < 0) return "-"+this.negate().toString(b);
    var k;
    if(b == 16) k = 4;
    else if(b == 8) k = 3;
    else if(b == 2) k = 1;
    else if(b == 32) k = 5;
    else if(b == 4) k = 2;
    else return this.toRadix(b);
    var km = (1<<k)-1, d, m = false, r = "", i = this.t;
    var p = this.DB-(i*this.DB)%k;
    if(i-- > 0) {
      if(p < this.DB && (d = this[i]>>p) > 0) { m = true; r = int2char(d); }
      while(i >= 0) {
        if(p < k) {
          d = (this[i]&((1<<p)-1))<<(k-p);
          d |= this[--i]>>(p+=this.DB-k);
        }
        else {
          d = (this[i]>>(p-=k))&km;
          if(p <= 0) { p += this.DB; --i; }
        }
        if(d > 0) m = true;
        if(m) r += int2char(d);
      }
    }
    return m?r:"0";
  }

  // (public) -this
  function bnNegate() { var r = nbi(); BigInteger.ZERO.subTo(this,r); return r; }

  // (public) |this|
  function bnAbs() { return (this.s<0)?this.negate():this; }

  // (public) return + if this > a, - if this < a, 0 if equal
  function bnCompareTo(a) {
    var r = this.s-a.s;
    if(r != 0) return r;
    var i = this.t;
    r = i-a.t;
    if(r != 0) return (this.s<0)?-r:r;
    while(--i >= 0) if((r=this[i]-a[i]) != 0) return r;
    return 0;
  }

  // returns bit length of the integer x
  function nbits(x) {
    var r = 1, t;
    if((t=x>>>16) != 0) { x = t; r += 16; }
    if((t=x>>8) != 0) { x = t; r += 8; }
    if((t=x>>4) != 0) { x = t; r += 4; }
    if((t=x>>2) != 0) { x = t; r += 2; }
    if((t=x>>1) != 0) { x = t; r += 1; }
    return r;
  }

  // (public) return the number of bits in "this"
  function bnBitLength() {
    if(this.t <= 0) return 0;
    return this.DB*(this.t-1)+nbits(this[this.t-1]^(this.s&this.DM));
  }

  // (protected) r = this << n*DB
  function bnpDLShiftTo(n,r) {
    var i;
    for(i = this.t-1; i >= 0; --i) r[i+n] = this[i];
    for(i = n-1; i >= 0; --i) r[i] = 0;
    r.t = this.t+n;
    r.s = this.s;
  }

  // (protected) r = this >> n*DB
  function bnpDRShiftTo(n,r) {
    for(var i = n; i < this.t; ++i) r[i-n] = this[i];
    r.t = Math.max(this.t-n,0);
    r.s = this.s;
  }

  // (protected) r = this << n
  function bnpLShiftTo(n,r) {
    var bs = n%this.DB;
    var cbs = this.DB-bs;
    var bm = (1<<cbs)-1;
    var ds = Math.floor(n/this.DB), c = (this.s<<bs)&this.DM, i;
    for(i = this.t-1; i >= 0; --i) {
      r[i+ds+1] = (this[i]>>cbs)|c;
      c = (this[i]&bm)<<bs;
    }
    for(i = ds-1; i >= 0; --i) r[i] = 0;
    r[ds] = c;
    r.t = this.t+ds+1;
    r.s = this.s;
    r.clamp();
  }

  // (protected) r = this >> n
  function bnpRShiftTo(n,r) {
    r.s = this.s;
    var ds = Math.floor(n/this.DB);
    if(ds >= this.t) { r.t = 0; return; }
    var bs = n%this.DB;
    var cbs = this.DB-bs;
    var bm = (1<<bs)-1;
    r[0] = this[ds]>>bs;
    for(var i = ds+1; i < this.t; ++i) {
      r[i-ds-1] |= (this[i]&bm)<<cbs;
      r[i-ds] = this[i]>>bs;
    }
    if(bs > 0) r[this.t-ds-1] |= (this.s&bm)<<cbs;
    r.t = this.t-ds;
    r.clamp();
  }

  // (protected) r = this - a
  function bnpSubTo(a,r) {
    var i = 0, c = 0, m = Math.min(a.t,this.t);
    while(i < m) {
      c += this[i]-a[i];
      r[i++] = c&this.DM;
      c >>= this.DB;
    }
    if(a.t < this.t) {
      c -= a.s;
      while(i < this.t) {
        c += this[i];
        r[i++] = c&this.DM;
        c >>= this.DB;
      }
      c += this.s;
    }
    else {
      c += this.s;
      while(i < a.t) {
        c -= a[i];
        r[i++] = c&this.DM;
        c >>= this.DB;
      }
      c -= a.s;
    }
    r.s = (c<0)?-1:0;
    if(c < -1) r[i++] = this.DV+c;
    else if(c > 0) r[i++] = c;
    r.t = i;
    r.clamp();
  }

  // (protected) r = this * a, r != this,a (HAC 14.12)
  // "this" should be the larger one if appropriate.
  function bnpMultiplyTo(a,r) {
    var x = this.abs(), y = a.abs();
    var i = x.t;
    r.t = i+y.t;
    while(--i >= 0) r[i] = 0;
    for(i = 0; i < y.t; ++i) r[i+x.t] = x.am(0,y[i],r,i,0,x.t);
    r.s = 0;
    r.clamp();
    if(this.s != a.s) BigInteger.ZERO.subTo(r,r);
  }

  // (protected) r = this^2, r != this (HAC 14.16)
  function bnpSquareTo(r) {
    var x = this.abs();
    var i = r.t = 2*x.t;
    while(--i >= 0) r[i] = 0;
    for(i = 0; i < x.t-1; ++i) {
      var c = x.am(i,x[i],r,2*i,0,1);
      if((r[i+x.t]+=x.am(i+1,2*x[i],r,2*i+1,c,x.t-i-1)) >= x.DV) {
        r[i+x.t] -= x.DV;
        r[i+x.t+1] = 1;
      }
    }
    if(r.t > 0) r[r.t-1] += x.am(i,x[i],r,2*i,0,1);
    r.s = 0;
    r.clamp();
  }

  // (protected) divide this by m, quotient and remainder to q, r (HAC 14.20)
  // r != q, this != m.  q or r may be null.
  function bnpDivRemTo(m,q,r) {
    var pm = m.abs();
    if(pm.t <= 0) return;
    var pt = this.abs();
    if(pt.t < pm.t) {
      if(q != null) q.fromInt(0);
      if(r != null) this.copyTo(r);
      return;
    }
    if(r == null) r = nbi();
    var y = nbi(), ts = this.s, ms = m.s;
    var nsh = this.DB-nbits(pm[pm.t-1]);	// normalize modulus
    if(nsh > 0) { pm.lShiftTo(nsh,y); pt.lShiftTo(nsh,r); }
    else { pm.copyTo(y); pt.copyTo(r); }
    var ys = y.t;
    var y0 = y[ys-1];
    if(y0 == 0) return;
    var yt = y0*(1<<this.F1)+((ys>1)?y[ys-2]>>this.F2:0);
    var d1 = this.FV/yt, d2 = (1<<this.F1)/yt, e = 1<<this.F2;
    var i = r.t, j = i-ys, t = (q==null)?nbi():q;
    y.dlShiftTo(j,t);
    if(r.compareTo(t) >= 0) {
      r[r.t++] = 1;
      r.subTo(t,r);
    }
    BigInteger.ONE.dlShiftTo(ys,t);
    t.subTo(y,y);	// "negative" y so we can replace sub with am later
    while(y.t < ys) y[y.t++] = 0;
    while(--j >= 0) {
      // Estimate quotient digit
      var qd = (r[--i]==y0)?this.DM:Math.floor(r[i]*d1+(r[i-1]+e)*d2);
      if((r[i]+=y.am(0,qd,r,j,0,ys)) < qd) {	// Try it out
        y.dlShiftTo(j,t);
        r.subTo(t,r);
        while(r[i] < --qd) r.subTo(t,r);
      }
    }
    if(q != null) {
      r.drShiftTo(ys,q);
      if(ts != ms) BigInteger.ZERO.subTo(q,q);
    }
    r.t = ys;
    r.clamp();
    if(nsh > 0) r.rShiftTo(nsh,r);	// Denormalize remainder
    if(ts < 0) BigInteger.ZERO.subTo(r,r);
  }

  // (public) this mod a
  function bnMod(a) {
    var r = nbi();
    this.abs().divRemTo(a,null,r);
    if(this.s < 0 && r.compareTo(BigInteger.ZERO) > 0) a.subTo(r,r);
    return r;
  }

  // Modular reduction using "classic" algorithm
  function Classic(m) { this.m = m; }
  function cConvert(x) {
    if(x.s < 0 || x.compareTo(this.m) >= 0) return x.mod(this.m);
    else return x;
  }
  function cRevert(x) { return x; }
  function cReduce(x) { x.divRemTo(this.m,null,x); }
  function cMulTo(x,y,r) { x.multiplyTo(y,r); this.reduce(r); }
  function cSqrTo(x,r) { x.squareTo(r); this.reduce(r); }

  Classic.prototype.convert = cConvert;
  Classic.prototype.revert = cRevert;
  Classic.prototype.reduce = cReduce;
  Classic.prototype.mulTo = cMulTo;
  Classic.prototype.sqrTo = cSqrTo;

  // (protected) return "-1/this % 2^DB"; useful for Mont. reduction
  // justification:
  //         xy == 1 (mod m)
  //         xy =  1+km
  //   xy(2-xy) = (1+km)(1-km)
  // x[y(2-xy)] = 1-k^2m^2
  // x[y(2-xy)] == 1 (mod m^2)
  // if y is 1/x mod m, then y(2-xy) is 1/x mod m^2
  // should reduce x and y(2-xy) by m^2 at each step to keep size bounded.
  // JS multiply "overflows" differently from C/C++, so care is needed here.
  function bnpInvDigit() {
    if(this.t < 1) return 0;
    var x = this[0];
    if((x&1) == 0) return 0;
    var y = x&3;		// y == 1/x mod 2^2
    y = (y*(2-(x&0xf)*y))&0xf;	// y == 1/x mod 2^4
    y = (y*(2-(x&0xff)*y))&0xff;	// y == 1/x mod 2^8
    y = (y*(2-(((x&0xffff)*y)&0xffff)))&0xffff;	// y == 1/x mod 2^16
    // last step - calculate inverse mod DV directly;
    // assumes 16 < DB <= 32 and assumes ability to handle 48-bit ints
    y = (y*(2-x*y%this.DV))%this.DV;		// y == 1/x mod 2^dbits
    // we really want the negative inverse, and -DV < y < DV
    return (y>0)?this.DV-y:-y;
  }

  // Montgomery reduction
  function Montgomery(m) {
    this.m = m;
    this.mp = m.invDigit();
    this.mpl = this.mp&0x7fff;
    this.mph = this.mp>>15;
    this.um = (1<<(m.DB-15))-1;
    this.mt2 = 2*m.t;
  }

  // xR mod m
  function montConvert(x) {
    var r = nbi();
    x.abs().dlShiftTo(this.m.t,r);
    r.divRemTo(this.m,null,r);
    if(x.s < 0 && r.compareTo(BigInteger.ZERO) > 0) this.m.subTo(r,r);
    return r;
  }

  // x/R mod m
  function montRevert(x) {
    var r = nbi();
    x.copyTo(r);
    this.reduce(r);
    return r;
  }

  // x = x/R mod m (HAC 14.32)
  function montReduce(x) {
    while(x.t <= this.mt2)	// pad x so am has enough room later
      x[x.t++] = 0;
    for(var i = 0; i < this.m.t; ++i) {
      // faster way of calculating u0 = x[i]*mp mod DV
      var j = x[i]&0x7fff;
      var u0 = (j*this.mpl+(((j*this.mph+(x[i]>>15)*this.mpl)&this.um)<<15))&x.DM;
      // use am to combine the multiply-shift-add into one call
      j = i+this.m.t;
      x[j] += this.m.am(0,u0,x,i,0,this.m.t);
      // propagate carry
      while(x[j] >= x.DV) { x[j] -= x.DV; x[++j]++; }
    }
    x.clamp();
    x.drShiftTo(this.m.t,x);
    if(x.compareTo(this.m) >= 0) x.subTo(this.m,x);
  }

  // r = "x^2/R mod m"; x != r
  function montSqrTo(x,r) { x.squareTo(r); this.reduce(r); }

  // r = "xy/R mod m"; x,y != r
  function montMulTo(x,y,r) { x.multiplyTo(y,r); this.reduce(r); }

  Montgomery.prototype.convert = montConvert;
  Montgomery.prototype.revert = montRevert;
  Montgomery.prototype.reduce = montReduce;
  Montgomery.prototype.mulTo = montMulTo;
  Montgomery.prototype.sqrTo = montSqrTo;

  // (protected) true iff this is even
  function bnpIsEven() { return ((this.t>0)?(this[0]&1):this.s) == 0; }

  // (protected) this^e, e < 2^32, doing sqr and mul with "r" (HAC 14.79)
  function bnpExp(e,z) {
    if(e > 0xffffffff || e < 1) return BigInteger.ONE;
    var r = nbi(), r2 = nbi(), g = z.convert(this), i = nbits(e)-1;
    g.copyTo(r);
    while(--i >= 0) {
      z.sqrTo(r,r2);
      if((e&(1<<i)) > 0) z.mulTo(r2,g,r);
      else { var t = r; r = r2; r2 = t; }
    }
    return z.revert(r);
  }

  // (public) this^e % m, 0 <= e < 2^32
  function bnModPowInt(e,m) {
    var z;
    if(e < 256 || m.isEven()) z = new Classic(m); else z = new Montgomery(m);
    return this.exp(e,z);
  }

  // protected
  BigInteger.prototype.copyTo = bnpCopyTo;
  BigInteger.prototype.fromInt = bnpFromInt;
  BigInteger.prototype.fromString = bnpFromString;
  BigInteger.prototype.clamp = bnpClamp;
  BigInteger.prototype.dlShiftTo = bnpDLShiftTo;
  BigInteger.prototype.drShiftTo = bnpDRShiftTo;
  BigInteger.prototype.lShiftTo = bnpLShiftTo;
  BigInteger.prototype.rShiftTo = bnpRShiftTo;
  BigInteger.prototype.subTo = bnpSubTo;
  BigInteger.prototype.multiplyTo = bnpMultiplyTo;
  BigInteger.prototype.squareTo = bnpSquareTo;
  BigInteger.prototype.divRemTo = bnpDivRemTo;
  BigInteger.prototype.invDigit = bnpInvDigit;
  BigInteger.prototype.isEven = bnpIsEven;
  BigInteger.prototype.exp = bnpExp;

  // public
  BigInteger.prototype.toString = bnToString;
  BigInteger.prototype.negate = bnNegate;
  BigInteger.prototype.abs = bnAbs;
  BigInteger.prototype.compareTo = bnCompareTo;
  BigInteger.prototype.bitLength = bnBitLength;
  BigInteger.prototype.mod = bnMod;
  BigInteger.prototype.modPowInt = bnModPowInt;

  // "constants"
  BigInteger.ZERO = nbv(0);
  BigInteger.ONE = nbv(1);

  // jsbn2 stuff

  // (protected) convert from radix string
  function bnpFromRadix(s,b) {
    this.fromInt(0);
    if(b == null) b = 10;
    var cs = this.chunkSize(b);
    var d = Math.pow(b,cs), mi = false, j = 0, w = 0;
    for(var i = 0; i < s.length; ++i) {
      var x = intAt(s,i);
      if(x < 0) {
        if(s.charAt(i) == "-" && this.signum() == 0) mi = true;
        continue;
      }
      w = b*w+x;
      if(++j >= cs) {
        this.dMultiply(d);
        this.dAddOffset(w,0);
        j = 0;
        w = 0;
      }
    }
    if(j > 0) {
      this.dMultiply(Math.pow(b,j));
      this.dAddOffset(w,0);
    }
    if(mi) BigInteger.ZERO.subTo(this,this);
  }

  // (protected) return x s.t. r^x < DV
  function bnpChunkSize(r) { return Math.floor(Math.LN2*this.DB/Math.log(r)); }

  // (public) 0 if this == 0, 1 if this > 0
  function bnSigNum() {
    if(this.s < 0) return -1;
    else if(this.t <= 0 || (this.t == 1 && this[0] <= 0)) return 0;
    else return 1;
  }

  // (protected) this *= n, this >= 0, 1 < n < DV
  function bnpDMultiply(n) {
    this[this.t] = this.am(0,n-1,this,0,0,this.t);
    ++this.t;
    this.clamp();
  }

  // (protected) this += n << w words, this >= 0
  function bnpDAddOffset(n,w) {
    if(n == 0) return;
    while(this.t <= w) this[this.t++] = 0;
    this[w] += n;
    while(this[w] >= this.DV) {
      this[w] -= this.DV;
      if(++w >= this.t) this[this.t++] = 0;
      ++this[w];
    }
  }

  // (protected) convert to radix string
  function bnpToRadix(b) {
    if(b == null) b = 10;
    if(this.signum() == 0 || b < 2 || b > 36) return "0";
    var cs = this.chunkSize(b);
    var a = Math.pow(b,cs);
    var d = nbv(a), y = nbi(), z = nbi(), r = "";
    this.divRemTo(d,y,z);
    while(y.signum() > 0) {
      r = (a+z.intValue()).toString(b).substr(1) + r;
      y.divRemTo(d,y,z);
    }
    return z.intValue().toString(b) + r;
  }

  // (public) return value as integer
  function bnIntValue() {
    if(this.s < 0) {
      if(this.t == 1) return this[0]-this.DV;
      else if(this.t == 0) return -1;
    }
    else if(this.t == 1) return this[0];
    else if(this.t == 0) return 0;
    // assumes 16 < DB < 32
    return ((this[1]&((1<<(32-this.DB))-1))<<this.DB)|this[0];
  }

  // (protected) r = this + a
  function bnpAddTo(a,r) {
    var i = 0, c = 0, m = Math.min(a.t,this.t);
    while(i < m) {
      c += this[i]+a[i];
      r[i++] = c&this.DM;
      c >>= this.DB;
    }
    if(a.t < this.t) {
      c += a.s;
      while(i < this.t) {
        c += this[i];
        r[i++] = c&this.DM;
        c >>= this.DB;
      }
      c += this.s;
    }
    else {
      c += this.s;
      while(i < a.t) {
        c += a[i];
        r[i++] = c&this.DM;
        c >>= this.DB;
      }
      c += a.s;
    }
    r.s = (c<0)?-1:0;
    if(c > 0) r[i++] = c;
    else if(c < -1) r[i++] = this.DV+c;
    r.t = i;
    r.clamp();
  }

  BigInteger.prototype.fromRadix = bnpFromRadix;
  BigInteger.prototype.chunkSize = bnpChunkSize;
  BigInteger.prototype.signum = bnSigNum;
  BigInteger.prototype.dMultiply = bnpDMultiply;
  BigInteger.prototype.dAddOffset = bnpDAddOffset;
  BigInteger.prototype.toRadix = bnpToRadix;
  BigInteger.prototype.intValue = bnIntValue;
  BigInteger.prototype.addTo = bnpAddTo;

  //======= end jsbn =======

  // Emscripten wrapper
  var Wrapper = {
    abs: function(l, h) {
      var x = new goog.math.Long(l, h);
      var ret;
      if (x.isNegative()) {
        ret = x.negate();
      } else {
        ret = x;
      }
      HEAP32[tempDoublePtr>>2] = ret.low_;
      HEAP32[tempDoublePtr+4>>2] = ret.high_;
    },
    ensureTemps: function() {
      if (Wrapper.ensuredTemps) return;
      Wrapper.ensuredTemps = true;
      Wrapper.two32 = new BigInteger();
      Wrapper.two32.fromString('4294967296', 10);
      Wrapper.two64 = new BigInteger();
      Wrapper.two64.fromString('18446744073709551616', 10);
      Wrapper.temp1 = new BigInteger();
      Wrapper.temp2 = new BigInteger();
    },
    lh2bignum: function(l, h) {
      var a = new BigInteger();
      a.fromString(h.toString(), 10);
      var b = new BigInteger();
      a.multiplyTo(Wrapper.two32, b);
      var c = new BigInteger();
      c.fromString(l.toString(), 10);
      var d = new BigInteger();
      c.addTo(b, d);
      return d;
    },
    stringify: function(l, h, unsigned) {
      var ret = new goog.math.Long(l, h).toString();
      if (unsigned && ret[0] == '-') {
        // unsign slowly using jsbn bignums
        Wrapper.ensureTemps();
        var bignum = new BigInteger();
        bignum.fromString(ret, 10);
        ret = new BigInteger();
        Wrapper.two64.addTo(bignum, ret);
        ret = ret.toString(10);
      }
      return ret;
    },
    fromString: function(str, base, min, max, unsigned) {
      Wrapper.ensureTemps();
      var bignum = new BigInteger();
      bignum.fromString(str, base);
      var bigmin = new BigInteger();
      bigmin.fromString(min, 10);
      var bigmax = new BigInteger();
      bigmax.fromString(max, 10);
      if (unsigned && bignum.compareTo(BigInteger.ZERO) < 0) {
        var temp = new BigInteger();
        bignum.addTo(Wrapper.two64, temp);
        bignum = temp;
      }
      var error = false;
      if (bignum.compareTo(bigmin) < 0) {
        bignum = bigmin;
        error = true;
      } else if (bignum.compareTo(bigmax) > 0) {
        bignum = bigmax;
        error = true;
      }
      var ret = goog.math.Long.fromString(bignum.toString()); // min-max checks should have clamped this to a range goog.math.Long can handle well
      HEAP32[tempDoublePtr>>2] = ret.low_;
      HEAP32[tempDoublePtr+4>>2] = ret.high_;
      if (error) throw 'range error';
    }
  };
  return Wrapper;
})();

//======= end closure i64 code =======



// === Auto-generated postamble setup entry stuff ===

if (memoryInitializer) {
  function applyData(data) {
    HEAPU8.set(data, STATIC_BASE);
  }
  if (ENVIRONMENT_IS_NODE || ENVIRONMENT_IS_SHELL) {
    applyData(Module['readBinary'](memoryInitializer));
  } else {
    addRunDependency('memory initializer');
    Browser.asyncLoad(memoryInitializer, function(data) {
      applyData(data);
      removeRunDependency('memory initializer');
    }, function(data) {
      throw 'could not load memory initializer ' + memoryInitializer;
    });
  }
}

function ExitStatus(status) {
  this.name = "ExitStatus";
  this.message = "Program terminated with exit(" + status + ")";
  this.status = status;
};
ExitStatus.prototype = new Error();
ExitStatus.prototype.constructor = ExitStatus;

var initialStackTop;
var preloadStartTime = null;
var calledMain = false;

dependenciesFulfilled = function runCaller() {
  // If run has never been called, and we should call run (INVOKE_RUN is true, and Module.noInitialRun is not false)
  if (!Module['calledRun'] && shouldRunNow) run();
  if (!Module['calledRun']) dependenciesFulfilled = runCaller; // try this again later, after new deps are fulfilled
}

Module['callMain'] = Module.callMain = function callMain(args) {
  assert(runDependencies == 0, 'cannot call main when async dependencies remain! (listen on __ATMAIN__)');
  assert(__ATPRERUN__.length == 0, 'cannot call main when preRun functions remain to be called');

  args = args || [];

  if (ENVIRONMENT_IS_WEB && preloadStartTime !== null) {
    Module.printErr('preload time: ' + (Date.now() - preloadStartTime) + ' ms');
  }

  ensureInitRuntime();

  var argc = args.length+1;
  function pad() {
    for (var i = 0; i < 4-1; i++) {
      argv.push(0);
    }
  }
  var argv = [allocate(intArrayFromString("/bin/this.program"), 'i8', ALLOC_NORMAL) ];
  pad();
  for (var i = 0; i < argc-1; i = i + 1) {
    argv.push(allocate(intArrayFromString(args[i]), 'i8', ALLOC_NORMAL));
    pad();
  }
  argv.push(0);
  argv = allocate(argv, 'i32', ALLOC_NORMAL);

  initialStackTop = STACKTOP;

  try {

    var ret = Module['_main'](argc, argv, 0);


    // if we're not running an evented main loop, it's time to exit
    if (!Module['noExitRuntime']) {
      exit(ret);
    }
  }
  catch(e) {
    if (e instanceof ExitStatus) {
      // exit() throws this once it's done to make sure execution
      // has been stopped completely
      return;
    } else if (e == 'SimulateInfiniteLoop') {
      // running an evented main loop, don't immediately exit
      Module['noExitRuntime'] = true;
      return;
    } else {
      if (e && typeof e === 'object' && e.stack) Module.printErr('exception thrown: ' + [e, e.stack]);
      throw e;
    }
  } finally {
    calledMain = true;
  }
}




function run(args) {
  args = args || Module['arguments'];

  if (preloadStartTime === null) preloadStartTime = Date.now();

  if (runDependencies > 0) {
    Module.printErr('run() called, but dependencies remain, so not running');
    return;
  }

  preRun();

  if (runDependencies > 0) return; // a preRun added a dependency, run will be called later
  if (Module['calledRun']) return; // run may have just been called through dependencies being fulfilled just in this very frame

  function doRun() {
    if (Module['calledRun']) return; // run may have just been called while the async setStatus time below was happening
    Module['calledRun'] = true;

    ensureInitRuntime();

    preMain();

    if (Module['_main'] && shouldRunNow) {
      Module['callMain'](args);
    }

    postRun();
  }

  if (Module['setStatus']) {
    Module['setStatus']('Running...');
    setTimeout(function() {
      setTimeout(function() {
        Module['setStatus']('');
      }, 1);
      if (!ABORT) doRun();
    }, 1);
  } else {
    doRun();
  }
}
Module['run'] = Module.run = run;

function exit(status) {
  ABORT = true;
  EXITSTATUS = status;
  STACKTOP = initialStackTop;

  // exit the runtime
  exitRuntime();

  // TODO We should handle this differently based on environment.
  // In the browser, the best we can do is throw an exception
  // to halt execution, but in node we could process.exit and
  // I'd imagine SM shell would have something equivalent.
  // This would let us set a proper exit status (which
  // would be great for checking test exit statuses).
  // https://github.com/kripken/emscripten/issues/1371

  // throw an exception to halt the current execution
  throw new ExitStatus(status);
}
Module['exit'] = Module.exit = exit;

function abort(text) {
  if (text) {
    Module.print(text);
    Module.printErr(text);
  }

  ABORT = true;
  EXITSTATUS = 1;

  throw 'abort() at ' + stackTrace();
}
Module['abort'] = Module.abort = abort;

// {{PRE_RUN_ADDITIONS}}

if (Module['preInit']) {
  if (typeof Module['preInit'] == 'function') Module['preInit'] = [Module['preInit']];
  while (Module['preInit'].length > 0) {
    Module['preInit'].pop()();
  }
}

// shouldRunNow refers to calling main(), not run().
var shouldRunNow = true;
if (Module['noInitialRun']) {
  shouldRunNow = false;
}

run();

// {{POST_RUN_ADDITIONS}}






// {{MODULE_ADDITIONS}}






