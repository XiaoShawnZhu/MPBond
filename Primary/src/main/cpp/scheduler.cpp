#include "scheduler.h"
#include "kernel_info.h"
#include "meta_buffer.h"
#include "tools.h"
#include <math.h>
#include <sstream>
#include <algorithm>

extern struct KERNEL_INFO kernelInfo;
extern struct META_BUFFER metaBuffer;

extern int kfd;

extern FILE * ofsDebug;

#define DELAY_TOLERANCE 0.0 // ms

void SCHEDULER::Setup() {
    int tmp [] = {PIPE_SELECTION_NEWTXDELAY, PIPE_SELECTION_TWOWAY,
                  PIPE_SELECTION_TWOWAY_NAIVE, PIPE_SELECTION_TWOWAY_BALANCE,
                  PIPE_SELECTION_NEWTXDELAY_OLD,
                  PIPE_SELECTION_MINRTT_KERNEL, PIPE_SELECTION_EMPTCP,
                  PIPE_SELECTION_WIFIONLY, PIPE_SELECTION_LTEONLY,
                  PIPE_SELECTION_ROUNDROBIN, PIPE_SELECTION_REMP,
                  PIPE_SELECTION_UDP
    };
//    copy(tmp, tmp + SCHED_SEQ_NUM, schedulerSeq);

    nTCP = PROXY_SETTINGS::nTCPPipes;
    if (nTCP == 2) {
        twoWayWiFi = 1;
        twoWayLTE = 2;
    } else {
        twoWayWiFi = 3;
        twoWayLTE = 4;
    }
    // [sched * 2] -> start
    // [sched * 2 + 1] -> end
    int tmp2 [] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    //int tmp2 [] = {5, 6, 3, 4, 13, 14, 15, 16, 17, 18, 1, 2, 7, 8, 9, 10, 11, 12};

    tmp2[PIPE_SELECTION_NEWTXDELAY*2] = 5;
    tmp2[PIPE_SELECTION_NEWTXDELAY*2+1] = 6;

    tmp2[PIPE_SELECTION_TWOWAY*2] = 3;
    tmp2[PIPE_SELECTION_TWOWAY*2+1] = 4;
    tmp2[PIPE_SELECTION_TWOWAY_NAIVE*2] = 13;
    tmp2[PIPE_SELECTION_TWOWAY_NAIVE*2+1] = 14;
    tmp2[PIPE_SELECTION_TWOWAY_BALANCE*2] = 15;
    tmp2[PIPE_SELECTION_TWOWAY_BALANCE*2+1] = 16;

    tmp2[PIPE_SELECTION_NEWTXDELAY_OLD*2] = 17;
    tmp2[PIPE_SELECTION_NEWTXDELAY_OLD*2+1] = 18;

    tmp2[PIPE_SELECTION_MINRTT_KERNEL*2] = 1;
    tmp2[PIPE_SELECTION_MINRTT_KERNEL*2+1] = 2;

    tmp2[PIPE_SELECTION_EMPTCP*2] = 7;
    tmp2[PIPE_SELECTION_EMPTCP*2+1] = 8;

    tmp2[PIPE_SELECTION_WIFIONLY*2] = 9;
    tmp2[PIPE_SELECTION_WIFIONLY*2+1] = 10;

    tmp2[PIPE_SELECTION_LTEONLY*2] = 11;
    tmp2[PIPE_SELECTION_LTEONLY*2+1] = 12;

    tmp2[PIPE_SELECTION_ROUNDROBIN*2] = 19;
    tmp2[PIPE_SELECTION_ROUNDROBIN*2+1] = 20;

    tmp2[PIPE_SELECTION_REMP*2] = 21;
    tmp2[PIPE_SELECTION_REMP*2+1] = 22;

    tmp2[PIPE_SELECTION_UDP*2] = 23;
    tmp2[PIPE_SELECTION_UDP*2+1] = 24;

//    copy(tmp2, tmp2 + 16 * 2, schedulerQ);
    indexSeq = -1;

    defaultScheduler = PROXY_SETTINGS::pipeSelectionAlgorithm;

    for (int i = 0; i < MAX_SCHEDULER; i++) {
        switch (i) {
            case PIPE_SELECTION_TWOWAY:
                SelectPipe[i] = &SCHEDULER::SelectPipe_TwoWay;
                break;

            case PIPE_SELECTION_NEWTXDELAY:
                SelectPipe[i] = &SCHEDULER::SelectPipe_NewTxDelay;
                break;

            case PIPE_SELECTION_TWOWAY_NAIVE:
                SelectPipe[i] = &SCHEDULER::SelectPipe_TwoWay_Naive;
                break;

            case PIPE_SELECTION_TWOWAY_BALANCE:
                SelectPipe[i] = &SCHEDULER::SelectPipe_TwoWay_Balance;
                break;

            case PIPE_SELECTION_NEWTXDELAY_OLD:
                SelectPipe[i] = &SCHEDULER::SelectPipe_NewTxDelay;
                break;

            case PIPE_SELECTION_MINRTT_KERNEL:
                SelectPipe[i] = &SCHEDULER::SelectPipe_MinRTT_Kernel;
                break;

            case PIPE_SELECTION_TXDELAY:
                SelectPipe[i] = &SCHEDULER::SelectPipe_TxDelay;
                break;

            case PIPE_SELECTION_WIFIONLY:
                SelectPipe[i] = &SCHEDULER::SelectPipe_Wifi;
                break;

            case PIPE_SELECTION_LTEONLY:
                SelectPipe[i] = &SCHEDULER::SelectPipe_LTE;
                break;

                /*
                case PIPE_SELECTION_BBS:
                    SelectPipe[i] = &SCHEDULER::SelectPipe_BBS;
                    break;

                case PIPE_SELECTION_CBS:
                    break;
                */

            case PIPE_SELECTION_BBS_MRT:
                SelectPipe[i] = &SCHEDULER::SelectPipe_BBS_MRT;
                break;

            case PIPE_SELECTION_EMPTCP:
                SelectPipe[i] = &SCHEDULER::SelectPipe_EMPTCP;
                break;

            case PIPE_SELECTION_BLOCK:
                SelectPipe[i] = &SCHEDULER::SelectPipe_Block;
                break;

            case PIPE_SELECTION_UDP:
                SelectPipe[i] = &SCHEDULER::SelectPipe_UDP;
                break;

            case PIPE_SELECTION_ROUNDROBIN:
                SelectPipe[i] = &SCHEDULER::SelectPipe_RoundRobin;
                break;

            case PIPE_SELECTION_REMP:
                SelectPipe[i] = &SCHEDULER::SelectPipe_ReMP;
                break;

                //default:
                //	MyAssert(0, 1979);
        }
    }
//    pkt_delay = new double[PROXY_SETTINGS::metaBufMsgCapacity];
}

int SCHEDULER::schedHasPackets(int sched) {
    int start = schedulerQ[sched*2], end = schedulerQ[sched*2+1];
    return metaBuffer.HasPackets(start, end);
}

int SCHEDULER::getSchedNormalQueue(int sched) {
    return schedulerQ[sched*2+1];
}

int SCHEDULER::getSchedFirstQueue(int sched) {
    return schedulerQ[sched*2];
}

int SCHEDULER::getSchedLastQueue(int sched) {
    return schedulerQ[sched*2+1];
}

// Get the next scheduling algorithm to send packet
// currently use round robin based on schedulerSeq
int SCHEDULER::getNextSchedNo() {
    int start = indexSeq;
    if (start == -1) start = SCHED_SEQ_NUM - 1;
    indexSeq++;
    while (indexSeq != start) {
        if (schedHasPackets(schedulerSeq[indexSeq])) break;

        indexSeq++;
        if (indexSeq >= SCHED_SEQ_NUM) indexSeq = 0;
    }
    return schedulerSeq[indexSeq];
}

int SCHEDULER::getNextSchedNo2() {

    if (schedHasPackets(PIPE_SELECTION_NEWTXDELAY)) return PIPE_SELECTION_NEWTXDELAY;

    if (schedHasPackets(PIPE_SELECTION_TWOWAY)) return PIPE_SELECTION_TWOWAY;

    return PIPE_SELECTION_TWOWAY;
}

const char * schedInfo(const char * s, int sched) {
//    ostringstream output;
//    output << s << " (" << sched << ")";
//    return output.str().c_str();
}

const char * SCHEDULER::getSchedulerName(int sched) {
    switch (sched) {
        case PIPE_SELECTION_TWOWAY:
            return schedInfo("TwoWay", sched);

        case PIPE_SELECTION_NEWTXDELAY:
            return schedInfo("TxDelay++", sched);

        case PIPE_SELECTION_TWOWAY_NAIVE:
            return schedInfo("TwoWay Naive", sched);

        case PIPE_SELECTION_TWOWAY_BALANCE:
            return schedInfo("TwoWay Balance", sched);

        case PIPE_SELECTION_NEWTXDELAY_OLD:
            return schedInfo("TxDelay++ Old", sched);

        case PIPE_SELECTION_MINRTT_KERNEL:
            return schedInfo("MinRTT", sched);

        case PIPE_SELECTION_TXDELAY:
            return schedInfo("TxDelay (old)", sched);


        case PIPE_SELECTION_WIFIONLY:
            return schedInfo("WiFi only", sched);

        case PIPE_SELECTION_LTEONLY:
            return schedInfo("LTE only", sched);

            /*
            case PIPE_SELECTION_BBS:
                return schedInfo("BBS", sched);

            case PIPE_SELECTION_CBS:
                return schedInfo("CBS", sched);
                */

        case PIPE_SELECTION_BBS_MRT:
            return schedInfo("BBS MRT", sched);

        case PIPE_SELECTION_EMPTCP:
            return schedInfo("eMPTCP", sched);

        case PIPE_SELECTION_BLOCK:
            return schedInfo("Block (no TX)", sched);

        case PIPE_SELECTION_UDP:
            return schedInfo("UDP pipes", sched);

        case PIPE_SELECTION_ROUNDROBIN:
            return schedInfo("Round Robin", sched);

        case PIPE_SELECTION_REMP:
            return schedInfo("ReMP", sched);

//        default:
//            ostringstream output;
//            output << "Default ["
//                   << getSchedulerName(PROXY_SETTINGS::pipeSelectionAlgorithm)
//                   << " (" << PROXY_SETTINGS::pipeSelectionAlgorithm << ")]";
//            return output.str().c_str();
    }
}

SUBFLOW_MSG * SCHEDULER::GetMessageForPipe(int pipeNo, int queueNo, int other, int isSmallRTT, int saveBytes, uint64_t currt) {
    SUBFLOW_MSG * msg;
    //static int flag = 0;
    if (metaBuffer.bPartial[pipeNo] > 0) {
        MyAssert(metaBuffer.pPartial[pipeNo] != NULL, 9800);
//        metaBuffer.pPartial[pipeNo]->SwitchSched(pipeNo);
        return metaBuffer.pPartial[pipeNo];
    }

    if (pipeNo == twoWayWiFi) {
        msg = metaBuffer.GetFirstUnackedMsg(queueNo, pipeNo, other, isSmallRTT, saveBytes, currt);
    } else if (pipeNo == twoWayLTE) {
        msg = metaBuffer.GetLastUnackedMsg(queueNo, pipeNo, other, isSmallRTT, saveBytes, currt);
    } else {
        MyAssert(0, 9888);
    }

    if (msg) {

        return msg;
    } else {
        return NULL;
    }
}

SUBFLOW_MSG * SCHEDULER::SelectPipe_TwoWay() {
    return SelectPipe_TwoWay_Base(1, PIPE_SELECTION_TWOWAY);
}

SUBFLOW_MSG * SCHEDULER::SelectPipe_TwoWay_Naive() {
    return SelectPipe_TwoWay_Base(0, PIPE_SELECTION_TWOWAY_NAIVE);
}

SUBFLOW_MSG * SCHEDULER::SelectPipe_TwoWay_Balance() {
    return SelectPipe_TwoWay_Base(2, PIPE_SELECTION_TWOWAY_BALANCE);
}

SUBFLOW_MSG * SCHEDULER::SelectPipe_TwoWay_Base(int saveBytes, int schedNo) {
    int space[5];//, rtt[5];
    int large_i, small_i, this_pipe;
    static int last_pipe = 0;
    SUBFLOW_MSG * msg;
    int queueNo = getSchedNormalQueue(schedNo);
//    uint64_t currt = get_current_microsecond();

    //fprintf(ofsDebug, "Enter Decision\n");

    for (int i = 1; i <= nTCP; i++) {
        space[i] = kernelInfo.GetTCPAvailableSpace(i);
        //rtt[i] = kernelInfo.GetSRTT(i);
    }

    int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
//    double owd = kernelInfo.GetOWD(twoWayWiFi, bif1, bif2);
//    if (owd > 0) { // rtt[twoWayWiFi] < rtt[twoWayLTE]
//        large_i = twoWayLTE;
//        small_i = twoWayWiFi;
//    } else {
//        large_i = twoWayWiFi;
//        small_i = twoWayLTE;
//    }
    //InfoMessage("---TwoWay: Get Message.");

    //InfoMessage("--- Space: [1] %d [2] %d", space[1], space[2]);
    //metaBuffer.PrintMetaBuffer(1);

    if (last_pipe == 0) {
        if (space[small_i] > 0) {
            //fprintf(ofsDebug, "a-path %d\n", small_i);
//            msg = GetMessageForPipe(small_i, queueNo, large_i, 1, saveBytes, currt);
            if (msg) {
                last_pipe = small_i;
                return msg;
            }
        }
        if (space[large_i] > 0) {
            //fprintf(ofsDebug, "b-path %d\n", large_i);
//            msg = GetMessageForPipe(large_i, queueNo, small_i, 0, saveBytes, currt);
            if (msg) {
                last_pipe = large_i;
                return msg;
            }
        }
    } else {
        this_pipe = twoWayLTE + twoWayWiFi - last_pipe;
        int this_small, last_small;
        if (this_pipe == small_i) {
            this_small = 1;
            last_small = 0;
        } else {
            last_small = 1;
            this_small = 0;
        }
        //fprintf(ofsDebug, "last: %d, this: %d", last_pipe, this_pipe);
        if (space[this_pipe] > 0) {
//            msg = GetMessageForPipe(this_pipe, queueNo, last_pipe, this_small, saveBytes, currt);
            //fprintf(ofsDebug, "c-path %d\n", this_pipe);
            if (msg) {
                last_pipe = this_pipe;
                return msg;
            }
        }
        if (space[last_pipe] > 0) {
//            msg = GetMessageForPipe(last_pipe, queueNo, this_pipe, last_small, saveBytes, currt);
            //fprintf(ofsDebug, "d-path %d\n", last_pipe);
            if (msg) return msg;
        }
    }

    //InfoMessage("No i. msg: NULL");
    return NULL;
}

double EstimateAdditionalDelay(int rtt_us, unsigned int cwnd_pkt, unsigned int ssthresh_pkt,
                               unsigned int mss_bytes, unsigned int buf_bytes) {
    if (buf_bytes <= cwnd_pkt * mss_bytes)
        return 0.0;

    return ((double)((int)buf_bytes - (int)cwnd_pkt * (int)mss_bytes)
            / (double)mss_bytes/(double)cwnd_pkt) * (double) rtt_us / 1000.0;
}

// ms in delay
double EstimatePacketDelay(int rtt_us, unsigned int cwnd_pkt, unsigned int ssthresh_pkt,
                           unsigned int mss_bytes, unsigned int buf_bytes) {
    if (buf_bytes <= cwnd_pkt * mss_bytes)
        return (double) rtt_us / 1000.0;

    return ((double)buf_bytes / (double)mss_bytes/(double)cwnd_pkt) * (double) rtt_us / 1000.0;
}

// search range: [left, right]
// adjust the "index" if the pipe message it points to has already been transmitted
// index1: the largest index so that index1 <= index and packet[index1] is not transmitted
// index2: the smallest index so that index2 >= index and packet[index2] is not transmitted


SUBFLOW_MSG * SCHEDULER::SelectPipe_NewTxDelay() {
    int queueNo = getSchedNormalQueue(PIPE_SELECTION_NEWTXDELAY);
    return SelectPipe_NewTxDelayBase(queueNo);
}

SUBFLOW_MSG * SCHEDULER::SelectPipe_ControlMsg() {
    return SelectPipe_NewTxDelayBase(0);
}


SUBFLOW_MSG * SCHEDULER::SelectPipe_UDP() {
    // No congestion control here
    int start = getSchedFirstQueue(PIPE_SELECTION_UDP),
            end = getSchedLastQueue(PIPE_SELECTION_UDP);
    return metaBuffer.GetNextUntransmittedMsg(3, start, end);
}
