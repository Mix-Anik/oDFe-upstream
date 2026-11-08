#ifndef TEST_STUBS_H
#define TEST_STUBS_H

#define MAX_TEST_CMDS	64
#define MAX_TEST_PRINTS	256

typedef struct {
	int		client;		// -1 = everybody
	char	text[256];
} testCmd_t;

extern testCmd_t testCmds[MAX_TEST_CMDS];
extern int numTestCmds;
extern char testPrints[MAX_TEST_PRINTS][512];
extern int numTestPrints;

// the HTTP requests plugins made (nothing is sent)
typedef struct {
	int		id, method, bodyLen;
	void	*owner;
	char	url[256];
	char	headers[512];
	char	body[3000];
} testHttp_t;

extern testHttp_t testHttp[8];
extern int numTestHttp;
void Test_HttpFinish( int index, int status, const char *body );
void Test_HttpReset( void );		// forgets the requests and the answers

void Test_SetCvar( const char *name, const char *value, int flags );

extern int testMilliseconds;		// Sys_Milliseconds() returns this

#endif
