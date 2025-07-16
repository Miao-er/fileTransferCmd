#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

struct ClientInfo {
    std::string ip;
    int port;
    std::string filepath;
    std::string local_ip;
};

void send_start_cmd(const ClientInfo& client, const std::string& cmd) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket error for " << client.ip << std::endl;
        return;
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2025);
    addr.sin_addr.s_addr = inet_addr(client.ip.c_str());
    if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Connect error for " << client.ip << std::endl;
        close(sockfd);
        return;
    }
    // 发送START指令
    send(sockfd, cmd.c_str(), cmd.size(), 0);

    // 发送参数（local_ip, server_ip, port, filepath）
    if(cmd == "START") {
        std::string param = client.local_ip + " " + client.ip + " " + std::to_string(client.port) + " " + client.filepath;
        send(sockfd, param.c_str(), param.size(), 0);
        std::cout << "Sent START command to " << client.ip << std::endl;
    }
    else
        std::cout << "Sent END command to " << client.ip << std::endl;

    close(sockfd);
}

int main(int argc, char* argv[]) {
    // 假设你有一个客户端列表
    if(argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <command>(START/END)" << std::endl;
        return 1;
    }
    std::vector<ClientInfo> clients = {
        {"192.168.11.2", 52025, "../test.file", "192.168.21.2"},
        {"192.168.12.2", 52025, "../test.file", "192.168.22.2"}
        // 可继续添加
    };

    std::vector<std::thread> threads;
    for (const auto& client : clients) {
        threads.emplace_back(send_start_cmd, client, std::string(argv[1]));
    }
    for (auto& t : threads) t.join();

    std::cout << "All commands sent." << std::endl;
    return 0;
}