#ifndef LWIPOPTS_H
#define LWIPOPTS_H

#define NO_SYS                      1
#define LWIP_SOCKET                 0
#define LWIP_NETCONN                0
#define SYS_LIGHTWEIGHT_PROT        0

#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_ICMP6                  1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DNS                    1
#define LWIP_DHCP                   0
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1

#define MEM_LIBC_MALLOC             0
#define MEMP_MEM_MALLOC             0
#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (32*1024)
#define MEMP_NUM_PBUF               24
#define MEMP_NUM_UDP_PCB            8
#define MEMP_NUM_TCP_PCB            8
#define MEMP_NUM_TCP_SEG            24
#define PBUF_POOL_SIZE              24
#define PBUF_POOL_BUFSIZE           1600

#define TCP_MSS                     1460
#define TCP_WND                     (8*TCP_MSS)
#define TCP_SND_BUF                 (4*TCP_MSS)
#define TCP_SND_QUEUELEN            16
#define LWIP_TCP_TIMESTAMPS         0

#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_GEN_ICMP           1
#define CHECKSUM_GEN_ICMP6          1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1
#define CHECKSUM_CHECK_ICMP         1
#define CHECKSUM_CHECK_ICMP6        1

#define LWIP_DEBUG                  0
#define LWIP_STATS                  0

#define LWIP_TIMERS                 1
#define LWIP_TIMERS_CUSTOM          0
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1

#define DNS_TABLE_SIZE              4
#define DNS_MAX_NAME_LENGTH         128
#define LWIP_DNS_SECURE             0

#define LWIP_IPV6_AUTOCONFIG        0
#define LWIP_IPV6_DHCP6             0
#define LWIP_IPV6_MLD               0
#define MEMP_NUM_MLD6_GROUP         4

#define LWIP_RAND()                 ((u32_t)0x12345678)

#endif
#define LWIP_RAW                    1
