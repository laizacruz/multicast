#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip6.h>
#include <net/if.h>
#include <pthread.h>
#include <time.h>

/*
 * IPv6 Muticast Sender & Receiver (multicast6.c)
 *
 * Sends to & Receives from multicast group address
 *
 * Usage:  ./multicast6 <send|recv|both> <mip> <port> [sip|-] [ifname]
 *
 *          send | recv | both  : mode of operation
 *          mip                 : ipv6 multicast group address
 *          port                : upd port number
 *          sip (optional)      : sender address for SSM
 *          ifname (optional)   : local interface name for multi-lan connectivity system
 *
 * Local interface name is required to select local interface thru which multicast
 * packets are sent and received, especially on multi-lan connectivity system.
 *
 * Reasons to implement code to select local interface are:
 * (1) On a system with dual lan port, where unexpected interface is selected.
 * (2) On older PPC platform, encountered 'address already in use' error.
 * These errors can be avoided by using inet_pton().
 *
 * Example:
 *
 *         ./multicast6 send ff15::1 12345                           // simple sender
 *         ./multicast6 send ff15::1 12345                           // ASM receiver
 *         ./multicast6 recv ff15::1 12345 2001:db8:0:1::1           // SSM receiver
 *         ./multicast6 send ff15::1 12345 - enp0s3                  // set local i/f to send
 *         ./multicast6 recv ff15::1 12345 - enp0s3                  // set local i/f to receive
 *         ./multicast6 recv ff15::1 12345 2001:db8:0:1::1 enp0s3    // SSM & local i/f
 *         ./multicast6 both ff15::1 12345                           // bidir sender & receiver
 *
 * Compile options:
 *
 *          gcc multicast6.c -o multicast6
 *
 *                  -D NOSSM    : no SSM with IGMPv3
 *                  -l pthread  : for older PPC platform
 *
 * Verified on AlmaLinux 9.4
 *
 * Written by: Laiza Cruz    2025/07/15
 */

/*
 * Global definitions
 */
 
// Buffer size Ethernet MTU - IP header - UDP header
#define BUFSIZE (1500 - 40 - 8)

// Local interface thru which multicast packets are sent and received
#define IFNAMEDEFAULT "default"
#define IFIDXDEFAULT 0

// Hop limit
#define HOP 64

// Common parameters
struct param {
    struct in6_addr mip;               // multicast group address
    u_short port;                      // port number
    struct in6_addr sip;               // source specific address
    unsigned ifidx;                    // interface index
    const char *ifname;                // interface name
    int ssm;                           // source specific multicast
    int loop;                          // enable loop back to local application
    int bidir;                         // bidirectional multicast
};

/*
 * Receiver Thread
 */
void *recv_thread(void *args) {
    struct param *pp = args;

    int sock;
    char buffer[BUFSIZE];

    // Create socket
    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed (receiver)");
        exit(EXIT_FAILURE);
    }

    // Enable SO_REUSEADDR to share port with other applications
    int reuse = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed (receiver)");
        exit(EXIT_FAILURE);
    }

    // Bind to local port
    struct sockaddr_in6 local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin6_family = AF_INET6;
    local_addr.sin6_addr = in6addr_any;                    // ipv6 magic!!!
    local_addr.sin6_port = pp->port;                       // upd-port-number

    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("Bind failed (receiver)");
        exit(EXIT_FAILURE);
    }

    // Join multicast group with any source multicast (ASM)
    if (pp->ssm == 0) {
        struct ipv6_mreq mreq;
        mreq.ipv6mr_multiaddr = pp->mip;                   // multicast-group
        mreq.ipv6mr_interface = pp->ifidx;                 // local interface
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_JOIN_GROUP,
                            &mreq, sizeof(mreq)) < 0) {
            perror("setsockopt(IPV6_JOIN_GROUP) failed");
            exit(EXIT_FAILURE);
        }
        char ipaddr[INET6_ADDRSTRLEN];
        printf("Joined ASM [%s]:%d via interface index %d (%s)\n", 
                    inet_ntop(AF_INET6, &pp->mip, ipaddr, sizeof(ipaddr)), 
                    ntohs(pp->port),
                    pp->ifidx, pp->ifname);
    }

#ifndef NOSSM
    // Join multicast group with source specific multicast (SSM)
    if (pp->ssm == 1) {
        if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE,
                                pp->ifname, strlen(pp->ifname)) < 0) {
            perror("setsockopt(SO_BINDTODEVICE) failed");
            exit(EXIT_FAILURE);
        }
        struct group_source_req mreq;
        memset(&mreq, 0, sizeof(mreq));

        struct sockaddr_in6 gaddr;
        memset(&gaddr, 0, sizeof(gaddr));
        gaddr.sin6_family = AF_INET6;
        gaddr.sin6_addr = pp->mip;
        memcpy(&mreq.gsr_group, &gaddr, sizeof(gaddr));              // multicast-group

        struct sockaddr_in6 saddr;
        memset(&saddr, 0, sizeof(saddr));
        saddr.sin6_family = AF_INET6;
        saddr.sin6_addr = pp->sip;
        memcpy(&mreq.gsr_source, &saddr, sizeof(saddr));             // sender-address

        mreq.gsr_interface = pp->ifidx;                              // local interface
        if (setsockopt(sock, IPPROTO_IPV6, MCAST_JOIN_SOURCE_GROUP,
                                &mreq, sizeof(mreq)) < 0) {
            perror("setsockopt(MCAST_JOIN_SOURCE_GROUP) failed");
            exit(EXIT_FAILURE);
        }
        char ipaddr[INET6_ADDRSTRLEN];
        printf("Joined SSM [%s]:%d ", 
                    inet_ntop(AF_INET6, &pp->mip, ipaddr, sizeof(ipaddr)),
                    ntohs(pp->port));
        printf("from %s via interface %d (%s)\n", 
                    inet_ntop(AF_INET6, &pp->sip, ipaddr, sizeof(ipaddr)),
                    pp->ifidx, pp->ifname);
    }
#endif

    // Receive multicast messages
    while (1) {
        struct sockaddr_in6 sender_addr;
        socklen_t sender_addr_len = sizeof(sender_addr);
        ssize_t received_size = recvfrom(sock, buffer, sizeof(buffer), 0,
                                        (struct sockaddr*)&sender_addr,
                                        &sender_addr_len);
        if (received_size < 0) {
            perror("recvfrom failed");
            continue;
        }

        // Get sender's IP address and port
        char sender_ip[INET6_ADDRSTRLEN];
        inet_ntop(AF_INET6, &sender_addr.sin6_addr, 
                            sender_ip, sizeof(sender_ip));
        u_short sender_port = ntohs(sender_addr.sin6_port);

        int i;
        for (i = 0; i < received_size; i++) {
            if (! isprint(buffer[i])) { buffer[i] = '.'; }
        }

        printf("Recv fm [%s]:%d = %.*s (%d)\n",
            sender_ip, sender_port,
            (int)received_size, buffer, (int)received_size);
    }

    close(sock);
    return 0;
}

/*
 * Sender Thread
 */
void *send_thread(void *args) {
    struct param *pp = args;

    int sock;
    char message[BUFSIZE];
    char fixstr[] = "0.....";

    // Create socket
    sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed (sender)");
        exit(EXIT_FAILURE);
    }

    // Bind local interface
    struct sockaddr_in6 local_bind; 
    memset(&local_bind, 0, sizeof(local_bind));
    local_bind.sin6_family = AF_INET6;
    //local_bind.sin6_addr = in6addr_any;
    local_bind.sin6_addr = pp->sip;                       // src address

    // Set source port number when bidir mode
    if (pp->bidir == 1) {
        int reuse = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            perror("setsockopt(SO_REUSEADDR) failed (sender)");
            exit(EXIT_FAILURE);
        }
        local_bind.sin6_port = pp->port;                   // src port number
    }

    if (bind(sock, (struct sockaddr*)&local_bind, sizeof(local_bind)) < 0) {
        perror("Bind for source interfce and port failed");
        exit(EXIT_FAILURE);
    }

    // Set multicast interface to send if local interface is specified
    if (pp->ifidx != IFIDXDEFAULT) {
        if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                &pp->ifidx, sizeof(pp->ifidx)) < 0) {
            perror("setsockopt(IPV6_MULTICAST_IF) failed");
            exit(EXIT_FAILURE);
        }
    }

    printf("Sending via interface index %d (%s)\n", pp->ifidx, pp->ifname);

    // Set hop limit value to packet to go beyond routers
    int hop = HOP;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
            &hop, sizeof(hop)) < 0) {
        perror("setsockopt(IPV6_MULTICAST_HOPS) failed");
        exit(EXIT_FAILURE);
    }

    // Enable/Disable IP_MULTICAST_LOOP to local receiver
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
            &pp->loop, sizeof(pp->loop)) < 0) {
        perror("setsockopt(IPV6_MULTICAST_LOOP) failed");
        exit(EXIT_FAILURE);
    }

    // Set up destination multicast address
    struct sockaddr_in6 multicast_addr;
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin6_family = AF_INET6;
    multicast_addr.sin6_addr = pp->mip;                    // multicast-group
    multicast_addr.sin6_port = pp->port;                   // udp-port-number

    // Send multicast message
    int i = 0;
    while (1) {
        char timestr[7];
        time_t now = time(NULL);

        // Get the current time and format message
        strftime(timestr, sizeof(timestr), "%H%M%S", localtime(&now));

        fixstr[0] = '0' + (i / (sizeof(fixstr)-1)) % 10;

        int sending_size = snprintf(message, sizeof(message), "%s%.*s/%s/%06d",
                    fixstr + (sizeof(fixstr)-1) - (i % (sizeof(fixstr)-1)), 
                    (int)((sizeof(fixstr)-1) - (i % (sizeof(fixstr)-1))), fixstr,
                    timestr, (i+1));

        // Send multicast message
        if (sendto(sock, message, sending_size, 0,
                (struct sockaddr*)&multicast_addr, 
                sizeof(multicast_addr)) < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }

        char ipaddr[INET6_ADDRSTRLEN];
        printf("Sent to [%s]:%d = %.*s (%d)\n",
                    inet_ntop(AF_INET6, &pp->mip, ipaddr, sizeof(ipaddr)), 
                    ntohs(pp->port),
                    sending_size, message, sending_size);
        sleep(1);
        i++;
    }

    close(sock);
    return 0;
}

/*
 * Show usage error
 */
void errusage(const char *fn) {
    fprintf(stderr, "Usage: %s <send|recv|both> <mip> <port> [sip|-] [ifname]\n", fn);
	exit(EXIT_FAILURE);
}

/*
 * Main, parase parameters and invoke threads
 */
int main(int argc, char const *argv[]) {
    if (argc < 4) { errusage(argv[0]); }

    struct param p;
    memset(&p, 0, sizeof(p));
    p.sip = in6addr_any;                         // default is ::
    p.ifname = IFNAMEDEFAULT;                    // default string
    p.ifidx = IFIDXDEFAULT;                      // default is 0

    const char *mode = argv[1];                  // mode send/recv/both

    inet_pton(AF_INET6, argv[2], &p.mip);        // multicast group address
    p.port = htons(atoi(argv[3]));               // udp port number

    if (argc >= 5) {
        if (strcmp(argv[4], "-") != 0) {         // skip if unspecified
#ifndef NOSSM
            p.ssm = 1;
#endif
            inet_pton(AF_INET6, argv[4], &p.sip);    // sender address for SSM
        }
    }

    if (argc >= 6) {
        p.ifname = argv[5];
        p.ifidx = if_nametoindex(p.ifname);
    }

    if (strcmp(mode,"recv") == 0) {              // invoke receiver thread
        pthread_t rt;
        pthread_create(&rt, NULL, recv_thread, &p);
    } else
    if (strcmp(mode,"send") == 0) {              // invoke sender thread
        p.loop = 1;
        pthread_t st;
        pthread_create(&st, NULL, send_thread, &p);
    } else
    if (strcmp(mode,"both") == 0) {              // invoke both thread
        p.bidir = 1;
        pthread_t rt;
        pthread_create(&rt, NULL, recv_thread, &p);
        pthread_t st;
        pthread_create(&st, NULL, send_thread, &p);
    } else {
        errusage(argv[0]);
    }
    pause();

    return 0;
}
