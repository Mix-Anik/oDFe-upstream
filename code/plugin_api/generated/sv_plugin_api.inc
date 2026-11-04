/* Generated from server.api by gen_api.py - do not edit. */

#ifndef ODF_ALIGNOF
#ifdef _MSC_VER
#define ODF_ALIGNOF	__alignof
#else
#define ODF_ALIGNOF	__alignof__
#endif
#endif

#define ODFS_API_VERSION	1
#define ODFS_CAP_chat	( 1u << 0 )
#define ODFS_CAP_auth	( 1u << 1 )
#define ODFS_CAP_ips	( 1u << 2 )
#define ODFS_CAP_http	( 1u << 3 )
#define ODFS_CAP_storage	( 1u << 4 )
#define ODFS_CAP_rawcmd	( 1u << 5 )
#define ODFS_CAPS_RESTRICTED	( ODFS_CAP_auth | ODFS_CAP_ips | ODFS_CAP_http | ODFS_CAP_rawcmd )
static const char *const odfs_capNames[] = { "chat", "auth", "ips", "http", "storage", "rawcmd" };
#define ODFS_NUM_CAPS	6

#define ODFS_NUM_IMPORTS	24

// The functions the engine implements, written by hand. The host also has to define
//   static qboolean ODF_CheatsAllowed( void );   the rule for the functions marked cheat
static void Imp_Print( const char *text );
static int Imp_Milliseconds( void );
static int Imp_UnixTime( void );
static int Imp_RandomBytes( char *buf, int len );
static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags );
static void Imp_CvarString( const char *name, char *out, int outSize );
static int Imp_CvarInteger( const char *name );
static float Imp_Sin( float x );
static float Imp_Cos( float x );
static float Imp_Atan2( float y, float x );
static float Imp_Sqrt( float x );
static void Imp_MemSet( char *dst, int value, int size );
static void Imp_MemCpy( char *dst, const char *src, int size );
static int Imp_GetClientInfo( int clientNum, odfClientInfo_t *out );
static void Imp_SendServerCommand( int clientNum, const char *command );
static int Imp_EventSubscribe( const char *name, int handler );
static int Imp_ChatCommandRegister( const char *name, int handler );
static void Imp_AuthResult( int clientNum, int serial, int result, int64_t accountID, const char *accountName, const char *reason );
static int Imp_HttpRequest( int method, const char *url, const char *headers, const char *body, int bodyLen, int timeoutMs, int handler, int userData );
static int Imp_HttpBody( int id, int offset, char *out, int outSize );
static int Imp_StoreRead( const char *name, int offset, char *out, int outSize );
static int Imp_StoreWrite( const char *name, const char *data, int len, int append );
static int Imp_StoreSize( const char *name );
static int Imp_StoreDelete( const char *name );

static ID_INLINE float Odf_ArgFloat( intptr_t arg )
{
	floatint_t fi;

	fi.i = (int)arg;
	return fi.f;
}

static ID_INLINE int Odf_FloatBits( float f )
{
	floatint_t fi;

	fi.f = f;
	return fi.i;
}

// Every call a plugin makes of the engine comes through here. args[0] is the function, args[1...] what the plugin passed.
// Nothing the plugin passed is used before it was checked; a plugin that passes something wrong is stopped.
static intptr_t Odfs_Dispatch( pluginVM_t *vm, intptr_t *args )
{
	pluginVMImportCalls++;
	switch ( args[0] ) {
		case 0: {		// Print
			const char * p_text = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_text );
			Imp_Print( p_text );
			return 0;
		}
		case 1: {		// Milliseconds
			return Imp_Milliseconds(  );
		}
		case 2: {		// UnixTime
			return Imp_UnixTime(  );
		}
		case 3: {		// RandomBytes
			char * p_buf = NULL;
			if ( (uint32_t)args[2] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[2] != 0 ) {
				PluginVM_Ptr( vm, args[1], (unsigned)args[2] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qfalse, (void **)&p_buf );
			}
			return Imp_RandomBytes( p_buf, (int)args[2] );
		}
		case 4: {		// CvarRegister
			const char * p_name = NULL;
			const char * p_defaultValue = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			PluginVM_String( vm, args[2], qfalse, &p_defaultValue );
			Imp_CvarRegister( p_name, p_defaultValue, (int)args[3] );
			return 0;
		}
		case 5: {		// CvarString
			const char * p_name = NULL;
			char * p_out = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			if ( (uint32_t)args[3] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[3] != 0 ) {
				PluginVM_Ptr( vm, args[2], (unsigned)args[3] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qfalse, (void **)&p_out );
			}
			Imp_CvarString( p_name, p_out, (int)args[3] );
			return 0;
		}
		case 6: {		// CvarInteger
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_CvarInteger( p_name );
		}
		case 7: {		// Sin
			return Odf_FloatBits( Imp_Sin( Odf_ArgFloat( args[1] ) ) );
		}
		case 8: {		// Cos
			return Odf_FloatBits( Imp_Cos( Odf_ArgFloat( args[1] ) ) );
		}
		case 9: {		// Atan2
			return Odf_FloatBits( Imp_Atan2( Odf_ArgFloat( args[1] ), Odf_ArgFloat( args[2] ) ) );
		}
		case 10: {		// Sqrt
			return Odf_FloatBits( Imp_Sqrt( Odf_ArgFloat( args[1] ) ) );
		}
		case 11: {		// MemSet
			char * p_dst = NULL;
			if ( (uint32_t)args[3] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[3] != 0 ) {
				PluginVM_Ptr( vm, args[1], (unsigned)args[3] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qfalse, (void **)&p_dst );
			}
			Imp_MemSet( p_dst, (int)args[2], (int)args[3] );
			return 0;
		}
		case 12: {		// MemCpy
			char * p_dst = NULL;
			const char * p_src = NULL;
			if ( (uint32_t)args[3] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[3] != 0 ) {
				PluginVM_Ptr( vm, args[1], (unsigned)args[3] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qfalse, (void **)&p_dst );
			}
			if ( (uint32_t)args[3] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[3] != 0 ) {
				PluginVM_Ptr( vm, args[2], (unsigned)args[3] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qfalse, (void **)&p_src );
			}
			Imp_MemCpy( p_dst, p_src, (int)args[3] );
			return 0;
		}
		case 13: {		// GetClientInfo
			odfClientInfo_t * p_out = NULL;
			PluginVM_Ptr( vm, args[2], sizeof( odfClientInfo_t ), ODF_ALIGNOF( odfClientInfo_t ) < 4 ? ODF_ALIGNOF( odfClientInfo_t ) : 4, qfalse, (void **)&p_out );
			return Imp_GetClientInfo( (int)args[1], p_out );
		}
		case 14: {		// SendServerCommand
			const char * p_command = NULL;
			PluginVM_String( vm, args[2], qfalse, &p_command );
			Imp_SendServerCommand( (int)args[1], p_command );
			return 0;
		}
		case 15: {		// EventSubscribe
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_EventSubscribe( p_name, (int)args[2] );
		}
		case 16: {		// ChatCommandRegister
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_chat ) ) {
				PluginVM_Denied( vm, "ChatCommandRegister", "chat" );
				return 0;
			}
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_ChatCommandRegister( p_name, (int)args[2] );
		}
		case 17: {		// AuthResult
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_auth ) ) {
				PluginVM_Denied( vm, "AuthResult", "auth" );
				return 0;
			}
			const char * p_accountName = NULL;
			const char * p_reason = NULL;
			PluginVM_String( vm, args[6], qtrue, &p_accountName );
			PluginVM_String( vm, args[7], qtrue, &p_reason );
			Imp_AuthResult( (int)args[1], (int)args[2], (int)args[3], (int64_t)( ( (uint64_t)(uint32_t)args[5] << 32 ) | (uint32_t)args[4] ), p_accountName, p_reason );
			return 0;
		}
		case 18: {		// HttpRequest
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_http ) ) {
				PluginVM_Denied( vm, "HttpRequest", "http" );
				return 0;
			}
			const char * p_url = NULL;
			const char * p_headers = NULL;
			const char * p_body = NULL;
			PluginVM_String( vm, args[2], qfalse, &p_url );
			PluginVM_String( vm, args[3], qtrue, &p_headers );
			if ( (uint32_t)args[5] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[5] != 0 ) {
				PluginVM_Ptr( vm, args[4], (unsigned)args[5] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qtrue, (void **)&p_body );
			}
			return Imp_HttpRequest( (int)args[1], p_url, p_headers, p_body, (int)args[5], (int)args[6], (int)args[7], (int)args[8] );
		}
		case 19: {		// HttpBody
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_http ) ) {
				PluginVM_Denied( vm, "HttpBody", "http" );
				return 0;
			}
			char * p_out = NULL;
			if ( (uint32_t)args[4] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[4] != 0 ) {
				PluginVM_Ptr( vm, args[3], (unsigned)args[4] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qfalse, (void **)&p_out );
			}
			return Imp_HttpBody( (int)args[1], (int)args[2], p_out, (int)args[4] );
		}
		case 20: {		// StoreRead
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_storage ) ) {
				PluginVM_Denied( vm, "StoreRead", "storage" );
				return 0;
			}
			const char * p_name = NULL;
			char * p_out = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			if ( (uint32_t)args[4] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[4] != 0 ) {
				PluginVM_Ptr( vm, args[3], (unsigned)args[4] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qfalse, (void **)&p_out );
			}
			return Imp_StoreRead( p_name, (int)args[2], p_out, (int)args[4] );
		}
		case 21: {		// StoreWrite
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_storage ) ) {
				PluginVM_Denied( vm, "StoreWrite", "storage" );
				return 0;
			}
			const char * p_name = NULL;
			const char * p_data = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			if ( (uint32_t)args[3] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[3] != 0 ) {
				PluginVM_Ptr( vm, args[2], (unsigned)args[3] * sizeof( char ), ODF_ALIGNOF( char ) < 4 ? ODF_ALIGNOF( char ) : 4, qtrue, (void **)&p_data );
			}
			return Imp_StoreWrite( p_name, p_data, (int)args[3], (int)args[4] );
		}
		case 22: {		// StoreSize
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_storage ) ) {
				PluginVM_Denied( vm, "StoreSize", "storage" );
				return 0;
			}
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_StoreSize( p_name );
		}
		case 23: {		// StoreDelete
			if ( !( PluginVM_Caps( vm ) & ODFS_CAP_storage ) ) {
				PluginVM_Denied( vm, "StoreDelete", "storage" );
				return 0;
			}
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_StoreDelete( p_name );
		}
		default:
			PluginVM_Trap( vm, "called a function the engine does not have" );
			return 0;
	}
}

enum {
	ODFS_EXP_OnInit,
	ODFS_EXP_OnShutdown,
	ODFS_EXP_OnFrame,
	ODFS_EXP_EventHandler,
	ODFS_EXP_ChatCommandHandler,
	ODFS_EXP_HttpHandler,
	ODFS_NUM_EXPORTS
};
static const char *const odfs_exportNames[] = { "OnInit", "OnShutdown", "OnFrame", "EventHandler", "ChatCommandHandler", "HttpHandler" };
// capability an export needs to be called (0 = none)
static const unsigned odfs_exportCaps[] = { 0, 0, 0, 0, 0, 0 };

static qboolean ODFS_Call_OnInit( pluginVM_t *w, int *result )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFS_EXP_OnInit ) ) {
		*result = 0;
		return qtrue;
	}
	PluginVM_IOBegin( w );
	return PluginVM_Call( w, ODFS_EXP_OnInit, args, 0, result, PLUGIN_VM_BUDGET_INIT );
}

static qboolean ODFS_Call_OnShutdown( pluginVM_t *w )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFS_EXP_OnShutdown ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	return PluginVM_Call( w, ODFS_EXP_OnShutdown, args, 0, NULL, PLUGIN_VM_BUDGET_INIT );
}

static qboolean ODFS_Call_OnFrame( pluginVM_t *w, int a_serverTime )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFS_EXP_OnFrame ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_serverTime;
	args[0] = (int)o;
	return PluginVM_Call( w, ODFS_EXP_OnFrame, args, 1, NULL, PLUGIN_VM_BUDGET_FRAME );
}

static qboolean ODFS_Call_EventHandler( pluginVM_t *w, int a_handler, const char *a_data )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFS_EXP_EventHandler ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_handler;
	args[0] = (int)o;
	o = PluginVM_IOPutString( w, a_data );
	if ( !o ) {
		PluginVM_Fail( w, "the arguments of EventHandler do not fit the io buffer" );
		return qfalse;
	}
	args[1] = (int)o;
	return PluginVM_Call( w, ODFS_EXP_EventHandler, args, 2, NULL, PLUGIN_VM_BUDGET_FRAME );
}

static qboolean ODFS_Call_ChatCommandHandler( pluginVM_t *w, int a_handler, int a_clientNum, int a_serial, const char *a_args )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFS_EXP_ChatCommandHandler ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_handler;
	args[0] = (int)o;
	o = (unsigned)a_clientNum;
	args[1] = (int)o;
	o = (unsigned)a_serial;
	args[2] = (int)o;
	o = PluginVM_IOPutString( w, a_args );
	if ( !o ) {
		PluginVM_Fail( w, "the arguments of ChatCommandHandler do not fit the io buffer" );
		return qfalse;
	}
	args[3] = (int)o;
	return PluginVM_Call( w, ODFS_EXP_ChatCommandHandler, args, 4, NULL, PLUGIN_VM_BUDGET_FRAME );
}

static qboolean ODFS_Call_HttpHandler( pluginVM_t *w, int a_handler, int a_id, int a_status, int a_bodyLen, int a_userData )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFS_EXP_HttpHandler ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_handler;
	args[0] = (int)o;
	o = (unsigned)a_id;
	args[1] = (int)o;
	o = (unsigned)a_status;
	args[2] = (int)o;
	o = (unsigned)a_bodyLen;
	args[3] = (int)o;
	o = (unsigned)a_userData;
	args[4] = (int)o;
	return PluginVM_Call( w, ODFS_EXP_HttpHandler, args, 5, NULL, PLUGIN_VM_BUDGET_FRAME );
}

// events and the capability that is needed to get them (0 = none)
static const struct { const char *name; unsigned cap; } odfs_eventCaps[] = {
	{ "player_finish", 0 },
	{ "auth_response", ODFS_CAP_auth },
	{ "client_disconnect", 0 },
	{ "map_begin", 0 },
};
