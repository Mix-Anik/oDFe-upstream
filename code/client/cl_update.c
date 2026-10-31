/*
Updater of the oDFs mod pk3 and of the engine itself: asks GitHub for the latest release, downloads the file of it that
belongs to this installation and puts it in place. Both run as independent jobs with the same code.

Each job has console commands and read only cvars (PREFIX is odfs_update for the mod, odfe_update for the engine) that the
mod menu drives and reads:
	odfs_checkupdate <installed version number> [auto]		the mod; [auto] does nothing when odfs_update_check is 0
	odfe_checkupdate [auto]									the engine, its version is known at compile time
	odfs_update, odfe_update								download the release found by the last check
	PREFIX_state		"" | checking | uptodate | available | downloading | ready | error | checkfailed | unsupported
	PREFIX_latest		"2.1.0"
	PREFIX_latest_num	20100
	PREFIX_progress		0..100 while downloading
	PREFIX_error		reason when the state is error or checkfailed (a failed check is not shown by the menu)
	odfe_version, odfe_version_num		the running engine

Mod: zzz-defrag_vm.pk3, its SHA-512 is checked against the release's .sha512 file. It is left next to the installed pk3 as
.new, files.c swaps it in at the next start, before any pk3 is opened (a loaded pk3 is locked on Windows).
Engine: the zip of the release for this platform (UPD_ENGINE_ASSET) holds every build of it: OpenGL, Vulkan and dedicated
server. All oDFe* files of it replace the files of that name next to the running executable, or are added there, see
Upd_InstallEngine(); the new ones run after a restart. Other platforms (MSVC, macOS, 32 bit, ARM) report "unsupported".

The repository is PREFIX_repo (startup only), the download URLs are built from it, the tag and the fixed file name, so
nothing the server or the API sends decides where a file comes from.
*/

#include "client.h"
#include "../qcommon/auth_proto.h"
#include "../qcommon/monocypher.h"
#include "../qcommon/monocypher-ed25519.h"
#include "../qcommon/odfe_version.h"
#include "../qcommon/unzip.h"

#ifndef _WIN32
#include <sys/stat.h>
#endif

#ifdef USE_CURL

#define UPD_HASH_SIZE		64		// SHA-512
#define UPD_API_MAX			( 512 * 1024 )
#define UPD_HASH_MAX		4096
#define UPD_FILE_MAX		( 64 * 1024 * 1024 )

#define UPD_MAX_FILES		16

// the zip of this platform in the release, as build.yml names them
#if defined( _WIN32 ) && defined( __GNUC__ ) && defined( __x86_64__ )
#define UPD_ENGINE_ASSET	"oDFe-windows-x86_64.zip"
#elif defined( __linux__ ) && defined( __x86_64__ )
#define UPD_ENGINE_ASSET	"oDFe-linux-x86_64.zip"
#endif

typedef enum {
	STAGE_NONE,
	STAGE_API,
	STAGE_HASH,
	STAGE_FILE
} updStage_t;

typedef struct updJob_s {
	const char	*what;			// for messages
	const char	*prefix;		// cvar name prefix
	const char	*asset;			// file name in the release, NULL: this build cannot be updated
	qboolean	hashed;			// the release has <asset>.sha512, the download must match it
	const char	*readyText;
	const char	*( *install )( const void *data, int len );	// NULL, or the reason it failed

	download_t	dl;				// only its function table is used
	qboolean	libLoaded;

	CURL		*easy;
	CURLM		*multi;
	updStage_t	stage;
	byte		*buf;
	size_t		len, cap, max;
	qboolean	overflow;
	double		dlTotal, dlNow;
	int			pct;

	qboolean	silent;			// the running request is a check: a failure is not shown to the player
	int			currentNum;		// installed version
	qboolean	haveLatest;
	char		tag[32];
	uint8_t		hash[UPD_HASH_SIZE];

	cvar_t		*state, *check, *repo;
} updJob_t;

static updJob_t	updMod, updEngine;

#define NUM_JOBS ( sizeof( updJobs ) / sizeof( updJobs[0] ) )
static updJob_t	*const updJobs[] = { &updMod, &updEngine };


static void Upd_SetState( updJob_t *u, const char *state )
{
	Cvar_Set( va( "%s_state", u->prefix ), state );
}


static void Upd_CloseHandles( updJob_t *u )
{
	if ( u->multi ) {
		if ( u->easy ) {
			u->dl.func.multi_remove_handle( u->multi, u->easy );
		}
		u->dl.func.multi_cleanup( u->multi );
		u->multi = NULL;
	}
	if ( u->easy ) {
		u->dl.func.easy_cleanup( u->easy );
		u->easy = NULL;
	}
	free( u->buf );
	u->buf = NULL;
	u->len = u->cap = 0;
	u->overflow = qfalse;
	u->stage = STAGE_NONE;
}


static void Upd_Release( updJob_t *u )
{
	Upd_CloseHandles( u );
	if ( u->libLoaded ) {
		Com_DL_Done( &u->dl );
		u->libLoaded = qfalse;
	}
}


static void Upd_Fail( updJob_t *u, const char *msg )
{
	Com_Printf( S_COLOR_RED "%s update: %s\n", u->what, msg );
	Cvar_Set( va( "%s_error", u->prefix ), msg );
	Upd_SetState( u, u->silent ? "checkfailed" : "error" );
	Upd_Release( u );
}


static size_t Upd_Write( void *ptr, size_t size, size_t nmemb, void *userdata )
{
	updJob_t *u = userdata;
	size_t n = size * nmemb;

	if ( n > u->max || u->len > u->max - n ) {
		u->overflow = qtrue;
		return 0;
	}
	if ( u->len + n + 1 > u->cap ) {
		size_t cap = u->cap ? u->cap : 16384;
		byte *nb;
		while ( cap < u->len + n + 1 ) {
			cap *= 2;
		}
		nb = realloc( u->buf, cap );
		if ( !nb ) {
			u->overflow = qtrue;
			return 0;
		}
		u->buf = nb;
		u->cap = cap;
	}
	memcpy( u->buf + u->len, ptr, n );
	u->len += n;
	u->buf[u->len] = '\0';
	return n;
}


#if CURL_AT_LEAST_VERSION(7, 32, 0)
static int Upd_Progress( void *data, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow )
#else
static int Upd_Progress( void *data, double dltotal, double dlnow, double ultotal, double ulnow )
#endif
{
	updJob_t *u = data;

	( void )ultotal; ( void )ulnow;
	u->dlTotal = ( double )dltotal;
	u->dlNow = ( double )dlnow;
	return 0;
}


static qboolean Upd_StartJob( updJob_t *u, updStage_t stage, const char *url, size_t max, long timeout )
{
	if ( !u->libLoaded ) {
		if ( !Com_DL_Init( &u->dl ) ) {
			Upd_Fail( u, "libcurl is not available" );
			return qfalse;
		}
		u->libLoaded = qtrue;
	}

	Upd_CloseHandles( u );
	u->stage = stage;
	u->max = max;
	u->dlTotal = u->dlNow = 0;

	u->easy = u->dl.func.easy_init();
	if ( !u->easy ) {
		Upd_Fail( u, "cannot start the download" );
		return qfalse;
	}

	u->dl.func.easy_setopt( u->easy, CURLOPT_URL, url );
	u->dl.func.easy_setopt( u->easy, CURLOPT_USERAGENT, "oDFe-updater" );
	u->dl.func.easy_setopt( u->easy, CURLOPT_WRITEFUNCTION, Upd_Write );
	u->dl.func.easy_setopt( u->easy, CURLOPT_WRITEDATA, u );
	u->dl.func.easy_setopt( u->easy, CURLOPT_NOPROGRESS, 0L );
#if CURL_AT_LEAST_VERSION(7, 32, 0)
	u->dl.func.easy_setopt( u->easy, CURLOPT_XFERINFOFUNCTION, Upd_Progress );
	u->dl.func.easy_setopt( u->easy, CURLOPT_XFERINFODATA, u );
#else
	u->dl.func.easy_setopt( u->easy, CURLOPT_PROGRESSFUNCTION, Upd_Progress );
	u->dl.func.easy_setopt( u->easy, CURLOPT_PROGRESSDATA, u );
#endif
	u->dl.func.easy_setopt( u->easy, CURLOPT_FAILONERROR, 1L );
	u->dl.func.easy_setopt( u->easy, CURLOPT_FOLLOWLOCATION, 1L );
	u->dl.func.easy_setopt( u->easy, CURLOPT_MAXREDIRS, 5L );
	u->dl.func.easy_setopt( u->easy, CURLOPT_NOSIGNAL, 1L );
	u->dl.func.easy_setopt( u->easy, CURLOPT_CONNECTTIMEOUT, 10L );
	u->dl.func.easy_setopt( u->easy, CURLOPT_LOW_SPEED_LIMIT, 1L );
	u->dl.func.easy_setopt( u->easy, CURLOPT_LOW_SPEED_TIME, 30L );
	if ( timeout ) {
		u->dl.func.easy_setopt( u->easy, CURLOPT_TIMEOUT, timeout );
	}
	// https only, also for redirects
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	u->dl.func.easy_setopt( u->easy, CURLOPT_PROTOCOLS_STR, "https" );
#else
	u->dl.func.easy_setopt( u->easy, CURLOPT_PROTOCOLS, ( long )CURLPROTO_HTTPS );
#endif

	u->multi = u->dl.func.multi_init();
	if ( !u->multi || u->dl.func.multi_add_handle( u->multi, u->easy ) != CURLM_OK ) {
		Upd_Fail( u, "cannot start the download" );
		return qfalse;
	}
	return qtrue;
}


static void Upd_SetProgress( updJob_t *u, int pct )
{
	if ( pct != u->pct ) {
		u->pct = pct;
		Cvar_SetIntegerValue( va( "%s_progress", u->prefix ), pct );
	}
}


// "v2.1.3" -> 20103 (major*10000 + minor*100 + patch). The tag ends up in a download URL, so it is rebuilt from the numbers.
static qboolean Upd_ParseTag( const char *text, int *num, char *tag, char *ver )
{
	int a, b, c, n = 0;

	if ( sscanf( text, "v%d.%d.%d%n", &a, &b, &c, &n ) != 3 || text[n] || a < 0 || b < 0 || c < 0 || a > 200 || b > 99 || c > 99 ) {
		return qfalse;
	}
	*num = a * 10000 + b * 100 + c;
	Com_sprintf( tag, 32, "v%d.%d.%d", a, b, c );
	Com_sprintf( ver, 16, "%d.%d.%d", a, b, c );
	return qtrue;
}


static void Upd_FinishApi( updJob_t *u )
{
	char name[32], tag[32], text[16];
	const char *p = strstr( ( const char * )u->buf, "\"tag_name\"" );
	int num;

	if ( !p || sscanf( p + 10, " : \"%31[^\"]\"", name ) != 1 || !Upd_ParseTag( name, &num, tag, text ) ) {
		Upd_Fail( u, "unrecognised release tag" );
		return;
	}

	Q_strncpyz( u->tag, tag, sizeof( u->tag ) );
	u->haveLatest = qtrue;
	Cvar_Set( va( "%s_latest", u->prefix ), text );
	Cvar_SetIntegerValue( va( "%s_latest_num", u->prefix ), num );
	Upd_Release( u );

	if ( num > u->currentNum ) {
		Com_Printf( "%s update: version %s is available (installed %i)\n", u->what, text, u->currentNum );
		Upd_SetState( u, "available" );
	} else {
		Com_Printf( "%s update: version %s is the latest\n", u->what, text );
		Upd_SetState( u, "uptodate" );
	}
}


static void Upd_FinishHash( updJob_t *u )
{
	char hex[UPD_HASH_SIZE * 2 + 1], url[MAX_OSPATH];

	// sha512sum output: 128 hex digits, then optionally " *file name"
	Q_strncpyz( hex, ( const char * )u->buf, sizeof( hex ) );
	if ( Auth_HexDecode( u->hash, UPD_HASH_SIZE, hex ) < 0 ) {
		Upd_Fail( u, "bad checksum file" );
		return;
	}

	Com_sprintf( url, sizeof( url ), "https://github.com/%s/releases/download/%s/%s", u->repo->string, u->tag, u->asset );
	Upd_StartJob( u, STAGE_FILE, url, UPD_FILE_MAX, 0 );
}


static void Upd_FinishFile( updJob_t *u )
{
	uint8_t hash[UPD_HASH_SIZE];
	const char *err;

	if ( u->hashed ) {
		crypto_sha512( hash, u->buf, u->len );
		if ( crypto_verify64( hash, u->hash ) ) {
			Upd_Fail( u, "checksum mismatch, the file was not installed" );
			return;
		}
	}
	err = u->install( u->buf, ( int )u->len );
	if ( err ) {
		Upd_Fail( u, err );
		return;
	}

	Upd_Release( u );
	Upd_SetProgress( u, 100 );
	Upd_SetState( u, "ready" );
	Com_Printf( S_COLOR_GREEN "%s update: downloaded, %s\n", u->what, u->readyText );
}


static void Upd_Frame( updJob_t *u )
{
	CURLMcode res = CURLM_OK;
	CURLMsg *msg;
	CURLcode result;
	long code = 0;
	int running, left, i;

	if ( !u->multi ) {
		return;
	}

	for ( i = 0; i < 16; i++ ) {
		res = u->dl.func.multi_perform( u->multi, &running );
		if ( res != CURLM_CALL_MULTI_PERFORM ) {
			break;
		}
	}
	if ( res != CURLM_OK && res != CURLM_CALL_MULTI_PERFORM ) {
		Upd_Fail( u, "network error" );
		return;
	}

	if ( u->stage == STAGE_FILE && u->dlTotal > 0 ) {
		Upd_SetProgress( u, ( int )( u->dlNow * 100.0 / u->dlTotal ) );
	}

	msg = u->dl.func.multi_info_read( u->multi, &left );
	if ( !msg || msg->msg != CURLMSG_DONE ) {
		return;
	}

	result = msg->data.result;
	u->dl.func.easy_getinfo( msg->easy_handle, CURLINFO_RESPONSE_CODE, &code );

	if ( result != CURLE_OK ) {
		char text[128];
		if ( u->overflow ) {
			Q_strncpyz( text, "the download is too large", sizeof( text ) );
		} else if ( code ) {
			Com_sprintf( text, sizeof( text ), "server answered %ld", code );
		} else {
			Com_sprintf( text, sizeof( text ), "%s", u->dl.func.easy_strerror( result ) );
		}
		Upd_Fail( u, text );
		return;
	}

	switch ( u->stage ) {
		case STAGE_API: Upd_FinishApi( u ); break;
		case STAGE_HASH: Upd_FinishHash( u ); break;
		case STAGE_FILE: Upd_FinishFile( u ); break;
		default: Upd_Release( u ); break;
	}
}


static void Upd_Check( updJob_t *u, int currentNum, qboolean autoCheck )
{
	char url[MAX_OSPATH];

	if ( !u->asset ) {
		Upd_SetState( u, "unsupported" );
		return;
	}
	if ( autoCheck && !u->check->integer ) {
		return;
	}
	if ( u->stage != STAGE_NONE || !Q_stricmp( u->state->string, "ready" ) ) {
		return;
	}
	u->currentNum = currentNum;
	u->silent = qtrue;

	Com_sprintf( url, sizeof( url ), "https://api.github.com/repos/%s/releases/latest", u->repo->string );

	Cvar_Set( va( "%s_error", u->prefix ), "" );
	Upd_SetState( u, "checking" );
	Upd_StartJob( u, STAGE_API, url, UPD_API_MAX, 20 );
}


static void Upd_Update( updJob_t *u )
{
	char url[MAX_OSPATH];

	if ( u->stage != STAGE_NONE ) {
		return;
	}
	if ( !u->haveLatest || ( Q_stricmp( u->state->string, "available" ) && Q_stricmp( u->state->string, "error" ) ) ) {
		Com_Printf( "%s update: nothing to download, run the check command first\n", u->what );
		return;
	}
	Com_sprintf( url, sizeof( url ), "https://github.com/%s/releases/download/%s/%s%s",
		u->repo->string, u->tag, u->asset, u->hashed ? ".sha512" : "" );

	u->silent = qfalse;
	u->pct = -1;
	Upd_SetProgress( u, 0 );
	Cvar_Set( va( "%s_error", u->prefix ), "" );
	Upd_SetState( u, "downloading" );
	if ( u->hashed ) {
		Upd_StartJob( u, STAGE_HASH, url, UPD_HASH_MAX, 20 );
	} else {
		Upd_StartJob( u, STAGE_FILE, url, UPD_FILE_MAX, 0 );
	}
}


static void Upd_CheckMod_f( void )
{
	Upd_Check( &updMod, atoi( Cmd_Argv( 1 ) ), Cmd_Argc() > 2 && !Q_stricmp( Cmd_Argv( 2 ), "auto" ) );
}


static void Upd_CheckEngine_f( void )
{
	Upd_Check( &updEngine, ODFE_VERSION_NUMBER, Cmd_Argc() > 1 && !Q_stricmp( Cmd_Argv( 1 ), "auto" ) );
}


static void Upd_UpdateMod_f( void )
{
	Upd_Update( &updMod );
}


static void Upd_UpdateEngine_f( void )
{
	Upd_Update( &updEngine );
}


static const char *Upd_InstallMod( const void *data, int len )
{
	if ( len < 4 || memcmp( data, "PK\x03\x04", 4 ) ) {
		return "the downloaded file is not a pk3";
	}
	return FS_WriteUpdatePak( data, len ) ? NULL : "cannot write the update file";
}


// next to the running executable
static void Upd_Path( char *out, size_t size, const char *name, const char *ext )
{
	Com_sprintf( out, size, "%s%c%s%s", Sys_Pwd(), PATH_SEP, name, ext );
}


static qboolean Upd_Exists( const char *path )
{
	FILE *f = Sys_FOpen( path, "rb" );

	if ( f ) {
		fclose( f );
		return qtrue;
	}
	return qfalse;
}


// the zip reader of the engine does not check the CRC of what it unpacks
static unsigned long Upd_Crc32( const byte *data, int len )
{
	unsigned long crc = 0xFFFFFFFFul;
	int i, k;

	for ( i = 0; i < len; i++ ) {
		crc ^= data[i];
		for ( k = 0; k < 8; k++ ) {
			crc = ( crc >> 1 ) ^ ( 0xEDB88320ul & ( 0ul - ( crc & 1 ) ) );
		}
	}
	return ~crc & 0xFFFFFFFFul;
}


static qboolean Upd_IsExecutable( const byte *data, int len )
{
#ifdef _WIN32
	return len >= 2 && data[0] == 'M' && data[1] == 'Z';
#else
	return len >= 4 && !memcmp( data, "\x7f" "ELF", 4 );
#endif
}


/*
All oDFe* files of the zip (OpenGL, Vulkan and dedicated server builds) replace the files of that name next to the running
executable, or are added. A running executable cannot be overwritten on Windows but it can be renamed: the old file becomes
<name>.old and is removed by Upd_CleanupEngine() at a later start. Either all files are installed or none.
*/
static const char *Upd_InstallEngine( const void *data, int len )
{
	char names[UPD_MAX_FILES][64], zipPath[MAX_OSPATH], path[MAX_OSPATH], old[MAX_OSPATH], nw[MAX_OSPATH];
	qboolean hadOld[UPD_MAX_FILES];
	const char *err = NULL;
	unzFile zip;
	FILE *f;
	int count = 0, swapped = 0, rc, i;

	Upd_Path( zipPath, sizeof( zipPath ), "oDFe-update.zip", ".tmp" );
	f = Sys_FOpen( zipPath, "wb" );
	if ( !f ) {
		return "cannot write next to the executable (is the folder writable?)";
	}
	if ( fwrite( data, 1, len, f ) != ( size_t )len || fclose( f ) ) {
		remove( zipPath );
		return "cannot write the update (disk full?)";
	}
	zip = unzOpen( zipPath );
	if ( !zip ) {
		remove( zipPath );
		return "the downloaded file is not a zip";
	}

	// every engine file of the zip is checked and written as <name>.new
	for ( rc = unzGoToFirstFile( zip ); rc == UNZ_OK && !err; rc = unzGoToNextFile( zip ) ) {
		unz_file_info info;
		char name[64];
		byte *buf;
		int n = -1;

		if ( unzGetCurrentFileInfo( zip, &info, name, sizeof( name ), NULL, 0, NULL, 0 ) != UNZ_OK ) {
			err = "damaged zip";
		} else if ( info.size_filename >= sizeof( name ) || Q_stricmpn( name, "oDFe", 4 ) || strpbrk( name, "/\\:" ) ) {
			continue;	// a folder or another file
		} else if ( count == UPD_MAX_FILES || info.uncompressed_size < 4 || info.uncompressed_size > UPD_FILE_MAX ) {
			err = "unexpected content of the zip";
		} else if ( !( buf = malloc( info.uncompressed_size ) ) ) {
			err = "out of memory";
		} else {
			if ( unzOpenCurrentFile( zip ) == UNZ_OK ) {
				n = unzReadCurrentFile( zip, buf, info.uncompressed_size );
				unzCloseCurrentFile( zip );
			}
			if ( n != ( int )info.uncompressed_size || Upd_Crc32( buf, n ) != ( info.crc & 0xFFFFFFFFul ) ) {
				err = "damaged zip";
			} else if ( !Upd_IsExecutable( buf, n ) ) {
				err = "the zip holds a file that is not an executable";
			} else {
				Q_strncpyz( names[count++], name, sizeof( names[0] ) );
				Upd_Path( nw, sizeof( nw ), name, ".new" );
				f = Sys_FOpen( nw, "wb" );
				if ( !f || fwrite( buf, 1, n, f ) != ( size_t )n ) {
					err = "cannot write next to the executable (is the folder writable?)";
				}
				if ( f && fclose( f ) && !err ) {
					err = "cannot write the update (disk full?)";
				}
#ifndef _WIN32
				chmod( nw, 0755 );
#endif
			}
			free( buf );
		}
	}
	unzClose( zip );
	remove( zipPath );
	if ( !err && !count ) {
		err = "the zip has no oDFe files";
	}

	// the old files move aside, the new ones take their names
	for ( i = 0; i < count && !err; i++ ) {
		Upd_Path( path, sizeof( path ), names[i], "" );
		Upd_Path( old, sizeof( old ), names[i], ".old" );
		Upd_Path( nw, sizeof( nw ), names[i], ".new" );
		remove( old );
		hadOld[i] = rename( path, old ) == 0;
		if ( !hadOld[i] && Upd_Exists( path ) ) {
			err = "cannot rename the running executable (close other copies of the game)";
		} else if ( rename( nw, path ) ) {
			err = "cannot put the new file in place";
			if ( hadOld[i] ) {
				rename( old, path );
			}
		} else {
			swapped++;
		}
	}

	if ( err ) {
		for ( i = 0; i < swapped; i++ ) {	// back to how it was
			Upd_Path( path, sizeof( path ), names[i], "" );
			Upd_Path( old, sizeof( old ), names[i], ".old" );
			remove( path );
			if ( hadOld[i] ) {
				rename( old, path );
			}
		}
		for ( i = 0; i < count; i++ ) {
			Upd_Path( nw, sizeof( nw ), names[i], ".new" );
			remove( nw );
		}
	}
	return err;
}


#ifdef UPD_ENGINE_ASSET
// what an earlier update left behind (best effort, a file stays while another copy still runs it)
static void Upd_CleanupEngine( void )
{
	static const char *const ext[] = { ".old", ".new" };
	char path[MAX_OSPATH];
	int e, i, n = 0;

	for ( e = 0; e < 2; e++ ) {
		char **list = Sys_ListFiles( Sys_Pwd(), ext[e], NULL, &n, qfalse );

		for ( i = 0; i < n; i++ ) {
			if ( !Q_stricmpn( list[i], "oDFe", 4 ) ) {
				Upd_Path( path, sizeof( path ), list[i], "" );
				remove( path );
			}
		}
		if ( list ) {
			Sys_FreeFileList( list );
		}
	}
	Upd_Path( path, sizeof( path ), "oDFe-update.zip", ".tmp" );
	remove( path );
}
#endif


static void Upd_InitJob( updJob_t *u, const char *repo, const char *checkHelp, const char *repoHelp )
{
	const char *prefix = u->prefix;

	u->state = Cvar_Get( va( "%s_state", prefix ), "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( va( "%s_latest", prefix ), "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( va( "%s_latest_num", prefix ), "0", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( va( "%s_progress", prefix ), "0", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( va( "%s_error", prefix ), "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	u->check = Cvar_Get( va( "%s_check", prefix ), "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( u->check, checkHelp );
	u->repo = Cvar_Get( va( "%s_repo", prefix ), repo, CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( u->repo, repoHelp );
}


void CL_Update_Init( void )
{
	updMod.what = "mod";
	updMod.prefix = "odfs_update";
	updMod.asset = UPD_PAK_NAME;
	updMod.hashed = qtrue;
	updMod.readyText = "it will be installed at the next start";
	updMod.install = Upd_InstallMod;
	Upd_InitJob( &updMod, "Mix-Anik/oDFs",
		"Look for a new oDFs release on GitHub when the main menu opens (0 = never, the Update button then does nothing until odfs_checkupdate is run by hand).",
		"GitHub repository (owner/name) the mod updater takes releases from." );

	updEngine.what = "engine";
	updEngine.prefix = "odfe_update";
#ifdef UPD_ENGINE_ASSET
	updEngine.asset = UPD_ENGINE_ASSET;
#endif
	updEngine.readyText = "restart the game to use it";
	updEngine.install = Upd_InstallEngine;
	Upd_InitJob( &updEngine, "Mix-Anik/oDFe-upstream",
		"Look for a new oDFe release on GitHub at startup (0 = never, the Update button then does nothing until odfe_checkupdate is run by hand).",
		"GitHub repository (owner/name) the engine updater takes releases from." );

	Cvar_Get( "odfe_version", ODFE_VERSION_STRING, CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( "odfe_version_num", va( "%i", ODFE_VERSION_NUMBER ), CVAR_ROM | CVAR_NOTABCOMPLETE );

	Cmd_AddCommand( "odfs_checkupdate", Upd_CheckMod_f );
	Cmd_AddCommand( "odfs_update", Upd_UpdateMod_f );
	Cmd_AddCommand( "odfe_checkupdate", Upd_CheckEngine_f );
	Cmd_AddCommand( "odfe_update", Upd_UpdateEngine_f );

#ifdef UPD_ENGINE_ASSET
	Upd_CleanupEngine();
#endif
}


void CL_Update_Frame( void )
{
	static qboolean engineChecked;
	int i;

	// the engine knows its version, so it looks by itself, once
	if ( !engineChecked ) {
		engineChecked = qtrue;
		Upd_Check( &updEngine, ODFE_VERSION_NUMBER, qtrue );
	}
	for ( i = 0; i < NUM_JOBS; i++ ) {
		Upd_Frame( updJobs[i] );
	}
}


void CL_Update_Shutdown( void )
{
	int i;

	for ( i = 0; i < NUM_JOBS; i++ ) {
		Upd_Release( updJobs[i] );
	}
	Cmd_RemoveCommand( "odfs_checkupdate" );
	Cmd_RemoveCommand( "odfs_update" );
	Cmd_RemoveCommand( "odfe_checkupdate" );
	Cmd_RemoveCommand( "odfe_update" );
}

#else // USE_CURL

static void Upd_Unavailable_f( void )
{
	Cvar_Set( "odfs_update_error", "this build has no libcurl" );
	Cvar_Set( "odfs_update_state", "error" );
	Cvar_Set( "odfe_update_error", "this build has no libcurl" );
	Cvar_Set( "odfe_update_state", "error" );
}

void CL_Update_Frame( void ) {}

void CL_Update_Init( void )
{
	Cvar_Get( "odfs_update_state", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( "odfs_update_error", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( "odfe_update_state", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( "odfe_update_error", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( "odfe_version", ODFE_VERSION_STRING, CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cmd_AddCommand( "odfs_checkupdate", Upd_Unavailable_f );
	Cmd_AddCommand( "odfs_update", Upd_Unavailable_f );
	Cmd_AddCommand( "odfe_checkupdate", Upd_Unavailable_f );
	Cmd_AddCommand( "odfe_update", Upd_Unavailable_f );
}

void CL_Update_Shutdown( void )
{
	Cmd_RemoveCommand( "odfs_checkupdate" );
	Cmd_RemoveCommand( "odfs_update" );
	Cmd_RemoveCommand( "odfe_checkupdate" );
	Cmd_RemoveCommand( "odfe_update" );
}

#endif
