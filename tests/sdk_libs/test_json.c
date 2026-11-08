#include <stdio.h>
#include <string.h>

#include "odf_json.h"

static int failures;

#define CHECK( cond, ... ) do { if ( !( cond ) ) { failures++; printf( "  FAIL line %d: ", __LINE__ ); printf( __VA_ARGS__ ); printf( "\n" ); } } while ( 0 )

int main( void )
{
	char buf[512], out[128];
	jsonw_t w;
	const char *v;
	int n;
	unsigned lo, hi;
	int b, len;

	/* ---- writer ---- */
	Jw_Init( &w, buf, sizeof( buf ) );
	Jw_BeginObject( &w );
	Jw_String( &w, "name", "mix" );
	Jw_Int( &w, "time_ms", 12345 );
	Jw_Int( &w, "neg", -7 );
	Jw_Bool( &w, "ok", 1 );
	Jw_BeginObjectKey( &w, "settings" );
	Jw_Int( &w, "sub_mode", -1 );
	Jw_Bool( &w, "x", 0 );
	Jw_EndObject( &w );
	Jw_String( &w, "last", "end" );
	Jw_EndObject( &w );
	len = Jw_Finish( &w );
	CHECK( len > 0 && len == (int)strlen( buf ), "length %d", len );
	CHECK( !strcmp( buf, "{\"name\":\"mix\",\"time_ms\":12345,\"neg\":-7,\"ok\":true,\"settings\":{\"sub_mode\":-1,\"x\":false},\"last\":\"end\"}" ), "document: %s", buf );

	Jw_Init( &w, buf, sizeof( buf ) );
	Jw_BeginObject( &w );
	Jw_String( &w, "s", "quote\" backslash\\ newline\n tab\t ctrl\x01 utf8 \xc3\xa4" );
	Jw_EndObject( &w );
	CHECK( Jw_Finish( &w ) > 0, "escaping failed" );
	CHECK( !strcmp( buf, "{\"s\":\"quote\\\" backslash\\\\ newline\\n tab\\t ctrl\\u0001 utf8 \xc3\xa4\"}" ), "escaped: %s", buf );
	/* what was written reads back unchanged */
	CHECK( Json_String( Json_Find( buf, "s" ), out, sizeof( out ) ) > 0 && !strcmp( out, "quote\" backslash\\ newline\n tab\t ctrl\x01 utf8 \xc3\xa4" ), "round trip: %s", out );

	/* 64 bit numbers are written from two halves */
	Jw_Init( &w, buf, sizeof( buf ) );
	Jw_BeginObject( &w );
	Jw_UInt64( &w, "a", 1, 0x00200000u );
	Jw_UInt64( &w, "b", 0, 0 );
	Jw_UInt64( &w, "c", 0xFFFFFFFFu, 0xFFFFFFFFu );
	Jw_UInt64( &w, "d", 4000000000u, 0 );
	Jw_EndObject( &w );
	CHECK( Jw_Finish( &w ) > 0 && !strcmp( buf, "{\"a\":9007199254740993,\"b\":0,\"c\":18446744073709551615,\"d\":4000000000}" ), "64 bit numbers: %s", buf );

	/* too small a buffer is reported, never overrun */
	{
		char small[16];

		Jw_Init( &w, small, sizeof( small ) );
		Jw_BeginObject( &w );
		Jw_String( &w, "key", "a value that does not fit" );
		Jw_EndObject( &w );
		CHECK( Jw_Finish( &w ) == -1 && strlen( small ) < sizeof( small ), "overflow not reported" );
	}
	Jw_Init( &w, buf, sizeof( buf ) );
	Jw_BeginObject( &w );
	CHECK( Jw_Finish( &w ) == -1, "unbalanced document accepted" );

	/* ---- reader: the API's answers ---- */
	{
		const char *saved = "{\"id\":\"0190a1b2-0000-7000-8000-000000000001\",\"rank\":3,\"is_pb\":true,\"is_wr\":false}";
		const char *err = "{\"error\":{\"code\":\"unknown_key\",\"message\":\"public key is not registered\"}}";
		const char *verify = "{\"user_id\":42,\"username\":\"mix\"}";

		CHECK( Json_String( Json_Find( saved, "id" ), out, sizeof( out ) ) == 36 && !strncmp( out, "0190a1b2", 8 ), "id" );
		CHECK( Json_Int( Json_Find( saved, "rank" ), &n ) && n == 3, "rank" );
		CHECK( Json_Bool( Json_Find( saved, "is_pb" ), &b ) && b == 1, "is_pb" );
		CHECK( Json_Bool( Json_Find( saved, "is_wr" ), &b ) && b == 0, "is_wr" );
		CHECK( Json_Find( saved, "missing" ) == NULL, "missing key found" );

		CHECK( Json_String( Json_Find( err, "error.code" ), out, sizeof( out ) ) == 11 && !strcmp( out, "unknown_key" ), "nested code" );
		CHECK( Json_String( Json_Find( err, "error.message" ), out, sizeof( out ) ) > 0, "nested message" );
		CHECK( Json_Find( err, "error.nothing" ) == NULL && Json_Find( err, "code" ) == NULL, "wrong nesting found" );

		CHECK( Json_Int( Json_Find( verify, "user_id" ), &n ) && n == 42, "user_id" );
		CHECK( Json_String( Json_Find( verify, "username" ), out, sizeof( out ) ) == 3 && !strcmp( out, "mix" ), "username" );
	}

	/* ---- reader: formatting, order, big numbers, types ---- */
	v = "  {\n  \"a\" : [1, 2, {\"b\": \"x\"}],\n \"b\": {\"b\": 7},  \"c\":\"v\\u00e4\\ud83d\",\"big\":9007199254740993 }";
	CHECK( Json_Find( v, "b" ) && Json_Int( Json_Find( v, "b.b" ), &n ) && n == 7, "key after an array, whitespace" );
	CHECK( Json_String( Json_Find( v, "c" ), out, sizeof( out ) ) > 0 && !strncmp( out, "v\xc3\xa4", 3 ) && out[3] == '?', "unicode escapes: %s", out );
	CHECK( !Json_Int( Json_Find( v, "big" ), &n ), "a number that does not fit in 32 bits read as one" );
	CHECK( Json_UInt64( Json_Find( v, "big" ), &lo, &hi ) && hi == 0x00200000u && lo == 1u, "64 bit integer: %08x %08x", hi, lo );
	CHECK( Json_UInt64( "18446744073709551615", &lo, &hi ) && hi == 0xFFFFFFFFu && lo == 0xFFFFFFFFu, "largest 64 bit integer" );
	CHECK( !Json_UInt64( "18446744073709551616", &lo, &hi ), "too big for 64 bits" );
	CHECK( !Json_UInt64( "-5", &lo, &hi ) && !Json_UInt64( "12x", &lo, &hi ) && !Json_UInt64( "", &lo, &hi ), "bad 64 bit integers" );
	CHECK( Json_UInt64( "42}", &lo, &hi ) && hi == 0 && lo == 42, "small 64 bit integer" );
	CHECK( !Json_Int( Json_Find( v, "c" ), &n ), "string read as integer" );
	CHECK( Json_String( Json_Find( v, "b.b" ), out, sizeof( out ) ) == -1, "number read as string" );
	CHECK( !Json_Bool( Json_Find( v, "big" ), &b ), "number read as bool" );

	/* strings that contain the characters the scanner looks for */
	v = "{\"x\":\"a } , \\\" ] :\",\"y\":1}";
	CHECK( Json_Int( Json_Find( v, "y" ), &n ) && n == 1, "structure characters inside a string" );
	CHECK( Json_String( Json_Find( v, "x" ), out, sizeof( out ) ) > 0 && !strcmp( out, "a } , \" ] :" ), "string value: %s", out );

	/* too long for the output */
	CHECK( Json_String( Json_Find( "{\"s\":\"0123456789\"}", "s" ), out, 5 ) == -1, "truncated string accepted" );

	/* ---- arrays ---- */
	{
		const char *board = "{\"total\":3,\"entries\":[ {\"rank\":1,\"username\":\"a\"}, {\"rank\":2,\"username\":\"b,]\"},\n{\"rank\":2,\"username\":\"c\"} ]}";
		const char *list = Json_Find( board, "entries" ), *e;
		int i, seen = 0;

		CHECK( list != NULL, "array not found" );
		for ( i = 0; ( e = Json_At( list, i ) ) != NULL && i < 10; i++ ) {
			char name[8] = "";

			seen++;
			Json_String( Json_Find( e, "username" ), name, sizeof( name ) );
			CHECK( ( i == 0 && !strcmp( name, "a" ) ) || ( i == 1 && !strcmp( name, "b,]" ) ) || ( i == 2 && !strcmp( name, "c" ) ), "element %d is '%s'", i, name );
			CHECK( Json_Int( Json_Find( e, "rank" ), &n ) && n == ( i == 0 ? 1 : 2 ), "element %d rank", i );
		}
		CHECK( seen == 3, "walked %d elements", seen );
		CHECK( Json_At( list, 3 ) == NULL && Json_At( list, -1 ) == NULL, "elements beyond the end" );
		CHECK( Json_At( "[]", 0 ) == NULL && Json_At( "[ ]", 0 ) == NULL, "empty array has an element" );
		CHECK( Json_At( "[1,2,3]", 2 ) && Json_Int( Json_At( "[1,2,3]", 2 ), &n ) && n == 3, "numbers in an array" );
		CHECK( Json_At( "{\"a\":1}", 0 ) == NULL && Json_At( "\"x\"", 0 ) == NULL && Json_At( NULL, 0 ) == NULL, "not an array" );
		CHECK( Json_At( "[1,2", 2 ) == NULL && Json_At( "[1 2]", 1 ) == NULL, "broken arrays" );
	}

	/* garbage must not crash or hang */
	{
		static const char *bad[] = { "", "{", "{\"a", "{\"a\":", "{\"a\":\"unterminated", "[1,2]", "not json", "{\"a\":{\"b\":", "{\"a\" 1}", "{1:2}",
			"{\"a\":[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[1", NULL };
		int i;

		for ( i = 0; bad[i]; i++ ) {
			const char *r = Json_Find( bad[i], "a" );

			if ( r ) {
				Json_String( r, out, sizeof( out ) );
				Json_Int( r, &n );
			}
			Json_Find( bad[i], "a.b.c" );
		}
		CHECK( Json_Find( NULL, "a" ) == NULL && Json_Find( "{}", "" ) == NULL, "null arguments" );
	}

	printf( failures ? "FAILED: %d check(s)\n" : "all json checks passed\n", failures );
	return failures ? 1 : 0;
}
