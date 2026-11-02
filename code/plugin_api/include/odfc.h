/* Generated from client.api by gen_api.py - do not edit. */

/*
===========================================================================
oDFe client plugin API, version 1.

See odf_common.h for how a plugin is built. The functions below are what the plugin can
call (imports) and what it can implement for the engine to call (exports).
===========================================================================
*/

#ifndef ODFC_H
#define ODFC_H

#include <stdint.h>
#include "odf_common.h"
#include "odfc_types.h"

#define ODFC_API_VERSION	1

// Capabilities. A plugin that imports a function of a group, or lists the group in its manifest (caps=a,b),
// is only loaded when it was given the capability.
//   world      what the player's own game shows: snapshots, predicted state, map traces
//   draw       adding to the 3D scene and drawing on the HUD
//   command    console commands
//   sound      being asked about every sound the game starts


// Writes a line to the console, prefixed with the plugin name.
ODF_IMPORT( "odfc", "Print" ) void odfc_Print( const char *text );

// Milliseconds since the engine started (wall clock, not game time).
ODF_IMPORT( "odfc", "Milliseconds" ) int odfc_Milliseconds( void );

// Creates the cvar if it does not exist. An existing value (config file, command line) is kept. The plugin can read the
// cvars it created and a few game settings (cg_*, r_*, df_*, m_* and so on), nothing else.
ODF_IMPORT( "odfc", "CvarRegister" ) void odfc_CvarRegister( const char *name, const char *defaultValue, int flags );

ODF_IMPORT( "odfc", "CvarString" ) void odfc_CvarString( const char *name, char *out, int outSize );

ODF_IMPORT( "odfc", "CvarInteger" ) int odfc_CvarInteger( const char *name );

ODF_IMPORT( "odfc", "CvarValue" ) float odfc_CvarValue( const char *name );

// Whether cheats are allowed: playing a demo, or connected to a server with sv_cheats 1 ("devmap"). The engine decides.
ODF_IMPORT( "odfc", "CheatsEnabled" ) int odfc_CheatsEnabled( void );

// Fills *out with the snapshot the game is currently showing (the newest one that is not ahead of the view time). Returns 0
// when there is none (not in a game). Without cheats it only lists the missiles of the watched player.
// Capability: world
ODF_IMPORT( "odfc", "GetSnapshot" ) int odfc_GetSnapshot( odfcSnapshot_t *out );

// Traces a line (or a box, if mins and maxs are not NULL) through the static map. Doors, platforms and players are not part of
// it. contentMask is a set of ODFC_CONTENTS_* flags, ODFC_MASK_SHOT is what missiles collide with.
// Capability: world
ODF_IMPORT( "odfc", "Trace" ) void odfc_Trace( odfcTrace_t *out, const float *start, const float *mins, const float *maxs, const float *end, int contentMask );

// The predicted state of the player being watched, for the frame that is being drawn. Returns 0 when the game module does not
// publish it (only the oDFs game module does), or outside of a game.
// Capability: world
ODF_IMPORT( "odfc", "GetPlayerState" ) int odfc_GetPlayerState( odfcPlayerState_t *out );

// What the movement of the watched player depends on besides the state above. Returns 0 under the same conditions.
// Capability: world
ODF_IMPORT( "odfc", "GetMoveState" ) int odfc_GetMoveState( odfcMoveState_t *out );

// The contents of the static map at a point (ODFC_CONTENTS_*): 0 in the air, ODFC_MASK_WATER bits in water.
// Capability: world
ODF_IMPORT( "odfc", "PointContents" ) int odfc_PointContents( const float *point );

// The weapons and ammunition of the watched player. Returns 0 under the same conditions as GetPlayerState.
// Capability: world
ODF_IMPORT( "odfc", "GetWeaponState" ) int odfc_GetWeaponState( odfcWeaponState_t *out );

// Returns a shader handle (0 is the default shader, which is also what a missing shader gives). Valid until cgame_shutdown.
// Only works from map_load on.
// Capability: draw
ODF_IMPORT( "odfc", "RegisterShader" ) int odfc_RegisterShader( const char *name );

// Like RegisterShader for pictures of the HUD: not reduced by r_picmip, no mipmaps.
// Capability: draw
ODF_IMPORT( "odfc", "RegisterShaderNoMip" ) int odfc_RegisterShaderNoMip( const char *name );

// Returns a model handle (0 when there is no such model). Same rules as RegisterShader.
// Capability: draw
ODF_IMPORT( "odfc", "RegisterModel" ) int odfc_RegisterModel( const char *name );

// Adds a polygon (3 to 64 vertices, convex) to the 3D scene. Only in draw3d.
// Capability: draw
ODF_IMPORT( "odfc", "AddPoly" ) void odfc_AddPoly( int shader, int numVerts, const odfcPolyVert_t *verts );

// Adds a line that looks like a rail gun beam core between two points, coloured with rgba (4 bytes), to the 3D scene. Only in
// draw3d. Use a shader like "railCore".
// Capability: draw
ODF_IMPORT( "odfc", "AddBeam" ) void odfc_AddBeam( int shader, const float *from, const float *to, const unsigned char *rgba );

// 2D drawing in the 640x480 virtual screen, only in draw2d. SetColor takes 4 floats or NULL for white. The engine resets the
// colour after draw2d returns.
// Capability: draw
ODF_IMPORT( "odfc", "SetColor" ) void odfc_SetColor( const float *rgba );

// Capability: draw
ODF_IMPORT( "odfc", "DrawStretchPic" ) void odfc_DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader );

// Draws a model into the rectangle of the 640x480 screen, lit and turned like the models of the game's own HUD (field of view
// 30, the model at origin, in front of the viewer, turned by angles). Only in draw2d. time is the animation time.
// Capability: draw
ODF_IMPORT( "odfc", "DrawModel" ) void odfc_DrawModel( float x, float y, float w, float h, int model, const float *origin, const float *angles, int time );

// Adds a console command (lower case letters, digits and _, up to 31 long) that calls the plugin's command export. Returns 0
// when the name is invalid or already used by the engine or another plugin. Commands are removed when the plugin stops.
// Capability: command
ODF_IMPORT( "odfc", "CommandRegister" ) int odfc_CommandRegister( const char *name );

// The arguments of the command being run (in command): the count, and argument n (0 is the name) copied to out.
// Capability: command
ODF_IMPORT( "odfc", "CommandArgc" ) int odfc_CommandArgc( void );

// Capability: command
ODF_IMPORT( "odfc", "CommandArgv" ) void odfc_CommandArgv( int n, char *out, int outSize );


// ---------------------------------------------------------------------------
// Exports: define the ones you need with exactly these prototypes.
// ---------------------------------------------------------------------------

// Called once after loading. Return 0 on success; anything else unloads the plugin (shutdown is not called then).
ODF_EXPORT_FN( "init" ) int odfc_init( void );

// Called once when the client shuts down or the plugin is switched off.
ODF_EXPORT_FN( "shutdown" ) void odfc_shutdown( void );

// Called when a map is loaded by the game (and again after a vid_restart): register shaders and reset all state that belongs to
// the map.
ODF_EXPORT_FN( "map_load" ) void odfc_map_load( const char *mapName );

// Called when the game module is shut down (disconnect, map change, vid_restart). Shader handles are no longer valid.
ODF_EXPORT_FN( "cgame_shutdown" ) void odfc_cgame_shutdown( void );

// Called once per frame before the 3D scene is rendered. Add polygons and beams here.
ODF_EXPORT_FN( "draw3d" ) void odfc_draw3d( const odfcView_t *view );

// Called twice per frame, with an ODFC_DRAW2D_* phase.
ODF_EXPORT_FN( "draw2d" ) void odfc_draw2d( int phase );

// A command registered with CommandRegister was entered in the console; the arguments are available through CommandArgc and
// CommandArgv.
ODF_EXPORT_FN( "command" ) void odfc_command( const char *name );

// Called for every sound the game module starts on an entity or loops on one. Return non-zero to drop the sound. entityNum is
// the entity (1023 is the world), isLoop whether it is a looping sound, entityClientNum the clientNum field of that entity in the
// snapshots seen so far (-1 if it has not been in one) and ownClientNum the watched player.
// Capability: sound
ODF_EXPORT_FN( "filter_sound" ) int odfc_filter_sound( int entityNum, int isLoop, int entityClientNum, int ownClientNum );

// The position of the demo that is playing was changed (seek, rewind, scrub). fromTime and toTime are server times in ms. Time can
// go backwards: drop what you kept about the frames after toTime. Not called outside demos.
ODF_EXPORT_FN( "demo_seek" ) void odfc_demo_seek( int fromTime, int toTime );

#endif // ODFC_H
