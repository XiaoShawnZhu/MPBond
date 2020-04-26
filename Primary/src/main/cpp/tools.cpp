#include "tools.h"
struct timespec tms;
uint64_t millis;

const char * ConvertDWORDToIP(DWORD ip) {
    static char ipstr[5][128];
    static int count = 0;

    int i = count++;
    if (count == 5) count = 0;
    sprintf(ipstr[i], "%d.%d.%d.%d",
            (ip & 0x000000FF),
            (ip & 0x0000FF00) >> 8,
            (ip & 0x00FF0000) >> 16,
            (ip & 0xFF000000) >> 24
    );
    return ipstr[i];
}

void SetNonBlockIO(int fd) {
    int val = fcntl(fd, F_GETFL, 0);
    if (fcntl(fd, F_SETFL, val | O_NONBLOCK) != 0) {
        MyAssert(0, 1616);
    }
}

#if TRANS == 1
void SetQuickACK(int fd) {
    static int enable = 1;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &enable, sizeof(int));
    MyAssert(r == 0, 2040);
}
#endif

uint64_t get_current_millisecond(){
    if (clock_gettime(CLOCK_REALTIME,&tms)) {
        return 0;
    }
    millis = tms.tv_sec;
    millis *= 1000;
    millis += (tms.tv_nsec+500000) / 1000000;
    return millis;
}

void SetSocketNoDelay_TCP(int fd) {
    static int enable = 1;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enable,sizeof(int));
    MyAssert(r == 0, 1768);
}

void SetSocketBuffer(int fd, int readBufSize, int writeBufSize) {
    int r1 = 0;
    int r2 = 0;
    if (readBufSize != 0) {
        r1 = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &readBufSize, sizeof(int));	MyAssert(r1 == 0, 1786);
    }

    if (writeBufSize != 0) {
        r2 = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &writeBufSize, sizeof(int)); MyAssert(r2 == 0, 1787);
    }

    /*
    socklen_t s1, s2;
    s1 = s2 = 4;
    r1 = getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &readBufSize, &s1);
    r2 = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &writeBufSize, &s2);
    */

    MyAssert(r1 == 0 && r2 == 0, 1769);

    //InfoMessage("*** %d %d readBufSize=%d writeBufSize=%d ***", (int)s1, (int)s2, readBufSize, writeBufSize);
}

double GetMillisecondTS() {
#ifndef VS_SIMULATION
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6 ;
#else
    return 0.0f;
#endif
}

extern unsigned long highResTimestampBase;

unsigned long GetHighResTimestamp() {
#ifndef VS_SIMULATION
    struct timeval tv;
    gettimeofday(&tv, NULL);
    unsigned long tNow = tv.tv_sec * 1000000 + tv.tv_usec;

    return tNow - highResTimestampBase;
#else
    return 0;
#endif
}

