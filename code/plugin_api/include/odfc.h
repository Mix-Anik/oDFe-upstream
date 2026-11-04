/* Generated from client.api by gen_api.py - do not edit. */

/*
===========================================================================
oDFe client plugin API, version 1.

See odf_common.h for how a plugin is built. The functions below are what the plugin can
call (engine functions) and what it can implement for the engine to call (exports).
===========================================================================
*/

#ifndef ODFC_H
#define ODFC_H

#include "odf_common.h"
#include "odfc_types.h"

#define ODFC_API_VERSION	1

// Capabilities. A plugin that needs a group says so in its manifest (caps=a,b); it is only loaded when it was given
// them, and a call of a function of a group it does not have stops the plugin.
//   world      what the player's own game shows: snapshots, predicted state, map traces
//   draw       adding to the 3D scene and drawing on the HUD
//   command    console commands
//   sound      being asked about every sound the game starts
// A function marked 'cheats only' does nothing and returns 0 unless cheats are allowed (see CheatsEnabled): the engine
// decides, not the plugin.

// ---------------------------------------------------------------------------
// Handlers: functions of the plugin that it gives to the engine (as an argument of one of the functions below).
// The engine calls them with exactly these arguments. A handler is an ordinary function with this prototype.
// ---------------------------------------------------------------------------

// The function of the plugin that runs a console command, like main: argv[0] is the name as it was typed, the others are the
// arguments, argc counts all of them. At most 16 are passed (the rest is dropped) and each is cut after 199 characters.
typedef void ( *odfcCommandHandler_t )( int argc, const char **argv );


// Writes a line to the console, prefixed with the plugin name.
void Print( const char *text );

// Milliseconds since the engine started (wall clock, not game time).
int Milliseconds( void );

// Creates the cvar if it does not exist. An existing value (config file, command line) is kept. The plugin can read the
// cvars it created and a few game settings (cg_*, r_*, df_*, m_* and so on), nothing else.
void CvarRegister( const char *name, const char *defaultValue, int flags );

void CvarString( const char *name, char *out, int outSize );

int CvarInteger( const char *name );

float CvarValue( const char *name );

// Whether cheats are allowed: playing a demo, or connected to a server with sv_cheats 1 ("devmap"). The engine decides.
int CheatsEnabled( void );

// Math. Angles are in radians.
float Sin( float x );

float Cos( float x );

float Atan2( float y, float x );

float Sqrt( float x );

// Fills size bytes at dst with the byte value. Copies size bytes from src to dst (they must not overlap).
void MemSet( void *dst, int value, int size );

void MemCpy( void *dst, const void *src, int size );

// Fills *out with the snapshot the game is currently showing (the newest one that is not ahead of the view time). Returns 0
// when there is none (not in a game). Without cheats it only lists the missiles of the watched player.
// Capability: world
int GetSnapshot( odfcSnapshot_t *out );

// Traces a line (or a box, if mins and maxs are not NULL) through the static map. Doors, platforms and players are not part of
// it. contentMask is a set of ODFC_CONTENTS_* flags, ODFC_MASK_SHOT is what missiles collide with.
// Capability: world
void Trace( odfcTrace_t *out, const float *start, const float *mins, const float *maxs, const float *end, int contentMask );

// The predicted state of the player being watched, for the frame that is being drawn. Returns 0 when the game module does not
// publish it (only the oDFs game module does), or outside of a game.
// Capability: world
int GetPlayerState( odfcPlayerState_t *out );

// What the movement of the watched player depends on besides the state above. Returns 0 under the same conditions.
// Capability: world
int GetMoveState( odfcMoveState_t *out );

// The contents of the static map at a point (ODFC_CONTENTS_*): 0 in the air, ODFC_MASK_WATER bits in water.
// Capability: world
int PointContents( const float *point );

// The weapons and ammunition of the watched player. Returns 0 under the same conditions as GetPlayerState.
// Capability: world
int GetWeaponState( odfcWeaponState_t *out );

// Returns a shader handle (0 is the default shader, which is also what a missing shader gives). Valid until OnCgameShutdown.
// Only works from OnMapLoad on.
// Capability: draw
int RegisterShader( const char *name );

// Like RegisterShader for pictures of the HUD: not reduced by r_picmip, no mipmaps.
// Capability: draw
int RegisterShaderNoMip( const char *name );

// Returns a model handle (0 when there is no such model). Same rules as RegisterShader.
// Capability: draw
int RegisterModel( const char *name );

// Adds a polygon (3 to 64 vertices, convex) to the 3D scene. Only in OnDraw3D.
// Capability: draw
void AddPoly( int shader, int numVerts, const odfcPolyVert_t *verts );

// Adds a line that looks like a rail gun beam core between two points, coloured with rgba (4 bytes), to the 3D scene. Only in
// OnDraw3D. Use a shader like "railCore".
// Capability: draw
void AddBeam( int shader, const float *from, const float *to, const unsigned char *rgba );

// 2D drawing in the 640x480 virtual screen, only in OnDraw2D. SetColor takes 4 floats or NULL for white. The engine resets the
// colour after OnDraw2D returns.
// Capability: draw
void SetColor( const float *rgba );

// Capability: draw
void DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader );

// Draws a model into the rectangle of the 640x480 screen, lit and turned like the models of the game's own HUD (field of view
// 30, the model at origin, in front of the viewer, turned by angles). Only in OnDraw2D. time is the animation time.
// Capability: draw
void DrawModel( float x, float y, float w, float h, int model, const float *origin, const float *angles, int time );

// Adds a console command (lower case letters, digits and _, up to 31 long) that calls handler when it is entered. Returns 0 when
// the name is invalid, handler is NULL or the name is already used by the engine or another plugin. Commands are removed when
// the plugin stops.
// Capability: command
int CommandRegister( const char *name, odfcCommandHandler_t handler );


// ---------------------------------------------------------------------------
// Exports: implement the ones you need with exactly these prototypes and list them in the EXPORTS
// of the entry point (see odfc_entry.h).
// ---------------------------------------------------------------------------

// Called once after loading. Return 0 on success; anything else unloads the plugin (OnShutdown is not called then).
int OnInit( void );

// Called once when the client shuts down or the plugin is switched off.
void OnShutdown( void );

// Called when a map is loaded by the game (and again after a vid_restart): register shaders and reset all state that belongs to
// the map.
void OnMapLoad( const char *mapName );

// Called when the game module is shut down (disconnect, map change, vid_restart). Shader handles are no longer valid.
void OnCgameShutdown( void );

// Called once per frame before the 3D scene is rendered. Add polygons and beams here.
void OnDraw3D( const odfcView_t *view );

// Called twice per frame, with an ODFC_DRAW2D_* phase.
void OnDraw2D( int phase );

// Called for every sound the game module starts on an entity or loops on one. Return non-zero to drop the sound. entityNum is
// the entity (1023 is the world), isLoop whether it is a looping sound, entityClientNum the clientNum field of that entity in the
// snapshots seen so far (-1 if it has not been in one) and ownClientNum the watched player.
// Capability: sound
int OnFilterSound( int entityNum, int isLoop, int entityClientNum, int ownClientNum );

// The position of the demo that is playing was changed (seek, rewind, scrub). fromTime and toTime are server times in ms. Time can
// go backwards: drop what you kept about the frames after toTime. Not called outside demos.
void OnDemoSeek( int fromTime, int toTime );

#endif // ODFC_H
