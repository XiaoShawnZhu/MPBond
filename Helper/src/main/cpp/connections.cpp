#include "connections.h"
#include "pipe.h"
#include <string.h>
#include <jni.h>
#include <stdio.h>

extern struct CONNECTIONS conns;
extern struct SUBFLOW subflows;
extern struct PIPE pipes;
extern struct BUFFER_SUBFLOW subflowOutput;
extern struct BUFFER_TCP tcpOutput;
extern struct BUFFER_PIPE pipeOutput;
extern struct PIPE_BUFFER pipeBuffer;
extern FILE * ofsIODump;
extern int keepRunning;
extern int tickCount;
extern int lastPipeActivityTick;
extern DWORD rpIPAddress;

int CONNECTIONS::Setup() {
    /* peers[0] <-- localListenFD
     * peers[1...subflows.n] <-- subflows
     * peers[subflows.n+1] <-- pipe
     * */
    memset(peers, 0, sizeof(peers));
    memset(peersExt, 0, sizeof(peersExt));

    for (int i=0; i < MAX_FDS; i++) {
        peers[i].fd = -1;
    }

    localListenFD = socket(AF_INET, SOCK_STREAM, 0);
    //TODO: add tools.cpp/h to enable the commented lines
//    SetNonBlockIO(localListenFD);
//    SetSocketNoDelay_TCP(localListenFD);
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(sockaddr_in));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(LOCAL_PROXY_PORT);//htons(1412);//htons(LOCAL_PROXY_PORT);
    serverAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int optval = 1;
    int r = setsockopt(localListenFD, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

//    MyAssert(r == 0, 1762);
    if (bind(localListenFD, (struct sockaddr *)&serverAddr, sizeof(serverAddr))) {
        LOGD("Bind failure.");
        return R_FAIL;
    }
    if (listen(localListenFD, 32)) {
        LOGD("Listen failure.");
        return R_FAIL;
    }

    peers[0].fd = localListenFD;
    peers[0].events = POLLRDNORM;
    nextConnID = 1; // TODO: update it to the right one
    for (int i = 0; i < subflows.n; i++) {
        peers[i + 1].fd = subflows.fd[i];
        peers[i + 1].events = POLLRDNORM;
        peersExt[i + 1].connID = 0;
        //they are not used by subflows, setting to succ/done to ensure they are ignored
        peersExt[i + 1].establishStatus = POLLFD_EXT::EST_SUCC;
        peersExt[i + 1].bSentSYNToPipe = 1;
        peersExt[i + 1].bToSendSYNACK = 0;
        peersExt[i + 1].bReceivedSYNACK = 1;
    }

    maxIdx = subflows.n + pipes.n; //

    for (int i = 0; i < subflows.n; i++) {
        peers[subflows.n + i + 1].fd = pipes.fd[i];
        peers[subflows.n + i + 1].events = POLLRDNORM;
    }

    return R_SUCC;
}

void CONNECTIONS::EnableSubflowWriteNotification(int pollPos, int bEnable) {
    LOGD("Pipe write notification changed. New value=%d Pipe#=%d", bEnable, pollPos);
    MyAssert(pollPos>=1 && pollPos<=subflows.n, 1746);

    if (bEnable) {
        peers[pollPos].events |= POLLWRNORM;
        LOGD("Pipe write notification ENABLED. Pipe#=%d", pollPos);
    } else {
        peers[pollPos].events &= ~POLLWRNORM;
    }

}

void BUFFER_SUBFLOW::Setup() {
    LOGD("BUFFER_Subflow Setup");

    static int bSetup = 0;

    MyAssert(!bSetup, 1639);

    memset(pDataBuffer, 0, sizeof(pDataBuffer));
    memset(pDataHead, 0, sizeof(pDataHead));
    memset(pDataTail, 0, sizeof(pDataTail));
    memset(dataBufSize, 0, sizeof(dataBufSize));

    memset(pMsgBuffer, 0, sizeof(pMsgBuffer));
    memset(pMsgHead, 0, sizeof(pMsgHead));
    memset(pMsgTail, 0, sizeof(pMsgTail));
    memset(msgBufSize, 0, sizeof(msgBufSize));

    memset(genSeq, 0, sizeof(genSeq));
//NOTE
    for (int i=1; i<=subflows.n; i++) {
        pDataBuffer[i] = new BYTE[PROXY_SETTINGS::pipeBufDataCapacity];
        pMsgBuffer[i] = new SUBFLOW_MSG[PROXY_SETTINGS::pipeBufMsgCapacity];
        MyAssert(pDataBuffer[i] != NULL && pMsgBuffer[i] != NULL, 1640);

        pDataHead[i] = NULL; pDataTail[i] = pDataBuffer[i];
        pMsgHead[i] = NULL; pMsgTail[i] = pMsgBuffer[i];
    }
    // NOTE: todo for pipe 0
    bSetup = 1;
}

void BUFFER_SUBFLOW::ResetPipe(int pollPos) {
    LOGD("BUFFER_Subflows::ResetPipe");

    msgBufSize[pollPos] = 0;
    dataBufSize[pollPos] = 0;
    pDataTail[pollPos] = pDataBuffer[pollPos];
    pDataHead[pollPos] = NULL;
    pMsgTail[pollPos] = pMsgBuffer[pollPos];
    pMsgHead[pollPos] = NULL;
}

int BUFFER_SUBFLOW::IsPipeFull(int pollPos) {
    MyAssert(pollPos >= 1 && pollPos <= subflows.n, 1689);

    if (msgBufSize[pollPos] >= PROXY_SETTINGS::pipeBufMsgCapacity - PROXY_SETTINGS::pipeBufMsgFullLeeway)
        return 1;
    if (dataBufSize[pollPos] >= PROXY_SETTINGS::pipeBufDataCapacity - PROXY_SETTINGS::pipeBufDataFullLeeway)
        return 1;

    return 0;
}

void CONNECTIONS::SafeClose(int fd, int pollPos) {

//    MyAssert(pollPos > pipes.n, 1763);
//
//    VerboseMessage("Close TCP connection: fd=%d pollPos=%d connID=%d",
//                   fd, pollPos, (int)peersExt[pollPos].connID);
    peers[pollPos].events = 0;
    close(fd);
}

void BUFFER_TCP::ReOrganizeDataCache() {
    //VerboseMessage("BUFFER_TCP::ReOrganizeDataCache");
    unsigned long currT = GetHighResTimestamp();

//    LOGD("Reorganizing TCP data cache. Before size=%d end=%d", dataCacheSize, dataCacheEnd);

    BYTE * pOldBase = pDataCache;

    if (pOldBase == dataCache1)
        pDataCache = dataCache2;
    else if (pOldBase == dataCache2)
        pDataCache = dataCache1;
    else
        MyAssert(0, 1685);

    int pollPosBegin = subflows.n + pipes.n + 1;
    int pollPosEnd = conns.maxIdx;

    BYTE * pNew = pDataCache;

    for (int i=pollPosBegin; i<=pollPosEnd + 2; i++) {
        //pollPosEnd: abandoned list (MAX_FDS)
        //pollPosEnd+1: not-yet-established list (MAX_FDS+1)
        LOGD("i: %d (maxIdx %d, maxFDS %d, pollPosEnd %d)", i, conns.maxIdx, MAX_FDS, pollPosEnd);
        SUBFLOW_MSG * pCur;
        if (i <= pollPosEnd)
            pCur = msgLists[i];
        else if (i == pollPosEnd + 1) {
            //msgLists[MAX_FDS] = NULL;
            pCur = msgLists[MAX_FDS];
        }
        else
            pCur = msgLists[MAX_FDS + 1];

        if (pCur == NULL) continue;

        while (pCur != NULL) {
            //InfoMessage("pCur: %u", pCur);
            //MyAssert(pCur->bytesLeft <= 0, 1686);
            memcpy(pNew, pCur->pMsgData, pCur->bytesOnWire - 8);
            pCur->pMsgData = pNew;
            pNew += pCur->bytesOnWire - 8;
            pCur = pCur->pNext;
        }
    }

    dataCacheEnd = pNew - pDataCache;

    LOGD("dataCacheSize=%d dataCacheEnd=%d", dataCacheSize, dataCacheEnd);
    MyAssert(dataCacheSize == dataCacheEnd, 1757);
    //dataCacheSize = dataCacheEnd;

    LOGD("After size=%d end=%d time=%lums", dataCacheSize, dataCacheEnd, (GetHighResTimestamp() - currT) / 1000);
}

void BUFFER_TCP::ReOrganizeMsgCache() {
    //VerboseMessage("BUFFER_TCP::ReOrganizeMsgCache");
    unsigned long currT = GetHighResTimestamp();
    LOGD("Reorganizing TCP message cache. Before size=%d end=%d", msgCacheSize, msgCacheEnd);

    SUBFLOW_MSG * pOldBase = pMsgCache;

    if (pOldBase == msgCache1)
        pMsgCache = msgCache2;
    else if (pOldBase == msgCache2)
        pMsgCache = msgCache1;
    else
        MyAssert(0, 1684);

    SUBFLOW_MSG * pNew = pMsgCache;

    int pollPosBegin = subflows.n + pipes.n + 1;
    int pollPosEnd = conns.maxIdx;
    for (int i=pollPosBegin; i<=pollPosEnd + 2; i++) {
        LOGD("i: %d (maxIdx %d)", i, conns.maxIdx);
        int j;

        //pollPosEnd: abandoned list (MAX_FDS)
        //pollPosEnd+1: not-yet-established list (MAX_FDS+1)
        SUBFLOW_MSG * pCur;
        if (i <= pollPosEnd) {
            j = i;
        } else if (i == pollPosEnd + 1) {
            j = MAX_FDS;
            //msgLists[j] = NULL;
        } else {
            j = MAX_FDS + 1;
        }

        pCur = msgLists[j];
        if (pCur == NULL) continue;

        SUBFLOW_MSG * pPrev = NULL;
        while (pCur != NULL) {
            memcpy(pNew, pCur, sizeof(SUBFLOW_MSG));
            pCur->pTemp = pNew;

            if (pPrev == NULL) {
                msgLists[j] = pNew;
            } else {
                pPrev->pNext = pNew;
            }

            pPrev = pNew++;
            pCur = pCur->pNext;
        }

        pPrev->pNext = NULL;
    }

    msgCacheEnd = pNew - pMsgCache;
    LOGD("After org msgCacheEnd=%u pNew=%u pMsgCache=%u msgCacheSize=%u",
         msgCacheEnd, pNew, pMsgCache, msgCacheSize);
    MyAssert(msgCacheSize == msgCacheEnd, 1756);
    //msgCacheSize = msgCacheEnd;

    //update pCurMsgs
    for (int i=1; i<=subflows.n; i++) {
        if (pCurMsgs[i] != NULL) {
            pCurMsgs[i] = pCurMsgs[i]->pTemp;
            LOGD("i: %d, pCurMsgs: %p, [%p, %p]",
                 i, pCurMsgs[i], pMsgCache, pMsgCache + PROXY_SETTINGS::tcpOverallBufMsgCapacity);
            MyAssert(pCurMsgs[i] >= pMsgCache && pCurMsgs[i] < pMsgCache + PROXY_SETTINGS::tcpOverallBufMsgCapacity, 1734);
        }
    }

    LOGD("After size=%d end=%d time=%lums", msgCacheSize, msgCacheEnd, (GetHighResTimestamp() - currT) / 1000);
}

void BUFFER_TCP::RemoveFromAbandonedList(const SUBFLOW_MSG * pMsg) {
    LOGD("BUFFER_TCP::RemoveFromAbandonedList");

    SUBFLOW_MSG * pPrev = NULL;
    SUBFLOW_MSG * pCur = msgLists[MAX_FDS];

    while (pCur != NULL) {
        if (pCur == pMsg) {
            if (pPrev == NULL)
                msgLists[MAX_FDS] = pCur->pNext;
            else
                pPrev->pNext = pCur->pNext;
            msgCacheSize--;
            LOGD("*** D: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pMsg->bytesOnWire));
            dataCacheSize -= pMsg->bytesOnWire ;
            //anyway the connection has been removed, so no need to decrease bytesInBuffer
            return;
        }

        pPrev = pCur;
        pCur = pCur->pNext;
    }

    MyAssert(0, 1733);
}

//int CONNECTIONS::TransferFromSubflowsToPipe(int fromPollPos, int fromFD) {
////    LOGD("CONNECTIONS::TransferFromSubflowsToPipe");
//
//    // blindly forwarding w/o decoding and re-encoding pipe headers
//    while (1) {
//        int len = 0;
//        BYTE buf[1000000];
//        ioctl(fromFD, FIONREAD, &len);
//        int r = read(fromFD, buf, len);
//
//        // TODO: acknowledegement for feedback
////        pipes.SendPipeInformedACK();
//        if (r > 0) {
//            PIPE_MSG pMsg;
//            pMsg.Setup();
//            pMsg.Set(buf, r);
//            pipeBuffer.Write(&pMsg, 1);
//            LOGD("Writing MSG to pipeBuffer, bytes: %d", r);
//        }
//        if (r == 0) {
//            return 0;
//        }
//    }
//}

int CONNECTIONS::TransferFromSubflowsToPipe(int fromPollPos, int fromFD) {
//    LOGD("CONNECTIONS::TransferFromSubflowsToPipe");

    // blindly forwarding w/o decoding and re-encoding pipe headers
    int sum_w = 0;
    while (1) {
        int len = 0;
        BYTE buf[1000000];
        ioctl(fromFD, FIONREAD, &len);
        int r = read(fromFD, buf, len);
//        LOGD("[Pre] fromPollPos=%d, fromFD=%d, Read bytes: %d", fromPollPos, fromFD, r);

        if (subflows.feedbackType == 2)
            subflows.SendPIE();
        int toFD = conns.peers[subflows.n + fromPollPos].fd;
        if (r > 0) {
            LOGD("%llu read %d bytes from WWAN %d", get_current_millisecond(), r, fromPollPos);
//            LOGD("[r  > 0]-----In TransferFromSubflowsToPipe-----fromFD=%d, Read bytes: %d, toFD=%d", fromFD, r, toFD);
            int w = write(toFD, buf, r);
            sum_w += w;
//            LOGD("Writing MSG to pipeBuffer, bytes: %d", r);
        }
        if (r == 0) {
//            LOGD("[r == 0]-----Exit TransferFromSubflowsToPipe-----toFD=%d, Write bytes: %d", toFD, sum_w);
            return sum_w;
        }
    }
}

void CONNECTIONS::UpdateNextConnID() {
//    VerboseMessage("CONNECTIONS::UpdateNextConnID");

//    MyAssert(proxyMode == PROXY_MODE_LOCAL && nextConnID != 0, 1692);
    int nTested = 0;

    while (nTested < MAX_CONN_ID_PLUS_ONE) {
        if (++nextConnID == MAX_CONN_ID_PLUS_ONE) nextConnID = 1;

        //0xFF is already used by NOP
        if (((nextConnID & 0xFF) != 0xFF) && connTab[nextConnID].GetInUseStatus(tickCount) == CONN_INFO::CONN_EMPTY) return;
        nTested++;
    }
    MyAssert(0, 1633);
}

void BUFFER_SUBFLOW::NewTCPConnection(int pollPos) {
    MyAssert(genSeq[pollPos] == 0, 1623);
}

void BUFFER_SUBFLOW::RemoveTCPConnection(int pollPos) {
    genSeq[pollPos] = 0;
}

void CONN_INFO::SetInUseStatus(int s) {
    LOGD("CONN_INFO::SetInUseStatus");

    int oldStatus = GetInUseStatus(tickCount);

    if (s == CONN_INUSE) {
        MyAssert(oldStatus != CONN_INUSE, 1723);
        closeTime = 0xFFFFFFFF;
    } else if (s == CONN_EMPTY) {
        MyAssert(oldStatus == CONN_INUSE, 1722);
        closeTime = tickCount;
    } else {
        MyAssert(0, 1719);
    }
}

const char * CONNECTIONS::DumpConnection(WORD connID, WORD msgType) {
    //MyAssert(connID != 0, 1629);
    if (connID <= 0) return NULL;

    if (msgType == SUBFLOW_MSG::MSG_OWD) return NULL;

    static char str[256];

    static const char * statStr [] = {"", "", "[JUST CLOSED]", "[EMPTY]"};

    CONN_INFO & c = connTab[connID];
    MyAssert(c.pollPos > subflows.n, 1630);
    sprintf(str, "{%s:%d-%s:%d cID=%d %s}",
            ConvertDWORDToIP(c.clientIP), c.clientPort,
            ConvertDWORDToIP(c.serverIP), c.serverPort,
            (int)connID,
            statStr[c.GetInUseStatus(tickCount)]
    );

    return str;
}

void CONNECTIONS::EnableTCPWriteNotification(int pollPos, int bEnable) {
    MyAssert(
            pollPos>subflows.n &&
            peers[pollPos].fd != -1 &&
            peersExt[pollPos].establishStatus == POLLFD_EXT::EST_SUCC, 1747
    );

    LOGD("TCP write notification changed. New value=%d connID=%d",
         bEnable, (int)peersExt[pollPos].connID);

    if (bEnable) {
        peers[pollPos].events |= POLLWRNORM;
        LOGD("TCP write notification ENABLED. ConnID=%d", (int)peersExt[pollPos].connID);
    } else {
        peers[pollPos].events &= ~POLLWRNORM;
    }

    /*
    short & e = peers[pollPos].events;
    if (bEnable) {
        if ((e & POLLWRNORM) == 0) {
            e |= POLLWRNORM;
            InfoMessage("TCP write notification ENABLED. ConnID=%d", (int)peersExt[pollPos].connID);
        }
    } else {
        if ((e & POLLWRNORM) != 0) {
            e &= ~POLLWRNORM;
            InfoMessage("TCP write notification DISABLED. ConnID=%d", (int)peersExt[pollPos].connID);
        }
    }
    */
}

void BUFFER_TCP::NewTCPConnection(int pollPos) {
    MyAssert(
            msgLists[pollPos] == NULL &&
            expSeq[pollPos] == 0,
            1673);
}

void BUFFER_TCP::Setup() {
    LOGD("BUFFER_TCP::Setup");

    static int bSetup = 0;
    MyAssert(!bSetup, 1657);

    msgCache1 = new SUBFLOW_MSG[PROXY_SETTINGS::tcpOverallBufMsgCapacity];
    msgCache2 = new SUBFLOW_MSG[PROXY_SETTINGS::tcpOverallBufMsgCapacity];
    pMsgCache = msgCache1;
    msgCacheEnd = 0;
    msgCacheSize = 0;

    msgCacheOverallAlmostFullThreshold = int(PROXY_SETTINGS::tcpOverallBufMsgCapacity * PROXY_SETTINGS::tcpBufAlmostFullRatio);
    msgCacheOverallFullThreshold = int(PROXY_SETTINGS::tcpOverallBufMsgCapacity * PROXY_SETTINGS::tcpBufFullRatio);
    msgCacheOverallReOrgThreshold = int(PROXY_SETTINGS::tcpOverallBufMsgCapacity * PROXY_SETTINGS::tcpBufReorgRatio);

    dataCache1 = new BYTE[PROXY_SETTINGS::tcpOverallBufDataCapacity];
    dataCache2 = new BYTE[PROXY_SETTINGS::tcpOverallBufDataCapacity];
    pDataCache = dataCache1;
    dataCacheEnd = 0;
    dataCacheSize = 0;

    dataCacheOverallAlmostFullThreshold = int(PROXY_SETTINGS::tcpOverallBufDataCapacity * PROXY_SETTINGS::tcpBufAlmostFullRatio);
    dataCacheOverallFullThreshold = int(PROXY_SETTINGS::tcpOverallBufDataCapacity * PROXY_SETTINGS::tcpBufFullRatio);
    dataCacheOverallReOrgThreshold = int(PROXY_SETTINGS::tcpOverallBufDataCapacity * PROXY_SETTINGS::tcpBufReorgRatio);

    dataCachePerConnAlmostFullThreshold = int(PROXY_SETTINGS::tcpPerConnBufDataCapacity * PROXY_SETTINGS::tcpBufAlmostFullRatio);
    dataCachePerConnFullThreshold = int(PROXY_SETTINGS::tcpPerConnBufDataCapacity * PROXY_SETTINGS::tcpBufFullRatio);


    memset(msgLists, 0, sizeof(msgLists));
    memset(expSeq, 0, sizeof(expSeq));

    memset(headers, 0, sizeof(headers));
    memset(pCurMsgs, 0, sizeof(pCurMsgs));
    // NOTE: pCurMsgs[0?]=0; for all.

    memset(writeBuf, 0, sizeof(writeBuf));
    memset(writePos, 0, sizeof(writePos));
    memset(writeSize, 0, sizeof(writeSize));
    //for (int i = 0; i < MAX_FDS + 2; i++) {
    //    writeBuf[i] = new BYTE[2000];
    //}
    bSetup = 1;
}

void BUFFER_PIPE::Setup() {
    LOGD("BUFFER_PIPE::Setup");

    static int bSetup = 0;
    MyAssert(!bSetup, 1657);

    msgCache1 = new SUBFLOW_MSG[PROXY_SETTINGS::tcpOverallBufMsgCapacity];
    msgCache2 = new SUBFLOW_MSG[PROXY_SETTINGS::tcpOverallBufMsgCapacity];
    pMsgCache = msgCache1;
    msgCacheEnd = 0;
    msgCacheSize = 0;

    msgCacheOverallAlmostFullThreshold = int(PROXY_SETTINGS::tcpOverallBufMsgCapacity * PROXY_SETTINGS::tcpBufAlmostFullRatio);
    msgCacheOverallFullThreshold = int(PROXY_SETTINGS::tcpOverallBufMsgCapacity * PROXY_SETTINGS::tcpBufFullRatio);
    msgCacheOverallReOrgThreshold = int(PROXY_SETTINGS::tcpOverallBufMsgCapacity * PROXY_SETTINGS::tcpBufReorgRatio);

    dataCache1 = new BYTE[PROXY_SETTINGS::tcpOverallBufDataCapacity];
    dataCache2 = new BYTE[PROXY_SETTINGS::tcpOverallBufDataCapacity];
    pDataCache = dataCache1;
    dataCacheEnd = 0;
    dataCacheSize = 0;

    dataCacheOverallAlmostFullThreshold = int(PROXY_SETTINGS::tcpOverallBufDataCapacity * PROXY_SETTINGS::tcpBufAlmostFullRatio);
    dataCacheOverallFullThreshold = int(PROXY_SETTINGS::tcpOverallBufDataCapacity * PROXY_SETTINGS::tcpBufFullRatio);
    dataCacheOverallReOrgThreshold = int(PROXY_SETTINGS::tcpOverallBufDataCapacity * PROXY_SETTINGS::tcpBufReorgRatio);

    dataCachePerConnAlmostFullThreshold = int(PROXY_SETTINGS::tcpPerConnBufDataCapacity * PROXY_SETTINGS::tcpBufAlmostFullRatio);
    dataCachePerConnFullThreshold = int(PROXY_SETTINGS::tcpPerConnBufDataCapacity * PROXY_SETTINGS::tcpBufFullRatio);

    memset(msgLists, 0, sizeof(msgLists));
    memset(expSeq, 0, sizeof(expSeq));

    memset(headers, 0, sizeof(headers));
    memset(pCurMsgs, 0, sizeof(pCurMsgs));
    // NOTE: pCurMsgs[0?]=0; for all.

    memset(writeBuf, 0, sizeof(writeBuf));
    memset(writePos, 0, sizeof(writePos));
    memset(writeSize, 0, sizeof(writeSize));
    //for (int i = 0; i < MAX_FDS + 2; i++) {
    //    writeBuf[i] = new BYTE[2000];
    //}
    bSetup = 1;
}

void CONNECTIONS::DumpIO(int reason, int nBytes, int pipeID, int connID, DWORD msgSeq) {
    //data flow:
    //TCPSOCKET --> PIPEBUFFER --> PIPESOCKET --> (network) --> PIPESOCKET --> TCPBUFFER --> TCPSOCKET
    //"Socket" means socket buffer at kernel
    //"Buffer" means buffer space maintained by CMAT at user space

    fprintf(ofsIODump, "%.3lf %d %d %d %d %d %u\n",
            GetMillisecondTS(),
            reason,
            nBytes,
            pipeID,
            tcpOutput.dataCacheSize,
            connID,
            msgSeq
    );
}
