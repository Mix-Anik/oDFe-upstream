/* Generated from client.api by gen_api.py - do not edit. */

#ifndef ODF_ALIGNOF
#ifdef _MSC_VER
#define ODF_ALIGNOF	__alignof
#else
#define ODF_ALIGNOF	__alignof__
#endif
#endif

#define ODFC_API_VERSION	1
#define ODFC_CAP_world	( 1u << 0 )
#define ODFC_CAP_draw	( 1u << 1 )
#define ODFC_CAP_command	( 1u << 2 )
#define ODFC_CAP_sound	( 1u << 3 )
#define ODFC_CAPS_RESTRICTED	( 0 )
static const char *const odfc_capNames[] = { "world", "draw", "command", "sound" };
#define ODFC_NUM_CAPS	4

#define ODFC_NUM_IMPORTS	28

// The functions the engine implements, written by hand. The host also has to define
//   static qboolean ODF_CheatsAllowed( void );   the rule for the functions marked cheat
static void Imp_Print( const char *text );
static int Imp_Milliseconds( void );
static void Imp_CvarRegister( const char *name, const char *defaultValue, int flags );
static void Imp_CvarString( const char *name, char *out, int outSize );
static int Imp_CvarInteger( const char *name );
static float Imp_CvarValue( const char *name );
static int Imp_CheatsEnabled( void );
static float Imp_Sin( float x );
static float Imp_Cos( float x );
static float Imp_Atan2( float y, float x );
static float Imp_Sqrt( float x );
static void Imp_MemSet( char *dst, int value, int size );
static void Imp_MemCpy( char *dst, const char *src, int size );
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
static int Imp_CommandRegister( const char *name, int handler );

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
static intptr_t Odfc_Dispatch( pluginVM_t *vm, intptr_t *args )
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
		case 2: {		// CvarRegister
			const char * p_name = NULL;
			const char * p_defaultValue = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			PluginVM_String( vm, args[2], qfalse, &p_defaultValue );
			Imp_CvarRegister( p_name, p_defaultValue, (int)args[3] );
			return 0;
		}
		case 3: {		// CvarString
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
		case 4: {		// CvarInteger
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_CvarInteger( p_name );
		}
		case 5: {		// CvarValue
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Odf_FloatBits( Imp_CvarValue( p_name ) );
		}
		case 6: {		// CheatsEnabled
			return Imp_CheatsEnabled(  );
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
		case 13: {		// GetSnapshot
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_world ) ) {
				PluginVM_Denied( vm, "GetSnapshot", "world" );
				return 0;
			}
			odfcSnapshot_t * p_out = NULL;
			PluginVM_Ptr( vm, args[1], sizeof( odfcSnapshot_t ), ODF_ALIGNOF( odfcSnapshot_t ) < 4 ? ODF_ALIGNOF( odfcSnapshot_t ) : 4, qfalse, (void **)&p_out );
			return Imp_GetSnapshot( p_out );
		}
		case 14: {		// Trace
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_world ) ) {
				PluginVM_Denied( vm, "Trace", "world" );
				return 0;
			}
			odfcTrace_t * p_out = NULL;
			const float * p_start = NULL;
			const float * p_mins = NULL;
			const float * p_maxs = NULL;
			const float * p_end = NULL;
			PluginVM_Ptr( vm, args[1], sizeof( odfcTrace_t ), ODF_ALIGNOF( odfcTrace_t ) < 4 ? ODF_ALIGNOF( odfcTrace_t ) : 4, qfalse, (void **)&p_out );
			PluginVM_Ptr( vm, args[2], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qfalse, (void **)&p_start );
			PluginVM_Ptr( vm, args[3], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qtrue, (void **)&p_mins );
			PluginVM_Ptr( vm, args[4], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qtrue, (void **)&p_maxs );
			PluginVM_Ptr( vm, args[5], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qfalse, (void **)&p_end );
			Imp_Trace( p_out, p_start, p_mins, p_maxs, p_end, (int)args[6] );
			return 0;
		}
		case 15: {		// GetPlayerState
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_world ) ) {
				PluginVM_Denied( vm, "GetPlayerState", "world" );
				return 0;
			}
			odfcPlayerState_t * p_out = NULL;
			PluginVM_Ptr( vm, args[1], sizeof( odfcPlayerState_t ), ODF_ALIGNOF( odfcPlayerState_t ) < 4 ? ODF_ALIGNOF( odfcPlayerState_t ) : 4, qfalse, (void **)&p_out );
			return Imp_GetPlayerState( p_out );
		}
		case 16: {		// GetMoveState
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_world ) ) {
				PluginVM_Denied( vm, "GetMoveState", "world" );
				return 0;
			}
			odfcMoveState_t * p_out = NULL;
			PluginVM_Ptr( vm, args[1], sizeof( odfcMoveState_t ), ODF_ALIGNOF( odfcMoveState_t ) < 4 ? ODF_ALIGNOF( odfcMoveState_t ) : 4, qfalse, (void **)&p_out );
			return Imp_GetMoveState( p_out );
		}
		case 17: {		// PointContents
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_world ) ) {
				PluginVM_Denied( vm, "PointContents", "world" );
				return 0;
			}
			const float * p_point = NULL;
			PluginVM_Ptr( vm, args[1], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qfalse, (void **)&p_point );
			return Imp_PointContents( p_point );
		}
		case 18: {		// GetWeaponState
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_world ) ) {
				PluginVM_Denied( vm, "GetWeaponState", "world" );
				return 0;
			}
			odfcWeaponState_t * p_out = NULL;
			PluginVM_Ptr( vm, args[1], sizeof( odfcWeaponState_t ), ODF_ALIGNOF( odfcWeaponState_t ) < 4 ? ODF_ALIGNOF( odfcWeaponState_t ) : 4, qfalse, (void **)&p_out );
			return Imp_GetWeaponState( p_out );
		}
		case 19: {		// RegisterShader
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "RegisterShader", "draw" );
				return 0;
			}
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_RegisterShader( p_name );
		}
		case 20: {		// RegisterShaderNoMip
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "RegisterShaderNoMip", "draw" );
				return 0;
			}
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_RegisterShaderNoMip( p_name );
		}
		case 21: {		// RegisterModel
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "RegisterModel", "draw" );
				return 0;
			}
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_RegisterModel( p_name );
		}
		case 22: {		// AddPoly
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "AddPoly", "draw" );
				return 0;
			}
			const odfcPolyVert_t * p_verts = NULL;
			if ( (uint32_t)args[2] > 1048576 ) {
				PluginVM_Trap( vm, "bad size passed to the engine" );
				return 0;
			}
			if ( args[2] != 0 ) {
				PluginVM_Ptr( vm, args[3], (unsigned)args[2] * sizeof( odfcPolyVert_t ), ODF_ALIGNOF( odfcPolyVert_t ) < 4 ? ODF_ALIGNOF( odfcPolyVert_t ) : 4, qfalse, (void **)&p_verts );
			}
			Imp_AddPoly( (int)args[1], (int)args[2], p_verts );
			return 0;
		}
		case 23: {		// AddBeam
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "AddBeam", "draw" );
				return 0;
			}
			const float * p_from = NULL;
			const float * p_to = NULL;
			const unsigned char * p_rgba = NULL;
			PluginVM_Ptr( vm, args[2], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qfalse, (void **)&p_from );
			PluginVM_Ptr( vm, args[3], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qfalse, (void **)&p_to );
			PluginVM_Ptr( vm, args[4], 4 * sizeof( unsigned char ), ODF_ALIGNOF( unsigned char ) < 4 ? ODF_ALIGNOF( unsigned char ) : 4, qfalse, (void **)&p_rgba );
			Imp_AddBeam( (int)args[1], p_from, p_to, p_rgba );
			return 0;
		}
		case 24: {		// SetColor
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "SetColor", "draw" );
				return 0;
			}
			const float * p_rgba = NULL;
			PluginVM_Ptr( vm, args[1], 4 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qtrue, (void **)&p_rgba );
			Imp_SetColor( p_rgba );
			return 0;
		}
		case 25: {		// DrawStretchPic
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "DrawStretchPic", "draw" );
				return 0;
			}
			Imp_DrawStretchPic( Odf_ArgFloat( args[1] ), Odf_ArgFloat( args[2] ), Odf_ArgFloat( args[3] ), Odf_ArgFloat( args[4] ), Odf_ArgFloat( args[5] ), Odf_ArgFloat( args[6] ), Odf_ArgFloat( args[7] ), Odf_ArgFloat( args[8] ), (int)args[9] );
			return 0;
		}
		case 26: {		// DrawModel
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_draw ) ) {
				PluginVM_Denied( vm, "DrawModel", "draw" );
				return 0;
			}
			const float * p_origin = NULL;
			const float * p_angles = NULL;
			PluginVM_Ptr( vm, args[6], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qfalse, (void **)&p_origin );
			PluginVM_Ptr( vm, args[7], 3 * sizeof( float ), ODF_ALIGNOF( float ) < 4 ? ODF_ALIGNOF( float ) : 4, qfalse, (void **)&p_angles );
			Imp_DrawModel( Odf_ArgFloat( args[1] ), Odf_ArgFloat( args[2] ), Odf_ArgFloat( args[3] ), Odf_ArgFloat( args[4] ), (int)args[5], p_origin, p_angles, (int)args[8] );
			return 0;
		}
		case 27: {		// CommandRegister
			if ( !( PluginVM_Caps( vm ) & ODFC_CAP_command ) ) {
				PluginVM_Denied( vm, "CommandRegister", "command" );
				return 0;
			}
			const char * p_name = NULL;
			PluginVM_String( vm, args[1], qfalse, &p_name );
			return Imp_CommandRegister( p_name, (int)args[2] );
		}
		default:
			PluginVM_Trap( vm, "called a function the engine does not have" );
			return 0;
	}
}

enum {
	ODFC_EXP_OnInit,
	ODFC_EXP_OnShutdown,
	ODFC_EXP_OnMapLoad,
	ODFC_EXP_OnCgameShutdown,
	ODFC_EXP_OnDraw3D,
	ODFC_EXP_OnDraw2D,
	ODFC_EXP_OnFilterSound,
	ODFC_EXP_OnDemoSeek,
	ODFC_EXP_CommandHandler,
	ODFC_NUM_EXPORTS
};
static const char *const odfc_exportNames[] = { "OnInit", "OnShutdown", "OnMapLoad", "OnCgameShutdown", "OnDraw3D", "OnDraw2D", "OnFilterSound", "OnDemoSeek", "CommandHandler" };
// capability an export needs to be called (0 = none)
static const unsigned odfc_exportCaps[] = { 0, 0, 0, 0, 0, 0, ODFC_CAP_sound, 0, 0 };

static qboolean ODFC_Call_OnInit( pluginVM_t *w, int *result )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnInit ) ) {
		*result = 0;
		return qtrue;
	}
	PluginVM_IOBegin( w );
	return PluginVM_Call( w, ODFC_EXP_OnInit, args, 0, result, PLUGIN_VM_BUDGET_INIT );
}

static qboolean ODFC_Call_OnShutdown( pluginVM_t *w )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnShutdown ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	return PluginVM_Call( w, ODFC_EXP_OnShutdown, args, 0, NULL, PLUGIN_VM_BUDGET_INIT );
}

static qboolean ODFC_Call_OnMapLoad( pluginVM_t *w, const char *a_mapName )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnMapLoad ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = PluginVM_IOPutString( w, a_mapName );
	if ( !o ) {
		PluginVM_Fail( w, "the arguments of OnMapLoad do not fit the io buffer" );
		return qfalse;
	}
	args[0] = (int)o;
	return PluginVM_Call( w, ODFC_EXP_OnMapLoad, args, 1, NULL, PLUGIN_VM_BUDGET_INIT );
}

static qboolean ODFC_Call_OnCgameShutdown( pluginVM_t *w )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnCgameShutdown ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	return PluginVM_Call( w, ODFC_EXP_OnCgameShutdown, args, 0, NULL, PLUGIN_VM_BUDGET_INIT );
}

static qboolean ODFC_Call_OnDraw3D( pluginVM_t *w, const odfcView_t *a_view )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnDraw3D ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = PluginVM_IOPut( w, a_view, sizeof( odfcView_t ), ODF_ALIGNOF( odfcView_t ) < 4 ? ODF_ALIGNOF( odfcView_t ) : 4 );
	if ( !o ) {
		PluginVM_Fail( w, "the arguments of OnDraw3D do not fit the io buffer" );
		return qfalse;
	}
	args[0] = (int)o;
	return PluginVM_Call( w, ODFC_EXP_OnDraw3D, args, 1, NULL, PLUGIN_VM_BUDGET_FRAME );
}

static qboolean ODFC_Call_OnDraw2D( pluginVM_t *w, int a_phase )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnDraw2D ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_phase;
	args[0] = (int)o;
	return PluginVM_Call( w, ODFC_EXP_OnDraw2D, args, 1, NULL, PLUGIN_VM_BUDGET_FRAME );
}

static qboolean ODFC_Call_OnFilterSound( pluginVM_t *w, int a_entityNum, int a_isLoop, int a_entityClientNum, int a_ownClientNum, int *result )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnFilterSound ) ) {
		*result = 0;
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_entityNum;
	args[0] = (int)o;
	o = (unsigned)a_isLoop;
	args[1] = (int)o;
	o = (unsigned)a_entityClientNum;
	args[2] = (int)o;
	o = (unsigned)a_ownClientNum;
	args[3] = (int)o;
	return PluginVM_Call( w, ODFC_EXP_OnFilterSound, args, 4, result, PLUGIN_VM_BUDGET_FRAME );
}

static qboolean ODFC_Call_OnDemoSeek( pluginVM_t *w, int a_fromTime, int a_toTime )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_OnDemoSeek ) ) {
		return qtrue;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_fromTime;
	args[0] = (int)o;
	o = (unsigned)a_toTime;
	args[1] = (int)o;
	return PluginVM_Call( w, ODFC_EXP_OnDemoSeek, args, 2, NULL, PLUGIN_VM_BUDGET_FRAME );
}

static qboolean ODFC_Call_CommandHandler( pluginVM_t *w, int a_handler, int a_argc, const char *const *a_argv )
{
	int args[8];
	unsigned o;
	(void)o;
	if ( !PluginVM_Has( w, ODFC_EXP_CommandHandler ) ) {
		return qtrue;
	}
	if ( a_argc > 16 ) {
		a_argc = 16;		// the most strings that are handed over
	}
	if ( a_argc < 0 ) {
		a_argc = 0;
	}
	PluginVM_IOBegin( w );
	o = (unsigned)a_handler;
	args[0] = (int)o;
	o = (unsigned)a_argc;
	args[1] = (int)o;
	{
		unsigned offsets[16];
		int k;
		for ( k = 0; k < a_argc; k++ ) {
			offsets[k] = PluginVM_IOPutString( w, a_argv[k] );
			if ( !offsets[k] ) {
				PluginVM_Fail( w, "the arguments of CommandHandler do not fit the io buffer" );
				return qfalse;
			}
		}
		o = PluginVM_IOPut( w, offsets, sizeof( offsets[0] ) * ( a_argc ? a_argc : 1 ), 4 );
	}
	if ( !o ) {
		PluginVM_Fail( w, "the arguments of CommandHandler do not fit the io buffer" );
		return qfalse;
	}
	args[2] = (int)o;
	return PluginVM_Call( w, ODFC_EXP_CommandHandler, args, 3, NULL, PLUGIN_VM_BUDGET_FRAME );
}
