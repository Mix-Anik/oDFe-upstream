/* A plugin without the io buffer the engine needs: it must be refused with a message that says what is missing. */

#include "odfc.h"

int odfc_init( void )
{
	odfc_Print( "no io buffer" );
	return 0;
}
