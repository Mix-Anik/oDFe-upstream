/* A server plugin: it imports from odfs, so the client must not load it (and must not list it). */

#include "odf_common.h"

ODF_IMPORT( "odfs", "Print" ) void odfs_Print( const char *text );

ODF_DEFINE_IO()

ODF_EXPORT_FN( "init" ) int server_init( void )
{
	odfs_Print( "i am a server plugin" );
	return 0;
}
