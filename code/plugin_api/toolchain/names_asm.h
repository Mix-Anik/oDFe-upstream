/* The preprocessor, the compiler and the assembler are linked into one program (odf-build), so the global names they share
   have to differ. This file is included before every source file of the assembler (-include). */
#define main	asm_main
#define symbols	asm_symbols
#define token	asm_token
#define verbose	asm_verbose
