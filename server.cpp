
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

int main(int narg, char *argv[])
{
    LocalConf local_conf(getConfigPath());
    if(local_conf.loadConf())
        return -1;
    signal(SIGPIPE, SIG_IGN);
    if(narg != 2)
    {
        cout << "Usage: " << argv[0] << " <bind_ip>" << endl;
        return -1;
    }
    // Create an hdRDMA object
    HwRdma hwrdma(local_conf.getRdmaGidIndex(), 1024UL * local_conf.getBlockSize() * local_conf.getBlockNum() * local_conf.getMaxThreadNum(), inet_addr(argv[1]));
    cout << "Server bind to addr: " << argv[1] << endl;
    if(hwrdma.init())
    {
        cout << "ERROR: initializing hwrdma!" << endl;
        return -1;
    }
    {
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(local_conf.getLocalPort());

        int reuse = 1;
        auto server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
        setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
        auto ret = bind(server_sockfd, (struct sockaddr *)&addr, sizeof(addr));
        if (ret != 0)
        {
            cout << "ERROR: binding server socket!" << endl;
            return -1;
        }
        listen(server_sockfd, local_conf.getMaxThreadNum());

        // Loop forever accepting connections
        cout << "Listening for connections on port ... " << local_conf.getLocalPort() << endl;
        ClientList client_list;
        while (1)
        {
            int peer_sockfd = -1;
            struct sockaddr_in peer_addr;
            socklen_t peer_addr_len = sizeof(struct sockaddr_in);
            peer_sockfd = accept(server_sockfd, (struct sockaddr *)&peer_addr, &peer_addr_len);
            if (peer_sockfd < 0)
            {
                cout << "Failed connection!  errno=" << errno << endl;
                continue;
            }
            {
                if(client_list.getClientNum() >= local_conf.getMaxThreadNum())
                {
                    close(peer_sockfd);
                    continue;
                }
                client_list.addClient(peer_sockfd, peer_addr.sin_addr.s_addr);
                cout << "Connection from " << inet_ntoa(peer_addr.sin_addr) << endl;
            }

            // Create a new thread to handle this connection
            std::thread thr(recvData, &hwrdma, peer_sockfd, &local_conf, &client_list);
            thr.detach();
        }
    }

    return 0;
}

