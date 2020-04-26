#include <thread>
#include <chrono>         // std::chrono::seconds
#include "pipe.h"
#include "proxy.h"
#include "proxy_setting.h"
#include "tools.h"
#include "connections.h"
#include "kernel_info.h"

extern struct PIPE pipes;
//extern struct SUBFLOW subflows;
extern struct KERNEL_INFO kernelInfo;
extern struct PIPE_BUFFER pipeBuffer;


// set up pipe listener in c++
int PIPE::Setup(const char * localIP, const char * remoteIP, int secNo, int nSubflow) {

    this->secNo = secNo;
    this->n = 2;
    localListenFD = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(sockaddr_in));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PIPE_LISTEN_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int optval = 1;
    int r = setsockopt(localListenFD, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    MyAssert(r == 0, 1762);
    if (bind(localListenFD, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0) return R_FAIL;
    if (listen(localListenFD, 32) != 0) return R_FAIL;

    LOGD("Setting up pipe listener.");
    localListenFDSide = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serverAddrSide;
    memset(&serverAddrSide, 0, sizeof(sockaddr_in));
    serverAddrSide.sin_family = AF_INET;
    serverAddrSide.sin_port = htons(PIPE_LISTEN_PORT_SIDE);
    serverAddrSide.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int rr = setsockopt(localListenFDSide, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    MyAssert(rr == 0, 1762);
    if (bind(localListenFDSide, (struct sockaddr *)&serverAddrSide, sizeof(serverAddrSide)) != 0) return R_FAIL;
    if (listen(localListenFDSide, 32) != 0) return R_FAIL;

    int udpoptval = 1;
    int rrr = setsockopt(feedbackFD, SOL_SOCKET, SO_REUSEADDR, &udpoptval, sizeof(udpoptval));
    MyAssert(rrr == 0, 1762);
    // Using WiFi as the subflow
    struct sockaddr_in udpClientAddr;
    memset(&udpClientAddr, 0, sizeof(struct sockaddr_in));
    udpClientAddr.sin_family = AF_INET;
    inet_pton(AF_INET, localIP, &udpClientAddr.sin_addr);
    Accept();
    return R_SUCC;
}

void * AcceptThread(void * arg) {
    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    pipes.fd[0] = accept(pipes.localListenFD, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (pipes.fd[0] == -1) return R_FAIL;
    pipes.SetCongestionControl(pipes.fd[0], PROXY_SETTINGS::pipeProtocol[0].c_str());
    SetSocketNoDelay_TCP(pipes.fd[0]);
    SetNonBlockIO(pipes.fd[0]);
    LOGD("[AcceptThread] Pipe connection %d established. IP=%s, port=%d, TCP=%s, fd=%d",
         0, inet_ntoa(clientAddr.sin_addr), (int)ntohs(clientAddr.sin_port),
         PROXY_SETTINGS::pipeProtocol[0].c_str(), pipes.fd[0]);
    return 0;
}

void * AcceptThreadSide(void * arg) {

    struct sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    pipes.fd[1] = accept(pipes.localListenFDSide, (struct sockaddr *)&clientAddr, &clientAddrLen);
    if (pipes.fd[1] == -1) return R_FAIL;
    pipes.SetCongestionControl(pipes.fd[1], PROXY_SETTINGS::pipeProtocol[1].c_str());
    SetSocketNoDelay_TCP(pipes.fd[1]);
    LOGD("[AcceptThreadSide] Pipe side connection established. IP=%s, port=%d, TCP=%s, fd=%d",
         inet_ntoa(clientAddr.sin_addr), (int)ntohs(clientAddr.sin_port), PROXY_SETTINGS::pipeProtocol[1].c_str(), pipes.fd[1]);
    return 0;
}

void * InfoThread(void * arg) {
//    LOGD("InfoT");
    // Reading ctrl information from pipe
    int len = 12;
    BYTE buf[len+1];
    buf[len] = 0;
    while (true) {
        // stream v.s. packet
        if (buf[len] == len) {
            kernelInfo.pipeBW[0] = *((int *) buf);
            kernelInfo.pipeRTT[0] = *((int *)(buf + 4));
            kernelInfo.bytesInPipe[0] = *((int *)(buf + 8));
            buf[len] = 0;
            LOGI("[pipe.cpp] kernelInfo.X[%d], throughput is %d, RTT is %d, bytesInpipe is %d, bytesOnDevice is %d", 0,
                 kernelInfo.pipeBW[0], kernelInfo.pipeRTT[0], kernelInfo.bytesInPipe[0], kernelInfo.bytesInPipe[0]);
        } else {
            int nLeft = len - buf[len];
            int r = read(pipes.fd[1], buf + buf[len], nLeft);
            buf[len] += r;
        }
    }
}

void PIPE::Accept() {

    pthread_t accept_thread;
    int r = pthread_create(&accept_thread, NULL, AcceptThread, NULL);
    MyAssert(r == 0, 2459);
    pthread_t accept_thread_side;
    int rr = pthread_create(&accept_thread_side, NULL, AcceptThreadSide, this);
    MyAssert(rr == 0, 2460);

    pthread_t info_thread;

    int rrr = pthread_create(&info_thread, NULL, InfoThread, NULL);
    MyAssert(rrr == 0, 2460);

    LOGD("Accept threads started.");
}

void PIPE::SetCongestionControl(int fd, const char * tcpVar) {
    if (!strcmp(tcpVar, "default") || !strcmp(tcpVar, "DEFAULT")) return;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, tcpVar, (int)strlen(tcpVar));
    MyAssert(r == 0, 1815);
}