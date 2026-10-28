#ifndef TEST_STUBS_H
#define TEST_STUBS_H

#define MAX_TEST_CMDS	64
#define MAX_TEST_PRINTS	64

typedef struct {
	int		client;		// -1 = everybody
	char	text[256];
} testCmd_t;

extern testCmd_t testCmds[MAX_TEST_CMDS];
extern int numTestCmds;
extern char testPrints[MAX_TEST_PRINTS][512];
extern int numTestPrints;

void Test_SetCvar( const char *name, const char *value );

#endif
