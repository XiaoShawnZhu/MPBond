#include "kernel_info.h"
#include "connections.h"
#include "tools.h"

extern struct CONNECTIONS conns;
extern struct KERNEL_INFO kernelInfo;
extern int kfd;

extern FILE * ofsOWD;

char ccname[16];
int cclen = 16;

void KERNEL_INFO::Setup() {
	infoSize = sizeof(subflowinfo[0]);
	for (int i = 1; i < MAX_SUBFLOWS; i++) {
		fd[i] = conns.peers[i].fd;
		//InfoMessage("Subflow %d: fd %d", i, fd[i]);
	}
    nTCP = PROXY_SETTINGS::nTCPSubflows;
    //owd_t = 0;
    //owd_var_default = 200; // 20ms
    //owd_valid = 0;

    for (int i = 1; i < MAX_SUBFLOWS; i++) {
        pipeBW[i] = 0;
        pipeRTT[i] = 0;
        bytesInPipe[i] = 0;
        bytesOnDevice[i] = 0;
    }
    primaryAck = 0;
    updated = 0;
    
    owdMapping.Setup();
    
}

void KERNEL_INFO::ChangeCC(int subflowNo, int cc) {
	switch(cc) {
	case TCP_CC_CUBIC:
		strcpy(ccname, "cubic");
		break;
	case TCP_CC_BBR:
		strcpy(ccname, "vegas");
		break;
	default:
		return;
	}
	setsockopt(fd[subflowNo], IPPROTO_TCP, TCP_CONGESTION, ccname, cclen);
}

// in bps
double KERNEL_INFO::GetBW(int subflowNo) {

	return (double) GetSndMss(subflowNo) * (double) GetSendCwnd(subflowNo) * 8 / (((double) GetSRTT(subflowNo) + 1) / 1000000.0);

}

void KERNEL_INFO::UpdateSubflowInfo(int subflowNo) {
	int r = 0;
	if (subflowNo > 0 && subflowNo < MAX_SUBFLOWS) {
		r = getsockopt(fd[subflowNo], IPPROTO_TCP, TCP_INFO,
			&subflowinfo[subflowNo], (socklen_t *)&infoSize);
		MyAssert(r == 0, 9110);
	} else {
		for (int i = 1; i <= nTCP; i++) {
			if (fd[i] >= 0) {
				r = getsockopt(fd[i], IPPROTO_TCP, TCP_INFO,
					&subflowinfo[i], (socklen_t *)&infoSize);
				MyAssert(r == 0, 9111);
			}
		}
	}
}

void KERNEL_INFO::UpdateTCPAvailableSpace() {
	int r;
    for (int i = 1; i <= nTCP; i++) {
        // int rmd = GetSendCwnd(i) - GetSndBuffer(i);
        // space[i] = (rmd > 0) ? rmd : 0;

        switch (i) {
            case 1:
    	        ioctl(kfd, CMAT_IOCTL_GET_FD1_AVAIL, &r);
        	    space[1] = r;
                break;
            case 2:
            	ioctl(kfd, CMAT_IOCTL_GET_FD2_AVAIL, &r);
            	space[2] = r;
                break;
            case 3:
                ioctl(kfd, CMAT_IOCTL_GET_FD3_AVAIL, &r);
                space[3] = r;
                break;
            case 4:
                ioctl(kfd, CMAT_IOCTL_GET_FD4_AVAIL, &r);
                space[4] = r;
                break;

            default:
                break;
        }
    }
}

/*
void KERNEL_INFO::ResetOWD() {
	owd_t = 0;	
}
*/

void KERNEL_INFO::UpdateOWD(int subflowNo, int owd, uint32_t ack) {
    int bif = -1;
    uint64_t input = ((uint64_t) subflowNo << 32) + (uint64_t) ack;

    ioctl(kfd, CMAT_IOCTL_SET_BIF_REQUEST, input);
    ioctl(kfd, CMAT_IOCTL_GET_BIF, &bif);

    //InfoMessage("OWD: subflow=%d owd=%d ack=%u bif=%d", subflowNo, owd, ack, bif);
	if (bif >= 0) {
    	owdMapping.AddSample(subflowNo, bif, owd); 
	}
    /*
	unsigned long currt = get_current_microsecond() / 1000;

	if (owd_t == 0) {//} || currt - owd_t > 50) {
		owd_default = owd;
		owd_valid = 1;
		owd_value = owd;
		owd_var = owd_var_default;
	} else {
			int d = (owd > owd_value)? (owd - owd_value): (owd_value - owd);
			owd_value = int((double) owd_value * 0.75 + (double) owd * 0.25);
			if (owd_var == 0) {
				owd_var = d;
			} else {
				owd_var = int((double) owd_var * 0.75 + (double) d * 0.25);
			}
	}
	owd_t = currt;
	fprintf(ofsOWD, "%lu\t%d\t%d\t%d\n",
		owd_t, owd, owd_value, owd_var);
	//InfoMessage("[%llu] OWD measurement: %d ms, %d. %d, %d", owd_t, owd / 10,
	//	GetSRTT(2) - GetSRTT(1), owd_value, owd_var);
    */
}

// actual value (ms)
double KERNEL_INFO::GetOWD(int subflowNo, int bif1, int bif2) {
    double r = owdMapping.GetOWDDiff(bif1, bif2);

    if (subflowNo == 1) return r;
    return -r;
    /*
	// return OWD[other] - OWD[subflowNo]
	unsigned long currt = get_current_microsecond() / 1000;
	double r = 0.0;
	if (owd_t == 0) {
		if (owd_valid > 0) {
			r = (double) owd_default / 10.0;//((double) GetSRTT(2) / 1000.0 - (double) GetSRTT(1) / 1000.0) / 2.0;
		} else {
			r = ((double) GetSRTT(2) / 1000.0 - (double) GetSRTT(1) / 1000.0) / 2.0;
		}
		//fprintf(ofsOWD, "%lu\t%d\t%f\t%f\n",
		//		currt, -1, r*10, GetOWDVar()*10);
	} else {
		if (currt - owd_t < 50) {
			r = (double) owd_value / 10.0;
		} else {
			r = (double) owd_default / 10.0;
			//fprintf(ofsOWD, "%lu\t%d\t%f\t%f\n",
			//	currt, -1, r*10, GetOWDVar()*10);
		}
	}
	if (subflowNo == 1) {
		return r;
	}
	return -r;
    */
}

// (ms)
double KERNEL_INFO::GetOWDVar() {
    return owdMapping.GetOWDDiffVar();
    /*
	//unsigned long currt = get_current_microsecond() / 1000;
	//if (currt - owd_t > 50) {
	//	return (double) owd_var_default / 10.0;
	//}
	if (owd_var > owd_var_default)
		return (double) owd_var / 10.0;	
	return (double) owd_var_default / 10.0;
    */
}

int KERNEL_INFO::HaveSpace() {
    for (int i = 1; i <= nTCP; i++) {
        if (space[i] > 0) return 1;
    }
    return 0;
}

int KERNEL_INFO::GetTCPAvailableSpace(int subflowNo) {
	if (subflowNo >= 1 && subflowNo <= nTCP)
		return space[subflowNo];
	return 0;
}

void KERNEL_INFO::DecreaseTCPAvailableSpace(int subflowNo, int delta) {
    // InfoMessage("space for %d decreased from %d by %d", subflowNo, space[subflowNo], delta);
	if (subflowNo >= 1 && subflowNo <= nTCP)
		space[subflowNo] -= delta;
}

int KERNEL_INFO::IsCongWindowFull(int subflowNo) {
	//int inFlight = GetInFlightSize(subflowNo);
	// TODO: implement this.
	return 0;
}

unsigned int KERNEL_INFO::GetSendCwnd(int subflowNo) {
	return subflowinfo[subflowNo].tcpi_snd_cwnd;
}

unsigned int KERNEL_INFO::GetSndSsthresh(int subflowNo) {
	return subflowinfo[subflowNo].tcpi_snd_ssthresh;
}

unsigned int KERNEL_INFO::GetSndMss(int subflowNo) {
	return subflowinfo[subflowNo].tcpi_snd_mss;
}

// bytes
int KERNEL_INFO::GetInFlightSize(int subflowNo) {
	return (subflowinfo[subflowNo].tcpi_unacked - subflowinfo[subflowNo].tcpi_sacked
		- subflowinfo[subflowNo].tcpi_lost + subflowinfo[subflowNo].tcpi_retrans) * GetSndMss(subflowNo);
}


// us
int KERNEL_INFO::GetSRTT(int subflowNo) {
	return subflowinfo[subflowNo].tcpi_rtt;
}

DWORD KERNEL_INFO::GetSndBuffer(int subflowNo) {
	int buf_size = 0;
	ioctl(fd[subflowNo], TIOCOUTQ, &buf_size);
	return buf_size;
}

void OWD_ENTRY::Setup(int no) {
    memset(valid, 0, sizeof(valid));
    subflowNo = no;
}

int OWD_ENTRY::BifToIndex(int bif) {
    int index = bif / 1024 / 10;
    if (index < 0) return 0;
    if (index < 1024) return index;
    return 1023;
}

// bif in bytes, owd in ms * 10
void OWD_ENTRY::AddSample(int bif, int owd_sample) {
    int index = BifToIndex(bif);
    if (valid[index] == 0) {
        owd[index] = owd_sample;
        var[index] = 0;
        valid[index] = 1;
    } else {
        int d = (owd_sample > owd[index])? (owd_sample - owd[index]): (owd[index] - owd_sample);
        owd[index] = int((double) owd[index] * 0.75 + (double) owd_sample * 0.25);
        if (var[index] == 0) {
            var[index] = d;
        } else {
            var[index] = int((double) var[index] * 0.75 + (double) d * 0.25);
        }
    } 
}

// ms
double OWD_ENTRY::GetOWD(int bif) {
    int index = BifToIndex(bif);
    if (valid[index] > 0) {
        tmp_var = (double) var[index] / 10.0;
        return (double) owd[index] / 10.0;
    }

    // no valid sample, use interpolation
    int index1 = index, index2 = index;
    while (index1 >= 0) {
        if (valid[index1] > 0) break;
        index1--;
    } 
    while (index2 < 1024) {
        if (valid[index2] > 0) break;
        index2++;
    }

    if (index1 < 0 && index2 >= 1024) {
        // No owd sample
        isOWD = 0;

    	tmp_var = 0.0;
    	return (double) kernelInfo.GetSRTT(subflowNo) / 2000.0;
    }

    isOWD = 1;
    if (index1 < 0) {
        tmp_var = (double) var[index2] / 10.0;
        return (double) owd[index2] / 10.0;
    }
    if (index2 >= 1024) {
        tmp_var = (double) var[index1] / 10.0;
        return (double) owd[index1] / 10.0;
    }

    double value1 = (double) var[index1] / 10.0;
    double value2 = (double) var[index2] / 10.0;

    tmp_var = (
        (double)(value1 * double(index2 - index) +
                    value2 * double(index - index1)) / (double)(index2 - index1));    


    value1 = (double) owd[index1] / 10.0;
    value2 = (double) owd[index2] / 10.0;
    
    return (double)(value1 * double(index2 - index) + value2 * double(index - index1)) / (double)(index2 - index1);
}

double OWD_MAPPING::PostProcessVar(double value) {
    if (value < owd_var_default) return owd_var_default;
    return value;
}

// ms
double OWD_ENTRY::GetVar() {
    /*
    int index = BifToIndex(bif);
    if (valid[index] > 0) {
        return PostProcessVar((double) var[index] / 10.0);
    }

    // no valid sample, use interpolation
    int index1 = index, index2 = index;
    while (index1 >= 0) {
        if (valid[index1] > 0) break;
        index1--;
    }
    while (index2 < 1024) {
        if (valid[index2] > 0) break;
        index2++;
    }

    if (index1 < 0) {
        return PostProcessVar((double) var[index2] / 10.0);
    }
    if (index2 >= 1024) {
        return PostProcessVar((double) var[index1] / 10.0);
    }
    double value1 = (double) var[index1] / 10.0;
    double value2 = (double) var[index2] / 10.0;

    return PostProcessVar(
        (double)(value1 * double(index2 - index) + 
                    value2 * double(index - index1)) / (double)(index2 - index1));
    */
    return tmp_var;
}

void OWD_ENTRY::PrintMapping() {
    InfoMessage("OWD mapping (subflow %d):", subflowNo);
    for (int i = 0; i < 1024; i++) {
        if (valid[i] > 0) {
            printf("%dK: %d/%dms*10, ", i*10, owd[i], var[i]);
        }
    }
    printf("\n");
}

void OWD_MAPPING::Setup() {
    for (int i = 0; i < 3; i++) {
        mapping[i].Setup(i);
   	}
   	owd_var_default = 10.0;
}

// subflow2 - subflow1
double OWD_MAPPING::GetOWDDiff(int bif1, int bif2) {
    double owd1 = mapping[1].GetOWD(bif1);
    double var1 = mapping[1].GetVar();
    int isOWD1 = mapping[1].isOWD;

    double owd2 = mapping[2].GetOWD(bif2);
    double var2 = mapping[2].GetVar();
    int isOWD2 = mapping[2].isOWD;

    if (isOWD1 > 0 && isOWD2 > 0) {
        tmp_var = PostProcessVar((var1 + var2) / 2.0);
        
        return owd2 - owd1;
    }
    tmp_var = PostProcessVar(0);

    return ((double) kernelInfo.GetSRTT(2) - (double) kernelInfo.GetSRTT(1)) / 2000.0;

}

double OWD_MAPPING::GetOWDDiffVar() {
    return tmp_var;
}

void OWD_MAPPING::AddSample(int subflowNo, int bif, int owd_sample) {
    mapping[subflowNo].AddSample(bif, owd_sample);
}

void OWD_MAPPING::PrintMapping() {
    mapping[1].PrintMapping();
    mapping[2].PrintMapping();
}
