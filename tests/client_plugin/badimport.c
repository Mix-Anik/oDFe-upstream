/* A plugin that asks for the system (WASI): it must not be loaded, whatever else it is. */

#include "odfc.h"

__attribute__( ( import_module( "wasi_snapshot_preview1" ), import_name( "fd_write" ) ) )
int wasi_fd_write( int fd, int iovs, int iovsLen, int written );

ODF_DEFINE_IO()

int odfc_init( void )
{
	return wasi_fd_write( 1, 0, 0, 0 );
}
