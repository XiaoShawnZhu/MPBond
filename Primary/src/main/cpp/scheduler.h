#ifndef MPBOND_SCHEDULER_H
#define MPBOND_SCHEDULER_H

#include "subflow.h"

/* Number of scheduling algorithms to consider
 * (size of schedulerSeq).
 * When adding new schedulers, the corresponding scheduler ID must be
 * added to schedulerSeq.
*/
#define SCHED_SEQ_NUM 12

struct SCHEDULER;

typedef SUBFLOW_MSG * (SCHEDULER::*SelectPipeFunc)();

struct SCHEDULER {
    double * pkt_delay;
    int schedulerSeq[SCHED_SEQ_NUM];
    //  mapping between schedNo (scheduler ID) and meta buffer index for different schedulers
    int schedulerQ[16 * 2];
    int indexSeq;
    int nTCP;
    int twoWayWiFi, twoWayLTE;

    void Setup();

    int schedHasPackets(int sched);
    int getNextSchedNo();
    int getNextSchedNo2();

    // SelectPipeFunc DefaultSelectPipe;
    int defaultScheduler;

    SelectPipeFunc SelectPipe[MAX_SCHEDULER];

    static const char * getSchedulerName(int sched);
    int getSchedNormalQueue(int sched);
    int getSchedFirstQueue(int sched);
    int getSchedLastQueue(int sched);

    void printPktDelay();
    void clearPktDelay();

    SUBFLOW_MSG * SelectPipe_ControlMsg();

private:
    //pipe selection algorithms
    SUBFLOW_MSG * SelectPipe_WithBinding();
    //SUBFLOW_MSG * SelectPipe_MinimumBuf();	//this is the classic algorithm
    //SUBFLOW_MSG * SelectPipe_RoundRobin();
    //SUBFLOW_MSG * SelectPipe_Random();
    //SUBFLOW_MSG * SelectPipe_RoundRobinChunk();
    //SUBFLOW_MSG * SelectPipe_Fixed();
    //SUBFLOW_MSG * SelectPipe_MinRTT();

    SUBFLOW_MSG * SelectPipe_TwoWay_Base(int saveBytes, int schedNo);
    SUBFLOW_MSG * SelectPipe_TwoWay();
    SUBFLOW_MSG * SelectPipe_TwoWay_Naive();
    SUBFLOW_MSG * SelectPipe_TwoWay_Balance();
    SUBFLOW_MSG * SelectPipe_NewTxDelay();
    SUBFLOW_MSG * SelectPipe_NewTxDelayBase(int queueNo);
    SUBFLOW_MSG * SelectPipe_MinRTT_Kernel();
    SUBFLOW_MSG * SelectPipe_TxDelay();
    SUBFLOW_MSG * SelectPipe_TxDelayPlus();
    SUBFLOW_MSG * SelectPipe_RoundRobin();
    SUBFLOW_MSG * SelectPipe_ReMP();
    SUBFLOW_MSG * SelectPipe_Wifi();
    SUBFLOW_MSG * SelectPipe_LTE();
    SUBFLOW_MSG * SelectPipe_BBS();
    SUBFLOW_MSG * SelectPipe_BBS_MRT();
    SUBFLOW_MSG * SelectPipe_EMPTCP();
    SUBFLOW_MSG * SelectPipe_Block();
    SUBFLOW_MSG * SelectPipe_UDP();

    SUBFLOW_MSG * SearchPacketForLargeRTTPath(int large_rtt_i, int small_rtt_i, int queueNo);
    SUBFLOW_MSG * GetMessageForPipe(int pipeNo, int queueNo, int other, int isSmallRTT, int saveBytes, uint64_t currt);
};

#endif
