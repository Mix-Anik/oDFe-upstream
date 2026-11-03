/* The preprocessor, the compiler and the assembler are linked into one program (odf-build), so the global names they share
   have to differ. This file is included before every source file of the compiler (-include). */
#define main	rcc_main
