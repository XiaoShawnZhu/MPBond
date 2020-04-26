#ifndef _PROXY_H_
#define _PROXY_H_

#define MY_VERSION "20191201"

//transport protocol 1=TCP, 2=UDP, 3=SCTP
#define TRANS 1

#define SOL_TCP 6
//#define MPTCP_ENABLED 26

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include <fcntl.h>
#include <linux/tcp.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <linux/fb.h>
#include <linux/sockios.h>	//for SIOCOUTQ

using namespace std;
#include <vector>
#include <map>
#include <algorithm>
#include <string>

#include "proxy_setting.h"

#define R_SUCC 1
#define R_FAIL 0

#define PROXY_MODE_LOCAL 1
#define PROXY_MODE_REMOTE 2

#define MAGIC_MSS_VALUE 1459

#ifndef TCP_CMAT_SUBFLOW
#define TCP_CMAT_SUBFLOW 31
#endif


typedef unsigned char BYTE;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef long long INT64;
typedef unsigned long long DWORD64;

#endif
