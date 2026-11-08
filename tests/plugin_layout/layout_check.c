/*
Compile-time checks of the layout of the structs in the API headers (layout.inc, written by gen_layout.py): a wrong number is
an array of negative size. Compiled with the compiler of the engine and with q3lcc, which must agree.
*/

#include "odfc.h"
#include "odfs.h"

#ifdef Q3_VM
#define ODF_OFFSETOF( type, field )	( (int)&( (type *)0 )->field )
#else
#include <stddef.h>
#define ODF_OFFSETOF( type, field )	( (int)offsetof( type, field ) )
#endif

#define ODF_SIZE( type, n )				typedef char size_of_##type[ sizeof( type ) == ( n ) ? 1 : -1 ];
#define ODF_OFFSET( type, field, n )	typedef char offset_of_##type##_##field[ ODF_OFFSETOF( type, field ) == ( n ) ? 1 : -1 ];

#include "layout.inc"
