/*
oDFs mod updater: asks GitHub for the latest release, downloads zzz-defrag_vm.pk3 of it, checks its SHA-512 and leaves it
next to the installed pk3 as zzz-defrag_vm.pk3.new. files.c swaps it in at the next start, before any pk3 is opened (a
loaded pk3 is locked on Windows).

The mod menu drives this with console commands and reads the result from cvars:
	odfs_checkupdate <installed version number> [auto]	[auto] does nothing when odfs_update_check is 0
	odfs_update											download the release found by the last check
	odfs_update_state		"" | checking | uptodate | available | downloading | ready | error | checkfailed
	odfs_update_latest		"2.1.0"
	odfs_update_latest_num	20100
	odfs_update_progress	0..100 while downloading
	odfs_update_error		reason when the state is error or checkfailed (a failed check is not shown by the menu)

The repository is odfs_update_repo (startup only), the download URL is built from it, the tag and the fixed pk3 name, so
nothing the server or the API sends decides where a file comes from.
*/

#include "client.h"
#include "../qcommon/auth_proto.h"
#include "../qcommon/monocypher.h"
#include "../qcommon/monocypher-ed25519.h"

#ifdef USE_CURL

#define UPD_HASH_SIZE		64		// SHA-512
#define UPD_API_MAX		( 512 * 1024 )
#define UPD_HASH_MAX		4096
#define UPD_PAK_MAX		( 64 * 1024 * 1024 )

typedef enum {
	STAGE_NONE,
	STAGE_API,
	STAGE_HASH,
	STAGE_PAK
} updStage_t;

static struct {
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
	int			currentNum;		// installed version, from the menu
	qboolean	haveLatest;
	char		tag[32];
	int			latestNum;
	uint8_t		hash[UPD_HASH_SIZE];
} upd;

static cvar_t	*upd_state, *upd_latest, *upd_latestNum, *upd_progress, *upd_error, *upd_check, *upd_repo;


static void Upd_CloseHandles( void )
{
	if ( upd.multi ) {
		if ( upd.easy ) {
			upd.dl.func.multi_remove_handle( upd.multi, upd.easy );
		}
		upd.dl.func.multi_cleanup( upd.multi );
		upd.multi = NULL;
	}
	if ( upd.easy ) {
		upd.dl.func.easy_cleanup( upd.easy );
		upd.easy = NULL;
	}
	free( upd.buf );
	upd.buf = NULL;
	upd.len = upd.cap = 0;
	upd.overflow = qfalse;
	upd.stage = STAGE_NONE;
}


static void Upd_Release( void )
{
	Upd_CloseHandles();
	if ( upd.libLoaded ) {
		Com_DL_Done( &upd.dl );
		upd.libLoaded = qfalse;
	}
}


static void Upd_Fail( const char *msg )
{
	Com_Printf( S_COLOR_RED "mod update: %s\n", msg );
	Cvar_Set( "odfs_update_error", msg );
	Cvar_Set( "odfs_update_state", upd.silent ? "checkfailed" : "error" );
	Upd_Release();
}


static size_t Upd_Write( void *ptr, size_t size, size_t nmemb, void *userdata )
{
	size_t n = size * nmemb;

	( void )userdata;
	if ( n > upd.max || upd.len > upd.max - n ) {
		upd.overflow = qtrue;
		return 0;
	}
	if ( upd.len + n + 1 > upd.cap ) {
		size_t cap = upd.cap ? upd.cap : 16384;
		byte *nb;
		while ( cap < upd.len + n + 1 ) {
			cap *= 2;
		}
		nb = realloc( upd.buf, cap );
		if ( !nb ) {
			upd.overflow = qtrue;
			return 0;
		}
		upd.buf = nb;
		upd.cap = cap;
	}
	memcpy( upd.buf + upd.len, ptr, n );
	upd.len += n;
	upd.buf[upd.len] = '\0';
	return n;
}


#if CURL_AT_LEAST_VERSION(7, 32, 0)
static int Upd_Progress( void *data, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow )
#else
static int Upd_Progress( void *data, double dltotal, double dlnow, double ultotal, double ulnow )
#endif
{
	( void )data; ( void )ultotal; ( void )ulnow;
	upd.dlTotal = ( double )dltotal;
	upd.dlNow = ( double )dlnow;
	return 0;
}


static qboolean Upd_StartJob( updStage_t stage, const char *url, size_t max, long timeout )
{
	if ( !upd.libLoaded ) {
		if ( !Com_DL_Init( &upd.dl ) ) {
			Upd_Fail( "libcurl is not available" );
			return qfalse;
		}
		upd.libLoaded = qtrue;
	}

	Upd_CloseHandles();
	upd.stage = stage;
	upd.max = max;
	upd.dlTotal = upd.dlNow = 0;

	upd.easy = upd.dl.func.easy_init();
	if ( !upd.easy ) {
		Upd_Fail( "cannot start the download" );
		return qfalse;
	}

	upd.dl.func.easy_setopt( upd.easy, CURLOPT_URL, url );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_USERAGENT, "oDFe-mod-updater" );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_WRITEFUNCTION, Upd_Write );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_NOPROGRESS, 0L );
#if CURL_AT_LEAST_VERSION(7, 32, 0)
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_XFERINFOFUNCTION, Upd_Progress );
#else
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_PROGRESSFUNCTION, Upd_Progress );
#endif
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_FAILONERROR, 1L );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_FOLLOWLOCATION, 1L );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_MAXREDIRS, 5L );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_NOSIGNAL, 1L );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_CONNECTTIMEOUT, 10L );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_LOW_SPEED_LIMIT, 1L );
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_LOW_SPEED_TIME, 30L );
	if ( timeout ) {
		upd.dl.func.easy_setopt( upd.easy, CURLOPT_TIMEOUT, timeout );
	}
	// https only, also for redirects
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_PROTOCOLS_STR, "https" );
#else
	upd.dl.func.easy_setopt( upd.easy, CURLOPT_PROTOCOLS, ( long )CURLPROTO_HTTPS );
#endif

	upd.multi = upd.dl.func.multi_init();
	if ( !upd.multi || upd.dl.func.multi_add_handle( upd.multi, upd.easy ) != CURLM_OK ) {
		Upd_Fail( "cannot start the download" );
		return qfalse;
	}
	return qtrue;
}


static void Upd_SetProgress( int pct )
{
	if ( pct != upd.pct ) {
		upd.pct = pct;
		Cvar_SetIntegerValue( "odfs_update_progress", pct );
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


static void Upd_FinishApi( void )
{
	char name[32], tag[32], text[16];
	const char *p = strstr( ( const char * )upd.buf, "\"tag_name\"" );
	int num;

	if ( !p || sscanf( p + 10, " : \"%31[^\"]\"", name ) != 1 || !Upd_ParseTag( name, &num, tag, text ) ) {
		Upd_Fail( "unrecognised release tag" );
		return;
	}

	Q_strncpyz( upd.tag, tag, sizeof( upd.tag ) );
	upd.latestNum = num;
	upd.haveLatest = qtrue;
	Cvar_Set( "odfs_update_latest", text );
	Cvar_SetIntegerValue( "odfs_update_latest_num", num );
	Upd_Release();

	if ( num > upd.currentNum ) {
		Com_Printf( "mod update: version %s is available (installed %i)\n", text, upd.currentNum );
		Cvar_Set( "odfs_update_state", "available" );
	} else {
		Com_Printf( "mod update: version %s is the latest\n", text );
		Cvar_Set( "odfs_update_state", "uptodate" );
	}
}


static void Upd_FinishHash( void )
{
	char hex[UPD_HASH_SIZE * 2 + 1], url[MAX_OSPATH];

	// sha512sum output: 128 hex digits, then optionally " *file name"
	Q_strncpyz( hex, ( const char * )upd.buf, sizeof( hex ) );
	if ( Auth_HexDecode( upd.hash, UPD_HASH_SIZE, hex ) < 0 ) {
		Upd_Fail( "bad checksum file" );
		return;
	}

	Com_sprintf( url, sizeof( url ), "https://github.com/%s/releases/download/%s/%s", upd_repo->string, upd.tag, UPD_PAK_NAME );
	Upd_StartJob( STAGE_PAK, url, UPD_PAK_MAX, 0 );
}


static void Upd_FinishPak( void )
{
	uint8_t hash[UPD_HASH_SIZE];

	if ( upd.len < 4 || memcmp( upd.buf, "PK\x03\x04", 4 ) ) {
		Upd_Fail( "the downloaded file is not a pk3" );
		return;
	}
	crypto_sha512( hash, upd.buf, upd.len );
	if ( crypto_verify64( hash, upd.hash ) ) {
		Upd_Fail( "checksum mismatch, the file was not installed" );
		return;
	}
	if ( !FS_WriteUpdatePak( upd.buf, ( int )upd.len ) ) {
		Upd_Fail( "cannot write the update file" );
		return;
	}

	Upd_Release();
	Upd_SetProgress( 100 );
	Cvar_Set( "odfs_update_state", "ready" );
	Com_Printf( S_COLOR_GREEN "mod update: downloaded, it will be installed at the next start\n" );
}


void CL_Update_Frame( void )
{
	CURLMcode res = CURLM_OK;
	CURLMsg *msg;
	CURLcode result;
	long code = 0;
	int running, left, i;

	if ( !upd.multi ) {
		return;
	}

	for ( i = 0; i < 16; i++ ) {
		res = upd.dl.func.multi_perform( upd.multi, &running );
		if ( res != CURLM_CALL_MULTI_PERFORM ) {
			break;
		}
	}
	if ( res != CURLM_OK && res != CURLM_CALL_MULTI_PERFORM ) {
		Upd_Fail( "network error" );
		return;
	}

	if ( upd.stage == STAGE_PAK && upd.dlTotal > 0 ) {
		Upd_SetProgress( ( int )( upd.dlNow * 100.0 / upd.dlTotal ) );
	}

	msg = upd.dl.func.multi_info_read( upd.multi, &left );
	if ( !msg || msg->msg != CURLMSG_DONE ) {
		return;
	}

	result = msg->data.result;
	upd.dl.func.easy_getinfo( msg->easy_handle, CURLINFO_RESPONSE_CODE, &code );

	if ( result != CURLE_OK ) {
		char text[128];
		if ( upd.overflow ) {
			Q_strncpyz( text, "the download is too large", sizeof( text ) );
		} else if ( code ) {
			Com_sprintf( text, sizeof( text ), "server answered %ld", code );
		} else {
			Com_sprintf( text, sizeof( text ), "%s", upd.dl.func.easy_strerror( result ) );
		}
		Upd_Fail( text );
		return;
	}

	switch ( upd.stage ) {
		case STAGE_API: Upd_FinishApi(); break;
		case STAGE_HASH: Upd_FinishHash(); break;
		case STAGE_PAK: Upd_FinishPak(); break;
		default: Upd_Release(); break;
	}
}


static qboolean Upd_Busy( void )
{
	return upd.stage != STAGE_NONE;
}


static void Upd_Check_f( void )
{
	char url[MAX_OSPATH];

	if ( Cmd_Argc() > 2 && !Q_stricmp( Cmd_Argv( 2 ), "auto" ) && !upd_check->integer ) {
		return;
	}
	if ( Upd_Busy() || !Q_stricmp( upd_state->string, "ready" ) ) {
		return;
	}
	upd.currentNum = atoi( Cmd_Argv( 1 ) );
	upd.silent = qtrue;

	Com_sprintf( url, sizeof( url ), "https://api.github.com/repos/%s/releases/latest", upd_repo->string );

	Cvar_Set( "odfs_update_error", "" );
	Cvar_Set( "odfs_update_state", "checking" );
	Upd_StartJob( STAGE_API, url, UPD_API_MAX, 20 );
}


static void Upd_Update_f( void )
{
	char url[MAX_OSPATH];

	if ( Upd_Busy() ) {
		return;
	}
	if ( !upd.haveLatest || ( Q_stricmp( upd_state->string, "available" ) && Q_stricmp( upd_state->string, "error" ) ) ) {
		Com_Printf( "mod update: nothing to download, run odfs_checkupdate first\n" );
		return;
	}
	Com_sprintf( url, sizeof( url ), "https://github.com/%s/releases/download/%s/%s.sha512", upd_repo->string, upd.tag, UPD_PAK_NAME );

	upd.silent = qfalse;
	upd.pct = -1;
	Upd_SetProgress( 0 );
	Cvar_Set( "odfs_update_error", "" );
	Cvar_Set( "odfs_update_state", "downloading" );
	Upd_StartJob( STAGE_HASH, url, UPD_HASH_MAX, 20 );
}


void CL_Update_Init( void )
{
	upd_state = Cvar_Get( "odfs_update_state", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	upd_latest = Cvar_Get( "odfs_update_latest", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	upd_latestNum = Cvar_Get( "odfs_update_latest_num", "0", CVAR_ROM | CVAR_NOTABCOMPLETE );
	upd_progress = Cvar_Get( "odfs_update_progress", "0", CVAR_ROM | CVAR_NOTABCOMPLETE );
	upd_error = Cvar_Get( "odfs_update_error", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	upd_check = Cvar_Get( "odfs_update_check", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( upd_check, "Look for a new oDFs release on GitHub when the main menu opens (0 = never, the Update button then does nothing until odfs_checkupdate is run by hand)." );
	upd_repo = Cvar_Get( "odfs_update_repo", "Mix-Anik/oDFs", CVAR_INIT | CVAR_PROTECTED );
	Cvar_SetDescription( upd_repo, "GitHub repository (owner/name) the mod updater takes releases from." );

	Cmd_AddCommand( "odfs_checkupdate", Upd_Check_f );
	Cmd_AddCommand( "odfs_update", Upd_Update_f );
}


void CL_Update_Shutdown( void )
{
	Upd_Release();
	Cmd_RemoveCommand( "odfs_checkupdate" );
	Cmd_RemoveCommand( "odfs_update" );
}

#else // USE_CURL

static void Upd_Unavailable_f( void )
{
	Cvar_Set( "odfs_update_error", "this build has no libcurl" );
	Cvar_Set( "odfs_update_state", "error" );
}

void CL_Update_Frame( void ) {}

void CL_Update_Init( void )
{
	Cvar_Get( "odfs_update_state", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cvar_Get( "odfs_update_error", "", CVAR_ROM | CVAR_NOTABCOMPLETE );
	Cmd_AddCommand( "odfs_checkupdate", Upd_Unavailable_f );
	Cmd_AddCommand( "odfs_update", Upd_Unavailable_f );
}

void CL_Update_Shutdown( void )
{
	Cmd_RemoveCommand( "odfs_checkupdate" );
	Cmd_RemoveCommand( "odfs_update" );
}

#endif
