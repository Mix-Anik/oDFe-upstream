/*
mkembed: writes a C file with the contents of files, for odf-build (which has the files of the SDK in itself).

    mkembed out.c root file1 file2 ...

The names of the files are written without 'root' (the path of the first file below root is "include/odfc.h" for
root/include/odfc.h). The C file defines

    const odfEmbedded_t odf_embedded[];  const int odf_num_embedded;

(see odf-build.c). It is a separate program so that building odf-build needs nothing but a C compiler.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main( int argc, char **argv )
{
	FILE *out;
	int i;
	size_t rootLen;

	if ( argc < 3 ) {
		fprintf( stderr, "usage: mkembed out.c root file ...\n" );
		return 2;
	}
	out = fopen( argv[1], "wb" );
	if ( !out ) {
		fprintf( stderr, "mkembed: cannot write %s\n", argv[1] );
		return 1;
	}
	rootLen = strlen( argv[2] );
	fprintf( out, "/* made by mkembed, do not edit */\n\ntypedef struct {\n\tconst char *path;\n\tconst unsigned char *data;\n\tunsigned size;\n} odfEmbedded_t;\n\n" );
	for ( i = 3; i < argc; i++ ) {
		FILE *f = fopen( argv[i], "rb" );
		int c;
		unsigned n = 0;

		if ( !f ) {
			fprintf( stderr, "mkembed: cannot read %s\n", argv[i] );
			return 1;
		}
		fprintf( out, "static const unsigned char file%d[] = {", i - 3 );
		while ( ( c = fgetc( f ) ) != EOF ) {
			fprintf( out, "%s%d,", n % 24 ? "" : "\n", c );
			n++;
		}
		fprintf( out, "0 };\n\n" );
		fclose( f );
	}
	fprintf( out, "const odfEmbedded_t odf_embedded[] = {\n" );
	for ( i = 3; i < argc; i++ ) {
		const char *name = argv[i];
		unsigned size = 0;
		FILE *f = fopen( argv[i], "rb" );

		if ( strncmp( name, argv[2], rootLen ) == 0 && ( name[rootLen] == '/' || name[rootLen] == '\\' ) ) {
			name += rootLen + 1;
		}
		fseek( f, 0, SEEK_END );
		size = (unsigned)ftell( f );
		fclose( f );
		fprintf( out, "\t{ \"%s\", file%d, %u },\n", name, i - 3, size );
	}
	fprintf( out, "};\n\nconst int odf_num_embedded = %d;\n", argc - 3 );
	return fclose( out ) == 0 ? 0 : 1;
}
