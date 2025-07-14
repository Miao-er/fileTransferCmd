#ifndef MESSAGE_SWAP_H
#define MESSAGE_SWAP_H
#include <unistd.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <chrono>
#include <iostream>
#include <cstring>
#include "timer.h"
#include "MessageInfo.h"

using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::duration;
using std::chrono::nanoseconds;
using std::cout, std::endl;

class MessageSwap
{
public:
    Timer *timer;
    int raw_socket;
    bool stop_event, pause_send_event, pause_recv_event;
    bool init_iter, rate_init;
    double init_rate, true_rate;
    double init_interval;
    LucpPacket cache_packet;
    uint32_t local_ip, remote_ip;
    uint16_t send_seq;
    high_resolution_clock::time_point fisrt_send,first_recv;

    MessageSwap(double rate, double init_interval)
    {
        this->raw_socket = -1;
        this->stop_event = false;
        this->pause_recv_event = true;
        this->pause_send_event = true;
        this->init_rate = rate;
        this->init_interval = init_interval;
        this->timer = new Timer(init_interval);
        this->true_rate = 0;
        this->send_seq = 0;
        this->init_iter = false;
        this->rate_init = false;

        cache_packet.hdr.code = 0;
        cache_packet.hdr.type = ICMP_INFO_REQUEST;
        cache_packet.hdr.checksum = 0;

        cache_packet.payload.r_c = 0;
        cache_packet.payload.r_s = 0;
        cache_packet.payload.r_t = 0;
        cache_packet.payload.o_s = 0.0;
        cache_packet.payload.g_s = init_rate;
        cache_packet.payload.x_r = 0;//init_rate;
        cache_packet.payload.n_iter = 0;
        local_ip = remote_ip = 0;
    }
    ~MessageSwap()
    {
        this->stop_event = true;
        this->pause_recv_event = true;
        this->pause_send_event = true;
        if(this->raw_socket >= 0)
            close(this->raw_socket);
        if(this->timer)
            delete timer;
    }

    int setUpAddr(uint32_t local_addr, uint32_t remote_addr)
    {
        this->local_ip = local_addr;
        this->remote_ip = remote_addr;
        if(!this->local_ip || !this->remote_ip)
        {
            cout << "ERROR: wrong ip address string." << endl;
            return -1;
        }
        return 0;
    }
    int createSocket()
    {
        this->raw_socket = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
        if (this->raw_socket < 0) {
            std::cerr << "Error creating raw socket for sending ICMP" << std::endl;
            return -1;
        }
        struct sockaddr_in local_addr;
        memset(&local_addr, 0, sizeof(local_addr));
        local_addr.sin_family = AF_INET;
        local_addr.sin_addr.s_addr = this->local_ip;
        if (bind(this->raw_socket, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
            std::cerr << "Error binding socket" << std::endl;
            close(this->raw_socket);
            return -1;
        }
        return 0;
    }
    void sniff()
    {
        struct sockaddr_in from;
        socklen_t fromlen = sizeof(from);
        struct sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = this->remote_ip;
        socklen_t tolen = sizeof(sockaddr_in);
        int count = 0;
        char buffer[sizeof(iphdr)+sizeof(LucpPacket)];
        while (!stop_event) {
            if(pause_recv_event && pause_send_event)
            {
                usleep(int(SEND_INTERVAL*1e6));
                continue;
            }
            //noblock recv
            int data_size = recvfrom(this->raw_socket, buffer, sizeof(iphdr)+sizeof(LucpPacket), MSG_DONTWAIT, (struct sockaddr*)&from, &fromlen);
            if (data_size < 0 && errno == EWOULDBLOCK)
            {
                continue;
            }
            else if(data_size == 0)
            {
                std::cout << "the peer is closed." << std::endl;
                break;
            }
            struct iphdr *ip_header = (struct iphdr *)buffer;
            struct  LucpPacket *lucp_packet = (struct LucpPacket *)(buffer + sizeof(struct iphdr));
            if(ip_header->daddr != this->local_ip || ip_header->saddr != this->remote_ip) continue;
            if(lucp_packet->hdr.type == ICMP_INFO_REPLY
            && !(this->pause_send_event)) // recv reply
            {
                if(lucp_packet->hdr.un.echo.sequence == 0)
                {
                    first_recv = high_resolution_clock::now();
                    // cout << "first RTT: " << duration_cast<duration<double>>(first_recv - fisrt_send).count() << endl;
                }
                if(!this->init_iter)
                {
                    this->cache_packet.payload.n_iter = lucp_packet->payload.n_iter;
                    this->init_iter = true;
                }
                // cout << "current iter:" << lucp_packet->payload.n_iter << endl;
                lucp_packet->hdr.type = ICMP_INFO_REQUEST;
                lucp_packet->payload.r_c = 0;
                lucp_packet->payload.r_t = lucp_packet->payload.r_s;
                lucp_packet->payload.r_s = 0;
                lucp_packet->payload.x_r =  std::min(this->init_rate,lucp_packet->payload.g_s);
                if(lucp_packet->payload.n_iter - this->cache_packet.payload.n_iter >= 3)
                {
                    if(lucp_packet->payload.o_s > 0 && lucp_packet->payload.x_r > this->cache_packet.payload.x_r)// && lucp_packet->payload.r_t != this->cache_packet.payload.r_t)
                        lucp_packet->payload.x_r = this->cache_packet.payload.x_r;
                    this->true_rate = lucp_packet->payload.x_r;
                }
                else 
                    this->true_rate = 0.0; 
                lucp_packet->payload.o_s = 0.0;
                lucp_packet->payload.g_s = this->init_rate;
                if(this->true_rate > 0)
                    this->timer->updateInterval(this->init_interval * this->init_rate / this->true_rate);
                //the first time when x_r change to postive value
                if(this->true_rate > 0 && !this->rate_init) 
                {
                    this->timer->start(); 
                    this->rate_init = true;
                }
                memcpy(&(this->cache_packet),lucp_packet, sizeof(LucpPacket) - sizeof(uint64_t));
                cout << " " << lucp_packet->payload.n_iter
                << " " << (int)lucp_packet->payload.r_t 
                << " " << lucp_packet->payload.x_r
                << " " << this->true_rate << endl;
            }
            else if(lucp_packet->hdr.type == ICMP_INFO_REQUEST
            && !(this->pause_recv_event)) //recv request
            {
                count ++;
                //std::cout << "id:" << lucp_packet->hdr.un.echo.id << " receive and forward " << count << " packet." << std::endl;

                lucp_packet->hdr.type = ICMP_INFO_REPLY; // ack to request.
                sendto(this->raw_socket, lucp_packet, sizeof(LucpPacket), 0, (struct sockaddr*)&to, tolen);
            }
        }
    }
    void send()
    {
        int count = 0;
        struct sockaddr_in to;
        memset(&to, 0, sizeof(to));
        to.sin_family = AF_INET;
        to.sin_addr.s_addr = this->remote_ip;
        socklen_t len = sizeof(sockaddr_in);

        bool first_message = true;
        auto timeout = high_resolution_clock::now();
        auto t = timeout;
        auto current = t;
        auto duration_interval = duration_cast<nanoseconds>(duration<double>(SEND_INTERVAL));
        while(!stop_event)
        {
            if(pause_send_event)
            {   
                usleep(int(SEND_INTERVAL*1e6));
                timeout = high_resolution_clock::now();
                continue;
            }
            t = high_resolution_clock::now();
            if(timeout > t)
                usleep(duration_cast<duration<double>>(timeout -t).count() * 1e6);
            // if(!first_message && !this->init_iter)
            // {
            //     timeout += duration_interval;
            //     continue;
            // }
            // cout << "interval = " << duration_cast<duration<double>>(high_resolution_clock::now() - current).count() <<endl;
            this->cache_packet.hdr.un.echo.sequence = htons(this->send_seq++);
            current = high_resolution_clock::now();
            sendto(this->raw_socket, &(this->cache_packet), sizeof(LucpPacket), 0, (struct sockaddr*)&to, len);
            if(first_message) first_message = false;
            if(this->send_seq == 1) 
            {fisrt_send = current;cout << "send first packet." << endl;}
            timeout += duration_interval;
            count += 1;
            // std::cout << "id: "<< id  << ", send " << count << " packet." << std::endl;
        }
    }
};

class RateController
{
public:
    uint64_t psize;
    MessageSwap *message_swap;
    std::thread * sniff_thread,*send_thread;

public:
    RateController(double default_rate, uint64_t psize)
    {   
        this->psize = psize;
        this->message_swap = new MessageSwap(default_rate, psize * 8 / (default_rate * 1e9));
    }

    ~RateController()
    {
        message_swap->stop_event = true;
        sniff_thread->join();
        send_thread->join();
        delete sniff_thread;
        delete send_thread;
        delete message_swap;
    }
    high_resolution_clock::time_point getTimeout()
    {
        return this->message_swap->timer->timeout;
    }
    int initSwap(uint32_t local_addr, uint32_t remote_addr)
    {
        //TODO:
        if(this->message_swap->setUpAddr(local_addr, remote_addr) < 0)
            return -1;
        if(this->message_swap->createSocket() < 0)
            return -1;
        return 0;
    }
    void startSwap()
    {
        message_swap->stop_event = false;
        sniff_thread = new std::thread(&MessageSwap::sniff, message_swap);
        send_thread = new std::thread(&MessageSwap::send, message_swap);
    }
    double getRate()
    {
        if(message_swap->rate_init)
            return this->message_swap->true_rate;
        else return 0.0;

    }
    void runSend()
    {   
        message_swap->pause_send_event = false;
        message_swap->timer->start();
    }
    void pauseSend(){
        message_swap->pause_send_event = true;
        message_swap->timer->pause();
    }
    void runRecv()
    {
        message_swap->pause_recv_event = false;
    }
    void pauseRecv()
    {
        message_swap->pause_recv_event = true;
    }
    bool timeToSend()
    {
        return message_swap->timer->isTimeOut();
    }
    void updateNextSend()
    {
        message_swap->timer->updateTimeOut();
    }
};
#endif // MESSAGE_SWAP_H