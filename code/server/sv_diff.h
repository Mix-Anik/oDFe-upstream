/*
===========================================================================
sv_diff.h -- runs of differing bytes between two blocks of memory (for sv_state.c)

Pure code: only memcmp / memcpy, so a test can include it without the engine.

A diff is a list of runs: int offset, int length, then length bytes. Equal stretches shorter than DIFF_GAP do not end a run.
===========================================================================
*/

#ifndef SV_DIFF_H
#define SV_DIFF_H

#include <string.h>

#define DIFF_GAP		16		// equal bytes that end a run of differences
#define DIFF_BLOCK		64		// the comparison skips equal blocks of this size

// Writes the runs of bytes where cur differs from base into out (nothing if out is NULL). Returns the size of the runs.
static int Diff_Make( const unsigned char *base, const unsigned char *cur, int size, unsigned char *out )
{
	int		i = 0, written = 0, start = 0, last = 0;
	int		open = 0;

#define DIFF_EMIT_RUN() \
	do { \
		const int len_ = last + 1 - start; \
		if ( out ) { \
			memcpy( out + written, &start, 4 ); \
			memcpy( out + written + 4, &len_, 4 ); \
			memcpy( out + written + 8, cur + start, len_ ); \
		} \
		written += 8 + len_; \
	} while ( 0 )

	while ( i < size ) {
		if ( i + DIFF_BLOCK <= size && !memcmp( base + i, cur + i, DIFF_BLOCK ) ) {
			if ( open ) {
				DIFF_EMIT_RUN();
				open = 0;
			}
			i += DIFF_BLOCK;
			continue;
		}
		if ( base[i] != cur[i] ) {
			if ( !open ) {
				start = i;
				open = 1;
			}
			last = i;
		} else if ( open && i - last > DIFF_GAP ) {
			DIFF_EMIT_RUN();
			open = 0;
		}
		i++;
	}
	if ( open ) {
		DIFF_EMIT_RUN();
	}
#undef DIFF_EMIT_RUN
	return written;
}

// dst = base with the runs put over it. Returns 0, or -1 if the runs do not fit into size bytes.
static int Diff_Apply( const unsigned char *runs, int runsSize, const unsigned char *base, unsigned char *dst, int size )
{
	int pos = 0;

	memcpy( dst, base, size );
	while ( pos + 8 <= runsSize ) {
		int off, len;

		memcpy( &off, runs + pos, 4 );
		memcpy( &len, runs + pos + 4, 4 );
		pos += 8;
		if ( off < 0 || len < 0 || off > size - len || len > runsSize - pos ) {
			return -1;
		}
		memcpy( dst + off, runs + pos, len );
		pos += len;
	}
	return 0;
}

#endif
