/* a server plugin: exports ODF_PluginEntry and no ODF_ClientPluginEntry, so the client must not load it */

#ifdef _WIN32
#define EXPORT __declspec( dllexport )
#else
#define EXPORT __attribute__( ( visibility( "default" ) ) )
#endif

EXPORT const void *ODF_PluginEntry( const void *imp )
{
	(void)imp;
	return 0;
}
