# The compiler of the plugins

`sdk/odf-build` builds plugins without any other program: the C compiler, its preprocessor and the assembler are in it.
Their sources are here. They are the QVM tools of ioquake3 (<https://github.com/ioquake/ioq3>, `code/tools`, taken from the
commit `5956299e`), which compile the QVMs of Quake 3 games:

| folder | what | origin |
|---|---|---|
| `lcc/src` | `rcc`, the compiler proper (the "bytecode" target makes QVM assembly) | lcc 4.1 of Fraser and Hanson, with the QVM target of id Software |
| `lcc/cpp` | the preprocessor | the preprocessor that comes with lcc, with changes of id Software and ioquake3 |
| `asm` | `q3asm`, the assembler and linker that writes the `.qvm` file | id Software, GPL v2 |

`odf-build` does what the driver `q3lcc` did, in `sdk/odf-build.c`: for each source file it runs the preprocessor and the
compiler (`odf-build --run-tool cpp|rcc|asm`: it runs itself with the main function of the tool), mends the assembly (see
`fix_assembly`), and runs the assembler on all of the files.

## Licenses

* `lcc/`: the lcc license, in [lcc/COPYRIGHT](lcc/COPYRIGHT). lcc may be redistributed in whole or in part, also changed,
  as long as it is acknowledged and the file is included; it must not be sold as a product in which lcc is a significant
  part. **Keep `lcc/COPYRIGHT` with these sources and in every distribution of `odf-build`**, and say that `odf-build` is
  based on lcc.
* `asm/`: GPL v2 or later (id Software), the same as the engine.

## Changes to the sources

Small, and marked in the code where it is not obvious:

* `lcc/cpp/unix.c`: the replacement of `memmove` of the preprocessor is not compiled in the one program (`ODF_BUNDLED`):
  it would replace the one of the C library for all the tools.
* `lcc/cpp/include.c`: an include folder given with a complete path (`/x`, `C:\x`) is used as it is. The preprocessor took
  every folder as relative to the file, which no complete path survives (this made builds from another drive impossible).
  The buffer for the name of an include file is 1024 bytes, not 256.
* `asm/q3asm.c`: includes `qvm_format.h` (the header of a `.qvm` file) instead of the headers of the engine.
* `lcc/src/dagcheck.c` is made from `dagcheck.md` by `lburg` (`lcc/lburg` in ioquake3): `lburg dagcheck.md dagcheck.c`.
  It is a part of the sources; there is no need to run `lburg` to build.
* The names `names_cpp.h`, `names_rcc.h`, `names_asm.h` are included first in every file of a tool (`-include`, see
  `sdk/Makefile`). The three tools have a few global names in common (`main`, `error`, `symbols` ...), which is a problem
  only in one program, so these headers give them a prefix.

`sdk/Makefile` compiles the sources without warnings (they are old code) and with the aliasing rules of the time
(`-fno-strict-aliasing`). A test that the tools did not change in a bad way is to build the plugins with the original
`q3lcc` and `q3asm` and compare: the result must be the same file.
