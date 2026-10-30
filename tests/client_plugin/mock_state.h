#ifndef MOCK_STATE_H
#define MOCK_STATE_H

#include "odf_client_plugin_api.h"

// what the mock plugin saw and got back, read by the test program
typedef struct {
	int				crashInDraw3D;		// set by the test: the next Draw3D dereferences NULL

	int				initCalls, shutdownCalls;
	int				cvarDefault;		// CvarInteger of a cvar the mock registered with a default
	float			cvarValue;
	char			secretSeen[64];		// what CvarString returned for a private cvar
	char			visibleSeen[64];

	int				mapLoads;
	char			lastMap[64];
	int				mapLoadShader;
	int				cgameShutdowns;

	int				draw3d;
	odfcView_t		view;
	int				snapOk;
	odfcSnapshot_t	snap;
	odfcTrace_t		traceGood, traceNaN, traceNull;
	int				cheatsFlag;
	int				psOk;
	odfcPlayerState_t	ps;
	int				msOk;
	odfcMoveState_t	ms;
	int				contentsLow, contentsHigh, contentsNaN;

	int				weaponOk;
	odfcWeaponState_t	weapons;
	int				modelHandle;
	int				cmdRegistered, cmdBadName, cmdTaken, cmdSecond;
	int				commandCalls, commandArgc;
	char			commandName[32], commandArg1[32];
	int				filterCalls, filterArgs[4], filterResult;	// set filterResult for what FilterSound returns
	int				romSeen;
	int				crashInCommand;
	int				drawModelFrom3D;

	int				draw2d;
	int				phases[8];
	int				numPhases;
} mockState_t;

#endif
