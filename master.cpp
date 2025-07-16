#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <sstream>

struct ClientInfo {
    std::string socket_ip;
    std::string local_ip;
    std::string server_ip;
    int port;
    std::string filepath;
};

void parse_master_config(const std::string& config_path, std::vector<ClientInfo>& clients) {
    std::ifstream fin(config_path);
    if (!fin.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return;
    }
    std::string line;
    while (std::getline(fin, line)) {
        // 去除首尾空白
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        ClientInfo client = {};
        if (!(iss >> client.socket_ip >> client.local_ip >> client.server_ip >> client.port >> client.filepath)) {
            std::cerr << "Invalid line in config: " << line << std::endl;
            continue;
        }
        clients.push_back(client);
    }
    fin.close();
}

void send_start_cmd(const ClientInfo& client, const std::string& cmd) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket error for " << client.local_ip << std::endl;
        return;
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2025);
    addr.sin_addr.s_addr = inet_addr(client.socket_ip.c_str());
    if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Connect error for " << client.local_ip << std::endl;
        close(sockfd);
        return;
    }
    // 发送START指令
    send(sockfd, cmd.c_str(), cmd.size(), 0);

    // 发送参数（local_ip, server_ip, port, filepath）
    if(cmd == "START") {
        std::string param = client.local_ip + " " + client.server_ip + " " + std::to_string(client.port) + " " + client.filepath;
        send(sockfd, param.c_str(), param.size(), 0);
        std::cout << "Sent START command to " << client.local_ip << std::endl;
    }
    else
        std::cout << "Sent END command to " << client.local_ip << std::endl;

    close(sockfd);
}

int main(int argc, char* argv[]) {
    // 假设你有一个客户端列表
    if(argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <command>(START/END)" << std::endl;
        return 1;
    }
    std::vector<ClientInfo> clients = {};
    parse_master_config("master.conf", clients);

    std::vector<std::thread> threads;
    for (const auto& client : clients) {
        threads.emplace_back(send_start_cmd, client, std::string(argv[1]));
    }
    for (auto& t : threads) t.join();

    std::cout << "All commands sent." << std::endl;
    return 0;
}