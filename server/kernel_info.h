#ifndef _KERNEL_INFO_H_
#define _KERNEL_INFO_H_

#include "proxy_setting.h"

#define TCP_CC_CUBIC 1
#define TCP_CC_BBR 2

struct OWD_ENTRY {
    // support up to 10MB bif
    int owd[1024]; // unit ms * 10, index: index * 10KB ~ (index+1) * 10KB --> owd
    int var[1024];
    int valid[1024];

    int subflowNo;

    double tmp_var;

    int isOWD;

    void Setup(int no);
    int BifToIndex(int bif); // bif in bytes
    void AddSample(int bif, int owd_sample); // bif in bytes, owd in ms * 10
    double GetOWD(int bif);
    double GetVar();
    void PrintMapping();
    //double PostProcessVar(double value);
};

struct OWD_MAPPING {
    struct OWD_ENTRY mapping[3]; // for subflow 1,2

    double tmp_var;
    double owd_var_default;

    void Setup();

    double PostProcessVar(double value);
    void AddSample(int subflowNo, int bif, int owd_sample);

    double GetOWDDiff(int bif1, int bif2);
    double GetOWDDiffVar();
    void PrintMapping();
};

struct KERNEL_INFO {
	struct tcp_info subflowinfo[MAX_SUBFLOWS];
    struct OWD_MAPPING owdMapping;
	int space[MAX_SUBFLOWS];
	//int owd_value, owd_var; // note: these are actual value * 10
	//int owd_default, owd_var_default; // note: these are actual value * 10
	//int owd_valid;
	//unsigned long owd_t;

	int fd[MAX_SUBFLOWS];
	int infoSize;
    int nTCP;
    // MPBond
    int pipeBW[MAX_SUBFLOWS]; // in kbps
    int pipeRTT[MAX_SUBFLOWS];
    int bytesInPipe[MAX_SUBFLOWS];
    int bytesOnDevice[MAX_SUBFLOWS];
    unsigned long long primaryAck; // for reliability/reinjection
    int updated;

	void Setup();
	void ChangeCC(int subflowNo, int cc);
	void UpdateSubflowInfo(int subflowNo);
	void UpdateTCPAvailableSpace();
	void DecreaseTCPAvailableSpace(int subflowNo, int delta);
	int IsCongWindowFull(int subflowNo);
	void UpdateOWD(int subflowNo, int owd, uint32_t ack);
	double GetOWD(int subflowNo, int bif1, int bif2);
	double GetOWDVar();
	//void ResetOWD();

    int HaveSpace();

    double GetBW(int subflowNo);
	int GetTCPAvailableSpace(int subflowNo);
	unsigned int GetSendCwnd(int subflowNo);
	unsigned int GetSndSsthresh(int subflowNo);
    unsigned int GetSndMss(int subflowNo);
	int GetInFlightSize(int subflowNo);
	int GetSRTT(int subflowNo);
	DWORD GetSndBuffer(int subflowNo);
    // MPBond
    double GetSubflowBW();
    int GetSubflowBuffer();
};

#endif
