#ifndef _PRIORITY_H_
#define _PRIORITY_H_

#include "proxy.h"
#include "subflows.h"
#include "connections.h"
#include "proxy_setting.h"

struct SUBFLOW_MSG_MAPPING {
    DWORD tcpSeq;
    SUBFLOW_MSG * msg;
};

struct META_BUFFER {
	/*
	 * Priority queues (priority only takes effect within the same scheduling algorithm):
	 *   0 (use TxDelay++): control packets of MPFlex, e.g. SYN, SYNACK, FIN, etc.
	 *   1 (use MinRTT): data packets for MinRTT, high priority.
	 *   2 (use MinRTT): data packets for MinRTT, normal priority.
	 *   3 (use TwoWay): data packets for TwoWay, high priority.
	 *   4 (use TwoWay): data packets for TwoWay, normal priority.
	 *   5 (use TxDelay++): data packets for TxDelay++, high priority.
	 *   6 (use TxDelay++): data packets for TxDelay++, normal priority.
	 *   7 (use eMPTCP): data packets for eMPTCP, high priority.
	 *   8 (use eMPTCP): data packets for eMPTCP, normal priority.
	 *   9 (use WiFi only): data packets for WiFi, high priority.
	 *   10 (use WiFi only): data packets for WiFi, normal priority.
	 *   11 (use SEC only): data packets for SEC, high priority.
	 *   12 (use SEC only): data packets for SEC, normal priority.
	 *   13 (use TwoWay naive)
	 *   14 (use TwoWay naive)
	 *   15 (use TwoWay balance)
	 *   16 (use TwoWay balance)
	 *   17 (use TxDelay++ old)
	 *   18 (use TxDelay++ old)
	 *   19 (use RoundRobin): data packets for Round Robin, high priority.
	 *   20 (use RoundRobin): data packets for Round Robin, normal priority.
	 *   21 (use ReMP): data packets for ReMP, high priority.
	 *   22 (use ReMP): data packets for ReMP, normal priority.
	 */

    // Multipath meta buffer above sub-flows
    // circular queue
    SUBFLOW_MSG * pMetaMsgBuffer[MAX_PRIORITY];
    SUBFLOW_MSG * pMetaMsgHead[MAX_PRIORITY];
    SUBFLOW_MSG * pMetaMsgTail[MAX_PRIORITY];
	SUBFLOW_MSG * pMetaMsgEnd[MAX_PRIORITY];
	SUBFLOW_MSG * pSubMsgHead[MAX_PRIORITY][MAX_SUBFLOWS];

    int metaMsgSize[MAX_PRIORITY];

    SUBFLOW_MSG * pMetaMsgLastUna[MAX_PRIORITY];
    SUBFLOW_MSG * pMetaMsgFirstUna[MAX_PRIORITY];

	BYTE * pMetaDataBuffer[MAX_PRIORITY];    //the buffer (static)

    BYTE * pMetaDataHead[MAX_PRIORITY];              //head pointer
    BYTE * pMetaDataTail[MAX_PRIORITY];             //tail pointer  - point to the next available slot
	BYTE * pMetaDataEnd[MAX_PRIORITY];
    int metaDataSize[MAX_PRIORITY];

    int untransSize[MAX_PRIORITY];

    SUBFLOW_MSG * pPartial[MAX_SUBFLOWS];
    int bPartial[MAX_SUBFLOWS];
/*
    SUBFLOW_MSG * pPartialTwoWay[MAX_SUBFLOWS];
    int bPartialTwoWay[MAX_SUBFLOWS];

    SUBFLOW_MSG * pPartialMinRTT[MAX_SUBFLOWS];
    int bPartialMinRTT[MAX_SUBFLOWS];

    SUBFLOW_MSG * pPartialTxDelay[MAX_SUBFLOWS];
    int bPartialTxDelay[MAX_SUBFLOWS];
*/
    int hasPartial();

    SUBFLOW_MSG_MAPPING * pMappingBuffer[6]; // per subflow
    SUBFLOW_MSG_MAPPING * pMappingHead[6];
    SUBFLOW_MSG_MAPPING * pMappingTail[6];
    SUBFLOW_MSG_MAPPING * pMappingEnd[6];

    int pMappingSize[6];

    // for MPBond
    int isRecallOn;
    int recallBytes;

    void AddMsgToMapping(SUBFLOW_MSG * msg);
    void IncrementMappingPointer(SUBFLOW_MSG_MAPPING * & p, int subflowNo);

	void Setup();

    void CheckACK();

	// increment the pointer p
	void IncrementDataPointer(BYTE * & p, int delta, int prio);
	// get p + delta without changing p
	BYTE * GetIncrementDataPointer(BYTE * p, int delta, int prio);
	void IncrementMsgPointer(SUBFLOW_MSG * & p, int prio);
	void DecrementMsgPointer(SUBFLOW_MSG * & p, int prio);
	SUBFLOW_MSG * GetDecrementMsgPointer(SUBFLOW_MSG * p, int prio);

	void IncrementData(int prio);

	int HasPackets();
	int HasPackets(int start, int end);

	int ConvertMsgPointerToIndex(SUBFLOW_MSG * p);
	SUBFLOW_MSG * GetMsgByIndex(int prio, int index);
	int ComputeMsgDataSize(int prio, int start, int end);

	void UpdateTail(int prio);
	int GetPriority(int connID, int bControl);
	unsigned int GetSize(int prio);
	//int Enqueue(SUBFLOW_MSG * msg, int prio);
	//SUBFLOW_MSG * Dequeue(int subflowNo, int prio);
	void RescheduleAll();

	SUBFLOW_MSG * GetNextMsgNew(int schedDecision);
	
	SUBFLOW_MSG * GetNextUntransmittedMsg(int schedDecision, int queueNo);
	SUBFLOW_MSG * GetNextUntransmittedMsg(int schedDecision, int startQueue, int endQueue);
	SUBFLOW_MSG * GetNextRempMsg(int schedDecision, int startQueue, int endQueue);
	//SUBFLOW_MSG * GetNextUntransmittedMsg(int schedDecision);

	SUBFLOW_MSG * GetNextMPBondMsg(int schedDecision, int startQueue, int endQueue);

    int CanTransmit(SUBFLOW_MSG * msg, int otherSubflowNo, int direction);
    int BalanceTransfer(int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
    	int otherSubflowNo, int isSmallOWD, int direction, uint64_t currt);
    int ControlRedundantTransfer(int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
    	int otherSubflowNo, int isSmallOWD, int direction, uint64_t currt);

    int CanLargePathTransmit(int saveBytes, SUBFLOW_MSG * msg, int schedDecision, int otherSubflowNo, int direction, uint64_t currt);
    int CanSmallPathTransmit(int saveBytes, SUBFLOW_MSG * msg, int schedDecision, int otherSubflowNo, uint64_t currt);

    int CheckDelay(SUBFLOW_MSG* msg, int schedDecision, int other, uint64_t currt);
	SUBFLOW_MSG * GetFirstUnackedMsg(int prio, int schedDecision, int other,
		int isSmallOWD, int saveBytes, uint64_t currt);
	SUBFLOW_MSG * GetLastUnackedMsg(int prio, int schedDecision, int other,
		int isSmallOWD, int saveBytes, uint64_t currt);

	SUBFLOW_MSG * GetMsgAfterSize(int prio, int schedDecision, int size);

	int TransmitFromMetaBuffer(SUBFLOW_MSG * msg);

	void UpdateAfterTransmit(int prio);
	void UpdateAfterTransmit();
    void MarkACKedMsg(int subflowNo);
    void MarkACKedMsg();

	void PrintMetaBuffer(int count);
	void PrintMetaBuffer(int prio, int count);

	// for MPBond
	int GetUntransmittedMsgNumber();
	int GetUntransmittedMsgNumber(int prio);
	void UpdateAfterRecall(int prio, int n);
	int GetRecallingMsgNumber();
	int GetRecallingMsgNumber(int prio);
};

#endif
