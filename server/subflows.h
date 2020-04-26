#ifndef _SUBFLOWS_H_
#define _SUBFLOWS_H_

#include "proxy.h"

#define CMAT_MAGIC 122
#define CMAT_IOCTL_SET_SUBFLOW_FD1		_IOW(CMAT_MAGIC, 1, int)
#define CMAT_IOCTL_SET_SUBFLOW_FD2		_IOW(CMAT_MAGIC, 2, int)
#define CMAT_IOCTL_GET_SCHED		_IOR(CMAT_MAGIC, 3, int)
#define CMAT_IOCTL_GET_SCHED_DELAY	_IOR(CMAT_MAGIC, 4, int)
#define CMAT_IOCTL_GET_FD1_DELAY        _IOR(CMAT_MAGIC, 5, int)
#define CMAT_IOCTL_GET_FD2_DELAY        _IOR(CMAT_MAGIC, 6, int)
#define CMAT_IOCTL_GET_FD1_RTT        _IOR(CMAT_MAGIC, 7, int)
#define CMAT_IOCTL_GET_FD2_RTT        _IOR(CMAT_MAGIC, 8, int)
#define CMAT_IOCTL_GET_SCHED_BUFFER     _IOR(CMAT_MAGIC, 9, int)
#define CMAT_IOCTL_GET_SCHED_BUFFER_MRT     _IOR(CMAT_MAGIC, 12, int)
#define CMAT_IOCTL_SET_META_BUFFER      _IOW(CMAT_MAGIC, 10, int)
#define CMAT_IOCTL_GET_SCHED_EMPTCP     _IOR(CMAT_MAGIC, 11, int)
#define CMAT_IOCTL_SET_RESET_EMPTCP      _IOW(CMAT_MAGIC, 13, int)
// The following two are used together
#define CMAT_IOCTL_SET_SEQ_MAPPING      _IOW(CMAT_MAGIC, 14, unsigned long long)
#define CMAT_IOCTL_GET_SEQ_MAPPING      _IOR(CMAT_MAGIC, 15, unsigned long long)

#define CMAT_IOCTL_GET_FD1_ACK          _IOR(CMAT_MAGIC, 16, unsigned long long)
#define CMAT_IOCTL_GET_FD2_ACK          _IOR(CMAT_MAGIC, 17, unsigned long long)

#define CMAT_IOCTL_GET_FD1_RTTVAR        _IOR(CMAT_MAGIC, 18, int)
#define CMAT_IOCTL_GET_FD2_RTTVAR        _IOR(CMAT_MAGIC, 19, int)
#define CMAT_IOCTL_GET_META_BUFFER      _IOR(CMAT_MAGIC, 20, unsigned int)

#define CMAT_IOCTL_GET_FD1_CWNDTHRPT    _IOR(CMAT_MAGIC, 21, unsigned int)
#define CMAT_IOCTL_GET_FD2_CWNDTHRPT    _IOR(CMAT_MAGIC, 22, unsigned int)

#define CMAT_IOCTL_GET_FD1_AVAIL	_IOR(CMAT_MAGIC, 23, int)
#define CMAT_IOCTL_GET_FD2_AVAIL	_IOR(CMAT_MAGIC, 24, int)


#define CMAT_IOCTL_SET_DISABLE_MAPPING	_IOR(CMAT_MAGIC, 25, int)

#define CMAT_IOCTL_SET_SUBFLOW_FD3     _IOW(CMAT_MAGIC, 31, int)
#define CMAT_IOCTL_SET_SUBFLOW_FD4     _IOW(CMAT_MAGIC, 32, int)

#define CMAT_IOCTL_GET_FD3_AVAIL    _IOR(CMAT_MAGIC, 33, int)
#define CMAT_IOCTL_GET_FD4_AVAIL    _IOR(CMAT_MAGIC, 34, int)

#define CMAT_IOCTL_GET_FD3_ACK      _IOR(CMAT_MAGIC, 35, unsigned long long)
#define CMAT_IOCTL_GET_FD4_ACK          _IOR(CMAT_MAGIC, 36, unsigned long long)

#define CMAT_IOCTL_SET_BIF_REQUEST      _IOW(CMAT_MAGIC, 37, uint64_t)
#define CMAT_IOCTL_GET_BIF      _IOR(CMAT_MAGIC, 38, int)

/******************************************
Message format specifications
Common header: 8 bytes
+0 [connID,2B] (a valid connID is always positive)
+2 [seq,4B]
+6 [len,2B]: greater than 0. Special values:
	0xFFFF: MSG_CREATE -> translate to 12 bytes
	0xFFFE: MSG_CLOSE  -> translate to 7 bytes
	0xFFFD: MSG_SYNACK
	0xFFFC and below: MSG_DATA, real length including header
if data:
	+8 [Data, XXB]
if control message:
	if MSG_CREATE
		+8 [ServerIP, 4B]
		+12 [ServerPort, 2B]
		+14 [Sched/traffic type, 1B]
		+15 [Time hints, 1B, type 2b, value 6b]
		+16 [Energy hints, 1B, type 2b, value 6b]
		+17 [Data hints, 1B, type 2b, value 6b]
	if MSG_CLOSE
		+8 [reason, 1B], MSG_CLOSE_FIN or MSG_CLOSE_RST
	if MSG_SYNACK
		+8 [reserved, 1B, always 0]
*******************************************/

struct SUBFLOW_MSG {
	static const WORD MSG_EMPTY =  0;
	static const WORD MSG_DATA =   0x1;
	static const WORD MSG_CREATE = 0xFFFF;
	static const WORD MSG_CLOSE =  0xFFFE;
	static const BYTE MSG_CLOSE_FIN = 0x88;
	static const BYTE MSG_CLOSE_RST = 0x99;	
	static const WORD MSG_SYNACK = 0xFFFD;
	static const WORD MSG_OWD = 0xFFFC;

	static const int MSG_HEADER_LEN = 8;
	static const int MSG_CREATE_LEN = 10;

	static WORD GetBytesOnWire(WORD msgType);

	//the following SetXXX functions are only used for subflow buffer. They include the 8-byte subflow msg header
	void SetData(WORD connID, BYTE * pMsgData, WORD payloadLen);
	void SetMoreData(WORD payloadLen);
	void SetClose(WORD connID, BYTE * pMsgData, BYTE closeReason);
	void SetCreate(WORD connID, BYTE * pMsgData, DWORD serverIP, WORD serverPort);
	void SetSYNACK(WORD connID, BYTE * pData);
    int isTransmitted();
    int isTransmitted(int subflowNo);

	void CopyDataFrom(SUBFLOW_MSG * pMsg);
	void CopyPointerFrom(SUBFLOW_MSG * pMsg);

	void Encode(DWORD seq, BYTE * pBuf, BYTE * pBufBegin, const BYTE * pBufEnd);
	void Decode();

    void SwitchSched(int targetSched);
    void ResetSched(int subflowNo);
	void Print();

	int bytesOnWire;	//always include header
	int bytesLeft[5];
	int bReinjected;

	DWORD transportSeq[5];   // corresponds to transport layer Seq on each subflow
	int bValidTSeq[5];
	//needed for both data and control msg
	WORD msgType;
	WORD connID;
	DWORD seq;

	//for data
	WORD payloadLen;
	BYTE * pMsgStart;
    // 0 for receive, 1-4 for tx over subflows
	BYTE * pMsgData[5];	//for subflow buffer: include the 8-byte subflow msg header
						//for tcp buffer: does not include the 8-byte subflow msg header
    
	//for MSG_CREATE
	DWORD serverIP;
	WORD serverPort;
	DWORD hints;

	//for MSG_CLOSE
	BYTE closeReason;

	//for MSG_OWD
	int owd;

	//for linked list in BUFFER_TCP
	SUBFLOW_MSG * pNext;

	//for Meta Buffer
	//PIPR_MSG * pMetaNext;

	//used in BUFFER_TCP::ReOrganizeMsgCache
	SUBFLOW_MSG * pTemp;

	//the status of the connection associated with the packet
	//can be CONN_INFO::CONN_INUSE/CONN_RECENTLY_CLOSED/CONN_EMPTY
	BYTE inUseFlag;
	unsigned long timestamp;
	uint64_t txTime[5];
	int bRnjAcked;

	DWORD subflowSeq;

	// for meta buffer
	int schedDecision;
	int oldDecision;
	int priority;
	int bTransmitted[5];
	int bSubflowAcked;

	// for MPBond
	int bRecalled;
};

//only one instance
struct SUBFLOWS {
	static const int TCP_SUBFLOW = 0;
	static const int UDP_SUBFLOW = 1;
	static const int SCTP_SUBFLOW = 2;

	int n;
    int nTCP, nUDP;
	int fd[MAX_SUBFLOWS];

	BYTE port2SubflowID[65536];

	struct sockaddr_in udpPeerAddr[2];
	int udpPeerPort[2];
	socklen_t udpPeerAddrLen[2];

	int Setup(const char * remoteIP);
	
	//int (SUBFLOWS::*SelectSubflowFunc)(WORD connID);
	//int (SUBFLOWS::*SelectSubflowFunc_Main)(WORD connID);
	
	int AllSubflowsFull();
	int GetSubflowType(int fd);
	// int GetUDPSubflowID(int subflowFD);

	//for handset (bHandset = 1)
	char remoteIP[32];	//proxy server IP, only meaningful when bHandset = 1

	//for remote proxy (bHandset = 0)
	int listenFD;

	void DumpSubflowMsg(BYTE * pData, int len);
	void DumpSubflowMsg_subflowID(int pollPos);

	static void SetCongestionControl(int fd, const char * tcpVar);
	void SetCwndLimit(int lim);

	int GetTotalSendBufferOccupancy();
};

struct CONN_INFO_KB {
	DWORD head;			//(4B) in bytes
	DWORD tail;			//(4B) in bytes
	DWORD seq;			//(4B) subflow msg seq for the next packet (i.e., subflow msg)
	int lpFinTS;	 //(4B)0=not set, otherwise timestamp
	WORD offset;		//(2B) in chunk
	BYTE n_chunks;	//(1B) how many chunks does the follow occupy? 0 means the slot is not used
	BYTE rpFinStat;		//(1B) 0=not injected; MSG_CLOSE_FIN or MSG_CLOSE_RST=injected but not sent; 1=injected & sent

	//for scheduling
	DWORD accuBytes;	//(4B)
	unsigned long r_clock;	//(8B)	real clock
	unsigned long l_clock;	//(8B)	logic clock
};


struct SUBFLOW_MONITOR {

private:
	SUBFLOW_MONITOR();
	~SUBFLOW_MONITOR();

private:
	static int bStarted;
	static map<DWORD, unsigned long> sendTS[MAX_SUBFLOWS];
	static int pktCount[MAX_SUBFLOWS];	//initialized to -1
	static DWORD expSeq[MAX_SUBFLOWS];	//highest level of TCP seq#	
	static unsigned long tsLastRTT[MAX_SUBFLOWS];	//last RTT record ts
	static unsigned long tcpPayload[MAX_SUBFLOWS];

	static const int CLEANUP_PACKET_COUNT_THRESHOLD = 5000;
	static const int CLEANUP_TIMING_GAP_THRESHOLD = 5000000;	//5 seconds
	static const int RESET_RTT_ESTIMATION = 1000000;	//1 second

public:
	static unsigned long rtt[MAX_SUBFLOWS];	//RTT in us

private:
	static int SEQ_L(DWORD seq1, DWORD seq2);
	static int SEQ_G(DWORD seq1, DWORD seq2);
	static int SEQ_LE(DWORD seq1, DWORD seq2);
	static int SEQ_GE(DWORD seq1, DWORD seq2);

public:
	static void StartListen();
	static void * PCAPThread(void * arg);
	static void * PCAPStatsThread(void * arg);

	static void ProcessPacket(u_char *args, const struct pcap_pkthdr *header, const u_char *packet);
	static void ProcessData(int subflowID, DWORD seqNum, const BYTE * pData, DWORD payloadLen);
};

#endif

