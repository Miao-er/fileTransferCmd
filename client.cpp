
#include <unistd.h>
#include <iostream>
#include <signal.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <string>
#include <thread>
#include <unordered_map>

#include "LocalConf.h"
#include "ClientInfo.h"
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

//ip port filename
int main(int narg, char *argv[])
{
    if(narg != 5)
    {
        cout << "Usage: " << argv[0] << "<local_ip> <ip> <port> <filepath>" << endl;
        return -1;
    }
    LocalConf local_conf(getConfigPath());
    if(local_conf.loadConf())
        return -1;
    signal(SIGPIPE, SIG_IGN);
    // Create an hdRDMA object
    HwRdma hwrdma(local_conf.getRdmaGidIndex(), (uint64_t)-1, inet_addr(argv[1]));
    cout << "Client bind to addr: " << argv[1] << endl;
    // HwRdma hwrdma(local_conf.getRdmaGidIndex(), 1024UL * local_conf.getBlockSize() * local_conf.getBlockNum() * local_conf.getMaxThreadNum());
    if(hwrdma.init())
    {
        cout << "ERROR: initializing hwrdma!" << endl;
        return -1;
    }
    {
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = inet_addr(argv[2]);
        addr.sin_port = htons(atoi(argv[3]));
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        auto ret = connect(sockfd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret != 0)
        {
            std::cout << "ERROR: connecting to server: " << argv[2] << ":" << argv[3] << std::endl;
            return -1;
        }
        std::shared_ptr<int> x(NULL, [&](int *){close(sockfd);});
        StreamControl stream_control(&hwrdma, sockfd, &local_conf,addr.sin_addr.s_addr, false);
        if (stream_control.createLucpContext())
            return -1;
        if (stream_control.connectPeer())
            return -1;
        if (stream_control.bindMemoryRegion())
            return -1;
        if (stream_control.createBufferPool())
            return -1;
        std::cout << "Connected to " << argv[2] << ":" << argv[3] << std::endl;
        std::cout << "Sending file: " << argv[4] << std::endl;
        char filename[256];
        if(parse_filename(argv[4], filename) < 0)
        {
            cout << "ERROR: parsing filename failed." << endl;
            return -1;
        }
        if(stream_control.postSendFile(argv[4], filename) < 0)
        {
            cout << "ERROR: postSendFile failed." << endl;
            return -1;
        }
    }

    return 0;
}

