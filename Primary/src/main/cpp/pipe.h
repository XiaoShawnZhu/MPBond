#ifndef MPBOND_PIPE_H
#define MPBOND_PIPE_H

#include "proxy_setting.h"

struct PIPE_MSG {

};

struct PIPE {

    int n;
    int localListenFD;
    int feedbackFD;
    int fd[MAX_PIPES];
    int Setup(bool isTether, int n, const char * rIP);
    void Accept();
    void SetCongestionControl(int fd, const char * tcpVar);
    void SendPipeInformedACK();

    // following for sec
    int secNo;
    int localListenFDSide;
    int secSetup(const char * localIP, const char * remoteIP, int secNo);
};

#endif
