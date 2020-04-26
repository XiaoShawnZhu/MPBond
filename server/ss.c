#include <linux/kernel.h>
#include <linux/kprobes.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netdevice.h>
#include <linux/miscdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <linux/time.h>

//#define REINJECTION

#define TIOCOUTQ        0x7472 
//#define DEBUG_DUMP
#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"

MODULE_AUTHOR("Feng Qian, Yihua Guo, Xiao Zhu");
//MODULE_LICENSE("TODO");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.1");

typedef unsigned char BYTE;
typedef unsigned int DWORD;
typedef unsigned short WORD;

#define CMAT_MAGIC 122
#define CMAT_IOCTL_SET_SUBFLOW_FD1		_IOW(CMAT_MAGIC, 1, int)
#define CMAT_IOCTL_SET_SUBFLOW_FD2		_IOW(CMAT_MAGIC, 2, int)
#define CMAT_IOCTL_GET_SCHED			_IOR(CMAT_MAGIC, 3, int)
#define CMAT_IOCTL_GET_SCHED_DELAY		_IOR(CMAT_MAGIC, 4, int)
#define CMAT_IOCTL_GET_FD1_DELAY	_IOR(CMAT_MAGIC, 5, int)
#define CMAT_IOCTL_GET_FD2_DELAY        _IOR(CMAT_MAGIC, 6, int)
#define CMAT_IOCTL_GET_FD1_RTT        _IOR(CMAT_MAGIC, 7, int)
#define CMAT_IOCTL_GET_FD2_RTT        _IOR(CMAT_MAGIC, 8, int)
#define CMAT_IOCTL_GET_SCHED_BUFFER	_IOR(CMAT_MAGIC, 9, int)
#define CMAT_IOCTL_GET_SCHED_BUFFER_MRT     _IOR(CMAT_MAGIC, 12, int)
#define CMAT_IOCTL_SET_META_BUFFER	_IOW(CMAT_MAGIC, 10, int)
#define CMAT_IOCTL_GET_META_BUFFER      _IOR(CMAT_MAGIC, 20, unsigned int)
#define CMAT_IOCTL_GET_SCHED_EMPTCP	_IOR(CMAT_MAGIC, 11, int)
#define CMAT_IOCTL_SET_RESET_EMPTCP      _IOW(CMAT_MAGIC, 13, int)
// The following two are used together
#define CMAT_IOCTL_SET_SEQ_MAPPING      _IOW(CMAT_MAGIC, 14, unsigned long long)
#define CMAT_IOCTL_GET_SEQ_MAPPING	_IOR(CMAT_MAGIC, 15, unsigned long long)

#define CMAT_IOCTL_GET_FD1_ACK		_IOR(CMAT_MAGIC, 16, unsigned long long)
#define CMAT_IOCTL_GET_FD2_ACK          _IOR(CMAT_MAGIC, 17, unsigned long long)
#define CMAT_IOCTL_GET_FD1_RTTVAR        _IOR(CMAT_MAGIC, 18, int)
#define CMAT_IOCTL_GET_FD2_RTTVAR        _IOR(CMAT_MAGIC, 19, int)

#define CMAT_IOCTL_GET_FD1_CWNDTHRPT	_IOR(CMAT_MAGIC, 21, unsigned int)
#define CMAT_IOCTL_GET_FD2_CWNDTHRPT	_IOR(CMAT_MAGIC, 22, unsigned int)

#define CMAT_IOCTL_GET_FD1_AVAIL	_IOR(CMAT_MAGIC, 23, int)
#define CMAT_IOCTL_GET_FD2_AVAIL	_IOR(CMAT_MAGIC, 24, int)

#define CMAT_IOCTL_SET_DISABLE_MAPPING	_IOR(CMAT_MAGIC, 25, int)

#define CMAT_IOCTL_GET_FD1_BIF      _IOR(CMAT_MAGIC, 27, int)
#define CMAT_IOCTL_GET_FD2_BIF      _IOR(CMAT_MAGIC, 28, int)

#define CMAT_IOCTL_SET_SUBFLOW_FD3     _IOW(CMAT_MAGIC, 31, int)
#define CMAT_IOCTL_SET_SUBFLOW_FD4     _IOW(CMAT_MAGIC, 32, int)

#define CMAT_IOCTL_GET_FD3_AVAIL    _IOR(CMAT_MAGIC, 33, int)
#define CMAT_IOCTL_GET_FD4_AVAIL    _IOR(CMAT_MAGIC, 34, int)

#define CMAT_IOCTL_GET_FD3_ACK      _IOR(CMAT_MAGIC, 35, unsigned long long)
#define CMAT_IOCTL_GET_FD4_ACK          _IOR(CMAT_MAGIC, 36, unsigned long long)

#define CMAT_IOCTL_SET_BIF_REQUEST      _IOW(CMAT_MAGIC, 37, uint64_t)
#define CMAT_IOCTL_GET_BIF      _IOR(CMAT_MAGIC, 38, int)

#define CMAT_DATA_BUFFER_NAME "cmatbuf"

//#define LTE_BW 9000000

//#define BUFFER_THRESHOLD 1024000// 819200 // B

static unsigned int bufthres __read_mostly = 800;
MODULE_PARM_DESC(bufthres, "Buffer threshold in KB (800)");
module_param(bufthres, uint, 0);

static unsigned int ltebw __read_mostly = 9000000;
MODULE_PARM_DESC(ltebw, "LTE preset bandwidth in bps (9000000)");
module_param(ltebw, uint, 0);

static unsigned int wifibw __read_mostly = 0;
MODULE_PARM_DESC(wifibw, "WiFi preset bandwidth in bps (0). If 0, use online BW estimation, otherwise use preset BW.");
module_param(wifibw, uint, 0);

static unsigned int appthrpt __read_mostly = 0;
MODULE_PARM_DESC(appthrpt, "App preset bandwidth in bps (0). If 0, use online BW estimation, otherwise use preset BW.");
module_param(appthrpt, uint, 0);

static unsigned int chunks __read_mostly = 2048;
MODULE_PARM_DESC(wifibw, "Chunk size in KB (2048).");
module_param(chunks, uint, 0);

static unsigned int mapsize __read_mostly = 4096;
MODULE_PARM_DESC(mapsize, "Mapping capacity (4096)");
module_param(mapsize, uint, 0);

static unsigned int bifsize __read_mostly = 16384;
MODULE_PARM_DESC(bifsize, "BIF Mapping capacity (16384)");
module_param(bifsize, uint, 0);

struct entry {
	WORD connID;
        DWORD seq;
        DWORD tcpSeq;
};

struct bif_entry {
	DWORD tcpExpAck;
	int bif;
};

struct mapping {
	struct entry * entries;
	DWORD lastSeq;
	int head, tail;
};

struct bif_mapping {
	struct bif_entry * entries;
	DWORD lastSeq;
	int head, tail;
};

/*
struct my_data {
	int subflowNo;
        ktime_t entry_stamp;
	struct sk_buff *skb;
};*/

static int onlineBWEst = 0;

struct mapping fd1Mapping, fd2Mapping, fd3Mapping, fd4Mapping;

struct bif_mapping fd1BIFMap, fd2BIFMap;

static struct sock * sock1;
static struct sock * sock2;
static struct sock * sock3;
static struct sock * sock4;

unsigned int lastACK1, lastACK2, lastACK3, lastACK4;
unsigned int lastSEQ1, lastSEQ2, lastSEQ3, lastSEQ4;
unsigned int bDupACK1, bDupACK2, bDupACK3, bDupACK4;

static int requestSubflowNo;
static WORD requestConnID;
static DWORD requestSeq;

static DWORD requestACK;

int fd1, fd2, fd3, fd4;
int used1, used2, used3, used4;
static unsigned int metabuf;

spinlock_t	thrptlock, acklock;
static int eState = 0;
unsigned long wifiST = 0, lteST = 0;
unsigned int wifiUna, lteUna; 

unsigned long lteActivateT = 0;
unsigned long lteTxT = 0;
static unsigned long elastT = 0;
static unsigned long etxStartT = 0;
static unsigned int etxStartUna = 0;

// EMPTCP throughput prediction (Holt-Winters)
int alpha = 8, beta = 2; // * 0.1 = real alpha/beta
int wifiThrpt = -1, lteThrpt = -1; // throughput forecast (bps) for determining multipath state
int wifiLastSThrpt = -1, lteLastSThrpt = -1; //  smoothing component i and i-1
int wifiLastTrend = 0, lteLastTrend = 0;
int needInitWifi = 1, needInitLte = 1;

// EMPTCP throughput mapping
// index * 0.2 = lteThrpt (Mbps), array values are in bps.
//const int lteOnlyThres[51] = {0,48697,97394,146092,194789,243487,292184,340882,389579,438276,486974,535671,584369,633066,681764,730461,779158,827856,876553,925251,973948,1022646,1071343,1120040,1168738,1217435,1266133,1314830,1363528,1412225,1460922,1509620,1558317,1607015,1655712,1704410,1753107,1801804,1850502,1899199,1947897,1996594,2045292,2093989,2142686,2191384,2240081,2288779,2337476,2386174,2434871};
//const int wifiOnlyThres[51] = {0,119672,239345,359018,478691,598364,718036,837709,957382,1077055,1196728,1316401,1436073,1555746,1675419,1795092,1914765,2034438,2154110,2273783,2393456,2513129,2632802,2752474,2872147,2991820,3111493,3231166,3350839,3470511,3590184,3709857,3829530,3949203,4068876,4188548,4308221,4427894,4547567,4667240,4786912,4906585,5026258,5145931,5265604,5385277,5504949,5624622,5744295,5863968,5983641};

const int lteOnlyThres[126] = {0,58838,117676,176514,235352,294190,353028,411866,470705,529543,588381,647219,706057,764895,823733,882571,941410,1000248,1059086,1117924,1176762,1235600,1294438,1353276,1412115,1470953,1529791,1588629,1647467,1706305,1765143,1823981,1882820,1941658,2000496,2059334,2118172,2177010,2235848,2294686,2353525,2412363,2471201,2530039,2588877,2647715,2706553,2765391,2824230,2883068,2941906,3000744,3059582,3118420,3177258,3236096,3294935,3353773,3412611,3471449,3530287,3589125,3647963,3706802,3765640,3824478,3883316,3942154,4000992,4059830,4118668,4177507,4236345,4295183,4354021,4412859,4471697,4530535,4589373,4648212,4707050,4765888,4824726,4883564,4942402,5001240,5060078,5118917,5177755,5236593,5295431,5354269,5413107,5471945,5530783,5589622,5648460,5707298,5766136,5824974,5883812,5942650,6001488,6060327,6119165,6178003,6236841,6295679,6354517,6413355,6472193,6531032,6589870,6648708,6707546,6766384,6825222,6884060,6942899,7001737,7060575,7119413,7178251,7237089,7295927,7354765};
const int wifiOnlyThres[126] = {0,122394,244789,367183,489578,611973,734367,856762,979157,1101551,1223946,1346340,1468735,1591130,1713524,1835919,1958314,2080708,2203103,2325498,2447892,2570287,2692681,2815076,2937471,3059865,3182260,3304655,3427049,3549444,3671838,3794233,3916628,4039022,4161417,4283812,4406206,4528601,4650996,4773390,4895785,5018179,5140574,5262969,5385363,5507758,5630153,5752547,5874942,5997336,6119731,6242126,6364520,6486915,6609310,6731704,6854099,6976494,7098888,7221283,7343677,7466072,7588467,7710861,7833256,7955651,8078045,8200440,8322834,8445229,8567624,8690018,8812413,8934808,9057202,9179597,9301992,9424386,9546781,9669175,9791570,9913965,10036359,10158754,10281149,10403543,10525938,10648333,10770727,10893122,11015516,11137911,11260306,11382700,11505095,11627490,11749884,11872279,11994673,12117068,12239463,12361857,12484252,12606647,12729041,12851436,12973831,13096225,13218620,13341014,13463409,13585804,13708198,13830593,13952988,14075382,14197777,14320171,14442566,14564961,14687355,14809750,14932145,15054539,15176934,15299329};

const unsigned long interval = 200 * HZ / 1000;

static int keepMapping = 1;

void ReportError(const char * format, ...) {
	char dest[784];
	va_list argptr;
	va_start(argptr, format);
	vsprintf(dest, format, argptr);
	va_end(argptr);
	printk("[CMAT-KM] +++++ERROR+++++: %s\n", dest);
}

void ReportInfo(const char * format, ...) {
	char dest[784];
	va_list argptr;
	va_start(argptr, format);
	vsprintf(dest, format, argptr);
	va_end(argptr);
 	printk("[CMAT-KM] %s\n", dest);
}

static inline DWORD ReverseDWORD(DWORD x) {
	return
		(x & 0xFF) << 24 |
		(x & 0xFF00) << 8 |
		(x & 0xFF0000) >> 8 |
		(x & 0xFF000000) >> 24;
}

static inline WORD ReverseWORD(WORD x) {
	return
		(x & 0xFF) << 8 |
		(x & 0xFF00) >> 8;
}

static inline int ReverseINT(int x) {
	return (int)ReverseDWORD((DWORD)x);
}

/////////////////////

static unsigned int computeBufThres(unsigned int lteBW, unsigned int wifiBW, unsigned int appThrpt, unsigned int size) {
	// lteBW, wifiBW, appThrpt in kbps, size in KB
	int chunknum = 1;
	unsigned int r = 999999, size_c;
	
	if (appThrpt <= wifiBW || appThrpt >= wifiBW + lteBW) {
		return 200;
	} else {
		while (r > 4096) {
			size_c = size / chunknum;
			r = (unsigned int)((uint64_t)size_c * (uint64_t)(appThrpt - wifiBW) *
				(uint64_t)(lteBW + wifiBW - appThrpt) /
				(uint64_t)lteBW / (uint64_t)appThrpt);
			chunknum++;
		}
		return r;
	}
}

void initializeMapping(void) {
	// tail - 1 points to the last entry
	fd1Mapping.head = 0;
	fd1Mapping.tail = 0;
	fd2Mapping.head = 0;
        fd2Mapping.tail = 0;
    fd3Mapping.head = 0;
    fd3Mapping.tail = 0;
    fd4Mapping.head = 0;
    fd4Mapping.tail = 0;
	fd1Mapping.entries = kcalloc(mapsize, sizeof(struct entry), GFP_KERNEL);
	fd2Mapping.entries = kcalloc(mapsize, sizeof(struct entry), GFP_KERNEL);
    fd3Mapping.entries = kcalloc(mapsize, sizeof(struct entry), GFP_KERNEL);
    fd4Mapping.entries = kcalloc(mapsize, sizeof(struct entry), GFP_KERNEL);
}

void initializeBIFMapping(void) {
	fd1BIFMap.head = 0;
	fd1BIFMap.tail = 0;
	fd2BIFMap.head = 0;
	fd2BIFMap.tail = 0;
	fd1BIFMap.entries = kcalloc(bifsize, sizeof(struct bif_entry), GFP_KERNEL);
	fd2BIFMap.entries = kcalloc(bifsize, sizeof(struct bif_entry), GFP_KERNEL);
}

void freeMapping(void) {
	kfree(fd1Mapping.entries);
	kfree(fd2Mapping.entries);
    kfree(fd3Mapping.entries);
    kfree(fd4Mapping.entries);
}

void freeBIFMapping(void) {
	kfree(fd1BIFMap.entries);
	kfree(fd2BIFMap.entries);
}

struct mapping * getMappingPointer(int subflowNo) {
	switch (subflowNo) {
	case 1:
		return &fd1Mapping;
	case 2:
		return &fd2Mapping;
    case 3:
        return &fd3Mapping;
    case 4:
        return &fd4Mapping;
	default:
		return NULL;
	}
}

struct bif_mapping * getBIFMappingPointer(int subflowNo) {
	switch (subflowNo) {
	case 1:
		return &fd1BIFMap;
	case 2:
		return &fd2BIFMap;
	default:
		return NULL;
	}
}

int getMappingOccupancy(int subflowNo) {
	struct mapping * m = getMappingPointer(subflowNo);
	// Maximum occupancy: mapsize - 1
	if (m->tail >= m->head) return (m->tail - m->head);
	return (m->tail + mapsize - m->head);
}

int getBIFMappingOccupancy(int subflowNo) {
	struct bif_mapping * m = getBIFMappingPointer(subflowNo);
	// Maximum occupancy: mapsize - 1
	if (m->tail >= m->head) return (m->tail - m->head);
	return (m->tail + bifsize - m->head);
}

void printMapping(int subflowNo) {
	struct mapping * m = getMappingPointer(subflowNo);
	int end = m->tail;

	if (end < m->head) {
		end += mapsize;
	}
/*
	ReportInfo("------ Mapping %i (%d entries): -------", subflowNo, getMappingOccupancy(subflowNo));
	for (i = m->head; i < end; i++) {
		ReportInfo("%d: (%u, %u) -> %u", i % mapsize, m->entries[i%mapsize].connID, m->entries[i%mapsize].seq, m->entries[i%mapsize].tcpSeq);
	}
	ReportInfo("----------------------------------------");
*/
}

void addEntry(int subflowNo, WORD connID, DWORD seq, DWORD tcpSeq) {
	struct mapping * m = getMappingPointer(subflowNo);

	int index = m->tail;

	// Discard retransmitted packets when adding entry
	if (m->head != m->tail) {
		if (tcpSeq <= m->lastSeq)
			return;
	}
	m->entries[index].connID = connID;
	m->entries[index].seq = seq;
	m->entries[index].tcpSeq = tcpSeq;
	m->lastSeq = tcpSeq;
	// m.tail increment
	m->tail = (m->tail + 1) % mapsize;
}

struct tcp_sock * getTPSocket(int subflowNo) {
	switch (subflowNo) {
	case 1:
		return tcp_sk(sock1);
	case 2:
		return tcp_sk(sock2);
	default:
		return NULL;
	}
}

// ack1 after ack2 (ack2 < ack1): return 1
int ackAfter(uint32_t ack1, uint32_t ack2) {
	int64_t tmp1 = (int64_t) ack1;
	int64_t tmp2 = (int64_t) ack2;

	if (tmp1 - tmp2 > 0 && tmp1 - tmp2 < 33554432) {
		return 1;
	}

	if (tmp1 < tmp2 && tmp1 + 4294967296 - tmp2 > 0 && tmp1 + 4294967296 - tmp2 < 33554432) {
		return 1;
	}

	return 0;
}

void addBIFEntry(int subflowNo, DWORD tcpSeq) {
	struct bif_mapping * m = getBIFMappingPointer(subflowNo);
	const struct tcp_sock *tp = getTPSocket(subflowNo);

	int index = m->tail;

	// Discard retransmitted packets when adding entry
	if (m->head != m->tail) {
		if (tcpSeq != m->lastSeq && ackAfter(tcpSeq, m->lastSeq) == 0)//if (tcpSeq < m->lastSeq)
			return;
	}

	m->entries[index].tcpExpAck = tcpSeq;
	m->entries[index].bif = tcp_packets_in_flight(tp) * tp->mss_cache;

	m->lastSeq = tcpSeq;
	// m.tail increment
	m->tail = (m->tail + 1) % mapsize;
}

void addMapping(struct sk_buff *skb, int subflowNo) {
	// Assuming TCP packets and each message is in exactly one packet
	if (getMappingOccupancy(subflowNo) == mapsize - 1) {
		//ReportInfo("Mapping %d full, skip skb entry.", subflowNo);
		return;
	}

	char * data;
	data = (char *)skb->data;
	WORD connID = ntohs((WORD)data[0] * 256 + (WORD)data[1]);
    DWORD seq = ntohl((DWORD)data[2] * 16777216 + (DWORD)data[3] * 65536 + (DWORD)data[4] * 256 + (DWORD)data[5]);
	
	//ReportInfo("Subflow %d, len: %u, TCP SEQ: %u, ConnID: %u, Seq: %u, data: %d %d %d %d %d %d %d %d %d %d %d %d", subflowNo, skb->len, TCP_SKB_CB(skb)->seq, connID, seq, (int)data[0], (int)data[1], (int)data[2], (int)data[3], (int)data[4], (int)data[5], (int)data[6], (int)data[7], (int)data[8], (int)data[9], (int)data[10], (int)data[11]);
	
	// Discard retransmitted packets when adding entry
	addEntry(subflowNo, connID, seq, TCP_SKB_CB(skb)->seq);
}

void addBIFMapping(struct sk_buff *skb, int subflowNo) {
	// Assuming TCP packets and each message is in exactly one packet
	if (getBIFMappingOccupancy(subflowNo) == bifsize - 1) {
		//ReportInfo("Mapping %d full, skip skb entry.", subflowNo);
		return;
	}	
	//ReportInfo("Subflow %d, len: %u, TCP SEQ: %u, ConnID: %u, Seq: %u, data: %d %d %d %d %d %d %d %d %d %d %d %d", subflowNo, skb->len, TCP_SKB_CB(skb)->seq, connID, seq, (int)data[0], (int)data[1], (int)data[2], (int)data[3], (int)data[4], (int)data[5], (int)data[6], (int)data[7], (int)data[8], (int)data[9], (int)data[10], (int)data[11]);
	
	// Discard retransmitted packets when adding entry
	addBIFEntry(subflowNo, TCP_SKB_CB(skb)->seq);
}


unsigned long long getMapping(void) {
	// Assuming the query always corresponds to the head entry (query all packets in order)
	struct mapping * m = getMappingPointer(requestSubflowNo);
	int head = m->head;
	unsigned long long r = 0;

	while (head != m->tail) {
		if (m->entries[head].connID == requestConnID &&
			m->entries[head].seq == requestSeq) {
			r = (unsigned long long)m->entries[head].tcpSeq;
			m->head = (head + 1) % mapsize;
			return r;
		}
		if (m->entries[head].connID == requestConnID &&
			m->entries[head].seq > requestSeq) {
			break;
		}
		head = (head + 1) % mapsize;
	}

    //ReportInfo("Mapping not found: subflow %d, connID %u, seq %u",
    //                requestSubflowNo, requestConnID, requestSeq);
    return (unsigned long long)1 << 48;
}

int getBIFMapping(void) {
	// Assuming the query always corresponds to the head entry (query all packets in order)
	struct bif_mapping * m = getBIFMappingPointer(requestSubflowNo);
	int head = m->head;
	int r = 0;

	while (head != m->tail) {
		if (m->entries[head].tcpExpAck == requestACK) {
			r = m->entries[head].bif;
			m->head = (head + 1) % mapsize;
			return r;
		}
		if (ackAfter(m->entries[head].tcpExpAck, requestACK) == 1) {//if (m->entries[head].tcpExpAck > requestACK) {
			break;
		}
		head = (head + 1) % bifsize;
	}

    //ReportInfo("Mapping not found: subflow %d, connID %u, seq %u",
    //                requestSubflowNo, requestConnID, requestSeq);
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
static int cmat_data_open(struct inode * inode, struct file * file) {
	return 0;
}

static int cmat_data_mmap(struct file *filp, struct vm_area_struct *vma)
{
   return 0;
}

/*
static void print_tcp_sock_info(struct tcp_sock *tp1, struct tcp_sock *tp2) {
	ReportInfo("[tp1] snd_cwnd: %d, snd_wnd: %d, in_flight: %d; [tp2] snd_cwnd: %d, snd_wnd: %d, in_flight: %d\n",
			tp1->snd_cwnd, tp1->snd_wnd, tcp_packets_in_flight(tp1),
			tp2->snd_cwnd, tp2->snd_wnd, tcp_packets_in_flight(tp2));
}*/

static int mptcp2_is_temp_unavailable(struct sock *sk) {
	const struct tcp_sock *tp = tcp_sk(sk);
        unsigned int mss_now, space, in_flight;//, delta, x;

        if (inet_csk(sk)->icsk_ca_state == TCP_CA_Loss) {
                if (!tcp_is_reno(tp)) {
                        return 1;
                }
                else if (tp->snd_una != tp->high_seq) {
                        return 2;
                }
        }

	if (test_bit(TSQ_THROTTLED, &tp->tsq_flags)) {
                return 3;
        }

        in_flight = tcp_packets_in_flight(tp);
	space = (tp->snd_cwnd - in_flight + 3) * tp->mss_cache;

        if (/*space <= 0 || */tp->write_seq - tp->snd_nxt > space) {
                return 5;
        }

        mss_now = tcp_current_mss(sk);
	if (/*skb && !zero_wnd_test &&*/
            after(tp->write_seq + mss_now /*min(skb->len, mss_now)*/, tcp_wnd_end(tp))) {
                return 6;
        }

        return 0;
}

static int mptcp_is_temp_unavailable(struct sock *sk)
				      //const struct sk_buff *skb,
				      //bool zero_wnd_test)
{
	const struct tcp_sock *tp = tcp_sk(sk);
	unsigned int mss_now, space, in_flight;//, delta, x;
	unsigned int delta;
	
	if (inet_csk(sk)->icsk_ca_state == TCP_CA_Loss) {
		/* If SACK is disabled, and we got a loss, TCP does not exit
		 * the loss-state until something above high_seq has been
		 * acked. (see tcp_try_undo_recovery)
		 *
		 * high_seq is the snd_nxt at the moment of the RTO. As soon
		 * as we have an RTO, we won't push data on the subflow.
		 * Thus, snd_una can never go beyond high_seq.
		 */
		
		if (!tcp_is_reno(tp)) {
			//ReportInfo("[temp una] 1 tcp_is_reno: false");
			return 1;
		}
		else if (tp->snd_una != tp->high_seq) {
			//ReportInfo("[temp una] 2 %u %u", tp->snd_una, tp->high_seq);
			return 2;
		}
		
	}

/*
	if (!tp->mptcp->fully_established) {
		// Make sure that we send in-order data 
		if (skb && tp->mptcp->second_packet &&
		    tp->mptcp->last_end_data_seq != TCP_SKB_CB(skb)->seq)
			return true;
	}
*/

	/* If TSQ is already throttling us, do not send on this subflow. When
	 * TSQ gets cleared the subflow becomes eligible again.
	 */
	if (test_bit(TSQ_THROTTLED, &tp->tsq_flags)) {
		//ReportInfo("[temp una] 3 tsq throttled");
		return 3;
	}

	in_flight = tcp_packets_in_flight(tp);
	
	//struct timespec tv;
	//getnstimeofday(&tv);
	//ReportInfo("[temp una] 4 %u %u %u %u %u %u %lu.%09lu", in_flight, tp->snd_cwnd, tp->packets_out, tp->sacked_out, tp->lost_out, tp->retrans_out, (unsigned long)tv.tv_sec, (unsigned long)tv.tv_nsec);
	/* Not even a single spot in the cwnd */
	//if (in_flight >= tp->snd_cwnd) {
		//ReportInfo("Warning: in_flight (%d) > cnwd (%d)", in_flight, tp->snd_cwnd);
	//	return 4;
	//}

	/* Now, check if what is queued in the subflow's send-queue
	 * already fills the cwnd.
	 */

	//delta = tp->snd_cwnd >> 2;
	//if (delta > 2) delta = 2;
    delta = 0;
    //delta = 10;
	space = (tp->snd_cwnd - in_flight + delta) * tp->mss_cache;
	
	//delta = tp->snd_cwnd * tp->mss_cache;

	//delta = (tp->snd_cwnd * tp->mss_cache) >> 4;
	//delta += 15 * tp->mss_cache;
	//space += delta;

	//space = tp->snd_cwnd * tp->mss_cache + ((tp->snd_cwnd * tp->mss_cache) >> 1);
	//if (tp->write_seq - tp->snd_una > space) {
	//x = (tp->write_seq - tp->snd_una) / tp->mss_cache;
	//ReportInfo("[temp una] 5 %u %u %u %u %u", tp->write_seq - tp->snd_nxt, space,
	//		tp->write_seq - tp->snd_una, x, (unsigned int)(x > tp->snd_cwnd));

	if (/*space <= 0 || */tp->write_seq - tp->snd_nxt > space) {
		//ReportInfo("[temp una] 5 %u %u", tp->write_seq - tp->snd_nxt, space);
		return 5;
	}

/*
	if (zero_wnd_test && !before(tp->write_seq, tcp_wnd_end(tp)))
		return true;
*/

	mss_now = tcp_current_mss(sk);

	/* Don't send on this subflow if we bypass the allowed send-window at
	 * the per-subflow level. Similar to tcp_snd_wnd_test, but manually
	 * calculated end_seq (because here at this point end_seq is still at
	 * the meta-level).
	 */
	
	/*
	if (skb && !zero_wnd_test &&
	    after(tp->write_seq + min(skb->len, mss_now), tcp_wnd_end(tp)))
		return true;
	*/
	
	if (/*skb && !zero_wnd_test &&*/
	    after(tp->write_seq + mss_now /*min(skb->len, mss_now)*/, tcp_wnd_end(tp))) {
		//ReportInfo("[temp una] 6 %u %u %u", tp->write_seq, mss_now, tcp_wnd_end(tp));
		return 6;
	}
	
	return 0;
}

static int tcp_available_space(struct sock *sk)
				      //const struct sk_buff *skb,
				      //bool zero_wnd_test)
{

	const struct tcp_sock *tp = tcp_sk(sk);
	unsigned int mss_now, space, in_flight;//, delta, x;
	unsigned int delta;
	
	if (inet_csk(sk)->icsk_ca_state == TCP_CA_Loss) {
		/* If SACK is disabled, and we got a loss, TCP does not exit
		 * the loss-state until something above high_seq has been
		 * acked. (see tcp_try_undo_recovery)
		 *
		 * high_seq is the snd_nxt at the moment of the RTO. As soon
		 * as we have an RTO, we won't push data on the subflow.
		 * Thus, snd_una can never go beyond high_seq.
		 */
		
		if (!tcp_is_reno(tp)) {

			ReportInfo("[avail-space temp una] 1 tcp_is_reno: false");
			return 0;
		}
		else if (tp->snd_una != tp->high_seq) {
			ReportInfo("[avail-space temp una] 2 %u %u", tp->snd_una, tp->high_seq);

			return 0;
		}
		
	}

/*
	if (!tp->mptcp->fully_established) {
		// Make sure that we send in-order data 
		if (skb && tp->mptcp->second_packet &&
		    tp->mptcp->last_end_data_seq != TCP_SKB_CB(skb)->seq)
			return true;
	}
*/

	/* If TSQ is already throttling us, do not send on this subflow. When
	 * TSQ gets cleared the subflow becomes eligible again.
	 */
	if (test_bit(TSQ_THROTTLED, &tp->tsq_flags)) {
		//ReportInfo("[avail-space temp una] 3 tsq throttled");
		//return 0;
	}

	in_flight = tcp_packets_in_flight(tp);
	
	//struct timespec tv;
	//getnstimeofday(&tv);
	// ReportInfo("[temp una] 4 %u %u %u %u %u %u %lu.%09lu", in_flight, tp->snd_cwnd, tp->packets_out, 
	// 	tp->sacked_out, tp->lost_out, tp->retrans_out, (unsigned long)tv.tv_sec, (unsigned long)tv.tv_nsec);
	// ReportInfo("[temp una] 4 %u %u", in_flight, tp->snd_cwnd);

	/* Not even a single spot in the cwnd */
	//if (in_flight >= tp->snd_cwnd) {
		//ReportInfo("Warning: in_flight (%d) > cnwd (%d)", in_flight, tp->snd_cwnd);
	//	return 4;
	//}

	/* Now, check if what is queued in the subflow's send-queue
	 * already fills the cwnd.
	 */


	delta = tp->snd_cwnd >> 2;
	if (delta > 2) delta = 2;

    // delta = 4;
    // delta = 10;
    // space = (tp->snd_cwnd - in_flight) * tp->mss_cache;
	space = (tp->snd_cwnd - in_flight + delta) * tp->mss_cache;
	
	//delta = tp->snd_cwnd * tp->mss_cache;

	//delta = (tp->snd_cwnd * tp->mss_cache) >> 4;
	//delta += 15 * tp->mss_cache;
	//space += delta;

	//space = tp->snd_cwnd * tp->mss_cache + ((tp->snd_cwnd * tp->mss_cache) >> 1);
	//if (tp->write_seq - tp->snd_una > space) {
	//x = (tp->write_seq - tp->snd_una) / tp->mss_cache;
	//ReportInfo("[temp una] 5 %u %u %u %u %u", tp->write_seq - tp->snd_nxt, space,
	//		tp->write_seq - tp->snd_una, x, (unsigned int)(x > tp->snd_cwnd));

    if (in_flight * tp->mss_cache > tp->snd_nxt-tp->snd_una) {
        space += (in_flight * tp->mss_cache - (tp->snd_nxt-tp->snd_una));
    }

	if (/*space <= 0 || */tp->write_seq - tp->snd_nxt > space) {

		ReportInfo("[avail-space temp una] 5 write-nxt=%u snd=%u cwnd=%u inflight=%u space=%u delta=%d", 
			tp->write_seq - tp->snd_nxt, tp->snd_nxt-tp->snd_una, tp->snd_cwnd * tp->mss_cache, 
			in_flight*tp->mss_cache, space, in_flight * tp->mss_cache - (tp->snd_nxt-tp->snd_una));

		return 0;
	}

/*
	if (zero_wnd_test && !before(tp->write_seq, tcp_wnd_end(tp)))
		return true;
*/
	// mss_now = tp->mss_cache;
	mss_now = tcp_current_mss(sk);

	/* Don't send on this subflow if we bypass the allowed send-window at
	 * the per-subflow level. Similar to tcp_snd_wnd_test, but manually
	 * calculated end_seq (because here at this point end_seq is still at
	 * the meta-level).
	 */
	
	/*
	if (skb && !zero_wnd_test &&
	    after(tp->write_seq + min(skb->len, mss_now), tcp_wnd_end(tp)))
		return true;
	*/
	
	if (/*skb && !zero_wnd_test &&*/
	    after(tp->write_seq /*min(skb->len, mss_now)*/, tcp_wnd_end(tp))) {
		//ReportInfo("[avail-space temp una] 6 %u %u %u", tp->write_seq, mss_now, tcp_wnd_end(tp));

		// return 0;
	}
	
    // ReportInfo("[avail-space] %u %u %u", space, tp->write_seq - tp->snd_nxt, space - (tp->write_seq - tp->snd_nxt));
	return (space - (tp->write_seq - tp->snd_nxt));
}


// static int tcp_available_space(struct sock *sk)
// 				      //const struct sk_buff *skb,
// 				      //bool zero_wnd_test)
// {
// 	// ReportInfo("[tcp_available_space]");

// 	const struct tcp_sock *tp = tcp_sk(sk);
// 	unsigned int mss_now, space, in_flight;//, delta, x;
// 	unsigned int delta;
	
// 	if (inet_csk(sk)->icsk_ca_state == TCP_CA_Loss) {
// 		/* If SACK is disabled, and we got a loss, TCP does not exit
// 		 * the loss-state until something above high_seq has been
// 		 * acked. (see tcp_try_undo_recovery)
// 		 *
// 		 * high_seq is the snd_nxt at the moment of the RTO. As soon
// 		 * as we have an RTO, we won't push data on the subflow.
// 		 * Thus, snd_una can never go beyond high_seq.
// 		 */
		
// 		if (!tcp_is_reno(tp)) {
// 			// ReportInfo("[avail-space temp una] 1 tcp_is_reno: false");
// 			return 0;
// 		}
// 		else if (tp->snd_una != tp->high_seq) {
// 			// ReportInfo("[avail-space temp una] 2 %u %u", tp->snd_una, tp->high_seq);
// 			return 0;
// 		}
		
// 	}

// /*
// 	if (!tp->mptcp->fully_established) {
// 		// Make sure that we send in-order data 
// 		if (skb && tp->mptcp->second_packet &&
// 		    tp->mptcp->last_end_data_seq != TCP_SKB_CB(skb)->seq)
// 			return true;
// 	}
// */

// 	/* If TSQ is already throttling us, do not send on this subflow. When
// 	 * TSQ gets cleared the subflow becomes eligible again.
// 	 */
// 	if (test_bit(TSQ_THROTTLED, &tp->tsq_flags)) {
// 		//ReportInfo("[avail-space temp una] 3 tsq throttled");
// 		return 0;
// 	}

// 	in_flight = tcp_packets_in_flight(tp);
	
// 	//struct timespec tv;
// 	//getnstimeofday(&tv);
// 	// ReportInfo("[temp una] 4 %u %u %u %u %u %u %lu.%09lu", in_flight, tp->snd_cwnd, tp->packets_out, 
// 	// 	tp->sacked_out, tp->lost_out, tp->retrans_out, (unsigned long)tv.tv_sec, (unsigned long)tv.tv_nsec);
// 	// ReportInfo("[temp una] 4 %u %u", in_flight, tp->snd_cwnd);
// 	/* Not even a single spot in the cwnd */
// 	if (in_flight >= tp->snd_cwnd) {
// 		ReportInfo("Warning: in_flight (%d) > cnwd (%d)", in_flight, tp->snd_cwnd);
// 		return 0;
// 	}

// 	/* Now, check if what is queued in the subflow's send-queue
// 	 * already fills the cwnd.
// 	 */

// 	delta = tp->snd_cwnd >> 2;
// 	if (delta > 2) delta = 2;
// 	delta = 0;
//     //delta = 4;
// 	space = (tp->snd_cwnd - in_flight + delta) * tp->mss_cache;
	
// 	//delta = tp->snd_cwnd * tp->mss_cache;

// 	//delta = (tp->snd_cwnd * tp->mss_cache) >> 4;
// 	//delta += 15 * tp->mss_cache;
// 	//space += delta;

// 	//space = tp->snd_cwnd * tp->mss_cache + ((tp->snd_cwnd * tp->mss_cache) >> 1);
// 	//if (tp->write_seq - tp->snd_una > space) {
// 	//x = (tp->write_seq - tp->snd_una) / tp->mss_cache;
// 	//ReportInfo("[temp una] 5 %u %u %u %u %u", tp->write_seq - tp->snd_nxt, space,
// 	//		tp->write_seq - tp->snd_una, x, (unsigned int)(x > tp->snd_cwnd));

//     // if (in_flight * tp->mss_cache > tp->snd_nxt-tp->snd_una) {
//     //     space += (in_flight * tp->mss_cache - (tp->snd_nxt-tp->snd_una));
//     // }

// 	if (/*space <= 0 || */tp->write_seq - tp->snd_nxt > space) {
// 		// ReportInfo("[avail-space temp una] 5 write-nxt=%u snd=%u cwnd=%u inflight=%u space=%u delta=%d", 
// 		// 	tp->write_seq - tp->snd_nxt, tp->snd_nxt-tp->snd_una, tp->snd_cwnd * tp->mss_cache, 
// 		// 	in_flight*tp->mss_cache, space, in_flight * tp->mss_cache - (tp->snd_nxt-tp->snd_una));
// 		return 0;
// 	}


// 	if (/*zero_wnd_test &&*/ !before(tp->write_seq, tcp_wnd_end(tp)))
// 		return 0;


// 	mss_now = tcp_current_mss(sk);

// 	/* Don't send on this subflow if we bypass the allowed send-window at
// 	 * the per-subflow level. Similar to tcp_snd_wnd_test, but manually
// 	 * calculated end_seq (because here at this point end_seq is still at
// 	 * the meta-level).
// 	 */
	
	
// 	// if (skb && !zero_wnd_test &&
// 	//     after(tp->write_seq + min(skb->len, mss_now), tcp_wnd_end(tp)))
// 	// 	return 0;
	
	
// 	if (/*skb && !zero_wnd_test &&*/
// 	    after(tp->write_seq /*min(skb->len, mss_now)*/, tcp_wnd_end(tp))) {
// 		//ReportInfo("[avail-space temp una] 6 %u %u %u", tp->write_seq, mss_now, tcp_wnd_end(tp));
// 		return 0;
// 	}
	
//     // ReportInfo("[avail-space] %u %u %u", space, 
//     //        tp->write_seq - tp->snd_nxt, space - (tp->write_seq - tp->snd_nxt));
// 	return (space - (tp->write_seq - tp->snd_nxt));
// }

static struct sock * GetTCPSocket(int fd) {
			int err;
			struct socket *sock = sockfd_lookup(fd, &err);
			if (sock == NULL) {
				ReportError("Cannot find socket with FD %d", fd);
				return NULL;
			}
			
			if (sock->sk == NULL) {
				ReportError("sock field is empty (FD=%d)", fd);
				return NULL;
			}
			
			return sock->sk;
}

static int GetSpace(struct sock *sk) {
        const struct tcp_sock *tp = tcp_sk(sk);
	int space = (tp->snd_cwnd - tcp_packets_in_flight(tp)) * tp->mss_cache;
	if (space < 0) return 0;
	else return space;
}

static unsigned int GetThrptSample(int isWifi, unsigned int una) {
// use Holt-Winters forecasting
	int thrpt = 0;
	int tmplast = 0, thrpt_backup = 0;
	if (isWifi) {
		if (onlineBWEst) {
			wifiThrpt = wifibw;
			return wifibw;
		}
		thrpt = (una > wifiUna)? una - wifiUna: 0;
		thrpt = thrpt * 8 * HZ / interval;	
		wifiUna = una;
		if (thrpt <= 0) {
			return 0;
		}
		if (wifiLastSThrpt < 0) {
			wifiLastSThrpt = thrpt;
			wifiThrpt = thrpt;
			needInitWifi = 1;
		} else {
			if (needInitWifi) {
				needInitWifi = 0;
				wifiLastTrend = thrpt - wifiLastSThrpt;
			}
			tmplast = alpha * thrpt + (10 - alpha) * (wifiLastSThrpt + wifiLastTrend);
			tmplast /= 10;
			wifiLastTrend = beta * (tmplast - wifiLastSThrpt) + (10 - beta) * wifiLastTrend;
			wifiLastTrend /= 10;
			wifiLastSThrpt = tmplast;
			thrpt_backup = wifiThrpt;
			wifiThrpt = tmplast + wifiLastTrend;
			if (wifiThrpt < 0)
				wifiThrpt = thrpt_backup;
		}
	} else {
		if (onlineBWEst) {
                        lteThrpt = ltebw;
                        return ltebw;
                }
		thrpt = (una > lteUna)? una - lteUna: 0;
		thrpt = thrpt * 8 * HZ / interval;
		lteUna = una;
		if (thrpt <= 0) {
                        if (lteThrpt > 0)
				return lteThrpt;
			else
				return ltebw;
		}
		if (eState == 0) {
			return thrpt;
		}
		if (lteLastSThrpt < 0) {
			lteLastSThrpt = thrpt;
			lteThrpt = thrpt;
			needInitLte = 1;
		} else {
			if (needInitLte) {
                                needInitLte = 0;
                                lteLastTrend = thrpt - lteLastSThrpt;
                        }
                        tmplast = alpha * thrpt + (10 - alpha) * (lteLastSThrpt + lteLastTrend);
			tmplast /= 10;
                        lteLastTrend = beta * (tmplast - lteLastSThrpt) + (10 - beta) * lteLastTrend;
			lteLastTrend /= 10;
                        lteLastSThrpt = tmplast;
			thrpt_backup = lteThrpt;
                        lteThrpt = tmplast + lteLastTrend;
			if (lteThrpt < 0)
				lteThrpt = thrpt_backup;;
		}
	}
	return thrpt;
}

//state: 0 WiFi only, 1 WiFi and LTE, 2 LTE only
static int UpdateEMPTCPState(void) {
	int wifi, lte, index = 0;
	if (wifiThrpt < 0) wifi = 10000000;
	else wifi = wifiThrpt;

	if (lteThrpt <= 0) { 
		lte = ltebw;
		lteThrpt = ltebw;
	}
	else lte = lteThrpt;
	
	for (index = 0; index <= 50; index++) {
		if (index * 200000 >= lte)
			break;
	}
	//ReportInfo("Index: %d", index);
	if (eState == 0) {
		if (wifi < wifiOnlyThres[index] * 9 / 10) {
			eState = 1;
			return 1;
		}
		return 0;
	}
	if (eState == 1) {
		if (jiffies - lteActivateT < 2 * HZ) {
			//ReportInfo("Multipath: LTE probe BW");
			eState = 1;
			return 1;
		}
		if (wifi >= wifiOnlyThres[index] * 11 / 10 ) {
			eState = 0;
			return 0;
		}
		if (wifi < lteOnlyThres[index] * 9 / 10) {
			eState = 2;
			return 2;
		}
		return 1;
	}
	if (eState == 2) {
		if (jiffies - lteActivateT < HZ) {
			//ReportInfo("LTE only: LTE probe BW");
			eState = 2;
			return 2;
		}
		if (wifi >= lteOnlyThres[index] * 11 / 10) {
			eState = 1;
			return 1;
		}
		return 2;
	}
	return -1;
}

static int Schedule(void) {
	
	static unsigned long lastT = 0;
	struct tcp_sock * tp1 = tcp_sk(sock1);
    struct tcp_sock * tp2 = tcp_sk(sock2);
	
	/*	
	if (jiffies - lastT > HZ) {
		ReportInfo("Reset (used) ssthresh");
		used1 = used2 = 0;
		tp1->snd_cwnd = 10;
		tp2->snd_cwnd = 10;
	}
	*/
	lastT = jiffies;
	/*
	if (!used1) {
		used1 = 1;
		ReportInfo("1 not used");
		return 1;
	}
	
	if (!used2) {
		used2 = 1;
		ReportInfo("2 not used");
		return 2;
	}
	*/

	
	//print_tcp_sock_info(tp1, tp2);
	//ReportInfo("una1");
	int una1 = mptcp_is_temp_unavailable(sock1);
	//ReportInfo("una2");
	int una2 = mptcp_is_temp_unavailable(sock2);
	
		
	//ReportInfo("una1=%d una2=%d srtt1=%u srtt2=%u", una1, una2, tp1->srtt_us, tp2->srtt_us);
	
	if (una1 && una2) return 0;
	if (una1) return 2;
	if (una2) return 1;

	if (tp1->srtt_us < tp2->srtt_us) 
		return 1; 
	else
		return 2;	
}

static int EstimateDelay(struct tcp_sock *tp) {
	int delay = (tp->srtt_us >> 3) * 100000  / (tp->snd_cwnd * tp->mss_cache);
	int p = -1;
	if (tp == tcp_sk(sock1)) p = 1;
	else if (tp == tcp_sk(sock2)) p = 2;
	//ReportInfo("kernel%d delay=%d srtt=%u inflight=%u cwnd=%u mss=%u buf=%u ssthresh=%u", p, delay, tp->srtt_us, tcp_packets_in_flight(tp), tp->snd_cwnd, tp->mss_cache, tp->write_seq - tp->snd_una, tp->snd_ssthresh);
        return delay;
}
/*
static int EstimateBW(struct tcp_sock *tp) {
	return (tp->snd_cwnd * tp->mss_cache * 8000) / (tp->srtt_us >> 3);
}*/

/*
static int GetBufferNotInCwnd(struct tcp_sock *tp) {
	return tp->write_seq - tp->snd_nxt;
}
*/

static int ScheduleDelay(void) {
	struct tcp_sock * tp1 = tcp_sk(sock1);
        struct tcp_sock * tp2 = tcp_sk(sock2);
	
	int delay1 = 0;//EstimateDelay(fd1, tp1);
	int delay2 = 0;//EstimateDelay(fd2, tp2);
	

	//ReportInfo("kernel delay1=%d delay2=%d srtt1=%u srtt2=%u inflight1=%u inflight2=%u cwnd1=%u cwnd2= %u mss1=%u mss2=%u", delay1, delay2, tp1->srtt_us, tp2->srtt_us, tcp_packets_in_flight(tp1), tcp_packets_in_flight(tp2), tp1->snd_cwnd, tp2->snd_cwnd, tp1->mss_cache, tp2->mss_cache);	

	if (delay1 < delay2)	return 1;
	else if (delay1 > delay2)	return 2;
	else {
		if (tp1->srtt_us < tp2->srtt_us)
                	return 1;
        	else
                	return 2;
	}
	return 0; 
}

/*
static int ScheduleBW(void) {
	struct tcp_sock * tp1 = tcp_sk(sock1);
        struct tcp_sock * tp2 = tcp_sk(sock2);

	//if ()
}*/

static void ResetEMPTCP(void) {
	struct tcp_sock * tp1 = tcp_sk(sock1);
	etxStartT = jiffies;
	etxStartUna = tp1->snd_una;
}

static int ScheduleEMPTCP(void) {
	struct tcp_sock * tp1 = tcp_sk(sock1);
        struct tcp_sock * tp2 = tcp_sk(sock2);
	int una1, una2;
	int r = 0;

	// determine state (in jtcp_transmit_skb)
	if (jiffies - elastT > 2 * HZ) {
                etxStartT = jiffies;
		etxStartUna = tp1->snd_una;
        }
        elastT = jiffies;

	if (jiffies - etxStartT < 3 * HZ) {
                eState = 0;
        } else {
		//ReportInfo("Check: %u %u %u", tp1->snd_una,
                //                etxStartUna, tp1->snd_una - etxStartUna);
                if (tp1->snd_una - etxStartUna < 1048576) {
			//ReportInfo("Stay on WiFi: %u %u %u", tp1->snd_una, 
			//	etxStartUna, tp1->snd_una - etxStartUna);
			eState = 0;
		}
        }

	// WiFi only
	if (eState == 0) {
		una1 = mptcp_is_temp_unavailable(sock1);
		if (una1) r = 0;
		else r = 1;
	}
	// WiFi and LTE
	else if (eState == 1) {
		una1 = mptcp_is_temp_unavailable(sock1);
        	una2 = mptcp_is_temp_unavailable(sock2);

        	if (una1 && una2) r = 0;
        	else if (una1) r = 2;
        	else if (una2) r = 1;
		else {
        		if (tp1->srtt_us < tp2->srtt_us)
                		r = 1;
        		else
                		r = 2;
		}
	}
	// LTE only
	else if (eState == 2) {
		una2 = mptcp_is_temp_unavailable(sock2);
		if (una2) r = 0;
		else r = 2;
	}

	if (r == 1) {
		if (wifiST == 0) {
			wifiST = jiffies;
			wifiUna = tp1->snd_una;
		}
		return 1;
	}
	if (r == 2) {
		if (lteST == 0) {
			lteST = jiffies;
			lteUna = tp2->snd_una;
		}
		if (lteTxT == 0) {
			lteTxT = jiffies;
		}
		if (jiffies - lteTxT > HZ) {
			lteActivateT = jiffies;
		}
		lteTxT = jiffies;
		if (lteActivateT == 0)
			lteActivateT = jiffies;
		return 2;
	}
	return 0;
}

static int ScheduleBuffer(int isMRT) {
	static unsigned long lastT = 0;
	static unsigned long lastWiFiStart = 0;
	static unsigned int lastOverThres = 0;
	static unsigned long startOverThresT = 0;
	static unsigned int state = 0; // 0: Wifi Only, 1: multipath 
	struct tcp_sock * tp1 = tcp_sk(sock1);
        struct tcp_sock * tp2 = tcp_sk(sock2);
	
	unsigned int space1 = GetSpace(sock1), space2 = GetSpace(sock2);
	
	//int buf1 = GetBufferNotInCwnd(tp1), buf2 = GetBufferNotInCwnd(tp2);
	unsigned int thres1 = 20000, thres2 = bufthres * 1024;//2 * tp1->snd_cwnd * tp1->mss_cache;
	
	if (jiffies - lastT > HZ) {
                //ReportInfo("Reset state");
		state = 0;
		lastWiFiStart = jiffies;
        }
	lastT = jiffies;
	unsigned int mrtAlpha = 0;
	if (isMRT > 0) {
		mrtAlpha = (tp1->srtt_us >> 3);
		//ReportInfo("mrt %d", mrtAlpha);
	}
	//ReportInfo("State=%u HZ=%d meta-buffer=%u space=%d thresh1=%u thresh2=%u srtt1=%u srtt2=%u inflight1=%u inflight2=%u cwnd1=%u cwnd2=%u", state, HZ, metabuf, GetSpace(sock1) + GetSpace(sock2), thres1, thres2, (tp1->srtt_us>>3)/1000, (tp2->srtt_us>>3)/1000, tcp_packets_in_flight(tp1)*(tp1->mss_cache), tcp_packets_in_flight(tp2)*(tp2->mss_cache), tp1->snd_cwnd * (tp1->mss_cache), tp2->snd_cwnd * (tp2->mss_cache));
	//int una1 = mptcp_is_temp_unavailable(sock1);
	//ReportInfo("kernel srtt1=%u srtt2=%u inflight1=%u inflight2=%u cwnd1=%u cwnd2=%u buf1=%u buf2=%u", tp1->srtt_us, tp2->srtt_us, tcp_packets_in_flight(tp1)*(tp1->mss_cache), tcp_packets_in_flight(tp2)*(tp2->mss_cache), tp1->snd_cwnd * (tp1->mss_cache), tp2->snd_cwnd * (tp2->mss_cache), buf1, buf2);
	// state 0: WiFi only, state 1: WiFi & LTE)
	if (state == 0) {
		if (metabuf > space1)
			metabuf -= space1;
		else
			metabuf = 0;
		if (/*jiffies - lastWiFiStart > HZ
			&&*/ tp1->snd_cwnd > tp1->snd_ssthresh) {
			if (metabuf >= thres2) {
				//ReportInfo("BBS: State 0, %d B, over limit %d.", metabuf, thres2);
				if (lastOverThres == 0) {
					startOverThresT = jiffies;
				}
				if (jiffies - startOverThresT >= 2* mrtAlpha * HZ/1000000 /*50 * HZ / 1000*/) {
					state = 1;
					lastOverThres = 0;
				}
				lastOverThres = 1;
			} else {
				lastOverThres = 0;
			}
		}
		int una1 = mptcp_is_temp_unavailable(sock1);
		if (una1) return 0;
		else return 1;
	}
	if (state == 1) {
		if (metabuf > space1 + space2)
			metabuf -= (space1 + space2);
		else
			metabuf = 0;
		if (metabuf <= thres1) {
			//ReportInfo("BBS: State 1, %d B, below limit %d.", metabuf, thres1);
			if (lastOverThres == 0) {
				startOverThresT = jiffies;
			}
			if (jiffies - startOverThresT >= 0*HZ/1000) {
                                state = 0;
                                lastOverThres = 0;
				lastWiFiStart = jiffies;
                        }
			lastOverThres = 1;
		} else {
			lastOverThres = 0;
		}
		int una1 = mptcp_is_temp_unavailable(sock1);
        //ReportInfo("una2");
        	int una2 = mptcp2_is_temp_unavailable(sock2);
		if (una1 && una2) { /*ReportInfo("Multipath: both busy");*/ return 0;}
        	if (una1) {/*ReportInfo("Multipath: WiFi busy, ->LTE");*/ return 4;}
       		if (una2) {/*ReportInfo("Multipath: LTE busy, ->Wifi");*/ return 3;}
		//return 3;
		
        	if (tp1->srtt_us < tp2->srtt_us) {
			//ReportInfo("Multipath: both avail ->WiFi");
                	return 3;
        	} else {
			//ReportInfo("Multipath: both avail, ->LTE");
                	return 4;
		}
		
	}
	return 0;

		//ReportInfo("kernel srtt1=%u srtt2=%u inflight1=%u inflight2=%u cwnd1=%u cwnd2=%u buf1=%u buf2=%u", tp1->srtt_us, tp2->srtt_us, tcp_packets_in_flight(tp1)*(tp1->mss_cache), tcp_packets_in_flight(tp2)*(tp2->mss_cache), tp1->snd_cwnd * (tp1->mss_cache), tp2->snd_cwnd * (tp2->mss_cache), buf1, buf2);
	//	return 2;
	//}
}

static unsigned int getThrptFromCwnd(int id) {
	struct tcp_sock * tp;
	switch (id) {
	case 1:
		tp = tcp_sk(sock1);
		break;
	case 2:
		tp = tcp_sk(sock2);
		break;
	default:
		return 0;
	}
	if (tp->srtt_us >> 3 == 0) return 0;
	return (unsigned int)((long)tp->snd_cwnd * tp->mss_cache * 8000 / (tp->srtt_us >> 3));
}

static int jtcp_transmit_skb(struct sock *sk, struct sk_buff *skb, int clone_it,
		    gfp_t gfp_mask) {
        struct tcp_sock * tp1 = tcp_sk(sock1);
        struct tcp_sock * tp2 = tcp_sk(sock2);
	int update = 0;
	int sample = 0;
	int tmpState = 0;
        // update throughput samples
//#ifdef REINJECTION
	if (keepMapping) {
		if (sk && skb && skb->len >= 8) {
			if (sock1 && sk == sock1) {
				addMapping(skb, 1);
				//printMapping(1);
			} else if (sock2 && sk == sock2) {
				addMapping(skb, 2);
				//printMapping(2);
			} else if (sock3 && sk == sock3) {
                addMapping(skb, 3);
            } else if (sock4 && sk == sock4) {
                addMapping(skb, 4);
            }
		}
	}
	
	if (sk && skb && skb->len > 0) {
		if (sock1 && sk == sock1) {
			addBIFMapping(skb, 1);
		} else if (sock2 && sk == sock2) {
			addBIFMapping(skb, 2);
		}
	}
	
	//#endif
    //	spin_lock(&thrptlock);
    if (wifiST > 0 && jiffies - wifiST >= interval) {
        sample = GetThrptSample(1, tp1->snd_una);
        //ReportInfo("Jiffies: %d WiFi ST: %d sample: %d predict: w %d /l %d cwnd predict: w %u /l %u", jiffies, wifiST, sample, wifiThrpt, lteThrpt, tp1->snd_cwnd * (tp1->mss_cache) * 8000 / (tp1->srtt_us >> 3), tp2->snd_cwnd * (tp2->mss_cache) * 8000 / (tp2->srtt_us >> 3));
        wifiST = jiffies;
		update = 1;
    }

    if (lteST > 0 && jiffies - lteST >= interval) {
        sample = GetThrptSample(0, tp2->snd_una);
		//ReportInfo("Jiffies: %d LTE ST: %d sample: %d predict: w %d /l %d cwnd predict: w %u /l %u", jiffies, lteST, sample, wifiThrpt, lteThrpt, tp1->snd_cwnd * (tp1->mss_cache) * 8000 / (tp1->srtt_us >> 3), tp2->snd_cwnd * (tp2->mss_cache) * 8000 / (tp2->srtt_us >> 3));
        lteST = jiffies;
		update = 1;
    }

	if (update) {
		tmpState = UpdateEMPTCPState();
		//ReportInfo("Update state: %d", tmpState);
	}

//	spin_unlock(&thrptlock);

	jprobe_return();
	return 0;
}


static void jtcp_rcv_established(struct sock *sk, struct sk_buff *skb,
                                 const struct tcphdr *th, unsigned int len)
{
//#ifdef REINJECTION
//	spin_lock(&acklock);
	if (skb && sk) {
		unsigned int seq, ack = 0;
		if (sock1 && sk == sock1) {
			seq = TCP_SKB_CB(skb)->seq;
            ack = TCP_SKB_CB(skb)->ack_seq;
			//ReportInfo("sock1 rcv process, len=%u, seq=%u, ack=%u", len, seq, ack);
			if (ack != lastACK1) {
				lastACK1 = ack;
				lastSEQ1 = seq;
				bDupACK1 = 0;
			} else if (seq == lastSEQ1) {
				bDupACK1 = 1;
			} else {
				lastSEQ1 = seq;
			}
        } else if (sock2 && sk == sock2) {
			seq = TCP_SKB_CB(skb)->seq;
            ack = TCP_SKB_CB(skb)->ack_seq;
			//ReportInfo("sock2 rcv process, len=%u, seq=%u, ack=%u", len, seq, ack);
			if (ack != lastACK2) {
                lastACK2 = ack;
                lastSEQ2 = seq;
                bDupACK2 = 0;
            } else if (seq == lastSEQ2) {
                bDupACK2 = 1;
            } else {
                lastSEQ2 = seq;
            }
        } else if (sock3 && sk == sock3) {
            seq = TCP_SKB_CB(skb)->seq;
            ack = TCP_SKB_CB(skb)->ack_seq;
            if (ack != lastACK3) {
                lastACK3 = ack;
                lastSEQ3 = seq;
                bDupACK3 = 0;
            } else if (seq == lastSEQ3) {
                bDupACK3 = 1;
            } else {
                lastSEQ3 = seq;
            }
        } else if (sock4 && sk == sock4) {
            seq = TCP_SKB_CB(skb)->seq;
            ack = TCP_SKB_CB(skb)->ack_seq;
            if (ack != lastACK4) {
                lastACK4 = ack;
                lastSEQ4 = seq;
                bDupACK4 = 0;
            } else if (seq == lastSEQ4) {
                bDupACK4 = 1;
            } else {
                lastSEQ4 = seq;
            }
        }
    }
//	spin_unlock(&acklock);
//#endif
	jprobe_return();
}

static struct jprobe tcp_jprobe_snd = {
        .kp = {
                .symbol_name    = "tcp_transmit_skb",
        },
        .entry  = jtcp_transmit_skb,
};

static struct jprobe tcp_jprobe_rcv = {
        .kp = {
                .symbol_name    = "tcp_rcv_established",
        },
        .entry  = jtcp_rcv_established,
};

static long cmat_data_ioctl(struct file *file,	unsigned int ioctl_num,	unsigned long ioctl_param) {
	//ReportInfo("In cmat_data_ioctl %u %d %d\n", ioctl_num, chunknum, chunksize);
	switch (ioctl_num) {
		case CMAT_IOCTL_SET_SUBFLOW_FD1:
		{
			int fd = (int)ioctl_param;
			sock1 = GetTCPSocket(fd);
			fd1 = fd;
			ReportInfo("Subflow 1 added, fd=%d", fd);
			return 0;
		}
		
		case CMAT_IOCTL_SET_SUBFLOW_FD2:
		{
			int fd = (int)ioctl_param;	
			sock2 = GetTCPSocket(fd);
			fd2 = fd;	
			ReportInfo("Subflow 2 added, fd=%d", fd);
			return 0;
		}

        case CMAT_IOCTL_SET_SUBFLOW_FD3:
        {
            int fd = (int)ioctl_param;
            sock3 = GetTCPSocket(fd);
            fd3 = fd;
            ReportInfo("Subflow 3 added, fd=%d", fd);
            return 0;
        }

        case CMAT_IOCTL_SET_SUBFLOW_FD4:
        {
            int fd = (int)ioctl_param;
            sock4 = GetTCPSocket(fd);
            fd4 = fd;
            ReportInfo("Subflow 4 added, fd=%d", fd);
            return 0;
        }

		case CMAT_IOCTL_SET_META_BUFFER:
		{
			metabuf = (unsigned int)ioctl_param;
			return 0;
		}

		case CMAT_IOCTL_GET_META_BUFFER:
		{
			//ReportInfo("CMAT_IOCTL_GET_META_BUFFER");
			return put_user(metabuf, (unsigned int *)ioctl_param);
		}
		
		case CMAT_IOCTL_GET_SCHED:
		{
			int s = Schedule();
			return put_user(s, (int *)ioctl_param);
		}

		case CMAT_IOCTL_GET_SCHED_DELAY:
                {
                        int s = ScheduleDelay();
                        return put_user(s, (int *)ioctl_param);
                }
		
		case CMAT_IOCTL_GET_FD1_DELAY:
		{
			//ReportInfo("SUBFLOW1");
			int d = EstimateDelay(tcp_sk(sock1));
			return put_user(d, (int *)ioctl_param);
		}
		
		case CMAT_IOCTL_GET_FD2_DELAY:
                {
                        //ReportInfo("SUBFLOW2");
                        int d = EstimateDelay(tcp_sk(sock2));
                        return put_user(d, (int *)ioctl_param);
                }
		
		case CMAT_IOCTL_GET_FD1_RTT:
                {
                        //ReportInfo("SUBFLOW1");
			int rtt = tcp_sk(sock1)->srtt_us >> 3;
                        return put_user(rtt, (int *)ioctl_param);
                }

                case CMAT_IOCTL_GET_FD2_RTT:
                {
                        //ReportInfo("SUBFLOW2");
			int rtt = tcp_sk(sock2)->srtt_us >> 3;
                        return put_user(rtt, (int *)ioctl_param);
                }

		case CMAT_IOCTL_GET_FD1_RTTVAR:
                {
                        //ReportInfo("SUBFLOW1");
                        int rtt = tcp_sk(sock1)->rttvar_us;
                        return put_user(rtt, (int *)ioctl_param);
                }

                case CMAT_IOCTL_GET_FD2_RTTVAR:
                {
                        //ReportInfo("SUBFLOW2");
                        int rtt = tcp_sk(sock2)->rttvar_us;
                        return put_user(rtt, (int *)ioctl_param);
                }

		case CMAT_IOCTL_GET_SCHED_BUFFER:
		{
			int s = ScheduleBuffer(0);
			return put_user(s, (int *)ioctl_param);
		}

		case CMAT_IOCTL_GET_SCHED_BUFFER_MRT:
                {
                        int s = ScheduleBuffer(1);
                        return put_user(s, (int *)ioctl_param);
                }
		
		case CMAT_IOCTL_GET_SCHED_EMPTCP:
                {
                        int s = ScheduleEMPTCP();
                        return put_user(s, (int *)ioctl_param);
                }
		case CMAT_IOCTL_SET_RESET_EMPTCP:
		{
			ResetEMPTCP();
			return 0;
		}
		case CMAT_IOCTL_SET_SEQ_MAPPING:
		{
			requestSubflowNo = (int)((ioctl_param >> 48) & 0xFFFF);
			requestConnID = (WORD)((ioctl_param >> 32) & 0xFFFF);
			requestSeq = (DWORD)(ioctl_param & 0xFFFFFFFF);
			return 0;
		}
		case CMAT_IOCTL_GET_SEQ_MAPPING:
                {
			unsigned long long s = getMapping();
                        return put_user(s, (unsigned long long *)ioctl_param);
                }
		case CMAT_IOCTL_GET_FD1_ACK:
		{
			unsigned long long s = ((unsigned long long)bDupACK1 << 32) + ((unsigned long long)lastACK1);
			//ReportInfo("FD1: %d %u", bDupACK1, lastACK1);
			return put_user(s, (unsigned long long *)ioctl_param);
		}
		case CMAT_IOCTL_GET_FD2_ACK:
		{
			//ReportInfo("FD2: %d %u", bDupACK2, lastACK2);
			unsigned long long s = ((unsigned long long)bDupACK2 << 32) + ((unsigned long long)lastACK2);
                        return put_user(s, (unsigned long long *)ioctl_param);
		}

        case CMAT_IOCTL_GET_FD3_ACK:
        {
            unsigned long long s = ((unsigned long long)bDupACK3 << 32) + ((unsigned long long)lastACK3);
            return put_user(s, (unsigned long long *)ioctl_param);
        }
                                            //
        case CMAT_IOCTL_GET_FD4_ACK:
        {
            unsigned long long s = ((unsigned long long)bDupACK4 << 32) + ((unsigned long long)lastACK4);
            return put_user(s, (unsigned long long *)ioctl_param);
        }

		case CMAT_IOCTL_GET_FD1_CWNDTHRPT:
		{
			//ReportInfo("CMAT_IOCTL_GET_FD1_CWNDTHRPT");
			unsigned int r = getThrptFromCwnd(1);
			return put_user(r, (unsigned int *)ioctl_param);
		}

		case CMAT_IOCTL_GET_FD2_CWNDTHRPT:
		{
			//ReportInfo("CMAT_IOCTL_GET_FD2_CWNDTHRPT");
                        unsigned int r = getThrptFromCwnd(2);
                        return put_user(r, (unsigned int *)ioctl_param);
                }

        case CMAT_IOCTL_GET_FD1_AVAIL:
        {
        	int r = tcp_available_space(sock1);

        	return put_user(r, (unsigned int *)ioctl_param);
        }
        case CMAT_IOCTL_GET_FD2_AVAIL:
        {
        	int r = tcp_available_space(sock2);

        	return put_user(r, (unsigned int *)ioctl_param);
        }

        case CMAT_IOCTL_GET_FD3_AVAIL:
        {
            int r = tcp_available_space(sock3);
            return put_user(r, (unsigned int *)ioctl_param);
        }

        case CMAT_IOCTL_GET_FD4_AVAIL:
        {
            int r = tcp_available_space(sock4);
            return put_user(r, (unsigned int *)ioctl_param);
        }
        /*
        case CMAT_IOCTL_GET_FD1_BIF:
        {
        }
        case CMAT_IOCTL_GET_FD2_BIF:
        {
        }
        */
        case CMAT_IOCTL_SET_DISABLE_MAPPING:
        {
        	keepMapping = 0;
        	return 0;
        }

        case CMAT_IOCTL_SET_BIF_REQUEST:
        {
        	requestSubflowNo = (int)((ioctl_param >> 32) & 0xFFFFFFFF);
			requestACK = (DWORD)(ioctl_param & 0xFFFFFFFF);
			return 0;
        }
		case CMAT_IOCTL_GET_BIF:
		{
			int r = getBIFMapping();
			return put_user(r, (int *)ioctl_param);
		}
		default:
			ReportError("Invalid ioctl num: %u\n", ioctl_num);
			return -1;
	}
}

static struct file_operations misc_dev_fops = {
	.owner = THIS_MODULE,
	.open = cmat_data_open,
	.mmap = cmat_data_mmap,
	.unlocked_ioctl = cmat_data_ioctl
};

static struct miscdevice misc = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = CMAT_DATA_BUFFER_NAME,
		.fops = &misc_dev_fops
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

int init_module()
{
	sock1 = sock2 = NULL;
	used1 = used2 = 0;
	fd1 = fd2 = -1;
	lastACK1 = lastACK2 = 0;
	lastSEQ1 = lastSEQ2 = 0;
	bDupACK1 = bDupACK2 = 0;
  int ret = misc_register(&misc);
  if (ret != 0) {
  	ReportError("Register device failed\n");
  	return -1;
  }
	if (wifibw == 0) {
                onlineBWEst = 1;
        } else {
		bufthres = computeBufThres(ltebw/1000, wifibw/1000, appthrpt/1000, chunks);
		onlineBWEst = 0;
        }
	ReportInfo("Buffer threshold: %u KB (chunk %u KB), LTE preset BW: %u bps, WiFi preset BW: %u bps, app preset BW: %u, online BW estimation: %d", bufthres, chunks, ltebw, wifibw, appthrpt, onlineBWEst);
	ret = register_jprobe(&tcp_jprobe_snd);
	if (ret) {
		unregister_jprobe(&tcp_jprobe_snd);
		return -1;
	}
	ret = register_jprobe(&tcp_jprobe_rcv);
	if (ret) {
                unregister_jprobe(&tcp_jprobe_rcv);
                return -1;
        }
	initializeMapping();
	initializeBIFMapping();
	return 0;
}

void cleanup_module()
{
	misc_deregister(&misc);
	unregister_jprobe(&tcp_jprobe_snd);
	unregister_jprobe(&tcp_jprobe_rcv);
	freeMapping();
	freeBIFMapping();
}

