#define _GNU_SOURCE

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <signal.h>
#include <unistd.h>
#include <arpa/inet.h>

#define REQ_TEST_TCP_STREAM 0x01
#define REQ_TEST_TCP_RR 0x02

extern unsigned int crc_pcl(unsigned char *buffer, int len, unsigned int crc_init);

struct timespec begin, end;
double elapsed_time;

unsigned int time_alarm;
int client_sockfd;
unsigned long long trans_size = 0;
const char msg_head_throughput[] = "Throughput: ";
const char msg_head_trans[] = "Trans. per sec.: ";
const char msg_tail_throughput[] = "Mbps";
const char msg_tail_trans[] = "trans./sec.";
unsigned long long multiplier;
unsigned long long divisor;
const char *msg_head_ptr;
const char *msg_tail_ptr;
void timer_sig_handler(int sig)
{
    const double print_val = ((double)trans_size / time_alarm) * (multiplier) / (divisor); // Mbps
    printf("%s: %lf%s\n", msg_head_ptr, print_val, msg_tail_ptr);

    clock_gettime(CLOCK_MONOTONIC, &begin);
    close(client_sockfd);
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_time = ((double)end.tv_sec - begin.tv_sec) + (((double)end.tv_nsec - begin.tv_nsec) / 1000000000);
    printf("Elapsed time for close(): %lf sec.\n", elapsed_time);

    exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc != 5)
    {
        fprintf(stderr, "usage: %s [ADDR] [PORT] [TEST_NAME] [TIME]\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *addr = argv[1];
    const int port = atoi(argv[2]);
    const char *test_name = argv[3];
    time_alarm = atoi(argv[4]);

    // Mimicking netperf: init buf
    static char app_buf[16384];
    static const char netperf_str[] = "netperf";
    for (int i = 0; i < 16384; i += 8)
    {
        memcpy(app_buf + i, netperf_str, 8);
    }
    //

    struct sockaddr_in client_addr = {
        0,
    };

    if ((client_sockfd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("socket()");
        exit(EXIT_FAILURE);
    }
    client_addr.sin_family = PF_INET;
    client_addr.sin_addr.s_addr = inet_addr(addr);
    client_addr.sin_port = htons(port);

    clock_gettime(CLOCK_MONOTONIC, &begin);
    if (connect(client_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)))
    {
        perror("connect()");
        exit(EXIT_FAILURE);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed_time = ((double)end.tv_sec - begin.tv_sec) + (((double)end.tv_nsec - begin.tv_nsec) / 1000000000);
    printf("Elapsed time for connect(): %lf sec.\n", elapsed_time);

    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = timer_sig_handler;
    if (sigaction(SIGALRM, &sa, NULL) < 0)
    {
        perror("sigaction()");
        exit(EXIT_FAILURE);
    }

    unsigned char cmd;
    if (!strcmp(test_name, "TCP_STREAM"))
    {
        multiplier = 8;
        divisor = 1024 * 1024;
        msg_head_ptr = msg_head_throughput;
        msg_tail_ptr = msg_tail_throughput;

#ifdef _DEBUG
        unsigned int crc = 0;
        for (int i = 0; i < (131072 / 16384); ++i)
        {
            crc = crc_pcl((unsigned char *)app_buf, 16384, crc);
            printf("#%d round: crc_pcl(app_buf, ...) = %u\n", i + 1, crc);
        }
#endif

        cmd = REQ_TEST_TCP_STREAM;
        sendto(client_sockfd, &cmd, sizeof(cmd), 0, NULL, 0);

        alarm(time_alarm);
        while (1)
        {
            trans_size += sendto(client_sockfd, app_buf, 16384, 0, NULL, 0);
        }
    }
    else if (!strcmp(test_name, "TCP_RR"))
    {
        multiplier = 1;
        divisor = 1;
        msg_head_ptr = msg_head_trans;
        msg_tail_ptr = msg_tail_trans;

        cmd = REQ_TEST_TCP_RR;
        sendto(client_sockfd, &cmd, sizeof(cmd), 0, NULL, 0);

        alarm(time_alarm);
        while (1)
        {
            sendto(client_sockfd, app_buf, 1, 0, NULL, 0);
            recvfrom(client_sockfd, app_buf, 1, 0, NULL, 0);
#ifdef _DEBUG
            printf("Received! crc_pcl(app_buf, 1, 0) = %u\n", crc_pcl((unsigned char *)app_buf, 1, 0));
#endif

            ++trans_size;
        }
    }
    else
    {
        fprintf(stderr, "Wrong test name!\n");
    }

    return 0;
}