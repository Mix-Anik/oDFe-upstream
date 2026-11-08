/* There is no heap: no malloc. Keep the data in static arrays. */

#ifndef ODF_STDLIB_H
#define ODF_STDLIB_H

#include <stddef.h>

#define RAND_MAX 32767

int abs( int x );
int atoi( const char *s );
long strtol( const char *s, char **end, int base );
unsigned long strtoul( const char *s, char **end, int base );
float strtof( const char *s, char **end );
double strtod( const char *s, char **end );
double atof( const char *s );
void qsort( void *base, size_t count, size_t size, int ( *compare )( const void *, const void * ) );
int rand( void );
void srand( unsigned seed );

#endif
