#include "proxy.h"
#include "connections.h"
#include "subflow.h"
#include "pipe.h"
#include "tools.h"
#include "hints.h"
#include <jni.h>

struct CONNECTIONS conns;
struct SUBFLOW subflows;
struct PIPE pipes;
struct BUFFER_SUBFLOW subflowOutput;  //the buffer for writing to subflows
struct BUFFER_TCP tcpOutput;  //the buffer for writing to tcp connections
struct KERNEL_INFO kernelInfo;
struct HINTS hintRules;
FILE * ofsIODump = NULL;

int keepRunning = 1;
int tickCount;
DWORD rpIPAddress = 0;
int lastPipeActivityTick;
unsigned long highResTimestampBase;

extern "C" {
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_util_Subflow_subflowFromJNI(JNIEnv *env, jobject thiz,
                                jstring rpIP, jint feedbackType);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_util_LocalConn_connSetupFromJNI(JNIEnv *env, jobject thiz);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_Primary_proxyFromJNI(JNIEnv *env, jobject thiz);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_util_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz,
                                jboolean isTether, jint n, jboolean isJava, jstring remoteIP, jstring rpIP);
};

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_util_Subflow_subflowFromJNI(JNIEnv *env, jobject thiz,
                                                                jstring rpIP, jint feedbackType) {
//    const char *subIp = env->GetStringUTFChars(subflowIP, 0);
    const char *rIp = env->GetStringUTFChars(rpIP, 0);
    LOGD("[subflow] rpIP = %s", rIp);
    PROXY_SETTINGS::ApplyRawSettings();
    PROXY_SETTINGS::AdjustSettings();
    if(subflows.Setup(rIp, feedbackType))
        return env->NewStringUTF("SubflowSetupSucc");
    return env->NewStringUTF("Subflow failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_util_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz,
                            jboolean isTether, jint numSec, jboolean isJava, jstring remoteIP, jstring rpIP) {
    if (isJava == JNI_TRUE) {
        // java pipe
        const char *rIP = env->GetStringUTFChars(rpIP, 0);
        if (pipes.Setup(isTether, numSec, rIP))
            return env->NewStringUTF("Pipe listener setup successfully.");
    }
    return env->NewStringUTF("Pipe failed. Hello from JNI LIBS!");
}


JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_util_LocalConn_connSetupFromJNI(JNIEnv *env, jobject thiz) {
    if(conns.Setup())
        return env->NewStringUTF("Conn setup successfully.");
    return env->NewStringUTF("Conn failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondpri_Primary_proxyFromJNI(JNIEnv *env, jobject thiz) {
    subflowOutput.Setup();
    tcpOutput.Setup();
    ProxyMain();
    return env->NewStringUTF("Proxy running on Primary.");
}

int ProxyMain() {

    while (keepRunning) {
        int nReady = poll(conns.peers, conns.maxIdx + 1, -1);
        if (nReady == 0) {
            continue;
        }

        if (conns.peers[0].revents & POLLRDNORM) {
            LOGD("New incoming TCP connection from client app");
            struct sockaddr_in clientAddr;
            socklen_t clientAddrLen = sizeof(clientAddr);
            int clientFD = accept(conns.localListenFD, (struct sockaddr *) &clientAddr,
                                  &clientAddrLen);
            if (clientFD != -1) {
                SetNonBlockIO(clientFD);
                SetSocketNoDelay_TCP(clientFD);
                SetSocketBuffer(clientFD, PROXY_SETTINGS::pipeReadBufLocalProxy,
                                PROXY_SETTINGS::pipeWriteBufLocalProxy);
                int newPollPos = conns.AddTCPConnection(clientFD,
                                                        clientAddr.sin_addr.s_addr,
                                                        ntohs(clientAddr.sin_port),
                                                        0, 0, 0
                );
                conns.TransferFromTCPToSubflows(newPollPos, clientFD); // sending SYN
            }
            if (--nReady <= 0) continue;
        }

        unsigned long cur_r_clock = 0;
        if (PROXY_SETTINGS::bScheduling) cur_r_clock = GetHighResTimestamp();
        int bursty_class = 0;
        unsigned long long min_l_clock = (unsigned long long) 1 << 60;
        int best_pollPos = -1;

        for (int i = 1; i <= conns.maxIdx; i++) {

            int peerFD = conns.peers[i].fd;
            if (peerFD < 0) continue;
            int bSubflow = i<=subflows.n;
            int bMarked = 0;

            if ((conns.peers[i].revents & (POLLRDNORM | POLLERR | POLLHUP)) ||
                (/*!bPipe &&*/ !conns.peersExt[i].bSentSYNToPipe) ||
                (/*!bPipe &&*/  conns.peersExt[i].establishStatus == POLLFD_EXT::EST_FAIL) ||
                (/*!bPipe &&*/  conns.peersExt[i].bToSendSYNACK)
                    ) {
                //conditions of !bSentSYNToPipe, establish failure, and to-send-SYNACK are triggered by
                //last time when the corresponding pipe msg was not sent due to buffer full

                bMarked = 1;
                MyAssert(conns.peersExt[i].establishStatus != POLLFD_EXT::EST_NOT_CONNECTED, 1704);

                if ((i > subflows.n) && (i <= subflows.n + pipes.n)) {
//                    LOGD("mpbond pipe message polled on %d", i);
                    if (conns.TransferFromSubflowsToTCP(i, peerFD) > 0) { // here subflow is pipe.
                        conns.TransferDelayedFINsToSubflows();
                    }
                    continue;
                }

                if (bSubflow) {
//                    LOGD("TCP subflow message polled");
                    if (conns.TransferFromSubflowsToTCP(i, peerFD) > 0) {
                        conns.TransferDelayedFINsToSubflows();
                    }
                    //conns.CheckOWDMeasurement();
                } else {
                    // Apply scheduling algorithm here (for RP)
                    if (PROXY_SETTINGS::bScheduling) {
                        WORD connID = conns.peersExt[i].connID;
                        MyAssert(connID>0, 2037);
                        struct CONN_INFO & c = conns.connTab[connID];
                        if (cur_r_clock - c.r_clock > (unsigned long)PROXY_SETTINGS::burstDurationTh) c.accuBurstyBytes = 0;
                        int bBursty = c.accuBurstyBytes < PROXY_SETTINGS::burstSizeTh;
                        if (bBursty) {
                            if (!bursty_class) {
                                bursty_class = 1;
                                min_l_clock = c.l_clock;
                                best_pollPos = i;
                            } else {
                                if (c.l_clock < min_l_clock) {
                                    min_l_clock = c.l_clock;
                                    best_pollPos = i;
                                }
                            }
                        } else {
                            if (!bursty_class) {
                                if (c.l_clock < min_l_clock) {
                                    min_l_clock = c.l_clock;
                                    best_pollPos = i;
                                }
                            }
                        }
                    } else {
                        // LOGD("No scheduling, use %d", i);
                        conns.TransferFromTCPToSubflows(i, peerFD);
                    }
                }
            }

            if (conns.peers[i].revents & POLLWRNORM) {
                bMarked = 1;
                MyAssert(conns.peersExt[i].establishStatus == POLLFD_EXT::EST_SUCC, 1705);
                if (bSubflow) {
                    if (subflowOutput.TransferFromTCPToSubflows(i, peerFD) > 0) {
                        conns.TransferDelayedFINsToSubflows();
                    }
                } else {
                    tcpOutput.TransferFromSubflowsToTCP(i);
                }
            }

            if (bMarked)
                if (--nReady <= 0) break;
        }
        if (/*PROXY_SETTINGS::bScheduling &&*/ best_pollPos != -1) {
            conns.TransferFromTCPToSubflows(best_pollPos, conns.peers[best_pollPos].fd);
        }
    }
    return 0;
}
