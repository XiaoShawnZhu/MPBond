#ifndef _TOOLS_H_
#define _TOOLS_H_

#include "proxy.h"
#include "subflows.h"

extern struct SUBFLOWS subflows;
extern FILE * ofsLatency;
extern FILE * ofsDebug;
extern FILE * ofsOWD;

inline void MyAssert(int x, int assertID) {
#ifdef DEBUG_ENABLE_ASSERTION
	if (!x) {
		fprintf(stderr, "Assertion failure: %d\n", assertID);
		fprintf(stderr, "errno = %d (%s)\n", errno, strerror(errno));
		fclose(ofsLatency);
        fclose(ofsDebug);
        fclose(ofsOWD);
        shutdown(subflows.fd[0], 2);
		shutdown(subflows.fd[1], 2);
		exit(-1);
	}
#endif
}

void WriteLocalIPPort(char *s, int port, int subflowNo);

//void MyAssert(int x, int assertID);
void SetNonBlockIO(int fd);
void SetSocketBuffer(int fd, int readBufSize, int writeBufSize);
void SetMaxSegSize(int fd, int nBytes);
void SetSocketNoDelay_TCP(int fd);
void SetQuickACK(int fd);

#if TRANS == 3
void SetSocketNoDelay_SCTP(int fd);
#endif

DWORD GetCongestionWinSize(int fd);
DWORD GetCwndSpace(int fd);
DWORD GetSndBuffer(int fd);

const char * ConvertDWORDToIP(DWORD ip);
DWORD ConvertIPToDWORD(const char * ip);

static inline WORD ReverseWORD(WORD x) {
	return
		(x & 0xFF) << 8 |
		(x & 0xFF00) >> 8;
}

void GetClientIPPort(int fd, DWORD & ip, WORD & port);

const char * GetTimeString();

void DebugMessage(const char * format, ...);
void WarningMessage(const char * format, ...);
void InfoMessage(const char * format, ...);
void InfoMessageHead(const char * format, ...);
void InfoMessageContinue(const char * format, ...);
void ErrorMessage(const char * format, ...);
void VerboseMessage(const char * format, ...);

char * Chomp(char * str);
char * ChompSpace(char * str);
char * ChompSpaceTwoSides(char * str);

int GetFileSize(const char * filename, int & size);

void Split(char * str, const char * seps, char * * s, int & n);

/*
int FindStr(const char * str, const BYTE * pBuf, int n);
int FindRequest(int & rr, const BYTE * pBuf, int n);
int FindResponse(int & rr, const BYTE * pBuf, int n);
*/

#define TCPSOCKET_2_SUBFLOWBUFFER 1
#define SUBFLOWBUFFER_2_SUBFLOWSOCKET 2
#define SUBFLOWSOCKET_2_TCPBUFFER 3
#define TCPBUFFER_2_TCPSOCKET 4


double GetMillisecondTS();

inline DWORD Reverse(DWORD x) {
	return
		(x & 0xFF) << 24 |
		(x & 0xFF00) << 8 |
		(x & 0xFF0000) >> 8 |
		(x & 0xFF000000) >> 24;
}

inline WORD Reverse(WORD x) {
	return
		(x & 0xFF) << 8 |
		(x & 0xFF00) >> 8;
}

inline int Reverse(int x) {
	return (int)Reverse(DWORD(x));
}

unsigned long long GetTimeMilli();
unsigned long long GetTimeMicro();
uint64_t get_current_microsecond();

unsigned long GetHighResTimestamp();

inline unsigned long GetLogicTime() {
	static unsigned long l = 100;
	return l++;
}


#endif
