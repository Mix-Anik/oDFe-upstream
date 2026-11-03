/* A server plugin that says hello when the server starts it.
   Build:  odf-build --server -o hello_server.qvm hello_server.c */
#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_DESCRIPTION "says hello"
#define EXPORTS( X ) X(OnInit)
#include "odfs_entry.h"

int OnInit( void )
{
	Print( "hello from a server plugin" );
	return 0;
}
