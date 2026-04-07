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
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
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
#include <signal.h>

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

void switch_promisc(int sockfd, bool enable, const char* iface = "enp2s0")
{
    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ);
    if (ioctl(sockfd, SIOCGIFFLAGS, &ifr) < 0) {
        perror("ioctl (SIOCGIFFLAGS)");
        close(sockfd);
        return;
    }
    if(enable)
        ifr.ifr_flags |= IFF_PROMISC;
    else
        ifr.ifr_flags &= ~IFF_PROMISC;
    if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) < 0) {
        perror("ioctl (SIOCSIFFLAGS)");
        close(sockfd);
        return;
    }
}

bool stop_event = false; // 嗅探循环停止信号，暂时无用

void stop_handler(int sig)
{
    stop_event = true;
}

unsigned char if_mac[6] = {0xb0,0x51,0x8e,0xf6,0x36,0x13};
int parse_mac_str(const char *mac_str)
{
    char *endptr;
    int i = 0;
    for (; i < 6; i++) {
        // 跳过非十六进制字符（如分隔符）
        while (*mac_str && !isxdigit(*mac_str)) mac_str++;
        if (!*mac_str) return -1; // 提前结束
        
        if_mac[i] = (uint8_t)strtol(mac_str, &endptr, 16);
        mac_str = endptr; // 移动到下一部分
    }
    printf("local MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
           	if_mac[0], if_mac[1], if_mac[2], if_mac[3], if_mac[4], if_mac[5]);
    return 0;
}
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
    enum Section { NONE, MAC, PORT, ROUTE } section = NONE;
    while (std::getline(fin, line)) {
        // 去除首尾空白
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        if(line == "[Mac]") {
            section = MAC;
            continue;
        }
        if (line == "[Port]") {
            section = PORT;
            continue;
        }
        if (line == "[Route]") {
            section = ROUTE;
            continue;
        }
        std::istringstream iss(line);
        if (section == MAC) {
            int ret = parse_mac_str(line.c_str());
            if(ret < 0)
            {
                cout << "wrong mac address\n" << endl;
                exit(-1);
            }
        } 
        else if (section == PORT) {
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

const int DELAY_BUF_SIZE = 4096;
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
std::mutex buf_mutex;
std::condition_variable buf_cv;


void sniff_lucp()
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset); // 绑定到CPU核心3
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_IP));
    if (sockfd < 0) {
        std::cerr << "Error creating raw socket for sniffing ICMP" << std::endl;
        return;
    }
    const char *iface = "enp2s0";
    struct sockaddr_ll addr;
    memset(&addr, 0, sizeof(addr));
    addr.sll_family = AF_PACKET;
    addr.sll_protocol = htons(ETH_P_IP);
    addr.sll_ifindex = if_nametoindex(iface);
    if (addr.sll_ifindex == 0) { 
        perror("if_nametoindex"); 
        return; 
    }

    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sockfd); return ;
    }
    switch_promisc(sockfd, true, iface);

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
	    struct ethhdr *eth = (struct ethhdr*)delay_buf[head].data;
        struct iphdr *ip_header = (struct iphdr*)(delay_buf[head].data + sizeof(struct ethhdr));
        auto it = dst_port_cache.find(ip_header->daddr);
        if (it == dst_port_cache.end())
            continue;
        /*printf("\n[帧长度: %ld]MAC: %02X:%02X:%02X:%02X:%02X:%02X → %02X:%02X:%02X:%02X:%02X:%02X\n", 
            delay_buf[head].size,
            eth->h_source[0], eth->h_source[1], eth->h_source[2],
            eth->h_source[3], eth->h_source[4], eth->h_source[5],
            eth->h_dest[0], eth->h_dest[1], eth->h_dest[2],
            eth->h_dest[3], eth->h_dest[4], eth->h_dest[5]);
        char src_addr[16], dst_addr[16];
        strcpy(src_addr, inet_ntoa(*(struct in_addr*)&ip_header->saddr));
        strcpy(dst_addr, inet_ntoa(*(struct in_addr*)&ip_header->daddr));
        printf("IP 长度: %d | IP: %s → %s | %s\n",
            ntohs(ip_header->tot_len),
            src_addr,dst_addr,
	    ip_header->protocol == IPPROTO_ICMP ? "ICMP" :
            ip_header->protocol == IPPROTO_UDP ? "UDP" : "其他");
	*/
        // memcpy(eth->h_dest, eth->h_source, 6);
        // memcpy(from.sll_addr, eth->h_source, 6);
        memcpy(eth->h_source, if_mac, 6);
        // ip_header->daddr = ip_header->saddr;
        // ip_header->saddr = inet_addr("10.2.152.201");
        memcpy(from.sll_addr, eth->h_dest, 6);
        if (ip_header->protocol == IPPROTO_ICMP) {
            struct LucpPacket *lucp_packet = (struct LucpPacket *)((char*)ip_header + sizeof(struct iphdr));
            if (lucp_packet->hdr.type == ICMP_INFO_REQUEST || lucp_packet->hdr.type == ICMP_INFO_REPLY) {
		        // lucp_packet->hdr.type = ICMP_INFO_REPLY;
                
                // 包中过载率和公平速率是否更新的判定
                if(lucp_packet->hdr.type == ICMP_INFO_REQUEST)
                {
                    lucp_packet->payload.r_c += 1; // 指示当前路由器
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
                    lucp_packet->hdr.checksum = 0;
                    lucp_packet->hdr.checksum = in_cksum((unsigned short *)lucp_packet, sizeof(LucpPacket));
                }
                // 计算校验和
                // 原路径转发回网络中
                // delay_buf[head].send_time = now_time + delay_ms; // 延迟发送
	//	cout << "recv lucp packet, x_r is :" << lucp_packet->payload.x_r << ", seq: " << htons(lucp_packet->hdr.un.echo.sequence)  << endl;
            }
            cout << "recv icmp packet, type is :" << (int)lucp_packet->hdr.type << ", seq: " << htons(lucp_packet->hdr.un.echo.sequence)  << endl;
            sendto(sockfd, delay_buf[head].data,delay_buf[head].size, 0, (struct sockaddr *)&from, fromlen);
        }
    }
    stop_event = true;
    switch_promisc(sockfd, false, iface);
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
    signal(SIGINT, stop_handler);
	signal(SIGTERM, stop_handler);
    parse_switch_config(argv[1]);
    std::thread sniff_thread(sniff_lucp);
    std::thread collect_thread(collect_thread_func);
    //std::thread delay_thread(delay_sender_thread, socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP)));
    sniff_thread.join();
    //delay_thread.join();
    collect_thread.join();
    destroy_switch();
    return 0;
}
