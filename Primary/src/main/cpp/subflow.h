#ifndef MPBOND_SUBFLOW_H
#define MPBOND_SUBFLOW_H

#include "proxy_setting.h"
#include "proxy.h"
#include "connections.h"

struct SUBFLOW_MSG {
    static const WORD MSG_EMPTY =  0;
    static const WORD MSG_DATA =   0x1;
    static const WORD MSG_CREATE = 0xFFFF;
    static const WORD MSG_CLOSE =  0xFFFE;
    static const BYTE MSG_CLOSE_FIN = 0x88;
    static const BYTE MSG_CLOSE_RST = 0x99;
    static const WORD MSG_SYNACK = 0xFFFD;
    static const WORD MSG_OWD = 0xFFFC;
    static const WORD MSG_UDP = 0xFFFB;
    static const WORD MSG_ACK = 0xFFFA;
    static const WORD MSG_NOTE = 0xFFF9;

    static const int MSG_HEADER_LEN = 8;
    static const int MSG_CREATE_LEN = 10;

    static WORD GetBytesOnWire(WORD msgType);

    //the following SetXXX functions are only used for subflow buffer. They include the 8-byte subflow msg header
    void SetData(WORD connID, BYTE * pMsgData, WORD payloadLen);
    void SetMoreData(WORD payloadLen);
    void SetClose(WORD connID, BYTE * pMsgData, BYTE closeReason);
    void SetCreate(WORD connID, BYTE * pMsgData, DWORD serverIP, WORD serverPort);
    void SetSYNACK(WORD connID, BYTE * pData);
    void SetOWD(BYTE * pMsgData, WORD pipeNo, DWORD ack);
    void SetUDP(WORD bytesOnWire);
    void CheckUDP(int bytesOnWire);
    void SetACK(WORD connID, DWORD seq);

    void Encode(DWORD seq, BYTE * pBuf, BYTE * pBufBegin, const BYTE * pBufEnd);
    void Decode();

    int bytesOnWire;	//always include header
    int bytesLeft;

    //needed for both data and control msg
    WORD msgType;
    WORD connID;
    DWORD seq;

    //WORD owd_pipeNo; put into connID field for OWD
    DWORD owd_ack;

    //for data
    WORD payloadLen;
    BYTE * pMsgData;	//for pipe buffer: include the 8-byte pipe msg header
    //for tcp buffer: does not include the 8-byte pipe msg header

    //for MSG_CREATE
    DWORD serverIP;
    WORD serverPort;
    DWORD hints;

    //for MSG_UDP
    WORD clientPort;

    //for MSG_CLOSE
    BYTE closeReason;

    //for linked list in BUFFER_TCP
    SUBFLOW_MSG * pNext;

    //used in BUFFER_TCP::ReOrganizeMsgCache
    SUBFLOW_MSG * pTemp;

    //the status of the connection associated with the packet
    //can be CONN_INFO::CONN_INUSE/CONN_RECENTLY_CLOSED/CONN_EMPTY
    BYTE inUseFlag;
    BYTE drop;
};

struct SUBFLOW {

    static const int TCP_PIPE = 0;
    static const int UDP_PIPE = 1;
    static const int SCTP_PIPE = 2;

    int GetPipeType(int pipeFD);
    int GetUDPPipeID(int pipeFD); // from 0, corresponding to udpPeerAddr
    int n, nTCP;
    int fd[MAX_PIPES];
    int feedbackFD;
    int feedbackType;

    int Setup(const char * remoteIP, int feedbackType);
    //pipe selection algorithms
    int SelectPipe_WithBinding(WORD connID);
    int SelectPipe_MinimumBuf(WORD connID);	//this is the classic algorithm
    int SelectPipe_RoundRobin(WORD connID);
    int SelectPipe_Random(WORD connID);
    int SelectPipe_RoundRobinChunk(WORD connID);
    int SelectPipe_Fixed(WORD connID);
    int (SUBFLOW::*SelectPipeFunc_Main)(WORD connID);
    int (SUBFLOW::*SelectPipeFunc)(WORD connID);
    void SendPIE();
    void AlwaysOnFeedback();
};

#endif
