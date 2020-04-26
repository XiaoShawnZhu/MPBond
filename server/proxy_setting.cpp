#include "stdafx.h"
#include "proxy_setting.h"
#include "tools.h"

int PROXY_SETTINGS::subflowReadBufRemoteProxy;
int PROXY_SETTINGS::subflowWriteBufRemoteProxy;
int PROXY_SETTINGS::tcpReadBufRemoteProxy;
int PROXY_SETTINGS::tcpTransferUnit;
int PROXY_SETTINGS::subflowBufDataCapacity;
int PROXY_SETTINGS::subflowBufMsgCapacity;
int PROXY_SETTINGS::subflowPerConnBufDataCapacity;
int PROXY_SETTINGS::tcpOverallBufMsgCapacity;
int PROXY_SETTINGS::tcpOverallBufDataCapacity;
int PROXY_SETTINGS::tcpPerConnBufDataCapacity;
double PROXY_SETTINGS::tcpBufAlmostFullRatio;
double PROXY_SETTINGS::tcpBufFullRatio;
double PROXY_SETTINGS::tcpBufReorgRatio;
int PROXY_SETTINGS::subflowBufMsgFullLeeway;
int PROXY_SETTINGS::subflowBufDataFullLeeway;
int PROXY_SETTINGS::pollTimeout;
long PROXY_SETTINGS::rcvContTime;
int PROXY_SETTINGS::connIDReuseTimeout;
int PROXY_SETTINGS::connectTimeout;
int PROXY_SETTINGS::maxPayloadPerMsgLP;
int PROXY_SETTINGS::maxPayloadPerMsgRP;
int PROXY_SETTINGS::maxPayloadPerMsg;
int PROXY_SETTINGS::maxPayloadPerMsgFromPeer;

double PROXY_SETTINGS::delayedFinD1;
double PROXY_SETTINGS::delayedFinD2;

int PROXY_SETTINGS::sctpMaxNumofStreams;
int PROXY_SETTINGS::sctpMaxStreamMsgSize;

int PROXY_SETTINGS::bDumpIO;

int PROXY_SETTINGS::nSubflows;
int PROXY_SETTINGS::nTCPSubflows;
int PROXY_SETTINGS::nUDPSubflows;
string PROXY_SETTINGS::subflowInterfaces[MAX_SUBFLOWS];
string PROXY_SETTINGS::subflowProtocol[MAX_SUBFLOWS];

string PROXY_SETTINGS::allSubflowInterface;
string PROXY_SETTINGS::allSubflowProtocol;

int PROXY_SETTINGS::subflowSelectionAlgorithm;
int PROXY_SETTINGS::burstDurationTh;
int PROXY_SETTINGS::burstSizeTh;
int PROXY_SETTINGS::bQuickACK_LP;
int PROXY_SETTINGS::bQuickACK_RP;
int PROXY_SETTINGS::bUseQuickACK;

int PROXY_SETTINGS::bZeroRTTHandshake;

int PROXY_SETTINGS::metaBufDataCapacity;
int PROXY_SETTINGS::metaBufMsgCapacity;

int PROXY_SETTINGS::isMPBondSchedEnhanced;

extern int proxyMode;

void PROXY_SETTINGS::ApplyRawSettings() {
	bZeroRTTHandshake = 1; //FindInt("ZERO_RTT_HANDSHAKE");

	subflowReadBufRemoteProxy = 8388608; //FindInt("SUBFLOW_READ_BUFFER_REMOTE_PROXY");
	subflowWriteBufRemoteProxy = 8388608; //FindInt("SUBFLOW_WRITE_BUFFER_REMOTE_PROXY");
	tcpReadBufRemoteProxy = 6291456;//3145728;//1048576;
	tcpTransferUnit = 2147483647; //FindInt("TCP_TRANSFER_UNIT"); // max 32-bit int value
	subflowBufDataCapacity = 4000000; //FindInt("SUBFLOW_BUFFER_DATA_CAPACITY");
	subflowBufMsgCapacity = 16384; //FindInt("SUBFLOW_BUFFER_MSG_CAPACITY");	//was 8192

	metaBufDataCapacity = 16000000; //256*1024*1024*17; //16*1024*1024;//2*1024*1024;//128*1024;
	metaBufMsgCapacity = 16384;

	subflowPerConnBufDataCapacity = 2000000; //FindInt("SUBFLOW_PER_CONN_BUFFER_DATA_CAPACITY");
	tcpOverallBufMsgCapacity = 32768; //FindInt("TCP_OVERALL_BUFFER_MSG_CAPACITY");
	tcpOverallBufDataCapacity = 33554432; //FindInt("TCP_OVERALL_BUFFER_DATA_CAPACITY");
	tcpPerConnBufDataCapacity = 20000000; //FindInt("TCP_PER_CONN_BUFFER_DATA_CAPACITY");
	tcpBufAlmostFullRatio = 0.7f; //FindDouble("TCP_BUFFER_ALMOST_FULL_RATIO");
	tcpBufFullRatio = 0.8f; //FindDouble("TCP_BUFFER_FULL_RATIO");
	tcpBufReorgRatio = 0.95f; //FindDouble("TCP_BUFFER_REORG_RATIO");
	subflowBufMsgFullLeeway = 16; //FindInt("SUBFLOW_BUFFER_MSG_FULL_LEEWAY");	//was 16
	subflowBufDataFullLeeway = 65536; //FindInt("SUBFLOW_BUFFER_DATA_FULL_LEEWAY");
	pollTimeout = 0;//5000; //FindInt("POLL_TIMEOUT");
    rcvContTime = 1000; // us
	connIDReuseTimeout = 20; //FindInt("CONN_ID_REUSE_TIMEOUT");
	connectTimeout = 10; //FindInt("CONNECT_TIMEOUT");
	maxPayloadPerMsgLP = 1300; //FindInt("MAX_PAYLOAD_PER_MSG_LP");	//was 4096
	maxPayloadPerMsgRP = 1300; //FindInt("MAX_PAYLOAD_PER_MSG_RP");	//was 4096
	delayedFinD1 = 3.0f; //FindDouble("DELAYED_FIN_D1");
	delayedFinD2 = 10.0f; //FindDouble("DELAYED_FIN_D2");
	sctpMaxNumofStreams = 512; //FindInt("SCTP_MAX_NUMBER_OF_STREAMS");
	sctpMaxStreamMsgSize = 32768; //FindInt("SCTP_MAX_STREAM_MESSAGE_SIZE");
	bDumpIO = 0; //FindInt("IS_DUMP_IO");

	//nSubflows = 1; //FindInt("SUBFLOW_NUMBERS");
	subflowSelectionAlgorithm = SUBFLOW_SELECTION_MINRTT_KERNEL; //FindInt("SUBFLOW_SELECTION_ALGORITHM");
	burstDurationTh = 500; //FindInt("BURST_DURATION_THRESHOLD");
	burstSizeTh = 500000; //FindInt("BURST_SIZE_THRESHOLD");
	bQuickACK_LP = 0; //FindInt("USE_QUICK_ACK_LP");
	bQuickACK_RP = 0; //FindInt("USE_QUICK_ACK_RP");

	char buf[32];
	for (int i = 0; i < nSubflows; i++) {
		sprintf(buf, "SUBFLOW_INTERFACE_%d", i+1);
		subflowInterfaces[i] = "default"; //FindString(buf);
		sprintf(buf, "SUBFLOW_PROTOCOL_%d", i+1);
		subflowProtocol[i] = "default"; //FindString(buf);
	}

	allSubflowInterface = "default"; //FindString("ALL_SUBFLOW_INTERFACE");
	allSubflowProtocol = "default"; //FindString("ALL_SUBFLOW_PROTOCOL");
	isMPBondSchedEnhanced = 0;
}

void PROXY_SETTINGS::AdjustSettings() {

	maxPayloadPerMsg = maxPayloadPerMsgRP;
	maxPayloadPerMsgFromPeer = maxPayloadPerMsgLP;
	bUseQuickACK = bQuickACK_RP;
	burstDurationTh *= 1000;	//ms to us
}
