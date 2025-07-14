#ifndef MESSAGE_SWAP_H
#define MESSAGE_SWAP_H
#include <unistd.h>
#include <stdint.h>
#include <netinet/ip_icmp.h>
const double SEND_INTERVAL = 0.001;
const unsigned int SEND_TIMES = 5;
const double COLLECT_INTERVAL = SEND_INTERVAL * SEND_TIMES;

struct LucpPayload
{
    uint8_t r_c;
    uint8_t r_s;
    uint8_t r_t;
    double o_s;
    double g_s;
    double x_r;
    uint64_t n_iter;
};
struct LucpPacket
{
    struct icmphdr hdr;
    struct LucpPayload payload;
};
#endif // MESSAGE_SWAP_H