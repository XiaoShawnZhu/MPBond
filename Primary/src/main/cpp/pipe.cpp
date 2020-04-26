#include "pipe.h"
#include "proxy.h"
#include "proxy_setting.h"
#include "tools.h"
#include "connections.h"
#include "kernel_info.h"

extern struct PIPE pipes;
extern struct KERNEL_INFO kernelInfo;

// for sec
const char * rIP;

//int infoFD;

// set up local pipe listener in c++, listening at loopback:pipe_listen_port
int PIPE::Setup(bool isTether, int numSec, const char * rIP) {

    this->n = numSec;
    // Tethering mode
    if (isTether) {
        fd[0] = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in serverAddr;
        memset(&serverAddr, 0, sizeof(sockaddr_in));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(REMOTE_PROXY_PORT);
        inet_pton(AF_INET, rIP, &serverAddr.sin_addr);
        if (connect(fd[0], (const struct sockaddr *) &serverAddr, sizeof(serverAddr)) != 0)
            return R_FAIL;
        SetNonBlockIO(fd[0]);
        return R_SUCC;
    }
    // MPBond
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

//    localListenFDSide = socket(AF_INET, SOCK_STREAM, 0);
//    struct sockaddr_in serverAddrSide;
//    memset(&serverAddrSide, 0, sizeof(sockaddr_in));
//    serverAddrSide.sin_family = AF_INET;
//    serverAddrSide.sin_port = htons(PIPE_LISTEN_PORT_SIDE);
//    serverAddrSide.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
//
//    int rr = setsockopt(localListenFDSide, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
//    MyAssert(rr == 0, 1762);
//    if (bind(localListenFDSide, (struct sockaddr *)&serverAddrSide, sizeof(serverAddrSide)) != 0) return R_FAIL;
//    if (listen(localListenFDSide, 32) != 0) return R_FAIL;

    Accept();
    return R_SUCC;
}

void * AcceptThread(void * arg) {
    for (int i = 0; i < pipes.n; i++) {
        struct sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        pipes.fd[i] = accept(pipes.localListenFD, (struct sockaddr *)&clientAddr, &clientAddrLen);
        if (pipes.fd[i] == -1) return R_FAIL;
        pipes.SetCongestionControl(pipes.fd[i], PROXY_SETTINGS::pipeProtocol[i].c_str());
        SetSocketNoDelay_TCP(pipes.fd[i]);
        SetNonBlockIO(pipes.fd[i]);
        int localPort = (int)ntohs(clientAddr.sin_port);
        LOGD("Pipe connection established. IP=%s, port=%d, TCP=%s, fd=%d",
             inet_ntoa(clientAddr.sin_addr), localPort,
             PROXY_SETTINGS::pipeProtocol[i].c_str(), pipes.fd[i]
        );
    }
    return NULL;
}

void * InfoThread(void * arg) {
    LOGD("InfoT");
    // Reading ctrl information from pipe
    int sockfd;
    int BUFSIZE = 1024;
//    char buffer[MAX_LEN];
    unsigned char buf[BUFSIZE];     /* receive buffer */
//    char *hello = "Hello from server";
    struct sockaddr_in serverAddr, clientAddr;
    socklen_t addrlen = sizeof(clientAddr);            /* length of addresses */
    // Creating socket file descriptor
    if ( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&serverAddr, 0, sizeof(serverAddr));
    memset(&clientAddr, 0, sizeof(clientAddr));

    // Filling server information
    serverAddr.sin_family    = AF_INET; // IPv4
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PIPE_LISTEN_PORT_SIDE);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
//    struct sockaddr_in remaddr;     /* remote address */
//    socklen_t addrlen = sizeof(remaddr);            /* length of addresses */
    while (1) {
        unsigned char buf[2048];     /* receive buffer */
        int recvlen = recvfrom(sockfd, buf, 2048, 0, (struct sockaddr *)&clientAddr, &addrlen);
        if (recvlen > 0) {
            // InfoMessage("[MPBond] recvlen = %d", recvlen);
            // buf[recvlen] = 0;
            int pipeNo = *((int *) buf);//*((int *)(buf + 16));
            int throughput = *((int *) (buf + 4));//*((int *) buf);
            int netRTT = *((int *) (buf + 8));//*((int *)(buf + 4));
            int bytesInPipe = *((int *) (buf + 12));//*((int *)(buf + 8));
            kernelInfo.pipeBW[pipeNo] = throughput;
            kernelInfo.pipeRTT[pipeNo] = netRTT;
            kernelInfo.bytesInPipe[pipeNo] = bytesInPipe;
            LOGD("recv pipeInfo: pipeNo=%d thrpt=%d rtt=%d bytesInPipe=%d", pipeNo, throughput, netRTT, bytesInPipe);
        }
    }
}

void PIPE::Accept() {
    // bStarted = 0;
    pthread_t accept_thread;
    int r = pthread_create(&accept_thread, NULL, AcceptThread, this);
    MyAssert(r == 0, 2459);
    pthread_t info_thread;
    int rr = pthread_create(&info_thread, NULL, InfoThread, NULL);
    MyAssert(rr == 0, 2460);
    // while (bStarted != 2) {pthread_yield();}
    LOGD("Accept threads started.");
}

void PIPE::SetCongestionControl(int fd, const char * tcpVar) {
    if (!strcmp(tcpVar, "default") || !strcmp(tcpVar, "DEFAULT")) return;
    int r = setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, tcpVar, (int)strlen(tcpVar));
    MyAssert(r == 0, 1815);
}