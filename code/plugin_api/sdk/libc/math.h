/* There is no floating point library: sin, cos, atan2 and sqrt are done by the engine, the rest is built from them. Angles are
in radians. A double is a float in this compiler, so the functions with and without f are the same. Missing: pow, exp, log. */

#ifndef ODF_MATH_H
#define ODF_MATH_H

#define M_PI		3.14159265358979323846f
#define M_PI_2		1.57079632679489661923f
#define M_E			2.71828182845904523536f

#define isnan( x )	( ( x ) != ( x ) )

float sinf( float x );
float cosf( float x );
float tanf( float x );
float atan2f( float y, float x );
float atanf( float x );
float asinf( float x );
float acosf( float x );
float sqrtf( float x );
float fabsf( float x );
float floorf( float x );
float ceilf( float x );
float truncf( float x );
float roundf( float x );
float fmodf( float x, float y );
long lroundf( float x );

double sin( double x );
double cos( double x );
double tan( double x );
double atan2( double y, double x );
double atan( double x );
double asin( double x );
double acos( double x );
double sqrt( double x );
double fabs( double x );
double floor( double x );
double ceil( double x );
double trunc( double x );
double round( double x );
double fmod( double x, double y );
long lround( double x );

#endif
