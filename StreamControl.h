#ifndef STREAM_CONTROL_H
#define STREAM_CONTROL_H
class StreamControl;
#include <infiniband/verbs.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <tuple>
#include <vector>
#include <list>
#include <errno.h>
#include <exception>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <memory>

#include "HwRdma.h"
#include "LocalConf.h"
#include "ClientInfo.h"

struct QPInfo
{
    uint16_t lid;
    // uint16_t lucp_id;
    uint32_t qp_num;
    uint32_t block_num;
    uint32_t block_size;
    uint8_t gid[16];
} __attribute__((packed));

struct FileInfo
{
    char file_path[256];
    uint64_t file_size;
} __attribute__((packed));


class StreamControl
{
private:
    uint8_t *buf_ptr = nullptr;
    struct ibv_mr *mr = nullptr;
    double default_rate;
    uint64_t block_size;
    HwRdma *hwrdma;
    std::vector<std::tuple<uint8_t *, uint64_t>> buffers;

    struct ibv_comp_channel *comp_channel = nullptr;
    struct ibv_cq *cq = nullptr;
    struct ibv_qp *qp = nullptr;
    QPInfo local_qp_info, remote_qp_info;
    ClientList *client_list = nullptr;
    LocalConf *local_conf = nullptr;
public:
    int peer_fd;
    StreamControl(HwRdma *hwrdma, int peer_fd, LocalConf *local_conf, ClientList *client_list = nullptr);

    ~StreamControl();
    int bindMemoryRegion();
    int createBufferPool();
    int createLucpContext();

    int sockSyncData(int xfer_size, char *local_data, char *remote_data);
    int changeQPState();
    int connectPeer();
    int prepareRecv();
    int postRecvFile();
    int postSendFile(const char *file_path, const char *file_name);
    int postRecvWr(uint64_t id);        
};

int recvData(HwRdma *hwrdma, int peer_fd,  LocalConf* local_conf, ClientList* client_list);

#endif