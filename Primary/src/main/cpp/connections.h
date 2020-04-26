#ifndef MPBOND_CONNECTIONS_H
#define MPBOND_CONNECTIONS_H

#include "proxy.h"
#include "proxy_setting.h"
#include "tools.h"
#include "subflow.h"
#include "kernel_info.h"
#include "pipe.h"


struct SUBFLOW_MSG;

struct CONN_INFO {
    static const int CONN_INUSE = 1;
    static const int CONN_RECENTLY_CLOSED = 2;
    static const int CONN_EMPTY = 3;	//e.g., useful when data pipe msg and SYN pipe msg are not in order

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
    int bytesInPipeBuffer;

    int accuChunkBytes;		//for chunk-based pipe selection
    int lastPipeID;			//for chunk-based pipe selection

    //for scheduling algorithm
    int accuBurstyBytes;
    unsigned long r_clock;
    unsigned long l_clock;
};

//extra states - associated with pollfd
struct POLLFD_EXT {
    static const BYTE EST_NOT_CONNECTED = 0;
    static const BYTE EST_SUCC = 1;
    static const BYTE EST_FAIL = 2;

    WORD connID;
    BYTE bSentSYNToPipe;	//whether a SYN pipe message has been sent out - only used in local proxy
    BYTE establishStatus;	//EST_XXX (0,1,2), useful for remote proxy only (the TCP conn between remote proxy and the real server)
    DWORD connectTime;		//used to measure the connection establishment timeout (for remote proxy only)

    BYTE bToSendSYNACK;		//about to send an SYN-ACK msg to the client - only used in remote proxy when bZeroRTTHandshake==0
    BYTE bReceivedSYNACK;	//whether SYN-ACK msg has been received - only used in local proxy when bZeroRTTHandshake==0
};

struct CONNECTIONS {

    int localListenFD;			//only for local proxy mode
    struct pollfd peers[MAX_FDS];
    struct POLLFD_EXT peersExt[MAX_FDS];
    int maxIdx;
    WORD nextConnID;			//only for local proxy mode, 0 means invalid
    CONN_INFO connTab[65536];

    int Setup();
    void UpdateNextConnID();
    int AddTCPConnection(int clientFD,
                         DWORD clientIP, WORD clientPort,
                         DWORD serverIP, WORD serverPort,	/*must be 0 for local proxy*/
                         WORD connID	/* must be 0 for local proxy */);
    int TransferFromTCPToSubflows(int fromPollPos, int fromFD);
    int TransferFromSubflowsToTCP(int pollPos, int fromFD);
    void ResetReadNotificationEnableState();
    void SafeClose(int fd, int pollPos);
    void RemoveConnection(int pollPos);
    void TransferDelayedFINsToSubflows();	//implementation similar to the above
    void EnableSubflowWriteNotification(int pollPos, int bEnable);

    static void DumpIO(int reason, int nBytes, int pipeID, int connID, DWORD msgSeq);

    void EnableTCPWriteNotification(int pollPos, int bEnable);

    const char *DumpConnection(WORD connID, WORD msgType);
};

struct BUFFER_SUBFLOW {

    //circular queue
    //Raw data always includes the subflow header,
    BYTE * pDataBuffer[MAX_FDS];	//the buffer (static)
    BYTE * pDataHead[MAX_FDS];		//head pointer
    BYTE * pDataTail[MAX_FDS];		//tail pointer	- point to the next available slot
    int dataBufSize[MAX_FDS];

    //circular queue
    SUBFLOW_MSG * pMsgBuffer[MAX_FDS];
    SUBFLOW_MSG * pMsgHead[MAX_FDS];
    SUBFLOW_MSG * pMsgTail[MAX_FDS];
    int msgBufSize[MAX_FDS];

    //for the above queues, only subscription range [1..nPipes] is used

    //"genSeq" (generated sequence number) is for TCP connections, only subscription range [nPipes+1..MAX_FDS-1] is used
    DWORD genSeq[MAX_FDS];

    void Setup();
    void NewTCPConnection(int pollPos);
    void RemoveTCPConnection(int pollPos);
    void ResetPipe(int pollPos);

    int TransferFromTCPToSubflows(int toPollPos, int toFD);
    int IsPipeFull(int pollPos);
};


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

    BYTE * writeBuf[MAX_FDS + 2];
    BYTE * writePos[MAX_FDS + 2];
    int writeSize[MAX_FDS + 2];

    //pollPos of TCP connection -> first slot in pMsgCache
    //special case:
    //msgLists[MAX_FDS] stores those messages to be abandoned after fully read (CONN_INFO::CONN_RECENTLY_CLOSED)
    //msgLists[MAX_FDS + 1] stores those messages whose SYN pipe message hasn't arrived yet (CONN_INFO::CONN_EMPTY)
    SUBFLOW_MSG * msgLists[MAX_FDS + 2];
    int GetMsgListSize(int pollPos);
    int GetMsgListBytes(int pollPos);

    DWORD expSeq[MAX_FDS];	//expected sequence number for TCP
    SUBFLOW_MSG * Allocate(WORD connID, int toPollPos, DWORD seq, WORD len, int pipeNo);

    //[0..7] headers for PIPES
    //the 8-th byte:
    //	0: no header has been received
    //	1-7: # of header bytes has been received
    //	8: all header bytes received, SUBFLOW_MSG allocated, getting payload now
    BYTE headers[MAX_FDS][9];

    //currently processing msgs for PIPES
    SUBFLOW_MSG * pCurMsgs[MAX_FDS];

    void Setup();
    void NewTCPConnection(int pollPos);
    void RemoveTCPConnection(int pollPos, WORD connID);


    void RemoveFromAbandonedList(const SUBFLOW_MSG * pMsg);
    void InsertSubflowMsgAndUpdateExpSeq(SUBFLOW_MSG * pMsg, WORD connID, int toPollPos, int pipeNo);

    static const int BUFFER_NOT_FULL = 1;
    static const int BUFFER_CLOSE_TO_FULL = 2;
    static const int BUFFER_FULL = 3;
    int GetBufferFullStatus(int connID);
    int EchoBack(int pipeNo, WORD connID, DWORD seq);

    void TransferFromSubflowsToTCP(int toPollPos);
};

#endif