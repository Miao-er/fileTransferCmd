#ifndef CLIENT_INFO_H
#define CLIENT_INFO_H

#include <string>
#include <stdint.h>
#include <unordered_map>
#include <mutex>
class ClientInfo;

enum ClientStatus 
{
    CLIENT_STATUS_INVALID = -1,
    CLIENT_STATUS_IDLE,
    CLIENT_STATUS_RECEIVING
};
// struct FileInfo
// {

//     std::string fileName;
//     uint64_t fileSize;
//     uint64_t offset;
//     ClientInfo* client;
//     FileInfo():fileName(""), fileSize(0), offset(0), client(nullptr) {}
//     FileInfo(const std::string& fn, uint64_t fs, ClientInfo* c)
//         : fileName(fn), fileSize(fs), offset(0), client(c) {}
// };

struct ClientInfo 
{
    int fd;
    uint32_t ip;
    // 状态
    ClientStatus clientStat;
    // FileInfo fileInfo;
    ClientInfo()
    : fd(-1), ip(0), clientStat(CLIENT_STATUS_INVALID) {}
    ClientInfo(int f, uint32_t i) 
    : fd(f), ip(i), clientStat(CLIENT_STATUS_IDLE){}
};

class ClientList{
private:
    std::unordered_map<int, ClientInfo> clientInfos;
    std::mutex clientInfosMutex;
public:
    ClientList(){
        clientInfos.clear();
    }
    void addClient(int fd, uint32_t ip){
        std::lock_guard<std::mutex> lock(clientInfosMutex);
        clientInfos.emplace(fd, ClientInfo(fd, ip));
    }
    void removeClient(int fd){
        std::lock_guard<std::mutex> lock(clientInfosMutex);
        if(clientInfos.find(fd) != clientInfos.end())
            clientInfos.erase(fd);
    }
    int getClientNum()
    {
        std::lock_guard<std::mutex> lock(clientInfosMutex);
        return clientInfos.size();
    }
    ClientInfo& getClientInfo(int fd){
        std::lock_guard<std::mutex> lock(clientInfosMutex);
        assert(clientInfos.find(fd) != clientInfos.end());
        return clientInfos[fd];
    }
};

#endif