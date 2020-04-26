#ifndef DMM_HINTS_H
#define DMM_HINTS_H

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
};

struct HINTS {
    int maxHint;
    HINT_ENTRY entries[MAX_HINT];
    DWORD getHints(DWORD serverIP, WORD serverPort);
    void print();
};

#endif //DMM_HINTS_H