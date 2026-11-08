/*
===========================================================================
The C library of oDFe plugins: the parts of string.h, ctype.h, stdlib.h, math.h and stdio.h a plugin is likely to want. A plugin
is built without the real C library (it has no heap, no files, no system calls), see README.md. Add this file as the last
source file of the program.

Written for q3lcc: C89, 32 bit integers, a double is a float. The math the VM cannot do itself (sin, cos, atan2, sqrt and big
memory copies) is done by the engine.
===========================================================================
*/

#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include "odf_api.h"

/*
===========================================================================
string.h
===========================================================================
*/

// a call of the engine costs more than a few iterations of a loop
#define BULK_SIZE	64

void *memcpy( void *dst, const void *src, size_t n )
{
	char *d = (char *)dst;
	const char *s = (const char *)src;

	if ( n >= BULK_SIZE ) {
		MemCpy( dst, src, (int)n );
		return dst;
	}
	while ( n-- ) {
		*d++ = *s++;
	}
	return dst;
}

void *memmove( void *dst, const void *src, size_t n )
{
	char *d = (char *)dst;
	const char *s = (const char *)src;

	if ( d < s ) {
		while ( n-- ) {
			*d++ = *s++;
		}
	} else {
		d += n;
		s += n;
		while ( n-- ) {
			*--d = *--s;
		}
	}
	return dst;
}

void *memset( void *dst, int value, size_t n )
{
	char *d = (char *)dst;

	if ( n >= BULK_SIZE ) {
		MemSet( dst, value, (int)n );
		return dst;
	}
	while ( n-- ) {
		*d++ = (char)value;
	}
	return dst;
}

int memcmp( const void *a, const void *b, size_t n )
{
	const unsigned char *x = (const unsigned char *)a;
	const unsigned char *y = (const unsigned char *)b;

	for ( ; n; n--, x++, y++ ) {
		if ( *x != *y ) {
			return *x < *y ? -1 : 1;
		}
	}
	return 0;
}

void *memchr( const void *s, int c, size_t n )
{
	const unsigned char *p = (const unsigned char *)s;

	for ( ; n; n--, p++ ) {
		if ( *p == (unsigned char)c ) {
			return (void *)p;
		}
	}
	return NULL;
}

size_t strlen( const char *s )
{
	const char *p = s;

	while ( *p ) {
		p++;
	}
	return (size_t)( p - s );
}

int strcmp( const char *a, const char *b )
{
	while ( *a && *a == *b ) {
		a++;
		b++;
	}
	return (unsigned char)*a - (unsigned char)*b;
}

int strncmp( const char *a, const char *b, size_t n )
{
	for ( ; n; n--, a++, b++ ) {
		if ( *a != *b ) {
			return (unsigned char)*a - (unsigned char)*b;
		}
		if ( !*a ) {
			break;
		}
	}
	return 0;
}

int strncasecmp( const char *a, const char *b, size_t n )
{
	for ( ; n; n--, a++, b++ ) {
		int x = tolower( (unsigned char)*a ), y = tolower( (unsigned char)*b );

		if ( x != y ) {
			return x - y;
		}
		if ( !x ) {
			break;
		}
	}
	return 0;
}

int strcasecmp( const char *a, const char *b )
{
	return strncasecmp( a, b, (size_t)-1 );
}

char *strcpy( char *dst, const char *src )
{
	char *d = dst;

	while ( ( *d++ = *src++ ) != 0 ) {
	}
	return dst;
}

char *strncpy( char *dst, const char *src, size_t n )
{
	char *d = dst;

	for ( ; n && *src; n-- ) {
		*d++ = *src++;
	}
	for ( ; n; n-- ) {
		*d++ = 0;
	}
	return dst;
}

char *strcat( char *dst, const char *src )
{
	strcpy( dst + strlen( dst ), src );
	return dst;
}

char *strncat( char *dst, const char *src, size_t n )
{
	char *d = dst + strlen( dst );

	for ( ; n && *src; n-- ) {
		*d++ = *src++;
	}
	*d = 0;
	return dst;
}

char *strchr( const char *s, int c )
{
	for ( ; ; s++ ) {
		if ( *s == (char)c ) {
			return (char *)s;
		}
		if ( !*s ) {
			return NULL;
		}
	}
}

char *strrchr( const char *s, int c )
{
	const char *found = NULL;

	for ( ; ; s++ ) {
		if ( *s == (char)c ) {
			found = s;
		}
		if ( !*s ) {
			return (char *)found;
		}
	}
}

char *strstr( const char *s, const char *find )
{
	size_t n = strlen( find );

	if ( !n ) {
		return (char *)s;
	}
	for ( ; *s; s++ ) {
		if ( *s == *find && !strncmp( s, find, n ) ) {
			return (char *)s;
		}
	}
	return NULL;
}

size_t strspn( const char *s, const char *set )
{
	const char *p = s;

	while ( *p && strchr( set, *p ) ) {
		p++;
	}
	return (size_t)( p - s );
}

size_t strcspn( const char *s, const char *set )
{
	const char *p = s;

	while ( *p && !strchr( set, *p ) ) {
		p++;
	}
	return (size_t)( p - s );
}

char *strpbrk( const char *s, const char *set )
{
	s += strcspn( s, set );
	return *s ? (char *)s : NULL;
}

/*
===========================================================================
ctype.h (the "C" locale)
===========================================================================
*/

int isdigit( int c ) { return c >= '0' && c <= '9'; }
int islower( int c ) { return c >= 'a' && c <= 'z'; }
int isupper( int c ) { return c >= 'A' && c <= 'Z'; }
int isalpha( int c ) { return islower( c ) || isupper( c ); }
int isalnum( int c ) { return isalpha( c ) || isdigit( c ); }
int isxdigit( int c ) { return isdigit( c ) || ( c >= 'a' && c <= 'f' ) || ( c >= 'A' && c <= 'F' ); }
int isspace( int c ) { return c == ' ' || ( c >= 9 && c <= 13 ); }
int isprint( int c ) { return c >= 32 && c < 127; }
int ispunct( int c ) { return isprint( c ) && c != ' ' && !isalnum( c ); }
int tolower( int c ) { return isupper( c ) ? c + 32 : c; }
int toupper( int c ) { return islower( c ) ? c - 32 : c; }

/*
===========================================================================
math.h
===========================================================================
*/

typedef union {
	float			f;
	unsigned int	u;
} floatBits_t;

#define TWO_23		8388608.0f		// from here on a float has no fraction

float sinf( float x ) { return Sin( x ); }
float cosf( float x ) { return Cos( x ); }
float atan2f( float y, float x ) { return Atan2( y, x ); }
float sqrtf( float x ) { return Sqrt( x ); }
float tanf( float x ) { return Sin( x ) / Cos( x ); }
float atanf( float x ) { return Atan2( x, 1.0f ); }

// outside of -1..1 the arguments are clamped (the C library returns NaN)
float asinf( float x )
{
	if ( x >= 1.0f ) {
		return M_PI_2;
	}
	if ( x <= -1.0f ) {
		return -M_PI_2;
	}
	return Atan2( x, Sqrt( 1.0f - x * x ) );
}

float acosf( float x )
{
	if ( x >= 1.0f ) {
		return 0.0f;
	}
	if ( x <= -1.0f ) {
		return M_PI;
	}
	return Atan2( Sqrt( 1.0f - x * x ), x );
}

float fabsf( float x )
{
	floatBits_t b;

	b.f = x;
	b.u &= 0x7FFFFFFFu;
	return b.f;
}

float truncf( float x )
{
	if ( !( fabsf( x ) < TWO_23 ) ) {		// big, infinite or NaN
		return x;
	}
	return (float)(int)x;
}

float floorf( float x )
{
	float t = truncf( x );

	return t > x ? t - 1.0f : t;
}

float ceilf( float x )
{
	float t = truncf( x );

	return t < x ? t + 1.0f : t;
}

// halves go away from zero
float roundf( float x )
{
	float t = truncf( x );

	if ( x - t >= 0.5f ) {
		return t + 1.0f;
	}
	if ( t - x >= 0.5f ) {
		return t - 1.0f;
	}
	return t;
}

long lroundf( float x )
{
	return (long)roundf( x );
}

// the quotient is a float, so this is only exact when it is less than 2^24
float fmodf( float x, float y )
{
	if ( y == 0.0f || !( fabsf( x ) < FLT_MAX ) ) {
		return 0.0f;
	}
	return x - truncf( x / y ) * y;
}

double sin( double x ) { return sinf( x ); }
double cos( double x ) { return cosf( x ); }
double tan( double x ) { return tanf( x ); }
double atan2( double y, double x ) { return atan2f( y, x ); }
double atan( double x ) { return atanf( x ); }
double asin( double x ) { return asinf( x ); }
double acos( double x ) { return acosf( x ); }
double sqrt( double x ) { return sqrtf( x ); }
double fabs( double x ) { return fabsf( x ); }
double floor( double x ) { return floorf( x ); }
double ceil( double x ) { return ceilf( x ); }
double trunc( double x ) { return truncf( x ); }
double round( double x ) { return roundf( x ); }
double fmod( double x, double y ) { return fmodf( x, y ); }
long lround( double x ) { return lroundf( x ); }

/*
===========================================================================
stdlib.h
===========================================================================
*/

int abs( int x )
{
	return x < 0 ? -x : x;
}

// the digit value of c in a base up to 36, or -1
static int DigitValue( int c )
{
	if ( isdigit( c ) ) {
		return c - '0';
	}
	if ( isalpha( c ) ) {
		return tolower( c ) - 'a' + 10;
	}
	return -1;
}

// the number without its sign; *negative says there was one
static unsigned long ParseUnsigned( const char *s, char **end, int base, int *negative, int *overflow )
{
	const char *start = s;
	unsigned long value = 0;
	int any = 0, digit;

	*negative = 0;
	*overflow = 0;
	while ( isspace( (unsigned char)*s ) ) {
		s++;
	}
	if ( *s == '-' ) {
		*negative = 1;
		s++;
	} else if ( *s == '+' ) {
		s++;
	}
	if ( ( base == 0 || base == 16 ) && s[0] == '0' && ( s[1] == 'x' || s[1] == 'X' ) && isxdigit( (unsigned char)s[2] ) ) {
		s += 2;
		base = 16;
	} else if ( base == 0 ) {
		base = s[0] == '0' ? 8 : 10;
	}
	for ( ; ( digit = DigitValue( (unsigned char)*s ) ) >= 0 && digit < base; s++ ) {
		if ( value > ( ULONG_MAX - digit ) / base ) {
			*overflow = 1;
		}
		value = value * base + digit;
		any = 1;
	}
	if ( end ) {
		*end = (char *)( any ? s : start );
	}
	return value;
}

unsigned long strtoul( const char *s, char **end, int base )
{
	int negative, overflow;
	unsigned long value = ParseUnsigned( s, end, base, &negative, &overflow );

	if ( overflow ) {
		return ULONG_MAX;
	}
	return negative ? 0ul - value : value;
}

long strtol( const char *s, char **end, int base )
{
	int negative, overflow;
	unsigned long value = ParseUnsigned( s, end, base, &negative, &overflow );

	if ( negative ) {
		return overflow || value > (unsigned long)LONG_MAX + 1ul ? LONG_MIN : -(long)value;
	}
	return overflow || value > (unsigned long)LONG_MAX ? LONG_MAX : (long)value;
}

int atoi( const char *s )
{
	return (int)strtol( s, NULL, 10 );
}

// value * 10^exponent; the powers are the nearest floats, so the result is within about 2 units of the last place
static float ScaleByPower10( float value, int exponent )
{
	static const float power[39] = {
		1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f, 1e11f, 1e12f, 1e13f, 1e14f, 1e15f, 1e16f, 1e17f, 1e18f,
		1e19f, 1e20f, 1e21f, 1e22f, 1e23f, 1e24f, 1e25f, 1e26f, 1e27f, 1e28f, 1e29f, 1e30f, 1e31f, 1e32f, 1e33f, 1e34f, 1e35f,
		1e36f, 1e37f, 1e38f
	};

	if ( exponent > 100 ) {
		exponent = 100;
	} else if ( exponent < -100 ) {
		exponent = -100;
	}
	for ( ; exponent > 38; exponent -= 38 ) {
		value *= power[38];
	}
	for ( ; exponent < -38; exponent += 38 ) {
		value /= power[38];
	}
	return exponent < 0 ? value / power[-exponent] : value * power[exponent];
}

// Digits, a point, an exponent. The first 9 significant digits count; the result is within about 2 units of the last place.
// Not understood: inf, nan and hexadecimal floats.
float strtof( const char *s, char **end )
{
	const char *start = s;
	unsigned int mantissa = 0;
	int digits = 0, exponent = 0, any = 0, neg = 0, point = 0, e;
	float value;

	while ( isspace( (unsigned char)*s ) ) {
		s++;
	}
	if ( *s == '-' || *s == '+' ) {
		neg = *s == '-';
		s++;
	}
	for ( ; ; s++ ) {
		if ( isdigit( (unsigned char)*s ) ) {
			any = 1;
			if ( digits < 9 ) {
				mantissa = mantissa * 10 + ( *s - '0' );
				if ( mantissa ) {
					digits++;
				}
				if ( point ) {
					exponent--;
				}
			} else if ( !point ) {
				exponent++;			// a digit of the integer part that does not fit
			}
		} else if ( *s == '.' && !point ) {
			point = 1;
		} else {
			break;
		}
	}
	if ( !any ) {
		if ( end ) {
			*end = (char *)start;
		}
		return 0.0f;
	}
	if ( *s == 'e' || *s == 'E' ) {
		const char *p = s + 1;
		int eneg = 0;

		if ( *p == '-' || *p == '+' ) {
			eneg = *p == '-';
			p++;
		}
		if ( isdigit( (unsigned char)*p ) ) {
			for ( e = 0; isdigit( (unsigned char)*p ); p++ ) {
				if ( e < 10000 ) {
					e = e * 10 + ( *p - '0' );
				}
			}
			exponent += eneg ? -e : e;
			s = p;
		}
	}
	if ( end ) {
		*end = (char *)s;
	}
	value = ScaleByPower10( (float)(int)mantissa, exponent );
	if ( neg ) {		// by the bits, so that -0 stays -0
		floatBits_t b;

		b.f = value;
		b.u |= 0x80000000u;
		value = b.f;
	}
	return value;
}

double strtod( const char *s, char **end ) { return strtof( s, end ); }
double atof( const char *s ) { return strtof( s, NULL ); }

// a shell sort: no recursion, no extra memory
void qsort( void *base, size_t count, size_t size, int ( *compare )( const void *, const void * ) )
{
	char *a = (char *)base;
	size_t gap, i, j, k;

	for ( gap = count / 2; gap > 0; gap = gap / 2 ) {
		for ( i = gap; i < count; i++ ) {
			for ( j = i; j >= gap && compare( a + ( j - gap ) * size, a + j * size ) > 0; j -= gap ) {
				char *p = a + ( j - gap ) * size, *q = a + j * size;

				for ( k = 0; k < size; k++ ) {
					char t = p[k];

					p[k] = q[k];
					q[k] = t;
				}
			}
		}
	}
}

static unsigned int randState = 1;

int rand( void )
{
	randState = randState * 1103515245u + 12345u;
	return (int)( ( randState >> 16 ) & RAND_MAX );
}

void srand( unsigned seed )
{
	randState = seed;
}

/*
===========================================================================
stdio.h: formatting
===========================================================================
*/

typedef struct {
	char	*out;
	int		size, len;
} sink_t;

static void Put( sink_t *s, int c )
{
	if ( s->len < s->size - 1 ) {
		s->out[s->len] = (char)c;
	}
	s->len++;
}

typedef struct {
	int		width, precision;
	int		left, zero, plus, space, alt;
} spec_t;

// prefix (sign or 0x), zeros, body; the padding goes to the left, or after the text, or as zeros after the prefix
static void PutField( sink_t *s, const spec_t *sp, const char *prefix, int zeros, const char *body, int bodyLen, int zeroPad )
{
	int prefixLen = (int)strlen( prefix ), pad, i;

	pad = sp->width - prefixLen - zeros - bodyLen;
	if ( pad < 0 ) {
		pad = 0;
	}
	if ( !sp->left ) {
		if ( zeroPad ) {
			zeros += pad;
		} else {
			for ( i = 0; i < pad; i++ ) {
				Put( s, ' ' );
			}
		}
	}
	for ( i = 0; prefix[i]; i++ ) {
		Put( s, prefix[i] );
	}
	for ( i = 0; i < zeros; i++ ) {
		Put( s, '0' );
	}
	for ( i = 0; i < bodyLen; i++ ) {
		Put( s, body[i] );
	}
	if ( sp->left ) {
		for ( i = 0; i < pad; i++ ) {
			Put( s, ' ' );
		}
	}
}

static int UnsignedToText( unsigned int v, int base, int upper, char *end )
{
	const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	char *p = end;

	do {
		*--p = set[v % base];
		v /= base;
	} while ( v );
	return (int)( end - p );
}

static void PutInteger( sink_t *s, const spec_t *sp, unsigned int v, int negative, int base, int upper )
{
	char text[40];
	char prefix[4];
	int len, zeros = 0, n = 0;

	if ( sp->precision == 0 && v == 0 ) {
		len = 0;				// "%.0d" of 0 prints nothing
	} else {
		len = UnsignedToText( v, base, upper, text + sizeof( text ) );
	}
	if ( negative ) {
		prefix[n++] = '-';
	} else if ( base == 10 && sp->plus ) {
		prefix[n++] = '+';
	} else if ( base == 10 && sp->space ) {
		prefix[n++] = ' ';
	}
	if ( sp->alt && v && base == 16 ) {
		prefix[n++] = '0';
		prefix[n++] = upper ? 'X' : 'x';
	}
	prefix[n] = 0;
	if ( sp->precision > len ) {
		zeros = sp->precision - len;
	}
	PutField( s, sp, prefix, zeros, text + sizeof( text ) - len, len, sp->zero && sp->precision < 0 );
}

// The exact decimal digits of a float that is 2^32 or more: the mantissa times 2, as many times as the exponent says.
static int BigToText( unsigned int mantissa, int doublings, char *end )
{
	unsigned char digit[48];		// the number, least significant digit first
	int count = 0, i, carry;

	while ( mantissa ) {
		digit[count++] = (unsigned char)( mantissa % 10 );
		mantissa /= 10;
	}
	while ( doublings-- > 0 ) {
		carry = 0;
		for ( i = 0; i < count; i++ ) {
			int d = digit[i] * 2 + carry;

			digit[i] = (unsigned char)( d % 10 );
			carry = d / 10;
		}
		if ( carry ) {
			digit[count++] = (unsigned char)carry;
		}
	}
	for ( i = 0; i < count; i++ ) {
		*--end = (char)( '0' + digit[i] );
	}
	return count;
}

// The fraction of a float is at most 149 bits, so it is kept as a fixed point number of 10 limbs of 16 bits (the first is the
// most significant) and the digits come out by multiplying with 10: what is printed is what the number is.
#define FRAC_LIMBS	10

// %f: halves round to even, like the C library; a precision above 20 is 20
static void PutFloat( sink_t *s, const spec_t *sp, float value )
{
	floatBits_t bits;
	char text[96];
	char fraction[24];
	char whole[128];
	char prefix[2];
	unsigned int mantissa, ip = 0, num = 0, limb[FRAC_LIMBS];
	int exponent, q = 0, precision, i, j, len = 0, n = 0, w = 0, up = 0;
	char *p = text + sizeof( text );

	bits.f = value;
	if ( bits.u >> 31 ) {
		prefix[n++] = '-';
	} else if ( sp->plus ) {
		prefix[n++] = '+';
	} else if ( sp->space ) {
		prefix[n++] = ' ';
	}
	prefix[n] = 0;
	exponent = ( bits.u >> 23 ) & 255;
	mantissa = bits.u & 0x7FFFFF;
	if ( exponent == 255 ) {
		PutField( s, sp, prefix, 0, mantissa ? "nan" : "inf", 3, 0 );
		return;
	}
	if ( exponent ) {
		mantissa |= 0x800000;
	} else {
		exponent = 1;			// a denormal
	}
	exponent -= 150;			// the value is mantissa * 2^exponent
	precision = sp->precision < 0 ? 6 : sp->precision > 20 ? 20 : sp->precision;
	for ( i = 0; i < precision; i++ ) {
		fraction[i] = 0;
	}

	if ( exponent >= 9 ) {		// 2^32 or more, no fraction
		len = BigToText( mantissa, exponent, p );
		p -= len;
	} else {
		if ( exponent >= 0 ) {
			ip = mantissa << exponent;
		} else if ( exponent > -24 ) {
			q = -exponent;
			ip = mantissa >> q;
			num = mantissa & ( ( 1u << q ) - 1 );
		} else {
			q = -exponent;
			num = mantissa;
		}
		// the fraction is num / 2^q: bit j of num is the bit q - j after the point
		for ( i = 0; i < FRAC_LIMBS; i++ ) {
			limb[i] = 0;
		}
		for ( j = 0; j < 24; j++ ) {
			if ( num & ( 1u << j ) ) {
				int k = q - j - 1;		// counted from the first bit after the point

				limb[k / 16] |= 0x8000u >> ( k % 16 );
			}
		}
		for ( i = 0; i < precision; i++ ) {
			unsigned int carry = 0;

			for ( j = FRAC_LIMBS - 1; j >= 0; j-- ) {
				unsigned int t = limb[j] * 10 + carry;

				limb[j] = t & 0xFFFF;
				carry = t >> 16;
			}
			fraction[i] = (char)carry;
		}
		// what is left decides the rounding
		if ( limb[0] & 0x8000u ) {
			int more = ( limb[0] & 0x7FFFu ) != 0;
			int lastOdd = precision ? fraction[precision - 1] & 1 : ip & 1;

			for ( j = 1; j < FRAC_LIMBS; j++ ) {
				more |= limb[j] != 0;
			}
			up = more || lastOdd;
		}
		if ( up ) {
			for ( i = precision - 1; i >= 0; i-- ) {
				if ( fraction[i] == 9 ) {
					fraction[i] = 0;
				} else {
					fraction[i]++;
					break;
				}
			}
			if ( i < 0 ) {
				ip++;
			}
		}
		len = UnsignedToText( ip, 10, 0, p );
		p -= len;
	}
	for ( i = 0; i < len; i++ ) {
		whole[w++] = p[i];
	}
	if ( precision || sp->alt ) {
		whole[w++] = '.';
	}
	for ( i = 0; i < precision; i++ ) {
		whole[w++] = (char)( '0' + fraction[i] );
	}
	PutField( s, sp, prefix, 0, whole, w, sp->zero );
}

int vsnprintf( char *out, size_t size, const char *format, va_list ap )
{
	sink_t s;
	spec_t sp;
	const char *f = format;
	int isize = size > (size_t)INT_MAX ? INT_MAX : (int)size;

	s.out = out;
	s.size = isize;
	s.len = 0;
	while ( *f ) {
		if ( *f != '%' ) {
			Put( &s, *f++ );
			continue;
		}
		f++;
		memset( &sp, 0, sizeof( sp ) );
		sp.precision = -1;
		for ( ; ; f++ ) {
			if ( *f == '-' ) {
				sp.left = 1;
			} else if ( *f == '+' ) {
				sp.plus = 1;
			} else if ( *f == ' ' ) {
				sp.space = 1;
			} else if ( *f == '0' ) {
				sp.zero = 1;
			} else if ( *f == '#' ) {
				sp.alt = 1;
			} else {
				break;
			}
		}
		if ( *f == '*' ) {
			sp.width = va_arg( ap, int );
			if ( sp.width < 0 ) {
				sp.left = 1;
				sp.width = -sp.width;
			}
			f++;
		} else {
			for ( ; isdigit( (unsigned char)*f ); f++ ) {
				sp.width = sp.width * 10 + ( *f - '0' );
			}
		}
		if ( *f == '.' ) {
			f++;
			sp.precision = 0;
			if ( *f == '*' ) {
				sp.precision = va_arg( ap, int );
				f++;
			} else {
				for ( ; isdigit( (unsigned char)*f ); f++ ) {
					sp.precision = sp.precision * 10 + ( *f - '0' );
				}
			}
		}
		while ( *f == 'h' || *f == 'l' || *f == 'z' || *f == 't' || *f == 'j' ) {
			f++;				// everything is 32 bits
		}
		if ( sp.left ) {
			sp.zero = 0;
		}
		switch ( *f ) {
			case 'd':
			case 'i':
				{
					int v = va_arg( ap, int );

					PutInteger( &s, &sp, v < 0 ? 0u - (unsigned int)v : (unsigned int)v, v < 0, 10, 0 );
				}
				break;
			case 'u':
				PutInteger( &s, &sp, va_arg( ap, unsigned int ), 0, 10, 0 );
				break;
			case 'x':
				PutInteger( &s, &sp, va_arg( ap, unsigned int ), 0, 16, 0 );
				break;
			case 'X':
				PutInteger( &s, &sp, va_arg( ap, unsigned int ), 0, 16, 1 );
				break;
			case 'o':
				PutInteger( &s, &sp, va_arg( ap, unsigned int ), 0, 8, 0 );
				break;
			case 'p':
				sp.alt = 1;
				PutInteger( &s, &sp, va_arg( ap, unsigned int ), 0, 16, 0 );
				break;
			case 'c':
				{
					char c = (char)va_arg( ap, int );

					PutField( &s, &sp, "", 0, &c, 1, 0 );
				}
				break;
			case 's':
				{
					const char *text = va_arg( ap, const char * );
					int len;

					if ( !text ) {
						text = "(null)";
					}
					len = 0;
					while ( text[len] && ( sp.precision < 0 || len < sp.precision ) ) {
						len++;
					}
					PutField( &s, &sp, "", 0, text, len, 0 );
				}
				break;
			case 'f':
			case 'F':
				PutFloat( &s, &sp, va_arg( ap, float ) );
				break;
			case '%':
				Put( &s, '%' );
				break;
			case 0:
				goto done;
			default:			// not understood: the text is printed as it is
				Put( &s, '%' );
				Put( &s, *f );
				break;
		}
		f++;
	}
done:
	if ( isize > 0 ) {
		out[s.len < isize ? s.len : isize - 1] = 0;
	}
	return s.len;
}

int snprintf( char *out, size_t size, const char *format, ... )
{
	va_list ap;
	int n;

	va_start( ap, format );
	n = vsnprintf( out, size, format, ap );
	va_end( ap );
	return n;
}

int sprintf( char *out, const char *format, ... )
{
	va_list ap;
	int n;

	va_start( ap, format );
	n = vsnprintf( out, 0x7FFFFFFF, format, ap );
	va_end( ap );
	return n;
}
