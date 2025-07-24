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
    void collect()
    {
        if (y_l <= line_rate * SEND_TIMES)
            o_l = 0;
        else
            o_l = (y_l - line_rate * SEND_TIMES) / y_l;

        if (n_l < SEND_TIMES)
            g_l = line_rate; // - my_cache.u_l / 5;
        else
            g_l = (line_rate * SEND_TIMES - u_l) / n_l;
   //     fout <<"iter:"<< n_iter + 1 <<"\tn:" << n_l << "\tu:" << u_l << "\ty:" << y_l
   //       << "\tg_l:"<<g_l<<"\to_l:"<<o_l<<endl;
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

// 嗅探 ICMP 数据包的线程函数
void sniff_lucp()
{
    // packet 收包计数
    int count = 0;

    // 创建可以抓取非发往本地IP的三层网络包的套接字
    int sockfd = socket(AF_PACKET, SOCK_DGRAM, htons(ETH_P_IP));
    if (sockfd < 0)
    {
        std::cerr << "Error creating raw socket for sniffing ICMP" << std::endl;
        return;
    }
    // 描述接收数据包时的以太网链路层的对端信息，实际上值为交换机连接本机的出端口信息
    // struct sockaddr_ll from;
    // socklen_t fromlen = sizeof(sockaddr_ll);
    const unsigned int MTU = 4500;                                                          // 主机MTU设置最大为4500
    char buffer[MTU];                                                                       // 用于接收报文的buffer
    auto collect_interval = duration_cast<nanoseconds>(duration<double>(COLLECT_INTERVAL)); // 统计更新间隔，用duration类表示
    auto timeout = high_resolution_clock::now() + collect_interval; // 首次超时时间 = 当前时刻 + 更新间隔
    auto t = timeout;
    while (!stop_event)
    {
        // 判断当前时刻是否触发更新
        t = high_resolution_clock::now();
        if (t >= timeout)
        {
            for (int i = 0; i < WIRE_NUM; i++){
                switch_cache[i]->collect();
	    }
           	 timeout += collect_interval; // 更新下次超时时间
        }
        // 非阻塞模式嗅探报文
        int data_size = recvfrom(sockfd, buffer, MTU, MSG_DONTWAIT, (struct sockaddr *)&from, &fromlen);
        // 非阻塞模式下，无报文产生立即返回，跳过
        if (data_size < 0 && errno == EWOULDBLOCK)
            continue;
        // 不知名原因
        else if (data_size == 0)
        {
            std::cout << "the peer is closed." << std::endl;
            break;
        }
        // buffer = ip_hdr + icmp_hdr + icmp_payload(our protocol)
        struct iphdr *ip_header = (struct iphdr *)buffer;
       
        if (ip_header->protocol == IPPROTO_ICMP) // icmp(proto.1) packet
        {
            struct LucpPacket *lucp_packet = (struct LucpPacket *)(buffer + sizeof(struct iphdr)); // icmp_hdr + icmp_payload
            if (lucp_packet->hdr.type == ICMP_INFO_REQUEST && dst_port_cache.find(ip_header->daddr) != dst_port_cache.end())                                                       // ->our protocol request (if type == 16 -> our protocol reply)
            {

                count += 1;                    // 包统计
                lucp_packet->payload.r_c += 1; // 指示当前路由器
                // 包中过载率和公平速率是否更新的判定
                if ((dst_port_cache[ip_header->daddr]->o_l > lucp_packet->payload.o_s && fabs(dst_port_cache[ip_header->daddr]->o_l - lucp_packet->payload.o_s) > 1e-6) ||
                    (fabs(dst_port_cache[ip_header->daddr]->o_l - lucp_packet->payload.o_s) < 1e-6 && dst_port_cache[ip_header->daddr]->g_l < lucp_packet->payload.g_s))
                {
                    lucp_packet->payload.r_s = lucp_packet->payload.r_c; // 建议拥塞路由器 <= 当前路由器
                    lucp_packet->payload.g_s = dst_port_cache[ip_header->daddr]->g_l;
                    lucp_packet->payload.o_s = dst_port_cache[ip_header->daddr]->o_l;
                }
                // 当前路由器 = 实际拥塞路由器，则增加流数，否则增加非瓶颈流量
                if (lucp_packet->payload.r_c == lucp_packet->payload.r_t)
                    dst_port_cache[ip_header->daddr]->n_l += 1;
                else
                    dst_port_cache[ip_header->daddr]->u_l += lucp_packet->payload.x_r;
                dst_port_cache[ip_header->daddr]->y_l += lucp_packet->payload.x_r;      // 总流量增加
                lucp_packet->payload.n_iter = dst_port_cache[ip_header->daddr]->n_iter; // 将当前迭代次数写入
                // 计算校验和
                lucp_packet->hdr.checksum = 0;
                lucp_packet->hdr.checksum = in_cksum((unsigned short *)lucp_packet, sizeof(LucpPacket));
                // 原路径转发回网络中
                sendto(sockfd, ip_header, sizeof(iphdr) + sizeof(LucpPacket), 0, (struct sockaddr *)&from, fromlen);
                if (ntohs(lucp_packet->hdr.un.echo.sequence) == 0) // for debug，报告新流到来
                    cout << "new flow id " << lucp_packet->hdr.un.echo.id << "arrives." << endl;
            }
            else if(dst_port_cache.find(ip_header->daddr) != dst_port_cache.end()) // 其他ICMP包，可能是回复
            {
                // cout << "not target packet, just forward." << endl;
                sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&from, fromlen);
            }
        }
        else if (ip_header->protocol == IPPROTO_UDP && dst_port_cache.find(ip_header->daddr) != dst_port_cache.end()) // udp(proto.17) packet
        {
            sendto(sockfd, buffer, data_size, 0, (struct sockaddr *)&from, fromlen);
        }
    }
    close(sockfd);
}


int main(int argc, char *argv[])
{
    if(argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <switch_config_file>" << std::endl;
        return 1;
    }
    parse_switch_config(argv[1]); // 解析交换机配置文件
    std::thread sniff_thread(sniff_lucp);
    sniff_thread.join();
    destroy_switch();
    return 0;
}
