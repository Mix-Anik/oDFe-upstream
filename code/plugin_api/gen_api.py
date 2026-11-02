#!/usr/bin/env python3
"""
Generates the plugin API from its description (client.api, server.api).

  include/<module>.h             what a plugin includes: the functions it can call and the ones it can implement
  include/<module>_syscalls.asm  the engine functions as q3asm sees them (give it to q3asm after the program)
  include/<module>_entry.h       the entry point (vmMain) of a plugin, built from the list of exports it implements
  generated/<cl|sv>_plugin_api.inc   what the engine includes: the dispatcher that checks every call of a plugin
                                 (pointers, capabilities, cheats), the export list and the functions that call the exports

  python gen_api.py            writes the files
  python gen_api.py --check    fails if the files on disk are not what the descriptions generate
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAX_COUNT = 1 << 20
MAX_SLOTS = 14          # the engine gets the number of the function and at most 15 values

SOURCES = [
    ("client.api", "cl_plugin_api.inc"),
    ("server.api", "sv_plugin_api.inc"),
]


class Param:
    def __init__(self, name, kind, ctype=None, count=None, nullable=False):
        self.name = name
        self.kind = kind          # i32 f32 i64 str ptrin ptrout
        self.ctype = ctype        # element type for pointers
        self.count = count        # int, a parameter name, or None (1)
        self.nullable = nullable
        self.buffer = False       # outstr / outbuf / inbuf: bytes, count is the size parameter
        self.cstr = False         # outstr: the buffer gets a string

    def slots(self):
        return 2 if self.kind == "i64" else 1


class Func:
    def __init__(self, kind, name, params, ret, cap, cheat, budget, doc):
        self.kind = kind          # import / export
        self.name = name
        self.params = params
        self.ret = ret            # None, i32, f32
        self.cap = cap
        self.cheat = cheat        # only works while cheats are allowed
        self.budget = budget
        self.doc = doc


class Api:
    def __init__(self):
        self.module = None
        self.version = 1
        self.types = None
        self.caps = []            # (name, restricted, doc)
        self.funcs = []
        self.events = []          # (name, cap, doc)


def split_args(text):
    parts, depth, cur = [], 0, ""
    for ch in text:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        parts.append(cur.strip())
    return parts


def parse_type(name, text):
    text = text.strip()
    if text in ("i32", "f32", "i64"):
        return Param(name, text)
    if text == "str":
        return Param(name, "str")
    if text == "str?":
        return Param(name, "str", nullable=True)
    m = re.fullmatch(r"(in|out)(\?)?\(\s*([A-Za-z_][A-Za-z0-9_ ]*?)\s*(?:,\s*([A-Za-z0-9_]+)\s*)?\)", text)
    if m:
        direction, q, ctype, count = m.groups()
        if count is not None and count.isdigit():
            count = int(count)
        return Param(name, "ptr" + direction, ctype=ctype, count=count, nullable=bool(q))
    m = re.fullmatch(r"(outstr|outbuf|inbuf)(\?)?\(\s*([A-Za-z0-9_]+)\s*\)", text)
    if m:
        what, q, size = m.groups()
        p = Param(name, "ptrin" if what == "inbuf" else "ptrout", ctype="char", count=size, nullable=bool(q))
        p.buffer = True
        p.cstr = what == "outstr"
        return p
    raise SystemExit("unknown type '%s' for %s" % (text, name))


def parse_params(text):
    params = []
    for item in split_args(text):
        name, _, typ = item.partition(":")
        params.append(parse_type(name.strip(), typ))
    return params


def parse_into(api, path):
    doc = []
    sig = re.compile(r"^(import|export)\s+([A-Za-z_][A-Za-z0-9_]*)\s*\((.*)\)\s*(.*)$")
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    for raw in lines:
        line = raw.rstrip()
        if not line.strip():
            doc = []
            continue
        if line.startswith("#"):
            doc.append(line[1:].lstrip(" ") if line[1:2] == " " else line[1:])
            continue
        m = sig.match(line)
        if m:
            kind, name, args, rest = m.groups()
            ret = None
            cap = None
            budget = "frame"
            mm = re.match(r"->\s*(i32|f32)\s*(.*)", rest)
            if mm:
                ret, rest = mm.groups()
            mm = re.search(r"\bcap\s+([a-z]+)", rest)
            if mm:
                cap = mm.group(1)
            cheat = re.search(r"\bcheat\b", rest) is not None
            mm = re.search(r"\bbudget\s+([a-z]+)", rest)
            if mm:
                budget = mm.group(1)
            if cheat and kind != "import":
                raise SystemExit("%s: only an import can be marked cheat" % name)
            api.funcs.append(Func(kind, name, parse_params(args), ret, cap, cheat, budget, [d for d in doc]))
            doc = []
            continue
        tok = line.split(None, 1)
        if tok[0] == "module":
            api.module = tok[1].strip()
        elif tok[0] == "version":
            api.version = int(tok[1])
        elif tok[0] == "types":
            api.types = tok[1].strip()
        elif tok[0] == "include":
            parse_into(api, os.path.join(os.path.dirname(path), tok[1].strip()))
            doc = []
        elif tok[0] == "cap":
            rest = tok[1].split(None, 1)
            name = rest[0]
            text = rest[1] if len(rest) > 1 else ""
            restricted = False
            if text.startswith("restricted"):
                restricted = True
                text = text[len("restricted"):].strip()
            api.caps.append((name, restricted, text))
        elif tok[0] == "event":
            rest = tok[1].split()
            cap = rest[2] if len(rest) >= 3 and rest[1] == "cap" else None
            api.events.append((rest[0], cap, [d for d in doc]))
            doc = []
        else:
            raise SystemExit("cannot parse: " + line)


def parse(path):
    api = Api()
    parse_into(api, path)
    names = [f.name for f in api.funcs]
    for n in names:
        if names.count(n) > 1:
            raise SystemExit("%s is defined twice" % n)
    for f in api.funcs:
        if f.cap and f.cap not in [c[0] for c in api.caps]:
            raise SystemExit("%s uses the unknown capability %s" % (f.name, f.cap))
        if f.kind == "import" and sum(p.slots() for p in f.params) > MAX_SLOTS:
            raise SystemExit("%s has too many arguments" % f.name)
        if f.kind == "export" and len(f.params) > 8:
            raise SystemExit("%s has too many arguments" % f.name)
        for p in f.params:
            if p.kind == "ptrout" and f.kind == "export":
                raise SystemExit("%s: an export can only get in(...) pointers" % f.name)
    return api


# ---------------------------------------------------------------------------
# plugin side
# ---------------------------------------------------------------------------

def ret_ctype(f):
    return {"i32": "int", "f32": "float", None: "void"}[f.ret]


def plugin_ctype(p):
    if p.kind == "i32":
        return "int"
    if p.kind == "f32":
        return "float"
    if p.kind == "str":
        return "const char *"
    if p.buffer:
        if p.kind == "ptrin":
            return "const void *"
        return "char *" if p.cstr else "void *"
    return ("const %s *" if p.kind == "ptrin" else "%s *") % p.ctype


def plugin_params(params):
    """The C parameter list of a function as the plugin sees it. A 64 bit number is two 32 bit ones (the VM has no 64 bit
    integers): the low half first."""
    if not params:
        return "void"
    out = []
    for p in params:
        if p.kind == "i64":
            out.append("unsigned %sLo" % p.name)
            out.append("unsigned %sHi" % p.name)
            continue
        t = plugin_ctype(p)
        out.append(t + ("" if t.endswith("*") else " ") + p.name)
    return ", ".join(out)


def cap_doc(api):
    lines = []
    for name, restricted, text in api.caps:
        lines.append("//   %-10s %s%s" % (name, "(restricted) " if restricted else "", text))
    return lines


def gen_header(api, src):
    M = api.module
    U = M.upper()
    o = []
    o.append("/* Generated from %s by gen_api.py - do not edit. */\n" % src)
    o.append("/*\n===========================================================================\n"
             "oDFe %s plugin API, version %d.\n\n"
             "See odf_common.h for how a plugin is built. The functions below are what the plugin can\n"
             "call (engine functions) and what it can implement for the engine to call (exports).\n"
             "===========================================================================\n*/\n" %
             ("client" if M == "odfc" else "server", api.version))
    o.append("#ifndef %s_H\n#define %s_H\n" % (U, U))
    o.append('#include "odf_common.h"\n#include "%s"\n' % api.types)
    o.append("#define %s_API_VERSION\t%d\n" % (U, api.version))
    o.append("// Capabilities. A plugin that needs a group says so in its manifest (caps=a,b); it is only loaded when it was given")
    o.append("// them, and a call of a function of a group it does not have stops the plugin.")
    o.extend(cap_doc(api))
    o.append("// A function marked 'cheats only' does nothing and returns 0 unless cheats are allowed (see CheatsEnabled): the engine")
    o.append("// decides, not the plugin.")
    o.append("")
    for f in api.funcs:
        if f.kind != "import":
            continue
        o.append("")
        for d in f.doc:
            o.append("//" + (" " + d if d else ""))
        if f.cap:
            o.append("// Capability: %s" % f.cap)
        if f.cheat:
            o.append("// Cheats only.")
        o.append("%s %s( %s );" % (ret_ctype(f), f.name, plugin_params(f.params)))
    o.append("\n\n// ---------------------------------------------------------------------------")
    o.append("// Exports: implement the ones you need with exactly these prototypes and list them in the EXPORTS")
    o.append("// of the entry point (see %s_entry.h)." % M)
    o.append("// ---------------------------------------------------------------------------")
    for f in api.funcs:
        if f.kind != "export":
            continue
        o.append("")
        for d in f.doc:
            o.append("//" + (" " + d if d else ""))
        if f.cap:
            o.append("// Capability: %s" % f.cap)
        o.append("%s %s( %s );" % ("int" if f.ret else "void", f.name, plugin_params(f.params)))
    o.append("\n#endif // %s_H\n" % U)
    return "\n".join(o)


def gen_syscalls(api, src):
    M = api.module
    o = ["; Generated from %s by gen_api.py - do not edit." % src,
         "; The engine functions of the API: a call of a negative address is a call of the engine (see odfc.h).", "code", ""]
    n = 0
    for f in api.funcs:
        if f.kind != "import":
            continue
        o.append("equ\t%s\t\t-%d" % (f.name, n + 1))
        n += 1
    return "\n".join(o) + "\n"


def export_case(f, index):
    """The line of the entry point that calls an export: a is the array of arguments the engine put in the io buffer."""
    args = []
    for i, p in enumerate(f.params):
        if p.kind == "str":
            args.append("(const char *)a[%d]" % i)
        elif p.kind == "i32":
            args.append("a[%d]" % i)
        elif p.kind == "ptrin":
            args.append("(const %s *)a[%d]" % (p.ctype, i))
        else:
            raise SystemExit("export argument type not supported: " + p.name)
    call = "%s( %s )" % (f.name, ", ".join(args))
    return call


def gen_entry(api, src):
    M = api.module
    U = M.upper()
    X = "EXPORTS"
    o = []
    o.append("/* Generated from %s by gen_api.py - do not edit. */\n" % src)
    o.append("/*\n===========================================================================\n"
             "The entry point of a plugin. Include this once, in the first source file of the program (the VM wants vmMain\n"
             "to be the first function), after defining\n\n"
             "    #define ODF_PLUGIN_VERSION     \"1.0\"\n"
             "    #define ODF_PLUGIN_CAPS        \"capability,capability\"     (optional: what the plugin needs to be given)\n"
             "    #define ODF_PLUGIN_DESCRIPTION \"what it does\"              (optional)\n"
             "    #define %s( X ) X(OnInit) X(OnShutdown) ...  the exports the program implements\n\n"
             "The API version (%d) is added by this header (ODF_PLUGIN_API, a string, overrides it: for tests).\n"
             "===========================================================================\n*/\n" % (X, api.version))
    o.append("#ifndef %s_ENTRY_H\n#define %s_ENTRY_H\n" % (U, U))
    o.append('#include "%s.h"\n' % M)
    o.append("#ifndef ODF_PLUGIN_VERSION\n#error define ODF_PLUGIN_VERSION before including %s_entry.h\n#endif\n"
             "#ifndef ODF_PLUGIN_CAPS\n#define ODF_PLUGIN_CAPS \"\"\n#endif\n"
             "#ifndef ODF_PLUGIN_DESCRIPTION\n#define ODF_PLUGIN_DESCRIPTION \"\"\n#endif\n"
             "#ifndef ODF_PLUGIN_API\n"
             "#define ODF_STRINGIFY_( x )\t#x\n#define ODF_STRINGIFY( x )\tODF_STRINGIFY_( x )\n"
             "#define ODF_PLUGIN_API ODF_STRINGIFY( %s_API_VERSION )\n"
             "#endif" % (M, U))
    o.append("#ifndef %s\n#error define %s( X ) before including %s_entry.h\n#endif\n" % (X, X, M))
    exports = [f for f in api.funcs if f.kind == "export"]
    for i, f in enumerate(exports):
        call = export_case(f, i)
        if f.ret:
            o.append("#define %s_CASE_%s\tcase %d: return %s;" % (U, f.name, i, call))
        else:
            o.append("#define %s_CASE_%s\tcase %d: %s; return 0;" % (U, f.name, i, call))
        o.append("#define %s_BIT_%s\t| ( 1 << %d )" % (U, f.name, i))
    o.append("#define %s_X_CASE( name )\t%s_CASE_##name" % (U, U))
    o.append("#define %s_X_BIT( name )\t%s_BIT_##name\n" % (U, U))
    o.append("static char odf_io_buffer[ODF_IO_SIZE];\n")
    o.append("int vmMain( int command, int ioAddress, int unused1, int unused2 )")
    o.append("{")
    o.append("\tconst int *a = (const int *)ioAddress;\n")
    o.append("\tswitch ( command ) {")
    o.append("\t\tcase ODF_CMD_IO:\n\t\t\treturn (int)odf_io_buffer;")
    o.append("\t\tcase ODF_CMD_MANIFEST:\n\t\t\treturn (int)(\n"
             "\t\t\t\t\"module=%s\\n\"\n"
             "\t\t\t\t\"version=\" ODF_PLUGIN_VERSION \"\\n\"\n"
             "\t\t\t\t\"api=\" ODF_PLUGIN_API \"\\n\"\n"
             "\t\t\t\t\"caps=\" ODF_PLUGIN_CAPS \"\\n\"\n"
             "\t\t\t\t\"description=\" ODF_PLUGIN_DESCRIPTION );" % M)
    o.append("\t\tcase ODF_CMD_EXPORTS:\n\t\t\treturn 0 %s( %s_X_BIT );" % (X, U))
    o.append("\t\t%s( %s_X_CASE )" % (X, U))
    o.append("\t}")
    o.append("\treturn 0;")
    o.append("}\n")
    o.append("#endif // %s_ENTRY_H\n" % U)
    return "\n".join(o)


# ---------------------------------------------------------------------------
# engine side
# ---------------------------------------------------------------------------

def host_ctype(p):
    if p.kind == "i32":
        return "int"
    if p.kind == "f32":
        return "float"
    if p.kind == "i64":
        return "int64_t"
    if p.kind == "str":
        return "const char *"
    if p.buffer:
        return "const char *" if p.kind == "ptrin" else "char *"
    return ("const %s *" if p.kind == "ptrin" else "%s *") % p.ctype


def host_params(params):
    if not params:
        return "void"
    out = []
    for p in params:
        t = host_ctype(p)
        out.append(t + ("" if t.endswith("*") else " ") + p.name)
    return ", ".join(out)


def elem_size(p):
    return "sizeof( %s )" % p.ctype


def gen_inc(api, src):
    M = api.module
    U = M.upper()
    o = []
    o.append("/* Generated from %s by gen_api.py - do not edit. */\n" % src)
    ncaps = len(api.caps)
    o.append("#define %s_API_VERSION	%d" % (U, api.version))
    for i, (name, restricted, text) in enumerate(api.caps):
        o.append("#define %s_CAP_%s\t( 1u << %d )" % (U, name, i))
    restricted_mask = " | ".join("%s_CAP_%s" % (U, n) for n, r, t in api.caps if r) or "0"
    o.append("#define %s_CAPS_RESTRICTED\t( %s )" % (U, restricted_mask))
    o.append("static const char *const %s_capNames[] = { %s };" % (M, ", ".join('"%s"' % c[0] for c in api.caps) or "NULL"))
    o.append("#define %s_NUM_CAPS\t%d\n" % (U, ncaps))

    imports = [f for f in api.funcs if f.kind == "import"]
    exports = [f for f in api.funcs if f.kind == "export"]

    o.append("#define %s_NUM_IMPORTS\t%d\n" % (U, len(imports)))
    o.append("// The functions the engine implements, written by hand. The host also has to define")
    o.append("//   static qboolean ODF_CheatsAllowed( void );   the rule for the functions marked cheat")
    for f in imports:
        o.append("static %s Imp_%s( %s );" % (ret_ctype(f), f.name, host_params(f.params)))
    o.append("")
    o.append("static ID_INLINE float Odf_ArgFloat( intptr_t arg )\n{\n\tfloatint_t fi;\n\n\tfi.i = (int)arg;\n\treturn fi.f;\n}\n")
    o.append("static ID_INLINE int Odf_FloatBits( float f )\n{\n\tfloatint_t fi;\n\n\tfi.f = f;\n\treturn fi.i;\n}\n")

    o.append("// Every call a plugin makes of the engine comes through here. args[0] is the function, args[1...] what the plugin passed.")
    o.append("// Nothing the plugin passed is used before it was checked; a plugin that passes something wrong is stopped.")
    o.append("static intptr_t %s_Dispatch( pluginVM_t *vm, intptr_t *args )\n{" % (M[0].upper() + M[1:]))
    o.append("\tpluginVMImportCalls++;")
    o.append("\tswitch ( args[0] ) {")
    for n, f in enumerate(imports):
        o.append("\t\tcase %d: {\t\t// %s" % (n, f.name))
        if f.cap:
            o.append("\t\t\tif ( !( PluginVM_Caps( vm ) & %s_CAP_%s ) ) {\n\t\t\t\tPluginVM_Denied( vm, \"%s\", \"%s\" );\n\t\t\t\treturn 0;\n\t\t\t}" % (U, f.cap, f.name, f.cap))
        if f.cheat:
            o.append("\t\t\tif ( !ODF_CheatsAllowed() ) {\n\t\t\t\treturn 0;\t\t// cheats only\n\t\t\t}")
        # slots of the arguments
        slot = {}
        k = 1
        for p in f.params:
            slot[p.name] = k
            k += p.slots()
        for p in f.params:
            if p.kind in ("ptrin", "ptrout", "str"):
                o.append("\t\t\t%s p_%s = NULL;" % (host_ctype(p), p.name))
        for p in f.params:
            nm = p.name
            s = slot[nm]
            if p.kind == "str":
                o.append("\t\t\tPluginVM_String( vm, args[%d], %s, &p_%s );" % (s, "qtrue" if p.nullable else "qfalse", nm))
            elif p.kind in ("ptrin", "ptrout"):
                null = "qtrue" if p.nullable else "qfalse"
                if p.count is None:
                    o.append("\t\t\tPluginVM_Ptr( vm, args[%d], %s, __alignof__( %s ) < 4 ? __alignof__( %s ) : 4, %s, (void **)&p_%s );" % (
                        s, elem_size(p), p.ctype, p.ctype, null, nm))
                elif isinstance(p.count, int):
                    o.append("\t\t\tPluginVM_Ptr( vm, args[%d], %d * %s, __alignof__( %s ) < 4 ? __alignof__( %s ) : 4, %s, (void **)&p_%s );" % (
                        s, p.count, elem_size(p), p.ctype, p.ctype, null, nm))
                else:
                    c = "args[%d]" % slot[p.count]
                    o.append("\t\t\tif ( (uint32_t)%s > %d ) {\n\t\t\t\tPluginVM_Trap( vm, \"bad size passed to the engine\" );\n\t\t\t\treturn 0;\n\t\t\t}" % (c, MAX_COUNT))
                    o.append("\t\t\tif ( %s != 0 ) {\n\t\t\t\tPluginVM_Ptr( vm, args[%d], (unsigned)%s * %s, __alignof__( %s ) < 4 ? __alignof__( %s ) : 4, %s, (void **)&p_%s );\n\t\t\t}" % (
                        c, s, c, elem_size(p), p.ctype, p.ctype, null, nm))
        cargs = []
        for p in f.params:
            s = slot[p.name]
            if p.kind in ("ptrin", "ptrout", "str"):
                cargs.append("p_" + p.name)
            elif p.kind == "i32":
                cargs.append("(int)args[%d]" % s)
            elif p.kind == "f32":
                cargs.append("Odf_ArgFloat( args[%d] )" % s)
            elif p.kind == "i64":
                cargs.append("(int64_t)( ( (uint64_t)(uint32_t)args[%d] << 32 ) | (uint32_t)args[%d] )" % (s + 1, s))
        call = "Imp_%s( %s )" % (f.name, ", ".join(cargs))
        if f.ret == "i32":
            o.append("\t\t\treturn %s;" % call)
        elif f.ret == "f32":
            o.append("\t\t\treturn Odf_FloatBits( %s );" % call)
        else:
            o.append("\t\t\t%s;\n\t\t\treturn 0;" % call)
        o.append("\t\t}")
    o.append("\t\tdefault:\n\t\t\tPluginVM_Trap( vm, \"called a function the engine does not have\" );\n\t\t\treturn 0;")
    o.append("\t}")
    o.append("}\n")

    o.append("enum {")
    for f in exports:
        o.append("\t%s_EXP_%s," % (U, f.name))
    o.append("\t%s_NUM_EXPORTS\n};" % U)
    o.append("static const char *const %s_exportNames[] = { %s };" % (M, ", ".join('"%s"' % f.name for f in exports)))
    o.append("// capability an export needs to be called (0 = none)")
    o.append("static const unsigned %s_exportCaps[] = { %s };\n" % (
        M, ", ".join(("%s_CAP_%s" % (U, f.cap)) if f.cap else "0" for f in exports)))

    for f in exports:
        ret = f.ret is not None
        cparams = []
        for p in f.params:
            if p.kind == "str":
                cparams.append("const char *a_%s" % p.name)
            elif p.kind == "i32":
                cparams.append("int a_%s" % p.name)
            elif p.kind == "ptrin":
                cparams.append("const %s *a_%s" % (p.ctype, p.name))
            else:
                raise SystemExit("export argument type not supported: " + p.name)
        sig = ", ".join(["pluginVM_t *w"] + cparams + (["int *result"] if ret else []))
        o.append("static qboolean %s_Call_%s( %s )\n{" % (U, f.name, sig))
        o.append("\tint args[8];\n\tunsigned o;\n\t(void)o;")
        o.append("\tif ( !PluginVM_Has( w, %s_EXP_%s ) ) {" % (U, f.name))
        if ret:
            o.append("\t\t*result = 0;")
        o.append("\t\treturn qtrue;\n\t}")
        o.append("\tPluginVM_IOBegin( w );")
        for i, p in enumerate(f.params):
            if p.kind == "str":
                o.append("\to = PluginVM_IOPutString( w, a_%s );" % p.name)
            elif p.kind == "ptrin":
                o.append("\to = PluginVM_IOPut( w, a_%s, sizeof( %s ), __alignof__( %s ) < 4 ? __alignof__( %s ) : 4 );" % (p.name, p.ctype, p.ctype, p.ctype))
            else:
                o.append("\to = (unsigned)a_%s;" % p.name)
                o.append("\targs[%d] = (int)o;" % i)
                continue
            o.append("\tif ( !o ) {\n\t\tPluginVM_Fail( w, \"the arguments of %s do not fit the io buffer\" );\n\t\treturn qfalse;\n\t}" % f.name)
            o.append("\targs[%d] = (int)o;" % i)
        budget = "PLUGIN_VM_BUDGET_INIT" if f.budget == "init" else "PLUGIN_VM_BUDGET_FRAME"
        o.append("\treturn PluginVM_Call( w, %s_EXP_%s, args, %d, %s, %s );\n}\n" % (
            U, f.name, len(f.params), "result" if ret else "NULL", budget))

    if api.events:
        o.append("// events and the capability that is needed to get them (0 = none)")
        o.append("static const struct { const char *name; unsigned cap; } %s_eventCaps[] = {" % M)
        for name, cap, doc in api.events:
            o.append('\t{ "%s", %s },' % (name, ("%s_CAP_%s" % (U, cap)) if cap else "0"))
        o.append("};\n")
    return "\n".join(o)


def outputs(sources=None, base=HERE):
    res = {}
    for src, inc in sources or SOURCES:
        api = parse(src if os.path.isabs(src) else os.path.join(base, src))
        res[os.path.join("include", api.module + ".h")] = gen_header(api, src)
        res[os.path.join("include", api.module + "_syscalls.asm")] = gen_syscalls(api, src)
        res[os.path.join("include", api.module + "_entry.h")] = gen_entry(api, src)
        res[os.path.join("generated", inc)] = gen_inc(api, src)
    return res


def main():
    check = "--check" in sys.argv
    bad = 0
    base = HERE
    sources = None
    if "--api" in sys.argv:
        # python gen_api.py --api file.api outdir : the files of a description that is not part of the engine (for the tests)
        i = sys.argv.index("--api")
        api_file, base = os.path.abspath(sys.argv[i + 1]), os.path.abspath(sys.argv[i + 2])
        sources = [(api_file, os.path.splitext(os.path.basename(api_file))[0] + "_api.inc")]
    for rel, text in outputs(sources).items():
        path = os.path.join(base, rel)
        text = text.replace("\r\n", "\n")
        if check:
            try:
                with open(path, encoding="utf-8", newline="") as f:
                    cur = f.read().replace("\r\n", "\n")
            except OSError:
                cur = None
            if cur != text:
                print("out of date: " + rel)
                bad = 1
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
            print("wrote " + rel)
    sys.exit(bad)


if __name__ == "__main__":
    main()
