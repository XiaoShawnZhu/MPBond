#include "stdafx.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "proxy.h"
#include "tools.h"
#include "subflows.h"
#include "connections.h"
#include "meta_buffer.h"
#include "kernel_info.h"
#include "scheduler.h"

struct SUBFLOWS subflows;
struct CONNECTIONS conns;
struct BUFFER_SUBFLOWS subflowOutput;	//the buffer for writing to subflows
struct BUFFER_TCP tcpOutput;	//the buffer for writing to tcp connections
struct META_BUFFER metaBuffer; // meta buffer for multipath sender side above multiplexed connections
struct DELAYED_FINS delayedFins;	//delayed fin
struct KERNEL_INFO kernelInfo;
struct SCHEDULER scheduler;

int kfd;
int proxyMode;
int tickCount;
int lastSubflowActivityTick;
int bLateBindingMPBond = 0;
int subflowInUseDMM = 2;
int reinjPAMS = 1;
int nSubHelper = 1;
int accBytes[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

FILE * ofsSubflowDump = NULL;
FILE * ofsIODump = NULL;
FILE * ofsAccDump = NULL;
FILE * ofsLatency = NULL;
FILE * ofsDebug = NULL;
FILE * ofsOWD = NULL;

char * debugFilename = "debug.txt";
const char * prefix = "/home/shawnzhu/dmm-proj/MPBond/server/log/";
char filename1[64];
char filename2[64];
char filename3[64];
unsigned long highResTimestampBase;
int keepRunning = 1;

int notEmpty = 0;

void intHandler(int dummy) {
	keepRunning = 0;
}

void ProxyMain() {
	int pollTimeout = PROXY_SETTINGS::pollTimeout;
	strcpy(filename1, prefix);
	strcat(filename1, "latency.txt");
	ofsLatency = fopen(filename1, "w");
	if (ofsLatency == NULL) {
            printf("Error opening file!\n");
            exit(1);
	}
	strcpy(filename2, prefix);
	strcat(filename2, debugFilename);
    strcat(filename2, "_debug.txt");
	ofsDebug = fopen(filename2, "w");
    if (ofsDebug == NULL) {
        printf("Error opening debug file!\n");
        exit(1);
    }

    strcpy(filename3, prefix);
	strcat(filename3, debugFilename);
    strcat(filename3, "_owd.txt");
	ofsOWD = fopen(filename3, "w");
    if (ofsOWD == NULL) {
        printf("Error opening owd file!\n");
        exit(1);
    }
	
	signal(SIGINT, intHandler);

	int space[11];
	int decision, bytes, schedulerNo;
    int nTCP = PROXY_SETTINGS::nTCPSubflows;
    uint64_t startTx;

	while (keepRunning) {
		VerboseMessage("Polling");
        
		int nReady = poll(conns.peers, conns.maxIdx + 1, pollTimeout);
		MyAssert(nReady >= 0, 1699);

		for (int i = 0; i < 2; i++) {
			if (conns.peers[i].revents & POLLRDHUP) {
				InfoMessage("Subflow closed.");
				MyAssert(0, 1698);
			}
		}

		if (nReady > 0) {
			//InfoMessage("****** Read data from subflows or connections.");
			//any new incoming connection (peers[0], for local proxy only)?
			MyAssert(conns.maxIdx >= subflows.n, 1620);

			//any new data from SUBFLOWS (peers[1..nSubflows]) and/or TCP PEERS (peers[nSubflows+1..maxIdx])?		
			for (int i=1; i<=conns.maxIdx; i++) {
				int peerFD = conns.peers[i].fd;
				if (peerFD < 0) continue;

				int bSubflow = i<=subflows.n;
				int bMarked = 0;
				
				// New connections
				if ((conns.peersExt[i].establishStatus == POLLFD_EXT::EST_NOT_CONNECTED) && 
					(conns.peers[i].revents & (POLLRDNORM | POLLWRNORM))
				) {
					MyAssert((proxyMode == PROXY_MODE_REMOTE) && (!bSubflow), 1700);

					bMarked = 1;
					//a new connection established (remote proxy mode only)
					int err;
					socklen_t len = sizeof(err);
					if (getsockopt(peerFD, SOL_SOCKET, SO_ERROR, &err, &len) < 0) {
						conns.ConnectToRemoteServerDone(i, CONNECTIONS::ESTABLISH_ERROR);
					} else if (err) {
						conns.ConnectToRemoteServerDone(i, CONNECTIONS::ESTABLISH_ERROR);
					} else {
						conns.ConnectToRemoteServerDone(i, CONNECTIONS::ESTABLISH_SUCC);
					}
					goto SLOT_FINISHED;
				}

				// There is data to read from the subflow or the connection
				if ((conns.peers[i].revents & (POLLRDNORM | POLLERR | POLLHUP)) || 
					(/*!bSubflow &&*/ !conns.peersExt[i].bSentSYNToSubflow) ||
					(/*!bSubflow &&*/  conns.peersExt[i].establishStatus == POLLFD_EXT::EST_FAIL) ||
					(/*!bSubflow &&*/  conns.peersExt[i].bToSendSYNACK)
				) {				
					//conditions of !bSentSYNToSubflow, establish failure, and to-send-SYNACK are triggered by 
					//last time the when the corresponding subflow msg was not sent due to buffer full
									
					bMarked = 1;
					MyAssert(conns.peersExt[i].establishStatus != POLLFD_EXT::EST_NOT_CONNECTED, 1704);
					// There is data from Subflow
					if (bSubflow) {
						if (conns.TransferFromSubflowsToTCP(i, peerFD) > 0) {
							conns.TransferDelayedFINsToSubflows();
						}
					}
					// There is data from TCP connection
					else {
						if (metaBuffer.GetSize(MAX_PRIORITY) > 100000) {
                            conns.TransferFromTCPToMetaBuffer(i, peerFD, 0);
                        } else {
                            startTx = get_current_microsecond();
                            while (conns.TransferFromTCPToMetaBuffer(i, peerFD, startTx) == 0) {
                               continue;
                            }
                        }
					}
				}
	
				// Writing is now possible
				if (conns.peers[i].revents & POLLWRNORM) {
					//if (subflowOutput.bActiveReinject[i])
					//	InfoMessage("Subflow %d writing possible, reinject: %d (%d messages)", i, subflowOutput.bActiveReinject[i], subflowOutput.msgBufSize[i]);
					bMarked = 1;
					MyAssert(conns.peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC, 1705); 

					if (bSubflow) {

					} else {
						tcpOutput.TransferFromSubflowsToTCP(i);				
					}
				}
				SLOT_FINISHED:	///////////////
				if (bMarked)
					if (--nReady <= 0) break;
			}
		}
		
		subflowOutput.UpdateACK();
        metaBuffer.MarkACKedMsg();
		metaBuffer.UpdateAfterTransmit();

		// Check meta buffer and subflow congestion window space
		// Do scheduling if there is available space
		if (metaBuffer.HasPackets()) {
			// InfoMessage("Has packets");
			// int numUnTXed = metaBuffer.GetUntransmittedMsgNumber();
			// InfoMessage("Number of untransmitted messages is %d", numUnTXed);
			if ((kernelInfo.updated == 0) && (metaBuffer.isRecallOn == 1)) {
				// recall: mark the last r bytes as untransmitted

				metaBuffer.UpdateAfterRecall(scheduler.getSchedLastQueue(SUBFLOW_SELECTION_PAMS), metaBuffer.recallBytes);
				// InfoMessage("Recall flag updated, recall %d bytes", metaBuffer.recallBytes);
				kernelInfo.updated = 1;
			}

			if ((metaBuffer.isRecallOn == 1) && (metaBuffer.GetRecallingMsgNumber() == 0)) {
				metaBuffer.isRecallOn = 0;
				kernelInfo.updated = 0;
			}

			notEmpty = 1;
			// InfoMessage("Meta buffer has bytes: %d", metaBuffer.GetSize(MAX_PRIORITY));
			kernelInfo.UpdateSubflowInfo(0);
			kernelInfo.UpdateTCPAvailableSpace();
			for (int i = 1; i <= nTCP; i++) {
				space[i] = kernelInfo.GetTCPAvailableSpace(i);
			}

			if (kernelInfo.HaveSpace() > 0) {
				// InfoMessage("Has space");
				// InfoMessage("untransmitted %d", metaBuffer.GetUntransmittedMsgNumber());
				// metaBuffer.PrintMetaBuffer(20);
                // Get the scheduling algorithm to send packet
                schedulerNo = scheduler.getNextSchedNo();
                SUBFLOW_MSG * msg = NULL;
                // InfoMessage("HaveSpace, sched=%d, hasPartial=%d", schedulerNo, metaBuffer.hasPartial());
                // check previously partial transmitted message
				// then check control messages
				// then data messages
                if (metaBuffer.hasPartial() == 0) {
			        msg = scheduler.SelectSubflow_ControlMsg();
                }
				if (msg == NULL) {
					msg = (scheduler.*scheduler.SelectSubflow[schedulerNo])();
				}
				if (msg != NULL) {
					
					decision = msg->schedDecision;
					// InfoMessage("There is a length %d msg with decision %d", msg->bytesLeft[decision], decision);
					MyAssert((decision >= 1) && (decision <= nTCP), 9210);
					//MyAssert(space[decision] > 0, 9211);

                    if (msg->bytesLeft[decision] == 0)
                        msg->Print();
                    MyAssert(msg->bytesLeft[decision] > 0, 9212);

					bytes = metaBuffer.TransmitFromMetaBuffer(msg);
					accBytes[decision] += bytes;
					unsigned long long seq = (unsigned long long)msg->transportSeq[decision];
					// InfoMessage("Pushed seq=%llu into decision=%d", seq, decision);
					// for (int i = 1; i < subflows.n + 1; i ++) {
					// 	InfoMessage("Subflow %d accBytes=%d", i, accBytes[i]);
					// }
					space[decision] -= bytes;
					kernelInfo.DecreaseTCPAvailableSpace(decision, bytes);
				} 
			}
		}
		else {
			if (notEmpty == 1) {
				// InfoMessage("Metabuffer has no packets left.");
				notEmpty = 0;
			}
		}
	}

	MyAssert(0, 1030);
}

void DumpProxyStatus() {
	fprintf(stderr, "\n");
	fprintf(stderr, "                                           P R O X Y    S T A T U S                                      \n");
	fprintf(stderr, "**********************************************************************************************************\n");
	fprintf(stderr, "SUBFLOWS: (%d)\n", subflows.n);
	fprintf(stderr, "dataBuf  msgBuf   readNotify  writeNotify\n");
	fprintf(stderr, "------------------------------------------\n");
	for (int i = 1; i <= subflows.n; i++) {
		fprintf(stderr, "%d  %d  %s  %s\n", 
			subflowOutput.dataBufSize[i], 
			subflowOutput.msgBufSize[i], 
			conns.peers[i].events & POLLRDNORM ? "YES" : "NO",
			conns.peers[i].events & POLLWRNORM ? "YES" : "NO"
			);
	}
	fprintf(stderr, "------------------------------------------\n");

	static const char * estStr[] = {"NOT_CONN", "SUCC", "FAIL"};

	fprintf(stderr, "TCP Connections: (maxIdx = %d)\n", conns.maxIdx);
	fprintf(stderr, "connID  status  pollPos  fd  buf  IP  rPort  lPort  R-Notify  W-Notify  sentSYN  estStatus  #msgLists  headers  pCurMsgs\n");
	fprintf(stderr, "----------------------------------------------------------------------------------------------------------\n");
	for (int i = 1; i <= 65535; i ++) {
		int s = conns.connTab[i].GetInUseStatus(tickCount);
		int pollPos = conns.connTab[i].pollPos;
		if (s == CONN_INFO::CONN_EMPTY) continue;
		fprintf(stderr, "%d  %s  %d  %d  %d  %s  %d  %d  %s  %s  %s  %s  %d  %d  %s\n", 
			i, 
			s==CONN_INFO::CONN_INUSE ? "INUSE" : "CLOSED", 
			pollPos, 
			conns.peers[pollPos].fd, 
			conns.connTab[i].bytesInTCPBuffer,
			ConvertDWORDToIP(conns.connTab[i].serverIP),
			conns.connTab[i].serverPort,
			conns.connTab[i].clientPort,
			conns.peers[pollPos].events & POLLRDNORM ? "YES" : "NO",
			conns.peers[pollPos].events & POLLWRNORM ? "YES" : "NO",
			conns.peersExt[pollPos].bSentSYNToSubflow ? "YES" : "NO",
			estStr[conns.peersExt[pollPos].establishStatus],
			tcpOutput.GetMsgListSize(pollPos),
			(int)tcpOutput.headers[pollPos][8],
			tcpOutput.pCurMsgs[pollPos] == NULL ? "NUL" : "NOT-NUL"
		);
	}
	fprintf(stderr, "----------------------------------------------------------------------------------------------------------\n");

	fprintf(stderr, "# Msgs to be abandoned after fully read (MAX_FDS):  %d\n", tcpOutput.GetMsgListSize(MAX_FDS));
	fprintf(stderr, "# Msgs to be included when SYN arrives (MAX_FDS+1): %d\n", tcpOutput.GetMsgListSize(MAX_FDS+1));

	fprintf(stderr, "TCP Data buffer: Capacity=%d  Size=%d  Tail=%d\n", PROXY_SETTINGS::tcpOverallBufDataCapacity, tcpOutput.dataCacheSize, tcpOutput.dataCacheEnd);
	fprintf(stderr, "TCP Msg  buffer: Capacity=%d  Size=%d  Tail=%d\n", PROXY_SETTINGS::tcpOverallBufMsgCapacity, tcpOutput.msgCacheSize, tcpOutput.msgCacheEnd);
	

	fprintf(stderr, "**********************************************************************************************************\n");
	fprintf(stderr, "\n");
}

pthread_mutex_t subflowDumpLock = PTHREAD_MUTEX_INITIALIZER;

void * TickCountThread(void * arg) {
	struct timeval tv;
	int r = gettimeofday(&tv, NULL);
	MyAssert(r == 0, 1725);
	time_t tBase = tv.tv_sec;
	int nTicks = 0;
	int g = TICK_COUNT_GRANULARITY_REMOTE_PROXY;
	int gDebug = 30;
	while (1) {		
		int r = gettimeofday(&tv, NULL);
		MyAssert(r == 0, 1726);
		tickCount = tv.tv_sec - tBase + 1 + PROXY_SETTINGS::connIDReuseTimeout;
		sleep(g);
		if (PROXY_SETTINGS::bDumpIO) {
			fflush(ofsIODump);
		}
		if (++nTicks % gDebug == 0) {
			#ifdef PERIODICALLY_DUMP_PROXY_STATUS
			DumpProxyStatus();
			#endif
		}
	}
	return NULL;
}

void StartTickCountThread() {
	tickCount = -1;
	pthread_t tick_count_thread;	
	int r = pthread_create(&tick_count_thread, NULL, TickCountThread, NULL);
	MyAssert(r == 0, 1724);
		
	while (tickCount == -1) {pthread_yield();}
	InfoMessage("Tick thread started.");
}

int main(int argc, char * * argv) {

	InfoMessage("MPBond Version: %s [USERLEVEL MULTIPATH]", MY_VERSION);
	srand((DWORD)time(NULL));

	struct timeval tv;
	gettimeofday(&tv, NULL);
	highResTimestampBase = tv.tv_sec * 1000000 + tv.tv_usec;

	int subflowSelectionPolicy = -1;
	int enhancePolicy = -1;

	if (argc == 4 || argc == 5 || argc == 6) {
		// Remote Proxy Mode
		InfoMessage("Remote Proxy Mode");
		proxyMode = PROXY_MODE_REMOTE;
		// Get the scheduler (subflow selection policy) to use
		subflowSelectionPolicy = atoi(argv[2]);
		enhancePolicy = atoi(argv[3]);

		switch (subflowSelectionPolicy) {
			case SUBFLOW_SELECTION_TWOWAY:
				break;
			case SUBFLOW_SELECTION_NEWTXDELAY:
				break;
			case SUBFLOW_SELECTION_TWOWAY_NAIVE:
				break;
			case SUBFLOW_SELECTION_TWOWAY_BALANCE:
				break;
			case SUBFLOW_SELECTION_NEWTXDELAY_OLD:
				break;
			case SUBFLOW_SELECTION_MINRTT_KERNEL:
				break;
			case SUBFLOW_SELECTION_TXDELAY:
				break;
			case SUBFLOW_SELECTION_WIFIONLY:
				break;
			case SUBFLOW_SELECTION_ONEPATH:
				if (argc == 5) {
					subflowInUseDMM = atoi(argv[4]);
				}
				break;
			case SUBFLOW_SELECTION_BBS_MRT:
				break;
			case SUBFLOW_SELECTION_EMPTCP:
				break;
			case SUBFLOW_SELECTION_BLOCK:
				break;
			case SUBFLOW_SELECTION_PAMS:
				if (argc == 5) {
					reinjPAMS = atoi(argv[4]);
				}
				else if (argc == 6) {
					reinjPAMS = atoi(argv[4]);
					nSubHelper = atoi(argv[5]);
				}
				break;
			case SUBFLOW_SELECTION_ROUNDROBIN:
				break;
			case SUBFLOW_SELECTION_REMP:
				break;
			default:
				goto SHOW_USAGE;
		}
		// Get the number of subflows from command-line arguments
        PROXY_SETTINGS::nTCPSubflows = atoi(argv[1]) + nSubHelper - 1;
		PROXY_SETTINGS::nSubflows = PROXY_SETTINGS::nTCPSubflows;

	} else {
		SHOW_USAGE:
		InfoMessage("Usage for local proxy:  %s [remote_proxy_IP] [number_of_subflows] [subflow1_if] [subflow2_if] ...", argv[0]);
		InfoMessage("Usage for remote proxy: %s [number_of_subflows] [subflow selection policy]", argv[0]);
		InfoMessage("Subflow selection policy:");
		InfoMessage("1 = Random");
		InfoMessage("2 = Round Robin");
		InfoMessage("3 = Round Robin / Minimum buffer");
		InfoMessage("5 = Only use Subflow 1");
		InfoMessage("6 = Minimum RTT (user)");
		InfoMessage("7 = Minimum RTT (kernel)");
		return 0;
	}
	
	// Apply default settings
	PROXY_SETTINGS::ApplyRawSettings();
	// Adjust the settings based on parameters and proxy location (RP/LP)
	PROXY_SETTINGS::AdjustSettings();
	
	if (PROXY_SETTINGS::nSubflows < 1 || PROXY_SETTINGS::nSubflows > 16) {
		ErrorMessage("Invalid number of subflows");
		return -1;
	}
	InfoMessage("Number of mobile devices: %d", atoi(argv[1]));

	PROXY_SETTINGS::subflowSelectionAlgorithm = subflowSelectionPolicy;

	PROXY_SETTINGS::isMPBondSchedEnhanced = enhancePolicy;
	// For PAMS enhancement "means" lateBinding.
	bLateBindingMPBond = enhancePolicy;
	// InfoMessage("Sched enhancement mechanism: %s", (PROXY_SETTINGS::isMPBondSchedEnhanced == 1)?"ON":"OFF");
	// InfoMessage("PAMS reinjection: %s", (reinjPAMS == 1)?"ON":"OFF");
	// InfoMessage("Number of subflows on the helper: %d", nSubHelper);
	StartTickCountThread();

	int r;	
	r = subflows.Setup(NULL);			
	
	if (r != R_SUCC) {
		ErrorMessage("Error setting up subflows");
		return -1;
	}

	if (conns.Setup() != R_SUCC) {
		ErrorMessage("Error setting up connections");
		return -1;
	}

	subflowOutput.Setup();
	tcpOutput.Setup();
	metaBuffer.Setup();
	kernelInfo.Setup();
	delayedFins.Setup(PROXY_SETTINGS::delayedFinD1, PROXY_SETTINGS::delayedFinD2);
	scheduler.Setup();

	lastSubflowActivityTick = tickCount;

	SUBFLOW_MONITOR::StartListen();
	
	kfd = open("/dev/cmatbuf", O_RDWR);
	MyAssert(kfd>=0, 2024);

	ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD1, subflows.fd[0]);
	ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD2, subflows.fd[1]);
    ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD3, subflows.fd[2]);
    ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD4, subflows.fd[3]);
 //    ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD5, subflows.fd[4]);
	// ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD6, subflows.fd[5]);
 //    ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD7, subflows.fd[6]);
 //    ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD8, subflows.fd[7]);
 //    ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD9, subflows.fd[8]);
	// ioctl(kfd, CMAT_IOCTL_SET_SUBFLOW_FD10, subflows.fd[9]);

	MyAssert(subflows.fd[0] == conns.peers[1].fd && subflows.fd[1] == conns.peers[2].fd, 2459);
    MyAssert(subflows.fd[2] == conns.peers[3].fd && subflows.fd[3] == conns.peers[4].fd, 2460);

	// Start running proxy
	ProxyMain();
	InfoMessage(" ************** Proxy Stopped. ***************");
	return 0;
}
