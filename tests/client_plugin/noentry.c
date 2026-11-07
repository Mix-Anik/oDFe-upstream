/* A server plugin: it is built for the odfs API, so the client must not load it (and must not list it). */

#define ODF_PLUGIN_VERSION "server 1.0"
#define EXPORTS( X ) X(OnInit)
#include "odfs_entry.h"

int OnInit( void )
{
	Print( "i am a server plugin" );
	return 0;
}
