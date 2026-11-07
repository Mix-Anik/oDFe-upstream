/*
2D drawing helpers for client plugins, on top of RegisterShader / SetColor / DrawStretchPic: filled rectangles, outlines and text
in the 16x16 font of the game ("gfx/2d/bigchars") with the usual ^0-^7 colour codes.

The coordinates are square units: the screen is 640 units wide and as high as it needs to be (360 at 16:9), so a square drawn
with equal width and height is a square on the screen. Draw_Height() tells the height.

Using it: #include "odf_draw.h" in the plugin; odf-build adds odf_draw.c to the program. Call Draw_MapLoad from OnMapLoad (the
shaders are registered there) and Draw_Begin at the start of OnDraw2D, with the screen size of the last OnDraw3D view.
*/

#ifndef ODF_DRAW_H
#define ODF_DRAW_H

#ifndef ODF_MODULE_CLIENT
#error odf_draw.h is for client plugins (odfc.h)
#endif

/* Draw_Text flags */
#define DRAW_ALIGN_RIGHT	1		/* x is the right end of the text */
#define DRAW_ALIGN_CENTER	2		/* x is the middle of the text */
#define DRAW_SHADOW			4		/* a black copy one unit lower right */

void	Draw_MapLoad( void );
/* screenWidth and screenHeight in pixels (odfcView_t); 0 means 640x480 */
void	Draw_Begin( int screenWidth, int screenHeight );
float	Draw_Height( void );

void	Draw_Rect( float x, float y, float w, float h, const float *rgba );
/* the border of a rectangle, thickness inside */
void	Draw_Outline( float x, float y, float w, float h, float thickness, const float *rgba );

/* size is the height and the width of a character; ^0-^7 change the colour (alpha stays), ^^ is a ^ */
void	Draw_Text( float x, float y, float size, const char *str, const float *rgba, int flags );
float	Draw_TextWidth( const char *str, float size );

/* Up to count numbers separated by white space; the ones that are missing are left as they were. Returns how many were read.
   For cvars like "420 32 1" and "0 0 0 0.45". */
int		Draw_ParseFloats( const char *s, float *out, int count );

#endif
