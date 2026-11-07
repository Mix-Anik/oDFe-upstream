/*
A minimal JSON writer and reader, enough for the HTTP APIs plugins talk to: flat and nested objects of strings, numbers and
booleans. No dependencies. A plugin is a QVM program, which has no 64 bit integers: numbers are 32 bit, and ids that are
bigger are two 32 bit halves (low, high).

Using it: #include "odf_json.h" in the plugin. odf-build sees the include and adds odf_json.c to the program.
The parser works on the answer inside the sandbox of the plugin; the engine never reads the JSON.
*/

#ifndef ODF_JSON_H
#define ODF_JSON_H

/* ---- writer: builds into a caller buffer, never overflows it ---- */

#define JW_MAX_DEPTH 8

typedef struct {
	char	*buf;
	int		size;
	int		len;
	int		overflow;
	int		depth;
	int		hasItems[JW_MAX_DEPTH];		/* a comma is needed before the next item at this depth */
} jsonw_t;

void	Jw_Init( jsonw_t *w, char *buf, int size );
void	Jw_BeginObject( jsonw_t *w );							/* as a value, or the root */
void	Jw_EndObject( jsonw_t *w );
void	Jw_BeginObjectKey( jsonw_t *w, const char *key );		/* "key":{ */
void	Jw_String( jsonw_t *w, const char *key, const char *value );
void	Jw_Int( jsonw_t *w, const char *key, int value );
void	Jw_UInt64( jsonw_t *w, const char *key, unsigned lo, unsigned hi );
void	Jw_Bool( jsonw_t *w, const char *key, int value );
/* Returns the length of the finished document, or -1 when it did not fit or was not balanced. */
int		Jw_Finish( jsonw_t *w );

/* ---- reader: values are located by path in a zero terminated document ---- */

/* Returns a pointer to the value at path ("id", "error.code"), or NULL if it is not there. */
const char	*Json_Find( const char *json, const char *path );
/* Copies a string value (unescaped) into out. Returns its length, -1 if v is not a string or it does not fit. */
int			Json_String( const char *v, char *out, int outSize );
/* Reads an integer value (32 bit; a bigger one is not read). Returns 1 on success. */
int			Json_Int( const char *v, int *out );
/* Reads a positive integer of up to 64 bits as two halves. Returns 1 on success. */
int			Json_UInt64( const char *v, unsigned *lo, unsigned *hi );
/* Reads true or false. Returns 1 on success. */
int			Json_Bool( const char *v, int *out );
/* Returns a pointer to element number index (from 0) of the array that starts at v, or NULL if v is not an array
   or has fewer elements. Use it to walk a list: for ( i = 0; ( e = Json_At( list, i ) ) != NULL; i++ ). */
const char	*Json_At( const char *v, int index );

#endif
