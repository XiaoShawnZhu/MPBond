#ifndef MPBOND_PROXY_H
#define MPBOND_PROXY_H

//transport protocol 1=TCP, 2=UDP, 3=SCTP
#define TRANS 1

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

#include <vector>
#include <map>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>

#include <linux/sockios.h>	//for SIOCOUTQ

#include <stdlib.h>
#include <time.h>

#include "proxy_setting.h"

#define R_SUCC 1
#define R_FAIL 0

typedef unsigned char BYTE;
typedef unsigned int DWORD;
typedef unsigned short WORD;


#define PROXY_MODE_LOCAL 1
#define PROXY_MODE_REMOTE 2

//using namespace std;

int ProxyMain();

#endif

