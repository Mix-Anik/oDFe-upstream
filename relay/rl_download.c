/*
===========================================================================
rl_download.c -- downloads the map of the remote server

The same thing the "download" / "dlmap" commands of the client do: the pk3 of the map comes from the URLs in the cvars dl_source
and dl_source2 (%m is the name of the map), the first one is tried first, the second one is the fallback. It is a small copy
of the download code of the client (cl_curl.c), which depends on the client and so is not part of the relay.

What the remote server calls a map is not to be trusted: the name is checked, the file has to be a zip, can not be bigger than
RL_DL_MAX_SIZE and has to contain the map, or it is deleted again.
===========================================================================
*/

#include "rl_local.h"

#ifdef USE_CURL

#ifdef USE_LOCAL_HEADERS
#include "curl/curl.h"
#else
#include <curl/curl.h>
#endif

#define RL_DL_MAX_SIZE		( 256 * 1024 * 1024 )
#define RL_DL_MAX_SOURCES	2

#ifdef USE_CURL_DLOPEN
#ifdef _WIN32
#define RL_CURL_LIB			"libcurl-3.dll"
#else
#define RL_CURL_LIB			"libcurl.so.4"
#endif
#endif

typedef struct {
	char		*(*easy_escape)( CURL *curl, const char *string, int length );
	void		(*free)( char *ptr );
	CURL		*(*easy_init)( void );
	CURLcode	(*easy_setopt)( CURL *curl, CURLoption option, ... );
	void		(*easy_cleanup)( CURL *curl );
	CURLcode	(*easy_getinfo)( CURL *curl, CURLINFO info, ... );
	const char	*(*easy_strerror)( CURLcode );
	CURLM		*(*multi_init)( void );
	CURLMcode	(*multi_add_handle)( CURLM *multi_handle, CURL *curl_handle );
	CURLMcode	(*multi_remove_handle)( CURLM *multi_handle, CURL *curl_handle );
	CURLMcode	(*multi_perform)( CURLM *multi_handle, int *running_handles );
	CURLMcode	(*multi_cleanup)( CURLM *multi_handle );
	CURLMsg		*(*multi_info_read)( CURLM *multi_handle, int *msgs_in_queue );
	void		*lib;
} rlCurl_t;

static struct {
	qboolean	running;
	char		map[MAX_QPATH];
	char		name[MAX_QPATH];		// the pk3, without extension
	char		gameDir[MAX_QPATH];
	char		tempName[MAX_OSPATH];
	char		url[MAX_OSPATH];
	char		error[256];				// why the last try failed
	int			source;					// 0, 1: dl_source, dl_source2
	int			size;
	int			count;
	int			lastReport;
	qboolean	tooBig;
	fileHandle_t file;
	CURL		*easy;
	CURLM		*multi;
	rlCurl_t	f;
} dl;

static cvar_t	*dl_source;
static cvar_t	*dl_source2;
static cvar_t	*dl_usebaseq3;

// what the client has by default
#define RL_DL_DEFAULT_SOURCE	"http://ws.q3df.org/maps/download/%m"
#define RL_DL_DEFAULT_SOURCE2	"https://defrag.racing/maps/download/%m"


static qboolean RL_Dl_LoadCurl( void )
{
#ifdef USE_CURL_DLOPEN
	dl.f.lib = Sys_LoadLibrary( RL_CURL_LIB );
	if ( !dl.f.lib ) {
		return qfalse;
	}
	Sys_LoadFunctionErrors();
	dl.f.easy_escape = Sys_LoadFunction( dl.f.lib, "curl_easy_escape" );
	dl.f.free = Sys_LoadFunction( dl.f.lib, "curl_free" );
	dl.f.easy_init = Sys_LoadFunction( dl.f.lib, "curl_easy_init" );
	dl.f.easy_setopt = Sys_LoadFunction( dl.f.lib, "curl_easy_setopt" );
	dl.f.easy_cleanup = Sys_LoadFunction( dl.f.lib, "curl_easy_cleanup" );
	dl.f.easy_getinfo = Sys_LoadFunction( dl.f.lib, "curl_easy_getinfo" );
	dl.f.easy_strerror = Sys_LoadFunction( dl.f.lib, "curl_easy_strerror" );
	dl.f.multi_init = Sys_LoadFunction( dl.f.lib, "curl_multi_init" );
	dl.f.multi_add_handle = Sys_LoadFunction( dl.f.lib, "curl_multi_add_handle" );
	dl.f.multi_remove_handle = Sys_LoadFunction( dl.f.lib, "curl_multi_remove_handle" );
	dl.f.multi_perform = Sys_LoadFunction( dl.f.lib, "curl_multi_perform" );
	dl.f.multi_cleanup = Sys_LoadFunction( dl.f.lib, "curl_multi_cleanup" );
	dl.f.multi_info_read = Sys_LoadFunction( dl.f.lib, "curl_multi_info_read" );
	if ( Sys_LoadFunctionErrors() ) {
		Sys_UnloadLibrary( dl.f.lib );
		Com_Memset( &dl.f, 0, sizeof( dl.f ) );
		return qfalse;
	}
	return qtrue;
#else
	dl.f.easy_escape = curl_easy_escape;
	dl.f.free = (void (*)( char * ))curl_free;
	dl.f.easy_init = curl_easy_init;
	dl.f.easy_setopt = curl_easy_setopt;
	dl.f.easy_cleanup = curl_easy_cleanup;
	dl.f.easy_getinfo = curl_easy_getinfo;
	dl.f.easy_strerror = curl_easy_strerror;
	dl.f.multi_init = curl_multi_init;
	dl.f.multi_add_handle = curl_multi_add_handle;
	dl.f.multi_remove_handle = curl_multi_remove_handle;
	dl.f.multi_perform = curl_multi_perform;
	dl.f.multi_cleanup = curl_multi_cleanup;
	dl.f.multi_info_read = curl_multi_info_read;
	return qtrue;
#endif
}

static void RL_Dl_UnloadCurl( void )
{
#ifdef USE_CURL_DLOPEN
	if ( dl.f.lib ) {
		Sys_UnloadLibrary( dl.f.lib );
	}
#endif
	Com_Memset( &dl.f, 0, sizeof( dl.f ) );
}

// stops the transfer, removes what is left of it
static void RL_Dl_Cleanup( void )
{
	if ( dl.multi ) {
		if ( dl.easy ) {
			dl.f.multi_remove_handle( dl.multi, dl.easy );
		}
		dl.f.multi_cleanup( dl.multi );
		dl.multi = NULL;
	}
	if ( dl.easy ) {
		dl.f.easy_cleanup( dl.easy );
		dl.easy = NULL;
	}
	if ( dl.file != FS_INVALID_HANDLE ) {
		FS_FCloseFile( dl.file );
		dl.file = FS_INVALID_HANDLE;
	}
	if ( dl.tempName[0] ) {
		FS_Remove( dl.tempName );
		dl.tempName[0] = '\0';
	}
	dl.running = qfalse;
}

// a map name the remote server gave us: letters, digits and a few more, nothing that is a path
static qboolean RL_Dl_ValidName( const char *name )
{
	int i, c;

	if ( !name[0] || strlen( name ) >= MAX_QPATH - 8 ) {
		return qfalse;
	}
	for ( i = 0; ( c = name[i] ) != '\0'; i++ ) {
		if ( !( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) ||
			c == '_' || c == '-' || c == '+' || c == '.' || c == '!' || c == '#' || c == '~' ) ) {
			return qfalse;
		}
	}
	return name[0] != '.';
}

static size_t RL_Dl_Write( void *ptr, size_t size, size_t nmemb, void *userdata )
{
	const byte *data = ptr;
	size_t n = size * nmemb;

	if ( dl.file == FS_INVALID_HANDLE ) {
		// a pk3 is a zip: local file header or an empty archive
		if ( n < 22 || data[0] != 'P' || data[1] != 'K' || !( ( data[2] == 3 && data[3] == 4 ) || ( data[2] == 5 && data[3] == 6 ) ) ) {
			Q_strncpyz( dl.error, "the answer is not a pk3", sizeof( dl.error ) );
			return (size_t)-1;
		}
		dl.file = FS_SV_FOpenFileWrite( dl.tempName );
		if ( dl.file == FS_INVALID_HANDLE ) {
			Q_strncpyz( dl.error, "can not write the file", sizeof( dl.error ) );
			return (size_t)-1;
		}
	}

	if ( dl.count + (int)n > RL_DL_MAX_SIZE ) {
		dl.tooBig = qtrue;
		Q_strncpyz( dl.error, "the file is too big", sizeof( dl.error ) );
		return (size_t)-1;
	}

	FS_Write( ptr, n, dl.file );
	return n;
}

// the name of the pk3 comes with the answer: Content-Disposition: attachment; filename="name.pk3"
static size_t RL_Dl_Header( void *ptr, size_t size, size_t nmemb, void *userdata )
{
	char header[1024], name[MAX_QPATH];
	const char *s;
	char quote;
	int len = 0;

	if ( size * nmemb >= sizeof( header ) ) {
		return size * nmemb;	// not a header we care about
	}
	Com_Memcpy( header, ptr, size * nmemb );
	header[size * nmemb] = '\0';

	s = Q_stristr( header, "content-disposition:" );
	if ( !s ) {
		return size * nmemb;
	}
	s = Q_stristr( s + 20, "filename=" );
	if ( !s ) {
		return size * nmemb;
	}
	s += 9;
	quote = ( *s == '\'' || *s == '"' ) ? *s++ : '\0';
	while ( *s && *s != quote && *s != '\r' && *s != '\n' && len < (int)sizeof( name ) - 1 ) {
		name[len++] = *s++;
	}
	name[len] = '\0';

	if ( len < 5 || Q_stricmp( name + len - 4, ".pk3" ) ) {
		Q_strncpyz( dl.error, "bad file name in the answer", sizeof( dl.error ) );
		return (size_t)-1;
	}
	name[len - 4] = '\0';
	if ( !RL_Dl_ValidName( name ) ) {
		Q_strncpyz( dl.error, "bad file name in the answer", sizeof( dl.error ) );
		return (size_t)-1;
	}
	Q_strncpyz( dl.name, name, sizeof( dl.name ) );
	return size * nmemb;
}

#if CURL_AT_LEAST_VERSION(7, 32, 0)
static int RL_Dl_Progress( void *data, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow )
#else
static int RL_Dl_Progress( void *data, double dltotal, double dlnow, double ultotal, double ulnow )
#endif
{
	dl.size = (int)dltotal;
	dl.count = (int)dlnow;
	return 0;
}

// starts the transfer from source number dl.source
static qboolean RL_Dl_Begin( void )
{
	const char *source = dl.source == 0 ? dl_source->string : dl_source2->string;
	char *escaped;
	int r;

	if ( !source[0] ) {
		Q_strncpyz( dl.error, "no download source is set", sizeof( dl.error ) );
		return qfalse;
	}

	dl.easy = dl.f.easy_init();
	if ( !dl.easy ) {
		Q_strncpyz( dl.error, "curl_easy_init failed", sizeof( dl.error ) );
		return qfalse;
	}

	escaped = dl.f.easy_escape( dl.easy, dl.map, 0 );
	if ( !escaped ) {
		Q_strncpyz( dl.error, "curl_easy_escape failed", sizeof( dl.error ) );
		RL_Dl_Cleanup();
		return qfalse;
	}
	Q_strncpyz( dl.url, source, sizeof( dl.url ) );
	if ( !Q_replace( "%m", escaped, dl.url, sizeof( dl.url ) ) ) {
		if ( dl.url[strlen( dl.url ) - 1] != '/' ) {
			Q_strcat( dl.url, sizeof( dl.url ), "/" );
		}
		Q_strcat( dl.url, sizeof( dl.url ), escaped );
	}
	dl.f.free( escaped );

	Q_strncpyz( dl.name, dl.map, sizeof( dl.name ) );	// unless the server of the file says otherwise
	Com_sprintf( dl.tempName, sizeof( dl.tempName ), "%s%c%s.%08x.tmp", dl.gameDir, PATH_SEP, dl.map, rand() | ( rand() << 16 ) );
	dl.size = dl.count = 0;
	dl.tooBig = qfalse;
	dl.error[0] = '\0';

	Com_Printf( "Relay: downloading %s from %s\n", dl.map, dl.url );

	dl.f.easy_setopt( dl.easy, CURLOPT_URL, dl.url );
	dl.f.easy_setopt( dl.easy, CURLOPT_REFERER, dl.url );
	dl.f.easy_setopt( dl.easy, CURLOPT_USERAGENT, Q3_VERSION );
	dl.f.easy_setopt( dl.easy, CURLOPT_TRANSFERTEXT, 0 );
	dl.f.easy_setopt( dl.easy, CURLOPT_WRITEFUNCTION, RL_Dl_Write );
	dl.f.easy_setopt( dl.easy, CURLOPT_HEADERFUNCTION, RL_Dl_Header );
	dl.f.easy_setopt( dl.easy, CURLOPT_NOPROGRESS, 0 );
#if CURL_AT_LEAST_VERSION(7, 32, 0)
	dl.f.easy_setopt( dl.easy, CURLOPT_XFERINFOFUNCTION, RL_Dl_Progress );
#else
	dl.f.easy_setopt( dl.easy, CURLOPT_PROGRESSFUNCTION, RL_Dl_Progress );
#endif
	dl.f.easy_setopt( dl.easy, CURLOPT_FAILONERROR, 1 );
	dl.f.easy_setopt( dl.easy, CURLOPT_FOLLOWLOCATION, 1 );
	dl.f.easy_setopt( dl.easy, CURLOPT_MAXREDIRS, 5 );
#if CURL_AT_LEAST_VERSION(7, 85, 0)
	dl.f.easy_setopt( dl.easy, CURLOPT_PROTOCOLS_STR, "http,https" );
#else
	dl.f.easy_setopt( dl.easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS );
#endif

	dl.multi = dl.f.multi_init();
	if ( !dl.multi || dl.f.multi_add_handle( dl.multi, dl.easy ) != CURLM_OK ) {
		Q_strncpyz( dl.error, "curl_multi failed", sizeof( dl.error ) );
		RL_Dl_Cleanup();
		return qfalse;
	}

	(void)r;
	dl.running = qtrue;
	return qtrue;
}

static qboolean RL_Dl_NextSource( void )
{
	while ( ++dl.source < RL_DL_MAX_SOURCES ) {
		if ( ( dl.source == 0 ? dl_source : dl_source2 )->string[0] && RL_Dl_Begin() ) {
			return qtrue;
		}
		RL_Dl_Cleanup();
	}
	return qfalse;
}

void RL_Download_Init( void )
{
	dl_source = Cvar_Get( "dl_source", RL_DL_DEFAULT_SOURCE, CVAR_ARCHIVE );
	dl_source2 = Cvar_Get( "dl_source2", RL_DL_DEFAULT_SOURCE2, CVAR_ARCHIVE );
	dl_usebaseq3 = Cvar_Get( "dl_usebaseq3", "0", CVAR_ARCHIVE_ND );
	dl.file = FS_INVALID_HANDLE;
}

qboolean RL_Download_Busy( void )
{
	return dl.running;
}

// Starts downloading the pk3 of a map. NULL if it is under way, else why not.
const char *RL_Download_Start( const char *map )
{
	if ( dl.running ) {
		RL_Dl_Cleanup();
	}
	if ( !RL_Dl_ValidName( map ) ) {
		return "the map has a name that is not allowed to download";
	}
	if ( !RL_Dl_LoadCurl() ) {
		return "libcurl is not available";
	}

	Q_strncpyz( dl.map, map, sizeof( dl.map ) );
	Q_strncpyz( dl.gameDir, dl_usebaseq3->integer ? FS_GetBaseGameDir() : FS_GetCurrentGameDir(), sizeof( dl.gameDir ) );
	dl.source = -1;
	dl.lastReport = Sys_Milliseconds();

	if ( !RL_Dl_NextSource() ) {
		RL_Dl_UnloadCurl();
		return dl.error[0] ? va( "the download did not start: %s", dl.error ) : "no download source is set (dl_source)";
	}
	return NULL;
}

// A finished transfer: the file gets its name and has to contain the map, else the next source is tried.
static qboolean RL_Dl_Finish( void )
{
	char name[MAX_OSPATH];
	char path[MAX_QPATH];

	FS_FCloseFile( dl.file );
	dl.file = FS_INVALID_HANDLE;

	Com_sprintf( name, sizeof( name ), "%s%c%s.pk3", dl.gameDir, PATH_SEP, dl.name );
	if ( FS_SV_FileExists( name ) ) {
		// a different file with that name: keep both
		Com_sprintf( name, sizeof( name ), "%s%c%s.%08x.pk3", dl.gameDir, PATH_SEP, dl.name, FS_GetZipChecksum( name ) );
		FS_Remove( name );
	}
	FS_SV_Rename( dl.tempName, name );
	dl.tempName[0] = '\0';

	FS_Reload();

	Com_sprintf( path, sizeof( path ), "maps/%s.bsp", dl.map );
	if ( FS_FOpenFileRead( path, NULL, qfalse ) == -1 ) {
		FS_Remove( name );
		FS_Reload();
		Q_strncpyz( dl.error, "the file does not contain the map", sizeof( dl.error ) );
		return qfalse;
	}

	Com_Printf( S_COLOR_GREEN "Relay: %s downloaded\n", name );
	return qtrue;
}

// Moves the transfer on. Returns 1 when the map is there, -1 when it failed for good, 0 while it goes on.
int RL_Download_Frame( void )
{
	CURLMsg *msg;
	int running, queued, i;
	CURLMcode res;

	if ( !dl.running ) {
		return -1;
	}

	res = dl.f.multi_perform( dl.multi, &running );
	for ( i = 0; res == CURLM_CALL_MULTI_PERFORM && i < 128; i++ ) {
		res = dl.f.multi_perform( dl.multi, &running );
	}

	if ( Sys_Milliseconds() - dl.lastReport >= 2000 ) {
		dl.lastReport = Sys_Milliseconds();
		Com_Printf( "Relay: downloading %s: %i KB%s\n", dl.name, dl.count / 1024, dl.size ? va( " of %i KB", dl.size / 1024 ) : "" );
	}

	msg = dl.f.multi_info_read( dl.multi, &queued );
	if ( !msg ) {
		return 0;
	}

	if ( msg->msg == CURLMSG_DONE && msg->data.result == CURLE_OK && dl.file != FS_INVALID_HANDLE ) {
		qboolean ok = RL_Dl_Finish();

		RL_Dl_Cleanup();
		if ( ok ) {
			RL_Dl_UnloadCurl();
			return 1;
		}
	} else {
		long code = 0;

		dl.f.easy_getinfo( msg->easy_handle, CURLINFO_RESPONSE_CODE, &code );
		if ( !dl.error[0] ) {
			Q_strncpyz( dl.error, dl.f.easy_strerror( msg->data.result ), sizeof( dl.error ) );
		}
		Com_Printf( S_COLOR_YELLOW "Relay: download error: %s (code %ld)\n", dl.error, code );
		RL_Dl_Cleanup();
	}

	// this source failed: try the next one
	if ( RL_Dl_NextSource() ) {
		Com_Printf( S_COLOR_YELLOW "Relay: trying the other source\n" );
		return 0;
	}
	RL_Dl_UnloadCurl();
	return -1;
}

const char *RL_Download_Error( void )
{
	return dl.error[0] ? dl.error : "unknown error";
}

void RL_Download_Abort( void )
{
	if ( dl.running ) {
		RL_Dl_Cleanup();
		RL_Dl_UnloadCurl();
	}
}

#else // !USE_CURL

void RL_Download_Init( void ) {}
qboolean RL_Download_Busy( void ) { return qfalse; }
const char *RL_Download_Start( const char *map ) { return "the relay was built without libcurl"; }
int RL_Download_Frame( void ) { return -1; }
const char *RL_Download_Error( void ) { return "no libcurl"; }
void RL_Download_Abort( void ) {}

#endif
