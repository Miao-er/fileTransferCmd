#include <unistd.h>
#include <iostream>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string>
#include <thread>
#include <cstring>
#include <sstream>
#include "LocalConf.h"
#include "HwRdma.h"
#include "StreamControl.h"
using namespace std;
#define IP_ADDR_LEN 16
enum CMD_TYPE
{
    CMD_END = 0,
    CMD_START_WITH_DCQCN = 1,
    CMD_START_WITH_LUCP = 2
};
struct cmd_info
{
    enum CMD_TYPE cmd_type;
    int port;
    char local_ip[IP_ADDR_LEN];
    char server_ip[IP_ADDR_LEN];
    uint64_t file_size;
    double delay; // in seconds
};

int parse_filename(const char* filepath, char* filename)
{
    if (filepath == nullptr || filename == nullptr)
        return -1;
    const char* last_slash = strrchr(filepath, '/');
    if (last_slash != nullptr)
        strcpy(filename, last_slash + 1);
    else
        strcpy(filename, filepath);
    return 0;
}

void handle_transfer(cmd_info cmd_info, bool use_message) {
    usleep(cmd_info.delay * 1000000); // 等待指定的延迟时间
    LocalConf local_conf(getConfigPath());
    if(local_conf.loadConf())
    {
        std::cerr << "Failed to load local configuration." << std::endl;
        return;
    }
    signal(SIGPIPE, SIG_IGN);
    HwRdma hwrdma(local_conf.getRdmaGidIndex(), (uint64_t)-1, inet_addr(cmd_info.local_ip));
    if(hwrdma.init())
    {
        std::cerr << "Failed to initialize RDMA." << std::endl;
        return;
    }
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(cmd_info.server_ip);
    addr.sin_port = htons(cmd_info.port);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        std::cerr << "Failed to connect to server at " << cmd_info.server_ip << ":" << cmd_info.port << std::endl;
        close(sockfd);
        return;
    }
    std::shared_ptr<int> x(NULL, [&](int *){close(sockfd);});
    StreamControl stream_control(&hwrdma, sockfd, &local_conf, addr.sin_addr.s_addr, false, use_message);
    if (stream_control.createLucpContext())
        return;
    if (stream_control.connectPeer())
        return;
    if (stream_control.bindMemoryRegion())
        return;
    if (stream_control.createBufferPool())
        return;
    stream_control.postSendFile(cmd_info.file_size);
}

int main(int argc, char* argv[])
{
    int listen_port = 2025; // 客户端监听端口
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(listen_port);
    if(bind(listen_fd, (sockaddr*)&listen_addr, sizeof(listen_addr)))
    {
	cout << "bind error:  " << errno << endl;
	return -errno;
    }
    int opt_val = 1;
    if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt_val, sizeof(opt_val))){
        cout << "set reuseaddr error:  " << errno << endl;
        return -errno;
    }
    if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, &opt_val, sizeof(opt_val))){
        cout << "set reuseport error:  " << errno << endl;
        return -errno;
    }

    listen(listen_fd, 5);

    std::cout << "Client listening on port " << listen_port << std::endl;

    while (true) {
        int conn_fd = accept(listen_fd, nullptr, nullptr);
        if (conn_fd < 0) continue;
        struct cmd_info cmd_info;
        memset(&cmd_info, 0, sizeof(cmd_info));
        int recv_bytes = 0, total_bytes = 0;
        while(total_bytes < sizeof(cmd_info)) {
            recv_bytes = recv(conn_fd, ((char*)&cmd_info) + total_bytes, sizeof(cmd_info) - total_bytes, 0);
            if (recv_bytes <= 0) {
                std::cerr << "Failed to receive command info." << std::endl;
                close(conn_fd);
                break;
            }
            total_bytes += recv_bytes;
        }
        if (cmd_info.cmd_type == CMD_START_WITH_DCQCN || cmd_info.cmd_type == CMD_START_WITH_LUCP) {
            // 解析参数
            bool use_message;
            if(cmd_info.cmd_type == CMD_START_WITH_LUCP)
                use_message = true;
            else use_message = false;
            printf("Received START command: %s\n", cmd_info.cmd_type == CMD_START_WITH_LUCP? "using LUCP" : "using DCQCN");
            printf("params: local_ip=%s, server_ip=%s, port=%d, file_size=%lf GB\n",
                   cmd_info.local_ip, cmd_info.server_ip, cmd_info.port, cmd_info.file_size / 1e9);
            printf("process will start after %lf seconds\n", cmd_info.delay);
            std::thread(handle_transfer, cmd_info, use_message).detach();
        }
        else
        if (cmd_info.cmd_type == CMD_END) {
            std::cout << "Received END command, stopping transfer." << std::endl;
            close(conn_fd);
            break; // 停止传输
        }
        else {
            std::cerr << "Unknown command: " << cmd_info.cmd_type << std::endl;
        }
        close(conn_fd);
    }
    return 0;
}
