#include "kernel_info.h"
#include "connections.h"
#include "tools.h"

extern struct CONNECTIONS conns;
extern struct KERNEL_INFO kernelInfo;

void KERNEL_INFO::Setup() {
    infoSize = sizeof(pipeinfo[0]);
    for (int i = 1; i < MAX_PIPES; i++) {
        fd[i] = conns.peers[i].fd;
    }
    nTCP = PROXY_SETTINGS::nTCPPipes;

    // DMM
    for (int i = 1; i < MAX_PIPES; i++) {
        pipeBW[i] = 0;
        pipeRTT[i] = 0;
        bytesInPipe[i] = 0;
        bytesOnDevice[i] = 0;
    }
}

void KERNEL_INFO::UpdatePipeInfo(int pipeNo) {
    int r = 0;
    if (pipeNo > 0 && pipeNo < MAX_PIPES) {
        r = getsockopt(fd[pipeNo], IPPROTO_TCP, TCP_INFO,
                       &pipeinfo[pipeNo], (socklen_t *)&infoSize);
        MyAssert(r == 0, 9110);
    } else {
        for (int i = 1; i <= 2; i++) {
            if (fd[i] >= 0) {
                r = getsockopt(fd[i], IPPROTO_TCP, TCP_INFO,
                               &pipeinfo[i], (socklen_t *)&infoSize);
                MyAssert(r == 0, 9111);
            }
        }
    }
}

// currently not available
int KERNEL_INFO::GetTCPAvailableSpace(int pipeNo) {
    if (pipeNo >= 1 && pipeNo <= nTCP)
        return space[pipeNo];
    return 0;
}

unsigned int KERNEL_INFO::GetSendCwnd(int pipeNo) {
    return pipeinfo[pipeNo].tcpi_snd_cwnd;
}

unsigned int KERNEL_INFO::GetSndMss(int pipeNo) {
    return pipeinfo[pipeNo].tcpi_snd_mss;
}

// bytes
int KERNEL_INFO::GetInFlightSize(int pipeNo) {
    return (pipeinfo[pipeNo].tcpi_unacked - pipeinfo[pipeNo].tcpi_sacked
            - pipeinfo[pipeNo].tcpi_lost + pipeinfo[pipeNo].tcpi_retrans) * GetSndMss(pipeNo);
}

// us
int KERNEL_INFO::GetSRTT(int pipeNo) {
    return pipeinfo[pipeNo].tcpi_rtt;
}

DWORD KERNEL_INFO::GetSndBuffer(int subflowNo) {
    int buf_size = 0;
    ioctl(fd[subflowNo], TIOCOUTQ, &buf_size);
    return buf_size;
}
