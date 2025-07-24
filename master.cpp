#include <iostream>
#include <vector>
#include <thread>
#include <string>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cstring>

#define IP_ADDR_LEN 16
struct ClientInfo {
    char socket_ip[IP_ADDR_LEN];
    char local_ip[IP_ADDR_LEN];
    char server_ip[IP_ADDR_LEN];
    int port;
    uint64_t file_size; // in bytes
    double delay; // in seconds
};
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
    uint64_t file_size; // in bytes
    double delay; // in seconds
};
void parse_size(const char* size_str, uint64_t* size) {
    if (size_str == nullptr || size == nullptr) 
    {   
        *size = (uint64_t) -1; // Invalid size
        return;
    }
    *size = 0;
    std::string str(size_str);
    if (str.back() == 'G' || str.back() == 'g') {
        str.pop_back();
        *size = std::stoull(str) * 1000 * 1000 * 1000; // GB to bytes
    } else if (str.back() == 'M' || str.back() == 'm') {
        str.pop_back();
        *size = std::stoull(str) * 1000 * 1000; // MB to bytes
    } else if (str.back() == 'K' || str.back() == 'k') {
        str.pop_back();
        *size = std::stoull(str) * 1000; // KB to bytes
    } else {
        *size = std::stoull(str); // bytes
    }
}

void parse_delay(const char* delay_str, double* delay) {
    if (delay_str == nullptr || delay == nullptr) 
    {   
        *delay = -1; // Invalid delay
        return;
    }
    std::string str(delay_str);
    if (str.back() == 's' || str.back() == 'S') {
        str.pop_back();
        *delay = std::stod(str); // seconds
    }
    else *delay = -1; // Invalid delay
}

int parse_master_config(const std::string& config_path, std::vector<ClientInfo>& clients) {
    std::ifstream fin(config_path);
    if (!fin.is_open()) {
        std::cerr << "Failed to open config file: " << config_path << std::endl;
        return -1;
    }
    std::string line;
    while (std::getline(fin, line)) {
        // 去除首尾空白
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        ClientInfo client = {};
        char file_size_str[16], delay_str[16];
        if (!(iss >> client.socket_ip >> client.local_ip >> client.server_ip >> client.port >> file_size_str >> delay_str)) {
            std::cerr << "Invalid line in config: " << line << std::endl;
            return -1;
        }
        parse_size(file_size_str, &client.file_size);
        parse_delay(delay_str, &client.delay);
        if (client.file_size == (uint64_t) -1) {
            std::cerr << "Invalid file size in config: " << file_size_str << std::endl;
            return -1;
        }
        if (client.delay < 0) {
            std::cerr << "Invalid delay in config: " << delay_str << std::endl;
            return -1;
        }
        clients.push_back(client);
    }
    fin.close();
    return 0;
}

void send_start_cmd(const ClientInfo& client, const std::string& cmd) {
        // 发送START指令
    cmd_info cmd_info;
    if (cmd == "DOWN") {
        cmd_info.cmd_type = CMD_START_WITH_DCQCN;
    } else if (cmd == "UP") {
        cmd_info.cmd_type = CMD_START_WITH_LUCP;
    } else if (cmd == "END") {
        cmd_info.cmd_type = CMD_END;
    } else {
        std::cerr << "Unknown command: " << cmd << std::endl;
        return;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Socket error for " << client.local_ip << std::endl;
        return;
    }
    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2025);
    addr.sin_addr.s_addr = inet_addr(client.socket_ip);
    if (connect(sockfd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Connect error for " << client.socket_ip << std::endl;
        close(sockfd);
        return;
    }
    cmd_info.port = client.port;
    strncpy(cmd_info.local_ip, client.local_ip, IP_ADDR_LEN);
    strncpy(cmd_info.server_ip, client.server_ip, IP_ADDR_LEN);
    cmd_info.file_size = client.file_size;
    cmd_info.delay = client.delay;

    send(sockfd, (char*)&cmd_info, sizeof(cmd_info), 0);

    // 发送参数（local_ip, server_ip, port, file_size）
    if(cmd == "UP") {
        std::cout << "Sent START_WITH_LUCP command to " << client.local_ip << std::endl;
    }
    else if(cmd == "DOWN") {
        std::cout << "Sent START_WITH_DCQCN command to " << client.local_ip << std::endl;
    }
    else
        std::cout << "Sent END command to " << client.local_ip << std::endl;

    close(sockfd);
}

int main(int argc, char* argv[]) {
    // 假设你有一个客户端列表
    if(argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <command>(START-UP/START-DOWN/END)" << std::endl;
        return 1;
    }
    std::vector<ClientInfo> clients = {};
    if(parse_master_config("master.conf", clients) < 0) {
        std::cerr << "Failed to parse master configuration." << std::endl;
        return 1;
    }

    std::vector<std::thread> threads;
    for (const auto& client : clients) {
        threads.emplace_back(send_start_cmd, client, std::string(argv[1]));
    }
    for (auto& t : threads) t.join();

    std::cout << "All commands sent." << std::endl;
    return 0;
}
