#include "connections.h"
#include <string.h>
#include <jni.h>
#include <stdio.h>
#include "pipe.h"
#include "subflow.h"
#include "kernel_info.h"

extern struct CONNECTIONS conns;
extern struct SUBFLOW subflows;
extern struct PIPE pipes;
extern struct BUFFER_SUBFLOW subflowOutput;
extern struct BUFFER_TCP tcpOutput;
extern struct KERNEL_INFO kernelInfo;
extern FILE * ofsIODump;
extern int keepRunning;
extern int tickCount;
extern int lastPipeActivityTick;
extern DWORD rpIPAddress;

uint32_t owd_ack = 0;
uint64_t owd_pipeNo = 0;
uint32_t owd = 0;

int CONNECTIONS::Setup() {

    memset(peers, 0, sizeof(peers));
    memset(peersExt, 0, sizeof(peersExt));

    for (int i=0; i < MAX_FDS; i++) {
        peers[i].fd = -1;
    }

    localListenFD = socket(AF_INET, SOCK_STREAM, 0);
//    localListenFD = socket(AF_INET6, SOCK_STREAM, 0);
    //TODO: add tools.cpp/h to enable the commented lines
//    SetNonBlockIO(localListenFD);
//    SetSocketNoDelay_TCP(localListenFD);
    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(sockaddr_in));
    serverAddr.sin_family = AF_INET;
//    serverAddr.sin_family = AF_INET6;
    serverAddr.sin_port = htons(LOCAL_PROXY_PORT);
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
    nextConnID = 1;
    peers[1].fd = subflows.fd[0];
    peers[1].events = POLLRDNORM;
    peersExt[1].connID = 0;
    //they are not used by subflows, setting to succ/done to ensure they are ignored
    peersExt[1].establishStatus = POLLFD_EXT::EST_SUCC;
    peersExt[1].bSentSYNToPipe = 1;
    peersExt[1].bToSendSYNACK = 0;
    peersExt[1].bReceivedSYNACK = 1;

    maxIdx = subflows.n + pipes.n;

    for (int i = 2; i <= maxIdx; i++){
        peers[i].fd = pipes.fd[i-2];
        peers[i].events = POLLRDNORM;
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
    LOGD("BUFFER_PIPES::Setup");

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
//    LOGD("BUFFER_PIPES::ResetPipe");

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

int BUFFER_SUBFLOW::TransferFromTCPToSubflows(int toPollPos, int toFD) {
    LOGD("BUFFER_SUBFLOWS::TransferFromTCPToSubflows");
    //int nMsgs = msgBufSize[toPollPos];
    int pipeType = subflows.GetPipeType(toFD);

#if TRANS==1 || TRANS==3
    static const BYTE zeros[4096] = {0};	//place holder for late binding, always 0
#endif

    int nBytesWritten = 0;

    BYTE * & pDHead = pDataHead[toPollPos];
    BYTE * & pDTail = pDataTail[toPollPos];
    BYTE * pDBuffer = pDataBuffer[toPollPos];
    int & dBufSize = dataBufSize[toPollPos];
    const BYTE * pDataEnd = pDBuffer + PROXY_SETTINGS::pipeBufDataCapacity;

    SUBFLOW_MSG * & pMHead = pMsgHead[toPollPos];
    SUBFLOW_MSG * & pMTail = pMsgTail[toPollPos];
    SUBFLOW_MSG * & pMBuffer = pMsgBuffer[toPollPos];
    int & mBufSize = msgBufSize[toPollPos];
    const SUBFLOW_MSG * pMsgEnd = pMBuffer + PROXY_SETTINGS::pipeBufMsgCapacity;

    MyAssert((pMHead==NULL) == (pDHead==NULL) && pMTail != NULL && pDTail != NULL, 1655);
    if (pMHead == NULL) {
        return nBytesWritten;
    }

    MyAssert(pMHead->pMsgData == pDHead, 1647);

    MyAssert(pMTail == pMHead + mBufSize || pMTail == pMHead + mBufSize - PROXY_SETTINGS::pipeBufMsgCapacity, 1652);
    MyAssert(pDTail == pDHead + dBufSize || pDTail == pDHead + dBufSize - PROXY_SETTINGS::pipeBufDataCapacity, 1653);

    int w1, w2;
    while (mBufSize > 0 /*pMHead < pMTail*/) {
        w1 = pMHead->bytesLeft;
        MyAssert(w1 > 0 && pMHead->pMsgData >= pDBuffer && pMHead->pMsgData < pDataEnd, 1648);

        // InfoMessage("*** To Write %d bytes to pipe %d ***", w1, toPollPos);

        if (PROXY_SETTINGS::bLateBindingRP) {
            w2 = 0;
        } else {
            w2 = pMHead->pMsgData + w1 - pDataEnd;
            if (w2 > 0) w1 -= w2; else w2 = 0;
        }

        int w = 0;

        pipeType = SUBFLOW::TCP_PIPE; // now we only have tcp connection
        switch (pipeType) {

            case SUBFLOW::TCP_PIPE:
                if (PROXY_SETTINGS::bLateBindingRP) {
#define EXTRA_BYTES 8
                    MyAssert(w1 > 8, 2034);
                    w1 += EXTRA_BYTES;		//11/6/14 for late binding we need additional 8 bytes
                    w = write(toFD, zeros, w1);
//                    LOGD("bLateBindingRP, write");
                    MyAssert(w == w1 || w < 0, 2032);
                    w -= EXTRA_BYTES;
                    w1 -= EXTRA_BYTES;
                } else {
                    w = write(toFD, pMHead->pMsgData, w1);
//                    LOGD("not bLateBindingRP, write");
                }
                break;
            default:
                break;
        }

#ifdef FEATURE_DUMP_SUBFLOW_MSG
        pipes.DumpPipeMsg(pMHead->pMsgData, w1);
#endif

        if (w > 0) {
            lastPipeActivityTick = tickCount;
            nBytesWritten += PROXY_SETTINGS::bLateBindingRP ? w+8 : w;
            conns.connTab[pMHead->connID].bytesInPipeBuffer -= w;

        }

        if (w == w1 && w2 == 0) {	//when late binding is used, this is the only non-error path
            dBufSize -= w;

            goto NEXT_MSG;
        } else if (w == w1 && w2 > 0) {
            MyAssert(!PROXY_SETTINGS::bLateBindingRP, 2023);
            pMHead->pMsgData = pDBuffer;
            pMHead->bytesLeft -= w;
            dBufSize -= w;

            switch (pipeType) {
                case SUBFLOW::TCP_PIPE:
                    w = write(toFD, pDBuffer, w2);
//                    LOGD("SUBFLOW::TCP_PIPE");
                    break;
                default:
                    break;
            }

            // need to be modified ...

            if (w > 0) {
                lastPipeActivityTick = tickCount;
                nBytesWritten += w;

            }

            if (w == w2) {
                dBufSize -= w;


                goto NEXT_MSG;
            } else if (w >= 0 && w < w2) {
                pMHead->pMsgData += w;
                pMHead->bytesLeft -= w;
                dBufSize -= w;

                break;
            } else if (w < 0 && errno == EWOULDBLOCK) {
                LOGD("Socket buffer full for pipe# %d", toPollPos);
                break;
            } else {
                LOGD("Error writing to pipe: %s(%d) Pipe #%d",
                     strerror(errno), errno, (int)toPollPos
                );
                MyAssert(0, 1650);
            }
        } else if (w >= 0 && w < w1) {
            MyAssert(!PROXY_SETTINGS::bLateBindingRP, 2036);
            pMHead->pMsgData += w;
            pMHead->bytesLeft -= w;
            dBufSize -= w;

            break;
        } else if (w < 0 && errno == EWOULDBLOCK) {
            LOGD("Socket buffer full for pipe# %d", toPollPos);
            break;
        } else {
            LOGD("Error writing to pipe #%d: %s(%d)",
                 (int)toPollPos, strerror(errno), errno
            );

            MyAssert(0, 1649);
        }

        NEXT_MSG:

        if (++pMHead == pMsgEnd) pMHead = pMBuffer;
        mBufSize--;

        if (nBytesWritten >= PROXY_SETTINGS::pipeTransferUnit) break;
    }

    MyAssert((mBufSize == 0) == (pMHead == pMTail), 1758);

    if (mBufSize == 0) {
        conns.EnableSubflowWriteNotification(toPollPos, 0);
        ResetPipe(toPollPos);
    } else {
        conns.EnableSubflowWriteNotification(toPollPos, 1);
        pDHead = pMHead->pMsgData;
        MyAssert(pDTail == pDHead + dBufSize || pDTail == pDHead + dBufSize - PROXY_SETTINGS::pipeBufDataCapacity, 1654);
    }
//    LOGD("Byteswritten: %d",nBytesWritten);
    return nBytesWritten;
}

void CONNECTIONS::SafeClose(int fd, int pollPos) {

//    MyAssert(pollPos > pipes.n, 1763);
//
//    VerboseMessage("Close TCP connection: fd=%d pollPos=%d connID=%d",
//                   fd, pollPos, (int)peersExt[pollPos].connID);
    peers[pollPos].events = 0;
    close(fd);
}

int CONNECTIONS::AddTCPConnection(int clientFD,
                                  DWORD clientIP, WORD clientPort,
                                  DWORD serverIP, WORD serverPort,	/*must be 0 for local proxy*/
                                  WORD connID	/* must be 0 for local proxy */
) {

    LOGD("AddTCPConnection");
    MyAssert(clientFD >= 0, 1716);
    int pollPos = -1;
    for (int i = subflows.n + pipes.n + 1; i < MAX_FDS; i++) {
        if (peers[i].fd < 0) {
            peers[i].fd = clientFD;
            peers[i].events = POLLRDNORM;
            peers[i].revents = 0;
            if (i > maxIdx) maxIdx = i;

            MyAssert(serverIP == 0 && serverPort == 0 && connID == 0, 1693);
            MyAssert(nextConnID != 0 && ((nextConnID & 0xFF) != 0xFF), 1628);
            CONN_INFO & ci = connTab[nextConnID];
            MyAssert(ci.GetInUseStatus(tickCount) == CONN_INFO::CONN_EMPTY, 1632);

            ci.clientIP = clientIP;
            ci.clientPort = clientPort;
            //ci.fd = clientFD;
            ci.pollPos = i;
            ci.SetInUseStatus(CONN_INFO::CONN_INUSE);

            ci.bytesInTCPBuffer = 0;
            ci.bytesInPipeBuffer = 0;
            ci.accuChunkBytes = 0;
            ci.lastPipeID = -1;

            ci.accuBurstyBytes = 0;
            ci.l_clock = ci.r_clock = 0;

            pollPos = i;

            BYTE ipOpt[11];
            memset(ipOpt, 0, sizeof(ipOpt));
            socklen_t ipOptLen = 11;
            if (getsockopt(clientFD, IPPROTO_IP, IP_OPTIONS, ipOpt, &ipOptLen) != 0 || ipOptLen != 11) {
                MyAssert(0, 1611);
            }
            int r = setsockopt(clientFD, IPPROTO_IP, IP_OPTIONS, NULL, 0);
            MyAssert(r == 0, 1712);

            ci.serverIP = *(DWORD *)(ipOpt + 3);

            if (ci.serverIP == rpIPAddress) {
                ci.serverIP = htonl(INADDR_LOOPBACK);
            }

            ci.serverPort = ReverseWORD(*((WORD *)(ipOpt + 7)));

            peersExt[i].connID = nextConnID;
            peersExt[i].bSentSYNToPipe = 0;
            peersExt[i].establishStatus = POLLFD_EXT::EST_SUCC; //not used in local proxy - setting to succ ensures it will be ignored
            peersExt[i].connectTime = 0;
            peersExt[i].bToSendSYNACK = 0;
            peersExt[i].bReceivedSYNACK = PROXY_SETTINGS::bZeroRTTHandshake ? 1 : 0;

            UpdateNextConnID();

            subflowOutput.NewTCPConnection(i);
            tcpOutput.NewTCPConnection(i);
            break;// important . stop looping
        }
    }

    ResetReadNotificationEnableState();

    MyAssert(pollPos != -1, 1608);
    return pollPos;

}

void BUFFER_TCP::RemoveTCPConnection(int pollPos, WORD connID) {
    LOGD("BUFFER_TCP::RemoveTCPConnection");

    expSeq[pollPos] = 0;

    if (msgLists[pollPos] != NULL) {
        SUBFLOW_MSG * pCur = msgLists[pollPos];
        while (pCur != NULL) {
            LOGD("Premature TCP connection termination: (connID=%d), discard in-buffer data (seq=%u)",
                 (int)connID, pCur->seq
            );

            if (pCur->bytesLeft <= 0) {
                //otherwise it's still one of pCurMsgs[], cannot consider it's been removed
                msgCacheSize--;

//                LOGD("*** A: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pCur->bytesOnWire - 8));

                dataCacheSize -= pCur->bytesOnWire - 8;
                //anyway the connection will be removed, so no need to decrease bytesInBuffer
            }

            pCur = pCur->pNext;
        }
        msgLists[pollPos] = NULL;
    }

    for (int i=1; i<=subflows.n; i++) {
        if (pCurMsgs[i] != NULL && pCurMsgs[i]->connID == connID) {
            //move the partial message to "abandoned" list, so it will not
            //be cleaned up during garbage collection
            //InfoMessage("RemoveTCPConnection: pMsg %p, %u %u.",
            //                pCurMsgs[i], pCurMsgs[i]->connID, pCurMsgs[i]->seq);
            if (pCurMsgs[i]->drop == 0) {
                pCurMsgs[i]->pNext = msgLists[MAX_FDS];
                msgLists[MAX_FDS] = pCurMsgs[i];
            }
            pCurMsgs[i]->inUseFlag = (BYTE)CONN_INFO::CONN_RECENTLY_CLOSED;
            //GetMsgListBytes(MAX_FDS);
        }
    }

    //"headers" and "pCurMsgs" should not be changed

    //the above operations may lead to more buffer space. So try to enable pipe read ntf

    ////////////////////////////// 08/07/2014 //////////////////////////////
    /*
    if (GetBufferFullStatus() != BUFFER_FULL) {
        conns.EnablePipeReadNotifications(1);
    }
    */
    ////////////////////////////////////////////////////////////////////////
}


void CONNECTIONS::RemoveConnection(int pollPos) {
    LOGD("CONNECTIONS::RemoveConnection (pollPos=%d)", pollPos);

    //only TCP connections can be removed. Pipes cannot.
    MyAssert(pollPos > subflows.n, 1618);

    WORD connID = peersExt[pollPos].connID;
    MyAssert(connID > 0 && connTab[connID].GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE, 1631);
    connTab[connID].SetInUseStatus(CONN_INFO::CONN_EMPTY);

    connTab[connID].bytesInTCPBuffer = 0;
    connTab[connID].bytesInPipeBuffer = 0;
    connTab[connID].accuChunkBytes = 0;
    connTab[connID].lastPipeID = -1;

    connTab[connID].accuBurstyBytes = 0;
    connTab[connID].l_clock = connTab[connID].r_clock = 0;

    peers[pollPos].fd = -1;
    peers[pollPos].events = 0;
    peers[pollPos].revents = 0;
    peersExt[pollPos].connID = 0;
    peersExt[pollPos].bSentSYNToPipe = 0;
    peersExt[pollPos].establishStatus = POLLFD_EXT::EST_NOT_CONNECTED;
    peersExt[pollPos].connectTime = 0;

    peersExt[pollPos].bToSendSYNACK = 0;
    peersExt[pollPos].bReceivedSYNACK = 0;

    subflowOutput.RemoveTCPConnection(pollPos);
    tcpOutput.RemoveTCPConnection(pollPos, connID);

    //try to decrease maxIdx
    while (maxIdx > subflows.n + pipes.n && peers[maxIdx].fd == -1) {
        maxIdx--;
    }

//    LOGD("Connection removed (connID = %d, pos = %d, maxIdx = %d)", (int)connID, pollPos, maxIdx);
}

void CONNECTIONS::TransferDelayedFINsToSubflows() {
#ifdef FEATURE_DELAYED_FIN
    int n = delayedFins.n;
	if (n == 0) return;

	LOGD("CONNECTIONS::TransferDelayedFINsToPipes");

	int i;
	for (i=0; i<n; i++) {
		int toPollPos = pipes.SelectPipe(TC_HIGH_PRIORITY, 1);
		if (toPollPos == -1) break;

		BYTE * pDataBuffer = pipeOutput.pDataBuffer[toPollPos];
		BYTE * & pDataHead = pipeOutput.pDataHead[toPollPos];
		BYTE * & pDataTail = pipeOutput.pDataTail[toPollPos];
		int & dataBufSize = pipeOutput.dataBufSize[toPollPos];

		SUBFLOW_MSG * pMsgBuffer = pipeOutput.pMsgBuffer[toPollPos];
		SUBFLOW_MSG * & pMsgHead = pipeOutput.pMsgHead[toPollPos];
		SUBFLOW_MSG * & pMsgTail = pipeOutput.pMsgTail[toPollPos];
		int & msgBufSize = pipeOutput.msgBufSize[toPollPos];

		const BYTE * pDataEnd = pDataBuffer + PROXY_SETTINGS::pipeBufDataCapacity;
		const SUBFLOW_MSG * pMsgEnd = pMsgBuffer + PROXY_SETTINGS::pipeBufMsgCapacity;

		SUBFLOW_MSG & finMsg = delayedFins.fins[i];


		int avalLen = PROXY_SETTINGS::pipeBufDataCapacity - dataBufSize;

		//need extra 9 bytes for MSG_CLOSE (different from 14 in TransferFromTCPToSubflows)
		MyAssert(
			avalLen >= 9 &&
			finMsg.msgType == SUBFLOW_MSG::MSG_CLOSE &&
			finMsg.closeReason == SUBFLOW_MSG::MSG_CLOSE_FIN, 1828
		);

		*pMsgTail = finMsg;
		pMsgTail->Encode(pMsgTail->seq, pDataTail, pDataBuffer, pDataEnd);
		pMsgTail->pMsgData = pDataTail;

		//update the circular queue data structure
		if (pDataHead == NULL) pDataHead = pDataTail;
		dataBufSize += pMsgTail->bytesOnWire;
		pDataTail += pMsgTail->bytesOnWire;
		if (pDataTail >= pDataEnd) pDataTail -= PROXY_SETTINGS::pipeBufDataCapacity;

		/*
		//////////////////////////////////////////////////////////
		// Stats for protocol efficiency
		statTotalMsgs++;
		statTotalBytesOnWire+=pMsgTail->bytesOnWire;
		//////////////////////////////////////////////////////////
		*/

		if (pMsgHead == NULL) pMsgHead = pMsgTail;
		msgBufSize++;
		pMsgTail++;
		if (pMsgTail >= pMsgEnd) pMsgTail -= PROXY_SETTINGS::pipeBufMsgCapacity;

		pipeOutput.TransferFromTCPToSubflows(toPollPos, peers[toPollPos].fd);
	}

	delayedFins.DeQueue(i);

	////////////////////////////// 08/07/2014 //////////////////////////////
	//EnableTCPReadNotifications(i==n);
	////////////////////////////////////////////////////////////////////////
#endif
}

int BUFFER_TCP::GetBufferFullStatus(int connID) {
    //quick test
    if (dataCacheSize > dataCacheOverallFullThreshold ||
        conns.connTab[connID].bytesInTCPBuffer > dataCachePerConnFullThreshold ||
        msgCacheSize > msgCacheOverallFullThreshold
            )
        return BUFFER_FULL;

    if (msgCacheEnd > msgCacheOverallReOrgThreshold) ReOrganizeMsgCache();
    if (dataCacheEnd > dataCacheOverallReOrgThreshold) {
        LOGD("Reorg data: end=%u, thres=%u",
             dataCacheEnd, dataCacheOverallReOrgThreshold);
        ReOrganizeDataCache();
    }

    if (dataCacheSize > dataCacheOverallAlmostFullThreshold ||
        conns.connTab[connID].bytesInTCPBuffer > dataCachePerConnAlmostFullThreshold ||
        msgCacheSize > msgCacheOverallAlmostFullThreshold
            )
        return BUFFER_CLOSE_TO_FULL;
    else
        return BUFFER_NOT_FULL;
}

void BUFFER_TCP::ReOrganizeDataCache() {
    //VerboseMessage("BUFFER_TCP::ReOrganizeDataCache");
    unsigned long currT = GetHighResTimestamp();

    LOGD("Reorganizing TCP data cache. Before size=%d end=%d", dataCacheSize, dataCacheEnd);

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
    LOGD("After org msgCacheEnd=%u pNew=%u pMsgCache=%u msgCacheSize=%u", msgCacheEnd, pNew, pMsgCache, msgCacheSize);
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

SUBFLOW_MSG * BUFFER_TCP::Allocate(WORD connID, int toPollPos, DWORD seq, WORD len /* include header */, int pipeNo) {
//    LOGD("BUFFER_TCP::Allocate");

    int fs = GetBufferFullStatus(connID);

    if (fs == BUFFER_FULL) {
        ////////////////////////////// 05/19/2014 //////////////////////////////
        //This can cause issues when directing specific traffic to certain pipes
        //conns.EnablePipeReadNotifications(0);
        ////////////////////////////////////////////////////////////////////////
        LOGD("Allocation Failed (buffer full), %u[%d], ", seq, pipeNo);
        return NULL;
    } else {
        ////////////////////////////// 05/19/2014 //////////////////////////////
        //conns.EnablePipeReadNotifications(1);
        ////////////////////////////////////////////////////////////////////////
    }

    int inUseStatus = conns.connTab[connID].GetInUseStatus(tickCount);

    DWORD & eSeq = expSeq[inUseStatus == CONN_INFO::CONN_INUSE ? toPollPos : 0];
    //InfoMessage("############ seq=%u eSeq=%u", seq, eSeq);

    //MyAssert(inUseStatus != CONN_INFO::CONN_INUSE || seq >= eSeq, 1678);

    //prioritize two types of messages when either buffer is close to full:
    //SYN pipe message and data pipe message matching the expected sequence number
    if (len !=SUBFLOW_MSG::MSG_CREATE && (inUseStatus != CONN_INFO::CONN_INUSE || seq != eSeq)) {
        //we should not call DisablePipeRead here due to potential deadlock
        //e.g., missing the pipe message matching the expected sequence number, so
        //cannot perform in-order write to TCP
        if (fs == BUFFER_CLOSE_TO_FULL) return NULL;
    }

    //if (msgCacheEnd > msgCacheReOrgThreshold) ReOrganizeMsgCache();
    //if (dataCacheEnd > dataCacheReOrgThreshold) ReOrganizeDataCache();

    SUBFLOW_MSG * pMsg = &pMsgCache[msgCacheEnd];

    switch (len) {
        case SUBFLOW_MSG::MSG_CREATE:
        case SUBFLOW_MSG::MSG_CLOSE:
        case SUBFLOW_MSG::MSG_SYNACK:
            pMsg->msgType = len;
            pMsg->bytesOnWire = SUBFLOW_MSG::GetBytesOnWire(len);
            break;

        default:
            pMsg->msgType = SUBFLOW_MSG::MSG_DATA;
            pMsg->bytesOnWire = len;
    }

    pMsg->drop = 0;
    pMsg->bytesLeft = (int)pMsg->bytesOnWire - 8;

    if (pMsg->bytesLeft <= 0) {
        LOGD("Error!!! connID=%d  seq=%u  msgType=%d  len=%d   bytesOnWire=%d",
             (int)connID, seq, (int)pMsg->msgType, (int)len, pMsg->bytesOnWire);
    }

    MyAssert(pMsg->bytesLeft > 0, 1663);
    pMsg->connID = connID;
    pMsg->seq = seq;

    if (inUseStatus == CONN_INFO::CONN_INUSE) {

        MyAssert(pMsg->msgType != SUBFLOW_MSG::MSG_CREATE, 1759);
        InsertSubflowMsgAndUpdateExpSeq(pMsg, connID, toPollPos, pipeNo);

    } else if (inUseStatus == CONN_INFO::CONN_RECENTLY_CLOSED) {
        //put it to the "abandon" list
        //it will be discarded when fully read
//        LOGD("In Allocate(): get a pipe message of a closed TCP connection");
        // InfoMessage("Get a pipe message of a closed TCP connection: pMsg %p, %u %u.",
        //        pMsg, pMsg->connID, pMsg->seq);
        pMsg->pNext = msgLists[MAX_FDS];
        msgLists[MAX_FDS] = pMsg;
        //GetMsgListBytes(MAX_FDS);

    } else if (inUseStatus == CONN_INFO::CONN_EMPTY) {
        //put it to the "future connection" list

        if (len != SUBFLOW_MSG::MSG_CREATE)
//            LOGD("In Allocate(): get a pipe message of future connection. ConnID=%d, seq=%u, len=%d",
//                 (int)pMsg->connID, pMsg->seq, pMsg->bytesOnWire
//            );

        pMsg->pNext = msgLists[MAX_FDS + 1];
        msgLists[MAX_FDS + 1] = pMsg;

    } else {
        MyAssert(0, 1735);
    }

    msgCacheEnd++;
    msgCacheSize++;

    //now allocate data (w/o 8-byte header)
    pMsg->pMsgData = pDataCache + dataCacheEnd;

//    LOGD("*** dataCacheEnd: %d->%d dataCacheSize: %d->%d",
//         dataCacheEnd, dataCacheEnd + pMsg->bytesLeft,
//         dataCacheSize, dataCacheSize + pMsg->bytesLeft
//    );

    dataCacheEnd += pMsg->bytesLeft;
    dataCacheSize += pMsg->bytesLeft;

    if (inUseStatus == CONN_INFO::CONN_INUSE)
        conns.connTab[connID].bytesInTCPBuffer += pMsg->bytesLeft;

    pMsg->inUseFlag = (BYTE)inUseStatus;

    return pMsg;
}

void BUFFER_TCP::InsertSubflowMsgAndUpdateExpSeq(SUBFLOW_MSG * pMsg, WORD connID, int toPollPos, int pipeNo) {
//    LOGD("BUFFER_TCP::InsertSubflowMsgAndUpdateExpSeq (connID=%d, toPollPos=%d)", (int)connID, toPollPos);

    MyAssert(toPollPos > subflows.n && toPollPos < MAX_FDS, 1738);
    MyAssert(conns.connTab[connID].GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE, 1740);

    DWORD seq = pMsg->seq;
    DWORD & eSeq = expSeq[toPollPos];

    if (pMsg->msgType == SUBFLOW_MSG::MSG_CREATE) {
        //in that case, discard the pipe message. Just update seq
        MyAssert(seq == 0 && eSeq == 0, 1752);
        eSeq = 1;

        SUBFLOW_MSG * pCur = msgLists[toPollPos];
        while (pCur != NULL) {
            MyAssert(pCur->seq >= eSeq, 1753);
            if (pCur->seq == eSeq) eSeq++; else return;
            pCur = pCur->pNext;
        }

        msgCacheSize--;
        LOGD("*** E: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pMsg->bytesOnWire - 8));
        dataCacheSize -= pMsg->bytesOnWire - 8;
        int & bib = conns.connTab[connID].bytesInTCPBuffer;
        bib -= pMsg->bytesOnWire - 8;
        if (bib < 0) bib = 0;

        LOGD("SYN message processed (connID=%d). Exp seq updated to %u", (int)connID, eSeq);
        //printf("[%llu] toPollPos=%d outoforder=\n", get_current_millisecond(), toPollPos);//, GetMsgListBytes(toPollPos));
        return;
    }

    //MyAssert(seq >= eSeq, 1755);
    // Drop any packet with smaller SEQ due to reinjection or duplicate packets over multiple pipes.
    if (seq < eSeq) {
        //InfoMessage("Drop pMsg, connID: %u, seq: %u, eSeq: %u, pipe: %d", connID, seq, eSeq, pipeNo);
        pMsg->pNext = msgLists[MAX_FDS];
        msgLists[MAX_FDS] = pMsg;
        pMsg->drop = 1;
        //msgCacheSize--;
        //dataCacheSize -= pMsg->bytesOnWire - 8;
        //InfoMessage("seq<eSeq, pMsg %p, %u %u",
        //        pMsg, pMsg->connID, pMsg->seq);
        //oob = GetMsgListBytes(MAX_FDS);
        //printf("Abandon size: %d\n", oob);
        //iend = get_current_microsecond();
        //printf("[%llu][1] connID=%u seq=%u pipe=%d eseq=%u toPollPos=%d outoforder=%d t=%llu\n", end/1000, connID, seq, pipeNo, eSeq, toPollPos, oob, end-start);
        return;
    }

    if (msgLists[toPollPos] == NULL) {
        msgLists[toPollPos] = pMsg;
        pMsg->pNext = NULL;

        if (seq == eSeq) eSeq++;

    } else {
        SUBFLOW_MSG * pPrev = NULL;
        SUBFLOW_MSG * pCur = msgLists[toPollPos];
        int bInserted = 0;
        while (1) {
            //MyAssert(pCur->seq != seq, 1656);
            // drop duplicate message
            if (pCur->seq == seq) {
                pMsg->pNext = msgLists[MAX_FDS];
                msgLists[MAX_FDS] = pMsg;
                pMsg->drop = 1;
                //InfoMessage("seq>eSeq exist, pMsg %p, %u %u",
                //                        pMsg, pMsg->connID, pMsg->seq);
                //GetMsgListBytes(MAX_FDS);
                //msgCacheSize--;
                //dataCacheSize -= pMsg->bytesOnWire - 8;
                break;
            }

            if (pCur->seq > seq && !bInserted) {
                //prev--->cur becomes prev--->new_one--->cur
                if (pPrev == NULL) {
                    pMsg->pNext = pCur;
                    msgLists[toPollPos] = pMsg;
                } else {
                    pMsg->pNext = pCur;
                    pPrev->pNext = pMsg;
                }
                bInserted = 1;
                if (seq == eSeq) eSeq++;
            }

            if (pCur->seq == eSeq)
                eSeq++;
            else {
                if (bInserted) {
                    MyAssert(pCur->seq > eSeq, 1754);
                    break;
                }
            }

            pPrev = pCur;
            pCur = pCur->pNext;

            if (pCur == NULL) {
                if (!bInserted) {
                    pMsg->pNext = NULL;
                    pPrev->pNext = pMsg;
                    if (seq == eSeq) eSeq++;
                }
                break;
            }
        }
    }
    //InfoMessage("MsgList %d(%u) size: %d(%d) (%u[%d], %u)", toPollPos, connID, GetMsgListBytes(toPollPos), GetMsgListSize(toPollPos), seq, pipeNo, eSeq);

    kernelInfo.expectedSeq = eSeq;
//    LOGD("Pipe message (seq=%u pipeNo=%d) inserted to TCP buffer (connID=%d). Now expected seq=%u",
//         seq, pipeNo, (int)connID, eSeq);
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

            LOGD("*** D: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pMsg->bytesOnWire - 8));

            dataCacheSize -= pMsg->bytesOnWire - 8;
            //anyway the connection has been removed, so no need to decrease bytesInBuffer

            return;
        }

        pPrev = pCur;
        pCur = pCur->pNext;
    }

    MyAssert(0, 1733);
}

int CONNECTIONS::TransferFromSubflowsToTCP(int fromPollPos, int fromFD) {
//    LOGD("CONNECTIONS::TransferFromPipesToTCP");
    int nBytesRead = 0;
    int pipeType = subflows.GetPipeType(fromFD);
//    LOGD("fromFD: %d, pipeType: %d", fromFD, pipeType);
    if (fromPollPos == 1) {
//        LOGI("Feedback");
        if (subflows.feedbackType == 2)
            subflows.SendPIE();
    }

    pipeType = SUBFLOW::TCP_PIPE;
    BYTE * header = tcpOutput.headers[fromPollPos];
    SUBFLOW_MSG * & pMsg = tcpOutput.pCurMsgs[fromPollPos];

    while (1) {
//        LOGD("Looping in CONNECTIONS::TransferFromPipesToTCP");
        if (header[8] == 8) {
            //InfoMessage("[%llu] header read. pCur=%u",
            //		get_current_millisecond(), pMsg);
            if (pMsg == NULL) { //allocate space
                WORD connID = *((WORD *)header);
                DWORD seq = *((DWORD *)(header + 2));
                WORD len = *((WORD *)(header + 6));
//                LOGD("connID=%d seq=%d len=%d", connID, seq, len);

                pMsg = tcpOutput.Allocate(connID, connTab[connID].pollPos, seq, len, fromPollPos);
                if (pMsg == NULL) {
                    //InfoMessage("Warning: Buffer is full!!!");
                    //buffer is full
                    break;
                }
                //InfoMessage(
//                LOGD("[%llu] Received subflow msg header: connID=%d seq=%u len=%d pCur=%u",
//                     get_current_millisecond(), (int)connID, seq, (int)len, pMsg);
            }

            //now read the payload
            MyAssert(pMsg->bytesLeft > 0 && pMsg->bytesOnWire >= pMsg->bytesLeft + 8, 1666);

            int n = 0;
            switch (pipeType) {

                case SUBFLOW::TCP_PIPE:
                    n = read(fromFD, pMsg->pMsgData + pMsg->bytesOnWire - 8 - pMsg->bytesLeft, pMsg->bytesLeft);
                    break;

            }
            if (n > 0) {
                LOGD("%llu read %d bytes from WWAN %d", get_current_millisecond(), n, fromPollPos);
//                LOGD("n > 0 in CONNECTIONS::TransferFromPipesToTCP");

                lastPipeActivityTick = tickCount;
                nBytesRead += n;

                if (PROXY_SETTINGS::bDumpIO) {
                    DumpIO(PIPESOCKET_2_TCPBUFFER, n, fromPollPos, pMsg->connID, pMsg->seq);
                }
            }

            if (n == pMsg->bytesLeft) {
                //msg fully read
//                LOGD("n == pMsg->bytesLeft in CONNECTIONS::TransferFromPipesToTCP");
                //conns.CheckOWDMeasurement();                                                                  //// what does this function mean?
                pMsg->bytesLeft = 0; //a pipe message won't be transferred to TCP if bytesLeft > 0
                pMsg->Decode();
                header[8] = 0;

                SUBFLOW_MSG * _pMsg = pMsg;
                pMsg = NULL;

                //try to transfer immediately
                CONN_INFO & ci = connTab[_pMsg->connID];

                if (_pMsg->drop) {
                    tcpOutput.RemoveFromAbandonedList(_pMsg);
                    continue;
                }
                switch (_pMsg->inUseFlag) {
                    case CONN_INFO::CONN_INUSE:
                        //common case: try to transfer immediately
                        tcpOutput.TransferFromSubflowsToTCP(ci.pollPos);
                        break;

                    case CONN_INFO::CONN_RECENTLY_CLOSED:
                        //ignore the pipe message
                        MyAssert(_pMsg->inUseFlag == CONN_INFO::CONN_RECENTLY_CLOSED, 1736);
                        tcpOutput.RemoveFromAbandonedList(_pMsg);
                        //InfoMessage("Remove recent close, pMsg %p, %u %u",
                        //                 _pMsg, _pMsg->connID, _pMsg->seq);
                        //tcpOutput.GetMsgListBytes(MAX_FDS);
//                        LOGD("Ignore subflow message of a closed TCP connection (connID = %d)", (int)_pMsg->connID);
                        break;

                    case CONN_INFO::CONN_EMPTY:
                    {
                        if (_pMsg->msgType == SUBFLOW_MSG::MSG_CREATE) {
                            //MyAssert(proxyMode == PROXY_MODE_REMOTE && _pMsg->seq == 0, 1688);
                            //conns.ConnectToRemoteServer(_pMsg->connID, _pMsg->serverIP, _pMsg->serverPort);      //// remote proxy only
                            /*
                            It is possible that the connection is not yet established,
                            but data coming from pipes accumulates - that is handled by an
                            additional call of TransferFromPipesToTCP() in ConnectToRemoteServerDone()
                            */
                        } else {

                            MyAssert(_pMsg->inUseFlag == CONN_INFO::CONN_EMPTY, 1736);
                        }
                        break;
                    }
                    default:
                        MyAssert(0, 1728);
                }

                continue;	//read next msg
            } else if (n > 0 && n < pMsg->bytesLeft) {
                pMsg->bytesLeft -= n;
                break;
            } else if (n == 0 || (n < 0 && errno == ECONNRESET)) {
                //pipes should never be closed
                LOGD("Subflow closed No.1");
                keepRunning = 0;
                MyAssert(0, 1667);
                break;
            } else if ((n < 0 && pipeType == SUBFLOW::TCP_PIPE && errno == EWOULDBLOCK )
                       || (n < 0 && pipeType == SUBFLOW::UDP_PIPE))  {
                //nothing to read
                break;
            } else {
                //error
                LOGD("Error reading from subflow %d: %s(%d)",
                     (int)fromPollPos, strerror(errno), errno
                );
                MyAssert(0, 1668);
            }
        } else {
            MyAssert(header[8]>=0 && header[8]<8, 1658);
            MyAssert(pMsg == NULL, 1665);

//            LOGD("Pipe header not fully read yet (%d), continue...", header[8]);
            READ_SUBFLOW_MSG_HEADER:
//            LOGD("after head in CONN");
            int nLeft = 8 - header[8];

            int n = 0;
            switch (pipeType) {

                case SUBFLOW::TCP_PIPE:
                    n = read(fromFD, header + header[8], nLeft);
                    break;
            }

            if (n > 0) {
                LOGD("%llu read %d bytes from WWAN %d", get_current_millisecond(), n, fromPollPos);
//                LOGD("n>0 in CONN");

                lastPipeActivityTick = tickCount;
                nBytesRead += n;

                if (PROXY_SETTINGS::bDumpIO) {
                    DumpIO(PIPESOCKET_2_TCPBUFFER, n, fromPollPos, -1, 0);
                }
            }
//            LOGD("after n>0 in CONN");

            if (PROXY_SETTINGS::bLateBindingLP) {
                //remove "nop" (0xFF) by adjusting n
                if (header[8] == 0 && n > 0) {
                    int k;
                    BYTE * p = header;
                    for (k=0; k<n; k++) if (*p != 0xFF) break; else p++;
                    if (k>0) {
                        LOGD("Read %d NOP bytes", k);
                        if (k<n) memmove(header, p, n-k);
                        n = n - k;
                        header[8] += n;
                        goto READ_SUBFLOW_MSG_HEADER;
                    }
                }
            }
//            LOGD("To check n==nLeft");

            if (n == nLeft) {
                //now we have a full header
                header[8] = 8;
                //InfoMessage("full header: %d", header[8]);
            } else if (n > 0 && n < nLeft) {
                header[8] += n;
                //InfoMessage("read header: %d", header[8]);
                break;
            } else if (n == 0 || (n < 0 && errno == ECONNRESET)) {
                //pipes should never be closed
                LOGD("Subflow closed No.2, n is %d, nleft is %d",n,nLeft);
                keepRunning = 0;
                MyAssert(0, 1659);
                break;
            } else if ((n < 0 && pipeType == SUBFLOW::TCP_PIPE && errno == EWOULDBLOCK )
                       || (n < 0 && pipeType == SUBFLOW::UDP_PIPE)) {
                //nothing to read
                //InfoMessage("Nothing to read, header len: %d", header[8]);
                break;
            } else {
                //error
                LOGD("Error reading from subflow: %s(%d) Pipe #%d",
                     strerror(errno), errno, (int)fromPollPos
                );
                MyAssert(0, 1660);
            }
        }
    }

#if TRANS == 1
    if (pipeType == SUBFLOW::TCP_PIPE) {
        if (PROXY_SETTINGS::bUseQuickACK && nBytesRead > 0) {
            SetQuickACK(fromFD);
        }
    }
#endif
//    LOGD("---------Exit TransferFromPipesToTCP----------");
    return nBytesRead;
}

int CONNECTIONS::TransferFromTCPToSubflows(int fromPollPos, int fromFD) {

    int bRemoved = 0;	//whether TCP connection is removed

//    LOGD("CONNECTIONS::TransferFromTCPToSubflows (fromPollPos=%d, fromFD=%d)", fromPollPos, fromFD);

    //toPollPos: pipe (1..nPipes)
    //fromPollPos: TCP connection (nPipes+1..)

    int bNewConnection = 0;
    if (fromPollPos > 0) {
        bNewConnection = !peersExt[fromPollPos].bSentSYNToPipe;
    }
//    MyAssert(proxyMode == PROXY_MODE_LOCAL || !bNewConnection, 1696);

    int bSendSYNACK = 0;
    if (fromPollPos > 0) {
        bSendSYNACK = peersExt[fromPollPos].bToSendSYNACK;
    }
//    MyAssert(proxyMode == PROXY_MODE_REMOTE || !bSendSYNACK, 2186);

    //two possible reasons for POLLFD_EXT::EST_FAIL
    //(1) when writing to TCP, find the connection has been RST
    //(2) failed establishment
    int bTerminateConnection = 0;
    if (fromPollPos > 0) {
        bTerminateConnection = peersExt[fromPollPos].establishStatus == POLLFD_EXT::EST_FAIL;
    }

    //MyAssert(proxyMode == PROXY_MODE_REMOTE || !bTerminateConnection, 1710);

    WORD connID = 0;
    if (fromPollPos > 0) {
        connID = peersExt[fromPollPos].connID;
    }

//    int toPollPos = (subflows.*subflows.SelectPipeFunc_Main)(connID);
    int toPollPos = 1;

    if (conns.connTab[connID].bytesInPipeBuffer >= PROXY_SETTINGS::pipePerConnBufDataCapacity) return bRemoved;

    BYTE * pDataBuffer = subflowOutput.pDataBuffer[toPollPos];
    BYTE * & pDataHead = subflowOutput.pDataHead[toPollPos];
    BYTE * & pDataTail = subflowOutput.pDataTail[toPollPos];
    int & dataBufSize = subflowOutput.dataBufSize[toPollPos];

    SUBFLOW_MSG * pMsgBuffer = subflowOutput.pMsgBuffer[toPollPos];
    SUBFLOW_MSG * & pMsgHead = subflowOutput.pMsgHead[toPollPos];
    SUBFLOW_MSG * & pMsgTail = subflowOutput.pMsgTail[toPollPos];
    int & msgBufSize = subflowOutput.msgBufSize[toPollPos];

    const BYTE * pDataEnd = pDataBuffer + PROXY_SETTINGS::pipeBufDataCapacity;
    const SUBFLOW_MSG * pMsgEnd = pMsgBuffer + PROXY_SETTINGS::pipeBufMsgCapacity;

    if (connID > 0) MyAssert(conns.connTab[connID].GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE, 1745);

    DWORD lastSeq = 0;

    if (fromPollPos > 0) {
        DWORD & seq = subflowOutput.genSeq[fromPollPos];
        MyAssert(seq == 0 || !bNewConnection, 1677);
    }

    int avalLen = PROXY_SETTINGS::pipeBufDataCapacity - dataBufSize;
//    LOGD("the 1st avalLen is %d",avalLen);
    //need extra 8 bytes for subflow header of data
    //need extra 9 bytes for MSG_CLOSE / MSG_SYNACK
    //need extra 14 bytes for MSG_CREATE
    //so we choose max(8,9,14)=14 (see BUFFER_PIPES::IsPipeFull)
    avalLen -= 14;
//    LOGD("the 2nd avalLen is %d",avalLen);
    MyAssert(avalLen > 0 && msgBufSize < PROXY_SETTINGS::pipeBufMsgCapacity, 1711);

    if (avalLen > PROXY_SETTINGS::maxPayloadPerMsg)
        avalLen = PROXY_SETTINGS::maxPayloadPerMsg;
//    LOGD("the 3rd avalLen is %d",avalLen);
    //8 is the common header size. Change the number when protocol format changes
    BYTE * pRealData = pDataTail + 8;

    //handling the circular queue
    if (pRealData >= pDataEnd) pRealData -= PROXY_SETTINGS::pipeBufDataCapacity;
    int r1 = pDataEnd - pRealData;
    if (r1 >= avalLen) r1 = avalLen;
    pMsgTail->msgType = SUBFLOW_MSG::MSG_EMPTY;
    MyAssert(r1 > 0, 1750);

    int bFirst = 1;
    while (1) {
        if (fromPollPos <= 0) {
            pMsgTail->SetOWD(pDataTail, owd_pipeNo, owd_ack);
            break;
        }
        // distinguish SYN packet
        if (bNewConnection) {
            CONN_INFO * pNewConn = &connTab[connID];
            pMsgTail->SetCreate(connID,	pDataTail, pNewConn->serverIP, pNewConn->serverPort);
            break;
        }

        if (bTerminateConnection) {
            pMsgTail->SetClose(connID, pDataTail, SUBFLOW_MSG::MSG_CLOSE_RST);
            break;
        }

        if (bSendSYNACK) {
//            MyAssert(proxyMode == PROXY_MODE_REMOTE && !PROXY_SETTINGS::bZeroRTTHandshake, 2190);
            pMsgTail->SetSYNACK(connID, pDataTail);
            break;
        }

        if (!peersExt[fromPollPos].bReceivedSYNACK) {
            //InfoMessage("@@@ SYNACK Haven't received. Jump out.");
//            MyAssert(proxyMode == PROXY_MODE_LOCAL && !PROXY_SETTINGS::bZeroRTTHandshake, 2192);
            break;
        }

        int n;
        //skip the pipe header, read the real payload
        if (bFirst) {
//            LOGD("TEST: R1 IS %d", r1);
            n = read(fromFD, pRealData, r1);
//            LOGD("bFirst,The length of data is %d",n);
        }
        else {
//            LOGD("TEST: R1 IS %d", avalLen - r1);
            n = read(fromFD, pRealData, avalLen - r1);
//            LOGD("NOT bFirst,The length of data is %d",n);
        }
        MyAssert(fromPollPos > 0, 1751);
        DWORD & seq = subflowOutput.genSeq[fromPollPos];

        if (n>0) {

            if (PROXY_SETTINGS::bLateBindingRP) {
                //KERNEL_INTERFACE::CopyUserData(connID, pRealData, n);
                //if (connID == 1) InfoMessage("Copy user data to conn 1: %d bytes", n);
            }

            if (PROXY_SETTINGS::bDumpIO) {
//                DumpIO(TCPSOCKET_2_PIPEBUFFER, n, toPollPos, connID, seq);
            }

            if (bFirst) {
                pMsgTail->SetData(connID, pDataTail, n);
            } else {
                pMsgTail->SetMoreData(n);
            }

        } else if (n == 0 || (n < 0 && errno == ECONNRESET)) { //closed by client
            if (bFirst) {
//                InfoMessage("TCP connection %s: %s",
//                            n == 0 ? "CLOSED" : "RESET",
//                            DumpConnection(connID, 0)
//                );
                SafeClose(fromFD, fromPollPos);

                if (n == 0) {
                    pMsgTail->SetClose(connID, pDataTail, SUBFLOW_MSG::MSG_CLOSE_FIN);
                    //if (PROXY_SETTINGS::bLateBindingRP) KERNEL_INTERFACE::SetRPFIN(connID, SUBFLOW_MSG::MSG_CLOSE_FIN);
                } else {
                    pMsgTail->SetClose(connID, pDataTail, SUBFLOW_MSG::MSG_CLOSE_RST);
                    // (PROXY_SETTINGS::bLateBindingRP) KERNEL_INTERFACE::SetRPFIN(connID, SUBFLOW_MSG::MSG_CLOSE_RST);
                }

                lastSeq = seq;
                RemoveConnection(fromPollPos);
                bRemoved = 1;
            }
            //if not bFirst, leave it to next read
        } else if (n < 0 && errno == EWOULDBLOCK) {
            //non-blocking read
        } else {
//            ErrorMessage("TCP Read error: %s(%d) %s",
//                         strerror(errno), errno,
//                         DumpConnection(connID, 0)
//            );
            MyAssert(0, 1638);
        }

        if (n != r1 || avalLen == r1) { //n<=0 also satisfies
            //we don't need second read
            break;
        }

        if (!bFirst) break; else {
            bFirst = 0;
            pRealData = pDataBuffer;
        }
    }

    if (pMsgTail->msgType == SUBFLOW_MSG::MSG_EMPTY) return bRemoved;

    ////////////// 4/3/2014 Handle delayed FIN /////////////////////////
#ifdef FEATURE_DELAYED_FIN
    if (pMsgTail->msgType == SUBFLOW_MSG::MSG_CLOSE && pMsgTail->closeReason == SUBFLOW_MSG::MSG_CLOSE_FIN) {
		if (tickCount - lastPipeActivityTick >= delayedFins.d1) {
			//delay it
			delayedFins.EnQueue(pMsgTail, lastSeq);

			//bTerminateConnection uses RST
			MyAssert(!bNewConnection && !bTerminateConnection, 1825);
			pMsgTail->msgType = SUBFLOW_MSG::MSG_EMPTY;
			return;
		}
	}
#endif
    ///////////////////////////////////////////////////////////////////
    if (fromPollPos > 0) {
        DWORD & seq = subflowOutput.genSeq[fromPollPos];
        if (pMsgTail->msgType == SUBFLOW_MSG::MSG_CLOSE) {
            if (bTerminateConnection) {
                pMsgTail->Encode(seq++, pDataTail, pDataBuffer, pDataEnd);
            } else {
                pMsgTail->Encode(lastSeq, pDataTail, pDataBuffer, pDataEnd);
            }
        } else {
            pMsgTail->Encode(seq++, pDataTail, pDataBuffer, pDataEnd);
        }
    } else {
        pMsgTail->Encode(owd, pDataTail, pDataBuffer, pDataEnd);
    }

    //update the circular queue data structure
    if (pDataHead == NULL) pDataHead = pDataTail;
    dataBufSize += pMsgTail->bytesOnWire;
    pDataTail += pMsgTail->bytesOnWire;
    if (pDataTail >= pDataEnd) pDataTail -= PROXY_SETTINGS::pipeBufDataCapacity;


    conns.connTab[connID].bytesInPipeBuffer += pMsgTail->bytesOnWire;
    connTab[connID].accuChunkBytes += pMsgTail->bytesOnWire;

    connTab[connID].accuBurstyBytes += pMsgTail->bytesOnWire;
    connTab[connID].r_clock = GetHighResTimestamp();
    connTab[connID].l_clock = GetLogicTime();

    if (pMsgHead == NULL) pMsgHead = pMsgTail;
    msgBufSize++;
    pMsgTail++;
    if (pMsgTail >= pMsgEnd) pMsgTail -= PROXY_SETTINGS::pipeBufMsgCapacity;

    if (bNewConnection) {
        peersExt[fromPollPos].bSentSYNToPipe = 1;
    }

    if (bTerminateConnection) {
        RemoveConnection(fromPollPos);
        bRemoved = 1;
    }

    if (bSendSYNACK) {
        peersExt[fromPollPos].bToSendSYNACK = 0;
    }
    // InfoMessage("After disabling BT, toPollPos becomes %d", toPollPos);
    if (subflowOutput.TransferFromTCPToSubflows(toPollPos, peers[toPollPos].fd) > 0) {
//        LOGD("delayed fins to subflow");
        TransferDelayedFINsToSubflows();
    }

    return bRemoved;
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


void CONNECTIONS::ResetReadNotificationEnableState() {
    //set both to -1 so that next time when
    //EnableXXXReadNotifications(bEnable) is called, cached results are not used
    //since bEnable is either 1 or 0

//    VerboseMessage("ResetReadNotificationEnableState");

//    bTCPReadNtfEnabled = -1;	//lazy init: neither 1 nor 0
//    bPipeReadNtfEnabled = -1;	//lazy init: neither 1 nor 0
}

void CONN_INFO::SetInUseStatus(int s) {
//    LOGD("CONN_INFO::SetInUseStatus");

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

void BUFFER_TCP::TransferFromSubflowsToTCP(int toPollPos) {
//    LOGD("BUFFER_TCP::TransferFromSubflowsToTCP (toPollPos=%d)", toPollPos);

    //InfoMessage("Enter TransferFromPipesToTCP.");
    //uint64_t start = get_current_millisecond();
    int size = 0;

    int bFree = 0;

    BYTE * & writeData = writeBuf[toPollPos];
    BYTE * & writePosNow = writePos[toPollPos];

    SUBFLOW_MSG * pCur = msgLists[toPollPos];
    SUBFLOW_MSG * newpCur = NULL;
    int count = 0;
    DWORD eSeq = expSeq[toPollPos];
    int nBytesWritten = 0;

    int bStop = 0;
    int toFD = conns.peers[toPollPos].fd;
    MyAssert(toFD >= 0, 1681);

    int flag = 0;
    if (writePosNow != NULL) {
        size = writeSize[toPollPos];
        flag = 1;
        goto WRITE_REMAIN;
    }
    CHECK_MSGLIST:
    flag = 0;
    while (pCur != NULL) {
        //VerboseMessage("*** pCur->seq=%u eSeq=%u ***", pCur->seq, eSeq);
        MyAssert(pCur->seq != eSeq, 1679);

        //"bytesLeft" used in the following functions:
        //(1) CONNECTIONS::TransferFromPipesToTCP
        //(2) BUFFER_TCP::TransferFromPipesToTCP
        //(3) BUFFER_TCP::Allocate

        //bytesLeft > 0: the message hasn't been fully read to the buffer, x bytes to go
        //bytesLeft = 0: the message has been fully read to the buffer, but it has not been written to TCP
        //bytesLeft < 0: the message has been fully read to the buffer, but it hasn't been fully written to TCP (only -x bytes have been done)

        if (pCur->seq > eSeq || pCur->bytesLeft > 0) {
            bStop = 3;
            break;
        }

        switch (pCur->msgType) {
            case SUBFLOW_MSG::MSG_DATA:
            {
                int x = -pCur->bytesLeft;	//# bytes already written
                int toWrite = pCur->bytesOnWire - 8 - x;

                //VerboseMessage("*** x=%d toWrite=%d pCur->pMsgData=%x ***", x, toWrite, pCur->pMsgData);

                MyAssert(x >= 0 && toWrite > 0, 1683);
                //memcpy(writeData, pCur->pMsgData + x, toWrite);
                //pCur = pCur->pNext;
                size += toWrite;
                count += 1;
                writeData = pCur->pMsgData + x;
                //continue;

                break;
            }

            case SUBFLOW_MSG::MSG_CREATE:
            {
                //Already handled in CONNECTIONS::TransferFromPipesToTCP
                MyAssert(0, 1743);
            }

            case SUBFLOW_MSG::MSG_SYNACK:
            {
                //MyAssert(
                //        proxyMode == PROXY_MODE_LOCAL &&
                //        !PROXY_SETTINGS::bZeroRTTHandshake &&
                //        !conns.peersExt[toPollPos].bReceivedSYNACK,
                //        2189);

                //at this moment CMAT doesn't know the connection is closed by the client
                //(if it indeed is), since the socket has been blocked before SYNACK is received
                conns.peersExt[toPollPos].bReceivedSYNACK = 1;

                LOGD("Received SYNACK. Read data now.");

                int bRemoved = conns.TransferFromTCPToSubflows(toPollPos, toFD);
                //int bRemoved = 0;

                //at this time the connection might be closed by the client

                if (bRemoved) {
                    bStop = 5;
                    goto FINISH_TRANSFER;
                } else {
                    break;
                }
            }

            case SUBFLOW_MSG::MSG_CLOSE:
            {
                switch (pCur->closeReason) {
                    case SUBFLOW_MSG::MSG_CLOSE_FIN:
                    {

                        LOGD("[%llu] Closing TCP %s by FIN",
                             get_current_millisecond(),
                             conns.DumpConnection(conns.peersExt[toPollPos].connID, 0)
                        );

                        break;
                    }

                    case SUBFLOW_MSG::MSG_CLOSE_RST:
                    {
                        LOGD("[%llu] Closing TCP %s by RST",
                             get_current_millisecond(),
                             conns.DumpConnection(conns.peersExt[toPollPos].connID, 0)
                        );

                        //send RST instead of FIN
                        struct linger so_linger;
                        so_linger.l_onoff = 1;
                        so_linger.l_linger = 0;
                        int r = setsockopt(toFD, SOL_SOCKET, SO_LINGER, &so_linger, sizeof (so_linger));
                        MyAssert(r == 0, 1713);

                        break;
                    }


                    default:
                        MyAssert(0, 1708);
                }

                conns.SafeClose(toFD, toPollPos);

                //"manually" process this FIN pipe message
                msgLists[toPollPos] = pCur->pNext;
                msgCacheSize--;
//                LOGD("*** B: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pCur->bytesOnWire - 8));
                dataCacheSize -= pCur->bytesOnWire - 8;

                //anyway the connection will be removed, so no need to decrease bytesInBuffer
                conns.RemoveConnection(toPollPos);

                //if (PROXY_SETTINGS::bLateBindingRP) KERNEL_INTERFACE::SetLPFIN(pCur->connID);

                bStop = 2;
                goto FINISH_TRANSFER;
            }

            default:
                MyAssert(0, 1680);
        }



        if (bStop) break;

        msgCacheSize--;

//        LOGD("*** C: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pCur->bytesOnWire - 8));

        dataCacheSize -= pCur->bytesOnWire - 8;


        int & bib = conns.connTab[conns.peersExt[toPollPos].connID].bytesInTCPBuffer;
        bib -= pCur->bytesOnWire - 8;
        if (bib < 0) bib = 0;

        pCur = pCur->pNext;

        if (nBytesWritten >= PROXY_SETTINGS::tcpTransferUnit && pCur != NULL) {
            bStop = 4;
            break;
        }
    }

    newpCur = pCur;
    if (count > 1) {
        pCur = msgLists[toPollPos];
        writeData = new BYTE[size];
        BYTE * dst = writeData;
        writePosNow = dst;
        while (pCur != newpCur) {
            switch (pCur->msgType) {
                case SUBFLOW_MSG::MSG_DATA:
                {
                    int x = -pCur->bytesLeft;   //# bytes already written
                    int toWrite = pCur->bytesOnWire - 8 - x;

                    MyAssert(x >= 0 && toWrite > 0, 1683);
                    memcpy(dst, pCur->pMsgData + x, toWrite);
                    dst += toWrite;
                }
                    break;
                default:
                    break;
            }
            pCur = pCur->pNext;
        }
    } else if (count == 1) {
        writePosNow = writeData;
    }
    msgLists[toPollPos] = newpCur;
    WRITE_REMAIN:
    if (size > 0) {
        // write data now
        int w = write(toFD, writePosNow, size);
        if (w == size) {
            bFree = 1;
            writePosNow = NULL;
            writeSize[toPollPos] = 0;
        } else if (w >=0 && w < size) {
            bStop = 1;
            writeSize[toPollPos] = size - w;
            writePosNow += w;
            goto LABEL_WRITE;
        } else if (w < 0 && errno == EWOULDBLOCK) {
            bStop = 1;
            goto LABEL_WRITE;
        } else if (w < 0 && (errno == ECONNRESET || errno == EPIPE)) {
            LOGD("SafeClose fd %d.", toPollPos);
            conns.SafeClose(toFD, toPollPos);
            conns.peersExt[toPollPos].establishStatus = POLLFD_EXT::EST_FAIL;
            conns.TransferFromTCPToSubflows(toPollPos, -1);
            bFree = 1;
            writePosNow = NULL;
            writeSize[toPollPos] = 0;
            goto FINISH_TRANSFER;
        } else {
            MyAssert(0, 1692);
        }
    }

    if (bFree == 1 && count > 1) {
        delete[] writeData;
        writeData = NULL;
    }
    if (flag == 1) {
        goto CHECK_MSGLIST;
    }

    LABEL_WRITE:
    //MyAssert((pCur == NULL) == (bStop == 0),  1717);
//	msgLists[toPollPos] = newpCur;
    conns.EnableTCPWriteNotification(toPollPos, bStop == 1 || bStop == 4);

    //bStop == 0: All data sent
    //bStop == 1: Not all data sent due to TCP sock buffer full
    //bStop == 2: connection closed
    //bStop == 3: Not all data sent due to out-of-order messages
    //bStop == 4: Reach transfer unit size
    //bStop == 5: receive SYN-ACK pipe msg and connection closed

    FINISH_TRANSFER:
    ////////////////////////////// 08/07/2014 //////////////////////////////
    //more space? Then try to enable pipe read ntf
    /*
    if (GetBufferFullStatus() != BUFFER_FULL) {
        conns.EnablePipeReadNotifications(1);
    }
    */
    ////////////////////////////////////////////////////////////////////////
    ;
    //InfoMessage("End transfer %llu ms, %d B.", get_current_millisecond() - start, size);
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

//    LOGD("TCP write notification changed. New value=%d connID=%d",
//         bEnable, (int)peersExt[pollPos].connID);

    if (bEnable) {
        peers[pollPos].events |= POLLWRNORM;
//        LOGD("TCP write notification ENABLED. ConnID=%d", (int)peersExt[pollPos].connID);
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
