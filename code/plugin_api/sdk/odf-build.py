#!/usr/bin/env python3
"""
Builds an oDFe plugin (a QVM program) from C sources with q3lcc and q3asm.

    python odf-build.py --client -o plugins/myplugin.qvm main.c other.c
    python odf-build.py --server -o plugins/myplugin.qvm -DDEBUG -Iinclude main.c

The first source file has to include the entry point (odfc_entry.h or odfs_entry.h), the VM wants it first in the program.
The C library of the SDK (odf_lib.c) and the engine functions (odfc_syscalls.asm / odfs_syscalls.asm) are added at the end.

Needs q3lcc, q3cpp, q3rcc and q3asm (the tools of ioquake3, ec- or oDFs builds). Give their folder with --tools or the
environment variable ODF_TOOLS; without either they are searched for in the PATH.
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
INCLUDE_ROOT = os.path.join(os.path.dirname(HERE), "include")

# module: (prefix of the generated files, preprocessor symbol)
MODULES = {
    "client": ("odfc", "ODF_MODULE_CLIENT"),
    "server": ("odfs", "ODF_MODULE_SERVER"),
    "test": ("odft", "ODF_MODULE_TEST"),
}


SEGMENTS = ("code", "data", "lit", "bss")


def find_tool(name, folder):
    candidates = [name, name + ".exe"]
    if folder:
        for c in candidates:
            path = os.path.join(folder, c)
            if os.path.isfile(path):
                # q3lcc finds q3cpp and q3rcc from its own name, which has to be written with one kind of slash
                return os.path.abspath(path)
        sys.exit("odf-build: %s not found in %s" % (name, folder))
    for c in candidates:
        path = shutil.which(c)
        if path:
            return os.path.abspath(path)
    sys.exit("odf-build: %s not found; give the folder of the tools with --tools or ODF_TOOLS" % name)


def move_data_tables(text):
    """Changes the 'lit' segments that hold addresses of data, not code, to 'data' (see fix_assembly)."""
    lines = text.split("\n")
    code_labels = set()
    segment = None
    for line in lines:
        t = line.strip()
        if t in SEGMENTS:
            segment = t
        elif t.startswith("proc "):
            code_labels.add(t.split()[1])
        elif segment == "code" and t.startswith("LABELV "):
            code_labels.add(t.split()[1])
    start = None       # index of the line that starts the current lit segment
    data_address = False
    for i, line in enumerate(lines + ["code"]):
        t = line.strip()
        if t in SEGMENTS:
            if start is not None and data_address:
                lines[start] = lines[start].replace("lit", "data")
            start = i if t == "lit" else None
            data_address = False
        elif start is not None and t.startswith("address "):
            target = re.split(r"[+-]", t.split()[1])[0]
            if target not in code_labels:
                data_address = True
    return "\n".join(lines)


def fix_assembly(path):
    """Two things q3lcc and q3asm get wrong, mended in the assembly of a file:

    - q3asm knows only the symbols that begin with $ as belonging to one file, so two files with a static function (or
      variable) of the same name collide. The names a file defines without exporting them get a $ in front.
    - q3asm does not know the conversion to unsigned short ((unsigned short)x of an unsigned value), q3rcc writes it. It is
      the same as keeping the lower 16 bits.
    - q3rcc puts constant data in the segment q3asm takes the jump tables of switch statements from, and every address in
      that segment is taken for a place the program jumps to. A table of strings or other data (static const char *names[])
      would be hundreds of jump targets that are not code, which the VM then has to give up on. Tables of data go to the data
      segment; tables of functions stay.
    """
    with open(path, newline="") as f:
        text = f.read()
    text = move_data_tables(text)
    exported = set(re.findall(r"^export (\S+)", text, re.M))
    defined = set(re.findall(r"^proc (\S+)", text, re.M)) | set(re.findall(r"^LABELV (\S+)", text, re.M))
    for name in defined - exported:
        if name.startswith("$"):
            continue
        text = re.sub(r"(?<![\w$.])" + re.escape(name) + r"(?![\w$.])", lambda m: "$" + name, text)
    text = re.sub(r"^CVUU2 \d+(\r?)$", lambda m: "CNSTU4 65535" + m.group(1) + "\nBANDU4" + m.group(1), text, flags=re.M)
    with open(path, "w", newline="") as f:
        f.write(text)


def stage_includes(include_dirs, folder):
    """Copies the include folders into a new folder inside 'folder'; returns it and the copies, in the order of the originals."""
    stage = tempfile.mkdtemp(prefix=".odfsdk", dir=folder)
    copies = []
    for n, d in enumerate(include_dirs):
        copy = os.path.join(stage, str(n))
        if os.path.isdir(d):
            shutil.copytree(d, copy)
        else:
            os.makedirs(copy)
        copies.append(copy)
    return stage, copies


def run(args, cwd, verbose):
    if verbose:
        print(" ".join(args))
    r = subprocess.run(args, cwd=cwd)
    if r.returncode != 0:
        sys.exit("odf-build: %s failed (%d)" % (os.path.basename(args[0]), r.returncode))


def main():
    ap = argparse.ArgumentParser(description="builds an oDFe plugin")
    kind = ap.add_mutually_exclusive_group(required=True)
    kind.add_argument("--client", action="store_const", dest="module", const="client", help="a client plugin (odfc.h)")
    kind.add_argument("--server", action="store_const", dest="module", const="server", help="a server plugin (odfs.h)")
    kind.add_argument("--test", action="store_const", dest="module", const="test", help="for the tests of the SDK")
    ap.add_argument("-o", dest="output", required=True, help="the plugin to write (name.qvm)")
    ap.add_argument("-I", dest="includes", action="append", default=[], help="a folder for #include")
    ap.add_argument("-D", dest="defines", action="append", default=[], help="a preprocessor symbol")
    ap.add_argument("--tools", default=os.environ.get("ODF_TOOLS"), help="the folder with q3lcc and q3asm")
    ap.add_argument("--no-lib", action="store_true", help="do not add the C library of the SDK (odf_lib.c)")
    ap.add_argument("--syscalls", help="the .asm of the engine functions (default: the one of the API)")
    ap.add_argument("--map", help="also write the map file of q3asm (where every function is) to this path")
    ap.add_argument("-v", dest="verbose", action="store_true", help="show the commands")
    ap.add_argument("sources", nargs="+", help="the C files, the one with the entry point first")
    a = ap.parse_args()

    prefix, symbol = MODULES[a.module]
    lcc = find_tool("q3lcc", a.tools)
    asm = find_tool("q3asm", a.tools)
    syscalls = os.path.abspath(a.syscalls or os.path.join(INCLUDE_ROOT, prefix + "_syscalls.asm"))
    if not os.path.isfile(syscalls):
        sys.exit("odf-build: %s not found" % syscalls)

    # the preprocessor of q3lcc takes include folders relative to the folder of the source file and cannot use complete paths,
    # so every file is compiled from its own folder with the include folders written relative to it
    include_dirs = [os.path.abspath(i) for i in a.includes]
    include_dirs += [os.path.join(HERE, "libc"), HERE, INCLUDE_ROOT]
    sources = [os.path.abspath(s) for s in a.sources]
    if not a.no_lib:
        sources.append(os.path.join(HERE, "odf_lib.c"))
    for s in sources:
        if not os.path.isfile(s):
            sys.exit("odf-build: %s not found" % s)

    out = os.path.abspath(a.output)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    work = tempfile.mkdtemp(prefix="odfbuild")
    stage_folders = []
    try:
        objects = []
        staged = {}
        for n, src in enumerate(sources):
            folder = os.path.dirname(src)
            try:
                rel = [os.path.relpath(i, folder).replace("\\", "/") for i in include_dirs]
            except ValueError:
                # another drive (Windows): the preprocessor cannot reach the include folders, so copies of them are put next to
                # the source for the time of the build
                key = os.path.splitdrive(folder)[0].lower()
                if key not in staged:
                    staged[key] = stage_includes(include_dirs, folder)
                    stage_folders.append(staged[key][0])
                rel = [os.path.relpath(c, folder).replace("\\", "/") for c in staged[key][1]]
            obj = os.path.join(work, "%d_%s.asm" % (n, os.path.splitext(os.path.basename(src))[0]))
            cmd = [lcc, "-S", "-N", "-DQ3_VM", "-D" + symbol]
            cmd += ["-D" + d for d in a.defines]
            cmd += ["-I" + r for r in rel]
            cmd += ["-o", obj, os.path.basename(src)]
            run(cmd, folder, a.verbose)
            fix_assembly(obj)
            objects.append(obj)
        # q3asm writes a map file next to the program, so it works in the temporary folder
        cmd = [asm] + (["-m"] if a.map else []) + ["-o", "plugin"] + [o.replace("\\", "/") for o in objects] + [syscalls.replace("\\", "/")]
        run(cmd, work, a.verbose)
        if not os.path.isfile(os.path.join(work, "plugin.qvm")):
            sys.exit("odf-build: q3asm did not write the plugin")
        shutil.copyfile(os.path.join(work, "plugin.qvm"), out)
        if a.map and os.path.isfile(os.path.join(work, "plugin.map")):
            shutil.copyfile(os.path.join(work, "plugin.map"), a.map)
    finally:
        shutil.rmtree(work, ignore_errors=True)
        for f in stage_folders:
            shutil.rmtree(f, ignore_errors=True)
    print("%s: %d bytes" % (a.output, os.path.getsize(out)))


if __name__ == "__main__":
    main()
