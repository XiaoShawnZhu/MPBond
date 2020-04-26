#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <net/route.h>
//#include "hostname.h"

#pragma GCC diagnostic ignored "-Wdeclaration-after-statement"
// #pragma GCC diagnostic ignored "-Wunused-result"

#define LOCAL_PROXY_IP "127.0.0.1"
#define LOCAL_PROXY_TCP_PORT 1202
#define LOCAL_PROXY_UDP_PORT 1202

#define PROT_TCP 6
#define PROT_UDP 17

#define TCPFLAG_SYN 0x2

#define DEBUG_DUMP
typedef unsigned char BYTE;
typedef unsigned int DWORD;
typedef unsigned short WORD;


WORD svrPort = 5000;
WORD cliPort;

DWORD localHost;
WORD localProxyPort;

// TCP connection mapping 
static WORD srcPort2serverPort[65536];
static DWORD srcPort2serverIP[65536];

typedef struct _IPv4_INFO {
  DWORD srcIP;
  DWORD dstIP;
  WORD protocol;
  WORD srcPort;
  WORD dstPort;
  int payloadLen;
  int ipHeaderLen;
  int tcpHeaderLen;
  BYTE tcpFlags;  
} IPv4_INFO;

static struct nf_hook_ops nfho;         //struct holding set of hook function options
static struct nf_hook_ops nfho_down;

MODULE_AUTHOR("Xiao Zhu");
MODULE_LICENSE("GPL");
//MODULE_INFO(vermagic, "3.10.73-g51dd5dc-dirty SMP preempt mod_unload modversions aarch64");

void Log(const char * format, ...) {
  char dest[784];
  va_list argptr;
  va_start(argptr, format);
  vsprintf(dest, format, argptr);
  va_end(argptr);
  printk(KERN_INFO "[MONITOR] %s", dest);
}

void ReportError(const char * format, ...) {
  char dest[784];
  va_list argptr;
  va_start(argptr, format);
  vsprintf(dest, format, argptr);
  va_end(argptr);
  printk("+++++ERROR+++++: %s\n", dest);
}

void DumpPayload(const struct sk_buff * skb) {
}

// DWORD ConvertIPToDWORD(const char * _ip) {
//   char ip[128];
//   DWORD ipc[4];
//   int len = strlen(ip);
//   int i, j=0, k=0;
  
//   strcpy(ip, _ip);
//   ip[len++] = '.';
  
//   for (i=0; i<len; i++) {
//     if (ip[i] == '.') {
//       ip[i] = 0;
//       if(kstrtou32(ip + j, 10, &ipc[k++]) == 1){

//       }
//       j = i+1;
//       if (k == 4) break;
//     }
//   }
//   return (ipc[0]) | (ipc[1] << 8) | (ipc[2] << 16)  | (ipc[3] << 24);
// }

static inline WORD ReverseWORD(WORD x) {
  return
    (x & 0xFF) << 8 |
    (x & 0xFF00) >> 8;
}

int IsIPv4(const struct sk_buff * skb, IPv4_INFO * pInfo) {
    const BYTE * pkt_data = skb->data;
    
    if (pkt_data == NULL) {
      ReportError("skb data empty");
      return 0;
    }

    BYTE ipFlag = *pkt_data;
    if ((ipFlag & 0xF0) != 0x40) return 0; 
    
    if ((ipFlag & 0x0F) < 5) {
      ReportError("IPv4 flag: %d", (int)ipFlag);
      DumpPayload(skb); 
      return 0; 
    }
    
    DWORD ipOptionLength = 4 * ((ipFlag & 0x0F) - 5);
    WORD ipLength = ReverseWORD(*((WORD *)(pkt_data + 2)));
    
    if (ipLength != skb->len) {
      ReportError("skb len (%d) != ipLen (%d)", (int)skb->len, (int)ipLength);
      DumpPayload(skb); 
      return 0;
    }
    
    pInfo->srcIP = *((DWORD *)(pkt_data + 12));
    pInfo->dstIP = *((DWORD *)(pkt_data + 16));
    pInfo->protocol = *((BYTE *)(pkt_data + 9));
    
    pInfo->ipHeaderLen = 20 + ipOptionLength;
    
    pkt_data += ipOptionLength; //***** Change offset
    if (pInfo->protocol == PROT_TCP) {
      if (ipLength < 20 + ipOptionLength + 20) {
        ReportError("Malformed TCP header");
        DumpPayload(skb);
        return 0;
      }           
      pInfo->srcPort = ReverseWORD(*((WORD *)(pkt_data + 20 + 0))); 
      pInfo->dstPort = ReverseWORD(*((WORD *)(pkt_data + 20 + 2))); 
      int tcpHeaderLen = (*((BYTE *)(pkt_data + 20 + 12)) & 0xF0) >> 2;
      
      pInfo->payloadLen = (int)ipLength - 20 - ipOptionLength - tcpHeaderLen;
      pInfo->tcpHeaderLen = tcpHeaderLen;
      pInfo->tcpFlags = *(pkt_data + 20 + 13);
      
      if (pInfo->payloadLen < 0) {
        ReportError("Malformed TCP packet");
        DumpPayload(skb);
        return 0;
      }
      
    } else if (pInfo->protocol == PROT_UDP) {     
      if (ipLength < 20 + ipOptionLength + 8) {
        ReportError("Malformed UDP header");
        DumpPayload(skb);
        return 0;
      }
      pInfo->srcPort = ReverseWORD(*((WORD *)(pkt_data + 20 + 0))); 
      pInfo->dstPort = ReverseWORD(*((WORD *)(pkt_data + 20 + 2))); 
      pInfo->tcpHeaderLen = 0;
      pInfo->tcpFlags = 0;
      
      pInfo->payloadLen = (int)ipLength - 20 - ipOptionLength - 8;
      if (pInfo->payloadLen < 0) {
        ReportError("Malformed UDP packet");
        DumpPayload(skb);
        return 0;
      }
      
    } else {
      pInfo->srcPort = 0;
      pInfo->dstPort = 0;
      pInfo->payloadLen = (int)ipLength - 20 - ipOptionLength;
      pInfo->tcpHeaderLen = 0;
    }
    
    return 1;
}

void AddIPOptionForSYN(struct sk_buff * skb, IPv4_INFO * pInfo) {
  //Use IP option (record route) to carry custom data
  
  static const int ipOptLen = 12; //must be a multiple of 4
  
  if (skb->end - skb->tail < ipOptLen) {
    ReportError("Not enough space in SKB");
    return;
  }
  
  //TODO: a case where there is already IP options
  skb_put(skb, ipOptLen);
  
  BYTE * p = skb->data + pInfo->ipHeaderLen;
  memmove(p+ipOptLen, p, pInfo->tcpHeaderLen + pInfo->payloadLen);  
  *p = 7; *(p+1) = 11; *(p+2) = 12;
  *((DWORD *)(p+3)) = pInfo->dstIP;
  *((DWORD *)(p+8)) = 0;
  *((WORD *)(p+7)) = ReverseWORD(pInfo->dstPort);
      
  //Update IP len
  pInfo->ipHeaderLen += ipOptLen;
  WORD newIpLen = ReverseWORD((WORD)(pInfo->ipHeaderLen + pInfo->tcpHeaderLen + pInfo->payloadLen));
  *((WORD *)(skb->data + 2)) = newIpLen; //ip len

  *skb->data = 0x40 | (BYTE)(5 + (ipOptLen >> 2));
}

WORD IPChecksum(WORD *data, int len) {
  DWORD sum = 0;
  int i, j;
  for (i=0, j=0; i<len; i+=2, j++) {
    if (i == 10) continue;
    sum += data[j];   
  }
  
  while(sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16); 
  
  return (WORD)(~sum);
}

void UpdateUDPIPChecksum(int dir, const struct sk_buff * skb, const IPv4_INFO * pInfo/*, DWORD srcIP, DWORD dstIP*/) {
  WORD ipSum;

  if (dir == NF_INET_LOCAL_OUT) {
    *(WORD *)(skb->data + 10) = ipSum = IPChecksum((WORD *)skb->data, pInfo->ipHeaderLen);
    
    /*
    *(WORD *)(skb->data + pInfo->ipHeaderLen + 16) = 
        TCPChecksum((WORD *)(skb->data + pInfo->ipHeaderLen), pInfo->payloadLen + pInfo->tcpHeaderLen, srcIP, dstIP);
    */

    *(WORD *)(skb->data + pInfo->ipHeaderLen + 6) = 0; 
  }
  //Log("\t IP Checksum = %x TCP Checksum = %x\n", ipSum, tcpSum);
}

void UpdateTCPIPChecksum(int dir, const struct sk_buff * skb, const IPv4_INFO * pInfo/*, DWORD srcIP, DWORD dstIP*/) {
  WORD ipSum /*, tcpSum*/;

  if (dir == NF_INET_LOCAL_OUT) {
    *(WORD *)(skb->data + 10) = ipSum = IPChecksum((WORD *)skb->data, pInfo->ipHeaderLen);
    
    /*
    *(WORD *)(skb->data + pInfo->ipHeaderLen + 16) = 
        TCPChecksum((WORD *)(skb->data + pInfo->ipHeaderLen), pInfo->payloadLen + pInfo->tcpHeaderLen, srcIP, dstIP);
    */
  }
  //Log("\t IP Checksum = %x TCP Checksum = %x\n", ipSum, tcpSum);
}

//function to be called by hook
unsigned int ModifyPacket(unsigned int hooknum, struct sk_buff * skb, const struct net_device *in, const struct net_device *out)
{
  IPv4_INFO info;
  if (!IsIPv4(skb, &info)) { // read Info from skb
    //Log("\t(not IPv4 packet)\n");
    return 0;
  }
  if (info.protocol == PROT_TCP || info.protocol == PROT_UDP) {
    #ifdef DEBUG_DUMP
    Log("%s\n",
      info.protocol == PROT_TCP ? "TCP" : "UDP"
    );
    #endif
  } else {
    #ifdef DEBUG_DUMP
    Log("Prot=%d\n",
      (int)info.protocol
    );
    #endif
    return 0;
  }
  if (info.protocol == PROT_UDP){
    if(hooknum == NF_INET_LOCAL_OUT){ //uplink
      if(info.dstPort != 4000) return 0;
      //printk(KERN_INFO "Redirect uplink UDP packet to local proxy.\n");
      /**********Manipulate dstIP and dstPort of UDP packet*********/
      *(DWORD *)(skb->data + 16) = localHost; //dstIP
      *(WORD *)(skb->data + info.ipHeaderLen + 2) = localProxyPort; //dstPort
      /**********Add pipe message header between packet header and payload to tell remote proxy the APP server address and client port*********/
      // go to payload
      BYTE * p = skb->data + info.ipHeaderLen + 8;
      static const int msgHeaderLen = 10;
      skb_put(skb, msgHeaderLen);
      memmove(p+msgHeaderLen, p, info.payloadLen);
      // 2byte srvPort + 4byte srvIP + 2byte len + 2byte client port
      *((WORD *)p) = info.dstPort; 
      *((DWORD *)(p+2)) = info.dstIP;
      *((WORD *)(p+6)) = 0;
      *((WORD *)(p+8)) = info.srcPort;
      // update packet length
      WORD newLen = ReverseWORD((WORD)(info.ipHeaderLen + 8 + info.payloadLen + msgHeaderLen));
      *((WORD *)(skb->data + 2)) = newLen;
      newLen = ReverseWORD((WORD)(8 + info.payloadLen + msgHeaderLen));
      *((WORD *)(skb->data + info.ipHeaderLen + 4)) = newLen;
      // update checksum
      UpdateUDPIPChecksum(hooknum, skb, &info);
      cliPort = *(WORD *)(skb->data + info.ipHeaderLen);
      return 1;
    }
    else{ //downlink
     // if (info.srcIP == localHost && info.srcPort == LOCAL_PROXY_PORT) {
      if (info.srcIP == localHost && info.srcPort == LOCAL_PROXY_UDP_PORT) {
        /*if (udpSrcPort2ServerPort[info.dstPort] == 0) {
          ReportError("Mapping not found for dstPort: %d", (int)info.dstPort);
        }*/
        /*******Return real data packet to client APP by changing its src address from local proxy to real server APP******/
        // UDP header lasts only 8 bytes, +10 points to svrIP field in pipe msg header, +8 points to the head of msg header
        DWORD svrIP = *(DWORD *)(skb->data + info.ipHeaderLen + 10);// = udpSrcPort2ServerIP[info.dstPort];
       
        WORD svrPort = *(WORD *)(skb->data + info.ipHeaderLen + 8);// = udpSrcPort2ServerPort[info.dstPort];
        
        // +12 points to the src ip field of the ip header
        *(DWORD *)(skb->data + 12) = svrIP; //2859259021;     //srcIP
        // + ip header points to the head of udp header(udp src port)
        *(WORD *)(skb->data + info.ipHeaderLen) = ReverseWORD(svrPort); //srcPort  
        //*(WORD *)(skb->data + info.ipHeaderLen + 2) = cliPort; //dstPort 
        //        skb_pull(skb, 10);
        // move the pointer to the start of the data(shifting ip header and 8-byte udp header length)
        BYTE * p = skb->data + info.ipHeaderLen + 8;
        int msgTotalHeaderLen = 10;
        memmove(p, p + msgTotalHeaderLen, info.payloadLen-msgTotalHeaderLen);
        // calcute new length
        WORD newLen = ReverseWORD((WORD)(info.ipHeaderLen + 8 + info.payloadLen - msgTotalHeaderLen));
        // update length field in IP header
        *((WORD *)(skb->data + 2)) = newLen;
        newLen = ReverseWORD((WORD)(8 + info.payloadLen - msgTotalHeaderLen));
        // update length field in UDP header
        *((WORD *)(skb->data + info.ipHeaderLen + 4)) = newLen;
        skb_trim(skb, info.ipHeaderLen + 8 + info.payloadLen - msgTotalHeaderLen);

        Log("Return downlink UDP packet.");
        UpdateUDPIPChecksum(hooknum, skb, &info);

        //UpdateTCPIPChecksum(hooknum, skb, &info);
        #ifdef DEBUG_UDP_DUMP
                          Log("\t ### UDP download SrcIP/Port changed to %s/%d, DstIP/Port %s/%d\n", ConvertDWORDToIP(svrIP), (int)svrPort, ConvertDWORDToIP(info.dstIP), (int)info.dstPort);
                          #endif
     
        return 1;
      }  
    }
  }
  else if(info.protocol == PROT_TCP){
    if(hooknum == NF_INET_LOCAL_OUT){
      if(info.dstPort != 4000){
      //if(info.dstPort != 4000 && info.dstPort != 443 && info.dstPort != 5228 && info.dstPort != 554 && info.dstPort != 19305){
        Log("Bypass dstPort %d\n", info.dstPort);
        return 0;
      }
      Log("TCP uplink packet with dstPort %d.\n", info.dstPort);
      //printk(KERN_INFO "Redirect uplink TCP packet to local proxy.\n");
      /**********Manipulate dstIP and dstPort of TCP packet*********/
      *(DWORD *)(skb->data + 16) = localHost; //dstIP
      *(WORD *)(skb->data + info.ipHeaderLen + 2) = localProxyPort; //dstPort

      // /**********Add pipe message header between packet header and payload to tell remote proxy the APP server address and client port*********/
      int bSYN;//, bFIN, bRST;
      bSYN = info.tcpFlags & TCPFLAG_SYN;
      if (bSYN) {         
          AddIPOptionForSYN(skb, &info);
          if (srcPort2serverIP[info.srcPort] != 0) {
            Log("*** DUPLICATE PORT!!! ***\n");
          }
          srcPort2serverPort[info.srcPort] = info.dstPort;
          srcPort2serverIP[info.srcPort] = info.dstIP;
      }
      UpdateTCPIPChecksum(hooknum, skb, &info);
      return 1;

    }
    else{
      if (info.srcIP == localHost && info.srcPort == LOCAL_PROXY_TCP_PORT) {
        Log("TCP downlink packet.\n");
        DWORD svrIP = srcPort2serverIP[info.dstPort];
        WORD svrPort = srcPort2serverPort[info.dstPort];
        
        *(DWORD *)(skb->data + 12) = svrIP; //srcIP
        *((WORD *)(skb->data + info.ipHeaderLen)) = ReverseWORD(svrPort); //srcPort
            
        UpdateTCPIPChecksum(hooknum, skb, &info);

        return 1;
      }
    }
    
  }

  // printk(KERN_INFO "packet accepted\n");                                             //log to var/log/messages
  return 0;                                                                   //accepts the packet
}

/* Function prototype in <linux/netfilter> */
unsigned int hook_func(unsigned int hooknum,  
                  struct sk_buff * skb,
                  const struct net_device *in,
                  const struct net_device *out,
                  int (*okfn)(struct sk_buff*))
{

  //unsigned hooknum = ops->hooknum;

  if (!skb) return NF_ACCEPT;
  //if (skb->pkt_type != PACKET_HOST) return NF_ACCEPT; 
  
  //pktCount++;
  int bMod = 0;
    
  if (hooknum == NF_INET_LOCAL_OUT) {
    bMod = ModifyPacket(hooknum, skb, in, out);
  }  
  else if(hooknum == NF_INET_LOCAL_IN){
    bMod = ModifyPacket(hooknum, skb, in, out);
  }
  else {
    goto NO_MOD;
  }
      
  if (bMod) {
    if (hooknum == NF_INET_LOCAL_OUT) {
      static struct net_device * pLO = NULL;
      if (pLO == NULL) pLO = dev_get_by_name(&init_net, "lo");        
      skb->dev = pLO;
            
      dev_hard_header(skb, skb->dev, ETH_P_IP, NULL //dest MAC addr
        , NULL //skb->dev->dev_addr //my MAC addr
        , skb->dev->addr_len
      );
      
      //no TCP checksum
      skb->ip_summed = CHECKSUM_UNNECESSARY;
        
      //Important: force to update the routing info
      ip_route_me_harder(skb, RTN_LOCAL);
            
          
      /*
      ////////////////////////// Dumping dst_entry ////////////////////// 
      struct rtable * rt = skb_rtable(skb);
      Log("RT: src=%s dst=%s, gateway=%s, spec_dst=%s\n",
        ConvertDWORDToIP(rt->rt_src),
        ConvertDWORDToIP(rt->rt_dst),
        ConvertDWORDToIP(rt->rt_gateway),
        ConvertDWORDToIP(rt->rt_spec_dst)
      );      
      ////////////////////////// Dumping dst_entry //////////////////////
      */
      
      int r = dev_queue_xmit(skb); //no need for kfree_skb(skb);
      if (r < 0) ReportError("dev_queue_xmit returns %d", r);     
      return NF_STOLEN;
    } else {
      goto NO_MOD; 
    } 
  }
    
NO_MOD:
  
  return NF_ACCEPT;
}

//Called when module loaded using 'insmod'
int init_module()
{
  // localHost = ConvertIPToDWORD(LOCAL_PROXY_IP);
  localHost = 16777343; //127.0.0.1

  localProxyPort = ReverseWORD(LOCAL_PROXY_UDP_PORT);
  Log("Local Proxy: %d\n", localProxyPort);

  int i;
  for (i=0; i<65535; i++) {
    srcPort2serverPort[i] = 0;
    srcPort2serverIP[i] = 0;
  }

  nfho.hook = hook_func;                       //function to call when conditions below met
  nfho.hooknum = NF_INET_LOCAL_OUT;            //called right after packet recieved, first hook in Netfilter
  nfho.pf = PF_INET;                           //IPV4 packets
  nfho.priority = NF_IP_PRI_MANGLE;            //set to highest priority over all other hook functions
  nfho_down.hook = hook_func;                       
  nfho_down.hooknum = NF_INET_LOCAL_IN;            
  nfho_down.pf = PF_INET;                           
  nfho_down.priority = NF_IP_PRI_MANGLE;             


  nf_register_hook(&nfho);                     //register hook
  nf_register_hook(&nfho_down);                     
  printk(KERN_INFO "Monitor init\n"); 
  
  return 0;                                    //return 0 for success
}

//Called when module unloaded using 'rmmod'
void cleanup_module()
{
  printk(KERN_INFO "Monitor cleanup\n"); 
  nf_unregister_hook(&nfho);                     //cleanup – unregister hook
  nf_unregister_hook(&nfho_down);
}