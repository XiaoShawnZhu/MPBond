#ifndef DMM_KERNEL_INFO_H
#define DMM_KERNEL_INFO_H

#include "proxy_setting.h"

struct KERNEL_INFO {
    struct tcp_info pipeinfo[MAX_PIPES];
    int space[MAX_PIPES];

    int fd[MAX_PIPES];
    int infoSize;
    int nTCP;

    // DMM
    int pipeBW[MAX_PIPES]; // in kbps
    int pipeRTT[MAX_PIPES];
    int bytesInPipe[MAX_PIPES];
    int bytesOnDevice[MAX_PIPES];

    void Setup();
    void UpdatePipeInfo(int pipeNo);

    int GetTCPAvailableSpace(int pipeNo);
    unsigned int GetSendCwnd(int pipeNo);
    unsigned int GetSndMss(int pipeNo);
    int GetInFlightSize(int pipeNo);
    int GetSRTT(int pipeNo);
    DWORD GetSndBuffer(int subflowNo);
};

#endif //DMM_KERNEL_INFO_H
