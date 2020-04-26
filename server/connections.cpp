//#define _GNU_SOURCE
#include "stdafx.h"

#include "connections.h"
#include "meta_buffer.h"
#include "tools.h"
#include "hints.h"
#include "kernel_info.h"

extern int proxyMode;
extern struct SUBFLOWS subflows;
extern struct CONNECTIONS conns;
extern struct BUFFER_SUBFLOWS subflowOutput;	
extern struct BUFFER_TCP tcpOutput;
extern struct META_BUFFER metaBuffer;
extern struct DELAYED_FINS delayedFins;
extern struct KERNEL_INFO kernelInfo;

extern int tickCount;
extern int lastSubflowActivityTick;

extern int keepRunning;
extern int statThreadRunning;

extern FILE * ofsIODump;
extern FILE * ofsLatency;
extern FILE * ofsDebug;
extern int accBytes[3];
extern int bLateBindingMPBond;

extern int kfd;
WORD lastConnID;
struct timespec wait2 = {0, 100000000}, rem2;
int testCounter = 0;
int rtt1, rtt2;

int CONNECTIONS::GetTCPBufferSizeAllConns() {
        int buf = 0;
        for (int i = 0; i < 65535; i++) {
                //if (conns.connTab[i].GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE)
                        buf += conns.connTab[i].bytesInTCPBuffer;
        }
        return buf;
}

int CONNECTIONS::GetInUseConns() {
	return 0;
}

int CONNECTIONS::Setup() {
	VerboseMessage("CONNECTIONS::Setup");

	memset(peers, 0, sizeof(peers));
	memset(peersExt, 0, sizeof(peersExt));
	for (int i=0; i < MAX_FDS; i++) { 
		peers[i].fd = -1;		
	}
	
	memset(connTab, 0, sizeof(connTab));

	nextConnID = 0;	//not used in remote proxy mode	
	
	InfoMessage("Remote proxy on");

	for (int i = 0; i < subflows.n; i++) {
		peers[i+1].fd = subflows.fd[i];
		peers[i+1].events = POLLRDNORM | POLLRDHUP;
		peersExt[i+1].connID = 0;
		//they are not used by subflows, setting to succ/done to ensure they are ignored
		peersExt[i+1].establishStatus = POLLFD_EXT::EST_SUCC;
		peersExt[i+1].bSentSYNToSubflow = 1;
		peersExt[i+1].bToSendSYNACK = 0;
		peersExt[i+1].bReceivedSYNACK = 1;
	}

	ResetReadNotificationEnableState();

	//local proxy mode: peers[0] is listen socket, peers[1..n] are subflows
	//remote proxy mode: peers[0] not used, peers[1..n] are subflows
	maxIdx = subflows.n;
    lastOWD = 0;

	return R_SUCC;
}

const char * CONNECTIONS::DumpConnection(WORD connID) {
	MyAssert(connID != 0, 1629);

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

void CONNECTIONS::RemoveConnection(int pollPos) {
	VerboseMessage("CONNECTIONS::RemoveConnection (pollPos=%d)", pollPos);

	//only TCP connections can be removed. Subflows cannot.
	MyAssert(pollPos > subflows.n, 1618);

	WORD connID = peersExt[pollPos].connID;
	MyAssert(connID > 0 && connTab[connID].GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE, 1631);
	connTab[connID].SetInUseStatus(CONN_INFO::CONN_EMPTY);
	
	connTab[connID].bytesInTCPBuffer = 0;
	connTab[connID].bytesInSubflowBuffer = 0;
	connTab[connID].accuChunkBytes = 0;
	connTab[connID].lastSubflowID = -1;
		
	peers[pollPos].fd = -1;
	peers[pollPos].events = 0;
	peers[pollPos].revents = 0;
	peersExt[pollPos].connID = 0;
	peersExt[pollPos].bSentSYNToSubflow = 0;
	peersExt[pollPos].establishStatus = POLLFD_EXT::EST_NOT_CONNECTED;
	peersExt[pollPos].connectTime = 0;

	peersExt[pollPos].bToSendSYNACK = 0;
	peersExt[pollPos].bReceivedSYNACK = 0;

	subflowOutput.RemoveTCPConnection(pollPos);
	tcpOutput.RemoveTCPConnection(pollPos, connID);

	//try to decrease maxIdx
	while (maxIdx > subflows.n && peers[maxIdx].fd == -1) {
		maxIdx--;
	}

	VerboseMessage("Connection removed (connID = %d, pos = %d, maxIdx = %d)", (int)connID, pollPos, maxIdx);
}

int CONNECTIONS::AddTCPConnection(int clientFD, 
								  DWORD clientIP, WORD clientPort, 
								  DWORD serverIP, WORD serverPort,	/*must be 0 for local proxy*/
								  WORD connID	/* must be 0 for local proxy */
) {
	VerboseMessage("CONNECTIONS::AddTCPConnection");

	MyAssert(clientFD >= 0, 1716);
	int pollPos = -1;
	for (int i = subflows.n + 1; i < MAX_FDS; i++) {
		if (peers[i].fd < 0) {
			peers[i].fd = clientFD;
			peers[i].events = POLLRDNORM;
			peers[i].revents = 0;
			if (i > maxIdx) maxIdx = i;

			MyAssert(connID > 0 && ((connID & 0xFF) != 0xFF), 1695);

			CONN_INFO & ci = connTab[connID];
			MyAssert(ci.GetInUseStatus(tickCount) == CONN_INFO::CONN_EMPTY, 1694);

			ci.clientIP = clientIP;
			ci.clientPort = clientPort;
			//ci.fd = clientFD;
			ci.pollPos = i;
			ci.SetInUseStatus(CONN_INFO::CONN_INUSE);
			
			ci.bytesInTCPBuffer = 0;
			ci.bytesInSubflowBuffer = 0;
			ci.accuChunkBytes = 0;
			ci.lastSubflowID = -1;

			pollPos = i;

			ci.serverIP = serverIP;
			ci.serverPort = serverPort;

			peersExt[i].connID = connID;
			peersExt[i].bSentSYNToSubflow = 1;	//not used in remote proxy - setting to 1 ensures it will be ignored
			peersExt[i].establishStatus = POLLFD_EXT::EST_NOT_CONNECTED;
			peersExt[i].connectTime = 0;
			
			peersExt[i].bToSendSYNACK = 0;
			peersExt[i].bReceivedSYNACK = 1;

			InfoMessage("[%llu] New outgoing connection: %s", get_current_microsecond(), DumpConnection(connID));
			ci.hints.print();

			subflowOutput.NewTCPConnection(i);
			tcpOutput.NewTCPConnection(i);
			break;
		}
	}

	ResetReadNotificationEnableState();

	MyAssert(pollPos != -1, 1608);
	return pollPos;
}

void BUFFER_SUBFLOWS::Setup() {
	VerboseMessage("BUFFER_SUBFLOWS::Setup");

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

	// Keep track of whether the mesages are received by LP
	// by using TCP SEQ and ACK information
	memset(pMsgUnacked, 0, sizeof(pMsgUnacked));
	memset(pMsgUnaHead, 0, sizeof(pMsgUnaHead));
	memset(pMsgUnaTail, 0, sizeof(pMsgUnaTail));
	memset(pMsgTimestamp, 0, sizeof(pMsgTimestamp));
	memset(unaSize, 0, sizeof(unaSize));
	memset(unaBytes, 0, sizeof(unaBytes));

	// Reinjection
	memset(pMsgReinject, 0, sizeof(pMsgReinject));
	memset(pMsgRnjHead, 0, sizeof(pMsgRnjHead));
	memset(pMsgRnjTail, 0, sizeof(pMsgRnjTail));
	memset(rnjSize, 0, sizeof(rnjSize));
	memset(bActiveReinject, 0, sizeof(bActiveReinject));

	memset(genSeq, 0, sizeof(genSeq));
	memset(lastUnaSeq, 0, sizeof(lastUnaSeq));
	memset(transportACK, 0, sizeof(transportACK));
	memset(transportSeq, 0, sizeof(transportSeq));

	for (int i=1; i<=subflows.n; i++) {
		pDataBuffer[i] = new BYTE[PROXY_SETTINGS::subflowBufDataCapacity];
		pMsgBuffer[i] = new SUBFLOW_MSG[PROXY_SETTINGS::subflowBufMsgCapacity];
		pMsgUnacked[i] = new SUBFLOW_MSG[PROXY_SETTINGS::subflowBufMsgCapacity];
		pMsgReinject[i] = new SUBFLOW_MSG[PROXY_SETTINGS::subflowBufMsgCapacity];

		MyAssert(pDataBuffer[i] != NULL && pMsgBuffer[i] != NULL
			&& pMsgUnacked[i] != NULL && pMsgReinject[i] != NULL, 1640);

		pDataHead[i] = NULL; pDataTail[i] = pDataBuffer[i];		
		pMsgHead[i] = NULL; pMsgTail[i] = pMsgBuffer[i];
		pMsgUnaHead[i] = NULL; pMsgUnaTail[i] = pMsgUnacked[i];
		pMsgTimestamp[i] = NULL;
		pMsgRnjHead[i] = NULL; pMsgRnjTail[i] = pMsgReinject[i];
	}

	// Added for DMM: Set up UDP receiver for feedback over the secondary subflow
	MonitorMPBond();
	bSetup = 1;
}

void * MonitorMPBondThread(void * arg) {
	
	struct sockaddr_in remaddr;     /* remote address */
    socklen_t addrlen = sizeof(remaddr);            /* length of addresses */
    while (1) {
    	unsigned char buf[2048];     /* receive buffer */
    	int recvlen = recvfrom(subflowOutput.monitorMPBondFD, buf, 2048, 0, (struct sockaddr *)&remaddr, &addrlen);
		if (recvlen > 0) {
			// InfoMessage("[MPBond] recvlen = %d", recvlen);
        	// buf[recvlen] = 0;
        	int subflowNo = *((int *) buf);//*((int *)(buf + 16));
        	int throughput = *((int *)(buf + 4));//*((int *) buf);
            int netRTT = *((int *)(buf + 8));//*((int *)(buf + 4));
            int byteInPipe = *((int *)(buf + 12));//*((int *)(buf + 8));
            int byteOnDevice = *((int *)(buf + 12));
            unsigned long long recvAck = *((unsigned long long *)(buf + 16));
        	// InfoMessage("[MPBond] received message for subflow %d: thrpt=%d, netRTT=%d, bytesInPipe=%d, byteOnDevice=%d, ack=%d",
	        // 	 subflowNo, throughput, netRTT, byteInPipe, byteOnDevice, recvAck);
        	if (subflowNo == 1) {
        		
        		// if (kernelInfo.primaryAck < recvAck) {
    				kernelInfo.primaryAck = recvAck; // reset for every conn
    			// }
    			// InfoMessage("[MPBond] received ack=%d", recvAck);
        	}
        	else {
        		
	        	kernelInfo.pipeRTT[subflowNo] = netRTT;
	        	kernelInfo.pipeBW[subflowNo] = throughput;
	        	kernelInfo.bytesInPipe[subflowNo] = byteInPipe;
	        	// InfoMessage("[MPBond] received byteInPipe=%d", byteInPipe);
        	}
        	
        	// kernelInfo.bytesOnDevice[subflowNo] = byteOnDevice;
    	}
    	else {
    		InfoMessage("[MPBond] recvlen = %d", recvlen);
    	}
    }
}

void BUFFER_SUBFLOWS::MonitorMPBond() {

	if ((monitorMPBondFD = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        InfoMessage("cannot create UDP socket for monitorMPBondFD");
    }
	struct sockaddr_in udpServerAddr;
	memset(&udpServerAddr, 0, sizeof(sockaddr_in));	
	udpServerAddr.sin_family = AF_INET;
	udpServerAddr.sin_port = htons(4399);
	udpServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	// inet_pton(AF_INET, "141.212.110.129", &udpServerAddr.sin_addr);
	if (bind(monitorMPBondFD, (struct sockaddr *)&udpServerAddr, sizeof(udpServerAddr)) < 0) {
        InfoMessage("bind failed");
    }
     // bStarted = 0;
    pthread_t monitorMPBond_thread;
    int r = pthread_create(&monitorMPBond_thread, NULL, MonitorMPBondThread, this);

    MyAssert(r == 0, 2459);
    // while (bStarted != 2) {pthread_yield();}
    InfoMessage("[MPBond] Monitor thread started.");
}

void BUFFER_SUBFLOWS::NewTCPConnection(int pollPos) {
	MyAssert(genSeq[pollPos] == 0, 1623);
}

void BUFFER_SUBFLOWS::RemoveTCPConnection(int pollPos) {
	genSeq[pollPos] = 0;
}

void BUFFER_SUBFLOWS::ResetSubflow(int pollPos) {
	VerboseMessage("BUFFER_SUBFLOWS::ResetSubflow");

	msgBufSize[pollPos] = 0;
	dataBufSize[pollPos] = 0;	
	pDataTail[pollPos] = pDataBuffer[pollPos];
	pDataHead[pollPos] = NULL;
	pMsgTail[pollPos] = pMsgBuffer[pollPos];
	pMsgHead[pollPos] = NULL;
}

void BUFFER_SUBFLOWS::UpdateACK() {
	unsigned long long r = -1;
	DWORD lastACK;
	//int bDupACK;

	for (int i = 1; i <= PROXY_SETTINGS::nTCPSubflows; i++) {
		if (i == 1) {
	        ioctl(kfd, CMAT_IOCTL_GET_FD1_ACK, &r);
		} else if (i == 2) {
			ioctl(kfd, CMAT_IOCTL_GET_FD2_ACK, &r);
		} else if (i == 3) {
            ioctl(kfd, CMAT_IOCTL_GET_FD3_ACK, &r);
        } else if (i == 4) {
            ioctl(kfd, CMAT_IOCTL_GET_FD4_ACK, &r);
        }
        // 0xFFFFFFFF = 2^32 - 1
		lastACK = (DWORD)(r & 0xFFFFFFFF);
		//bDupACK = (int)((r >> 32) & 0xFFFFFFFF);
		transportACK[i] = lastACK;
	}
}

int BUFFER_SUBFLOWS::UpdateSubflowACKStatus(SUBFLOW_MSG * msg, int subflowNo) {
	unsigned long long seq = (unsigned long long)msg->transportSeq[subflowNo],
	ack = (unsigned long long)transportACK[subflowNo];
	unsigned long long seq2 = seq;
	seq += (unsigned long long)msg->bytesOnWire;
	seq %= ((unsigned long long)1 << 32);
	// for MPBond
	if (bLateBindingMPBond == 1) {
		DWORD msgSeq = msg->seq;
		if (kernelInfo.primaryAck > msgSeq) {
			// primaryAck is the next expected seq
			// InfoMessage("Message %d got ACKed (%d) by the primary", msgSeq, kernelInfo.primaryAck);
			// msg->bPrimaryAcked = 1;
			msg->bSubflowAcked = 1;
			return 1;
		}
		else {
			// InfoMessage("Message %d NOT ACKed (%d) by the primary", msgSeq, kernelInfo.primaryAck);
			return 0;
		}
	}
	
	// unsigned long long ackInfo;
	// struct tcp_info ti;
	// int tcpInfoSize = sizeof(ti);
	// int r = getsockopt(subflows.fd[subflowNo-1], IPPROTO_TCP, TCP_INFO, &ti, (socklen_t *)&tcpInfoSize);
	// ackInfo = (DWORD)(ti.tcpi_bytes_acked & 0xFFFFFFFF);
	
	if (ack >= seq) {
		// InfoMessage("Acked, MSG on %d SEQ2: %llu, ACK: %llu", subflowNo, seq2, ack);
		msg->bSubflowAcked = 1;
		return 1;
	}
	// 4294967296 = 2^32
	if (ack + 4294967296 - seq < 16000000) {
		// InfoMessage("Acked, MSG on %d SEQ2: %llu, ACK: %llu", subflowNo, seq2, ack);
		msg->bSubflowAcked = 1;
		return 1;
	}
	// InfoMessage("UnAcked, MSG on %d SEQ2: %llu, ACK: %llu", subflowNo, seq2, ack);
	return 0;
}

void BUFFER_SUBFLOWS::UpdateACKStatus(SUBFLOW_MSG * msg) {
    if (msg->isTransmitted() == 0) {
        //InfoMessage("msg not transmitted: connID=%u seq=%u",
        //        msg->connID, msg->seq);
        return;
    }
	if (msg->schedDecision > 0) {
		if (UpdateSubflowACKStatus(msg, msg->schedDecision) > 0) return;
	}

	if (msg->oldDecision > 0) {
		if (UpdateSubflowACKStatus(msg, msg->oldDecision) > 0) {
			//if (msg->bTransmitted == 0) {
			//	InfoMessage("Second xmit not finish, but packet acked .......");
			//	msg->Print();
			//}
			return;
		}
	}
}

void CONNECTIONS::UpdateNextConnID() {
	VerboseMessage("CONNECTIONS::UpdateNextConnID");

	MyAssert(proxyMode == PROXY_MODE_LOCAL && nextConnID != 0, 1692);
	int nTested = 0;

	while (nTested < MAX_CONN_ID_PLUS_ONE) {		
		if (++nextConnID == MAX_CONN_ID_PLUS_ONE) nextConnID = 1;

		//0xFF is already used by NOP
		if (((nextConnID & 0xFF) != 0xFF) && connTab[nextConnID].GetInUseStatus(tickCount) == CONN_INFO::CONN_EMPTY) return;
		nTested++;
	}
	MyAssert(0, 1633);
}

int CONNECTIONS::TransferFromTCPToMetaBuffer(int fromPollPos, int fromFD, uint64_t ts) {
	// This function reads data from TCP connection fd and encodes one subflow message.

    uint64_t currTs = get_current_microsecond();

    if (ts > 0) {
        if ((long)(currTs - ts) > PROXY_SETTINGS::rcvContTime) return -1;
    }

	int bRemoved = 0;	//whether TCP connection is removed

	VerboseMessage("CONNECTIONS::TransferFromTCPToMetaBuffer (fromPollPos=%d, fromFD=%d)", fromPollPos, fromFD);

	//toPollPos: subflow (1..nSubflows)
	//fromPollPos: TCP connection (nSubflows+1..)

	int bNewConnection = !peersExt[fromPollPos].bSentSYNToSubflow;
	MyAssert(proxyMode == PROXY_MODE_LOCAL || !bNewConnection, 1696);

	int bSendSYNACK = peersExt[fromPollPos].bToSendSYNACK;
	MyAssert(proxyMode == PROXY_MODE_REMOTE || !bSendSYNACK, 2186);
	
	//two possible reasons for POLLFD_EXT::EST_FAIL
	//(1) when writing to TCP, find the connection has been RST
	//(2) failed establishment
	int bTerminateConnection = peersExt[fromPollPos].establishStatus == POLLFD_EXT::EST_FAIL;

	WORD connID = peersExt[fromPollPos].connID;

	if (conns.connTab[connID].bytesInSubflowBuffer >= PROXY_SETTINGS::subflowPerConnBufDataCapacity) return bRemoved;

	int bControl = 0;
	// Change to use meta buffer
	if (bNewConnection + bSendSYNACK + bTerminateConnection > 0) bControl = 1;

	int prio = metaBuffer.GetPriority(connID, bControl);

	MyAssert(conns.connTab[connID].GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE, 1745);

	DWORD & seq = subflowOutput.genSeq[fromPollPos];
	DWORD lastSeq = 0;

	MyAssert(seq == 0 || !bNewConnection, 1677);

	int avalLen = PROXY_SETTINGS::metaBufDataCapacity - metaBuffer.metaDataSize[prio] - 2;

	//need extra 8 bytes for subflow header of data
	//need extra 9 bytes for MSG_CLOSE / MSG_SYNACK
	//need extra 14 bytes for MSG_CREATE
	//so we choose max(8,9,14)=14 (see BUFFER_SUBFLOWS::IsSubflowFull)
	avalLen -= 14;	

	//MyAssert(avalLen > 0 && msgBufSize < PROXY_SETTINGS::subflowBufMsgCapacity, 1711);
	if (avalLen <= 0) {
		//InfoMessage("Meta buffer full %d.", avalLen);
		return -1;
	}
	if (metaBuffer.metaMsgSize[prio] >= PROXY_SETTINGS::metaBufMsgCapacity - 1) return -1;

	if (avalLen > PROXY_SETTINGS::maxPayloadPerMsg) 
		avalLen = PROXY_SETTINGS::maxPayloadPerMsg;

	//8 is the common header size. Change the number when protocol format changes
	//BYTE * pRealData = pDataTail + 8;
	BYTE * & pDataTail = metaBuffer.pMetaDataTail[prio];
	BYTE * pRealData = metaBuffer.GetIncrementDataPointer(metaBuffer.pMetaDataTail[prio], 8, prio);

	// pRealData   pMetaDataEnd
	//     |			|
	//     v            v
	//     |     r1     |
	//     | avalLen  |
	//     | avalLen           |
	int r1 = metaBuffer.pMetaDataEnd[prio] - pRealData;
	if (r1 >= avalLen) r1 = avalLen;
	SUBFLOW_MSG * & pMsgTail = metaBuffer.pMetaMsgTail[prio];
	pMsgTail->msgType = SUBFLOW_MSG::MSG_EMPTY;
	MyAssert(r1 > 0, 1750);

	// SYN message (MSG_CREATE) for new connection, only for local proxy
	if (bNewConnection) {
		CONN_INFO * pNewConn = &connTab[connID];
		pMsgTail->SetCreate(connID,	pDataTail, pNewConn->serverIP, pNewConn->serverPort);
	}
	// FIN message (MSG_CLOSE) for failed connection
	else if (bTerminateConnection) {
		pMsgTail->SetClose(connID, pDataTail, SUBFLOW_MSG::MSG_CLOSE_RST);
	}
	// SYN-ACK message (MSG_SYNACK) from remote proxy for new connection
	else if (bSendSYNACK) {
		MyAssert(proxyMode == PROXY_MODE_REMOTE && !PROXY_SETTINGS::bZeroRTTHandshake, 2190);
		pMsgTail->SetSYNACK(connID, pDataTail);
	}
	// Check if SYN-ACK is received at local proxy only
	else if (!peersExt[fromPollPos].bReceivedSYNACK) {
		//InfoMessage("@@@ SYNACK Haven't received. Jump out.");
		MyAssert(proxyMode == PROXY_MODE_LOCAL && !PROXY_SETTINGS::bZeroRTTHandshake, 2192);
	}
	// Data message to send over subflows
	else {
		int bFirst = 1;
		while (1) {

			// Need to read data from TCP receive buffer first
			int n;
			//skip the subflow header, read the real payload
			if (bFirst)
				n = read(fromFD, pRealData, r1);	
			else
				n = read(fromFD, pRealData, avalLen - r1);

			//InfoMessage("+++++ Read %d bytes", n);
			if (n>0) {

				if (bFirst) {
					pMsgTail->SetData(connID, pDataTail, n);				
				} else {
					pMsgTail->SetMoreData(n);
				}

			} else if (n == 0) {
                bRemoved = -1;
            } else if (n < 0 && errno == ECONNRESET) { //closed by client
				if (bFirst) { 
					InfoMessage("TCP connection %s: %s", 
						n == 0 ? "CLOSED" : "RESET",
						DumpConnection(connID)
					);
					SafeClose(fromFD, fromPollPos);
					
					if (n == 0) {
						pMsgTail->SetClose(connID, pDataTail, SUBFLOW_MSG::MSG_CLOSE_FIN);
					} else {
						pMsgTail->SetClose(connID, pDataTail, SUBFLOW_MSG::MSG_CLOSE_RST);
					}

					lastSeq = seq;
					RemoveConnection(fromPollPos);
					bRemoved = 1;
				}
				//if not bFirst, leave it to next read
			} else if (n < 0 && errno == EWOULDBLOCK) {
				//non-blocking read
				bRemoved = -1;
				break;
			} else {
				ErrorMessage("TCP Read error: %s(%d) %s", 
					strerror(errno), errno,
					DumpConnection(connID)
				);
				MyAssert(0, 1638);
			}

			if (n != r1 || avalLen == r1) { //n<=0 also satisfies
				//we don't need second read
				break;
			}

			if (!bFirst) break; else {
				bFirst = 0;
				pRealData = metaBuffer.pMetaDataBuffer[prio];
			}
		}
	}
	
	if (pMsgTail->msgType == SUBFLOW_MSG::MSG_EMPTY) return bRemoved;
	
	if (pMsgTail->msgType == SUBFLOW_MSG::MSG_CLOSE) {
		if (bTerminateConnection) {
			//pMsgTail->Encode(seq++, pDataTail, pDataBuffer, pDataEnd);
			pMsgTail->Encode(seq++, pDataTail,
				metaBuffer.pMetaDataBuffer[prio], metaBuffer.pMetaDataEnd[prio]);
		} else {
			//pMsgTail->Encode(lastSeq, pDataTail, pDataBuffer, pDataEnd);
			pMsgTail->Encode(lastSeq, pDataTail, 
				metaBuffer.pMetaDataBuffer[prio], metaBuffer.pMetaDataEnd[prio]);
		}
	} else {
		pMsgTail->Encode(seq++, pDataTail,
			metaBuffer.pMetaDataBuffer[prio], metaBuffer.pMetaDataEnd[prio]);
	}

	if (pMsgTail->msgType == SUBFLOW_MSG::MSG_DATA) {
		uint64_t timestamp = get_current_microsecond();
		fprintf(ofsDebug, "%d\t%u\t%u\t%u\t%lu\n", -1,
                pMsgTail->connID, pMsgTail->seq, pMsgTail->bytesOnWire,
                timestamp);
	}
	pMsgTail->priority = prio;
    for (int i = 0; i <= 4; i++) {
	    pMsgTail->bTransmitted[i] = 0;
        pMsgTail->bytesLeft[i] = 0;
    }
	pMsgTail->bSubflowAcked = 0;
	pMsgTail->bRecalled = 0;
    pMsgTail->schedDecision = 0;
	pMsgTail->oldDecision = 0;
	memset(pMsgTail->transportSeq, 0, sizeof(pMsgTail->transportSeq));
	memset(pMsgTail->bValidTSeq, 0, sizeof(pMsgTail->bValidTSeq));
    //MyAssert(pMsgTail->bytesLeft > 0, 8233);

	//update the circular queue data structure
	metaBuffer.UpdateTail(prio);
	
	metaBuffer.GetSize(1);
	// InfoMessage("Add to meta buffer: %d", metaBuffer.GetSize(2));


	if (bNewConnection) {
		peersExt[fromPollPos].bSentSYNToSubflow = 1;
	}

	if (bTerminateConnection) {
		RemoveConnection(fromPollPos);
		bRemoved = 1;
	}

	if (bSendSYNACK) {
		peersExt[fromPollPos].bToSendSYNACK = 0;
	}


#ifdef DEBUG_REINJECT
	InfoMessage("^^^^^^^^^^^^^^^^^^^^^^^^^");
    PrintUnaQueue(1);
	PrintUnaQueue(2);
	InfoMessage("^^^^^^^^^^^^^^^^^^^^^^^^^");
#endif


	return bRemoved;
}

void CONNECTIONS::TransferDelayedFINsToSubflows() {
#ifdef FEATURE_DELAYED_FIN
	int n = delayedFins.n;
	if (n == 0) return;

	VerboseMessage("CONNECTIONS::TransferDelayedFINsToSubflows");
	
	int i;
	for (i=0; i<n; i++) {
		int toPollPos = subflows.SelectSubflow(TC_HIGH_PRIORITY, 1);
		if (toPollPos == -1) break;
	
		BYTE * pDataBuffer = subflowOutput.pDataBuffer[toPollPos];
		BYTE * & pDataHead = subflowOutput.pDataHead[toPollPos];
		BYTE * & pDataTail = subflowOutput.pDataTail[toPollPos];
		int & dataBufSize = subflowOutput.dataBufSize[toPollPos];

		SUBFLOW_MSG * pMsgBuffer = subflowOutput.pMsgBuffer[toPollPos];
		SUBFLOW_MSG * & pMsgHead = subflowOutput.pMsgHead[toPollPos];
		SUBFLOW_MSG * & pMsgTail = subflowOutput.pMsgTail[toPollPos];
		int & msgBufSize = subflowOutput.msgBufSize[toPollPos];

		const BYTE * pDataEnd = pDataBuffer + PROXY_SETTINGS::subflowBufDataCapacity;
		const SUBFLOW_MSG * pMsgEnd = pMsgBuffer + PROXY_SETTINGS::subflowBufMsgCapacity;

		SUBFLOW_MSG & finMsg = delayedFins.fins[i];


		int avalLen = PROXY_SETTINGS::subflowBufDataCapacity - dataBufSize;

		//need extra 9 bytes for MSG_CLOSE (different from 14 in TransferFromTCPToSubflows)
		MyAssert(
			avalLen >= 9 &&
			finMsg.msgType == SUBFLOW_MSG::MSG_CLOSE && 
			finMsg.closeReason == SUBFLOW_MSG::MSG_CLOSE_FIN, 1828
		);
		
		*pMsgTail = finMsg;
		pMsgTail->Encode(pMsgTail->seq, pDataTail, pDataBuffer, pDataEnd);
        for (int i = 1; i <= 4; i++)
		    pMsgTail->pMsgData[i] = pDataTail;

		//update the circular queue data structure
		if (pDataHead == NULL) pDataHead = pDataTail;
		dataBufSize += pMsgTail->bytesOnWire;
		pDataTail += pMsgTail->bytesOnWire;
		if (pDataTail >= pDataEnd) pDataTail -= PROXY_SETTINGS::subflowBufDataCapacity;


		if (pMsgHead == NULL) pMsgHead = pMsgTail;
		msgBufSize++;
		pMsgTail++;
		if (pMsgTail >= pMsgEnd) pMsgTail -= PROXY_SETTINGS::subflowBufMsgCapacity;	
	
		subflowOutput.TransferFromTCPToSubflows(toPollPos, peers[toPollPos].fd);
	}

	delayedFins.DeQueue(i);


#endif
}

int CONNECTIONS::TransferFromSubflowsToTCP(int fromPollPos, int fromFD) {
	VerboseMessage("CONNECTIONS::TransferFromSubflowsToTCP");
	int nBytesRead = 0;
	int subflowType = subflows.GetSubflowType(fromFD);
	//printf("fromFD: %d, subflowType: %d", fromFD, subflowType);
	

	BYTE * header = tcpOutput.headers[fromPollPos];
	SUBFLOW_MSG * & pMsg = tcpOutput.pCurMsgs[fromPollPos];
    if (lastOWD > 0) {
        pMsg = &(subflowOutput.owdMsg);
    }

	while (1) {
		if (header[8] == 8) {

			if (pMsg == NULL) { //allocate space
				WORD connID = *((WORD *)header);
				DWORD seq = *((DWORD *)(header + 2));
				WORD len = *((WORD *)(header + 6));

				VerboseMessage("Received subflow msg header: connID=%d seq=%u len=%d",
					(int)connID, seq, (int)len
				);

                /* change OWD format. add info in the subflow msg payload.
				if (len == SUBFLOW_MSG::MSG_OWD) {
					kernelInfo.UpdateOWD((int) seq);
					header[8] = 0;
					continue;
				}
                */

				pMsg = tcpOutput.Allocate(connID, connTab[connID].pollPos, seq, len);
				if (pMsg == NULL) {
					//buffer is full
					break;
				}
			}
			
			//now read the payload
			//InfoMessage("pMsg: type=%d lastOWD=%d bytesleft=%d onWire=%d", 
			//	pMsg->msgType, lastOWD, pMsg->bytesLeft[0], pMsg->bytesOnWire);
			MyAssert(pMsg->bytesLeft[0] > 0 && pMsg->bytesOnWire >= pMsg->bytesLeft[0] + 8, 1666);			

			int n = 0;
			switch (subflowType) {

			case SUBFLOWS::TCP_SUBFLOW:
				n = read(fromFD, pMsg->pMsgData[0] + pMsg->bytesOnWire - 8 - pMsg->bytesLeft[0], pMsg->bytesLeft[0]);
				break;

			default:
				break;
			}
			if (n > 0) {

				lastSubflowActivityTick = tickCount;
				nBytesRead += n;

				if (PROXY_SETTINGS::bDumpIO) {
					DumpIO(SUBFLOWSOCKET_2_TCPBUFFER, n, fromPollPos, pMsg->connID, pMsg->seq);
				}
			}
			
			if (n == pMsg->bytesLeft[0]) {
				//msg fully read
				pMsg->bytesLeft[0] = 0; //a subflow message won't be transferred to TCP if bytesLeft > 0
				pMsg->Decode();
				header[8] = 0;
			
                if (pMsg->msgType == SUBFLOW_MSG::MSG_OWD) {
                    lastOWD = 0;
                    pMsg = NULL;
                    continue;          
                }
	
				SUBFLOW_MSG * _pMsg = pMsg;
				pMsg = NULL;
				
				//try to transfer immediately
				
				CONN_INFO & ci = connTab[_pMsg->connID];
				/*
				//below is not correct, must use inUseFlag set in Allocate()
				switch (ci.GetInUseStatus(tickCount)) {
				*/
				switch (_pMsg->inUseFlag) {
					case CONN_INFO::CONN_INUSE:
						//common case: try to transfer immediately						
						tcpOutput.TransferFromSubflowsToTCP(ci.pollPos);
						break;

					case CONN_INFO::CONN_RECENTLY_CLOSED:
						//ignore the subflow message
						MyAssert(_pMsg->inUseFlag == CONN_INFO::CONN_RECENTLY_CLOSED, 1736);
						tcpOutput.RemoveFromAbandonedList(_pMsg);
						VerboseMessage("Ignore subflow message of a closed TCP connection (connID = %d)", (int)_pMsg->connID);
						break;

					case CONN_INFO::CONN_EMPTY:
						{
							if (_pMsg->msgType == SUBFLOW_MSG::MSG_CREATE) {
								MyAssert(proxyMode == PROXY_MODE_REMOTE && _pMsg->seq == 0, 1688);
								ci.hints.decodeEntry(_pMsg->hints);
								conns.ConnectToRemoteServer(_pMsg->connID, _pMsg->serverIP, _pMsg->serverPort);
								/*
								It is possible that the connection is not yet established, 
								but data coming from subflows accumulates - that is handled by an
								additional call of TransferFromSubflowsToTCP() in ConnectToRemoteServerDone()
								*/
							} else {
								/*
								the message has a connID that was not recognized
								because out-of-order subflow messages - the SYN subflow message is
								supposed to arrive soon. So now the message is in the 
								"not-yet-established" queue (msgLists[MAX_FDS+1]).
								It is ignored for now, and will be pull back to a normal queue when
								the connection is established
								*/
								MyAssert(_pMsg->inUseFlag == CONN_INFO::CONN_EMPTY, 1736);
							}
							break;
						}

					default:
						MyAssert(0, 1728);
				}
				
				continue;	//read next msg
			} else if (n > 0 && n < pMsg->bytesLeft[0]) {
				pMsg->bytesLeft[0] -= n;
				break;
			} else if (n == 0 || (n < 0 && errno == ECONNRESET)) { 
				//subflows should never be closed
				ErrorMessage("Subflow closed");
				MyAssert(0, 1667);
			} else if (n < 0 && errno == EWOULDBLOCK) {
				//nothing to read
				break;
			} else {
				//error
				ErrorMessage("Error reading from subflow %d: %s(%d)", 
					(int)fromPollPos, strerror(errno), errno
				);
				MyAssert(0, 1668);
			}
		} else {
			MyAssert(header[8]>=0 && header[8]<8, 1658); 
			MyAssert(pMsg == NULL, 1665);

			int nLeft = 8 - header[8];
			int n = 0;
			switch (subflowType) {

			case SUBFLOWS::TCP_SUBFLOW:
				n = read(fromFD, header + header[8], nLeft);
				break;

			default:
				break;
			}
			if (n > 0) {

				lastSubflowActivityTick = tickCount;
				nBytesRead += n;

				if (PROXY_SETTINGS::bDumpIO) {
					DumpIO(SUBFLOWSOCKET_2_TCPBUFFER, n, fromPollPos, -1, 0);
				}
			}

			if (n == nLeft) {
				//now we have a full header
				header[8] = 8;
			} else if (n > 0 && n < nLeft) {
				header[8] += n;
				break;
			} else if (n == 0 || (n < 0 && errno == ECONNRESET)) { 
				//subflows should never be closed
				ErrorMessage("Subflow closed");
				// Proxy should be stopped
				keepRunning = 0;
				statThreadRunning = 0;
				while (statThreadRunning != -1) {
					nanosleep(&wait2, &rem2);
					InfoMessage("Wait for stat thread to finish...");
				}
				MyAssert(0, 1659);
			} else if (n < 0 && errno == EWOULDBLOCK) {
				//nothing to read
				break;
			} else {
				//error
				ErrorMessage("Error reading from subflow: %s(%d) Subflow #%d", 
					strerror(errno), errno, (int)fromPollPos					
				);
				MyAssert(0, 1660);
			}
		}
	}

#if TRANS == 1
	if (subflowType == SUBFLOWS::TCP_SUBFLOW) {
		if (PROXY_SETTINGS::bUseQuickACK && nBytesRead > 0) {
			SetQuickACK(fromFD);
		}
	}
#endif

	return nBytesRead;
}

void CONNECTIONS::DumpIO(int reason, int nBytes, int subflowID, int connID, DWORD msgSeq) {
	//data flow:
	//TCPSOCKET --> SUBFLOWBUFFER --> SUBFLOWSOCKET --> (network) --> SUBFLOWSOCKET --> TCPBUFFER --> TCPSOCKET
	//"Socket" means socket buffer at kernel
	//"Buffer" means buffer space maintained by CMAT at user space
	
	fprintf(ofsIODump, "%.3lf %d %d %d %d %d %u\n", 
		GetMillisecondTS(), 
		reason, 
		nBytes, 
		subflowID, 
		tcpOutput.dataCacheSize,
		connID,
		msgSeq
	);
}


void BUFFER_TCP::Setup() {
	VerboseMessage("BUFFER_TCP::Setup");

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
	bSetup = 1;
}

int BUFFER_TCP::GetBufferFullStatus(int connID) {
	//quick test
	if (dataCacheSize > dataCacheOverallFullThreshold ||
		conns.connTab[connID].bytesInTCPBuffer > dataCachePerConnFullThreshold ||
		msgCacheSize > msgCacheOverallFullThreshold
	) {
		InfoMessage("dataCacheSize=%d conns.connTab[connID].bytesInTCPBuffer=%d msgCacheSize=%d",
		 dataCacheSize, conns.connTab[connID].bytesInTCPBuffer, msgCacheSize);
		return BUFFER_FULL;
	}

	if (msgCacheEnd > msgCacheOverallReOrgThreshold) ReOrganizeMsgCache();
	if (dataCacheEnd > dataCacheOverallReOrgThreshold) ReOrganizeDataCache();

	if (dataCacheSize > dataCacheOverallAlmostFullThreshold ||
		conns.connTab[connID].bytesInTCPBuffer > dataCachePerConnAlmostFullThreshold ||		
		msgCacheSize > msgCacheOverallAlmostFullThreshold
	) {
		InfoMessage("[CLOSE_TO_FULL] dataCacheSize=%d conns.connTab[connID].bytesInTCPBuffer=%d msgCacheSize=%d",
		 dataCacheSize, conns.connTab[connID].bytesInTCPBuffer, msgCacheSize);
		return BUFFER_CLOSE_TO_FULL;
	}
	else
		return BUFFER_NOT_FULL;
}

SUBFLOW_MSG * BUFFER_TCP::Allocate(WORD connID, int toPollPos, DWORD seq, WORD len /* include header */) {
	VerboseMessage("BUFFER_TCP::Allocate");

    if (len == SUBFLOW_MSG::MSG_OWD) {
    	//InfoMessage("New OWD");
        subflowOutput.owdMsg.msgType = len;
        subflowOutput.owdMsg.bytesOnWire = SUBFLOW_MSG::GetBytesOnWire(len);
        subflowOutput.owdMsg.bytesLeft[0] = (int)subflowOutput.owdMsg.bytesOnWire - 8;

        MyAssert(subflowOutput.owdMsg.bytesLeft[0] > 0, 1664);

        subflowOutput.owdMsg.connID = connID;
        subflowOutput.owdMsg.seq = seq;
        subflowOutput.owdMsg.pMsgData[0] = subflowOutput.owdBuffer;
        conns.lastOWD = 1;
        return &(subflowOutput.owdMsg);
    }

	int fs = GetBufferFullStatus(connID);

	if (fs == BUFFER_FULL) {
		return NULL;
	} 

	int inUseStatus = conns.connTab[connID].GetInUseStatus(tickCount);
	
	DWORD & eSeq = expSeq[inUseStatus == CONN_INFO::CONN_INUSE ? toPollPos : 0];	
	//InfoMessage("############ seq=%u eSeq=%u", seq, eSeq);

	MyAssert(inUseStatus != CONN_INFO::CONN_INUSE || seq >= eSeq, 1678);

	//prioritize two types of messages when either buffer is close to full:
	//SYN subflow message and data subflow message matching the expected sequence number
	if (len != SUBFLOW_MSG::MSG_CREATE && (inUseStatus != CONN_INFO::CONN_INUSE || seq != eSeq)) {		
		//we should not call DisableSubflowRead here due to potential deadlock
		//e.g., missing the subflow message matching the expected sequence number, so
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

	pMsg->bytesLeft[0] = (int)pMsg->bytesOnWire - 8;

	if (pMsg->bytesLeft[0] <= 0) {
		InfoMessage("Error!!! connID=%d  seq=%u  msgType=%d  len=%d   bytesOnWire=%d",
			(int)connID, seq, (int)pMsg->msgType, (int)len, pMsg->bytesOnWire);
	}

	MyAssert(pMsg->bytesLeft[0] > 0, 1663);


	pMsg->connID = connID;
	pMsg->seq = seq;
	
	if (inUseStatus == CONN_INFO::CONN_INUSE) {
		MyAssert(pMsg->msgType != SUBFLOW_MSG::MSG_CREATE, 1759);
		InsertSubflowMsgAndUpdateExpSeq(pMsg, connID, toPollPos);

	} else if (inUseStatus == CONN_INFO::CONN_RECENTLY_CLOSED) {
		//put it to the "abandon" list
		//it will be discarded when fully read		
		WarningMessage("In Allocate(): get a subflow message of a closed TCP connection");
		
		pMsg->pNext = msgLists[MAX_FDS];
		msgLists[MAX_FDS] = pMsg;

	} else if (inUseStatus == CONN_INFO::CONN_EMPTY) {
		//put it to the "future connection" list

		if (len != SUBFLOW_MSG::MSG_CREATE) 
			WarningMessage("In Allocate(): get a subflow message of future connection. ConnID=%d, seq=%u, len=%d",
				(int)pMsg->connID, pMsg->seq, pMsg->bytesOnWire
			);

		pMsg->pNext = msgLists[MAX_FDS + 1];
		msgLists[MAX_FDS + 1] = pMsg;

	} else {
		MyAssert(0, 1735);
	}

	msgCacheEnd++;
	msgCacheSize++;

	//now allocate data (w/o 8-byte header)
	pMsg->pMsgData[0] = pDataCache + dataCacheEnd;

	VerboseMessage("*** dataCacheEnd: %d->%d dataCacheSize: %d->%d",
		dataCacheEnd, dataCacheEnd + pMsg->bytesLeft[0],
		dataCacheSize, dataCacheSize + pMsg->bytesLeft[0]
	);


	dataCacheEnd += pMsg->bytesLeft[0];
	dataCacheSize += pMsg->bytesLeft[0];
	
	if (inUseStatus == CONN_INFO::CONN_INUSE)
		conns.connTab[connID].bytesInTCPBuffer += pMsg->bytesLeft[0];

	pMsg->inUseFlag = (BYTE)inUseStatus;

	return pMsg;
}

void BUFFER_TCP::NewTCPConnection(int pollPos) {
	MyAssert(
		msgLists[pollPos] == NULL &&		
		expSeq[pollPos] == 0, 
	1673);
}

void BUFFER_TCP::RemoveTCPConnection(int pollPos, WORD connID) {
	VerboseMessage("BUFFER_TCP::RemoveTCPConnection");

	expSeq[pollPos] = 0;

	if (msgLists[pollPos] != NULL) {
		SUBFLOW_MSG * pCur = msgLists[pollPos];
		while (pCur != NULL) {
			WarningMessage("Premature TCP connection termination: (connID=%d), discard in-buffer data (seq=%u)", 
				(int)connID, pCur->seq
			);
			
			if (pCur->bytesLeft[0] <= 0) {
				//otherwise it's still one of pCurMsgs[], cannot consider it's been removed
				msgCacheSize--;
				
				VerboseMessage("*** A: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pCur->bytesOnWire - 8));
				
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
			pCurMsgs[i]->pNext = msgLists[MAX_FDS];
			msgLists[MAX_FDS] = pCurMsgs[i];
			pCurMsgs[i]->inUseFlag = (BYTE)CONN_INFO::CONN_RECENTLY_CLOSED;
		}
	}

}

void BUFFER_TCP::TransferFromSubflowsToTCP(int toPollPos) {
	VerboseMessage("BUFFER_TCP::TransferFromSubflowsToTCP (toPollPos=%d)", toPollPos);

	if (proxyMode == PROXY_MODE_REMOTE) {
		/*
		the TCP connection is not established yet.
		Note even when bEstablished == FALSE, CONNECTIONS::TransferFromSubflowsToTCP 
		can still be executed since it justs pulls the data from subflow to tcpOutput buffer
		*/
		if (conns.peersExt[toPollPos].establishStatus != POLLFD_EXT::EST_SUCC) return;

		/*
		if establishStatus == EST_FAIL, it will be accumulated in the tcpOutput buffer, and
		finally removed when the connection is removed (i.e., after the FIN subflow msg is sent
		out in TransferFromTCPToSubflows)
		*/
	}

	SUBFLOW_MSG * pCur = msgLists[toPollPos];
	DWORD eSeq = expSeq[toPollPos];
	int nBytesWritten = 0;

	int bStop = 0;
	while (pCur != NULL) {
		//VerboseMessage("*** pCur->seq=%u eSeq=%u ***", pCur->seq, eSeq);
		MyAssert(pCur->seq != eSeq, 1679);

		//"bytesLeft" used in the following functions:
		//(1) CONNECTIONS::TransferFromSubflowsToTCP
		//(2) BUFFER_TCP::TransferFromSubflowsToTCP
		//(3) BUFFER_TCP::Allocate

		//bytesLeft > 0: the message hasn't been fully read to the buffer, x bytes to go
		//bytesLeft = 0: the message has been fully read to the buffer, but it has not been written to TCP
		//bytesLeft < 0: the message has been fully read to the buffer, but it hasn't been fully written to TCP (only -x bytes have been done)

		if (pCur->seq > eSeq || pCur->bytesLeft[0] > 0) {
			bStop = 3;
			break;
		}
		
		int toFD = conns.peers[toPollPos].fd;
		MyAssert(toFD >= 0, 1681);

		switch (pCur->msgType) {
			case SUBFLOW_MSG::MSG_DATA:
				{
					int x = -pCur->bytesLeft[0];	//# bytes already written
					int toWrite = pCur->bytesOnWire - 8 - x;

					//VerboseMessage("*** x=%d toWrite=%d pCur->pMsgData=%x ***", x, toWrite, pCur->pMsgData);

					MyAssert(x >= 0 && toWrite > 0, 1683);
					
					int w = write(toFD, pCur->pMsgData[0] + x, toWrite);
					if (w>0) {
						nBytesWritten += w;

						/*
						int rr;
						if (FindRequest(rr, pCur->pMsgData + x, w)) {
							DebugMessage("### Request %d: SUBFLOW->TCP ###", rr);
						} else if (FindResponse(rr, pCur->pMsgData + x, w)) {
							DebugMessage("||| Response %d: SUBFLOW->TCP |||", rr);
						}
						*/

						if (PROXY_SETTINGS::bDumpIO) {
							CONNECTIONS::DumpIO(TCPBUFFER_2_TCPSOCKET, w, -1, pCur->connID, pCur->seq);
						}
					}

					//VerboseMessage("*** w=%d toWrite=%d ***", w, toWrite);

					if (w == toWrite)  {
						//the current message is fully written, go to next message to write						
						//bStop = 0;
						pCur->bytesLeft[0] -= toWrite;

						VerboseMessage("Write %d bytes to TCP %s. Full subflow message written (seq=%u)",
							w, conns.DumpConnection(conns.peersExt[toPollPos].connID), pCur->seq
						);

					} else if (w >=0 && w < toWrite) {
						bStop = 1;
						pCur->bytesLeft[0] -= w;

						VerboseMessage("Write %d bytes to TCP %s",
							w, conns.DumpConnection(conns.peersExt[toPollPos].connID), pCur->seq
						);

					} else if (w < 0 && errno == EWOULDBLOCK) {
						WarningMessage("Socket buffer full for TCP %s",
							conns.DumpConnection(conns.peersExt[toPollPos].connID)
						);
						bStop = 1;
					} else if (w < 0 && (errno == ECONNRESET || errno == EPIPE)) {
						
						WarningMessage("Connection RST/Closed when writing to TCP %s", 
							conns.DumpConnection(conns.peersExt[toPollPos].connID)
						);
						
						msgLists[toPollPos] = pCur;

						//the logic here is similar to CONNECTIONS::ConnectToRemoteServerDone
						conns.SafeClose(toFD, toPollPos);

						//we cannot remove the connection now, because a RST subflow message needs to be sent
						//protects the closed connection - see the comments at the beginning of TransferFromSubflowsToTCP()
						conns.peersExt[toPollPos].establishStatus = POLLFD_EXT::EST_FAIL; 
						conns.TransferFromTCPToMetaBuffer(toPollPos, -1, 0); //removing the connection will be handled there
						
						bStop = 2;
						goto FINISH_TRANSFER;
					
					} else {
						ErrorMessage("Error writing to TCP: %s(%d) %s", 
							strerror(errno), errno,
							conns.DumpConnection(conns.peersExt[toPollPos].connID)
						);
						MyAssert(0, 1682);
					}

					break;
				}

			case SUBFLOW_MSG::MSG_CREATE:
				{					
					//Already handled in CONNECTIONS::TransferFromSubflowsToTCP
					MyAssert(0, 1743);
				}

			case SUBFLOW_MSG::MSG_SYNACK:
				{
					MyAssert(
						proxyMode == PROXY_MODE_LOCAL && 
						!PROXY_SETTINGS::bZeroRTTHandshake && 
						!conns.peersExt[toPollPos].bReceivedSYNACK, 
					2189);

					//at this moment CMAT doesn't know the connection is closed by the client
					//(if it indeed is), since the socket has been blocked before SYNACK is received
					conns.peersExt[toPollPos].bReceivedSYNACK = 1;

					VerboseMessage("Received SYNACK. Read data now.");
					
					int bRemoved = conns.TransferFromTCPToMetaBuffer(toPollPos, toFD, 0);
					//int bRemoved = 0;

					//at this time the connection might be closed by the client
					
					if (bRemoved == 1) {
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

								InfoMessage("Closing TCP %s by FIN",
									conns.DumpConnection(conns.peersExt[toPollPos].connID)
								);
								//kernelInfo.owdMapping.PrintMapping();
								accBytes[1] = 0;
								accBytes[2] = 0;
								kernelInfo.bytesInPipe[2] = 0;
								break;
							}

						case SUBFLOW_MSG::MSG_CLOSE_RST:
							{
								InfoMessage("Closing TCP %s by RST",
									conns.DumpConnection(conns.peersExt[toPollPos].connID)
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

					metaBuffer.isRecallOn = 0;
					kernelInfo.updated = 0;
					
					conns.SafeClose(toFD, toPollPos);

					//"manually" process this FIN subflow message
					msgLists[toPollPos] = pCur->pNext;
					msgCacheSize--;
					VerboseMessage("*** B: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pCur->bytesOnWire - 8));
					dataCacheSize -= pCur->bytesOnWire - 8;

					//anyway the connection will be removed, so no need to decrease bytesInBuffer
					conns.RemoveConnection(toPollPos); 

					bStop = 2;
					goto FINISH_TRANSFER;
				}

			default:
				MyAssert(0, 1680);
		}

		

		if (bStop) break;

		msgCacheSize--;

		VerboseMessage("*** C: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pCur->bytesOnWire - 8));

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

	//MyAssert((pCur == NULL) == (bStop == 0),  1717);
	msgLists[toPollPos] = pCur;
	conns.EnableTCPWriteNotification(toPollPos, bStop == 1 || bStop == 4);

	//bStop == 0: All data sent
	//bStop == 1: Not all data sent due to TCP sock buffer full
	//bStop == 2: connection closed
	//bStop == 3: Not all data sent due to out-of-order messages
	//bStop == 4: Reach transfer unit size
	//bStop == 5: receive SYN-ACK subflow msg and connection closed

FINISH_TRANSFER:
	////////////////////////////// 08/07/2014 //////////////////////////////
	//more space? Then try to enable subflow read ntf
	/*
	if (GetBufferFullStatus() != BUFFER_FULL) {
		conns.EnableSubflowReadNotifications(1);
	}
	*/
	////////////////////////////////////////////////////////////////////////
	;
}

void BUFFER_TCP::ReOrganizeMsgCache() {
	//VerboseMessage("BUFFER_TCP::ReOrganizeMsgCache");

	InfoMessage("Reorganizing TCP message cache. Before size=%d end=%d", msgCacheSize, msgCacheEnd);

	SUBFLOW_MSG * pOldBase = pMsgCache;
	
	if (pOldBase == msgCache1)
		pMsgCache = msgCache2;
	else if (pOldBase == msgCache2)
		pMsgCache = msgCache1;
	else
		MyAssert(0, 1684);

	SUBFLOW_MSG * pNew = pMsgCache;
	
	int pollPosBegin = subflows.n + 1;
	int pollPosEnd = conns.maxIdx;
	for (int i=pollPosBegin; i<=pollPosEnd + 2; i++) {
		int j;

		//pollPosEnd: abandoned list (MAX_FDS)
		//pollPosEnd+1: not-yet-established list (MAX_FDS+1)
		SUBFLOW_MSG * pCur;
		if (i <= pollPosEnd) {
			j = i;
		} else if (i == pollPosEnd + 1) {
			j = MAX_FDS;
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
	MyAssert(msgCacheSize == msgCacheEnd, 1756);

	//update pCurMsgs
	for (int i=1; i<=subflows.n; i++) {
		if (pCurMsgs[i] != NULL) {
			pCurMsgs[i] = pCurMsgs[i]->pTemp;
			MyAssert(pCurMsgs[i] >= pMsgCache && pCurMsgs[i] < pMsgCache + PROXY_SETTINGS::tcpOverallBufMsgCapacity, 1734);
		}
	}

	InfoMessage("After size=%d end=%d", msgCacheSize, msgCacheEnd);
}

void BUFFER_TCP::ReOrganizeDataCache() {
	//VerboseMessage("BUFFER_TCP::ReOrganizeDataCache");

	InfoMessage("Reorganizing TCP data cache. Before size=%d end=%d", dataCacheSize, dataCacheEnd);
	
	BYTE * pOldBase = pDataCache;

	if (pOldBase == dataCache1)
		pDataCache = dataCache2;
	else if (pOldBase == dataCache2)
		pDataCache = dataCache1;
	else
		MyAssert(0, 1685);

	int pollPosBegin = subflows.n + 1;
	int pollPosEnd = conns.maxIdx;

	BYTE * pNew = pDataCache;

    // for TCP connections
	for (int i=pollPosBegin; i<=pollPosEnd + 2; i++) {
		//pollPosEnd: abandoned list (MAX_FDS)
		//pollPosEnd+1: not-yet-established list (MAX_FDS+1)
		SUBFLOW_MSG * pCur;
		if (i <= pollPosEnd)
			pCur = msgLists[i];
		else if (i == pollPosEnd + 1)
			pCur = msgLists[MAX_FDS];
		else
			pCur = msgLists[MAX_FDS + 1];

		if (pCur == NULL) continue;

		while (pCur != NULL) {
			//MyAssert(pCur->bytesLeft <= 0, 1686);
			memcpy(pNew, pCur->pMsgData[0], pCur->bytesOnWire - 8);
			pCur->pMsgData[0] = pNew;
			pNew += pCur->bytesOnWire - 8;
			pCur = pCur->pNext;
		}
	}

	dataCacheEnd = pNew - pDataCache;

	VerboseMessage("dataCacheSize=%d dataCacheEnd=%d", dataCacheSize, dataCacheEnd);
	MyAssert(dataCacheSize == dataCacheEnd, 1757);

	InfoMessage("After size=%d end=%d", dataCacheSize, dataCacheEnd);
}

int BUFFER_SUBFLOWS::IsSubflowFull(int pollPos) {
	MyAssert(pollPos >= 1 && pollPos <= subflows.n, 1689);

	if (msgBufSize[pollPos] >= PROXY_SETTINGS::subflowBufMsgCapacity - PROXY_SETTINGS::subflowBufMsgFullLeeway)
		return 1;
	if (dataBufSize[pollPos] >= PROXY_SETTINGS::subflowBufDataCapacity - PROXY_SETTINGS::subflowBufDataFullLeeway)
		return 1;

	return 0;

}

int CONNECTIONS::ConnectToRemoteServer(WORD connID, DWORD serverIP, WORD serverPort) {
	VerboseMessage("CONNECTIONS::ConnectToRemoteServer");

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	MyAssert(fd >= 0 && proxyMode == PROXY_MODE_REMOTE, 1690);

	//SetMaxSegSize(fd, MAGIC_MSS_VALUE);

	struct sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(serverAddr));
	serverAddr.sin_family = AF_INET;
	
	serverAddr.sin_port = htons(serverPort);
	serverAddr.sin_addr.s_addr = serverIP;

	SetNonBlockIO(fd);
	SetSocketNoDelay_TCP(fd);
	SetSocketBuffer(fd, PROXY_SETTINGS::tcpReadBufRemoteProxy, 0);

	InfoMessage("Connect to remote server: %s",
		ConvertDWORDToIP(serverAddr.sin_addr.s_addr)	
	);

	//non-blocking connect
	int n = connect(fd, (const struct sockaddr *) &serverAddr, sizeof(serverAddr));
	if (errno != EINPROGRESS) MyAssert(0, 1691);
	
	DWORD clientIP;
	WORD clientPort;
	GetClientIPPort(fd, clientIP, clientPort);

	int pollPos = AddTCPConnection(fd, clientIP, clientPort, serverIP, serverPort, connID);
	peersExt[pollPos].connectTime = tickCount;

	{
		//now we have a valid connID. Move subflow messages in the "not-yet-established" queue to
		//the normal queue
		SUBFLOW_MSG * pCur = tcpOutput.msgLists[MAX_FDS + 1];
		SUBFLOW_MSG * pPrev = NULL;

		while (pCur != NULL) {
			MyAssert(pCur->inUseFlag == CONN_INFO::CONN_EMPTY, 1737);
			SUBFLOW_MSG * pNext = pCur->pNext;

			if (pCur->connID == connID) {
				if (pPrev == NULL)
					tcpOutput.msgLists[MAX_FDS + 1] = pNext;
				else
					pPrev->pNext = pNext;

				//move pMsg to the normal queue
				pCur->inUseFlag = CONN_INFO::CONN_INUSE;
				tcpOutput.InsertSubflowMsgAndUpdateExpSeq(pCur, connID, pollPos);
			} else {
				pPrev = pCur;				
			}
			pCur = pNext;
		}
	}

	if (n == 0) {
		ConnectToRemoteServerDone(pollPos, ESTABLISH_SUCC);
		return 1;
	} else {
		MyAssert(peers[pollPos].events == POLLRDNORM, 1698);
		peers[pollPos].events |= (POLLRDNORM | POLLWRNORM);
		ResetReadNotificationEnableState();
		return 0;
	}
}

//used by remote proxy only
void CONNECTIONS::ConnectToRemoteServerDone(int pollPos, int result) {
	VerboseMessage("CONNECTIONS::ConnectToRemoteServerDone");

	MyAssert(proxyMode == PROXY_MODE_REMOTE, 1709);

	CONN_INFO & ci = connTab[peersExt[pollPos].connID];
	MyAssert(ci.GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE && 
			 peersExt[pollPos].establishStatus == POLLFD_EXT::EST_NOT_CONNECTED, 1741);

	ResetReadNotificationEnableState();

	switch (result) {
		case ESTABLISH_SUCC:
			{

				VerboseMessage("Connection establishment succ: %s", DumpConnection(peersExt[pollPos].connID));

				peers[pollPos].events = POLLRDNORM;
				peersExt[pollPos].establishStatus = POLLFD_EXT::EST_SUCC;

				//tcpOutput.expSeq[pollPos] = 1;

				if (PROXY_SETTINGS::bZeroRTTHandshake) {
					//some messages for the just-established connection may already accumulate
					tcpOutput.TransferFromSubflowsToTCP(pollPos); 
				} else {
					BYTE & b = peersExt[pollPos].bToSendSYNACK;
					MyAssert(b == 0, 2187);
					b = 1;
					// Send SYN-ACK Now
					TransferFromTCPToMetaBuffer(pollPos, -1, 0);
				}
				
				break;
			}
	
		case ESTABLISH_TIMEOUT:
		case ESTABLISH_ERROR:
			{
				if (result == ESTABLISH_TIMEOUT)
					WarningMessage("Connection establishment timeout: %s", DumpConnection(peersExt[pollPos].connID));
				else
					WarningMessage("Connection establishment error: %s", DumpConnection(peersExt[pollPos].connID));

				MyAssert(peers[pollPos].fd >= 0, 1706);
				SafeClose(peers[pollPos].fd, pollPos);
				
				//we cannot remove the connection now, because a RST subflow message needs to be sent

				//protects the closed connection - see the comments at the beginning of TransferFromSubflowsToTCP()
				peersExt[pollPos].establishStatus = POLLFD_EXT::EST_FAIL; 

				TransferFromTCPToMetaBuffer(pollPos, -1, 0); //removing the connection will be handled there
				break;
			}

		default:
			MyAssert(0, 1703);
	}
}

void CONN_INFO::SetInUseStatus(int s) {
	VerboseMessage("CONN_INFO::SetInUseStatus");

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

/*
inline int CONN_INFO::GetInUseStatus() {
	if (closeTime == 0xFFFFFFFF) return CONN_INUSE;

	DWORD now = tickCount;
	MyAssert(now >= closeTime, 1720);

	if (now - closeTime > CONN_ID_REUSE_TIMEOUT) 
		return CONN_EMPTY;
	else
		return CONN_RECENTLY_CLOSED;
}
*/

void BUFFER_TCP::RemoveFromAbandonedList(const SUBFLOW_MSG * pMsg) {
	VerboseMessage("BUFFER_TCP::RemoveFromAbandonedList");

	SUBFLOW_MSG * pPrev = NULL;
	SUBFLOW_MSG * pCur = msgLists[MAX_FDS];

	while (pCur != NULL) {
		if (pCur == pMsg) {
			if (pPrev == NULL) 
				msgLists[MAX_FDS] = pCur->pNext;
			else
				pPrev->pNext = pCur->pNext;

			msgCacheSize--;

			VerboseMessage("*** D: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pMsg->bytesOnWire - 8));

			dataCacheSize -= pMsg->bytesOnWire - 8;
			//anyway the connection has been removed, so no need to decrease bytesInBuffer

			return;
		}

		pPrev = pCur;
		pCur = pCur->pNext;
	}

	MyAssert(0, 1733);
}

void BUFFER_TCP::InsertSubflowMsgAndUpdateExpSeq(SUBFLOW_MSG * pMsg, WORD connID, int toPollPos) {
	VerboseMessage("BUFFER_TCP::InsertSubflowMsgAndUpdateExpSeq (connID=%d, toPollPos=%d)", (int)connID, toPollPos);

	MyAssert(toPollPos > subflows.n && toPollPos < MAX_FDS, 1738);
	MyAssert(conns.connTab[connID].GetInUseStatus(tickCount) == CONN_INFO::CONN_INUSE, 1740);

	DWORD seq = pMsg->seq;
	DWORD & eSeq = expSeq[toPollPos];

	if (pMsg->msgType == SUBFLOW_MSG::MSG_CREATE) {
		//in that case, discard the subflow message. Just update seq
		MyAssert(seq == 0 && eSeq == 0, 1752);
		eSeq = 1;

		SUBFLOW_MSG * pCur = msgLists[toPollPos];
		while (pCur != NULL) {
			MyAssert(pCur->seq >= eSeq, 1753);
			if (pCur->seq == eSeq) eSeq++; else return;
			pCur = pCur->pNext;
		}

		msgCacheSize--;

		VerboseMessage("*** E: Data Cachesize: %d->%d ***", dataCacheSize, dataCacheSize - (pMsg->bytesOnWire - 8));

		dataCacheSize -= pMsg->bytesOnWire - 8;
		
		int & bib = conns.connTab[connID].bytesInTCPBuffer;
		bib -= pMsg->bytesOnWire - 8;
		if (bib < 0) bib = 0;

		VerboseMessage("SYN message processed (connID=%d). Exp seq updated to %u", (int)connID, eSeq);
		return;
	}

	MyAssert(seq >= eSeq, 1755);

	if (msgLists[toPollPos] == NULL) {
		msgLists[toPollPos] = pMsg;
		pMsg->pNext = NULL;

		if (seq == eSeq) eSeq++;

	} else {
		SUBFLOW_MSG * pPrev = NULL;
		SUBFLOW_MSG * pCur = msgLists[toPollPos];
		int bInserted = 0;
		while (1) {
			MyAssert(pCur->seq != seq, 1698);

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

	VerboseMessage("Subflow message (seq=%u) inserted to TCP buffer (connID=%d). Now expected seq=%u", 
		seq, (int)connID, eSeq);
}


void CONNECTIONS::EnableSubflowWriteNotification(int pollPos, int bEnable) {
	VerboseMessage("Subflow write notification changed. New value=%d Subflow#=%d", bEnable, pollPos);
	MyAssert(pollPos>=1 && pollPos<=subflows.n, 1746);
	
	if (bEnable) {		
		peers[pollPos].events |= POLLWRNORM;
		VerboseMessage("Subflow write notification ENABLED. Subflow#=%d", pollPos);
	} else {
		peers[pollPos].events &= ~POLLWRNORM;
	}

	/*
	short & e = peers[pollPos].events;
	if (bEnable) {
		if ((e & POLLWRNORM) == 0) {		
			e |= POLLWRNORM;
			InfoMessage("Subflow write notification ENABLED. Subflow#=%d", pollPos);
		}
	} else {
		if ((e & POLLWRNORM) != 0) {		
			e &= ~POLLWRNORM;
			InfoMessage("Subflow write notification DISABLED. Subflow#=%d", pollPos);
		}
	}
	*/
}

void CONNECTIONS::EnableTCPWriteNotification(int pollPos, int bEnable) {
	MyAssert(
		pollPos>subflows.n && 
		peers[pollPos].fd != -1 && 
		peersExt[pollPos].establishStatus == POLLFD_EXT::EST_SUCC, 1747
	);

	VerboseMessage("TCP write notification changed. New value=%d connID=%d", 
		bEnable, (int)peersExt[pollPos].connID);
	
	if (bEnable) {
		peers[pollPos].events |= POLLWRNORM;
		VerboseMessage("TCP write notification ENABLED. ConnID=%d", (int)peersExt[pollPos].connID);
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

void CONNECTIONS::EnableTCPReadNotifications(int bEnable) {
	MyAssert(bEnable == 0 || bEnable == 1, 1748);

	VerboseMessage("TCP read notification changed. New value=%d", bEnable);

	if (bEnable == bTCPReadNtfEnabled) return;
	bTCPReadNtfEnabled = bEnable;

	if (bEnable) {		
		for (int i=subflows.n+1; i<MAX_FDS; i++) 
			if (peers[i].fd != -1 && peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC)
				peers[i].events |= POLLRDNORM;
	} else {
		VerboseMessage("TCP read notification DISABLED");
		for (int i=subflows.n+1; i<MAX_FDS; i++)
			if (peers[i].fd != -1 && peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC)
				peers[i].events &= ~POLLRDNORM;
	}
}

void CONNECTIONS::EnableSubflowReadNotifications(int bEnable) {
	MyAssert(bEnable == 0 || bEnable == 1, 1749);

	VerboseMessage("Subflow read notification changed. New value=%d", bEnable);

	if (bEnable == bSubflowReadNtfEnabled) return;
	bSubflowReadNtfEnabled = bEnable;

	if (bEnable) {
		for (int i=1; i<=subflows.n; i++) peers[i].events |= POLLRDNORM;
	} else {
		VerboseMessage("Subflow read notification DISABLED");
		for (int i=1; i<=subflows.n; i++) peers[i].events &= ~POLLRDNORM;
	}
}

void CONNECTIONS::ResetReadNotificationEnableState() {
	//set both to -1 so that next time when
	//EnableXXXReadNotifications(bEnable) is called, cached results are not used
	//since bEnable is either 1 or 0

	VerboseMessage("ResetReadNotificationEnableState");

	bTCPReadNtfEnabled = -1;	//lazy init: neither 1 nor 0
	bSubflowReadNtfEnabled = -1;	//lazy init: neither 1 nor 0
}

void CONNECTIONS::SafeClose(int fd, int pollPos) {

	MyAssert(pollPos > subflows.n, 1763);

	VerboseMessage("Close TCP connection: fd=%d pollPos=%d connID=%d", 
		fd, pollPos, (int)peersExt[pollPos].connID);
	peers[pollPos].events = 0;
	close(fd);
}

int BUFFER_TCP::GetMsgListSize(int pollPos) {
	SUBFLOW_MSG * pMsg = tcpOutput.msgLists[pollPos];

	int n = 0;
	while (pMsg != NULL) {
		//n++;
		n += pMsg->payloadLen;
		pMsg = pMsg->pNext;
	}

	return n;
}

void DELAYED_FINS::Setup(double d1, double d2) {
	this->d1 = d1;
	this->d2 = d2;
	n = 0;
}

void DELAYED_FINS::EnQueue(SUBFLOW_MSG * pMsg, DWORD seq) {	
	MyAssert(n < MAX_CAPACITY, 1824);
	fins[n] = *pMsg;
	fins[n].seq = seq;

	if (n == 0)	deadline = tickCount + d2;

	InfoMessage("Enqueue delayed FIN, deadline = %.2lf", deadline);

	n++;
}

void DELAYED_FINS::DeQueue(int m /*how many*/) {
	InfoMessage("Dequeue %d delayed FIN", m);
	if (m == 0) return;
	if (m == n) {
		n = 0;
	} else {
		MyAssert(m < n, 1829);
		for (int i=m; i<n; i++) {
			fins[i-m] = fins[i];
		}
		n -= m;
	}
}

//#if TRANS == 2

int CONNECTIONS::udpRead(int fd, BYTE * buf, int bytes) { //support partially read of a datagram
	static BYTE msg[4096];
	static int offset = 0;
	static int len = 0;

	if (len == 0) {
		MyAssert(offset == 0, 2210);
		struct sockaddr_in udpPeerAddrTmp;
                socklen_t udpPeerAddrLenTmp;
		int n = recvfrom(fd, msg, sizeof(msg), 0, (sockaddr *)&udpPeerAddrTmp, &udpPeerAddrLenTmp);
		if (n > 0) {
			MyAssert(n <= PROXY_SETTINGS::maxPayloadPerMsgFromPeer + 8, 2211);
			len = n;
		} else if (n < 0 && errno == EWOULDBLOCK) {
			return -1;
		} else {
			MyAssert(0, 2213);
		}
	}

	if (len > 0 && offset > 0) { //at most two partial read (one: header, the other: data) for a subflow message
		MyAssert(bytes == len - offset, 2208);
		memcpy(buf, msg + offset, bytes);
		len = 0;
		offset = 0;
	} else if (len > 0 && offset == 0) {
		MyAssert(bytes <= len, 2209);
		memcpy(buf, msg, bytes);
		offset += bytes;
		if (offset == len) {
			len = 0;
			offset = 0;			
		}
	} else {
		MyAssert(0, 2212);
	}

	return bytes;
}


// Only called once for each outgoing packets on subflows
void CONNECTIONS::GetTCPSeqForUna(SUBFLOW_MSG * pCurrMsg, int nBytesWritten) {
	int subflowNo = pCurrMsg->schedDecision;
	static int disabled = 0;
	uint64_t timestamp = get_current_microsecond();
    //MyAssert(subflowNo > 0, 6767);

	if (pCurrMsg->bytesLeft[subflowNo] == 0) {
		pCurrMsg->txTime[subflowNo] = timestamp;
	}

	if (pCurrMsg->transportSeq[subflowNo] > 0) {
		/*
		InfoMessage("Packet already with transportSeq!! subflowNo=%u, connID=%u, seq=%u, tcpSeq=%u, bytesOnWire=%d, left=%d",
			subflowNo, pCurrMsg->connID, pCurrMsg->seq, pCurrMsg->transportSeq[subflowNo],
			pCurrMsg->bytesOnWire, pCurrMsg->bytesLeft);
			*/
        if (nBytesWritten > 0) {
        	tmp_bif1 = kernelInfo.GetInFlightSize(1);
        	tmp_bif2 = kernelInfo.GetInFlightSize(2);
        	tmp_owd1 = kernelInfo.owdMapping.mapping[1].GetOWD(tmp_bif1);
        	tmp_owd2 = kernelInfo.owdMapping.mapping[2].GetOWD(tmp_bif2);
        	tmp_var1 = kernelInfo.owdMapping.mapping[1].GetVar();
        	tmp_var2 = kernelInfo.owdMapping.mapping[2].GetVar();
        fprintf(ofsDebug, "%d\t%u\t%u\t%u\t%u\t%d\t%d\t%lu\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t%f\t%d\t%f\t%f\n", subflowNo,
                pCurrMsg->connID, pCurrMsg->seq, pCurrMsg->bytesOnWire,
                pCurrMsg->transportSeq[subflowNo], pCurrMsg->bytesLeft[subflowNo],
                nBytesWritten, timestamp, 
                kernelInfo.GetSRTT(1), kernelInfo.GetSRTT(2),
                kernelInfo.GetTCPAvailableSpace(1),
                kernelInfo.GetTCPAvailableSpace(2),
                tmp_bif1, tmp_bif2,
                kernelInfo.owdMapping.mapping[1].isOWD, tmp_owd1, tmp_var1,
                kernelInfo.owdMapping.mapping[2].isOWD, tmp_owd2, tmp_var2);
        }
		return;
	}

	unsigned long long r = 0;

	if (subflowOutput.transportSeq[subflowNo] == 0) {
		unsigned long long param = ((unsigned long long)subflowNo << 48) + 
			(((unsigned long long)pCurrMsg->connID) << 32) + ((unsigned long long)pCurrMsg->seq);
		ioctl(kfd, CMAT_IOCTL_SET_SEQ_MAPPING, param);
		ioctl(kfd, CMAT_IOCTL_GET_SEQ_MAPPING, &r);
		// InfoMessage("Get mapping: subflowNo=%u, connID=%u, seq=%u", subflowNo, pCurrMsg->connID, pCurrMsg->seq);

		if (r == (unsigned long long)1 << 48) {
			//InfoMessage("Mapping not found: (%d, %u, %u). Use cache: %u", subflowNo, pCurrMsg->connID, pCurrMsg->seq, subflowOutput.tcpSeq[subflowNo]);
			pCurrMsg->transportSeq[subflowNo] = subflowOutput.transportSeq[subflowNo];
			pCurrMsg->bValidTSeq[subflowNo] = 1;
			subflowOutput.transportSeq[subflowNo] = (DWORD)(
				((unsigned long long)(subflowOutput.transportSeq[subflowNo]) + 
					pCurrMsg->bytesOnWire) % ((unsigned long long)1 << 32));
			return;
		}

		pCurrMsg->transportSeq[subflowNo] = (DWORD)r;
		pCurrMsg->bValidTSeq[subflowNo] = 1;

		if (subflowOutput.transportSeq[subflowNo] != pCurrMsg->transportSeq[subflowNo]) {
			// InfoMessage("Update subflow %d tcpSeq: %u -> %u (%u, %u)", subflowNo,
			// 	subflowOutput.transportSeq[subflowNo], pCurrMsg->transportSeq[subflowNo], pCurrMsg->connID, pCurrMsg->seq);
		} else {
			//InfoMessage("Update correct!");
		}
		subflowOutput.transportSeq[subflowNo] = (DWORD)((r + pCurrMsg->bytesOnWire) % ((unsigned long long)1 << 32));
		// InfoMessage("Update next: %u", subflowOutput.transportSeq[subflowNo]);
	} else {
		if (!disabled) {
            int all = 1;
            for (int i = 1; i <= PROXY_SETTINGS::nTCPSubflows; i++) {
                if (subflowOutput.transportSeq[i] == 0) {
                    all = 0;
                    break;
                }
            }
			if (all > 0) {
				ioctl(kfd, CMAT_IOCTL_SET_DISABLE_MAPPING, 0);
				// InfoMessage("Disable kernel mapping");
				disabled = 1;
			}
		}
		pCurrMsg->transportSeq[subflowNo] = subflowOutput.transportSeq[subflowNo];
		pCurrMsg->bValidTSeq[subflowNo] = 1;
		int tmp = 0;
		if (subflowOutput.transportSeq[subflowNo] > 4294960000) {
			tmp = 1;
			InfoMessage("Update: %u", subflowOutput.transportSeq[subflowNo]);
		}
		subflowOutput.transportSeq[subflowNo] = (DWORD)(
				((unsigned long long)(subflowOutput.transportSeq[subflowNo]) + 
					pCurrMsg->bytesOnWire) % ((unsigned long long)1 << 32));
		if (tmp)
			InfoMessage("After update: %u", subflowOutput.transportSeq[subflowNo]);
	}
    if (nBytesWritten > 0) {
    	tmp_bif1 = kernelInfo.GetInFlightSize(1);
        	tmp_bif2 = kernelInfo.GetInFlightSize(2);
        	tmp_owd1 = kernelInfo.owdMapping.mapping[1].GetOWD(tmp_bif1);
        	tmp_owd2 = kernelInfo.owdMapping.mapping[2].GetOWD(tmp_bif2);
        	tmp_var1 = kernelInfo.owdMapping.mapping[1].GetVar();
        	tmp_var2 = kernelInfo.owdMapping.mapping[2].GetVar();
        fprintf(ofsDebug, "%d\t%u\t%u\t%u\t%u\t%d\t%d\t%lu\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%f\t%f\t%d\t%f\t%f\n", subflowNo,
                pCurrMsg->connID, pCurrMsg->seq, pCurrMsg->bytesOnWire,
                pCurrMsg->transportSeq[subflowNo], pCurrMsg->bytesLeft[subflowNo],
                nBytesWritten, timestamp, 
                kernelInfo.GetSRTT(1), kernelInfo.GetSRTT(2),
                kernelInfo.GetTCPAvailableSpace(1),
                kernelInfo.GetTCPAvailableSpace(2),
                tmp_bif1, tmp_bif2,
                kernelInfo.owdMapping.mapping[1].isOWD, tmp_owd1, tmp_var1,
                kernelInfo.owdMapping.mapping[2].isOWD, tmp_owd2, tmp_var2);
                //kernelInfo.GetOWD(1, tmp_bif1, tmp_bif2));
    }
    metaBuffer.AddMsgToMapping(pCurrMsg);
}

//#endif
