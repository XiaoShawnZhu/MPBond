#include "hints.h"
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

DWORD HINTS::getHints(DWORD serverIP, WORD serverPort) {
    DWORD value = 0xFFFFFFFF;
    LOGD("Get Hints for: %u %u", serverIP, serverPort);
    for (int i = 0; i <= maxHint; i++) {
        if (entries[i].matchEntry(serverIP, serverPort)) {
            value = entries[i].getHints();
            break;
        }
    }
    return value;
}

void HINTS::print() {
    for (int i = 0; i <= maxHint; i++) {
        LOGD("Rule %d: serverIP=%u ipPrefix=%d serverPort=%u "
                     "sched=%d time=%d/%d energy=%d/%d data=%d/%d",
             i, entries[i].serverIP, entries[i].ipPrefix, entries[i].serverPort,
             entries[i].sched, entries[i].timeType, entries[i].timeValue,
             entries[i].energyType, entries[i].energyValue,
             entries[i].dataType, entries[i].dataValue);
    }
}
