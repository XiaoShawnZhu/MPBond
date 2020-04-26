#ifndef DMM_PIPE_H
#define DMM_PIPE_H

#include "proxy_setting.h"

struct PIPE_MSG {
    BYTE * pData;
    int len = 0;

    void Set(BYTE * buf, int len);
    int Get();
    void Setup();
};

struct PIPE {

    int n;
    int localListenFD;
    int localListenFDSide;
    int feedbackFD;
    int fd[MAX_PIPES];
    // distinguish different secondary devices in DMM
    int secNo;
    int nextSubflow;

    int Setup(const char * localIP, const char * remoteIP, int secNo, int nSubflow);
    void Accept();
    void SetCongestionControl(int fd, const char * tcpVar);
    void Feedback();
    void SendPipeInformedACK();
    void Write2Pipe();

};

#endif //DMM_PIPE_H
