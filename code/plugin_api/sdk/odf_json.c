/* A minimal JSON writer and reader for plugins (see odf_json.h). odf-build adds this file to a program that includes odf_json.h. */

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "odf_json.h"

/* ---------------------------------------------------------------------------
   writer
   --------------------------------------------------------------------------- */

static void PutChar( jsonw_t *w, char c )
{
	if ( w->len + 1 < w->size ) {
		w->buf[w->len++] = c;
		w->buf[w->len] = '\0';
	} else {
		w->overflow = 1;
	}
}

static void PutText( jsonw_t *w, const char *s )
{
	while ( *s ) {
		PutChar( w, *s++ );
	}
}

static void PutQuoted( jsonw_t *w, const char *s )
{
	PutChar( w, '"' );
	for ( ; *s; s++ ) {
		unsigned char c = (unsigned char)*s;

		switch ( c ) {
		case '"':	PutText( w, "\\\"" ); break;
		case '\\':	PutText( w, "\\\\" ); break;
		case '\n':	PutText( w, "\\n" ); break;
		case '\r':	PutText( w, "\\r" ); break;
		case '\t':	PutText( w, "\\t" ); break;
		default:
			if ( c < 0x20 ) {
				char esc[8];

				snprintf( esc, sizeof( esc ), "\\u%04x", c );
				PutText( w, esc );
			} else {
				PutChar( w, (char)c );		/* UTF-8 passes through */
			}
		}
	}
	PutChar( w, '"' );
}

/* comma before every item but the first of an object */
static void Separator( jsonw_t *w )
{
	if ( w->depth > 0 ) {
		if ( w->hasItems[w->depth - 1] ) {
			PutChar( w, ',' );
		}
		w->hasItems[w->depth - 1] = 1;
	}
}

static void Key( jsonw_t *w, const char *key )
{
	Separator( w );
	PutQuoted( w, key );
	PutChar( w, ':' );
}

void Jw_Init( jsonw_t *w, char *buf, int size )
{
	memset( w, 0, sizeof( *w ) );
	w->buf = buf;
	w->size = size;
	if ( size > 0 ) {
		buf[0] = '\0';
	}
}

static void Open( jsonw_t *w )
{
	if ( w->depth >= JW_MAX_DEPTH ) {
		w->overflow = 1;
		return;
	}
	PutChar( w, '{' );
	w->hasItems[w->depth++] = 0;
}

void Jw_BeginObject( jsonw_t *w )
{
	Separator( w );
	Open( w );
}

void Jw_BeginObjectKey( jsonw_t *w, const char *key )
{
	Key( w, key );
	Open( w );
}

void Jw_EndObject( jsonw_t *w )
{
	if ( w->depth == 0 ) {
		w->overflow = 1;
		return;
	}
	w->depth--;
	PutChar( w, '}' );
}

void Jw_String( jsonw_t *w, const char *key, const char *value )
{
	Key( w, key );
	PutQuoted( w, value );
}

void Jw_Int( jsonw_t *w, const char *key, int value )
{
	char num[32];

	Key( w, key );
	snprintf( num, sizeof( num ), "%d", value );
	PutText( w, num );
}

/* the decimal digits of a 64 bit number kept in two halves: long division by 10 in 16 bit pieces */
void Jw_UInt64( jsonw_t *w, const char *key, unsigned lo, unsigned hi )
{
	char num[24];
	unsigned limb[4];
	int n = (int)sizeof( num ) - 1, i, nonzero;

	limb[0] = hi >> 16;
	limb[1] = hi & 0xFFFF;
	limb[2] = lo >> 16;
	limb[3] = lo & 0xFFFF;
	num[n] = '\0';
	do {
		unsigned rest = 0;

		nonzero = 0;
		for ( i = 0; i < 4; i++ ) {
			unsigned cur = rest * 65536 + limb[i];

			limb[i] = cur / 10;
			rest = cur % 10;
			nonzero |= limb[i] != 0;
		}
		num[--n] = (char)( '0' + rest );
	} while ( nonzero );
	Key( w, key );
	PutText( w, num + n );
}

void Jw_Bool( jsonw_t *w, const char *key, int value )
{
	Key( w, key );
	PutText( w, value ? "true" : "false" );
}

int Jw_Finish( jsonw_t *w )
{
	if ( w->overflow || w->depth != 0 ) {
		return -1;
	}
	return w->len;
}

/* ---------------------------------------------------------------------------
   reader
   --------------------------------------------------------------------------- */

#define MAX_NESTING 32

static const char *SkipSpace( const char *s )
{
	while ( *s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ) {
		s++;
	}
	return s;
}

/* s points at the opening quote; returns the character after the closing one, NULL if unterminated */
static const char *SkipString( const char *s )
{
	for ( s++; *s; s++ ) {
		if ( *s == '\\' ) {
			if ( !s[1] ) {
				return NULL;
			}
			s++;
		} else if ( *s == '"' ) {
			return s + 1;
		}
	}
	return NULL;
}

/* skips any value (string, number, literal, object or array); NULL on malformed input */
static const char *SkipValue( const char *s )
{
	int depth = 0;

	s = SkipSpace( s );
	if ( *s == '"' ) {
		return SkipString( s );
	}
	if ( *s != '{' && *s != '[' ) {
		/* number or literal */
		const char *start = s;

		while ( *s && *s != ',' && *s != '}' && *s != ']' && *s != ' ' && *s != '\t' && *s != '\n' && *s != '\r' ) {
			s++;
		}
		return s == start ? NULL : s;
	}
	for ( ; *s; s++ ) {
		if ( *s == '"' ) {
			s = SkipString( s );
			if ( !s ) {
				return NULL;
			}
			s--;
		} else if ( *s == '{' || *s == '[' ) {
			if ( ++depth > MAX_NESTING ) {
				return NULL;
			}
		} else if ( *s == '}' || *s == ']' ) {
			if ( --depth == 0 ) {
				return s + 1;
			}
		}
	}
	return NULL;
}

/* compares the quoted key at s with name of length n (keys with escapes never match: the API has none) */
static int KeyEquals( const char *s, const char *name, size_t n )
{
	return strncmp( s + 1, name, n ) == 0 && s[1 + n] == '"';
}

const char *Json_Find( const char *json, const char *path )
{
	const char *s = json;
	const char *dot;
	size_t n;

	if ( !json || !path || !*path ) {
		return NULL;
	}
	dot = strchr( path, '.' );
	n = dot ? (size_t)( dot - path ) : strlen( path );

	s = SkipSpace( s );
	if ( *s != '{' ) {
		return NULL;
	}
	s++;
	for ( ;; ) {
		const char *keyStart, *value;

		s = SkipSpace( s );
		if ( *s == '}' || !*s ) {
			return NULL;
		}
		if ( *s == ',' ) {
			s++;
			continue;
		}
		if ( *s != '"' ) {
			return NULL;
		}
		keyStart = s;
		s = SkipString( s );
		if ( !s ) {
			return NULL;
		}
		s = SkipSpace( s );
		if ( *s != ':' ) {
			return NULL;
		}
		value = SkipSpace( s + 1 );

		if ( KeyEquals( keyStart, path, n ) ) {
			return dot ? Json_Find( value, dot + 1 ) : value;
		}
		s = SkipValue( value );
		if ( !s ) {
			return NULL;
		}
	}
}

const char *Json_At( const char *v, int index )
{
	int i;

	if ( !v || index < 0 ) {
		return NULL;
	}
	v = SkipSpace( v );
	if ( *v != '[' ) {
		return NULL;
	}
	v = SkipSpace( v + 1 );
	for ( i = 0; ; i++ ) {
		if ( *v == ']' || !*v ) {
			return NULL;
		}
		if ( i == index ) {
			return v;
		}
		v = SkipValue( v );
		if ( !v ) {
			return NULL;
		}
		v = SkipSpace( v );
		if ( *v == ',' ) {
			v = SkipSpace( v + 1 );
		} else if ( *v != ']' ) {
			return NULL;
		}
	}
}

static int HexDigit( char c )
{
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

int Json_String( const char *v, char *out, int outSize )
{
	int len = 0;

	if ( !v || *v != '"' || outSize < 1 ) {
		return -1;
	}
	v++;
	while ( *v && *v != '"' ) {
		unsigned int cp;
		char one[4];
		int n, i;

		if ( *v != '\\' ) {
			cp = (unsigned char)*v++;
			one[0] = (char)cp;
			n = 1;
		} else {
			v++;
			switch ( *v ) {
			case 'n':	cp = '\n'; v++; break;
			case 'r':	cp = '\r'; v++; break;
			case 't':	cp = '\t'; v++; break;
			case 'b':	cp = '\b'; v++; break;
			case 'f':	cp = '\f'; v++; break;
			case '"': case '\\': case '/':
				cp = (unsigned char)*v++;
				break;
			case 'u':
				if ( HexDigit( v[1] ) < 0 || HexDigit( v[2] ) < 0 || HexDigit( v[3] ) < 0 || HexDigit( v[4] ) < 0 ) {
					return -1;
				}
				cp = (unsigned)( HexDigit( v[1] ) << 12 | HexDigit( v[2] ) << 8 | HexDigit( v[3] ) << 4 | HexDigit( v[4] ) );
				v += 5;
				break;
			default:
				return -1;
			}
			/* UTF-8; surrogate halves are not combined, they become '?' */
			if ( cp >= 0xD800 && cp <= 0xDFFF ) {
				cp = '?';
			}
			if ( cp < 0x80 ) {
				one[0] = (char)cp;
				n = 1;
			} else if ( cp < 0x800 ) {
				one[0] = (char)( 0xC0 | cp >> 6 );
				one[1] = (char)( 0x80 | ( cp & 0x3F ) );
				n = 2;
			} else {
				one[0] = (char)( 0xE0 | cp >> 12 );
				one[1] = (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
				one[2] = (char)( 0x80 | ( cp & 0x3F ) );
				n = 3;
			}
		}
		if ( len + n >= outSize ) {
			return -1;
		}
		for ( i = 0; i < n; i++ ) {
			out[len++] = one[i];
		}
	}
	if ( *v != '"' ) {
		return -1;
	}
	out[len] = '\0';
	return len;
}

static int EndsNumber( char c )
{
	return c == '\0' || c == ',' || c == '}' || c == ']' || c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

int Json_Int( const char *v, int *out )
{
	char *end;
	long n;

	if ( !v ) {
		return 0;
	}
	n = strtol( v, &end, 10 );
	if ( end == v || !EndsNumber( *end ) || n == LONG_MAX || n == LONG_MIN || n > INT_MAX || n < INT_MIN ) {
		return 0;		/* not a number, or one that does not fit (long has 64 bits on some hosts, 32 in the plugin VM) */
	}
	*out = (int)n;
	return 1;
}

int Json_UInt64( const char *v, unsigned *lo, unsigned *hi )
{
	unsigned l = 0, h = 0;
	int digits = 0;

	if ( !v ) {
		return 0;
	}
	for ( ; *v >= '0' && *v <= '9'; v++, digits++ ) {
		/* (h,l) = (h,l) * 10 + digit, in 16 bit pieces so that nothing needs more than 32 bits */
		unsigned a = ( l & 0xFFFF ) * 10 + (unsigned)( *v - '0' );
		unsigned b = ( l >> 16 ) * 10 + ( a >> 16 );
		unsigned c = ( h & 0xFFFF ) * 10 + ( b >> 16 );
		unsigned d = ( h >> 16 ) * 10 + ( c >> 16 );

		if ( d >> 16 ) {
			return 0;		/* more than 64 bits */
		}
		l = ( a & 0xFFFF ) | ( b << 16 );
		h = ( c & 0xFFFF ) | ( d << 16 );
	}
	if ( !digits || !EndsNumber( *v ) ) {
		return 0;
	}
	*lo = l;
	*hi = h;
	return 1;
}

int Json_Bool( const char *v, int *out )
{
	if ( !v ) {
		return 0;
	}
	if ( !strncmp( v, "true", 4 ) ) {
		*out = 1;
		return 1;
	}
	if ( !strncmp( v, "false", 5 ) ) {
		*out = 0;
		return 1;
	}
	return 0;
}
