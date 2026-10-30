#ifndef TEST_STUBS_H
#define TEST_STUBS_H

#define MAX_TEST_PRINTS	64

extern char testPrints[MAX_TEST_PRINTS][1024];
extern int numTestPrints;

// what reached the (fake) renderer and the collision code
extern int stPolyCalls, stPolyVerts;
extern int stBeamCalls, stBeamShader;
extern float stBeamFrom[3], stBeamTo[3];
extern unsigned char stBeamRGBA[4];
extern int stSetColorCalls, stSetColorLastNull;
extern int stStretchCalls;
extern float stStretchLast[4];		// x y w h as the renderer got them
extern int stModelCalls, stModelHandle, stClearScenes, stRenderScenes;
extern int stRenderRect[4], stRenderFlags;
extern int stSoundCalls;
extern int numAddedCommands;
extern char addedCommands[8][32];
extern xcommand_t addedCommandFn[8];
extern char stCmdArgs[8][64];
extern int stCmdArgc;
extern int stCvarFlagsLast;
extern int stTraceCalls;
extern int stTraceBoxCalls;		// traces with a box (mins/maxs not NULL)

void Test_SetCvar( const char *name, const char *value, int flags );

#endif
