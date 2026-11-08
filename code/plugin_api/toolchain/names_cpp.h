/* The preprocessor, the compiler and the assembler are linked into one program (odf-build), so the global names they share
   have to differ. This file is included before every source file of the preprocessor (-include). */
#ifndef _MSC_VER
#include <unistd.h>	/* open, read, write, creat ... (the sources use them without declaring them) */
#include <fcntl.h>
#endif

#define main	cpp_main
#define error	cpp_error
#define fillbuf	cpp_fillbuf
#define lookup	cpp_lookup
#define process	cpp_process
#define verbose	cpp_verbose
#define optarg	cpp_optarg
#define opterr	cpp_opterr
#define optind	cpp_optind
#define optopt	cpp_optopt
