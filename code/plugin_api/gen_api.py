#!/usr/bin/env python3
"""
Generates the plugin API from its description (client.api, server.api).

  include/<module>.h             what a plugin includes: the functions it can import and export
  generated/<cl|sv>_plugin_api.inc   what the engine includes: argument checks for every import, the import table,
                                 the export list and the functions that call the exports

  python gen_api.py            writes the files
  python gen_api.py --check    fails if the files on disk are not what the descriptions generate
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAX_COUNT = 1 << 20

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


class Func:
    def __init__(self, kind, name, params, ret, cap, budget, doc):
        self.kind = kind          # import / export
        self.name = name
        self.params = params
        self.ret = ret            # None, i32, f32
        self.cap = cap
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
        self.header_doc = []


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


def parse(path):
    api = Api()
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
            mm = re.search(r"\bbudget\s+([a-z]+)", rest)
            if mm:
                budget = mm.group(1)
            api.funcs.append(Func(kind, name, parse_params(args), ret, cap, budget, [d for d in doc]))
            doc = []
            continue
        tok = line.split(None, 1)
        if tok[0] == "module":
            api.module = tok[1].strip()
        elif tok[0] == "version":
            api.version = int(tok[1])
        elif tok[0] == "types":
            api.types = tok[1].strip()
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
    for f in api.funcs:
        if f.cap and f.cap not in [c[0] for c in api.caps]:
            raise SystemExit("%s uses the unknown capability %s" % (f.name, f.cap))
    return api


# ---------------------------------------------------------------------------
# plugin side header
# ---------------------------------------------------------------------------

def plugin_ctype(p):
    if p.kind == "i32":
        return "int"
    if p.kind == "f32":
        return "float"
    if p.kind == "i64":
        return "int64_t"
    if p.kind == "str":
        return "const char *"
    if p.buffer:
        if p.kind == "ptrin":
            return "const void *"
        return "char *" if p.cstr else "void *"
    return ("const %s *" if p.kind == "ptrin" else "%s *") % p.ctype


def decl_params(params, ctype_fn):
    if not params:
        return "void"
    out = []
    for p in params:
        t = ctype_fn(p)
        out.append(t + ("" if t.endswith("*") else " ") + p.name)
    return ", ".join(out)


def gen_header(api, src):
    M = api.module
    U = M.upper()
    o = []
    o.append("/* Generated from %s by gen_api.py - do not edit. */\n" % src)
    o.append("/*\n===========================================================================\n"
             "oDFe %s plugin API, version %d.\n\n"
             "See odf_common.h for how a plugin is built. The functions below are what the plugin can\n"
             "call (imports) and what it can implement for the engine to call (exports).\n"
             "===========================================================================\n*/\n" %
             ("client" if M == "odfc" else "server", api.version))
    o.append("#ifndef %s_H\n#define %s_H\n" % (U, U))
    o.append('#include <stdint.h>\n#include "odf_common.h"\n#include "%s"\n' % api.types)
    o.append("#define %s_API_VERSION\t%d\n" % (U, api.version))
    o.append("// Capabilities. A plugin that imports a function of a group, or lists the group in its manifest (caps=a,b),")
    o.append("// is only loaded when it was given the capability.")
    for name, restricted, text in api.caps:
        o.append("//   %-10s %s%s" % (name, "(restricted) " if restricted else "", text))
    o.append("")
    for f in api.funcs:
        if f.kind != "import":
            continue
        o.append("")
        for d in f.doc:
            o.append("//" + (" " + d if d else ""))
        if f.cap:
            o.append("// Capability: %s" % f.cap)
        o.append('ODF_IMPORT( "%s", "%s" ) %s %s_%s( %s );' % (
            M, f.name, {"i32": "int", "f32": "float", None: "void"}[f.ret], M, f.name, decl_params(f.params, plugin_ctype)))
    o.append("\n\n// ---------------------------------------------------------------------------")
    o.append("// Exports: define the ones you need with exactly these prototypes.")
    o.append("// ---------------------------------------------------------------------------")
    for f in api.funcs:
        if f.kind != "export":
            continue
        o.append("")
        for d in f.doc:
            o.append("//" + (" " + d if d else ""))
        if f.cap:
            o.append("// Capability: %s" % f.cap)
        params = []
        for p in f.params:
            params.append(Param(p.name, p.kind, p.ctype, p.count, p.nullable))
        o.append('ODF_EXPORT_FN( "%s" ) %s %s_%s( %s );' % (
            f.name, {"i32": "int", None: "void"}[f.ret], M, f.name, decl_params(params, plugin_ctype)))
    o.append("\n#endif // %s_H\n" % U)
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


def wrapper_ctype(p):
    return {"i32": "int32_t", "f32": "float", "i64": "int64_t"}.get(p.kind, "uint32_t")


def sig_char(p):
    return {"i32": "i", "f32": "f", "i64": "I"}.get(p.kind, "i")


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

    o.append("// the functions the engine implements")
    for f in imports:
        ret = {"i32": "int", "f32": "float", None: "void"}[f.ret]
        o.append("static %s Imp_%s( %s );" % (ret, f.name, decl_params(f.params, host_ctype)))
    o.append("")

    for f in imports:
        ret = {"i32": "int", "f32": "float", None: "void"}[f.ret]
        dflt = {"i32": "0", "f32": "0.0f", None: ""}[f.ret]
        ret_stmt = "return %s;" % dflt if f.ret else "return;"
        wparams = ", ".join(["void *env"] + ["%s a_%s" % (wrapper_ctype(p), p.name) for p in f.params])
        o.append("static %s W_%s( %s )\n{" % (ret, f.name, wparams))
        o.append("\todfWasmImportCalls++;")
        for p in f.params:
            if p.kind in ("ptrin", "ptrout", "str"):
                o.append("\t%s p_%s = NULL;" % (host_ctype(p), p.name))
        for p in f.params:
            n = p.name
            if p.kind == "str":
                o.append("\tif ( !OdfWasm_String( env, a_%s, %s, &p_%s ) ) {\n\t\t%s\n\t}" % (
                    n, "qtrue" if p.nullable else "qfalse", n, ret_stmt))
            elif p.kind in ("ptrin", "ptrout"):
                null = "qtrue" if p.nullable else "qfalse"
                if p.count is None:
                    count = "1"
                    o.append("\tif ( !OdfWasm_Ptr( env, a_%s, %s, __alignof__( %s ), %s, (void **)&p_%s ) ) {\n\t\t%s\n\t}" % (
                        n, elem_size(p), p.ctype, null, n, ret_stmt))
                elif isinstance(p.count, int):
                    o.append("\tif ( !OdfWasm_Ptr( env, a_%s, %d * %s, __alignof__( %s ), %s, (void **)&p_%s ) ) {\n\t\t%s\n\t}" % (
                        n, p.count, elem_size(p), p.ctype, null, n, ret_stmt))
                else:
                    c = "a_" + p.count
                    o.append("\tif ( (uint32_t)%s > %d ) {\n\t\tOdfWasm_Trap( env, \"bad size passed to the engine\" );\n\t\t%s\n\t}" % (c, MAX_COUNT, ret_stmt))
                    o.append("\tif ( %s != 0 && !OdfWasm_Ptr( env, a_%s, (unsigned)%s * %s, __alignof__( %s ), %s, (void **)&p_%s ) ) {\n\t\t%s\n\t}" % (
                        c, n, c, elem_size(p), p.ctype, null, n, ret_stmt))
        args = ", ".join(("p_" + p.name) if p.kind in ("ptrin", "ptrout", "str") else "a_" + p.name for p in f.params)
        o.append("\t%sImp_%s( %s );\n}\n" % ("return " if f.ret else "", f.name, args))

    o.append("static const odfWasmImport_t %s_importTable[] = {" % M)
    for f in imports:
        sig = "(" + "".join(sig_char(p) for p in f.params) + ")" + {"i32": "i", "f32": "f", None: ""}[f.ret]
        cap = "%s_CAP_%s" % (U, f.cap) if f.cap else "0"
        o.append('\t{ "%s", (void *)W_%s, "%s", %s },' % (f.name, f.name, sig, cap))
    o.append("};")
    o.append("#define %s_NUM_IMPORTS\t( (int)( sizeof( %s_importTable ) / sizeof( %s_importTable[0] ) ) )\n" % (U, M, M))

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
        sig = ", ".join(["odfWasm_t *w"] + cparams + (["int *result"] if ret else []))
        o.append("static qboolean %s_Call_%s( %s )\n{" % (U, f.name, sig))
        o.append("\tint args[8];\n\tunsigned o;\n\t(void)o;")
        o.append("\tif ( !OdfWasm_Has( w, %s_EXP_%s ) ) {" % (U, f.name))
        if ret:
            o.append("\t\t*result = 0;")
        o.append("\t\treturn qtrue;\n\t}")
        o.append("\tOdfWasm_IOBegin( w );")
        for i, p in enumerate(f.params):
            if p.kind == "str":
                o.append("\to = OdfWasm_IOPutString( w, a_%s );" % p.name)
            elif p.kind == "ptrin":
                o.append("\to = OdfWasm_IOPut( w, a_%s, sizeof( %s ), __alignof__( %s ) );" % (p.name, p.ctype, p.ctype))
            else:
                o.append("\to = (unsigned)a_%s;" % p.name)
                o.append("\targs[%d] = (int)o;" % i)
                continue
            o.append("\tif ( !o ) {\n\t\tOdfWasm_Fail( w, \"the arguments of %s do not fit the io buffer\" );\n\t\treturn qfalse;\n\t}" % f.name)
            o.append("\targs[%d] = (int)o;" % i)
        budget = "ODF_WASM_BUDGET_INIT" if f.budget == "init" else "ODF_WASM_BUDGET_FRAME"
        o.append("\treturn OdfWasm_Call( w, %s_EXP_%s, args, %d, %s, %s );\n}\n" % (
            U, f.name, len(f.params), "result" if ret else "NULL", budget))

    if api.events:
        o.append("// events and the capability that is needed to get them (0 = none)")
        o.append("static const struct { const char *name; unsigned cap; } %s_eventCaps[] = {" % M)
        for name, cap, doc in api.events:
            o.append('\t{ "%s", %s },' % (name, ("%s_CAP_%s" % (U, cap)) if cap else "0"))
        o.append("};\n")
    return "\n".join(o)


def outputs():
    res = {}
    for src, inc in SOURCES:
        api = parse(os.path.join(HERE, src))
        res[os.path.join("include", api.module + ".h")] = gen_header(api, src)
        res[os.path.join("generated", inc)] = gen_inc(api, src)
    return res


def main():
    check = "--check" in sys.argv
    bad = 0
    for rel, text in outputs().items():
        path = os.path.join(HERE, rel)
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
