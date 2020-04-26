#include "proxy.h"
#include "subflow.h"
#include <jni.h>
#include "connections.h"
#include "proxy_setting.h"
#include "hints.h"
#include "tools.h"
#include "pipe.h"

struct CONNECTIONS conns;
struct SUBFLOW subflows;
struct PIPE pipes;
struct BUFFER_SUBFLOW subflowOutput;  //the buffer for writing to subflows
struct BUFFER_TCP tcpOutput;  //the buffer for writing to tcp connections
struct BUFFER_PIPE pipeOutput;
struct KERNEL_INFO kernelInfo;
struct HINTS hintRules;
FILE * ofsIODump = NULL;
int keepRunning = 1;
int tickCount;
DWORD rpIPAddress = 0;
int lastPipeActivityTick;
unsigned long highResTimestampBase;

extern "C" {
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_util_Subflow_subflowFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP,
                                                                        jstring rpIP, jint feedbackType, jint subflowId, jint nSubflow);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_util_LocalConn_connSetupFromJNI(JNIEnv *env, jobject thiz);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_Helper_proxyFromJNI(JNIEnv *env, jobject thiz);
    JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_util_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP,
                                                                                       jstring rpIP, jint secNo, jboolean isJava, jint nSubflow);
    JNIEXPORT jint JNICALL Java_edu_robustnet_xiao_mpbondhel_util_WiFiListener_toolFromJNI(JNIEnv *env, jobject thiz, jint fd);
};

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_util_Subflow_subflowFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP,
                                                                                    jstring rpIP, jint feedbackType, jint subflowId, jint nSubflow) {
    const char *subIp = env->GetStringUTFChars(subflowIP, 0);
    const char *rIp = env->GetStringUTFChars(rpIP, 0);
//    LOGD("[subflow] subflowIP = %s, rpIP = %s", subIp, rIp);
    if(subflows.Setup(subIp, rIp, feedbackType, subflowId, nSubflow))
        return env->NewStringUTF("SubflowSetupSucc");
    return env->NewStringUTF("Subflow setup failed.");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_util_Pipe_pipeSetupFromJNI(JNIEnv *env, jobject thiz, jstring subflowIP,
                                                                                   jstring rpIP, jint secNo, jboolean isJava, jint nSubflow) {
    const char *subIp = env->GetStringUTFChars(subflowIP, 0);
    const char *rIp = env->GetStringUTFChars(rpIP, 0);
    LOGD("[subflow] subflowIP = %s, rpIP = %s", subIp, rIp);
    PROXY_SETTINGS::ApplyRawSettings();
    PROXY_SETTINGS::AdjustSettings();
    if (isJava == JNI_TRUE) {
        if (pipes.Setup(subIp, rIp, secNo, nSubflow))
            return env->NewStringUTF("Pipe listener setup successfully.");
    }
    return env->NewStringUTF("Pipe failed. Hello from JNI LIBS!");
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_util_LocalConn_connSetupFromJNI(JNIEnv *env, jobject thiz) {
    if(conns.Setup())// should be pipe conns , need to be modified
        return env->NewStringUTF("Conn setup successfully.");
    return env->NewStringUTF("Conn failed. Hello from JNI LIBS!");
}

JNIEXPORT jint JNICALL Java_edu_robustnet_xiao_mpbondhel_util_WiFiListener_toolFromJNI(JNIEnv *env, jobject thiz, jint fd) {
    return GetSendBufSpace(fd);
//    GetRecvBufSpace(fd);
}

JNIEXPORT jstring JNICALL Java_edu_robustnet_xiao_mpbondhel_Helper_proxyFromJNI(JNIEnv *env, jobject thiz) {
//    PROXY_SETTINGS::nSubflows = nSubflow;
    LOGI("About to start proxy, nSubflow=%d", PROXY_SETTINGS::nSubflows);
    subflowOutput.Setup();
    tcpOutput.Setup();
    pipeOutput.Setup();
    kernelInfo.Setup();
//    pipeBuffer.Setup(10000000);
    ProxyMain();
    return env->NewStringUTF("Proxy running.");
}

int ProxyMain() {
    LOGD("Entering ProxyMain");

    while (keepRunning) {

        int nReady = poll(conns.peers, conns.maxIdx + 1, -1);
        if (nReady == 0) {
//            LOGD("Somehow we polled nothing, directly going to next round of the loop.");
            continue;
        }
//        LOGD("nReady=%d", nReady);

        for (int i = 1; i <= conns.maxIdx; i++) {
            if (i > subflows.n) {
                continue;
                //reserve for MPBond pipe
                //currently uplink app traffic all goes to primary subflow
                //TODO: uplink pipe traffic
            }

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
                //last time the when the corresponding pipe msg was not sent due to buffer full

                bMarked = 1;
                MyAssert(conns.peersExt[i].establishStatus != POLLFD_EXT::EST_NOT_CONNECTED, 1704);
                if (bSubflow) {
//                    LOGD("There is TCP subflow %d message from remote proxy", i);
                    kernelInfo.UpdatePipeInfo(0);
                    conns.TransferFromSubflowsToPipe(i, peerFD);
                }
            }
            if (bMarked)
                if (--nReady <= 0) break;
        }
    }
    return 0;
}
