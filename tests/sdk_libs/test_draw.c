/* Tests odf_draw (code/plugin_api/sdk/odf_draw.c) natively against a made-up engine that records what was drawn. */

#include <stdio.h>
#include <string.h>

#include "odfc.h"
#include "odf_draw.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

/* ---- the made-up engine ---- */
typedef struct {
	float x, y, w, h, s1, t1, s2, t2;
	int shader;
	float color[4];
} pic_t;

static pic_t pics[256];
static int numPics;
static float curColor[4] = { 1, 1, 1, 1 };

int RegisterShader( const char *name )
{
	if ( !strcmp( name, "white" ) ) {
		return 5;
	}
	if ( !strcmp( name, "gfx/2d/bigchars" ) ) {
		return 6;
	}
	return 0;
}

void SetColor( const float *rgba )
{
	int i;

	for ( i = 0; i < 4; i++ ) {
		curColor[i] = rgba ? rgba[i] : 1.0f;
	}
}

void DrawStretchPic( float x, float y, float w, float h, float s1, float t1, float s2, float t2, int shader )
{
	pic_t *p;

	if ( numPics >= 256 ) {
		return;
	}
	p = &pics[numPics++];
	p->x = x; p->y = y; p->w = w; p->h = h; p->s1 = s1; p->t1 = t1; p->s2 = s2; p->t2 = t2; p->shader = shader;
	memcpy( p->color, curColor, sizeof( curColor ) );
}

static int Near( float a, float b ) { return a - b < 0.001f && b - a < 0.001f; }

int main( void )
{
	static const float white[4] = { 1, 1, 1, 0.5f };
	float v[4] = { 9, 9, 9, 9 };

	Draw_MapLoad();
	Draw_Begin( 1920, 1080 );
	CHECK( Near( Draw_Height(), 360 ), "height at 16:9: %g", Draw_Height() );
	Draw_Begin( 0, 0 );
	CHECK( Near( Draw_Height(), 480 ), "height without a size: %g", Draw_Height() );
	Draw_Begin( 1920, 1080 );

	/* a rectangle: square units become 640x480 */
	numPics = 0;
	Draw_Rect( 10, 90, 100, 36, white );
	CHECK( numPics == 1 && pics[0].shader == 5 && Near( pics[0].x, 10 ) && Near( pics[0].y, 120 ) && Near( pics[0].w, 100 ) && Near( pics[0].h, 48 ),
		"rect: %d pics, y %g h %g", numPics, pics[0].y, pics[0].h );
	CHECK( Near( pics[0].color[3], 0.5f ), "rect colour" );
	numPics = 0;
	Draw_Rect( 0, 0, 0, 10, white );
	Draw_Rect( 0, 0, 10, -1, white );
	CHECK( numPics == 0, "an empty rectangle was drawn" );

	/* an outline: four rectangles that do not overlap */
	numPics = 0;
	Draw_Outline( 0, 0, 20, 10, 2, white );
	CHECK( numPics == 4, "outline: %d rectangles", numPics );
	numPics = 0;
	Draw_Outline( 0, 0, 3, 3, 2, white );
	CHECK( numPics == 1, "a box too small for its border is filled: %d", numPics );

	/* text: one picture per character, the font is 16x16 */
	numPics = 0;
	Draw_Text( 4, 8, 8, "AB C", white, 0 );
	CHECK( numPics == 3, "text pictures %d (a space is not drawn)", numPics );
	CHECK( pics[0].shader == 6 && Near( pics[0].s1, 0.0625f * ( 'A' & 15 ) ) && Near( pics[0].t1, 0.0625f * ( 'A' >> 4 ) ) && Near( pics[0].s2 - pics[0].s1, 0.0625f ),
		"glyph A: s %g t %g", pics[0].s1, pics[0].t1 );
	CHECK( Near( pics[1].x, 12 ) && Near( pics[2].x, 28 ), "advance: %g %g", pics[1].x, pics[2].x );
	CHECK( Near( pics[0].w, 8 ) && Near( pics[0].h, 8 * 480.0f / 360.0f ), "glyph size %g x %g", pics[0].w, pics[0].h );
	CHECK( Near( Draw_TextWidth( "AB C", 8 ), 32 ), "width %g", Draw_TextWidth( "AB C", 8 ) );

	/* colour codes: not drawn, not counted, change the colour but not the alpha; ^^ is a ^ */
	CHECK( Near( Draw_TextWidth( "^1ab^7c", 10 ), 30 ), "width with codes %g", Draw_TextWidth( "^1ab^7c", 10 ) );
	CHECK( Near( Draw_TextWidth( "a^^b", 10 ), 30 ), "width of ^^ %g", Draw_TextWidth( "a^^b", 10 ) );
	numPics = 0;
	Draw_Text( 0, 0, 8, "^1a^2b", white, 0 );
	CHECK( numPics == 2 && Near( pics[0].color[0], 1 ) && Near( pics[0].color[1], 0 ) && Near( pics[1].color[0], 0 ) && Near( pics[1].color[1], 1 )
		&& Near( pics[0].color[3], 0.5f ) && Near( pics[1].color[3], 0.5f ), "colour codes" );

	/* alignment and shadow */
	numPics = 0;
	Draw_Text( 100, 0, 8, "abcd", white, DRAW_ALIGN_RIGHT );
	CHECK( Near( pics[0].x, 68 ), "right aligned starts at %g", pics[0].x );
	numPics = 0;
	Draw_Text( 100, 0, 8, "abcd", white, DRAW_ALIGN_CENTER );
	CHECK( Near( pics[0].x, 84 ), "centred starts at %g", pics[0].x );
	numPics = 0;
	Draw_Text( 0, 0, 8, "^1ab", white, DRAW_SHADOW );
	CHECK( numPics == 4 && Near( pics[0].color[0], 0 ) && Near( pics[0].x, 1 ) && Near( pics[2].color[0], 1 ) && Near( pics[2].x, 0 ),
		"shadow: %d pictures, first colour %g x %g", numPics, pics[0].color[0], pics[0].x );
	numPics = 0;
	Draw_Text( 0, 0, 8, NULL, white, 0 );
	Draw_Text( 0, 0, 8, "a", NULL, 0 );
	CHECK( numPics == 0, "text without a string or colour" );

	/* numbers */
	CHECK( Draw_ParseFloats( "420 32 1.5", v, 3 ) == 3 && Near( v[0], 420 ) && Near( v[1], 32 ) && Near( v[2], 1.5f ), "three numbers" );
	v[0] = v[1] = v[2] = v[3] = 9;
	CHECK( Draw_ParseFloats( "  0.25\t-3 ", v, 4 ) == 2 && Near( v[0], 0.25f ) && Near( v[1], -3 ) && Near( v[2], 9 ), "two numbers, the rest stays" );
	CHECK( Draw_ParseFloats( "abc", v, 4 ) == 0 && Near( v[0], 0.25f ), "no number" );
	CHECK( Draw_ParseFloats( "1 2 3 4 5", v, 2 ) == 2, "count limit" );
	CHECK( Draw_ParseFloats( NULL, v, 2 ) == 0 && Draw_ParseFloats( "", v, 2 ) == 0, "empty input" );

	if ( failures ) {
		printf( "%d FAILED\n", failures );
		return 1;
	}
	printf( "draw tests passed\n" );
	return 0;
}
