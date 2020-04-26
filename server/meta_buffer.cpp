#include "stdafx.h"
#include "kernel_info.h"
#include "meta_buffer.h"
#include "tools.h"
#include "scheduler.h"

extern int proxyMode;
extern struct SUBFLOWS subflows;
extern struct CONNECTIONS conns;
extern struct BUFFER_SUBFLOWS subflowOutput;
extern struct BUFFER_TCP tcpOutput;
extern struct META_BUFFER metaBuffer;
extern struct DELAYED_FINS delayedFins;
extern struct SCHEDULER scheduler;
extern struct KERNEL_INFO kernelInfo;

extern int tickCount;
extern int lastSubflowActivityTick;

extern FILE * ofsDebug;

int isFinished = 0;

void META_BUFFER::Setup() {

	isRecallOn = 0;

	memset(pMetaMsgBuffer, 0, sizeof(pMetaMsgBuffer));
	memset(pMetaMsgHead, 0, sizeof(pMetaMsgHead));
	memset(pMetaMsgTail, 0, sizeof(pMetaMsgTail));
	memset(pMetaMsgEnd, 0, sizeof(pMetaMsgEnd));
	memset(pSubMsgHead, 0, sizeof(pSubMsgHead));
	memset(metaMsgSize, 0, sizeof(metaMsgSize));

	memset(pMetaDataBuffer, 0, sizeof(pMetaDataBuffer));
	memset(pMetaDataHead, 0, sizeof(pMetaDataHead));
	memset(pMetaDataTail, 0, sizeof(pMetaDataTail));
	memset(pMetaDataEnd, 0, sizeof(pMetaDataEnd));
	memset(metaDataSize, 0, sizeof(metaDataSize));
	memset(untransSize, 0, sizeof(untransSize));

    memset(pMappingBuffer, 0, sizeof(pMappingBuffer));
    memset(pMappingHead, 0, sizeof(pMappingHead));
    memset(pMappingTail, 0, sizeof(pMappingTail));
    memset(pMappingEnd, 0, sizeof(pMappingEnd));

    memset(pPartial, 0, sizeof(pPartial));
    memset(bPartial, 0, sizeof(bPartial));

	for (int i = 0; i < MAX_PRIORITY; i++) {
		pMetaMsgBuffer[i] = new SUBFLOW_MSG[PROXY_SETTINGS::metaBufMsgCapacity];
		pMetaDataBuffer[i] = new BYTE[PROXY_SETTINGS::metaBufDataCapacity];
		MyAssert(pMetaMsgBuffer[i] != NULL && pMetaDataBuffer[i] != NULL, 7000);

		pMetaMsgHead[i] = NULL; pMetaMsgTail[i] = pMetaMsgBuffer[i];
		pMetaMsgEnd[i] = pMetaMsgBuffer[i] + PROXY_SETTINGS::metaBufMsgCapacity;

		pMetaDataHead[i] = NULL; pMetaDataTail[i] = pMetaDataBuffer[i];
		pMetaDataEnd[i] = pMetaDataBuffer[i] + PROXY_SETTINGS::metaBufDataCapacity;
		
		for (int j = 0; j < MAX_SUBFLOWS; j++) {
			pSubMsgHead[i][j] = NULL;
		}
	}

    memset(pMappingSize, 0, sizeof(pMappingSize));

    for (int i = 0; i < 6; i++) {
        pMappingBuffer[i] = new SUBFLOW_MSG_MAPPING[PROXY_SETTINGS::metaBufMsgCapacity * 2];
        pMappingHead[i] = NULL; pMappingTail[i] = pMappingBuffer[i];
        pMappingEnd[i] = pMappingBuffer[i] + PROXY_SETTINGS::metaBufMsgCapacity * 2;
    }
}

void META_BUFFER::CheckACK() {
    int subflowNo = 0;
    for (int i = 0; i < MAX_PRIORITY; i++) {
        SUBFLOW_MSG * iter = pMetaMsgHead[i];
        if (iter == NULL) continue;
        if (iter != pMetaMsgTail[i]) {
            if (iter->schedDecision > 0) {
                if (iter->transportSeq[iter->schedDecision] + iter->bytesOnWire <= subflowOutput.transportACK[iter->schedDecision] && iter->bSubflowAcked == 0) {
                    InfoMessage("Error ACK:");
                    iter->Print();
                }
            }
            if (iter->oldDecision > 0) {
                subflowNo = iter->oldDecision;
                if (iter->transportSeq[subflowNo] + iter->bytesOnWire <= subflowOutput.transportACK[subflowNo] && iter->bSubflowAcked == 0) {
                    InfoMessage("Error ACK:");
                    iter->Print();
                }
            }
        }
        IncrementMsgPointer(iter, i);
    }
}

int META_BUFFER::hasPartial() {
    for (int i = 0; i < MAX_SUBFLOWS; i++) {
        if (bPartial[i] > 0) return 1;
    }
    return 0;
}

void META_BUFFER::IncrementDataPointer(BYTE * & p, int delta, int prio) {
	p += delta;
	if (p >= pMetaDataEnd[prio]) p -= PROXY_SETTINGS::metaBufDataCapacity;
}

BYTE * META_BUFFER::GetIncrementDataPointer(BYTE * p, int delta, int prio) {
	BYTE * r = p + delta;
	if (r >= pMetaDataEnd[prio]) r -= PROXY_SETTINGS::metaBufDataCapacity;
        return r;
}

void META_BUFFER::IncrementMsgPointer(SUBFLOW_MSG * & p, int prio) {
	p++;
	if (p >= pMetaMsgEnd[prio]) p -= PROXY_SETTINGS::metaBufMsgCapacity;
}

void META_BUFFER::DecrementMsgPointer(SUBFLOW_MSG * & p, int prio) {
	p--;
	if (p < pMetaMsgBuffer[prio]) p += PROXY_SETTINGS::metaBufMsgCapacity;
}

SUBFLOW_MSG * META_BUFFER::GetDecrementMsgPointer(SUBFLOW_MSG * p, int prio) {
	SUBFLOW_MSG * r = p - 1;
	if (r < pMetaMsgBuffer[prio]) r += PROXY_SETTINGS::metaBufMsgCapacity;
	return r;
}

int META_BUFFER::ConvertMsgPointerToIndex(SUBFLOW_MSG * p) {
	SUBFLOW_MSG * head = pMetaMsgHead[p->priority];
	if (p >= head) {
		//InfoMessage("p >= head: %u %u", p, head);
		return (p - head);
	}
	//InfoMessage("p < head: %u %u", p, head);
	return PROXY_SETTINGS::metaBufMsgCapacity - (head - p);
}

void META_BUFFER::UpdateTail(int prio) {
	if (pMetaDataHead[prio] == NULL) pMetaDataHead[prio] = pMetaDataTail[prio];
	metaDataSize[prio] += pMetaMsgTail[prio]->bytesOnWire;
	IncrementDataPointer(pMetaDataTail[prio], pMetaMsgTail[prio]->bytesOnWire, prio);

	if (pMetaMsgHead[prio] == NULL) pMetaMsgHead[prio] = pMetaMsgTail[prio];
	metaMsgSize[prio] += 1;
	IncrementMsgPointer(pMetaMsgTail[prio], prio);
}

void META_BUFFER::AddMsgToMapping(SUBFLOW_MSG * msg) {
    int subflowNo = msg->schedDecision;
    pMappingTail[subflowNo]->msg = msg;
    pMappingTail[subflowNo]->tcpSeq = msg->transportSeq[subflowNo];
    /*
    fprintf(ofsDebug, "map\t%d\t%u\t%u\t%u\t%d\t%d\t%u\t%d\n",
            subflowNo, msg->connID, msg->seq, msg->transportSeq[subflowNo],
            msg->bytesLeft[subflowNo],
            msg->oldDecision, msg->transportSeq[msg->oldDecision],
            msg->bytesLeft[msg->oldDecision]);
            */
    if (pMappingSize[subflowNo] >= PROXY_SETTINGS::metaBufMsgCapacity * 2) {
        InfoMessage("!!!!!!!!!!!!!!!!! pMapping Full: %d, %d msgs !!!!!!!!!!!!!!!!", subflowNo, pMappingSize[subflowNo]);
    }
    if (pMappingHead[subflowNo] == NULL) pMappingHead[subflowNo] = pMappingTail[subflowNo];
    IncrementMappingPointer(pMappingTail[subflowNo], subflowNo);
    pMappingSize[subflowNo] += 1;
}

void META_BUFFER::IncrementMappingPointer(SUBFLOW_MSG_MAPPING * & p, int subflowNo) {
    p++;
    if (p >= pMappingEnd[subflowNo]) p -= PROXY_SETTINGS::metaBufMsgCapacity * 2;
}
/*
void BUFFER_META::Increment(int prio) {
	if (pMetaDataHead[prio] == NULL) pMetaDataHead[prio] = pMetaDataTail[prio];
	metaMsgSize[prio] += 1;
}
*/
int META_BUFFER::GetPriority(int connID, int bControl) {
	// Control messages -> 0
	// Data messages according to the scheduler
	if (bControl) return 0;

	CONN_INFO ci = conns.connTab[connID];

	switch (ci.hints.sched) {
	case SUBFLOW_SELECTION_TWOWAY:
    case SUBFLOW_SELECTION_TWOWAY_NAIVE:
    case SUBFLOW_SELECTION_TWOWAY_BALANCE:
    case SUBFLOW_SELECTION_NEWTXDELAY:
    case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
	case SUBFLOW_SELECTION_MINRTT_KERNEL:
	case SUBFLOW_SELECTION_EMPTCP:
	case SUBFLOW_SELECTION_WIFIONLY:
	case SUBFLOW_SELECTION_ONEPATH:
	case SUBFLOW_SELECTION_ROUNDROBIN:
	case SUBFLOW_SELECTION_REMP:
	case SUBFLOW_SELECTION_PAMS:
		return scheduler.getSchedNormalQueue(ci.hints.sched);
	case SUBFLOW_SELECTION_DEFAULT:
		return scheduler.getSchedNormalQueue(scheduler.defaultScheduler);
	default:
		MyAssert(0, 8999);
	}
	return -1;
}

/*
int META_BUFFER::Enqueue(SUBFLOW_MSG * msg, int prio) {
	// Scheduling decision here
	return 0;
}

SUBFLOW_MSG * META_BUFFER::Dequeue(int subflowNo, int prio) {
	return NULL;
}
*/
void META_BUFFER::RescheduleAll() {
}

void META_BUFFER::PrintMetaBuffer(int count) {
	int size;
	for (int i = 0; i < MAX_PRIORITY; i++) {
		size = GetSize(i);
		if (size > 0) {
			printf("Queue %d (%d B): ", i, size);
			PrintMetaBuffer(i, count);
		}
	}
}

void META_BUFFER::PrintMetaBuffer(int prio, int count) {
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
	int i = 0;
	if (count < 0) count = 8000000;
	if (iter == NULL) return;
	printf("[Meta prio:%d] ", prio);
	while (iter != pMetaMsgTail[prio]) {
		if (i >= count) {
			printf("...\n");
			break;
		}
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			printf("[%d]sched=%d;old=%d;trans=%d(%d);acked=%d;conn=%u;seq=%u;tcpSeq=%u(%u) ",
				i, iter->schedDecision, iter->oldDecision, iter->bTransmitted[iter->schedDecision],
                iter->bTransmitted[iter->oldDecision], 
                iter->bSubflowAcked, iter->connID, iter->seq, iter->transportSeq[iter->schedDecision],
                iter->transportSeq[iter->oldDecision]);
		}
		IncrementMsgPointer(iter, prio);
		i++;
	}
	printf("\n");
}

int META_BUFFER::HasPackets() {
	return HasPackets(-1, -1);
}

int META_BUFFER::HasPackets(int start, int end) {
	if (start < 0) start = 0;
	if (end < 0) end = MAX_PRIORITY - 1;

	for (int i = start; i <= end; i++) {
		if (metaMsgSize[i] > 0) {
			// InfoMessage("Queue %d Message size %d", i, metaMsgSize[i]);
			return 1;
		}
	}
	return 0;
}

SUBFLOW_MSG * META_BUFFER::GetMsgByIndex(int prio, int index) {
	if (index >= metaMsgSize[prio]) return NULL;
	SUBFLOW_MSG * r = pMetaMsgHead[prio];
	r += index;
	if (r >= pMetaMsgEnd[prio]) r -= PROXY_SETTINGS::metaBufMsgCapacity;
	return r;
}

int META_BUFFER::ComputeMsgDataSize(int prio, int start, int end) {
	SUBFLOW_MSG * iter = GetMsgByIndex(prio, start), * iter_end = GetMsgByIndex(prio, end);
	int size = 0;
	while (iter != iter_end) {
		if (iter->isTransmitted() == 0)
			size += iter->bytesOnWire;
        //else if (iter->bSubflowAcked == 0) {
        //    subflowOutput.UpdateACKStatus(iter);
        //}
		IncrementMsgPointer(iter, prio);
	}
	return size;
}

// Sometimes buggy
SUBFLOW_MSG * META_BUFFER::GetNextUntransmittedMsg(int schedDecision, int queueNo) {
	return GetNextUntransmittedMsg(schedDecision, queueNo, queueNo);
}

SUBFLOW_MSG * META_BUFFER::GetNextUntransmittedMsg(int schedDecision, int startQueue, int endQueue) {
	for (int i = startQueue; i <= endQueue; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		int cnt = 0;
		// InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			// InfoMessage("iter cnt %d", cnt);
			cnt += 1;
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
				&& ((iter->isTransmitted() == 0 && iter->schedDecision == 0) || (iter->isTransmitted(schedDecision) == 0 && metaBuffer.isRecallOn == 1 && iter->bRecalled == 1))) 
			{
				if (metaBuffer.isRecallOn) {
					iter->oldDecision = iter->schedDecision;
					iter->bRecalled = 0;
				}
				iter->schedDecision = schedDecision;
                if (iter->bytesLeft[schedDecision] == 0) {
                    iter->bytesLeft[schedDecision] = iter->bytesOnWire;
                }
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	// InfoMessage("GetNextMsg is NULL");
	return NULL;
}

SUBFLOW_MSG * META_BUFFER::GetNextRempMsg(int schedDecision, int startQueue, int endQueue) {
	for (int i = startQueue; i <= endQueue; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
				&& iter->isTransmitted(schedDecision) == 0) {
				if (iter->schedDecision == 0) {
					iter->schedDecision = schedDecision;
				} else {
					iter->oldDecision = iter->schedDecision;
					iter->schedDecision = schedDecision;
				}
                if (iter->bytesLeft[schedDecision] == 0) {
                    iter->bytesLeft[schedDecision] = iter->bytesOnWire;
                }
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}

SUBFLOW_MSG * META_BUFFER::GetNextMPBondMsg(int schedDecision, int startQueue, int endQueue) {
	double est[subflows.n + 1];
	int buf[subflows.n + 1];
	int rtt[subflows.n + 1];
	int index[subflows.n + 1];
	double temp;
	double order[subflows.n + 1]; // after sorting, order[] becomes the sorted version of est[]
	int map[subflows.n + 1]; // map the index of order[] to the index of est[] with the same value
	int tmp;

	memset(index, 0, sizeof(index));
	for (int i = startQueue; i <= endQueue; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			// InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
				&& ((iter->isTransmitted() == 0 && iter->schedDecision == 0) || (iter->isTransmitted(schedDecision) == 0 && metaBuffer.isRecallOn == 1 && iter->bRecalled == 1)))
			{

				for (int i = 1; i < subflows.n + 1; i++) {
					buf[i] = kernelInfo.GetSndBuffer(i);
					rtt[i] = kernelInfo.GetSRTT(i);
					if (i == 1) {
						// est[i] = ((double) buf[i] + 1300 * index[i]) * 8 / (5000 + 1);
						est[i] = ((double) buf[i] + 1300 * index[i]) * 8 / (kernelInfo.GetBW(i) / 1000.0 + 1); /*+ ((double) rtt[i]) / 1000.0 / 2*/
					} else {
						est[i] = ((double) buf[i] + kernelInfo.bytesInPipe[i]) * 8 / (kernelInfo.pipeBW[i] + 1) 
									+ 1300 * index[i] / (kernelInfo.pipeBW[i] + 1);
					}
					order[i] = est[i];
					map[i] = i;
				}

				// sort est[1, n]: order the maximum est[j] in est[1, n - i] to its last pos est[i] in every round i 
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

				// select the path with shortest delay
				if (map[1] == schedDecision) {
					if (metaBuffer.isRecallOn) {
						iter->oldDecision = iter->schedDecision;
						iter->bRecalled = 0;
					}
					iter->schedDecision = schedDecision;
	                if (iter->bytesLeft[schedDecision] == 0) {
	                    iter->bytesLeft[schedDecision] = iter->bytesOnWire;
	                }
					return iter;
				}
				else {
					index[map[1]] += 1;
				}

			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}

/*
SUBFLOW_MSG * META_BUFFER::GetNextUntransmittedMsg(int schedDecision) {
	for (int i = 0; i < MAX_PRIORITY; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
				&& iter->bTransmitted == 0) {
				iter->schedDecision = schedDecision;
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}*/
//subflowOutput.UpdateACKStatus(iter);
SUBFLOW_MSG * META_BUFFER::GetNextMsgNew(int schedDecision) {
	for (int i = 1; i < MAX_PRIORITY; i++) {
		SUBFLOW_MSG * iter = pMetaMsgHead[i];
		//InfoMessage("GetNextMsg: i: %d", i);
		if (iter == NULL) continue;
		while (iter != pMetaMsgTail[i]) {
			//InfoMessage("head %d end %d: %d", pMetaMsgHead[i], pMetaMsgEnd[i], iter);
			if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
				iter->schedDecision = schedDecision;
				return iter;
			}
			IncrementMsgPointer(iter, i);
		}
	}
	return NULL;
}

// return 1: can transmit
// return 0: cannot transmit
int META_BUFFER::CanTransmit(SUBFLOW_MSG * msg, int otherSubflowNo, int direction) {
    SUBFLOW_MSG * iter = msg;
    int space = kernelInfo.GetTCPAvailableSpace(otherSubflowNo);
    int prio = msg->priority, size = 0;
    // direction 0: head->tail, 1: tail->head
    if (direction == 0) {
        while (iter != pMetaMsgTail[prio]) {
            if (iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            } else {
                break;
            }
            if (size > space) return 1;
            IncrementMsgPointer(iter, prio);
        }
    } else {
        while (iter != GetDecrementMsgPointer(pMetaMsgHead[prio], prio)) {
            if (iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            } else {
                break;
            }
            if (size > space) return 1;
            DecrementMsgPointer(iter, prio);
        }
    }

    if (size > space) return 1; 
    return 0;
}

// For TwoWay
// return 1: can transmit
// return 0: cannot transmit
int META_BUFFER::CanLargePathTransmit(int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
            int otherSubflowNo, int direction, uint64_t currt) {
    
	if (msg->bTransmitted[otherSubflowNo] > 0) {
		InfoMessage("CanLargePathTransmit msg->bTransmitted[otherSubflowNo] > 0 return 0");
		return 0;
	}

	SUBFLOW_MSG * iter = msg;
	double owd = 0.0;
	int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
	if (saveBytes == 2) {
		owd = kernelInfo.GetOWD(otherSubflowNo, bif1, bif2);
	}
	else if (saveBytes == 1) {
		owd = kernelInfo.GetOWD(otherSubflowNo, bif1, bif2);
		owd -= kernelInfo.GetOWDVar();
	}

	if (owd < 0.0) owd = 0.0;
    int space = kernelInfo.GetTCPAvailableSpace(otherSubflowNo) + (int)(
    	kernelInfo.GetBW(otherSubflowNo) * owd / 8000.0);

    int prio = msg->priority, size = 0;

    if (direction == 0) {
        while (iter != pMetaMsgTail[prio]) {
            if (iter->bTransmitted[schedDecision] == 0
                && iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            }/* else {
                break;
            }*/
            if (size > space - size)
            	return 1;

            IncrementMsgPointer(iter, prio);
        }
    } else {
        while (iter != GetDecrementMsgPointer(pMetaMsgHead[prio], prio)) {
            if (iter->bTransmitted[schedDecision] == 0
                && iter->bTransmitted[otherSubflowNo] == 0) {
                size += iter->bytesOnWire;
            }/* else {
                break;
            }*/
            if (size > space)
            	return 1;

            DecrementMsgPointer(iter, prio);
        }
    }

    if (size > space)
    	return 1; 

    return 0;
}

int META_BUFFER::CanSmallPathTransmit(
	int saveBytes, SUBFLOW_MSG * msg, int schedDecision, int otherSubflowNo, uint64_t currt) {
	if (msg->bTransmitted[otherSubflowNo] == 0) return 1;

	double delta = (double)(currt - msg->txTime[otherSubflowNo]) / 1000.0;
	// reinject
	if (saveBytes == 1) {
		int bif1 = kernelInfo.GetInFlightSize(1), bif2 = kernelInfo.GetInFlightSize(2);
		double owd = kernelInfo.GetOWD(schedDecision, bif1, bif2);
		owd += kernelInfo.GetOWDVar();

		if (delta <= owd) return 1;
		return 0;
	}

	// only balance
	if (saveBytes == 2) {
		InfoMessage("CanSmallPathTransmit DEMS_Balance return 0");
		//if (delta <= kernelInfo.GetOWD()) return 1;
		return 0;
	}

	MyAssert(0, 9031);
	return 1;
}

// return 1: can transmit
// return 0: cannot transmit
int META_BUFFER::CheckDelay(SUBFLOW_MSG* msg, int schedDecision, int other, uint64_t currt) {
    MyAssert(msg->txTime[other] > 0, 3848);
    int srtt1 = kernelInfo.GetSRTT(schedDecision);
    int srtt2 = kernelInfo.GetSRTT(other);
    int delta = (int64_t)currt - (int64_t)msg->txTime[other];
    //InfoMessage("srtt: %d %d, %d", srtt1, srtt2, delta);
    if (delta > srtt2 - srtt1) {
        return 0;
    } else {
        return 1;
    }
}

int META_BUFFER::BalanceTransfer(
	int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
	int otherSubflowNo, int isSmallOWD, int direction, uint64_t currt) {

	// saveBytes 0: no balance consider
	if (saveBytes == 0) {
		return 1;
	}
	// saveBytes 1/2: balance transfer
	if (saveBytes == 1 || saveBytes == 2) {
		if (saveBytes == 1) {
            /*
			if (schedDecision == 1) {
				return 1;
			}*/
		}
		if (isSmallOWD == 1) {
			return CanSmallPathTransmit(saveBytes, msg, schedDecision, otherSubflowNo, currt);
		} else {
			// return 1;
			return CanLargePathTransmit(saveBytes, msg, schedDecision, otherSubflowNo, direction, currt);
		}
	}
	MyAssert(0, 9032);
	return 1;
}


// return 1: can transmit
// return 0: cannot transmit
// direction: 0: GetFirstUnackedMsg, 1: GetLastUnackedMsg
int META_BUFFER::ControlRedundantTransfer(
	int saveBytes, SUBFLOW_MSG * msg, int schedDecision,
	int otherSubflowNo, int isSmallOWD, int direction, uint64_t currt) {

	// saveBytes 0: reinject any bytes unacknowledged
	if (saveBytes == 0) {
		return 1;
	}
	// saveBytes 2: only balance bytes of subflows, no reinjection
	if (saveBytes == 2) {
		return 0;
	}
	
	// saveBytes 1: reinject bytes based on OWD
	if (saveBytes == 1) {
		// WiFi can anyway transmit
		if (schedDecision == 1) {
			return 1;
		}
		if (isSmallOWD == 1) {
			return CanSmallPathTransmit(saveBytes, msg, schedDecision, otherSubflowNo, currt);
		}
		return 0;
	}
	MyAssert(0, 9033);
	return 1;
}

SUBFLOW_MSG * META_BUFFER::GetFirstUnackedMsg(
	int prio, int schedDecision, int other, int isSmallOWD, int saveBytes, uint64_t currt) {
	//InfoMessage("TwoWay %d", saveBytes);
    //int other = (schedDecision == 1)? 2: 1;
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
    SUBFLOW_MSG * trans_msg = NULL;
    SUBFLOW_MSG * reinj_msg = NULL;
	if (iter == NULL) return NULL;
	while (iter != pMetaMsgTail[prio]) {
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			//if (iter->bSubflowAcked == 0) {
			//	subflowOutput.UpdateACKStatus(iter);
			//}
			if (iter->bTransmitted[schedDecision] > 0) {
				IncrementMsgPointer(iter, prio);
				continue;
			}
            if (iter->bSubflowAcked == 1 && iter->bytesLeft[schedDecision] == 0) {
                IncrementMsgPointer(iter, prio);
                continue;
            }
            // Transmit second time
            MyAssert(iter->bTransmitted[schedDecision] == 0, 9503);

            if (iter->schedDecision == 0) {
            	// new msg
            	if (BalanceTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 0, currt) > 0) {
            		//fprintf(ofsDebug, "Balance: allow\n");
            		trans_msg = iter;
            	} else {
            		//fprintf(ofsDebug, "Balance: do not allow\n");
            	}
            	break;
            } else {
            	// reinject msg
            	// meta-buffer not full: do reinject
            	if (metaBuffer.metaDataSize[prio] < PROXY_SETTINGS::metaBufDataCapacity - 16000) {
	            	if (reinj_msg == NULL) {
	            		if (ControlRedundantTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 0, currt) > 0) {
	            			reinj_msg = iter;
	            			//fprintf(ofsDebug, "Reinject: allow\n");
	            		} else {
	            			//fprintf(ofsDebug, "Reinject: do not allow\n");
	            		}
	            		//break;
	            		//InfoMessage("reinject option %p %dB iter=%p ******************",
	            		//	reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            	} else {
	            		if (reinj_msg->txTime[other] < iter->txTime[other]) {
	            			reinj_msg = iter;
	            			//InfoMessage("reinject update %p %dB iter=%p ******************",
	            			//	reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            		}
	            	}
	            }
            }

		}
		IncrementMsgPointer(iter, prio);
	}

	if (trans_msg == NULL) {
		trans_msg = reinj_msg;
		//InfoMessage("reinject %p %p %dB ******************",
		//	trans_msg, reinj_msg, metaBuffer.metaDataSize[prio]);
	} else {
		//InfoMessage("*********");
	}

	if (trans_msg != NULL) {
		if (trans_msg->schedDecision != schedDecision) {
            if (trans_msg->oldDecision == 0) {
                trans_msg->oldDecision = trans_msg->schedDecision;
                trans_msg->schedDecision = schedDecision;
                trans_msg->bytesLeft[schedDecision] = trans_msg->bytesOnWire;
            } else {
                if (trans_msg->oldDecision == schedDecision) {
                    trans_msg->oldDecision = trans_msg->schedDecision;
                    trans_msg->schedDecision = schedDecision;
                } else {
                    MyAssert(0, 9504);
                }
            }
            //return trans_msg;
        } else {
            //return trans_msg;
        }
    }
	return trans_msg;
}

SUBFLOW_MSG * META_BUFFER::GetLastUnackedMsg(
	int prio, int schedDecision, int other, int isSmallOWD, int saveBytes, uint64_t currt) {
	//InfoMessage("TwoWay %d", saveBytes);
	if (pMetaMsgHead[prio] == NULL) return NULL;
	SUBFLOW_MSG * iter = GetDecrementMsgPointer(pMetaMsgTail[prio], prio);
    SUBFLOW_MSG * trans_msg = NULL;
    SUBFLOW_MSG * reinj_msg = NULL;
	while (iter != GetDecrementMsgPointer(pMetaMsgHead[prio], prio)) {
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			//if (iter->bSubflowAcked == 0) {
			//	subflowOutput.UpdateACKStatus(iter);
			//}
			if (iter->bTransmitted[schedDecision] > 0) {
				DecrementMsgPointer(iter, prio);
				continue;
			}
            if (iter->bSubflowAcked == 1 && iter->bytesLeft[schedDecision] == 0) {
                DecrementMsgPointer(iter, prio);
                continue;
            }
// Transmit second time
            
            MyAssert(iter->bTransmitted[schedDecision] == 0, 9506);
            /*
            if (saveBytes > 0) {
                if (isSmallRTT == 0 && CanTransmit(iter, other, 1) == 0) {
                    return NULL;
                }
                if (isSmallRTT == 1 && iter->bTransmitted[other] > 0 && CheckDelay(iter, schedDecision, other, currt) == 0) {
                    return NULL;
                }
            }
            */
            if (iter->schedDecision == 0) {
            	// new msg
            	if (BalanceTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 1, currt) > 0) {
            		//fprintf(ofsDebug, "Balance: allow\n");
            		trans_msg = iter;
            	} else {
            		//fprintf(ofsDebug, "Balance: do not allow\n");
            	}
            	break;
            } else {
            	// reinject msg
            	// meta-buffer not full: do reinject
            	if (metaBuffer.metaDataSize[prio] < PROXY_SETTINGS::metaBufDataCapacity - 16000) {
	            	if (reinj_msg == NULL) {
	            		if (ControlRedundantTransfer(saveBytes, iter, schedDecision, other, isSmallOWD, 1, currt) > 0) {
	            			reinj_msg = iter;
	            			//fprintf(ofsDebug, "Reinject: allow\n");
	            		} else {
	            			//fprintf(ofsDebug, "Reinject: do not allow\n");
	            		}
	            		//break;
	            		//InfoMessage("reinject option %p %dB iter=%p ******************",
	            		//	reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            	} else {
	            		if (reinj_msg->txTime[other] < iter->txTime[other]) {
	            			reinj_msg = iter;
	            		//	InfoMessage("reinject update %p %dB iter=%p ******************",
	            		//		reinj_msg, metaBuffer.metaDataSize[prio], iter);
	            		}
	            	}
	            }
            }
		}
		DecrementMsgPointer(iter, prio);
	}
    
    if (trans_msg == NULL) {
		trans_msg = reinj_msg;
		//InfoMessage("reinject %p %p %dB ******************",
		//	trans_msg, reinj_msg, metaBuffer.metaDataSize[prio]);
	} else {
		//InfoMessage("*********");
	}

    if (trans_msg != NULL) {
		if (trans_msg->schedDecision != schedDecision) {
            if (trans_msg->oldDecision == 0) {
                trans_msg->oldDecision = trans_msg->schedDecision;
                trans_msg->schedDecision = schedDecision;
                trans_msg->bytesLeft[schedDecision] = trans_msg->bytesOnWire;
            } else {
                if (trans_msg->oldDecision == schedDecision) {
                    trans_msg->oldDecision = trans_msg->schedDecision;
                    trans_msg->schedDecision = schedDecision;
                } else {
                    MyAssert(0, 9505);
                }
            }
            //return trans_msg;
        } else {
            //return trans_msg;
        }
    }
    return trans_msg;
}

// Assume use by TxDelay+
SUBFLOW_MSG * META_BUFFER::GetMsgAfterSize(int prio, int schedDecision, int size) {
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
	SUBFLOW_MSG * msg = NULL;
	if (iter == NULL) return NULL;

	int data = 0, after = 0;
	while (iter != pMetaMsgTail[prio]) {
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY
			&& iter->isTransmitted() == 0) {
			if (iter->schedDecision == 0) {
				data += iter->bytesOnWire;
				msg = iter;
				after = 0;
			} else {
				if (iter->schedDecision != schedDecision) {
					data += iter->bytesLeft[iter->schedDecision];
					after += iter->bytesLeft[iter->schedDecision];
				}
			}
			if (data > size) {
				if (iter->schedDecision == 0) {
					iter->schedDecision = schedDecision;
					iter->bytesLeft[schedDecision] = iter->bytesOnWire;
					untransSize[prio] = -1;
					return iter;
				}
			}
		}
		IncrementMsgPointer(iter, prio);
	}
	untransSize[prio] = data - after;
	return msg;
}

unsigned int META_BUFFER::GetSize(int prio) {
	int size = 0;
	if (prio < MAX_PRIORITY) {
		return metaDataSize[prio];
	}
	
	for (int i = 0; i < MAX_PRIORITY; i++) {
		size += metaDataSize[i];
	}

	return size;
}

int META_BUFFER::TransmitFromMetaBuffer(SUBFLOW_MSG * msg) {
	// Transfer already encoded subflow message on the subflow.

	int subflowNo = msg->schedDecision;
	int prio = msg->priority;
	int toFD = conns.peers[subflowNo].fd;

	int nBytesWritten = 0;
	int dBufSize = metaBuffer.metaDataSize[prio];

	// w1: bytes to the end of circular buffer
	int w1, w2;
	w1 = msg->bytesLeft[subflowNo];
	// consider circular buffer
	w2 = msg->pMsgData[subflowNo] + w1 - metaBuffer.pMetaDataEnd[prio];
	if (w2 > 0) w1 -= w2; else w2 = 0;
	int w = 0;
	w = write(toFD, msg->pMsgData[subflowNo], w1);
	// InfoMessage("subflowNo=%d, toFD=%d, dBufSize=%d, w1=%d, w2=%d, w=%d", subflowNo, toFD, dBufSize, w1, w2, w);

	if (w > 0) {
		lastSubflowActivityTick = tickCount;
		nBytesWritten += w;
		conns.connTab[msg->connID].bytesInSubflowBuffer -= w;
	}
		
	if (w == w1 && w2 == 0) {	//when late binding is used, this is the only non-error path				
		msg->bytesLeft[subflowNo] = 0;
		dBufSize -= w;

		VerboseMessage("Write %d bytes from TCP %s to subflow %d. Full subflow message written (seq=%u)",
			w, conns.DumpConnection(msg->connID), subflowNo, msg->seq);

		goto NEXT_MSG;
	} else if (w == w1 && w2 > 0) {
		// A case where the message encounters the circular wrap
		msg->pMsgData[subflowNo] = metaBuffer.pMetaDataBuffer[prio]; 
		msg->bytesLeft[subflowNo] -= w;
		dBufSize -= w; 

		VerboseMessage("Write %d bytes from TCP %s to subflow %d.",
			w, conns.DumpConnection(msg->connID), subflowNo);

		w = write(toFD, metaBuffer.pMetaDataBuffer[prio], w2);

		if (w > 0) {
			lastSubflowActivityTick = tickCount;
			nBytesWritten += w;
		}

		if (w == w2) {
			dBufSize -= w;
			msg->bytesLeft[subflowNo] = 0;
			VerboseMessage("Write %d bytes from TCP %s to subflow %d. Full subflow message written (seq=%u)",
				w, conns.DumpConnection(msg->connID), subflowNo, msg->seq);

			goto NEXT_MSG;
		} else if (w >= 0 && w < w2) {
			msg->pMsgData[subflowNo] += w;
			msg->bytesLeft[subflowNo] -= w;
            
			dBufSize -= w;

			VerboseMessage("Write %d bytes from TCP %s to subflow %d.",
				w, conns.DumpConnection(msg->connID), subflowNo
			);

			goto FINISH;
		} else if (w < 0 && errno == EWOULDBLOCK) {
			//WarningMessage("Socket buffer full for subflow# %d", subflowNo);
			goto FINISH;
		} else {
			ErrorMessage("Error writing to subflow: %s(%d) Subflow #%d", 
				strerror(errno), errno, (int)subflowNo					
			);
			MyAssert(0, 1650);
		}

	} else if (w >= 0 && w < w1) {
			msg->pMsgData[subflowNo] += w;
			msg->bytesLeft[subflowNo] -= w;
			dBufSize -= w;//??

			if (w == 0) InfoMessage("No bytes written to subflow %d", subflowNo);
			VerboseMessage("Write %d bytes from TCP %s to subflow %d.",
				w, conns.DumpConnection(msg->connID), subflowNo);

			goto FINISH;
	} else if (w < 0 && errno == EWOULDBLOCK) {	

			goto FINISH;
	} else {
			ErrorMessage("Error writing to subflow #%d: %s(%d)", 
				(int)subflowNo, strerror(errno), errno
			);
			MyAssert(0, 1649);
	}

	NEXT_MSG:

	FINISH:

	// InfoMessage("Finishing META_BUFFER::TransmitFromMetaBuffer()");

	if (msg->bytesLeft[subflowNo] == 0) {
		msg->bTransmitted[subflowNo] = 1;
        bPartial[subflowNo] = 0;
        pPartial[subflowNo] = NULL;
        if (subflowNo <= 4) {
        	conns.GetTCPSeqForUna(msg, nBytesWritten);
        }
	} else {
        pPartial[subflowNo] = msg;
        bPartial[subflowNo] = 1;
        if (subflowNo <= 4) {
        	conns.GetTCPSeqForUna(msg, nBytesWritten);
        }
    }

    // InfoMessage("Returning META_BUFFER::TransmitFromMetaBuffer()");

	return nBytesWritten;
}

void META_BUFFER::UpdateAfterTransmit(int prio) {
	SUBFLOW_MSG * & iter = pMetaMsgHead[prio];
    //SUBFLOW_MSG * iter = pMetaMsgHead[prio];
	if (iter == NULL) return;
	while (iter != pMetaMsgTail[prio]) {
		//InfoMessage("head %d end %d: %d", pMetaMsgHead[prio], pMetaMsgEnd[prio], iter);
		if (iter->msgType != SUBFLOW_MSG::MSG_EMPTY) {
			// Currently transmitted packets are removed from meta buffer
			// TODO: keep unACKed packets in meta buffer

			// InfoMessage("msg seq=%d", iter->seq);

			if (iter->isTransmitted()) { 

				if (iter->bSubflowAcked) {

					// for MPBond
					// if (iter->bPrimaryAcked) {
						//MyAssert(iter->bTransmitted, 9566);
						MyAssert(pMetaDataHead[prio] == iter->pMsgStart, 9565);
	                    //if (iter == pMetaMsgHead[prio]) {
						metaDataSize[prio] -= iter->bytesOnWire;
						metaMsgSize[prio] -= 1;
						IncrementDataPointer(pMetaDataHead[prio], iter->bytesOnWire, prio);
	                    //}
						IncrementMsgPointer(iter, prio);
	                    //if (iter == pMetaMsgHead[prio])
	                    //    IncrementMsgPointer(pMetaMsgHead[prio], prio);
					// }
					
				} else {
                    //IncrementMsgPointer(iter, prio);
					break;
				}
			} else {
                //IncrementMsgPointer(iter, prio);
				break;
			}
		} else {
            //IncrementMsgPointer(iter, prio);
			break;
		}
	}
	if (pMetaMsgHead[prio] == pMetaMsgTail[prio]) {
		pMetaMsgHead[prio] = NULL;
		pMetaDataHead[prio] = NULL;
		MyAssert(metaMsgSize[prio] == 0 && metaDataSize[prio] == 0, 9200);

	}
}

void META_BUFFER::UpdateAfterTransmit() {
	//InfoMessage("Remove ACKed packets");
	for (int i = 0; i < MAX_PRIORITY; i++) {
		UpdateAfterTransmit(i);
	}
}


void META_BUFFER::MarkACKedMsg(int subflowNo) {
    SUBFLOW_MSG_MAPPING * & iter = pMappingHead[subflowNo];
    if (iter == NULL) return;
    while (iter != pMappingTail[subflowNo]) {


        //MyAssert(iter->msg->transportSeq[subflowNo] > 0, 6769);
        if (iter->tcpSeq != iter->msg->transportSeq[subflowNo]) {
            IncrementMappingPointer(iter, subflowNo);
            pMappingSize[subflowNo] -= 1;
            continue;
        }
        int flag = 0;
        if (iter->msg->bTransmitted[subflowNo] > 0) {
            flag = subflowOutput.UpdateSubflowACKStatus(iter->msg, subflowNo);
        }
        if (flag == 1) {
            IncrementMappingPointer(iter, subflowNo);
            pMappingSize[subflowNo] -= 1;
        } else {
            break;
        }
    }
    if (iter == pMappingTail[subflowNo]) {
        iter = NULL;
        pMappingTail[subflowNo] = pMappingBuffer[subflowNo];
    }
}

void META_BUFFER::MarkACKedMsg() {
    for (int i = 0; i < 6; i++) {
        MarkACKedMsg(i);
    }
}

int META_BUFFER::GetUntransmittedMsgNumber(int prio) {
	int r = 0;
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
    if (iter == NULL) return r;
    while (iter != pMetaMsgTail[prio]) {

    	if (iter->isTransmitted()) { 

    	}
    	else {
    		r += 1;
    	}
    	IncrementMsgPointer(iter, prio);
    }
    return r;
}

int META_BUFFER::GetUntransmittedMsgNumber() {
	//InfoMessage("Remove ACKed packets");
	int r = 0;
	for (int i = 0; i < MAX_PRIORITY; i++) {
		r += GetUntransmittedMsgNumber(i);
		// InfoMessage("Getting number %d", r);
	}
	return r;
}

int META_BUFFER::GetRecallingMsgNumber(int prio) {
	int r = 0;
	SUBFLOW_MSG * iter = pMetaMsgHead[prio];
    if (iter == NULL) return r;
    while (iter != pMetaMsgTail[prio]) {
    	if (iter->bRecalled == 1) {
    		r += 1;
    	}
    	IncrementMsgPointer(iter, prio);
    }
    return r;
}

int META_BUFFER::GetRecallingMsgNumber() {
	int r = 0;
	for (int i = 0; i < MAX_PRIORITY; i++) {
		r += GetRecallingMsgNumber(i);
	}
	return r;
}

void META_BUFFER::UpdateAfterRecall(int prio, int r) {
	int i = 0;
	SUBFLOW_MSG * iter = pMetaMsgTail[prio];
    if (iter == NULL) return;
    while ((iter != pMetaMsgHead[prio]) && (i < r)) {
    	iter->bRecalled = 1;
    	i++;
    	DecrementMsgPointer(iter, prio);
    }
}
