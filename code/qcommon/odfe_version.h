/*
Version of the oDFe engine, shown to the player and compared with the GitHub release tag (vMAJOR.MINOR.PATCH) by the
updater (cl_update.c). Raise it before tagging a release; minor and patch must stay below 100.
*/

#ifndef __ODFE_VERSION_H
#define __ODFE_VERSION_H

#define ODFE_VERSION_MAJOR		1
#define ODFE_VERSION_MINOR		0
#define ODFE_VERSION_PATCH		0

#define ODFE_VERSION_NUMBER		( ODFE_VERSION_MAJOR * 10000 + ODFE_VERSION_MINOR * 100 + ODFE_VERSION_PATCH )

#define ODFE_STR2( x )			#x
#define ODFE_STR( x )			ODFE_STR2( x )
#define ODFE_VERSION_STRING		ODFE_STR( ODFE_VERSION_MAJOR ) "." ODFE_STR( ODFE_VERSION_MINOR ) "." ODFE_STR( ODFE_VERSION_PATCH )

#endif
