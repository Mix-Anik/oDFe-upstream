/* Generated from client.api by gen_api.py - do not edit. */

#define ODFC_API_VERSION	1
#define ODFC_CAP_world	( 1u << 0 )
#define ODFC_CAP_draw	( 1u << 1 )
#define ODFC_CAP_command	( 1u << 2 )
#define ODFC_CAP_sound	( 1u << 3 )
#define ODFC_CAPS_RESTRICTED	( 0 )
static const char *const odfc_capNames[] = { "world", "draw", "command", "sound" };
#define ODFC_NUM_CAPS	4

// the functions the engine implements
static void Imp_Print( const char *text );
static int Imp_Milliseconds( void );
static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags );
static void Imp_CvarString( const char *name, char *out, int outSize );
static int Imp_CvarInteger( const char *name );
static float Imp_CvarValue( const char *name );
static int Imp_CheatsEnabled( void );
static int Imp_GetSnapshot( odfcSnapshot_t *out );
static void Imp_Trace( odfcTrace_t *out, const float *start, const float *mins, const float *maxs, const float *end, int contentMask );
static int Imp_GetPlayerState( odfcPlayerState_t *out );
static int Imp_GetMoveState( odfcMoveState_t *out );
static int Imp_PointContents( const float *point );
static int Imp_GetWeaponState( odfcWeaponState_t *out );
static int Imp_RegisterShader( const char *name );
static int Imp_RegisterShaderNoMip( const char *name );
static int Imp_RegisterModel( const char *name );
static void Imp_AddPoly( int shader, int numVerts, const odfcPolyVert_t *verts );
static void Imp_AddBeam( int shader, const float *from, const float *to, const unsigned char *rgba );
static void Imp_SetColor( const float *rgba );
static void Imp_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader );
static void Imp_DrawModel( float x, float y, float w, float h, int model, const float *origin, const float *angles, int time );
static int Imp_CommandRegister( const char *name );
static int Imp_CommandArgc( void );
static void Imp_CommandArgv( int n, char *out, int outSize );

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

static float W_CvarValue( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0.0f;
	}
	return Imp_CvarValue( p_name );
}

static int W_CheatsEnabled( void *env )
{
	odfWasmImportCalls++;
	return Imp_CheatsEnabled(  );
}

static int W_GetSnapshot( void *env, uint32_t a_out )
{
	odfWasmImportCalls++;
	odfcSnapshot_t * p_out = NULL;
	if ( !OdfWasm_Ptr( env, a_out, sizeof( odfcSnapshot_t ), __alignof__( odfcSnapshot_t ), qfalse, (void **)&p_out ) ) {
		return 0;
	}
	return Imp_GetSnapshot( p_out );
}

static void W_Trace( void *env, uint32_t a_out, uint32_t a_start, uint32_t a_mins, uint32_t a_maxs, uint32_t a_end, int32_t a_contentMask )
{
	odfWasmImportCalls++;
	odfcTrace_t * p_out = NULL;
	const float * p_start = NULL;
	const float * p_mins = NULL;
	const float * p_maxs = NULL;
	const float * p_end = NULL;
	if ( !OdfWasm_Ptr( env, a_out, sizeof( odfcTrace_t ), __alignof__( odfcTrace_t ), qfalse, (void **)&p_out ) ) {
		return;
	}
	if ( !OdfWasm_Ptr( env, a_start, 3 * sizeof( float ), __alignof__( float ), qfalse, (void **)&p_start ) ) {
		return;
	}
	if ( !OdfWasm_Ptr( env, a_mins, 3 * sizeof( float ), __alignof__( float ), qtrue, (void **)&p_mins ) ) {
		return;
	}
	if ( !OdfWasm_Ptr( env, a_maxs, 3 * sizeof( float ), __alignof__( float ), qtrue, (void **)&p_maxs ) ) {
		return;
	}
	if ( !OdfWasm_Ptr( env, a_end, 3 * sizeof( float ), __alignof__( float ), qfalse, (void **)&p_end ) ) {
		return;
	}
	Imp_Trace( p_out, p_start, p_mins, p_maxs, p_end, a_contentMask );
}

static int W_GetPlayerState( void *env, uint32_t a_out )
{
	odfWasmImportCalls++;
	odfcPlayerState_t * p_out = NULL;
	if ( !OdfWasm_Ptr( env, a_out, sizeof( odfcPlayerState_t ), __alignof__( odfcPlayerState_t ), qfalse, (void **)&p_out ) ) {
		return 0;
	}
	return Imp_GetPlayerState( p_out );
}

static int W_GetMoveState( void *env, uint32_t a_out )
{
	odfWasmImportCalls++;
	odfcMoveState_t * p_out = NULL;
	if ( !OdfWasm_Ptr( env, a_out, sizeof( odfcMoveState_t ), __alignof__( odfcMoveState_t ), qfalse, (void **)&p_out ) ) {
		return 0;
	}
	return Imp_GetMoveState( p_out );
}

static int W_PointContents( void *env, uint32_t a_point )
{
	odfWasmImportCalls++;
	const float * p_point = NULL;
	if ( !OdfWasm_Ptr( env, a_point, 3 * sizeof( float ), __alignof__( float ), qfalse, (void **)&p_point ) ) {
		return 0;
	}
	return Imp_PointContents( p_point );
}

static int W_GetWeaponState( void *env, uint32_t a_out )
{
	odfWasmImportCalls++;
	odfcWeaponState_t * p_out = NULL;
	if ( !OdfWasm_Ptr( env, a_out, sizeof( odfcWeaponState_t ), __alignof__( odfcWeaponState_t ), qfalse, (void **)&p_out ) ) {
		return 0;
	}
	return Imp_GetWeaponState( p_out );
}

static int W_RegisterShader( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	return Imp_RegisterShader( p_name );
}

static int W_RegisterShaderNoMip( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	return Imp_RegisterShaderNoMip( p_name );
}

static int W_RegisterModel( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	return Imp_RegisterModel( p_name );
}

static void W_AddPoly( void *env, int32_t a_shader, int32_t a_numVerts, uint32_t a_verts )
{
	odfWasmImportCalls++;
	const odfcPolyVert_t * p_verts = NULL;
	if ( (uint32_t)a_numVerts > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return;
	}
	if ( a_numVerts != 0 && !OdfWasm_Ptr( env, a_verts, (unsigned)a_numVerts * sizeof( odfcPolyVert_t ), __alignof__( odfcPolyVert_t ), qfalse, (void **)&p_verts ) ) {
		return;
	}
	Imp_AddPoly( a_shader, a_numVerts, p_verts );
}

static void W_AddBeam( void *env, int32_t a_shader, uint32_t a_from, uint32_t a_to, uint32_t a_rgba )
{
	odfWasmImportCalls++;
	const float * p_from = NULL;
	const float * p_to = NULL;
	const unsigned char * p_rgba = NULL;
	if ( !OdfWasm_Ptr( env, a_from, 3 * sizeof( float ), __alignof__( float ), qfalse, (void **)&p_from ) ) {
		return;
	}
	if ( !OdfWasm_Ptr( env, a_to, 3 * sizeof( float ), __alignof__( float ), qfalse, (void **)&p_to ) ) {
		return;
	}
	if ( !OdfWasm_Ptr( env, a_rgba, 4 * sizeof( unsigned char ), __alignof__( unsigned char ), qfalse, (void **)&p_rgba ) ) {
		return;
	}
	Imp_AddBeam( a_shader, p_from, p_to, p_rgba );
}

static void W_SetColor( void *env, uint32_t a_rgba )
{
	odfWasmImportCalls++;
	const float * p_rgba = NULL;
	if ( !OdfWasm_Ptr( env, a_rgba, 4 * sizeof( float ), __alignof__( float ), qtrue, (void **)&p_rgba ) ) {
		return;
	}
	Imp_SetColor( p_rgba );
}

static void W_DrawStretchPic( void *env, float a_x, float a_y, float a_w, float a_h, float a_s1, float a_t1, float a_s2, float a_t2, int32_t a_shader )
{
	odfWasmImportCalls++;
	Imp_DrawStretchPic( a_x, a_y, a_w, a_h, a_s1, a_t1, a_s2, a_t2, a_shader );
}

static void W_DrawModel( void *env, float a_x, float a_y, float a_w, float a_h, int32_t a_model, uint32_t a_origin, uint32_t a_angles, int32_t a_time )
{
	odfWasmImportCalls++;
	const float * p_origin = NULL;
	const float * p_angles = NULL;
	if ( !OdfWasm_Ptr( env, a_origin, 3 * sizeof( float ), __alignof__( float ), qfalse, (void **)&p_origin ) ) {
		return;
	}
	if ( !OdfWasm_Ptr( env, a_angles, 3 * sizeof( float ), __alignof__( float ), qfalse, (void **)&p_angles ) ) {
		return;
	}
	Imp_DrawModel( a_x, a_y, a_w, a_h, a_model, p_origin, p_angles, a_time );
}

static int W_CommandRegister( void *env, uint32_t a_name )
{
	odfWasmImportCalls++;
	const char * p_name = NULL;
	if ( !OdfWasm_String( env, a_name, qfalse, &p_name ) ) {
		return 0;
	}
	return Imp_CommandRegister( p_name );
}

static int W_CommandArgc( void *env )
{
	odfWasmImportCalls++;
	return Imp_CommandArgc(  );
}

static void W_CommandArgv( void *env, int32_t a_n, uint32_t a_out, int32_t a_outSize )
{
	odfWasmImportCalls++;
	char * p_out = NULL;
	if ( (uint32_t)a_outSize > 1048576 ) {
		OdfWasm_Trap( env, "bad size passed to the engine" );
		return;
	}
	if ( a_outSize != 0 && !OdfWasm_Ptr( env, a_out, (unsigned)a_outSize * sizeof( char ), __alignof__( char ), qfalse, (void **)&p_out ) ) {
		return;
	}
	Imp_CommandArgv( a_n, p_out, a_outSize );
}

static const odfWasmImport_t odfc_importTable[] = {
	{ "Print", (void *)W_Print, "(i)", 0 },
	{ "Milliseconds", (void *)W_Milliseconds, "()i", 0 },
	{ "CvarRegister", (void *)W_CvarRegister, "(iii)", 0 },
	{ "CvarString", (void *)W_CvarString, "(iii)", 0 },
	{ "CvarInteger", (void *)W_CvarInteger, "(i)i", 0 },
	{ "CvarValue", (void *)W_CvarValue, "(i)f", 0 },
	{ "CheatsEnabled", (void *)W_CheatsEnabled, "()i", 0 },
	{ "GetSnapshot", (void *)W_GetSnapshot, "(i)i", ODFC_CAP_world },
	{ "Trace", (void *)W_Trace, "(iiiiii)", ODFC_CAP_world },
	{ "GetPlayerState", (void *)W_GetPlayerState, "(i)i", ODFC_CAP_world },
	{ "GetMoveState", (void *)W_GetMoveState, "(i)i", ODFC_CAP_world },
	{ "PointContents", (void *)W_PointContents, "(i)i", ODFC_CAP_world },
	{ "GetWeaponState", (void *)W_GetWeaponState, "(i)i", ODFC_CAP_world },
	{ "RegisterShader", (void *)W_RegisterShader, "(i)i", ODFC_CAP_draw },
	{ "RegisterShaderNoMip", (void *)W_RegisterShaderNoMip, "(i)i", ODFC_CAP_draw },
	{ "RegisterModel", (void *)W_RegisterModel, "(i)i", ODFC_CAP_draw },
	{ "AddPoly", (void *)W_AddPoly, "(iii)", ODFC_CAP_draw },
	{ "AddBeam", (void *)W_AddBeam, "(iiii)", ODFC_CAP_draw },
	{ "SetColor", (void *)W_SetColor, "(i)", ODFC_CAP_draw },
	{ "DrawStretchPic", (void *)W_DrawStretchPic, "(ffffffffi)", ODFC_CAP_draw },
	{ "DrawModel", (void *)W_DrawModel, "(ffffiiii)", ODFC_CAP_draw },
	{ "CommandRegister", (void *)W_CommandRegister, "(i)i", ODFC_CAP_command },
	{ "CommandArgc", (void *)W_CommandArgc, "()i", ODFC_CAP_command },
	{ "CommandArgv", (void *)W_CommandArgv, "(iii)", ODFC_CAP_command },
};
#define ODFC_NUM_IMPORTS	( (int)( sizeof( odfc_importTable ) / sizeof( odfc_importTable[0] ) ) )

enum {
	ODFC_EXP_init,
	ODFC_EXP_shutdown,
	ODFC_EXP_map_load,
	ODFC_EXP_cgame_shutdown,
	ODFC_EXP_draw3d,
	ODFC_EXP_draw2d,
	ODFC_EXP_command,
	ODFC_EXP_filter_sound,
	ODFC_EXP_demo_seek,
	ODFC_NUM_EXPORTS
};
static const char *const odfc_exportNames[] = { "init", "shutdown", "map_load", "cgame_shutdown", "draw3d", "draw2d", "command", "filter_sound", "demo_seek" };
// capability an export needs to be called (0 = none)
static const unsigned odfc_exportCaps[] = { 0, 0, 0, 0, 0, 0, 0, ODFC_CAP_sound, 0 };

static qboolean ODFC_Call_init( odfWasm_t *w, int *result )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_init ) ) {
		*result = 0;
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	return OdfWasm_Call( w, ODFC_EXP_init, args, 0, result, ODF_WASM_BUDGET_INIT );
}

static qboolean ODFC_Call_shutdown( odfWasm_t *w )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_shutdown ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	return OdfWasm_Call( w, ODFC_EXP_shutdown, args, 0, NULL, ODF_WASM_BUDGET_INIT );
}

static qboolean ODFC_Call_map_load( odfWasm_t *w, const char *a_mapName )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_map_load ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = OdfWasm_IOPutString( w, a_mapName );
	if ( !o ) {
		OdfWasm_Fail( w, "the arguments of map_load do not fit the io buffer" );
		return qfalse;
	}
	args[0] = (int)o;
	return OdfWasm_Call( w, ODFC_EXP_map_load, args, 1, NULL, ODF_WASM_BUDGET_INIT );
}

static qboolean ODFC_Call_cgame_shutdown( odfWasm_t *w )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_cgame_shutdown ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	return OdfWasm_Call( w, ODFC_EXP_cgame_shutdown, args, 0, NULL, ODF_WASM_BUDGET_INIT );
}

static qboolean ODFC_Call_draw3d( odfWasm_t *w, const odfcView_t *a_view )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_draw3d ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = OdfWasm_IOPut( w, a_view, sizeof( odfcView_t ), __alignof__( odfcView_t ) );
	if ( !o ) {
		OdfWasm_Fail( w, "the arguments of draw3d do not fit the io buffer" );
		return qfalse;
	}
	args[0] = (int)o;
	return OdfWasm_Call( w, ODFC_EXP_draw3d, args, 1, NULL, ODF_WASM_BUDGET_FRAME );
}

static qboolean ODFC_Call_draw2d( odfWasm_t *w, int a_phase )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_draw2d ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = (unsigned)a_phase;
	args[0] = (int)o;
	return OdfWasm_Call( w, ODFC_EXP_draw2d, args, 1, NULL, ODF_WASM_BUDGET_FRAME );
}

static qboolean ODFC_Call_command( odfWasm_t *w, const char *a_name )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_command ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = OdfWasm_IOPutString( w, a_name );
	if ( !o ) {
		OdfWasm_Fail( w, "the arguments of command do not fit the io buffer" );
		return qfalse;
	}
	args[0] = (int)o;
	return OdfWasm_Call( w, ODFC_EXP_command, args, 1, NULL, ODF_WASM_BUDGET_FRAME );
}

static qboolean ODFC_Call_filter_sound( odfWasm_t *w, int a_entityNum, int a_isLoop, int a_entityClientNum, int a_ownClientNum, int *result )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_filter_sound ) ) {
		*result = 0;
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = (unsigned)a_entityNum;
	args[0] = (int)o;
	o = (unsigned)a_isLoop;
	args[1] = (int)o;
	o = (unsigned)a_entityClientNum;
	args[2] = (int)o;
	o = (unsigned)a_ownClientNum;
	args[3] = (int)o;
	return OdfWasm_Call( w, ODFC_EXP_filter_sound, args, 4, result, ODF_WASM_BUDGET_FRAME );
}

static qboolean ODFC_Call_demo_seek( odfWasm_t *w, int a_fromTime, int a_toTime )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !OdfWasm_Has( w, ODFC_EXP_demo_seek ) ) {
		return qtrue;
	}
	OdfWasm_IOBegin( w );
	o = (unsigned)a_fromTime;
	args[0] = (int)o;
	o = (unsigned)a_toTime;
	args[1] = (int)o;
	return OdfWasm_Call( w, ODFC_EXP_demo_seek, args, 2, NULL, ODF_WASM_BUDGET_FRAME );
}
