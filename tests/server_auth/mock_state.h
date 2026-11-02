#ifndef MOCK_STATE_H
#define MOCK_STATE_H

#include "odfs_types.h"

// What the mock plugin saw and got back, and what the test program tells it to do. The plugin runs in its sandbox: it copies
// this struct to the test program at the end of every call (Test_MirrorPush) and takes it back at the start of the next
// (Test_MirrorPull), so both sides see the same values.

#define MOCK_MAX_LOG	32

typedef struct {
	// ---- set by the test ----
	int			mode;				// 0 answer logins in on_event, 1 in the next frame, 2 never
	int			resultCode;
	unsigned	resultIDLo, resultIDHi;	// the account number, 64 bit
	char		resultName[40];
	char		resultReason[40];

	// ---- what happened ----
	int			eventCount;
	char		eventName[MOCK_MAX_LOG][32];
	char		eventData[MOCK_MAX_LOG][1100];
	int			frames;
	int			initCalls;

	int			infoOk;				// the result of the event "query"
	odfClientInfo_t	info;

	int			actResult;			// the result of the last "act"
	char		actText[256];
	char		readBuf[256];
	int			readLen;

	int			httpDone;			// requests that came back
	int			httpId[4], httpStatus[4], httpBodyLen[4];
	char		httpBody[4][64];
} mockState_t;

#endif
