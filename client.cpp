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

void handle_transfer(const std::string& local_ip, const std::string& server_ip, int port, const std::string& filepath, bool use_message) {
    LocalConf local_conf(getConfigPath());
    if(local_conf.loadConf())
    {
        std::cerr << "Failed to load local configuration." << std::endl;
        return;
    }
    signal(SIGPIPE, SIG_IGN);
    HwRdma hwrdma(local_conf.getRdmaGidIndex(), (uint64_t)-1, inet_addr(local_ip.c_str()));
    if(hwrdma.init())
    {
        std::cerr << "Failed to initialize RDMA." << std::endl;
        return;
    }
    struct sockaddr_in addr;
    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    addr.sin_port = htons(port);
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(sockfd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        std::cerr << "Failed to connect to server at " << server_ip << ":" << port << std::endl;
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
    char filename[256];
    if(parse_filename(filepath.c_str(), filename) < 0)
    {
        std::cerr << "Failed to parse filename from filepath: " << filepath << std::endl;
        return;
    }
    stream_control.postSendFile(filepath.c_str(), filename);
}

int main(int argc, char* argv[])
{
    int listen_port = 2025; // 客户端监听端口
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in listen_addr;
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_addr.s_addr = INADDR_ANY;
    listen_addr.sin_port = htons(listen_port);
    bind(listen_fd, (sockaddr*)&listen_addr, sizeof(listen_addr));
    listen(listen_fd, 5);

    std::cout << "Client listening on port " << listen_port << std::endl;

    while (true) {
        int conn_fd = accept(listen_fd, nullptr, nullptr);
        if (conn_fd < 0) continue;
        char buf[1024] = {0};
        recv(conn_fd, buf, sizeof(buf), 0);
        std::string cmd(buf);
        cout << "MASTER CMD:" << cmd << endl;
        if (cmd.find("START") == 0) {
            // 解析参数
            bool use_message;
            if(cmd.find("UP") != std::string::npos)
                use_message = true;
            else use_message = false;
            char param_buf[1024] = {0};
            int n = recv(conn_fd, param_buf, sizeof(param_buf), 0);
            if(n <= 0) {
                std::cerr << "Failed to receive parameters." << std::endl;
                close(conn_fd);
                continue;
            }
            std::string param(param_buf);
            std::istringstream iss(param);
            std::string local_ip, server_ip, port_str, filepath;
            iss >> local_ip >> server_ip >> port_str >> filepath;
            cout << "params: " << "local_ip=" << local_ip << ", server_ip=" << server_ip << ", port=" << port_str << ", filepath=" << filepath << std::endl;
            int port = std::stoi(port_str);
            std::thread(handle_transfer, local_ip, server_ip, port, filepath, use_message).detach();
        }
        else
        if (cmd.find("END") == 0) {
            std::cout << "Received END command, stopping transfer." << std::endl;
            close(conn_fd);
            break; // 停止传输
        }
        else {
            std::cerr << "Unknown command: " << cmd << std::endl;
        }
        close(conn_fd);
    }
    return 0;
}