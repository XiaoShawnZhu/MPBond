#include "scheduler.h"
#include "kernel_info.h"
#include "meta_buffer.h"
#include "tools.h"
#include <math.h>
#include <sstream>
#include <algorithm>

extern struct KERNEL_INFO kernelInfo;
extern struct META_BUFFER metaBuffer;
extern struct SUBFLOWS subflows;

extern int kfd;
extern FILE * ofsDebug;
extern int bLateBindingMPBond;
extern int subflowInUseDMM;
extern int reinjPAMS;
extern int accBytes[3];

#define DELAY_TOLERANCE 0.0 // ms

void SCHEDULER::Setup() {
	int tmp [] = {SUBFLOW_SELECTION_NEWTXDELAY, SUBFLOW_SELECTION_TWOWAY,
                SUBFLOW_SELECTION_TWOWAY_NAIVE, SUBFLOW_SELECTION_TWOWAY_BALANCE,
                SUBFLOW_SELECTION_NEWTXDELAY_OLD,
				SUBFLOW_SELECTION_MINRTT_KERNEL, SUBFLOW_SELECTION_EMPTCP,
				SUBFLOW_SELECTION_WIFIONLY, SUBFLOW_SELECTION_ONEPATH,
                SUBFLOW_SELECTION_ROUNDROBIN, SUBFLOW_SELECTION_REMP,
                SUBFLOW_SELECTION_PAMS
            };
	copy(tmp, tmp + SCHED_SEQ_NUM, schedulerSeq);

    nTCP = PROXY_SETTINGS::nTCPSubflows;
    if (nTCP == 2) {
        twoWayWiFi = 1;
        twoWaySEC = 2;
    } else {
        twoWayWiFi = 3;
        twoWaySEC = 4;
    }
	// [sched * 2] -> start
	// [sched * 2 + 1] -> end
    int tmp2 [] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
	//int tmp2 [] = {5, 6, 3, 4, 13, 14, 15, 16, 17, 18, 1, 2, 7, 8, 9, 10, 11, 12};

    tmp2[SUBFLOW_SELECTION_NEWTXDELAY*2] = 5;
    tmp2[SUBFLOW_SELECTION_NEWTXDELAY*2+1] = 6;

    tmp2[SUBFLOW_SELECTION_TWOWAY*2] = 3;
    tmp2[SUBFLOW_SELECTION_TWOWAY*2+1] = 4;
    tmp2[SUBFLOW_SELECTION_TWOWAY_NAIVE*2] = 13;
    tmp2[SUBFLOW_SELECTION_TWOWAY_NAIVE*2+1] = 14;
    tmp2[SUBFLOW_SELECTION_TWOWAY_BALANCE*2] = 15;
    tmp2[SUBFLOW_SELECTION_TWOWAY_BALANCE*2+1] = 16;

    tmp2[SUBFLOW_SELECTION_NEWTXDELAY_OLD*2] = 17;
    tmp2[SUBFLOW_SELECTION_NEWTXDELAY_OLD*2+1] = 18;

    tmp2[SUBFLOW_SELECTION_MINRTT_KERNEL*2] = 1;
    tmp2[SUBFLOW_SELECTION_MINRTT_KERNEL*2+1] = 2;

    tmp2[SUBFLOW_SELECTION_EMPTCP*2] = 7;
    tmp2[SUBFLOW_SELECTION_EMPTCP*2+1] = 8;

    tmp2[SUBFLOW_SELECTION_WIFIONLY*2] = 9;
    tmp2[SUBFLOW_SELECTION_WIFIONLY*2+1] = 10;

    tmp2[SUBFLOW_SELECTION_ONEPATH*2] = 11;
    tmp2[SUBFLOW_SELECTION_ONEPATH*2+1] = 12;

    tmp2[SUBFLOW_SELECTION_ROUNDROBIN*2] = 19;
    tmp2[SUBFLOW_SELECTION_ROUNDROBIN*2+1] = 20;

    tmp2[SUBFLOW_SELECTION_REMP*2] = 21;
    tmp2[SUBFLOW_SELECTION_REMP*2+1] = 22;

    tmp2[SUBFLOW_SELECTION_PAMS*2] = 23;
    tmp2[SUBFLOW_SELECTION_PAMS*2+1] = 24;

	copy(tmp2, tmp2 + 16 * 2, schedulerQ);
	indexSeq = -1;

	defaultScheduler = PROXY_SETTINGS::subflowSelectionAlgorithm;

	for (int i = 0; i < MAX_SCHEDULER; i++) {
		switch (i) {
			case SUBFLOW_SELECTION_TWOWAY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_TwoWay;
				break;

			case SUBFLOW_SELECTION_NEWTXDELAY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_NewTxDelay;
				break;

            case SUBFLOW_SELECTION_TWOWAY_NAIVE:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_TwoWay_Naive;
                break;

            case SUBFLOW_SELECTION_TWOWAY_BALANCE:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_TwoWay_Balance;
                break;

            case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_NewTxDelay;
                break;

			case SUBFLOW_SELECTION_MINRTT_KERNEL:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_MinRTT_Kernel;
				break;

			case SUBFLOW_SELECTION_TXDELAY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_TxDelay;
				break;

			case SUBFLOW_SELECTION_WIFIONLY:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_Wifi;
				break;

			case SUBFLOW_SELECTION_ONEPATH:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_ONEPATH;
				break;

			case SUBFLOW_SELECTION_BBS_MRT:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_BBS_MRT;
				break;

			case SUBFLOW_SELECTION_EMPTCP:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_EMPTCP;
				break;

			case SUBFLOW_SELECTION_BLOCK:
				SelectSubflow[i] = &SCHEDULER::SelectSubflow_Block;
				break;

            case SUBFLOW_SELECTION_ROUNDROBIN:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_RoundRobin;
                break;

            case SUBFLOW_SELECTION_REMP:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_ReMP;
                break;

            case SUBFLOW_SELECTION_PAMS:
                SelectSubflow[i] = &SCHEDULER::SelectSubflow_PAMS;
                break;
		}
	}
    pkt_delay = new double[PROXY_SETTINGS::metaBufMsgCapacity];
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

    if (schedHasPackets(SUBFLOW_SELECTION_NEWTXDELAY)) return SUBFLOW_SELECTION_NEWTXDELAY;

    if (schedHasPackets(SUBFLOW_SELECTION_TWOWAY)) return SUBFLOW_SELECTION_TWOWAY;

    return SUBFLOW_SELECTION_TWOWAY;
}

const char * schedInfo(const char * s, int sched) {
    ostringstream output;
    output << s << " (" << sched << ")";
    return output.str().c_str();
}

const char * SCHEDULER::getSchedulerName(int sched) {
    switch (sched) {
        case SUBFLOW_SELECTION_TWOWAY:
            return schedInfo("TwoWay", sched);

        case SUBFLOW_SELECTION_NEWTXDELAY:
            return schedInfo("TxDelay++", sched);

        case SUBFLOW_SELECTION_TWOWAY_NAIVE:
            return schedInfo("TwoWay Naive", sched);

        case SUBFLOW_SELECTION_TWOWAY_BALANCE:
            return schedInfo("TwoWay Balance", sched);

        case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
            return schedInfo("TxDelay++ Old", sched);

        case SUBFLOW_SELECTION_MINRTT_KERNEL:
            return schedInfo("MinRTT", sched);

        case SUBFLOW_SELECTION_TXDELAY:
            return schedInfo("TxDelay (old)", sched);

        case SUBFLOW_SELECTION_WIFIONLY:
            return schedInfo("WiFi only", sched);

        case SUBFLOW_SELECTION_ONEPATH:
            return schedInfo("One path only", sched);

        case SUBFLOW_SELECTION_BBS_MRT:
            return schedInfo("BBS MRT", sched);

        case SUBFLOW_SELECTION_EMPTCP:
            return schedInfo("EMPTCP", sched);

        case SUBFLOW_SELECTION_BLOCK:
            return schedInfo("Block (no TX)", sched);
    
        case SUBFLOW_SELECTION_ROUNDROBIN:
            return schedInfo("Round Robin", sched);

        case SUBFLOW_SELECTION_REMP:
            return schedInfo("ReMP", sched);

        case SUBFLOW_SELECTION_PAMS:
            return schedInfo("PAMS", sched);

        default:
            ostringstream output;
            output << "Default ["
                << getSchedulerName(PROXY_SETTINGS::subflowSelectionAlgorithm)
                << " (" << PROXY_SETTINGS::subflowSelectionAlgorithm << ")]";
            return output.str().c_str();
    }
}

SUBFLOW_MSG * SCHEDULER::GetMessageForSubflow(int subflowNo, int queueNo, int other, int isSmallRTT, int saveBytes, uint64_t currt) {
    SUBFLOW_MSG * msg;

    if (metaBuffer.bPartial[subflowNo] > 0) {
        MyAssert(metaBuffer.pPartial[subflowNo] != NULL, 9800);
        metaBuffer.pPartial[subflowNo]->SwitchSched(subflowNo);
        return metaBuffer.pPartial[subflowNo];
    }

    if (subflowNo == twoWayWiFi) {
        msg = metaBuffer.GetFirstUnackedMsg(queueNo, subflowNo, other, isSmallRTT, saveBytes, currt);
    } else if (subflowNo == twoWaySEC) {
        msg = metaBuffer.GetLastUnackedMsg(queueNo, subflowNo, other, isSmallRTT, saveBytes, currt);
    } else {
        MyAssert(0, 9888);
    }

    if (msg) {
        return msg;
    } else {
        return NULL;
    }
}

// balance + redundant
SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay() {
    return SelectSubflow_TwoWay_Base(1, SUBFLOW_SELECTION_TWOWAY);
}
// reinject all unack
SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay_Naive() {
    return SelectSubflow_TwoWay_Base(0, SUBFLOW_SELECTION_TWOWAY_NAIVE);
}
// only balance

SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay_Balance() {
    return SelectSubflow_TwoWay_Base(2, SUBFLOW_SELECTION_TWOWAY_BALANCE);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_TwoWay_Base(int saveBytes, int schedNo) {

	int space[5];//, rtt[5];
	int large_i, small_i, this_subflow;
    static int last_subflow = 0;
	SUBFLOW_MSG * msg;
	int queueNo = getSchedNormalQueue(schedNo);
    uint64_t currt = get_current_microsecond();

    //fprintf(ofsDebug, "Enter Decision\n");

    for (int i = 1; i <= nTCP; i++) {
        space[i] = kernelInfo.GetTCPAvailableSpace(i);
        //rtt[i] = kernelInfo.GetSRTT(i);
    }

    int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
    double owd = kernelInfo.GetOWD(twoWayWiFi, bif1, bif2); // owd2 - owd1

	if (owd > 0) { // rtt[twoWayWiFi] < rtt[twoWaySEC]
		large_i = twoWaySEC;
		small_i = twoWayWiFi;
	} else {
		large_i = twoWayWiFi;
		small_i = twoWaySEC;
	}

	//InfoMessage("---TwoWay: Get Message.");

    //InfoMessage("--- Space: [1] %d [2] %d", space[1], space[2]);
    //metaBuffer.PrintMetaBuffer(1);

    if (last_subflow == 0) {
	    if (space[small_i] > 0) {
            //fprintf(ofsDebug, "a-path %d\n", small_i);
	        msg = GetMessageForSubflow(small_i, queueNo, large_i, 1, saveBytes, currt);
            if (msg) {
                last_subflow = small_i;
                return msg;
            }
        }
	    if (space[large_i] > 0) {
            //fprintf(ofsDebug, "b-path %d\n", large_i);
	        msg = GetMessageForSubflow(large_i, queueNo, small_i, 0, saveBytes, currt);
            if (msg) {
                last_subflow = large_i;
                return msg;
            }
        }
    } else {
        this_subflow = twoWaySEC + twoWayWiFi - last_subflow;
        int this_small, last_small;
        if (this_subflow == small_i) {
            this_small = 1;
            last_small = 0;
        } else {
            last_small = 1;
            this_small = 0;
        }
        //fprintf(ofsDebug, "last: %d, this: %d", last_subflow, this_subflow);
        if (space[this_subflow] > 0) {
            msg = GetMessageForSubflow(this_subflow, queueNo, last_subflow, this_small, saveBytes, currt);
            //fprintf(ofsDebug, "c-path %d\n", this_subflow);
            if (msg) {
                last_subflow = this_subflow;
                return msg;
            }
        }
        if (space[last_subflow] > 0) {
            msg = GetMessageForSubflow(last_subflow, queueNo, this_subflow, last_small, saveBytes, currt);
            //fprintf(ofsDebug, "d-path %d\n", last_subflow);
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
    /*
    if (cwnd_pkt < ssthresh_pkt) {
        // Slow start
        return (log2((double)buf_bytes / (double)mss_bytes/(double)cwnd_pkt) + 1.0) * (double) rtt_us / 1000.0;
    }
    */
    /*
    InfoMessage("Est delay: %f ms (buf=%uB, cwnd=%u, mss=%uB, rtt=%dus)",
    		((double)buf_bytes / (double)mss_bytes/(double)cwnd_pkt) * (double) rtt_us / 1000.0,
			buf_bytes, cwnd_pkt, mss_bytes, rtt_us);
	*/
    return ((double)buf_bytes / (double)mss_bytes/(double)cwnd_pkt) * (double) rtt_us / 1000.0;
}

// search range: [left, right]
// adjust the "index" if the subflow message it points to has already been transmitted
// index1: the largest index so that index1 <= index and packet[index1] is not transmitted
// index2: the smallest index so that index2 >= index and packet[index2] is not transmitted
void AdjustIndex(int queueNo, int & index, int & index1, int & index2, int left, int right) {
    SUBFLOW_MSG * iter = metaBuffer.GetMsgByIndex(queueNo, index);
    SUBFLOW_MSG * iter2 = iter;

    if (iter->isTransmitted() == 0) {
        index1 = index;
        index2 = index;
        return;
    }

    index1 = index;
    index2 = index;
    while (iter != metaBuffer.pMetaMsgTail[queueNo] && iter->isTransmitted() > 0
        && index2 <= right) {
        index2++;
        metaBuffer.IncrementMsgPointer(iter, queueNo);
    }

    while (iter2 != metaBuffer.GetDecrementMsgPointer(metaBuffer.pMetaMsgHead[queueNo], queueNo)
        && iter2->isTransmitted() > 0 && index1 >= left) {
        index1--;
        metaBuffer.DecrementMsgPointer(iter2, queueNo);
    }

    if (index1 < left && index2 > right) {
        index = -1;
        index1 = -1;
        index2 = -1;
        return;
    }

    if (index1 < left) {
        index1 = index2;
        index = index2;
        return;
    }

    if (index2 > right) {
        index = index1;
        index2 = index1;
        return;
    }

    index = index1;
}

void SCHEDULER::printPktDelay() {
	//pkt_delay
	printf("PktDelay: ");
	for (int i = 0; i < PROXY_SETTINGS::metaBufMsgCapacity; i++) {
		if (pkt_delay[i] != 0) {
			printf("%d:%fms,", i, pkt_delay[i]);
		}
	}
	printf("\n");
}

void SCHEDULER::clearPktDelay() {
	for (int i = 0; i < PROXY_SETTINGS::metaBufMsgCapacity; i++) {
		pkt_delay[i] = 0;
	}
}

// delay in ms
int ComputeSizeBasedOnDelay(double small_delay, double large_delay, int subflowNo) {
	double r = 0;
	r = large_delay - small_delay;
	if (r <= 0) return 0;
	//r = r / ((double)small_rtt / 1000.0) * (double)small_cwnd;
    r = r * kernelInfo.GetBW(subflowNo) / 8000.0;
	return int(r);
}

SUBFLOW_MSG * SCHEDULER::SearchPacketForLargeRTTPath(int large_rtt_i, int small_rtt_i, int queueNo) {
	// TODO: consider different priority levels
    if (metaBuffer.metaMsgSize[queueNo] == 0) return NULL;

    int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
    double owd = kernelInfo.GetOWD(small_rtt_i, bif1, bif2);
    int size = ComputeSizeBasedOnDelay(0.0, owd, small_rtt_i);
    SUBFLOW_MSG * msg = metaBuffer.GetMsgAfterSize(queueNo, large_rtt_i, size);
    if (msg == NULL) return NULL;
    if (metaBuffer.untransSize[queueNo] == -1) {
    	return msg;
    } else {
	    size = ComputeSizeBasedOnDelay(0.0, owd - DELAY_TOLERANCE, small_rtt_i);
	    if (size < metaBuffer.untransSize[queueNo])
	    	return msg;
	    else
	    	return NULL;
	}
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_NewTxDelayBase(int queueNo) {
	int space[3], j;

	for (int i = 1; i <= 2; i++) {
		space[i] = kernelInfo.GetTCPAvailableSpace(i);
	    //rtt[i] = kernelInfo.GetSRTT(i);
	}

	if (space[1] <= 0 && space[2] <= 0)
	    return NULL;

    double owd;
    int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);

    // owd for twoWayWiFi
    //if (owd > 0) { // rtt[twoWayWiFi] < rtt[twoWaySEC]

	for (int i = 1; i <= 2; i++) {
	    j = 3 - i;
	    if (space[i] > 0) {
            if (metaBuffer.bPartial[i] > 0) {
                MyAssert(metaBuffer.pPartial[i] != NULL, 9800);
                metaBuffer.pPartial[i]->SwitchSched(i);
                return metaBuffer.pPartial[i];
            }
            owd = kernelInfo.GetOWD(i, bif1, bif2);
	        if (owd > 0) {
	            return metaBuffer.GetNextUntransmittedMsg(i, queueNo);
	        } else if (space[j] <= 0) {
	            return SearchPacketForLargeRTTPath(i, j, queueNo);
	        }
	    }
	}

	return NULL;
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_NewTxDelay() {
    int queueNo = getSchedNormalQueue(SUBFLOW_SELECTION_NEWTXDELAY);
    return SelectSubflow_NewTxDelayBase(queueNo);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_ControlMsg() {
	return SelectSubflow_NewTxDelayBase(0);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_Wifi() {
    int space = kernelInfo.GetTCPAvailableSpace(1);
    int start = getSchedFirstQueue(SUBFLOW_SELECTION_WIFIONLY),
    		end = getSchedLastQueue(SUBFLOW_SELECTION_WIFIONLY);

    if (space <= 0) return NULL;
    if (metaBuffer.bPartial[1] > 0) {
        MyAssert(metaBuffer.pPartial[1] != NULL, 9800);
        metaBuffer.pPartial[1]->SwitchSched(1);
        return metaBuffer.pPartial[1];
    }
    else return metaBuffer.GetNextUntransmittedMsg(1, start, end);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_ONEPATH() {
    return SelectSubflow_ONEPATH_Base(subflowInUseDMM, SUBFLOW_SELECTION_ONEPATH);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_ONEPATH_Base(int subflowNo, int schedNo) {
    int space = kernelInfo.GetTCPAvailableSpace(subflowNo);
    int start = getSchedFirstQueue(schedNo),
         end = getSchedLastQueue(schedNo);

    if (space <= 0) return NULL;
    if (metaBuffer.bPartial[subflowNo] > 0) {                
        MyAssert(metaBuffer.pPartial[subflowNo] != NULL, 9800);
        metaBuffer.pPartial[subflowNo]->SwitchSched(subflowNo);
        return metaBuffer.pPartial[subflowNo];                            
    }
    else return metaBuffer.GetNextUntransmittedMsg(subflowNo, start, end);
}

// Pipe-Aware Multipath Scheduler (PAMS)
SUBFLOW_MSG * SCHEDULER::SelectSubflow_PAMS() {
    int buf[subflows.n + 1]; // bytes in subflow TCP send buffer
    int rtt[subflows.n + 1]; // subflow RTT
    double est[subflows.n + 1]; // pipe-aware one-way delay estimation
    int space[subflows.n + 1]; // available cwnd space
    double minEst[2] = {-1, 1000000}; // {subflowNo, ms}
    int r = -1;
    int allHasSpace = 1;
    double temp;
    int tmp;
    double order[subflows.n + 1]; // after sorting, order[] becomes the sorted version of est[]
    int map[subflows.n + 1]; // map the index of order[] to the index of est[] with the same value

    int start = getSchedFirstQueue(SUBFLOW_SELECTION_PAMS), end = getSchedLastQueue(SUBFLOW_SELECTION_PAMS);

    for (int i = 1; i < subflows.n + 1; i++) {
        space[i] = kernelInfo.GetTCPAvailableSpace(i);
        buf[i] = kernelInfo.GetSndBuffer(i);
        rtt[i] = kernelInfo.GetSRTT(i);
        allHasSpace *= (space[i] > 0);
        if (buf[i] >= PROXY_SETTINGS::subflowBufDataCapacity) goto DONE;
        if (i == 1) {
            // est[i] = 1;//((double) buf[i]) * 8 / (5000 + 1);
            est[i] = ((double) buf[i]) * 8 / (kernelInfo.GetBW(i) / 1000.0 + 1) + ((double) rtt[i]) / 1000.0 / 2;
        }
        else {
            est[i] = ((double) buf[i] + kernelInfo.bytesInPipe[i]) * 8 / (kernelInfo.pipeBW[i] + 1) 
                    + ((double) rtt[i]) / 1000.0 / 2 + kernelInfo.pipeRTT[i] / 2;
        }
        order[i] = est[i];
        map[i] = i;
        // InfoMessage("est[%d]=%f", i, est[i]);
        
    }
   
    // double f = (order[subflows.n] - order[1]) / order[1];
    if ((reinjPAMS == 1) && (metaBuffer.isRecallOn == 0)) {
        for (int i = 1; i < subflows.n; i++) {
            for (int j = 1; j <= subflows.n - i; j++) {
                if (order[j] > order[j+1]) {
                    temp = order[j];
                    order[j] = order[j+1];
                    order[j+1] = temp;
                    tmp = map[j];
                    map[j] = map[j+1];
                    map[j+1] = tmp;
                }
            }
        }

        if (order[subflows.n] > order[1] * 1.2) {
        // InfoMessage("Large diff %d", metaBuffer.GetUntransmittedMsgNumber());
            if (metaBuffer.GetUntransmittedMsgNumber() == 0) {
                metaBuffer.isRecallOn = 1;
                metaBuffer.recallBytes = (order[subflows.n] - order[1]) * kernelInfo.pipeBW[map[subflows.n]] / 8;
                // InfoMessage("Recalling");
            }
        }
    }
    
    // InfoMessage("est1 = %f, est2 = %f, isRecallOn=%d", est[1], est[2], metaBuffer.isRecallOn);
    // InfoMessage("*** PAD2=%f buf2=%d BIP2=%d space2=%d ***", 
    //     est[2], buf[2], kernelInfo.bytesInPipe[2], space[2]);
    
    if (bLateBindingMPBond) {
        for (int i = 1; i < subflows.n + 1; i++) {
            if (space[i] <= 0) {
                continue;
            }
            else {
                if (est[i] < minEst[1]) {
                    minEst[0] = i;
                    minEst[1] = est[i];
                }
            }
        }
    }
    else {
        // allow extra data in send buffer
       for (int i = 1; i < subflows.n + 1; i++) {
            if (est[i] < minEst[1]) {
                minEst[0] = i;
                minEst[1] = est[i];
            }
        }
    }
    r = minEst[0];

    if (r == -1)
        return NULL;

    if (metaBuffer.bPartial[r] > 0) {
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];
    }

    for (int i = 1; i < subflows.n + 1; i++) {
        if (metaBuffer.bPartial[i] > 0) {
            MyAssert(metaBuffer.pPartial[i] != NULL, 9800);
            metaBuffer.pPartial[i]->SwitchSched(i);
            return metaBuffer.pPartial[i];
        }
    }

DONE:
    // InfoMessage("");
    if (r <= 0) return NULL;
    if (bLateBindingMPBond) {
        // return metaBuffer.GetNextUntransmittedMsg(r, start, end);
        if (allHasSpace)
            return metaBuffer.GetNextUntransmittedMsg(r, start, end);
        return metaBuffer.GetNextMPBondMsg(r, start, end);
    }
    return metaBuffer.GetNextUntransmittedMsg(r, start, end);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_EMPTCP() {

    int r = -1;
    int start = getSchedFirstQueue(SUBFLOW_SELECTION_EMPTCP),
                end = getSchedLastQueue(SUBFLOW_SELECTION_EMPTCP);
    
    ioctl(kfd, CMAT_IOCTL_GET_SCHED_EMPTCP, &r);
    if (r <= 0) return NULL;
    if (metaBuffer.bPartial[r] > 0) {                
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];                            
    }
    return metaBuffer.GetNextUntransmittedMsg(r, start, end);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_BBS_MRT() {
    int r = -1;
    static int state = 0;
    unsigned int buf = metaBuffer.GetSize(MAX_PRIORITY);

    ioctl(kfd, CMAT_IOCTL_SET_META_BUFFER, buf);
    ioctl(kfd, CMAT_IOCTL_GET_SCHED_BUFFER_MRT, &r);

    if (r <= 0) return NULL;
    if (r > 2) {
        if (state == 0) {
            InfoMessage("To multipath state.");
            state = 1;
        }
        return metaBuffer.GetNextUntransmittedMsg(r-2, 15);
    }
    if (state == 1) {
        InfoMessage("To WiFi only state.");
        state = 0;
    }
    return metaBuffer.GetNextUntransmittedMsg(r, 15);
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_Block() {
    unsigned int buf = metaBuffer.GetSize(MAX_PRIORITY);
    InfoMessage("Buffer size: %d", buf);
    return NULL;
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_TxDelay() {
    int buf1 = -1, buf2 = -1, rtt1 = -1, rtt2 = -1, delay1 = -1, delay2 = -1;
    double est1 = 0.0, est2 = 0.0;
    int r = -1;

    buf1 = kernelInfo.GetSndBuffer(1);    
    buf2 = kernelInfo.GetSndBuffer(2);

    if (buf1 >= PROXY_SETTINGS::subflowBufDataCapacity) goto DONE;
    if (buf2 >= PROXY_SETTINGS::subflowBufDataCapacity) goto DONE;

    rtt1 = kernelInfo.GetSRTT(1);
    rtt2 = kernelInfo.GetSRTT(2);

    delay1 = rtt1 * 100000 / (kernelInfo.GetSendCwnd(1) * kernelInfo.GetSndMss(1));
    delay2 = rtt2 * 100000 / (kernelInfo.GetSendCwnd(2) * kernelInfo.GetSndMss(2));;

    est1 = ((double) delay1) / 100000.0 * ((double) buf1) / 1000.0 + ((double) rtt1) / 1000.0;
    est2 = ((double) delay2) / 100000.0 * ((double) buf2) / 1000.0 + ((double) rtt2) / 1000.0;

    //InfoMessage("*** Est delay selection=%d  buf1=%d delay1=%d buf2=%d delay2=%d && est1=%f est2=%f  ***", r, buf1, delay1, buf2, delay2, est1, est2);
    if (est1 < est2) r = 1;
    else {
        if (rtt1 < rtt2) r = 1;
    	else r = 2;
    }
DONE:
	InfoMessage("*** Info: BUF1=%d EST1=%d BUF2=%d EST2=%d", buf1, int(est1), buf2, int(est2));
    InfoMessage("*** Selected=%d   RTT1=%d   RTT2=%d   ***", r, rtt1/1000, rtt2/1000);
    if (r != -1 && SUBFLOW_MONITOR::rtt[r-1] == 0) SUBFLOW_MONITOR::rtt[r-1] = 1;
    if (r <= 0) return NULL;
    return metaBuffer.GetNextUntransmittedMsg(r, 15);
}


SUBFLOW_MSG * SCHEDULER::SelectSubflow_MinRTT_Kernel() {

    // for file split
    static int split;
    int r = -1;
    int space[subflows.n+1];
    int noneSpace = 1;
    int minRTT[2] = {-1, 1000000000}; // {subflowNo, us}
    int pipeRTT[subflows.n+1];

    for (int i = 1; i < subflows.n + 1; i++) {
        space[i] = kernelInfo.GetTCPAvailableSpace(i);
        noneSpace *= (space[i] <= 0);
    }
    if (noneSpace != 0)
        return NULL;

    int start = getSchedFirstQueue(SUBFLOW_SELECTION_MINRTT_KERNEL),
            end = getSchedLastQueue(SUBFLOW_SELECTION_MINRTT_KERNEL);

    if (PROXY_SETTINGS::isMPBondSchedEnhanced) {
        for (int i = 2; i < subflows.n + 1; i++) {
            pipeRTT[i] = kernelInfo.pipeRTT[i] * 1000;
            if (kernelInfo.bytesOnDevice[i] > 30000)
                space[i] = 0;
        }
    }
    else {
        for (int i = 2; i < subflows.n + 1; i++) {
            pipeRTT[i] = 0;
        }
    }
    pipeRTT[1] = 0;

    for (int i = 1; i < subflows.n + 1; i++) {
        if (space[i] <= 0) {
            continue;
        }
        else {
            int RTT = kernelInfo.GetSRTT(i) + pipeRTT[i];
            // if (i == 1)
            //     RTT = 1;
            // else
            //     RTT = 2;
            if (RTT < minRTT[1]) {
                minRTT[0] = i;
                minRTT[1] = RTT;
            }
        }
    }

    r = minRTT[0];
    // InfoMessage("MinRTT: subflow 1, space=%d, sched decision=%d", space[1], r);
    // InfoMessage("MinRTT: subflow 2, space=%d, sched decision=%d", space[2], r);

    if (r == -1) {
        return NULL;
    }

    if (metaBuffer.bPartial[r] > 0) {
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];
    }

    for (int i = 1; i < subflows.n + 1; i++){
        // InfoMessage("MinRTT: subflow %d, space=%d, sched decision=%d, RTT=%d, cwnd=%d", i, space[i], r, kernelInfo.GetSRTT(i) + pipeRTT[i], kernelInfo.GetSendCwnd(i));
        if (metaBuffer.bPartial[i] > 0) {
            MyAssert(metaBuffer.pPartial[i] != NULL, 9800);
            metaBuffer.pPartial[i]->SwitchSched(i);
            return metaBuffer.pPartial[i];
        }
    }

    return metaBuffer.GetNextUntransmittedMsg(r, start, end);

    MyAssert(0, 2459);
    return NULL;
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_RoundRobin() {
    static int k = 0;
    // static int acc = 0;
    int r = -1;
    int space1 = kernelInfo.GetTCPAvailableSpace(1), space2 = kernelInfo.GetTCPAvailableSpace(2);

    if (space1 <= 0 && space2 <= 0)
        return NULL;

    int start = getSchedFirstQueue(SUBFLOW_SELECTION_ROUNDROBIN),
            end = getSchedLastQueue(SUBFLOW_SELECTION_ROUNDROBIN);

    // if (accBytes[1] > /*2820000/1220000*/2020000/**/){
    //     r = 2;
    // }
    // else if (accBytes[2] > /*1220000/2820000*/2020000/**/) {
    //     r = 1;
    // }
    // else {
    //     if (space1 <= 0) {
    //         r = 2;
    //         k = 2;
    //     }
    //     else if (space2 <= 0) {
    //         r = 1;
    //         k = 1;
    //     }
    //     else {
    //         /* Round robin when both paths are available */
    //         if (++k > 2) {
    //             k = 1;
    //         }
    //         r = k;
    //     }
    // }

    if (space1 <= 0) {
        r = 2;
        k = 2;
    }
    else if (space2 <= 0) {
        r = 1;
        k = 1;
    }
    else {
        /* Round robin when both paths are available */
        if (++k > 2) {
            k = 1;
        }
        r = k;
    }

    // if (++k > 2) {
    //     k = 1;
    // }
    // r = k;

    // if (++k > 10) {
    //     k = 1;
    // }
    // r = (k > 7) ? 2 : 1;
    // r = (k > 3) ? 2 : 1;
    // r = (k > 5) ? 2 : 1;
    // InfoMessage("Sched decision r = %d, acc = %d", r, acc);
    InfoMessage("Sched decision r = %d, space[1] = %d, space[2] = %d", r, space1, space2);

    if (metaBuffer.bPartial[r] > 0) {
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        // k--;
        return metaBuffer.pPartial[r];
    }

    if (r == 1) {
        return metaBuffer.GetNextUntransmittedMsg(1, start, end);
    }

    if (r == 2) {
        // acc += 1300;
        return metaBuffer.GetNextUntransmittedMsg(2, start, end);
    }
    
    MyAssert(0, 2460);
    return NULL;
}

SUBFLOW_MSG * SCHEDULER::SelectSubflow_ReMP() {
    static int k = 0;
    int r = -1;
    int space1 = kernelInfo.GetTCPAvailableSpace(1), space2 = kernelInfo.GetTCPAvailableSpace(2);

    if (space1 <= 0 && space2 <= 0)
        return NULL;

    int start = getSchedFirstQueue(SUBFLOW_SELECTION_REMP),
            end = getSchedLastQueue(SUBFLOW_SELECTION_REMP);

    if (space1 <= 0) {
        r = 2;
        k = 2;
    }
    else if (space2 <= 0) {
        r = 1;
        k = 1;
    }
    else {
        /* Round robin when both paths are available */
        if (++k > 2) {
            k = 1;
        }
        r = k;
    }

    if (metaBuffer.bPartial[r] > 0) {
        MyAssert(metaBuffer.pPartial[r] != NULL, 9800);
        metaBuffer.pPartial[r]->SwitchSched(r);
        return metaBuffer.pPartial[r];
    }

    if (r == 1 || r == 2) {
        return metaBuffer.GetNextRempMsg(r, start, end);
    }
    
    MyAssert(0, 2461);
    return NULL;
}
