#ifndef MPBOND_TOOLS_H
#define MPBOND_TOOLS_H

#define TAG    "Shawn-JNI"
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,TAG,__VA_ARGS__)
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,TAG,__VA_ARGS__)

#include <android/log.h>
#include "proxy.h"

const char * ConvertDWORDToIP(DWORD ip);

void SetNonBlockIO(int fd);

uint64_t get_current_millisecond();

void SetQuickACK(int fd);
void SetSocketNoDelay_TCP(int fd);

void SetSocketBuffer(int fd, int readBufSize, int writeBufSize);

inline void MyAssert(int x, int assertID) {
#ifdef DEBUG_ENABLE_ASSERTION
    if (!x) {
		fprintf(stderr, "Assertion failure: %d\n", assertID);
		fprintf(stderr, "errno = %d (%s)\n", errno, strerror(errno));
		fclose(ofsOWD);
		fclose(owd_proc_f);
        fclose(owd1_proc_f);
        fclose(owd2_proc_f);
        fclose(ack1_proc_f);
        fclose(ack2_proc_f);
		shutdown(pipes.fd[0], 2);
		shutdown(pipes.fd[1], 2);
		printf("exit.\n");
		exit(-1);
	}
#endif
}
double GetMillisecondTS();
static inline WORD ReverseWORD(WORD x) {
    return
            (x & 0xFF) << 8 |
            (x & 0xFF00) >> 8;
}

unsigned long GetHighResTimestamp();

#define TCPSOCKET_2_PIPEBUFFER 1
#define PIPEBUFFER_2_PIPESOCKET 2
#define PIPESOCKET_2_TCPBUFFER 3
#define TCPBUFFER_2_TCPSOCKET 4


inline unsigned long GetLogicTime() {
    static unsigned long l = 100;
    return l++;
}


#endif
