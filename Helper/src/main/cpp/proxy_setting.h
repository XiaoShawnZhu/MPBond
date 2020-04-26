#ifndef DMM_PROXY_SETTING_H
#define DMM_PROXY_SETTING_H

#include "proxy.h"

#define LOCAL_PROXY_PORT 1202
#define PIPE_LISTEN_PORT 1303
#define PIPE_LISTEN_PORT_SIDE 1304
#define REMOTE_PROXY_PORT 7001//6003
#define REMOTE_PROXY_UDP1_PORT 7002 // 6004 // for WiFi
#define REMOTE_PROXY_UDP2_PORT 7003 // 6005 // for cellular
#define MAX_CONN_ID_PLUS_ONE 16381 //max is 65536
#define TICK_COUNT_GRANULARITY_LOCAL_PROXY 1 //sec
#define TICK_COUNT_GRANULARITY_REMOTE_PROXY 1 //sec

#define MAX_PRIORITY 26
#define MAX_SCHEDULER 17

#define MAX_PIPES 32
#define MAX_FDS 512

//using namespace std;

#define PIPE_SELECTION_RANDOM 1
#define PIPE_SELECTION_ROUNDROBIN 2
#define PIPE_SELECTION_MINBUF 3
#define PIPE_SELECTION_ROUNDROBIN_CHUNK 4
#define PIPE_SELECTION_FIXED 5

struct PROXY_SETTINGS {
private:
    PROXY_SETTINGS();
    ~PROXY_SETTINGS();

public:
    static int bZeroRTTHandshake;

    static int pipeReadBufLocalProxy;
    static int pipeReadBufRemoteProxy;
    static int pipeWriteBufLocalProxy;
    static int pipeWriteBufRemoteProxy;
    static int pipeTransferUnit;
    static int tcpTransferUnit;

    static int pipeBufDataCapacity;
    static int pipeBufMsgCapacity;
    static int pipePerConnBufDataCapacity;

    static int tcpOverallBufMsgCapacity;
    static int tcpOverallBufDataCapacity;
    static int tcpPerConnBufDataCapacity;

    static double tcpBufAlmostFullRatio;
    static double tcpBufFullRatio;
    static double tcpBufReorgRatio;
    static int pipeBufMsgFullLeeway;
    static int pipeBufDataFullLeeway;
    static int pollTimeout;
    static int connIDReuseTimeout;
    static int connectTimeout;

    static double delayedFinD1;
    static double delayedFinD2;
    static int sctpMaxNumofStreams;
    static int sctpMaxStreamMsgSize;

    static int pipeSelectionAlgorithm;
    static int chunkSizeThreshold;

    static int nPipes;
    static int nTCPPipes;
    static int nUDPPipes;
    static int nSubflows;
    static std::string pipeInterfaces[MAX_PIPES];
    static std::string pipeProtocol[MAX_PIPES];
    static std::string allPipeInterface;
    static std::string allPipeProtocol;

    static int bDumpIO;

    static int bLateBindingRP;
    static int bLateBindingLP;

    static int bUseQuickACK;


    static int maxPayloadPerMsg;
    static int maxPayloadPerMsgFromPeer;

    static int bScheduling;
    static int burstDurationTh;
    static int burstSizeTh;


    static int enableTCPSocketBufferMonitor;
    //host-based congestion control: prerequisite: enableTCPSocketBufferMonitor = 1
    static int acc_enabled;
    static int acc_mincwin;	//256000
    static double acc_lamda;
    static double acc_alpha;	//the weight of new sample, set to 0.125
    static int acc_bif_th_for_calc_rttbase;	//64000, in bytes
    static int acc_adjust_freq;	//200, in # pkts
    static int acc_dump_info;

private:
    static int bQuickACK_RP;
    static int bQuickACK_LP;

    static int maxPayloadPerMsgLP;
    static int maxPayloadPerMsgRP;

    static int bUseLateBinding;

public:
    static void ApplyRawSettings();
    static void AdjustSettings();

    /*
    static int SendFile(const char * filename, const char * remoteIP);
    static int ReceiveFile(const char * filename);
    */



};

#endif //DMM_PROXY_SETTING_H

