#ifndef _CONNECTIONS_H_
#define _CONNECTIONS_H_

#include "proxy.h"
#include "subflows.h"
#include "proxy_setting.h"
#include "hints.h"

//#define REINJECTION

struct CONN_INFO {
	static const int CONN_INUSE = 1;
	static const int CONN_RECENTLY_CLOSED = 2;	
	static const int CONN_EMPTY = 3;	//e.g., useful when data subflow msg and SYN subflow msg are not in order

	//0: this connID is never used
	//0xFFFFFFFF: in use
	//other value: closed, the value is close time
	DWORD closeTime;

	//for local proxy: the actual client IP/port seen by the application on handset
	//for remote proxy: the IP/port of the remote proxy itself for the corresponding connection to remote server
	DWORD clientIP;
	int clientPort;

	//always the real server IP/port, regardless of local/remote proxy
	DWORD serverIP;
	int serverPort;
	HINT_ENTRY hints;

	int pollPos;
	//int fd;	//the TCP connection 

	inline int GetInUseStatus(DWORD now) {
		if (closeTime == 0xFFFFFFFF) return CONN_INUSE;

		//DWORD now = tickCount;
		//MyAssert(now >= closeTime, 1720);

		if ((int)now - (int)closeTime > PROXY_SETTINGS::connIDReuseTimeout) 
			return CONN_EMPTY;
		else
			return CONN_RECENTLY_CLOSED;
	}

	void SetInUseStatus(int s);

	int bytesInTCPBuffer;
	int bytesInSubflowBuffer;

	int accuChunkBytes;		//for chunk-based subflow selection
	int lastSubflowID;			//for chunk-based subflow selection

	//for scheduling algorithm
	//int accuBurstyBytes;
	//unsigned long r_clock;
	//unsigned long l_clock;

	// per-connection scheduler
	SUBFLOW_MSG * (CONN_INFO::*SelectSubflow)();
};

//extra states - associated with pollfd
struct POLLFD_EXT {
	static const BYTE EST_NOT_CONNECTED = 0;
	static const BYTE EST_SUCC = 1;
	static const BYTE EST_FAIL = 2;

	WORD connID;
	BYTE bSentSYNToSubflow;	//whether a SYN subflow message has been sent out - only used in local proxy
	BYTE establishStatus;	//EST_XXX (0,1,2), useful for remote proxy only (the TCP conn between remote proxy and the real server)
	DWORD connectTime;		//used to measure the connection establishment timeout (for remote proxy only)

	BYTE bToSendSYNACK;		//about to send an SYN-ACK msg to the client - only used in remote proxy when bZeroRTTHandshake==0
	BYTE bReceivedSYNACK;	//whether SYN-ACK msg has been received - only used in local proxy when bZeroRTTHandshake==0
};


struct CONNECTIONS;
struct BUFFER_TCP {
	//message cache
	SUBFLOW_MSG * msgCache1;
	SUBFLOW_MSG * msgCache2;
	SUBFLOW_MSG * pMsgCache;	//point to the current cache
	void ReOrganizeMsgCache();
	int msgCacheEnd;
	int msgCacheSize;
	int msgCacheOverallAlmostFullThreshold;	//if the cache size reaches this threshold, only msg of the expected seq can be accepted
	int msgCacheOverallFullThreshold;			//if the cache size reaches this threshold, no msg can be accepted
	int msgCacheOverallReOrgThreshold;			//if the tail reaches this threshold, perform reorganize

	//data cache
	BYTE * dataCache1;
	BYTE * dataCache2;
	BYTE * pDataCache;		//point to the current cache
	void ReOrganizeDataCache();
	int dataCacheEnd;
	int dataCacheSize;
	int dataCacheOverallAlmostFullThreshold;	//if the cache size reaches this threshold, only msg of the expected seq can be accepted
	int dataCacheOverallFullThreshold;			//if the cache size reaches this threshold, no msg can be accepted
	int dataCacheOverallReOrgThreshold;			//if the tail reaches this threshold, perform reorganize

	int dataCachePerConnFullThreshold;
	int dataCachePerConnAlmostFullThreshold;

	//pollPos of TCP connection -> first slot in pMsgCache 
	//special case: 
	//msgLists[MAX_FDS] stores those messages to be abandoned after fully read (CONN_INFO::CONN_RECENTLY_CLOSED)
	//msgLists[MAX_FDS + 1] stores those messages whose SYN subflow message hasn't arrived yet (CONN_INFO::CONN_EMPTY)
	SUBFLOW_MSG * msgLists[MAX_FDS + 2];
	int GetMsgListSize(int pollPos);
		
	DWORD expSeq[MAX_FDS];	//expected sequence number for TCP
	SUBFLOW_MSG * Allocate(WORD connID, int toPollPos, DWORD seq, WORD len);

	//[0..7] headers for SUBFLOWS
	//the 8-th byte:
	//	0: no header has been received
	//	1-7: # of header bytes has been received
	//	8: all header bytes received, SUBFLOW_MSG allocated, getting payload now
	BYTE headers[MAX_FDS][9];

	// subflow message for OWD
	SUBFLOW_MSG pOwdMsg;
	//currently processing msgs for SUBFLOWS
	SUBFLOW_MSG * pCurMsgs[MAX_FDS];
	
	void Setup();
	void NewTCPConnection(int pollPos);
	void RemoveTCPConnection(int pollPos, WORD connID);
		
	void TransferFromSubflowsToTCP(int toPollPos);

	void RemoveFromAbandonedList(const SUBFLOW_MSG * pMsg);
	void InsertSubflowMsgAndUpdateExpSeq(SUBFLOW_MSG * pMsg, WORD connID, int toPollPos);

	static const int BUFFER_NOT_FULL = 1;
	static const int BUFFER_CLOSE_TO_FULL = 2;
	static const int BUFFER_FULL = 3;	
	int GetBufferFullStatus(int connID);	
};

struct BUFFER_SUBFLOWS {
	
	//circular queue
	//Raw data always includes the subflow header, 
	BYTE * pDataBuffer[MAX_FDS];	//the buffer (static)
	BYTE * pDataHead[MAX_FDS];		//head pointer
	BYTE * pDataTail[MAX_FDS];		//tail pointer	- point to the next available slot
	int dataBufSize[MAX_FDS];

    BYTE owdBuffer[8];

	//circular queue
	SUBFLOW_MSG * pMsgBuffer[MAX_FDS];
	SUBFLOW_MSG * pMsgHead[MAX_FDS];
	SUBFLOW_MSG * pMsgTail[MAX_FDS];
	int msgBufSize[MAX_FDS];

	SUBFLOW_MSG * pMsgUnacked[MAX_FDS];  // Un-ACKed message queue
	SUBFLOW_MSG * pMsgUnaHead[MAX_FDS];
	SUBFLOW_MSG * pMsgUnaTail[MAX_FDS];
	SUBFLOW_MSG * pMsgTimestamp[MAX_FDS];
	int unaSize[MAX_FDS];
	int unaBytes[MAX_FDS];
	DWORD lastUnaSeq[MAX_FDS];

	SUBFLOW_MSG * pMsgReinject[MAX_FDS];
	SUBFLOW_MSG * pMsgRnjHead[MAX_FDS];
	SUBFLOW_MSG * pMsgRnjTail[MAX_FDS];
	SUBFLOW_MSG * pRnjUna[MAX_FDS];
	SUBFLOW_MSG * pRnjCurr[MAX_FDS];
	int rnjSize[MAX_FDS];

    SUBFLOW_MSG owdMsg;

	unsigned int transportSeq[MAX_FDS];
	DWORD transportACK[MAX_FDS];

	int bActiveReinject[MAX_FDS]; // add reinject packets to another subflow from the subflow of which have sending data 
	//for the above queues, only subscription range [1..nSubflows] is used

	//"genSeq" (generated sequence number) is for TCP connections, only subscription range [nSubflows+1..MAX_FDS-1] is used
	DWORD genSeq[MAX_FDS];

	// MPBond: UDP feedback over subflows
	int monitorMPBondFD;
	void MonitorMPBond();
	
	void Setup();
	void NewTCPConnection(int pollPos);
	void RemoveTCPConnection(int pollPos);
	void ResetSubflow(int pollPos);
	void UpdateACK();
	int UpdateSubflowACKStatus(SUBFLOW_MSG * msg, int subflowNo);
	void UpdateACKStatus(SUBFLOW_MSG * msg);
	
	//int TransferFromTCPToSubflows(int toPollPos, int toFD, int bReinject);
	int IsSubflowFull(int pollPos);
};

struct CONNECTIONS {
	static const int ESTABLISH_SUCC = 0;
	static const int ESTABLISH_ERROR = 1;
	static const int ESTABLISH_TIMEOUT = 2;

    int lastOWD;

	struct pollfd peers[MAX_FDS];
	struct POLLFD_EXT peersExt[MAX_FDS];
		
	CONN_INFO connTab[65536];

	int tmp_bif1, tmp_bif2;
	double tmp_owd1, tmp_owd2, tmp_var1, tmp_var2;

	// MPBond
	int feedbackFD;
		
	int Setup();
	int maxIdx;

	int GetTCPBufferSizeAllConns();
	int GetInUseConns();

	void PrintUnaQueue(int toPollPos);
	void PrintUnaQueueSimple(int toPollPos);
	void PrintRnjQueue(int toPollPos);
	void PrintMsgQueue(int toPollPos);
	void PrintMsgQueueSimple(int toPollPos);
	int AddReinjectPacket(int originPollPos, int reinjPollPos);
	void Reinjection(int toPollPos);

	const char * DumpConnection(WORD connID);
	void RemoveConnection(int pollPos);
	int TransferFromSubflowsToTCP(int pollPos, int fromFD);
		
	int TransferFromTCPToMetaBuffer(int pollPos, int fromFD, uint64_t ts);  //can also send a SYN or SYN-ACK subflow message, return 1 if the TCP connection is removed, o.w. 0
	void TransferDelayedFINsToSubflows();	//implementation similar to the above

	// Another copy of message data
	SUBFLOW_MSG * CopyFromMsgBufToUna(int toPollPos, SUBFLOW_MSG * pMsg);
	// Only copy the pointer of the message data
	void CopyFromRnjToMsgBuf(int rnjPollPos, int toPollPos);
	// Maintain originPollPos based on rnjPollPos
	void MaintainUnaQueue(int originPollPos, int rnjPollPos);

	void GetTCPSeqForUna(SUBFLOW_MSG * pCurrMsg, int nBytesWritten);
	int isRnjAcked(SUBFLOW_MSG * pMsgUnaHead, int rnjPollPos);

	int localListenFD;			//only for local proxy mode
	void UpdateNextConnID();	//only for local proxy mode
	WORD nextConnID;			//only for local proxy mode, 0 means invalid
	
	//for both local and remote
	int AddTCPConnection(int clientFD, DWORD clientIP, WORD clientPort, DWORD serverIP, WORD serverPort, WORD connID); //return pollPos
	
	int ConnectToRemoteServer(WORD connID, DWORD svrIP, WORD port); //remote proxy only
	void ConnectToRemoteServerDone(int pollPos, int result); //remote proxy only

	void EnableTCPWriteNotification(int pollPos, int bEnable);
	void EnableSubflowWriteNotification(int pollPos, int bEnable);

	int bTCPReadNtfEnabled;
	void EnableTCPReadNotifications(int bEnable);

	int bSubflowReadNtfEnabled;
	void EnableSubflowReadNotifications(int bEnable);

	void ResetReadNotificationEnableState();
	void SafeClose(int fd, int pollPos);

	/*
	//statistics
	DWORD statTotalMsgs;
	DWORD statTotalBytesOnWire;
	*/

	static void DumpIO(int reason, int nBytes, int subflowID, int connID, DWORD msgSeq);

	static int udpRead(int fd, BYTE * buf, int bytes);
};

struct DELAYED_FINS {
	static const int MAX_CAPACITY = 256;
	SUBFLOW_MSG fins[MAX_CAPACITY];
	int n;

	double d1;
	double d2;

	double deadline;

	void Setup(double d1, double d2);
	void EnQueue(SUBFLOW_MSG * pMsg, DWORD seq);
	void DeQueue(int m);
};


#endif
