#ifndef _HINTS_H_
#define _HINTS_H_

#include "proxy.h"

#define MAX_HINT 100

struct HINT_ENTRY {
	DWORD serverIP;
	int ipPrefix;
	WORD serverPort;

	int sched; // 8 bit
	int timeType, timeValue; // 2 bit + 6 bit
	int energyType, energyValue; // 2 bit + 6 bit
	int dataType, dataValue; // 2 bit + 6 bit

	int matchEntry(DWORD serverIP, WORD serverPort);
	DWORD getHints();
	void decodeEntry(DWORD hints);
	void print();
};

struct HINTS {
	int maxHint;
	HINT_ENTRY entries[MAX_HINT];
	DWORD getHints(DWORD serverIP, WORD serverPort);
};

#endif