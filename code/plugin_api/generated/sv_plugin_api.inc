/* Generated from server.api by gen_api.py - do not edit. */

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

// the functions the engine implements
static void Imp_Print( const char *text );
static int Imp_Milliseconds( void );
static int Imp_RandomBytes( char *buf, int len );
static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags );
static void Imp_CvarString( const char *name, char *out, int outSize );
static int Imp_CvarInteger( const char *name );
static int Imp_GetClientInfo( int clientNum, odfClientInfo_t *out );
static void Imp_SendServerCommand( int clientNum, const char *command );
static void Imp_AuthResult( int clientNum, int serial, int result, int64_t accountID, const char *accountName, const char *reason );
static int Imp_HttpRequest( int method, const char *url, const char *headers, const char *body, int bodyLen, int timeoutMs );
static int Imp_HttpBody( int id, int offset, char *out, int outSize );
static int Imp_StoreRead( const char *name, int offset, char *out, int outSize );
static int Imp_StoreWrite( const char *name, const char *data, int len, int append );
static int Imp_StoreSize( const char *name );
static int Imp_StoreDelete( const char *name );

static void W_Print( void *env, uint32_t a_text )
{
	odfWasmImportCalls++;
	const char * p_text = NULL;
	if ( !OdfWasm_String( env, a_text, qfalse, &p_text ) ) {
		return;
	}
	Imp_Print( p_text );
}

static int W_Milliseconds( void *env )
{
	odfWasmImportCalls++;
	return Imp_Milliseconds(  );
}

static int W_RandomBytes( void *env, uint32_t a_buf, int32_t a_len )
{
	odfWasmImportCalls++;
	char * p_buf = NULL;
	if ( (uint32_t)a_len > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return 0;
	}
	if ( a_len != 0 && !OdfWasm_Ptr( env, a_buf, (unsigned)a_len * sizeof( char ), __alignof__( char ), qfalse, (void **)&p_buf ) ) {
		return 0;
	}
	return Imp_RandomBytes( p_buf, a_len );
}

static void W_CvarRegister( void *env, uint32_t a_name, uint32_t a_defaultValue, int32_t a_flags )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	const char * p_defaultValue = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return;
	}
	if ( !OdfWasm_String( env, a_defaultValue, qfalse, &p_defaultValue ) ) {
		return;
	}
	Imp_CvarRegister( p_name, p_defaultValue, a_flags );
}

static void W_CvarString( void *env, uint32_t a_name, uint32_t a_out, int32_t a_outSize )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	char * p_out = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return;
	}
	if ( (uint32_t)a_outSize > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return;
	}
	if ( a_outSize != 0 && !OdfWasm_Ptr( env, a_out, (unsigned)a_outSize * sizeof( char ), __alignof__( char ), qfalse, (void **)&p_out ) ) {
		return;
	}
	Imp_CvarString( p_name, p_out, a_outSize );
}

static int W_CvarInteger( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	return Imp_CvarInteger( p_name );
}

static int W_GetClientInfo( void *env, int32_t a_clientNum, uint32_t a_out )
{
	odfWasmImportCalls++;
	odfClientInfo_t * p_out = NULL;
	if ( !OdfWasm_Ptr( env, a_out, sizeof( odfClientInfo_t ), __alignof__( odfClientInfo_t ), qfalse, (void **)&p_out ) ) {
		return 0;
	}
	return Imp_GetClientInfo( a_clientNum, p_out );
}

static void W_SendServerCommand( void *env, int32_t a_clientNum, uint32_t a_command )
{
	odfWasmImportCalls++;
	const char * p_command = NULL;
	if ( !OdfWasm_String( env, a_command, qfalse, &p_command ) ) {
		return;
	}
	Imp_SendServerCommand( a_clientNum, p_command );
}

static void W_AuthResult( void *env, int32_t a_clientNum, int32_t a_serial, int32_t a_result, int64_t a_accountID, uint32_t a_accountName, uint32_t a_reason )
{
	odfWasmImportCalls++;
	const char * p_accountName = NULL;
	const char * p_reason = NULL;
	if ( !OdfWasm_String( env, a_accountName, qtrue, &p_accountName ) ) {
		return;
	}
	if ( !OdfWasm_String( env, a_reason, qtrue, &p_reason ) ) {
		return;
	}
	Imp_AuthResult( a_clientNum, a_serial, a_result, a_accountID, p_accountName, p_reason );
}

static int W_HttpRequest( void *env, int32_t a_method, uint32_t a_url, uint32_t a_headers, uint32_t a_body, int32_t a_bodyLen, int32_t a_timeoutMs )
{
	odfWasmImportCalls++;
	const char * p_url = NULL;
	const char * p_headers = NULL;
	const char * p_body = NULL;
	if ( !OdfWasm_String( env, a_url, qfalse, &p_url ) ) {
		return 0;
	}
	if ( !OdfWasm_String( env, a_headers, qtrue, &p_headers ) ) {
		return 0;
	}
	if ( (uint32_t)a_bodyLen > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return 0;
	}
	if ( a_bodyLen != 0 && !OdfWasm_Ptr( env, a_body, (unsigned)a_bodyLen * sizeof( char ), __alignof__( char ), qtrue, (void **)&p_body ) ) {
		return 0;
	}
	return Imp_HttpRequest( a_method, p_url, p_headers, p_body, a_bodyLen, a_timeoutMs );
}

static int W_HttpBody( void *env, int32_t a_id, int32_t a_offset, uint32_t a_out, int32_t a_outSize )
{
	odfWasmImportCalls++;
	char * p_out = NULL;
	if ( (uint32_t)a_outSize > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return 0;
	}
	if ( a_outSize != 0 && !OdfWasm_Ptr( env, a_out, (unsigned)a_outSize * sizeof( char ), __alignof__( char ), qfalse, (void **)&p_out ) ) {
		return 0;
	}
	return Imp_HttpBody( a_id, a_offset, p_out, a_outSize );
}

static int W_StoreRead( void *env, uint32_t a_name, int32_t a_offset, uint32_t a_out, int32_t a_outSize )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	char * p_out = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	if ( (uint32_t)a_outSize > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return 0;
	}
	if ( a_outSize != 0 && !OdfWasm_Ptr( env, a_out, (unsigned)a_outSize * sizeof( char ), __alignof__( char ), qfalse, (void **)&p_out ) ) {
		return 0;
	}
	return Imp_StoreRead( p_name, a_offset, p_out, a_outSize );
}

static int W_StoreWrite( void *env, uint32_t a_name, uint32_t a_data, int32_t a_len, int32_t a_append )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	const char * p_data = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	if ( (uint32_t)a_len > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return 0;
	}
	if ( a_len != 0 && !OdfWasm_Ptr( env, a_data, (unsigned)a_len * sizeof( char ), __alignof__( char ), qtrue, (void **)&p_data ) ) {
		return 0;
	}
	return Imp_StoreWrite( p_name, p_data, a_len, a_append );
}

static int W_StoreSize( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	return Imp_StoreSize( p_name );
}

static int W_StoreDelete( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	return Imp_StoreDelete( p_name );
}

static const odfWasmImport_t odfs_importTable[] = {
	{ "Print", (void *)W_Print, "(i)", 0 },
	{ "Milliseconds", (void *)W_Milliseconds, "()i", 0 },
	{ "RandomBytes", (void *)W_RandomBytes, "(ii)i", 0 },
	{ "CvarRegister", (void *)W_CvarRegister, "(iii)", 0 },
	{ "CvarString", (void *)W_CvarString, "(iii)", 0 },
	{ "CvarInteger", (void *)W_CvarInteger, "(i)i", 0 },
	{ "GetClientInfo", (void *)W_GetClientInfo, "(ii)i", 0 },
	{ "SendServerCommand", (void *)W_SendServerCommand, "(ii)", 0 },
	{ "AuthResult", (void *)W_AuthResult, "(iiiIii)", ODFS_CAP_auth },
	{ "HttpRequest", (void *)W_HttpRequest, "(iiiiii)i", ODFS_CAP_http },
	{ "HttpBody", (void *)W_HttpBody, "(iiii)i", ODFS_CAP_http },
	{ "StoreRead", (void *)W_StoreRead, "(iiii)i", ODFS_CAP_storage },
	{ "StoreWrite", (void *)W_StoreWrite, "(iiii)i", ODFS_CAP_storage },
	{ "StoreSize", (void *)W_StoreSize, "(i)i", ODFS_CAP_storage },
	{ "StoreDelete", (void *)W_StoreDelete, "(i)i", ODFS_CAP_storage },
};
#define ODFS_NUM_IMPORTS	( (int)( sizeof( odfs_importTable ) / sizeof( odfs_importTable[0] ) ) )

enum {
	ODFS_EXP_init,
	ODFS_EXP_shutdown,
	ODFS_EXP_frame,
	ODFS_EXP_on_event,
	ODFS_EXP_on_http,
	ODFS_NUM_EXPORTS
};
static const char *const odfs_exportNames[] = { "init", "shutdown", "frame", "on_event", "on_http" };
// capability an export needs to be called (0 = none)
static const unsigned odfs_exportCaps[] = { 0, 0, 0, 0, 0 };

static qboolean ODFS_Call_init( odfWasm_t *w, int *result )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFS_EXP_init ) ) {
		*result = 0;
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	return OdfWasm_Call( w, ODFS_EXP_init, args, 0, result, ODF_WASM_BUDGET_INIT );
}

static qboolean ODFS_Call_shutdown( odfWasm_t *w )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFS_EXP_shutdown ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	return OdfWasm_Call( w, ODFS_EXP_shutdown, args, 0, NULL, ODF_WASM_BUDGET_INIT );
}

static qboolean ODFS_Call_frame( odfWasm_t *w, int a_serverTime )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFS_EXP_frame ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = (unsigned)a_serverTime;
	args[0] = (int)o;
	return OdfWasm_Call( w, ODFS_EXP_frame, args, 1, NULL, ODF_WASM_BUDGET_FRAME );
}

static qboolean ODFS_Call_on_event( odfWasm_t *w, const char *a_name, const char *a_data )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFS_EXP_on_event ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = OdfWasm_IOPutString( w, a_name );
	if ( !o ) {
		OdfWasm_Fail( w, "the arguments of on_event do not fit the io buffer" );
		return qfalse;
	}
	args[0] = (int)o;
	o = OdfWasm_IOPutString( w, a_data );
	if ( !o ) {
		OdfWasm_Fail( w, "the arguments of on_event do not fit the io buffer" );
		return qfalse;
	}
	args[1] = (int)o;
	return OdfWasm_Call( w, ODFS_EXP_on_event, args, 2, NULL, ODF_WASM_BUDGET_FRAME );
}

static qboolean ODFS_Call_on_http( odfWasm_t *w, int a_id, int a_status, int a_bodyLen )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFS_EXP_on_http ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = (unsigned)a_id;
	args[0] = (int)o;
	o = (unsigned)a_status;
	args[1] = (int)o;
	o = (unsigned)a_bodyLen;
	args[2] = (int)o;
	return OdfWasm_Call( w, ODFS_EXP_on_http, args, 3, NULL, ODF_WASM_BUDGET_FRAME );
}

// events and the capability that is needed to get them (0 = none)
static const struct { const char *name; unsigned cap; } odfs_eventCaps[] = {
	{ "player_finish", 0 },
	{ "auth_response", ODFS_CAP_auth },
	{ "chat_command", ODFS_CAP_chat },
	{ "client_disconnect", 0 },
	{ "map_begin", 0 },
};
