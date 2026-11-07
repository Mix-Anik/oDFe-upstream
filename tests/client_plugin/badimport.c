/* A plugin that calls a function the engine does not have (it makes up the address): it is stopped, whatever else it is. */

#define ODF_PLUGIN_VERSION "badimport 1.0"
#define EXPORTS( X ) X(OnInit)
#include "odfc_entry.h"

int OnInit( void )
{
	return ( (int (*)( int ))-5000 )( 1 );
}
