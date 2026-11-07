/* 2D drawing helpers for client plugins (see odf_draw.h). odf-build adds this file to a program that includes odf_draw.h. */

#include <stdlib.h>
#include <string.h>

#include "odfc.h"
#include "odf_draw.h"

static int		draw_white, draw_font;
static float	draw_yScale = 1.0f;		/* from square units to the 640x480 of the engine */
static float	draw_height = 480.0f;

/* the colours of ^0 to ^7 */
static const float draw_codes[8][3] = {
	{ 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 }, { 0, 0, 1 }, { 0, 1, 1 }, { 1, 0, 1 }, { 1, 1, 1 }
};

void Draw_MapLoad( void )
{
	draw_white = RegisterShader( "white" );
	draw_font = RegisterShader( "gfx/2d/bigchars" );
}

void Draw_Begin( int screenWidth, int screenHeight )
{
	if ( screenWidth <= 0 || screenHeight <= 0 ) {
		screenWidth = 640;
		screenHeight = 480;
	}
	draw_height = 640.0f * (float)screenHeight / (float)screenWidth;
	draw_yScale = 480.0f / draw_height;
}

float Draw_Height( void )
{
	return draw_height;
}

void Draw_Rect( float x, float y, float w, float h, const float *rgba )
{
	if ( !( w > 0 ) || !( h > 0 ) ) {
		return;
	}
	SetColor( rgba );
	DrawStretchPic( x, y * draw_yScale, w, h * draw_yScale, 0, 0, 0, 0, draw_white );
}

void Draw_Outline( float x, float y, float w, float h, float t, const float *rgba )
{
	if ( !( t > 0 ) || w < 2 * t || h < 2 * t ) {
		Draw_Rect( x, y, w, h, rgba );
		return;
	}
	Draw_Rect( x, y, w, t, rgba );
	Draw_Rect( x, y + h - t, w, t, rgba );
	Draw_Rect( x, y + t, t, h - 2 * t, rgba );
	Draw_Rect( x + w - t, y + t, t, h - 2 * t, rgba );
}

static int IsColorCode( const char *s )
{
	return s[0] == '^' && s[1] && s[1] != '^';
}

float Draw_TextWidth( const char *str, float size )
{
	int n = 0;

	if ( !str ) {
		return 0;
	}
	while ( *str ) {
		if ( IsColorCode( str ) ) {
			str += 2;
			continue;
		}
		if ( str[0] == '^' && str[1] == '^' ) {
			str++;
		}
		n++;
		str++;
	}
	return n * size;
}

static void DrawRun( float x, float y, float size, const char *str, const float *rgba )
{
	float c[4];

	c[0] = rgba[0];
	c[1] = rgba[1];
	c[2] = rgba[2];
	c[3] = rgba[3];
	SetColor( c );
	while ( *str ) {
		int ch;

		if ( IsColorCode( str ) ) {
			const int code = str[1] & 7;

			c[0] = draw_codes[code][0];
			c[1] = draw_codes[code][1];
			c[2] = draw_codes[code][2];
			SetColor( c );
			str += 2;
			continue;
		}
		if ( str[0] == '^' && str[1] == '^' ) {
			str++;
		}
		ch = (unsigned char)*str++;
		if ( ch != ' ' && draw_font ) {
			const float row = 0.0625f * (float)( ch >> 4 ), col = 0.0625f * (float)( ch & 15 );

			DrawStretchPic( x, y * draw_yScale, size, size * draw_yScale, col, row, col + 0.0625f, row + 0.0625f, draw_font );
		}
		x += size;
	}
}

void Draw_Text( float x, float y, float size, const char *str, const float *rgba, int flags )
{
	static const float black[4] = { 0, 0, 0, 1 };
	float w;

	if ( !str || !rgba ) {
		return;
	}
	w = Draw_TextWidth( str, size );
	if ( flags & DRAW_ALIGN_RIGHT ) {
		x -= w;
	} else if ( flags & DRAW_ALIGN_CENTER ) {
		x -= w * 0.5f;
	}
	if ( flags & DRAW_SHADOW ) {
		float shadow[4];

		shadow[0] = black[0];
		shadow[1] = black[1];
		shadow[2] = black[2];
		shadow[3] = rgba[3];
		/* the shadow ignores colour codes: it is all black */
		{
			char plain[256];
			int n = 0;
			const char *s = str;

			while ( *s && n < (int)sizeof( plain ) - 1 ) {
				if ( IsColorCode( s ) ) {
					s += 2;
					continue;
				}
				if ( s[0] == '^' && s[1] == '^' && n < (int)sizeof( plain ) - 2 ) {
					plain[n++] = *s++;		/* keep ^^: DrawRun turns it into one ^ */
				}
				plain[n++] = *s++;
			}
			plain[n] = '\0';
			DrawRun( x + 1, y + 1, size, plain, shadow );
		}
	}
	DrawRun( x, y, size, str, rgba );
}

int Draw_ParseFloats( const char *s, float *out, int count )
{
	int n = 0;

	if ( !s ) {
		return 0;
	}
	while ( n < count ) {
		char *end;
		float v;

		while ( *s == ' ' || *s == '\t' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		v = strtof( s, &end );
		if ( end == s ) {
			break;
		}
		out[n++] = v;
		s = end;
	}
	return n;
}
