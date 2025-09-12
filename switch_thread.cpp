#include <iostream>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <netinet/if_ether.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <net/if.h>
#include <unistd.h>
#include <fcntl.h>
#include <chrono>
#include <list>
#include <fstream>
#include <atomic>
#include <cmath>
#include <unordered_map>
#include <string>
#include <algorithm>
#include <sstream>
#include "MessageInfo.h"
#include <mutex>
#include <queue>
#include <condition_variable>

using std::chrono::duration;
using std::chrono::duration_cast;
using std::chrono::high_resolution_clock;
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using std::cout, std::endl;
using std::string;

// std::ofstream fout("log.txt");
int WIRE_NUM = 0;
const int MAX_WIRE_NUM = 4;

// 描述接收数据包时的以太网链路层的对端信息，实际上值为交换机连接本机的出端口信息
struct sockaddr_ll from;
socklen_t fromlen = sizeof(sockaddr_ll);

struct SwitchCache
{
    string port_name;
    double line_rate;
    double o_l; // 过载率
    double g_l; // 公平速率

    uint32_t n_l;    // 流数统计
    double u_l;      // 非瓶颈流统计
    double y_l;      // 总流量统计
    uint64_t n_iter; // 当前统计迭代次数
    std::mutex cache_mutex; // 互斥锁，保护缓存数据
    void collect()
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        if (y_l <= line_rate * SEND_TIMES)
            o_l = 0;
        else
            o_l = (y_l - line_rate * SEND_TIMES) / y_l;

        if (n_l < SEND_TIMES)
            g_l = line_rate; // - my_cache.u_l / 5;
        else
            g_l = (line_rate * SEND_TIMES - u_l) / n_l;
        // cout <<"iter:"<< n_iter + 1 <<"\tn:" << n_l << "\tu:" << u_l << "\ty:" << y_l
        //  << "\tg_l:"<<g_l<<"\to_l:"<<o_l<<endl;
        if (g_l < 0)
            g_l = 0;
        n_l = 0;
        u_l = y_l = 0.0;
        n_iter = n_iter + 1;
    }
};

std::unordered_map<uint32_t, SwitchCache *> dst_port_cache;
std::unordered_map<string, SwitchCache *> port_name_cache;
SwitchCache *switch_cache[MAX_WIRE_NUM];
bool stop_event = false; // 嗅探循环停止信号，暂时无用

// 解析交换机路由配置文件
// config_path: 配置文件路径
// port_name_cache: 端口名到SwitchCache*的映射
// dst_port_cache: 目的IP到SwitchCache*的映射
void parse_switch_config(const std::string& config_path) {
    std::ifstream fin(config_path);
    if (!fin.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return;
    }
    std::string line;
    enum Section { NONE, PORT, ROUTE } section = NONE;
    while (std::getline(fin, line)) {
        // 去除首尾空白
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        if (line == "[Port]") {
            section = PORT;
            continue;
        }
        if (line == "[Route]") {
            section = ROUTE;
            continue;
        }
        std::istringstream iss(line);
        if (section == PORT) {
            std::string port_name;
            double line_rate;
            if (!(iss >> port_name >> line_rate)) continue;
            auto* cache = new SwitchCache();
            switch_cache[WIRE_NUM++] = cache; // 添加到全局缓存
            memset(cache, 0, sizeof(SwitchCache));
            cache->port_name = port_name;
            cache->line_rate = line_rate;
            cache->g_l = line_rate;
            port_name_cache[port_name] = cache;
        } else if (section == ROUTE) {
            std::string ip_str, port_name;
            if (!(iss >> ip_str >> port_name)) continue;
            auto it = port_name_cache.find(port_name);
            if (it == port_name_cache.end()) continue;
            uint32_t ip = inet_addr(ip_str.c_str());
            dst_port_cache[ip] = it->second;
        }
    }
    fin.close();
}
// 统计更新函数
void destroy_switch()
{
     for (int i = 0; i < WIRE_NUM; i++)
        delete switch_cache[i];
}
/*
计算校验和checksum
*/
unsigned short in_cksum(unsigned short *addr, int len)
{
    int nleft = len;
    int sum = 0;
    unsigned short *w = addr;
    unsigned short answer = 0;

    while (nleft > 1)
    {
        sum += *w++;
        nleft -= 2;
    }
    if (nleft == 1)
    {
        *(unsigned char *)(&answer) = *(unsigned char *)w;
        sum += answer;
    }
    sum = (sum >> 16) + (sum & 0xffff);
    sum += (sum >> 16);
    answer = ~sum;
    return answer;
}

const int DELAY_BUF_SIZE = 40960;
const unsigned int MTU = 2048;
const double DELAY_MS = 0; // 延迟发送的时间间隔

struct DelayedPacket {
    char data[MTU];
    int size;
    std::chrono::high_resolution_clock::time_point send_time;
    bool valid = false;
};

DelayedPacket delay_buf[DELAY_BUF_SIZE];
int head = 0;
int tail = 0;
std::mutex buf_mutex;
std::condition_variable buf_cv;

void delay_sender_thread(int sockfd) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset); // 绑定到CPU核心2
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    while (!stop_event) {
        while (tail != head) {
            DelayedPacket &pkt = delay_buf[tail];
            if (pkt.valid)/*&& std::chrono::high_resolution_clock::now() >= pkt.send_time)*/ { 
                sendto(sockfd, pkt.data, pkt.size, 0, (struct sockaddr *)&from, fromlen);
                pkt.valid = false;
                tail = (tail + 1) % DELAY_BUF_SIZE;
            } else {
                break;
            }
        }
        // wait_for(buf_cv, std::chrono::milliseconds(DELAY_MS), [] {
        //     return stop_event || (tail != head && delay_buf[tail].valid && 
        //            std::chrono::high_resolution_clock::now() >= delay_buf[tail].send_time);
        // });
    }
}

void sniff_lucp()
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset); // 绑定到CPU核心3
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    int sockfd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
    if (sockfd < 0) {
        std::cerr << "Error creating raw socket for sniffing ICMP" << std::endl;
        return;
    }
    auto delay_ms = std::chrono::nanoseconds(int(DELAY_MS * 1000000)); // 转换为纳秒
    while (!stop_event)
    {
        // 直接在环形缓冲区接收数据
        delay_buf[head].size = recvfrom(sockfd, delay_buf[head].data, MTU, MSG_DONTWAIT, (struct sockaddr *)&from, &fromlen);
        if (delay_buf[head].size < 0 && errno == EWOULDBLOCK)
            continue;
        else if (delay_buf[head].size == 0) {
            std::cout << "the peer is closed." << std::endl;
            break;
        }
        // auto now_time = std::chrono::high_resolution_clock::now();
        struct iphdr *ip_header = (struct iphdr *)delay_buf[head].data;
        auto it = dst_port_cache.find(ip_header->daddr);
        // cout << "dst_ip :"  << inet_ntoa(*((struct in_addr*)&(ip_header->daddr))) << endl;
        if (it == dst_port_cache.end())
            continue;
        if (ip_header->protocol == IPPROTO_ICMP) {
            struct LucpPacket *lucp_packet = (struct LucpPacket *)(delay_buf[head].data + sizeof(struct iphdr));
            if (lucp_packet->hdr.type == ICMP_INFO_REQUEST) {
                lucp_packet->payload.r_c += 1; // 指示当前路由器
                // 包中过载率和公平速率是否更新的判定
                {
                    std::lock_guard<std::mutex> lock(it->second->cache_mutex);
                    // 如果当前路由器的过载率大于包中的过载率，
                    // 或者当前路由器的公平速率小于包中的公平速率
                    // 则更新包中的过载率和公平速率
                    if ((it->second->o_l > lucp_packet->payload.o_s && fabs(it->second->o_l - lucp_packet->payload.o_s) > 1e-6) ||
                        (fabs(it->second->o_l - lucp_packet->payload.o_s) < 1e-6 && it->second->g_l < lucp_packet->payload.g_s))
                    {
                        lucp_packet->payload.r_s = lucp_packet->payload.r_c; // 建议拥塞路由器 <= 当前路由器
                        lucp_packet->payload.g_s = it->second->g_l;
                        lucp_packet->payload.o_s = it->second->o_l;
                    }
                    // 当前路由器 = 实际拥塞路由器，则增加流数，否则增加非瓶颈流量
                    if (lucp_packet->payload.r_c == lucp_packet->payload.r_t)
                        it->second->n_l += 1;
                    else
                        it->second->u_l += lucp_packet->payload.x_r;
                    it->second->y_l += lucp_packet->payload.x_r;      // 总流量增加
                    lucp_packet->payload.n_iter = it->second->n_iter; // 将当前迭代次数写入
                }
                // 计算校验和
                lucp_packet->hdr.checksum = 0;
                lucp_packet->hdr.checksum = in_cksum((unsigned short *)lucp_packet, sizeof(LucpPacket));
                // 原路径转发回网络中
                // delay_buf[head].send_time = now_time + delay_ms; // 延迟发送
                delay_buf[head].valid = true;
                head = (head + 1) % DELAY_BUF_SIZE; // 更新环形缓冲区头部

                // sendto(sockfd, ip_header, sizeof(iphdr) + sizeof(LucpPacket), 0, (struct sockaddr *)&from, fromlen);
#ifdef DEBUG
                if (ntohs(lucp_packet->hdr.un.echo.sequence) == 0) // for debug，报告新流到来
                    cout << "new flow id " << lucp_packet->hdr.un.echo.id << "arrives." << endl;
#endif
            }
            else
            {
                // delay_buf[head].send_time = now_time + delay_ms; // 延迟发送
                delay_buf[head].valid = true;
                head = (head + 1) % DELAY_BUF_SIZE; // 更新环形缓冲区头部
                // sendto(sockfd, delay_buf[head].data, delay_buf[head].size, 0, (struct sockaddr *)&from, fromlen);
            }
        }
        else if (ip_header->protocol == IPPROTO_UDP)
        {
            // 缓存延迟发送
            // cout << "UDP packet received, size: " << delay_buf[head].size << endl;
            // delay_buf[head].send_time = now_time + delay_ms; // 延迟发送
            delay_buf[head].valid = true;
            head = (head + 1) % DELAY_BUF_SIZE; // 更新环形缓冲区头部
            // sendto(sockfd, delay_buf[head].data, delay_buf[head].size, 0, (struct sockaddr *)&from, fromlen);
        }
    }
    stop_event = true;
    close(sockfd);
}

// 定时统计线程
void collect_thread_func() {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(4, &cpuset); // 绑定到CPU核心4
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    auto collect_interval = std::chrono::duration_cast<nanoseconds>(duration<double>(COLLECT_INTERVAL));
    auto timeout = high_resolution_clock::now() + collect_interval;
    while (!stop_event) {
        auto t = high_resolution_clock::now();
        if (t >= timeout) {
            for (int i = 0; i < WIRE_NUM; i++) {
                switch_cache[i]->collect();
            }
            timeout += collect_interval;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(10)); // 防止空转
    }
}

// main函数启动两个线程
int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <switch_config_file>" << std::endl;
        return 1;
    }
    parse_switch_config(argv[1]);
    std::thread sniff_thread(sniff_lucp);
    std::thread collect_thread(collect_thread_func);
    std::thread delay_thread(delay_sender_thread, socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP)));
    sniff_thread.join();
    delay_thread.join();
    collect_thread.join();
    destroy_switch();
    return 0;
}
