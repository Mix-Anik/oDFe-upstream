/*
===========================================================================
The engine side of the functions both kinds of plugin get (common.api): math and bulk memory work. Included by cl_plugin.c and
sv_plugin.c after the generated dispatcher. The dispatcher has checked the pointers and sizes before these run.
===========================================================================
*/

#ifndef PLUGIN_COMMON_IMP_H
#define PLUGIN_COMMON_IMP_H

static float Imp_Sin( float x )
{
	return sinf( x );
}

static float Imp_Cos( float x )
{
	return cosf( x );
}

static float Imp_Atan2( float y, float x )
{
	return atan2f( y, x );
}

static float Imp_Sqrt( float x )
{
	return x > 0.0f ? sqrtf( x ) : 0.0f;
}

static void Imp_MemSet( char *dst, int value, int size )
{
	if ( dst && size > 0 ) {
		memset( dst, value, size );
	}
}

static void Imp_MemCpy( char *dst, const char *src, int size )
{
	if ( dst && src && size > 0 ) {
		memmove( dst, src, size );
	}
}

#endif // PLUGIN_COMMON_IMP_H
