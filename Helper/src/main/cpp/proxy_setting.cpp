#include "proxy_setting.h"
#include "tools.h"

int PROXY_SETTINGS::pipeReadBufLocalProxy;
int PROXY_SETTINGS::pipeReadBufRemoteProxy;
int PROXY_SETTINGS::pipeWriteBufLocalProxy;
int PROXY_SETTINGS::pipeWriteBufRemoteProxy;
int PROXY_SETTINGS::pipeTransferUnit;
int PROXY_SETTINGS::tcpTransferUnit;
int PROXY_SETTINGS::pipeBufDataCapacity;
int PROXY_SETTINGS::pipeBufMsgCapacity;
int PROXY_SETTINGS::pipePerConnBufDataCapacity;
int PROXY_SETTINGS::tcpOverallBufMsgCapacity;
int PROXY_SETTINGS::tcpOverallBufDataCapacity;
int PROXY_SETTINGS::tcpPerConnBufDataCapacity;
double PROXY_SETTINGS::tcpBufAlmostFullRatio;
double PROXY_SETTINGS::tcpBufFullRatio;
double PROXY_SETTINGS::tcpBufReorgRatio;
int PROXY_SETTINGS::pipeBufMsgFullLeeway;
int PROXY_SETTINGS::pipeBufDataFullLeeway;
int PROXY_SETTINGS::pollTimeout;
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

int PROXY_SETTINGS::nPipes;
int PROXY_SETTINGS::nSubflows;
int PROXY_SETTINGS::nTCPPipes;
int PROXY_SETTINGS::nUDPPipes;
std::string PROXY_SETTINGS::pipeInterfaces[MAX_PIPES];
std::string PROXY_SETTINGS::pipeProtocol[MAX_PIPES];

std::string PROXY_SETTINGS::allPipeInterface;
std::string PROXY_SETTINGS::allPipeProtocol;

int PROXY_SETTINGS::pipeSelectionAlgorithm;
int PROXY_SETTINGS::chunkSizeThreshold;

int PROXY_SETTINGS::bLateBindingRP;
int PROXY_SETTINGS::bLateBindingLP;
int PROXY_SETTINGS::bUseLateBinding;

int PROXY_SETTINGS::bScheduling;
int PROXY_SETTINGS::burstDurationTh;
int PROXY_SETTINGS::burstSizeTh;

int PROXY_SETTINGS::bQuickACK_LP;
int PROXY_SETTINGS::bQuickACK_RP;
int PROXY_SETTINGS::bUseQuickACK;

int PROXY_SETTINGS::enableTCPSocketBufferMonitor;
int PROXY_SETTINGS::acc_enabled;
int PROXY_SETTINGS::acc_mincwin;
double PROXY_SETTINGS::acc_lamda;
double PROXY_SETTINGS::acc_alpha;
int PROXY_SETTINGS::acc_bif_th_for_calc_rttbase;
int PROXY_SETTINGS::acc_adjust_freq;
int PROXY_SETTINGS::acc_dump_info;

int PROXY_SETTINGS::bZeroRTTHandshake;

extern int proxyMode;

void PROXY_SETTINGS::ApplyRawSettings() {
    bZeroRTTHandshake = 1; //FindInt("ZERO_RTT_HANDSHAKE");

    pipeReadBufLocalProxy = 8388608;//-1;//8000000; //FindInt("PIPE_READ_BUFFER_LOCAL_PROXY");
    pipeReadBufRemoteProxy = 8388608; //FindInt("PIPE_READ_BUFFER_REMOTE_PROXY");
    pipeWriteBufLocalProxy = 8388608;//-1;//4194304; //FindInt("PIPE_WRITE_BUFFER_LOCAL_PROXY");
    pipeWriteBufRemoteProxy = 8388608; //FindInt("PIPE_WRITE_BUFFER_REMOTE_PROXY");
    pipeTransferUnit = 2147483647; //FindInt("PIPE_TRANSFER_UNIT");
    tcpTransferUnit = 2147483647; //FindInt("TCP_TRANSFER_UNIT");
    pipeBufDataCapacity = 4000000; //FindInt("PIPE_BUFFER_DATA_CAPACITY");
    pipeBufMsgCapacity = 8192; //FindInt("PIPE_BUFFER_MSG_CAPACITY");
    pipePerConnBufDataCapacity = 2000000; //FindInt("PIPE_PER_CONN_BUFFER_DATA_CAPACITY");
    tcpOverallBufMsgCapacity = 32768; //FindInt("TCP_OVERALL_BUFFER_MSG_CAPACITY");
    tcpOverallBufDataCapacity = 33554432; //FindInt("TCP_OVERALL_BUFFER_DATA_CAPACITY");
    tcpPerConnBufDataCapacity = 20000000; //FindInt("TCP_PER_CONN_BUFFER_DATA_CAPACITY");
    tcpBufAlmostFullRatio = 0.7f; //FindDouble("TCP_BUFFER_ALMOST_FULL_RATIO");
    tcpBufFullRatio = 0.8f; //FindDouble("TCP_BUFFER_FULL_RATIO");
    tcpBufReorgRatio = 0.95f; //FindDouble("TCP_BUFFER_REORG_RATIO");
    pipeBufMsgFullLeeway = 16; //FindInt("PIPE_BUFFER_MSG_FULL_LEEWAY");
    pipeBufDataFullLeeway = 65536; //FindInt("PIPE_BUFFER_DATA_FULL_LEEWAY");
    pollTimeout = 2000;//5000; //FindInt("POLL_TIMEOUT");
    connIDReuseTimeout = 20; //FindInt("CONN_ID_REUSE_TIMEOUT");
    connectTimeout = 10; //FindInt("CONNECT_TIMEOUT");
    maxPayloadPerMsgLP = 4096; //FindInt("MAX_PAYLOAD_PER_MSG_LP");
    maxPayloadPerMsgRP = 4096; //FindInt("MAX_PAYLOAD_PER_MSG_RP");
    delayedFinD1 = 3.0f; //FindDouble("DELAYED_FIN_D1");
    delayedFinD2 = 10.0f; //FindDouble("DELAYED_FIN_D2");
    sctpMaxNumofStreams = 512; //FindInt("SCTP_MAX_NUMBER_OF_STREAMS");
    sctpMaxStreamMsgSize = 32768; //FindInt("SCTP_MAX_STREAM_MESSAGE_SIZE");
    enableTCPSocketBufferMonitor = 0; //FindInt("ENABLE_TCP_SOCKET_BUFFER_MONITOR");
    bDumpIO = 0; //FindInt("IS_DUMP_IO");

    /*
    enableLargeFlowBinding = FindInt("ENABLE_LARGE_FLOW_BINDING");
    largeFlowBytesThreshold = FindInt("LARGE_FLOW_BINDING_THRESHOLD");
    maxNumberBindedPipes = FindInt("MAX_NUMBER_OF_BINDED_PIPES");
    */

    nSubflows = 1;
    nPipes = 1; //FindInt("PIPE_NUMBERS");
    pipeSelectionAlgorithm = 5; //FindInt("PIPE_SELECTION_ALGORITHM");
    //pipeSelectionAlgorithm = 6; //test UDP
    chunkSizeThreshold = 256000; //FindInt("CHUNK_SIZE_THRESHOLD");

    bUseLateBinding = 0; //FindInt("LATE_BINDING");

    bScheduling = 0; //FindInt("IS_SCHEDULING");
    burstDurationTh = 500; //FindInt("BURST_DURATION_THRESHOLD");
    burstSizeTh = 500000; //FindInt("BURST_SIZE_THRESHOLD");

    bQuickACK_LP = 0; //FindInt("USE_QUICK_ACK_LP");
    bQuickACK_RP = 0; //FindInt("USE_QUICK_ACK_RP");

    char buf[32];
    for (int i=0; i<nPipes; i++) {
        sprintf(buf, "PIPE_INTERFACE_%d", i+1);
        pipeInterfaces[i] = "default"; //FindString(buf);
        sprintf(buf, "PIPE_PROTOCOL_%d", i+1);
        pipeProtocol[i] = "default"; //FindString(buf);
    }

    allPipeInterface = "default"; //FindString("ALL_PIPE_INTERFACE");
    allPipeProtocol = "default"; //FindString("ALL_PIPE_PROTOCOL");

    acc_enabled = 0; //FindInt("ACC_ENABLED");
    acc_mincwin = 256000; //FindInt("ACC_MIN_CWND");
    acc_lamda = 4; //FindDouble("ACC_LAMDA");
    acc_alpha = 0.125; //FindDouble("ACC_ALPHA");
    acc_bif_th_for_calc_rttbase = 64000; //FindInt("ACC_BIF_TH_FOR_CALC_RTTBASE");
    acc_adjust_freq = 100; //FindInt("ACC_ADJUST_FREQ");
    acc_dump_info = 0; //FindInt("ACC_DUMP_INFO");
}

void PROXY_SETTINGS::AdjustSettings() {

    enableTCPSocketBufferMonitor = 0;
    acc_enabled = 0;
    bScheduling = 0;

    maxPayloadPerMsg = maxPayloadPerMsgLP;
    maxPayloadPerMsgFromPeer = maxPayloadPerMsgRP;
    bUseQuickACK = bQuickACK_LP;


    bLateBindingLP = bLateBindingRP = 0;
//    if (bUseLateBinding) {
//        if (proxyMode == PROXY_MODE_LOCAL)
//            bLateBindingLP = 1;
//        else
//            bLateBindingRP = 1;
//    }

    burstDurationTh *= 1000;	//ms to us

//    if (proxyMode == PROXY_MODE_LOCAL) {
//        if (strcmp(allPipeInterface.c_str(), "none") && strcmp(allPipeInterface.c_str(), "NONE")) {
//            InfoMessage("All pipes use the same interface: %s", allPipeInterface.c_str());
//            for (int i=0; i<nPipes; i++) {
//                pipeInterfaces[i] = allPipeInterface;
//            }
//        }
//
//        if (strcmp(allPipeProtocol.c_str(), "none") && strcmp(allPipeProtocol.c_str(), "NONE")) {
//            InfoMessage("All pipes use the same protocol: %s", allPipeProtocol.c_str());
//            for (int i=0; i<nPipes; i++) {
//                pipeProtocol[i] = allPipeProtocol;
//            }
//        }
//    }

#if TRANS == 2
    nPipes = 1;
#endif
}