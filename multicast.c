#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <time.h>

/*
 * Muticast Sender & Receiver (multicast.c)
 *
 * Sends to & Receives from multicast group address
 *
 * Usage:  ./multicast <send|recv|both> <mip> <port> [sip|-] [ifip]
 *
 *          send | recv | both  : mode of operation
 *          mip                 : multicast group address
 *          port                : upd port number
 *          sip (optional)      : sender address for SSM
 *          ifip (optional)     : local ip address for multi-homing system
 *
 * Local ip address is requied to select local interface thru which multicast
 * packets are sent and received, instead of using htonl(INADDR_ANY), especially
 * on multi-homing system.
 *
 * Reasons to implement code to select local interface are:
 * (1) On a system with dual lan port, where unexpected interface is selected.
 * (2) On older PPC platform, encountered 'address already in use' error.
 * These errors can be avoided by using inet_addr("172.16.2.2").
 *
 * Example:
 *
 *         ./multicast send 239.1.1.1 12345                          // normal sender
 *         ./multicast recv 239.1.1.1 12345 172.16.1.1               // SSM
 *         ./multicast send 239.1.1.1 12345 - 172.16.1.1             // select local ip to send
 *         ./multicast recv 239.1.1.1 12345 - 172.16.2.2             // select local ip to recv
 *         ./multicast recv 239.1.1.1 12345 172.16.1.1 172.16.2.2    // SSM & local ip
 *
 * Compile options:
 *
 *          gcc multicast.c -o multicast
 *
 *                  -D NOSSM    : no SSM with IGMPv3
 *                  -l pthread  : for older PPC platform
 *
 * Verified on AlmaLinux 9.4
 *
 * Written by: Laiza Cruz    2025/06/01
 */

/*
 * Global definitions
 */
 
// Buffer size Ethernet MTU - IP header - UDP header
#define BUFSIZE (1500 - 20 - 8)

// Time to live
#define TTL 64

// Common parameters
struct param {
    struct in_addr mip;                // multicast group address
    u_short port;                      // port number
    struct in_addr sip;                // source specific address
    struct in_addr ifip;               // local interface to bind
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
    sock = socket(AF_INET, SOCK_DGRAM, 0);
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
    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    //local_addr.sin_addr.s_addr = pp->ifip.s_addr;       // local interface
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);       // magic!!!
    local_addr.sin_port = pp->port;                       // upd-port-number

    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        perror("Bind failed (receiver)");
        exit(EXIT_FAILURE);
    }

    // Join multicast group with any source multicast (ASM)
    if (pp->ssm == 0) {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = pp->mip.s_addr;       // multicast-group
        mreq.imr_interface.s_addr = pp->ifip.s_addr;      // local interface
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                &mreq, sizeof(mreq)) < 0) {
            perror("setsockopt(IP_ADD_MEMBERSHIP) failed");
            exit(EXIT_FAILURE);
        }
        printf("Joined ASM %s:%d ", inet_ntoa(pp->mip), ntohs(pp->port));
        printf("via interface %s\n", inet_ntoa(pp->ifip)); // avoid overwritten
    }
 
#ifndef NOSSM
    // Join multicast group with source specific multicast (SSM)
    if (pp->ssm == 1) {
        struct ip_mreq_source mreq;
        mreq.imr_multiaddr.s_addr = pp->mip.s_addr;       // multicast-group
        mreq.imr_interface.s_addr = pp->ifip.s_addr;      // local interface
        mreq.imr_sourceaddr.s_addr = pp->sip.s_addr;      // sender-address
        if (setsockopt(sock, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                                &mreq, sizeof(mreq)) < 0) {
            perror("setsockopt(IP_ADD_SOURCE_MEMBERSHIP) failed");
            exit(EXIT_FAILURE);
        }
        printf("Joined SSM %s:%d ", inet_ntoa(pp->mip), ntohs(pp->port));
        printf("from %s ", inet_ntoa(pp->sip));           // avoid overwritten
        printf("via interface %s\n", inet_ntoa(pp->ifip));
    }
#endif

    // Receive multicast messages
    while (1) {
        struct sockaddr_in sender_addr;
        socklen_t sender_addr_len = sizeof(sender_addr);
        ssize_t received_size = recvfrom(sock, buffer, sizeof(buffer), 0,
                                        (struct sockaddr*)&sender_addr,
                                        &sender_addr_len);
        if (received_size < 0) {
            perror("recvfrom failed");
            continue;
        }

        // Get sender's IP address and port
        char sender_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));
        u_short sender_port = ntohs(sender_addr.sin_port);

        int i;
        for (i = 0; i < received_size; i++) {
            if (! isprint(buffer[i])) { buffer[i] = '.'; }
        }

        printf("Recv fm %s:%d = %.*s (%d)\n",
            sender_ip, sender_port, received_size, buffer, received_size);
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
    char fixstr[] = ".....*";

    // Create socket
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Socket creation failed (sender)");
        exit(EXIT_FAILURE);
    }

    // Bind local interface
    struct sockaddr_in local_bind;
    memset(&local_bind, 0, sizeof(local_bind));
    local_bind.sin_family = AF_INET;
    local_bind.sin_addr.s_addr = pp->ifip.s_addr;          // local interface

    // Set source port number when bidir mode
    if (pp->bidir == 1) {
        int reuse = 1;
        if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
            perror("setsockopt(SO_REUSEADDR) failed (sender)");
            exit(EXIT_FAILURE);
        }
        local_bind.sin_port = pp->port; // src and dst port number are same
    }

    if (bind(sock, (struct sockaddr*)&local_bind, sizeof(local_bind)) < 0) {
        perror("Bind for source interfce and port failed");
        exit(EXIT_FAILURE);
    }

    // Set multicast interface to send if local interface is specified
    if (pp->ifip.s_addr != htonl(INADDR_ANY)) {
        if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, 
                &pp->ifip, sizeof(pp->ifip)) < 0) {
            perror("setsockopt(IP_MULTICAST_IF) failed");
            exit(EXIT_FAILURE);
        }
    }

    printf("Sending via interface %s\n", inet_ntoa(pp->ifip));

    // Set TTL value to packet to go beyond routers
    int ttl = TTL;
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
            &ttl, sizeof(ttl)) < 0) {
        perror("setsockopt(IP_MULTICAST_TTL) failed");
        exit(EXIT_FAILURE);
    }

    // Enable/Disable IP_MULTICAST_LOOP to local receiver
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_LOOP,
            &pp->loop, sizeof(pp->loop)) < 0) {
        perror("setsockopt(IP_MULTICAST_LOOP) failed");
        exit(EXIT_FAILURE);
    }

    // Set up destination multicast address
    struct sockaddr_in multicast_addr;
    memset(&multicast_addr, 0, sizeof(multicast_addr));
    multicast_addr.sin_family = AF_INET;
    multicast_addr.sin_addr.s_addr = pp->mip.s_addr;      // multicast-group
    multicast_addr.sin_port = pp->port;                   // udp-port-number

    // Send multicast message
    int i = 0;
    while (1) {
        char timestr[7];
        time_t now = time(NULL);

        // Get the current time and format message
        strftime(timestr, sizeof(timestr), "%H%M%S", localtime(&now));

        int sending_size = snprintf(message, sizeof(message), "%s%.*s/%s/%06d",
                    fixstr + (i % (sizeof(fixstr)-1)), 
                    (i % (sizeof(fixstr)-1)), fixstr, timestr, i);

        // Send multicast message
        if (sendto(sock, message, sending_size, 0,
                (struct sockaddr*)&multicast_addr, 
                sizeof(multicast_addr)) < 0) {
            perror("Send failed");
            exit(EXIT_FAILURE);
        }
        printf("Sent to %s:%d = %.*s (%d)\n",
                    inet_ntoa(pp->mip), ntohs(pp->port),
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
    fprintf(stderr, "Usage: %s <send|recv|both> <mip> <port> [sip|-] [ifip]\n", fn);
    exit(EXIT_FAILURE);
}

/*
 * Main, parase parameters and invoke threads
 */
int main(int argc, char const *argv[]) {
    if (argc < 4) { errusage(argv[0]); }

    struct param p;
    memset(&p, 0, sizeof(p));
    p.sip.s_addr = htonl(INADDR_ANY);           // default is 0.0.0.0
    p.ifip.s_addr = htonl(INADDR_ANY);          // default is 0.0.0.0

    const char *mode = argv[1];                 // mode send/recv/both

    p.mip.s_addr = inet_addr(argv[2]);          // multicast group address
    p.port = htons(atoi(argv[3]));              // udp port number

    if (argc >= 5) {
        if (strcmp(argv[4], "-") != 0) {        // skip if unspecified
#ifndef NOSSM
            p.ssm = 1;
#endif
            p.sip.s_addr = inet_addr(argv[4]);  // sender address for SSM
        }
    }

    if (argc >= 6) {
        p.ifip.s_addr = inet_addr(argv[5]);     // local interface ip address
    }

    if (strcmp(mode,"recv") == 0) {             // invoke receiver thread
        pthread_t rt;
        pthread_create(&rt, NULL, recv_thread, &p);
    } else
    if (strcmp(mode,"send") == 0) {             // invoke sender thread
        p.loop = 1;
        pthread_t st;
        pthread_create(&st, NULL, send_thread, &p);
    } else
    if (strcmp(mode,"both") == 0) {             // invoke both thread
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
