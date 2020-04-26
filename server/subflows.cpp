#include "stdafx.h"
#include "subflows.h"
#include "tools.h"
#include "connections.h"
#include "proxy.h"
#include "meta_buffer.h"
#include "kernel_info.h"
#include <pcap/pcap.h>
#include <sys/types.h>
#include <sys/socket.h>

extern int tickCount;
extern int proxyMode;
extern struct SUBFLOWS subflows;
extern struct CONNECTIONS conns;
extern struct BUFFER_SUBFLOWS subflowOutput;	
extern struct BUFFER_TCP tcpOutput;
extern struct META_BUFFER metaBuffer;
extern struct KERNEL_INFO kernelInfo;

extern FILE * ofsSubflowDump;	//for dump subflow msg feature
extern FILE * ofsIODump;

extern int kfd;
extern pthread_mutex_t subflowDumpLock;

char buf[100];

int statCount = 0;
struct timespec wait = {0, 500000000}, rem;

int statThreadRunning = 1;

int SUBFLOWS::Setup(const char * remoteIP) {
	VerboseMessage("SUBFLOWS::Setup");

	this->n = PROXY_SETTINGS::nSubflows;
	this->nTCP = PROXY_SETTINGS::nTCPSubflows;

	memset(port2SubflowID, 0xFF, sizeof(port2SubflowID));

	for (int i = 0; i< MAX_SUBFLOWS; i++) fd[i] = -1;

	MyAssert(remoteIP == NULL, 1718);
	// Setup TCP subflows
	int listenFD = socket(AF_INET, SOCK_STREAM, 0);
	if (listenFD < 0) return R_FAIL;

	struct sockaddr_in serverAddr;
	memset(&serverAddr, 0, sizeof(sockaddr_in));	
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(REMOTE_PROXY_PORT);
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	int optval = 1;
	int r = setsockopt(listenFD, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
	MyAssert(r == 0, 1762);
	
	SetSocketBuffer(listenFD, PROXY_SETTINGS::subflowReadBufRemoteProxy, PROXY_SETTINGS::subflowWriteBufRemoteProxy); //This must be called for listenFD !

	if (bind(listenFD, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) != 0) return R_FAIL;

	if (listen(listenFD, 32) != 0) return R_FAIL;

	for (int i = 0; i < nTCP; i++) {
		InfoMessage("Waiting for subflow %d to connect", i + 1);
		//wait for n TCP subflow connections
        //subflow 1: primary, subflow 2...n: helper
		struct sockaddr_in clientAddr;
		socklen_t clientAddrLen = sizeof(clientAddr);			
		fd[i] = accept(listenFD, (struct sockaddr *)&clientAddr, &clientAddrLen);
		if (fd[i] == -1) return R_FAIL;

		SetCongestionControl(fd[i], PROXY_SETTINGS::subflowProtocol[i].c_str());
		SetSocketNoDelay_TCP(fd[i]);
		SetNonBlockIO(fd[i]);

		int localPort = (int)ntohs(clientAddr.sin_port);

	    InfoMessage("Subflow %d established. IP=%s, port=%d, TCP=%s, fd=%d", 
			i+1, inet_ntoa(clientAddr.sin_addr), localPort,
			PROXY_SETTINGS::subflowProtocol[i].c_str(), fd[i]
		);
        WriteLocalIPPort(inet_ntoa(clientAddr.sin_addr), localPort, i+1);
		port2SubflowID[localPort] = i;
	}

	close(listenFD);

	if (PROXY_SETTINGS::bDumpIO) {
		if (proxyMode == PROXY_MODE_LOCAL)
			ofsIODump = fopen("./cmat.io.lp.dump", "wb");
		else
			ofsIODump = fopen("./cmat.io.rp.dump", "wb");
		MyAssert(ofsIODump != NULL, 1964);
	}

	return R_SUCC;
}

void printLog() {
	InfoMessage("SUBFLOWS: (%d) %d %d", subflows.n, MAX_FDS, conns.maxIdx);
        InfoMessage("i  socket_recv  dataBuf  msgBuf   readNotify  writeNotify");
        //InfoMessage("------------------------------------------");
        for (int i = 1; i <= MAX_FDS; i++) {
		int fd = conns.peers[i].fd, bytes_available = 0;
		ioctl(fd, FIONREAD ,&bytes_available);
                if (subflowOutput.dataBufSize[i] > 0 || bytes_available > 0)
                        InfoMessage("%d  %d  %d  %d  %s  %s",
                        i, bytes_available,
                        subflowOutput.dataBufSize[i],
                        subflowOutput.msgBufSize[i],
                        conns.peers[i].events & POLLRDNORM ? "YES" : "NO",
                        conns.peers[i].events & POLLWRNORM ? "YES" : "NO"
                        );
        }
        InfoMessage("# Msgs to be abandoned after fully read (MAX_FDS):  %d", tcpOutput.GetMsgListSize(MAX_FDS));
        InfoMessage("# Msgs to be included when SYN arrives (MAX_FDS+1): %d", tcpOutput.GetMsgListSize(MAX_FDS+1));

        InfoMessage( "TCP Data buffer: Capacity=%d  Size=%d  Tail=%d", PROXY_SETTINGS::tcpOverallBufDataCapacity, tcpOutput.dataCacheSize, tcpOutput.dataCacheEnd);
        InfoMessage( "TCP Msg  buffer: Capacity=%d  Size=%d  Tail=%d", PROXY_SETTINGS::tcpOverallBufMsgCapacity, tcpOutput.msgCacheSize, tcpOutput.msgCacheEnd);
	InfoMessage("------------------------------------------");

}

unsigned int getMetaBufferSize() {
	unsigned int buf = 0;
	int fd;
	unsigned int bytes_available;
	for (int i=subflows.n+1; i<=MAX_FDS; i++) {
                fd = conns.peers[i].fd;
		//bytes_available = 0;
		if (fd >= 0 && conns.peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC) {
                	ioctl(fd, FIONREAD ,&bytes_available);
                	if (bytes_available > 0)
				buf += bytes_available;
		}
        }
	return buf;
}

//return -1 if error
int SUBFLOWS::GetTotalSendBufferOccupancy() {
	int c = 0;
	int v;
	for (int i=0; i<n; i++) {
		int r = ioctl(fd[i], SIOCOUTQ, &v);
		if (r != 0) return -1;
		c += v;
	}
	return c;
}

int SUBFLOWS::AllSubflowsFull() {
	for (int i=1; i<=n; i++) if (!subflowOutput.IsSubflowFull(i)) return 0;

	return 1;
}

int SUBFLOWS::GetSubflowType(int subflowFD) {

	return SUBFLOWS::TCP_SUBFLOW;
}

void SUBFLOW_MSG::SwitchSched(int targetSched) {
    if (schedDecision != targetSched) {
        if (oldDecision == targetSched) {
            oldDecision = schedDecision;
            schedDecision = targetSched;
        } else {
            MyAssert(0, 1680);
        }
    }
}

void SUBFLOW_MSG::ResetSched(int subflowNo) {
    MyAssert(this->schedDecision == subflowNo && this->bytesLeft[subflowNo] == this->bytesOnWire, 1685);
    if (this->oldDecision != 0) {
        this->schedDecision = this->oldDecision;
    } else {
        this->schedDecision = 0;
        this->bytesLeft[subflowNo] = 0;
    }
}

void SUBFLOW_MSG::SetData(WORD connID, BYTE * pMsgData, WORD payloadLen) {
	this->msgType = MSG_DATA;
	this->connID = connID;
    for (int i = 1; i <= 4; i++)
	    this->pMsgData[i] = pMsgData;
	this->pMsgStart = pMsgData;
	
	this->payloadLen = payloadLen;
	this->bytesOnWire = (int)payloadLen + 8;
    //this->bytesLeft = bytesOnWire;

	VerboseMessage("Subflow msg created. Type=DATA, Payload=%d", (int)payloadLen);
}

void SUBFLOW_MSG::SetMoreData(WORD payloadLen) {
	MyAssert(this->msgType == MSG_DATA, 1662);
	this->payloadLen += payloadLen;
	this->bytesOnWire += payloadLen;
	//this->bytesLeft += payloadLen;

	VerboseMessage("Subflow msg updated. Payload=%d", (int)payloadLen);
}

void SUBFLOW_MSG::SetClose(WORD connID, BYTE * pMsgData, BYTE closeReason) {
	this->msgType = MSG_CLOSE;
	this->connID = connID;
    for (int i = 1; i <= 4; i++)
	    this->pMsgData[i] = pMsgData;
	this->pMsgStart = pMsgData;

	this->closeReason = closeReason;
	this->bytesOnWire = 8 + 1;
	//this->bytesLeft = this->bytesOnWire;

	VerboseMessage("Subflow msg created. Type=%s", closeReason == MSG_CLOSE_FIN ? "CLOSE_FIN" : "CLOSE_RST"); 
}

void SUBFLOW_MSG::SetSYNACK(WORD connID, BYTE * pMsgData) {
	this->msgType = MSG_SYNACK;
	this->connID = connID;
    for (int i = 1; i <= 4; i++)
	    this->pMsgData[i] = pMsgData;
	this->pMsgStart = pMsgData;

	this->bytesOnWire = 8 + 1;
	//this->bytesLeft = this->bytesOnWire;

	VerboseMessage("Subflow msg created. Type=SYNACK"); 
}

void SUBFLOW_MSG::SetCreate(WORD connID, BYTE * pMsgData, DWORD serverIP, WORD serverPort) {
	this->msgType = MSG_CREATE;
	this->connID = connID;
    for (int i = 1; i <= 4; i++)
	    this->pMsgData[i] = pMsgData;
	this->pMsgStart = pMsgData;

	this->serverIP = serverIP;
	this->serverPort = serverPort;
	this->bytesOnWire = MSG_HEADER_LEN + MSG_CREATE_LEN;
	//this->bytesLeft = this->bytesOnWire;

	VerboseMessage("Subflow msg created. Type=CREATE");
}

// FIXME: subflow number hard coded now.
// transmitted on all the currenly scheduled subflows
// currently only used for non-reinj cases
int SUBFLOW_MSG::isTransmitted() {
    //for (int i = 1; i <= 4; i++) {
    //    if (bTrasmitted[i] > 0) return 1;
    //}
    /* if (recalled) {
    	return 0;
    }*/
    // haven't even been scheduled
    if (schedDecision == 0 && oldDecision == 0) return 0;
    // scheduled but may not be transmitted
    if (schedDecision > 0) {
        if (bTransmitted[schedDecision] == 0) return 0;
    }
    // scheduled more than once
    if (oldDecision > 0) {
        if (bTransmitted[oldDecision] == 0) return 0;
    }
    return 1;
}

int SUBFLOW_MSG::isTransmitted(int subflowNo) {
	if (this->bSubflowAcked > 0) {
		return 1;
	}
	if (schedDecision == 0 && oldDecision == 0) {
		return 0;
	}
	return bTransmitted[subflowNo];
}

void SUBFLOW_MSG::Decode() {
	DWORD ack = 0;
	switch (this->msgType) {
		case MSG_DATA:
			this->payloadLen = this->bytesOnWire - 8;
			break;

		case MSG_OWD:
			//this->owd = *(int *)(pMsgData[0]);
            ack = *(DWORD *)(pMsgData[0]);
            kernelInfo.UpdateOWD((int) connID, (int) seq, ack);
			break;
		case MSG_CREATE:
			this->serverIP = *(DWORD *)(pMsgData[0]);
			this->serverPort = *(WORD *)(pMsgData[0] + 4);
			this->hints = *(DWORD *)(pMsgData[0] + 6);
			break;

		case MSG_CLOSE:
			this->closeReason = *pMsgData[0];
			break;

		case MSG_SYNACK:
			MyAssert(*pMsgData[0] == 0, 2188);
			break;

		default:
			MyAssert(0, 1669);
	}
}

void SUBFLOW_MSG::Encode(DWORD seq, BYTE * pBuf, BYTE * pBufBegin, const BYTE * pBufEnd) {
	//for data, just fill in the header (8 bytes)
	//for control msg, fill in everything
	
	VerboseMessage("SUBFLOW_MSG::Encode (seq=%u)", seq);

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

		case MSG_CREATE:
			*((DWORD *)(pData + 8)) = serverIP;
			*((WORD *)(pData + 12)) = serverPort;
			break;

		case MSG_CLOSE:
			*(pData + 8) = closeReason;
			break;

		case MSG_SYNACK:
			*(pData + 8) = 0;
			break;

		default:
			MyAssert(0, 1645);
	}

	if (pData == pBuf) return;
	for (int i=0; i<bytesToFill; i++) {
		*pBuf = *pData;
		pData++;
		if (++pBuf == pBufEnd) pBuf = pBufBegin;
	}
}

void SUBFLOW_MSG::Print() {
	InfoMessage("connID=%d seq=%d bytesOnWire=%d bytesLeft=%d(%d) sched=%d old=%d "
	 	"trans=%d(%d) tSeq1=%u tSeq2=%u acked=%d",
		connID, seq, bytesOnWire, bytesLeft[schedDecision],
        bytesLeft[oldDecision], schedDecision, oldDecision,
		bTransmitted[schedDecision], bTransmitted[oldDecision],
        transportSeq[1], transportSeq[2], bSubflowAcked);
}

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

void SUBFLOWS::DumpSubflowMsg(BYTE * pData, int len) {
#ifdef FEATURE_DUMP_SUBFLOW_MSG

	pthread_mutex_lock(&subflowDumpLock);
	if (ofsSubflowDump != NULL)
		fwrite(pData, len, 1, ofsSubflowDump);
	pthread_mutex_unlock(&subflowDumpLock);

#else
	MyAssert(0, 1835);
#endif

}

void SUBFLOWS::DumpSubflowMsg_subflowID(int pollPos) {
#ifdef FEATURE_DUMP_SUBFLOW_MSG

	MyAssert(pollPos>=1 && pollPos<=subflows.n, 1844);
	BYTE p = (BYTE)pollPos;

	pthread_mutex_lock(&subflowDumpLock);
	if (ofsSubflowDump != NULL)
		fwrite(&p, 1, 1, ofsSubflowDump);
	pthread_mutex_unlock(&subflowDumpLock);

#else
	MyAssert(0, 1843);
#endif
}

void SUBFLOWS::SetCongestionControl(int fd, const char * tcpVar) {
	if (!strcmp(tcpVar, "default") || !strcmp(tcpVar, "DEFAULT")) return;
	int r = setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, tcpVar, (int)strlen(tcpVar));
	MyAssert(r == 0, 1815);	
}

void SUBFLOWS::SetCwndLimit(int lim) {
	int l = lim / n;
	for (int i=0; i<n; i++) {
		SetSocketBuffer(fd[i], -1, l);
	}
}

int SUBFLOW_MONITOR::bStarted = 0;
map<DWORD, unsigned long> SUBFLOW_MONITOR::sendTS[MAX_SUBFLOWS];
int SUBFLOW_MONITOR::pktCount[MAX_SUBFLOWS];
DWORD SUBFLOW_MONITOR::expSeq[MAX_SUBFLOWS];
unsigned long SUBFLOW_MONITOR::rtt[MAX_SUBFLOWS];
unsigned long SUBFLOW_MONITOR::tsLastRTT[MAX_SUBFLOWS];
unsigned long SUBFLOW_MONITOR::tcpPayload[MAX_SUBFLOWS];

int SUBFLOW_MONITOR::SEQ_L(DWORD seq1, DWORD seq2) {
	//consider a 2GB window
	static const DWORD half = 2 << 30;
	
	if (seq2 >= half) 
		return seq1 < seq2 && seq1 > seq2 - half; 
	else
		return seq1 < seq2  || seq1 > seq2 + half;
}

int SUBFLOW_MONITOR::SEQ_LE(DWORD seq1, DWORD seq2) {
	return seq1 == seq2 || SEQ_L(seq1, seq2);
}

void SUBFLOW_MONITOR::ProcessPacket(u_char *args, const struct pcap_pkthdr *header, const u_char * pkt_data) {

	int len = (int)header->len;
	if (len < 14) return;
	len -= 14;

	WORD etherType = *((WORD *)(pkt_data + 12));
	if (etherType != 0x8) return;
	
	BYTE ipFlag = *((BYTE *)(pkt_data + 14));
	if ((ipFlag & 0xF0) != 0x40) return; 
	
	if ((ipFlag & 0x0F) < 5) return;
	DWORD ipOptionLength = 4 * ((ipFlag & 0x0F) - 5);

	WORD ipLength = Reverse(*((WORD *)(pkt_data + 14 + 2)));
	if (len < ipLength) return; else len = ipLength;
	if (len < (int)ipOptionLength + 20) return;

	/*
	pi.srcIP = *((DWORD *)(pkt_data + 14 + 12));
	pi.dstIP = *((DWORD *)(pkt_data + 14 + 16));
	pi.len = (WORD)len;
	pi.payloadLen = (WORD)(len - 20 - ipOptionLength);
	*/

	WORD payloadLen = (WORD)(len - 20 - ipOptionLength);

	//transport-layer protocol
	BYTE prot = *((BYTE *)(pkt_data + 14 + 9));
	len -= 20 + ipOptionLength;

	if (prot != 6 /*PROT_TCP*/ /*|| prot != 17*/) return; 
	{
		pkt_data += ipOptionLength;
		if (len < 20) return;
		
		/*
		pi.srcPort = Reverse(*((WORD *)(pkt_data + 14 + 20 + 0)));
		pi.dstPort = Reverse(*((WORD *)(pkt_data + 14 + 20 + 2)));
		pi.tcpFlag = *((BYTE *)(pkt_data + 14 + 20 + 13));	
		pi.seqNum = Reverse(*((DWORD *)(pkt_data + 14 + 20 + 4)));	
		pi.ackNum = Reverse(*((DWORD *)(pkt_data + 14 + 20 + 8)));	
		pi.winSize = Reverse(*((WORD *)(pkt_data + 14 + 20 + 14)));	
		*/

		WORD srcPort = Reverse(*((WORD *)(pkt_data + 14 + 20 + 0)));
		WORD dstPort = Reverse(*((WORD *)(pkt_data + 14 + 20 + 2)));

		int subflowID, dir;

		if (srcPort == REMOTE_PROXY_PORT) {
			dir = DOWNLINK;
			subflowID = subflows.port2SubflowID[dstPort];
		} else if (dstPort == REMOTE_PROXY_PORT) {
			dir = UPLINK;
			subflowID = subflows.port2SubflowID[srcPort];
		} else {
			return;
		}

		if (subflowID<0 || subflowID>=subflows.n) {
			WarningMessage("SUBFLOW_MONITOR: ignore dangling packet with dstPort = %d", dstPort);
			return;
		}

		int headerLen = (*((BYTE *)(pkt_data + 14 + 20 + 12)) & 0xF0) >> 2;		
		payloadLen -= headerLen;

		MyAssert(payloadLen >= 0, 2039);

		if (dir == DOWNLINK) {
			if (payloadLen == 0) return;
			DWORD seqNum = Reverse(*((DWORD *)(pkt_data + 14 + 20 + 4)));
			
			//retransmitted packets not considered
			if (SEQ_L(seqNum, expSeq[subflowID]) && pktCount[subflowID]>=0) return;

			if (++pktCount[subflowID] >= CLEANUP_PACKET_COUNT_THRESHOLD) {
				
				//clean
				map<DWORD, unsigned long> & h = sendTS[subflowID];
				map<DWORD, unsigned long>::iterator it = h.begin();
				unsigned long ts = GetHighResTimestamp();
				while (it != h.end()) {
					if (ts > CLEANUP_TIMING_GAP_THRESHOLD + it->second) {
						h.erase(it++);
					} else {
						++it;
					}
				}

				pktCount[subflowID] = 0;
				//InfoMessage("TotalBif=%d,  SubflowID=%d,   hashMapSize=%d", totalBif, subflowID+1, (int)h.size());
			}

			seqNum += payloadLen;
			expSeq[subflowID] = seqNum;
			sendTS[subflowID][seqNum] = GetHighResTimestamp();
			tcpPayload[subflowID] += payloadLen;
			
		} else { //UPLINK
			if (payloadLen > 0) return;
			DWORD ackNum = Reverse(*((DWORD *)(pkt_data + 14 + 20 + 8)));

			map<DWORD, unsigned long> & h = sendTS[subflowID];
			if (h.find(ackNum) == h.end()) return;	//likely a duplicated ACK

			unsigned long curTS = GetHighResTimestamp();
			unsigned long curRTT = curTS - h[ackNum];
			h.erase(ackNum);
			
			DWORD & es = expSeq[subflowID];

			/*
			if (!SEQ_LE(ackNum, es)) {
				InfoMessage("### ack=%u expSeq=%u ###", ackNum, es);
				MyAssert(0, 2041);
			}
			*/

			MyAssert(SEQ_LE(ackNum, es), 2041);

			//InfoMessage("RTT sample on subflow %d: %d ms", subflowID+1, curRTT/1000);
						
			if (curTS - tsLastRTT[subflowID] > RESET_RTT_ESTIMATION) {
				rtt[subflowID] = curRTT; 
			} else {
				MyAssert(rtt[subflowID] > 0, 2455);
				rtt[subflowID] = rtt[subflowID] * 7 / 8 + curRTT / 8;
			}
			if (rtt[subflowID] == 0) rtt[subflowID] = 1;	//0 is a special value
			tsLastRTT[subflowID] = curTS;
			
		}
	} 
}

void * SUBFLOW_MONITOR::PCAPStatsThread(void * arg) {
	bStarted++;
	char optval[16];
	unsigned int optlen = 16;
	while (statThreadRunning > 0) {
		//sleep(5);
		
		if (statCount % 1 == 0) {
		// if (statCount % 10 == 0) {
			// InfoMessage("*** Statistics ***");
			for (int i = 0; i < subflows.n; i++) {

				getsockopt(kernelInfo.fd[i+1], IPPROTO_TCP, TCP_CONGESTION, optval, &optlen);
				// if (i == 0) {
				// 	InfoMessage("subflow=%d  rtt=%d ms bytes=%lu KB ssth=%u cwnd=%u tcpACK=%u CC=%s space=%d inSendBuf=%d",
				// 	i+1, (int)(kernelInfo.GetSRTT(i+1)/1000), //subflowOutput.unaBytes[i+1]/1000,
				// 	tcpPayload[i]/1000, kernelInfo.GetSndSsthresh(i+1),
    //                 kernelInfo.GetSendCwnd(i+1),
				// 	subflowOutput.transportSeq[i+1], optval, kernelInfo.space[i+1],
				// 	kernelInfo.GetSndBuffer(i+1));
				// }
				// else {
				// 	InfoMessage("subflow=%d  rtt=%d ms bytes=%lu KB ssth=%u cwnd=%u tcpACK=%u CC=%s space=%d inSendBuf=%d pipeBufBytes=%d B, pipeOWD=%d ms, pipeBW=%d kbps",
				// 	i+1, (int)(kernelInfo.GetSRTT(i+1)/1000), //subflowOutput.unaBytes[i+1]/1000,
				// 	tcpPayload[i]/1000, kernelInfo.GetSndSsthresh(i+1),
    //                 kernelInfo.GetSendCwnd(i+1),
				// 	subflowOutput.transportSeq[i+1], optval, kernelInfo.space[i+1],
				// 	kernelInfo.GetSndBuffer(i+1), kernelInfo.bytesInPipe[i+1], kernelInfo.pipeOWD[i+1], kernelInfo.pipeBW[i+1]);
				// }
			}
			// printLog();
		}
		unsigned int size = metaBuffer.GetSize(MAX_PRIORITY);
		unsigned int bw[subflows.n];

		for (int i = 1; i < subflows.n + 1; i++) {
			bw[i] = kernelInfo.GetBW(i);
			if (size != 0) {
				// if (i == 1) {
				// 	InfoMessage("meta-buffer=%u B, statCount=%d, subflow1=%u bps (%u)",
				// 	size, statCount, bw[1], subflowOutput.transportACK[1]);
				// }
				// else {
				// 	InfoMessage("subflow%d=%u bps (%u), bytesInPipe=%d B, pipeOWD=%d ms, pipeBW=%d kbps",
				// 	 i, bw[i], subflowOutput.transportACK[i], 
				// 	 kernelInfo.bytesInPipe, kernelInfo.pipeOWD, kernelInfo.pipeBW);
				// }
			}
		}

		statCount++;
		nanosleep(&wait, &rem);
	}
	statThreadRunning = -1;
	InfoMessage("PCAP stats thread stopped.");
	return NULL;
}

void * SUBFLOW_MONITOR::PCAPThread(void * arg) {
	char errbuf[PCAP_ERRBUF_SIZE];
	pcap_t * handle = pcap_open_live("eth0", 128, 0, 0, errbuf);
	MyAssert(handle != NULL, 1965);
	bStarted++;
	pcap_loop(handle, -1, ProcessPacket, NULL);
	return NULL;
}

void SUBFLOW_MONITOR::StartListen() {
	bStarted = 0;
	memset(pktCount, 0xFF, sizeof(pktCount));
	memset(rtt, 0, sizeof(rtt));
	memset(tsLastRTT, 0, sizeof(tsLastRTT));
	memset(tcpPayload, 0, sizeof(tcpPayload));

	int r;

	pthread_t pcap_stats_thread;
	r = pthread_create(&pcap_stats_thread, NULL, SUBFLOW_MONITOR::PCAPStatsThread, NULL);
	MyAssert(r == 0, 2456);	

	pthread_t pcap_thread;	
	r = pthread_create(&pcap_thread, NULL, SUBFLOW_MONITOR::PCAPThread, NULL);
	MyAssert(r == 0, 1724);

	while (bStarted != 2) {pthread_yield();}
	InfoMessage("PCAP threads started.");

}
