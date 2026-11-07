/* A client plugin that draws the horizontal speed of the watched player under the crosshair.
   It shows: cvars of a plugin, the state of the game (GetPlayerState, GetMoveState) and the 2D drawing helpers (odf_draw.h).
   Build:  odf-build --client -o speedometer.qvm speedometer.c
   Cvars:  speedo_draw (0 or 1), speedo_y (the height of the number, in the 640 x 360 to 480 units of the screen) */
#define ODF_PLUGIN_VERSION     "1.0"
#define ODF_PLUGIN_CAPS        "world,draw"
#define ODF_PLUGIN_DESCRIPTION "draws the speed under the crosshair"
#define EXPORTS( X ) X(OnInit) X(OnMapLoad) X(OnDraw3D) X(OnDraw2D)
#include "odfc_entry.h"
#include "odf_draw.h"            /* odf-build adds odf_draw.c to the program because of this */

#include <stdio.h>

static const float white[4] = { 1, 1, 1, 1 };

/* OnDraw2D is not told the size of the screen, the view of OnDraw3D is: keep it */
static int screenWidth, screenHeight;

int OnInit( void )
{
	CvarRegister( "speedo_draw", "1", ODFC_CVAR_ARCHIVE );
	CvarRegister( "speedo_y", "250", ODFC_CVAR_ARCHIVE );
	return 0;
}

/* shaders (the font of Draw_Text) can only be registered from OnMapLoad on */
void OnMapLoad( const char *mapName )
{
	Draw_MapLoad();
}

void OnDraw3D( const odfcView_t *view )
{
	screenWidth = view->screenWidth;
	screenHeight = view->screenHeight;
}

void OnDraw2D( int phase )
{
	odfcPlayerState_t ps;
	odfcMoveState_t ms;
	char text[16];
	float speed;

	if ( phase != ODFC_DRAW2D_FRAME || !CvarInteger( "speedo_draw" ) ) {
		return;                  /* FRAME: after the game drew its HUD, so the number is on top of it */
	}
	/* 0: not in a game, or a game module that does not publish its state (only the oDFs one does) */
	if ( !GetPlayerState( &ps ) || !GetMoveState( &ms ) ) {
		return;
	}
	if ( ms.viewFlags & ( ODFC_VIEW_SCOREBOARD | ODFC_VIEW_NOT_FIRST_PERSON ) ) {
		return;
	}

	speed = Sqrt( ps.velocity[0] * ps.velocity[0] + ps.velocity[1] * ps.velocity[1] );
	snprintf( text, sizeof( text ), "%d", (int)( speed + 0.5f ) );

	Draw_Begin( screenWidth, screenHeight );
	Draw_Text( 320, CvarValue( "speedo_y" ), 16, text, white, DRAW_ALIGN_CENTER | DRAW_SHADOW );
}
