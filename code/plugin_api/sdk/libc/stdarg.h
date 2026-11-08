/* Argument lists the way the VM passes them: every argument takes whole 4 byte slots on the stack. A float (like a double,
which is the same size in this compiler) is one slot. */

#ifndef ODF_STDARG_H
#define ODF_STDARG_H

typedef char *va_list;

#define ODF_INTSIZEOF( n )	( ( sizeof( n ) + sizeof( int ) - 1 ) & ~( sizeof( int ) - 1 ) )
#define va_start( ap, v )	( ap = (va_list)&v + ODF_INTSIZEOF( v ) )
#define va_arg( ap, t )		( *(t *)( ( ap += ODF_INTSIZEOF( t ) ) - ODF_INTSIZEOF( t ) ) )
#define va_end( ap )		( ap = (va_list)0 )
#define va_copy( dst, src )	( dst = src )

#endif
