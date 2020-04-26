#include "hints.h"
#include "scheduler.h"
#include "tools.h"

int HINT_ENTRY::matchEntry(DWORD serverIP, WORD serverPort) {
	if (this->serverIP >> (32-ipPrefix) == serverIP >> (32-ipPrefix)
		&& this->serverPort == serverPort)
		return 1;
	return 0;
}

DWORD HINT_ENTRY::getHints() {
	DWORD value = 0;
	value += ((DWORD) sched << 24);
	value += ((DWORD) timeType << 22);
	value += ((DWORD) timeValue << 16);
	value += ((DWORD) energyType << 14);
	value += ((DWORD) energyValue << 8);
	value += ((DWORD) dataType << 6);
	value += ((DWORD) dataValue);
	return value;
}

void HINT_ENTRY::decodeEntry(DWORD hints) {
	dataValue = int(hints & 0x3F);
	hints >>= 6;
	dataType = int(hints & 0x3);
	hints >>= 2;
	energyValue = int(hints & 0x3F);
	hints >>= 6;
	energyType = int(hints & 0x3);
	hints >>= 2;
	timeValue = int(hints & 0x3F);
	hints >>= 6;
	timeType = int(hints & 0x3);
	hints >>= 2;
	sched = int(hints & 0xFF);
}

void HINT_ENTRY::print() {
	// InfoMessage("[Hints] Sched/traffic: %s [%d]; Time: %d/%d; Energy: %d/%d; Data: %d/%d.", 
	// 	SCHEDULER::getSchedulerName(sched), sched,
	// 	timeType, timeValue, energyType, energyValue, dataType, dataValue);
}


DWORD HINTS::getHints(DWORD serverIP, WORD serverPort) {
	DWORD value = 0xFFFFFFFF;
	for (int i = 0; i <= maxHint; i++) {
		if (entries[i].matchEntry(serverIP, serverPort)) {
			value = entries[i].getHints();
			break;
		}
	}
	return value;
}


