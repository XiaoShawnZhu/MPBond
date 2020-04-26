#include "subflow.h"
#include "connections.h"
#include "kernel_info.h"
#include "hints.h"
#include <thread>

extern struct BUFFER_SUBFLOW subflowOutput;
extern struct CONNECTIONS conns;
extern int tickCount;
extern struct KERNEL_INFO kernelInfo;
extern struct HINTS hintRules;
extern struct SUBFLOW subflows;
extern struct PIPE pipes;

const char * rpIP;

WORD SUBFLOW_MSG::GetBytesOnWire(WORD msgType) {
    switch (msgType) {
        case MSG_CREATE: return MSG_HEADER_LEN+MSG_CREATE_LEN;
        case MSG_CLOSE:  return 8+1;
        case MSG_SYNACK: return 8+1;
        case MSG_OWD: return 8+4;
        default:
            MyAssert(0, 1664);
    }
    return 0;
}

int SUBFLOW::SelectPipe_MinimumBuf(WORD connID) {

    int f1 = subflowOutput.IsPipeFull(1);
    int f2 = subflowOutput.IsPipeFull(2);
    if (!f1) return 1;
    if (!f2) return 2;
    return -1;
}

int SUBFLOW::SelectPipe_RoundRobin(WORD connID) {
    static int k = 0;
    if (++k > n) k = 1;

    int i = k - 1;
    int j = 0;

    while (++j <= n) {
        if (++i > n) i = 1;

        if (subflowOutput.IsPipeFull(i))
            continue;
        else
            return i;
    }

    return -1;
}

int SUBFLOW::SelectPipe_RoundRobinChunk(WORD connID) {
    CONN_INFO & c = conns.connTab[connID];

    if (c.GetInUseStatus(tickCount) != CONN_INFO::CONN_INUSE) {
        return SelectPipe_RoundRobin(connID);
    } else {
        int & p = c.lastPipeID;
        if (c.accuChunkBytes < PROXY_SETTINGS::chunkSizeThreshold && p != -1 && !subflowOutput.IsPipeFull(p)) {
            return p;
        } else {
            p = SelectPipe_RoundRobin(connID);
            c.accuChunkBytes = 0;
            return p;
        }
    }
}

int SUBFLOW::SelectPipe_Fixed(WORD connID) {
    int r = -1;
    // InfoMessage("RTT1=%d, RTT2=%d", kernelInfo.GetSRTT(1), kernelInfo.GetSRTT(2));
    if (kernelInfo.GetSRTT(1) > kernelInfo.GetSRTT(2)) {
        r = 2;
    } else {
        r = 1;
    }

    // always use BT for handshake
    r = 1;

    if (subflowOutput.IsPipeFull(r))
        return -1;
    else
        return r;
}

int SUBFLOW::SelectPipe_Random(WORD connID) {
    //InfoMessage("*** random ***");

    static int pipeIDList[MAX_PIPES];
    int nAvailPipes = 0;

    int i=1;
    while (i<=n) {
        if (!subflowOutput.IsPipeFull(i)) {
            pipeIDList[nAvailPipes++] = i;
        }
        i++;
    }

    if (nAvailPipes == 0)
        return -1;
    else
        return pipeIDList[rand()%nAvailPipes];
}

int SUBFLOW::Setup(const char * remoteIP, int feedbackType) {
    LOGD("Start to setup subflow");
    rpIP = remoteIP;

    this->feedbackType = feedbackType;
    this->n = PROXY_SETTINGS::nPipes;
    this->n = 1;
    this->nTCP = PROXY_SETTINGS::nTCPPipes;
    fd[0] = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in clientAddr;
    memset(&clientAddr, 0, sizeof(struct sockaddr_in));
    clientAddr.sin_family = AF_INET;

    LOGD("setup subflow");
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(REMOTE_PROXY_PORT);
    inet_pton(AF_INET, remoteIP, &serverAddr.sin_addr);

    if (connect(fd[0], (const struct sockaddr *) &serverAddr, sizeof(serverAddr)) != 0)
        return R_FAIL;
    LOGD("Subflow connected to remote proxy");
    SetNonBlockIO(fd[0]);

    switch (PROXY_SETTINGS::pipeSelectionAlgorithm) {
        case PIPE_SELECTION_MINBUF:
            SelectPipeFunc = &SUBFLOW::SelectPipe_MinimumBuf;
            break;
        case PIPE_SELECTION_RANDOM:
            SelectPipeFunc = &SUBFLOW::SelectPipe_Random;
            break;
        case PIPE_SELECTION_ROUNDROBIN:
            SelectPipeFunc = &SUBFLOW::SelectPipe_RoundRobin;
            break;
        case PIPE_SELECTION_ROUNDROBIN_CHUNK:
            SelectPipeFunc = &SUBFLOW::SelectPipe_RoundRobinChunk;
            break;
        case PIPE_SELECTION_FIXED:
            SelectPipeFunc = &SUBFLOW::SelectPipe_Fixed;
            break;
        default:
            MyAssert(0, 1979);
    }
    SelectPipeFunc_Main = SelectPipeFunc;

    // Set up UDP socket for feedback over secondary subflow
    feedbackFD = socket(AF_INET, SOCK_DGRAM, 0);
    if (feedbackFD == -1) {
        LOGD("Could not create feedback socket");
    }
    SetNonBlockIO(feedbackFD);

    if(feedbackType == 1 || feedbackType == 4)
        AlwaysOnFeedback();

    return R_SUCC;
}

void SUBFLOW_MSG::SetData(WORD connID, BYTE * pMsgData, WORD payloadLen) {
    this->msgType = MSG_DATA;
    this->connID = connID;
    this->pMsgData = pMsgData;

    this->payloadLen = payloadLen;
    this->bytesOnWire = (int)payloadLen + 8;
    this->bytesLeft = bytesOnWire;

    LOGD("Pipe msg created. Type=DATA, Payload=%d", (int)payloadLen);
}

void SUBFLOW_MSG::SetMoreData(WORD payloadLen) {
    MyAssert(this->msgType == MSG_DATA, 1662);
    this->payloadLen += payloadLen;
    this->bytesOnWire += payloadLen;
    this->bytesLeft += payloadLen;

    LOGD("Pipe msg updated. Payload=%d", (int)payloadLen);
}

void SUBFLOW_MSG::SetClose(WORD connID, BYTE * pMsgData, BYTE closeReason) {
    this->msgType = MSG_CLOSE;
    this->connID = connID;
    this->pMsgData = pMsgData;

    this->closeReason = closeReason;
    this->bytesOnWire = 8 + 1;
    this->bytesLeft = this->bytesOnWire;

    LOGD("Pipe msg created. Type=%s", closeReason == MSG_CLOSE_FIN ? "CLOSE_FIN" : "CLOSE_RST");
}

void SUBFLOW_MSG::SetSYNACK(WORD connID, BYTE * pMsgData) {
    this->msgType = MSG_SYNACK;
    this->connID = connID;
    this->pMsgData = pMsgData;

    this->bytesOnWire = 8 + 1;
    this->bytesLeft = this->bytesOnWire;

    LOGD("Pipe msg created. Type=SYNACK");
}

void SUBFLOW_MSG::SetCreate(WORD connID, BYTE * pMsgData, DWORD serverIP, WORD serverPort) {
    this->msgType = MSG_CREATE;
    this->connID = connID;
    this->pMsgData = pMsgData;

    this->serverIP = serverIP;
    this->serverPort = serverPort;
    this->bytesOnWire = MSG_HEADER_LEN + MSG_CREATE_LEN;
    this->bytesLeft = this->bytesOnWire;

    LOGD("Pipe msg created. Type=CREATE");
}

void SUBFLOW_MSG::SetACK(WORD connID, DWORD seq) {
    this->msgType = MSG_ACK;
    this->connID = connID;
    this->seq = seq;

    this->bytesOnWire = 8 + 1;
    this->bytesLeft = this->bytesOnWire;

    LOGD("Pipe msg created. Type=ACK");
}

void SUBFLOW_MSG::SetOWD(BYTE * pMsgData, WORD pipeNo, DWORD ack) {
    this->msgType = MSG_OWD;
    this->connID = pipeNo;
    this->owd_ack = ack;
    this->pMsgData = pMsgData;
    this->bytesOnWire = 8 + 4;
    this->bytesLeft = this->bytesOnWire;
}

void SUBFLOW_MSG::SetUDP(WORD bytesOnWire) {
    *((WORD *)(pMsgData + 6)) = bytesOnWire | (WORD)(1 << 15);
}

void SUBFLOW_MSG::CheckUDP(int bytesOnWire) {
    this->bytesOnWire = bytesOnWire;
    if (((*(WORD *)(pMsgData + 6)) & 0x8000) > 0) {
        this->msgType = MSG_UDP;
    }
}

void SUBFLOW_MSG::Decode() {
    switch (this->msgType) {
        case MSG_DATA:
            this->payloadLen = this->bytesOnWire - 8;
            break;

        case MSG_UDP:
            this->serverPort = *(WORD *)(pMsgData);
            this->serverIP = *(DWORD *)(pMsgData + 2);
            this->payloadLen = *(WORD *)(pMsgData + 6);
            this->payloadLen &= 0x7FFF;
            this->payloadLen -= 24;
            this->clientPort = *(WORD *)(pMsgData + 8);
            //this->pMsgData = this->pMsgData + 10;
            //InfoMessage("payloadlen=%d, bytesonwire=%d", this->payloadLen, this->bytesOnWire);
            MyAssert(this->bytesOnWire - 24 == this->payloadLen, 2156);
            break;

        case MSG_CREATE:
            this->serverIP = *(DWORD *)(pMsgData);
            this->serverPort = *(WORD *)(pMsgData + 4);
            break;

        case MSG_CLOSE:
            this->closeReason = *pMsgData;
            break;

        case MSG_SYNACK:
            MyAssert(*pMsgData == 0, 2188);
            break;

        default:
            MyAssert(0, 1669);
    }
}

void SUBFLOW_MSG::Encode(DWORD seq, BYTE * pBuf, BYTE * pBufBegin, const BYTE * pBufEnd) {
    //for data, just fill in the header (8 bytes)
    //for control msg, fill in everything

    LOGD("SUBFLOW_MSG::Encode (seq=%u)", seq);

    static BYTE headerBuf[64];
    BYTE * pData;

    int bytesToFill = this->msgType == MSG_DATA ? 8 : bytesOnWire;
    if (pBuf + bytesToFill > pBufEnd)
        pData = headerBuf;
    else
        pData = pBuf;

    this->seq = seq;
    *((WORD *)pData) = connID;
    *((DWORD *)(pData + 2)) = seq;

    switch (msgType) {
        case MSG_DATA:
            *((WORD *)(pData + 6)) = (WORD)bytesOnWire;
            MyAssert(bytesOnWire > 0 && bytesOnWire < 0xFFFD, 1644);
            break;
        case MSG_OWD:
        case MSG_CREATE:
        case MSG_CLOSE:
        case MSG_SYNACK:
            MyAssert(bytesToFill == GetBytesOnWire(msgType), 1714);
            *((WORD *)(pData + 6)) = (WORD)msgType;
            break;
        default:
            MyAssert(0, 1661);
    }

    switch (msgType) {
        case MSG_DATA:
            break;

            // dst IP: 4 bytes, dst Port 2 bytes,
            // sched/traffic type: 1 byte, time hint: 1 byte (2 + 6)
            // energy hint: 1 byte (2 + 6), data hint: 1 byte (2 + 6)
        case MSG_CREATE:
            *((DWORD *)(pData + 8)) = serverIP;
            *((WORD *)(pData + 12)) = serverPort;
            *((DWORD *)(pData + 14)) = hintRules.getHints(serverIP, serverPort);
            break;

        case MSG_CLOSE:
            *(pData + 8) = closeReason;
            break;

        case MSG_SYNACK:
            *(pData + 8) = 0;
            break;

        case MSG_OWD:
            *((DWORD *)(pData + 8)) = owd_ack;
            break;

        default:
            MyAssert(0, 1645);
    }

    if (pData == pBuf) return;
    for (int i = 0; i < bytesToFill; i++) {
        *pBuf = *pData;
        pData++;
        if (++pBuf == pBufEnd) pBuf = pBufBegin;
    }
}

// Given the fd of the pipe,
// return the pipe type: TCP, UDP, etc
int SUBFLOW::GetPipeType(int pipeFD) {
    if (pipeFD == fd[n-1] || pipeFD == fd[n-2])
        return SUBFLOW::UDP_PIPE;
    return SUBFLOW::TCP_PIPE;
}

int SUBFLOW::GetUDPPipeID(int pipeFD) {
    if (pipeFD == fd[n-1]) return 1;
    if (pipeFD == fd[n-2]) return 0;
    return -1;
}

void SUBFLOW::SendPIE() {

    struct sockaddr_in udpServerAddr;
    memset(&udpServerAddr, 0, sizeof(sockaddr_in));
    udpServerAddr.sin_family = AF_INET;
    udpServerAddr.sin_port = htons(4399);
    inet_pton(AF_INET, rpIP, &udpServerAddr.sin_addr);
    int leng = 24;
    BYTE buf[leng + 1];
    buf[leng] = 0;
    *((int *) buf) = 1; // subflow ID of the primary is 1
    *((int *)(buf + 4)) = kernelInfo.pipeBW[0];
    *((int *)(buf + 8)) = kernelInfo.pipeRTT[0];
    *((int *)(buf + 12)) = kernelInfo.bytesInPipe[0];
    *((unsigned long long *)(buf + 16)) = kernelInfo.expectedSeq; // the expected seq
    if(sendto(feedbackFD, buf, leng, 0, (sockaddr*)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
        LOGD("Send feedback of primary failed");
    }
    LOGI("[SendPIE] subflow=%d, throughput=%d, netRTT=%d, bytesInPipe=%d, eSeq=%d",
         1, kernelInfo.pipeBW[0], kernelInfo.pipeRTT[0], kernelInfo.bytesInPipe[0], kernelInfo.expectedSeq);
    for (int i = 0; i < pipes.n; i++) {
        //    kernelInfo.bytesOnDevice[0] = kernelInfo.GetSndBuffer(2); // 0: local, 1: subflow, 2:pipe
        *((int *) buf) = i + 2; // subflow ID of helpers start from 2
        *((int *)(buf + 4)) = kernelInfo.pipeBW[i];
        *((int *)(buf + 8)) = kernelInfo.pipeRTT[i];
        *((int *)(buf + 12)) = kernelInfo.bytesInPipe[i];
        *((unsigned long long *)(buf + 16)) = kernelInfo.expectedSeq; // the expected seq
    LOGI("[SendPIE] subflow=%d, throughput=%d, netRTT=%d, bytesInPipe=%d, eSeq=%d",
         i + 2, kernelInfo.pipeBW[i], kernelInfo.pipeRTT[i], kernelInfo.bytesInPipe[i], kernelInfo.expectedSeq);
//    if(sendto(localListenFD, buf, 20, 0, (sockaddr*)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
        if(sendto(feedbackFD, buf, leng, 0, (sockaddr*)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
            LOGD("Send feedback failed");
        }
    }
}

void * AlwaysOnFeedbackThread(void * arg) {

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        // fixed-always-on
        subflows.SendPIE();
    }

    return NULL;
}

void SUBFLOW::AlwaysOnFeedback() {
    pthread_t always_on_feedback_thread;
    int r = pthread_create(&always_on_feedback_thread, NULL, AlwaysOnFeedbackThread, this);
    MyAssert(r == 0, 2453);
}