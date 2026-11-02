/* Only the formatting functions. Supported: %d %i %u %x %X %o %c %s %p %f %% with the flags - + space 0 #, a width, a
precision and * for both. Not supported: %e %g, 64 bit numbers (there are none). A float is exact to what a float is: about
7 digits. The output is always terminated and cut to the size; the result is the length the full text would have had. */

#ifndef ODF_STDIO_H
#define ODF_STDIO_H

#include <stddef.h>
#include <stdarg.h>

int snprintf( char *out, size_t size, const char *format, ... );
int vsnprintf( char *out, size_t size, const char *format, va_list ap );
int sprintf( char *out, const char *format, ... );

#endif
