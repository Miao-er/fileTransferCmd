#include <iostream>
#include <chrono>
#include <string>
#include <thread>
#include <sys/stat.h>
#include <deque>
#include <algorithm>
#include "StreamControl.h"
#define NO_IO
#define LONG_HAUL_SERVER_RC_BLOCK_SIZE 512
using std::chrono::high_resolution_clock;
using std::chrono::nanoseconds;
using std::chrono::duration;
using std::chrono::duration_cast;

StreamControl::StreamControl(HwRdma *hwrdma, int peer_fd, LocalConf *local_conf, uint32_t peer_addr, bool server_mode, bool use_message)
{
    this->hwrdma = hwrdma;
    this->peer_fd = peer_fd;
    this->default_rate = local_conf->getDefaultRate();
    this->local_conf = local_conf;
    this->peer_addr = peer_addr;
    this->server_mode = server_mode;
    this->use_message = use_message;
}

StreamControl::~StreamControl()
{
    if(rateController!= nullptr)
        delete rateController;
    if (qp != nullptr)
    {
        struct ibv_qp_attr qp_attr;
        bzero(&qp_attr, sizeof(qp_attr));
        qp_attr.qp_state = IBV_QPS_RESET;
        ibv_modify_qp(qp, &qp_attr, IBV_QP_STATE);
    }
    if (qp != nullptr)
        ibv_destroy_qp(qp);
    if (cq != nullptr)
        ibv_destroy_cq(cq);
    if (comp_channel != nullptr)
        ibv_destroy_comp_channel(comp_channel);
    if (mr != nullptr)
        hwrdma->destroy_mr(mr);
}
int StreamControl::bindMemoryRegion()
{
    size_t length = this->block_size * local_conf->getBlockNum();
    if (hwrdma->create_mr(&this->mr, &this->buf_ptr, length))
    {
        return -1;
    }
    return 0;
}
int StreamControl::createBufferPool()
{
    if (!this->buf_ptr || !this->mr)
    {
        cout << "ERROR: NULL memory region." << endl;
        return -1;
    }
    uint64_t loc = 0;

    while (loc + this->block_size <= this->mr->length)
    {
        buffers.emplace_back((uint8_t *)buf_ptr + loc, this->block_size);
        loc += this->block_size;
    }
    return 0;
}
int StreamControl::createLucpContext()
{
    // create cp_channel
    comp_channel = ibv_create_comp_channel(hwrdma->ctx);
    // create cq
    cq = ibv_create_cq(hwrdma->ctx, 2 * local_conf->getBlockNum(), NULL, comp_channel, 0);
    if (!cq)
    {
        cout << "ERROR: Unable to create Completion Queue" << endl;
        return -1;
    }
    // create qp
    struct ibv_qp_init_attr qp_init_attr;
    bzero(&qp_init_attr, sizeof(qp_init_attr));
    qp_init_attr.send_cq = cq;
    qp_init_attr.recv_cq = cq;
    qp_init_attr.cap.max_send_wr = local_conf->getBlockNum();
    qp_init_attr.cap.max_recv_wr = 2 * local_conf->getBlockNum();
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    if(this->use_message)
        qp_init_attr.qp_type = IBV_QPT_UC;
    else
        qp_init_attr.qp_type = IBV_QPT_RC;

    local_qp_info.lid = hwrdma->port_attr.lid;
    local_qp_info.block_num = local_conf->getBlockNum();
    if(this->use_message)
        local_qp_info.block_size = local_conf->getBlockSize();
    else
        local_qp_info.block_size = LONG_HAUL_SERVER_RC_BLOCK_SIZE;
    local_qp_info.lucp_id = duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count(); //TODO
    // local_qp_info.recv_depth = qp_init_attr.cap.max_recv_wr; //must before create qp,or max_recv_wr will change.
    memcpy(local_qp_info.gid, &hwrdma->gid, 16);
    // Create Queue Pair
    qp = ibv_create_qp(hwrdma->pd, &qp_init_attr);
    if (!qp)
    {
        cout << "ERROR: Unable to create QP!" << endl;
        return -1;
    }
    local_qp_info.qp_num = qp->qp_num;
    return 0;
}

int StreamControl::sockSyncData(int xfer_size, char *local_data, char *remote_data)
{
    int rc;
    int read_bytes = 0;
    int total_read_bytes = 0;
    while((rc = send(this->peer_fd, local_data, xfer_size, MSG_NOSIGNAL)) <= 0)
    {
        if (rc == 0 || (rc < 0 && errno != EINTR))
        {
            cout << "ERROR: Failed writing data during sock_sync_data." << endl;
            cout << "errno: " << errno << endl;
            cout << "strerror: " << strerror(errno) << endl;
            cout << "rc: " << rc << endl;
            cout << "peer_fd: " << peer_fd << endl;
            return -1;
        }
    }

    while (total_read_bytes < xfer_size)
    {
        read_bytes = read(this->peer_fd, remote_data, xfer_size);
        if (read_bytes > 0)
        {
            total_read_bytes += read_bytes;
        }
        else if(read_bytes < 0 && errno == EINTR)
            continue;
        else 
        {
            cout << "ERROR: Failed reading data during sock_sync_data." << endl;
            cout << "errno: " << errno << endl;
            cout << "strerror: " << strerror(errno) << endl;
            cout << "read_bytes: " << read_bytes << endl;
            cout << "peer_fd: " << this->peer_fd << endl;
            return -1;
        }
    }
    return 0;
}

int StreamControl::changeQPState()
{
    /* Change QP state to INIT */
    {
        struct ibv_qp_attr qp_attr;
        bzero(&qp_attr, sizeof(qp_attr));
        qp_attr.qp_state = IBV_QPS_INIT,
        qp_attr.pkey_index = 0,
        qp_attr.port_num = hwrdma->port_num,
        qp_attr.qp_access_flags = IBV_ACCESS_LOCAL_WRITE |
                                    IBV_ACCESS_REMOTE_READ |
                                    IBV_ACCESS_REMOTE_ATOMIC |
                                    IBV_ACCESS_REMOTE_WRITE;

        auto ret = ibv_modify_qp(this->qp, &qp_attr,
                                    IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                        IBV_QP_PORT | IBV_QP_ACCESS_FLAGS);
        if (ret != 0)
        {
            cout << "ERROR: Unable to set QP to INIT state!" << endl;
            return -1;
        }
    }
    /* Change QP state to RTR */
    {
        struct ibv_qp_attr qp_attr;
        bzero(&qp_attr, sizeof(qp_attr));
        qp_attr.qp_state = IBV_QPS_RTR,
        qp_attr.path_mtu = hwrdma->port_attr.active_mtu,
        qp_attr.dest_qp_num = this->remote_qp_info.qp_num,
        qp_attr.rq_psn = 0;
        if(!this->use_message)
        {
            qp_attr.max_dest_rd_atomic = 1,
            qp_attr.min_rnr_timer = 0x12,
            qp_attr.ah_attr.is_global = 0;
        }
        qp_attr.ah_attr.dlid = this->remote_qp_info.lid,
        qp_attr.ah_attr.sl = 0,
        qp_attr.ah_attr.src_path_bits = 0,
        qp_attr.ah_attr.port_num = hwrdma->port_num,

        qp_attr.ah_attr.is_global = 1,
        memcpy(&qp_attr.ah_attr.grh.dgid, remote_qp_info.gid, 16),
        qp_attr.ah_attr.grh.flow_label = 0,
        qp_attr.ah_attr.grh.hop_limit = 3, // TODO modify
        qp_attr.ah_attr.grh.sgid_index = hwrdma->gid_idx,
        qp_attr.ah_attr.grh.traffic_class = 0;

        auto ret = ibv_modify_qp(qp, &qp_attr,
                                    IBV_QP_STATE | IBV_QP_AV |
                                        IBV_QP_PATH_MTU | IBV_QP_DEST_QPN |
                                        IBV_QP_RQ_PSN | (this->use_message ? 0:(IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER)));
                                        // IBV_QP_RQ_PSN |IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER);
        if (ret != 0)
        {
            cout << "ERROR: Unable to set QP to RTR state!" << endl;
            return -1;
        }
    }

    /* Change QP state to RTS */
    {
        struct ibv_qp_attr qp_attr;
        bzero(&qp_attr, sizeof(qp_attr));
        qp_attr.qp_state = IBV_QPS_RTS;
        if(!this->use_message)
        {
            qp_attr.timeout = 20,
            qp_attr.retry_cnt = 7,
            qp_attr.rnr_retry = 0,
            qp_attr.max_rd_atomic = 1;
        }
        qp_attr.sq_psn = 0;

        auto ret = ibv_modify_qp(qp, &qp_attr,
                                    IBV_QP_STATE | IBV_QP_SQ_PSN
                                    | (this->use_message ? 0 : (IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_TIMEOUT | IBV_QP_MAX_QP_RD_ATOMIC)));
                                    // | (IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_TIMEOUT | IBV_QP_MAX_QP_RD_ATOMIC));
        if (ret != 0)
        {
            cout << "ERROR: Unable to set QP to RTS state!" << endl;
            return -1;
        }
    }
    return 0;
}
int StreamControl::connectPeer()
{
    char remote_ready_char;
    if(sockSyncData(1,"R",&remote_ready_char))
    {
        cout << "ERROR: connect failed when sync ready info." << endl;
        return -2;
    }
    if(remote_ready_char != 'R')
    {
        cout << "ERROR: remote not ready to connect." << endl;
        return -1;
    }
    QPInfo net_local_qp_info, net_remote_qp_info;
    net_local_qp_info.lid = htons(local_qp_info.lid);
    net_local_qp_info.lucp_id = htons(local_qp_info.lucp_id);
    net_local_qp_info.qp_num = htonl(local_qp_info.qp_num);
    net_local_qp_info.block_num = htonl(local_qp_info.block_num);
    net_local_qp_info.block_size = htonl(local_qp_info.block_size);
    //net_local_qp_info.recv_depth = htonl(local_qp_info.recv_depth);
    memcpy(net_local_qp_info.gid, local_qp_info.gid, 16);

    if (sockSyncData(sizeof(QPInfo), (char *)&net_local_qp_info, (char *)&net_remote_qp_info) < 0)
    {
        cout << "ERROR: connect failed when sync qpinfo." << endl;
        return -2;
    }
    remote_qp_info.lid = ntohs(net_remote_qp_info.lid);
    remote_qp_info.lucp_id = ntohs(net_remote_qp_info.lucp_id);
    remote_qp_info.qp_num = ntohl(net_remote_qp_info.qp_num);
    remote_qp_info.block_num = ntohl(net_remote_qp_info.block_num);
    remote_qp_info.block_size = ntohl(net_remote_qp_info.block_size);
    //remote_qp_info.recv_depth = ntohl(net_remote_qp_info.recv_depth);
    memcpy(remote_qp_info.gid, net_remote_qp_info.gid, 16);
    //client apply block size from server
    if(!this->server_mode)
        this->block_size = 1024UL * remote_qp_info.block_size;
    else
        this->block_size = 1024UL * (this->use_message? local_conf->getBlockSize() : LONG_HAUL_SERVER_RC_BLOCK_SIZE);
        // this->block_size = 1024UL * local_conf->getBlockSize();
    this->rateController = new RateController(this->default_rate, this->block_size);
    if(this->rateController->initSwap(hwrdma->bind_ip, this->peer_addr) < 0)
    {
        cout << "RateController init failed." << endl;
        return -1;
    }
    this->rateController->setId(remote_qp_info.lucp_id ^ local_qp_info.lucp_id);
    this->rateController->startSwap();
#ifndef DEBUG
    cout << "     local:" << endl
    << "       lid:" << local_qp_info.lid << endl
    << "    qp_num:" << local_qp_info.qp_num << endl
    << " block_num:" << local_qp_info.block_num << endl
    << "block_size:" << local_qp_info.block_size << "(KB)" << endl;
    cout << "    local_gid:";
    for(int i = 0; i < 16; i++)
    {
        cout << std::hex << (int)local_qp_info.gid[i];
        if(i < 15) cout << ":";
    }
    cout << std::dec << endl;
    // << "recv_depth:" << local_qp_info.recv_depth << endl
    cout << "    remote:" << endl
    << "       lid:" << remote_qp_info.lid << endl
    << "    qp_num:" << remote_qp_info.qp_num << endl
    << " block_num:" << remote_qp_info.block_num << endl
    << "block_size:" << remote_qp_info.block_size << "(KB)" << endl;
    cout << "    remote_gid:";
    for(int i = 0; i < 16; i++)
    {
        cout << std::hex << (int)remote_qp_info.gid[i];
        if(i < 15) cout << ":";
    }
    cout << std::dec << endl;
    // << "recv_depth:" << remote_qp_info.recv_depth << endl;
#endif
    return changeQPState();
}

int StreamControl::prepareRecv()
{
    int i = 0;
    for(uint64_t i = 0; i < this->buffers.size(); i++)
    {
        auto ret = postRecvWr(i);
        if(ret < 0)
        {
            cout << "ERROR: prepareRecv failed for buffer " << i << endl;
            return -1;
        }
    }
    return 0;
}

struct recv_item
{
    uint32_t seq;
    bool recved;
    recv_item(uint32_t _seq, bool _recved):seq(_seq), recved(_recved){}
};
int StreamControl::postRecvFile()
{
    FileInfo file_info, remote_file_info;
    strcpy(file_info.file_path, "READY_TO_RECEIVE");
    file_info.file_size = 0;
    if (sockSyncData(sizeof(file_info), (char *)&file_info, (char *)&remote_file_info) != 0)
    {
        cout << "ERROR: synchronous failed before post file." << endl;
        return -2;
    }
    cout << "sync receiving file: " << remote_file_info.file_path << "(" << (double)remote_file_info.file_size/1e9 << "GB)" << endl;
    char sync_char = 'Y';
    struct ibv_wc *wc = new ibv_wc[buffers.size()];
    std::shared_ptr<int> x(NULL, [&](int *){
        delete[] wc;
    });
    if(sockSyncData(1, (char *)&sync_char, (char *)&sync_char) == -1)
        return -2;
    cout <<"start receiving file, sync_char: " << sync_char << endl;
    if(sync_char != 'Y')
    {
        cout << "WARNING: remote not ready to send." << endl;
        return 0;
    }
    cout << "use_message: " << this->use_message << endl;
    uint64_t recv_bytes = 0;
    auto t = high_resolution_clock::now();
    auto t_last_recv = t;
    double delta_io = 0,delta = 0;
    int recv_num = 0;
    uint32_t recv_id = 0;
    uint32_t expect_seq = 0;
    uint32_t restran_size = sizeof(uint32_t); 
    int64_t recv_max_seq = -1;
    uint32_t numOfBlocks = remote_file_info.file_size / this->block_size + ((remote_file_info.file_size % this->block_size == 0) ? 0 : 1);
    this->rateController->runRecv();
    std::deque<recv_item> unrecv_packet_seqs;
    unrecv_packet_seqs.clear();
    bool start_restran = false, first_restran = true;
    char * restran_space = nullptr;
    while(recv_num < numOfBlocks)//expect_seq < numOfBlocks)
    {
        int n = ibv_poll_cq(cq, 16, wc);
        if(n < 0)
        {
            cout << "ERROR: ibv_poll_cq returned " << n << " - closing connection";
            return -1;
        }
        else if(n == 0) //std::this_thread::sleep_for(std::chrono::microseconds(1));
        {
           if( duration_cast<duration<double>>(high_resolution_clock::now() - t_last_recv).count() > 1.0)
            {
		        // cout << "last_recv_id :" << recv_id << ", recv_num:" << recv_num << ", numOfBlocks:" << numOfBlocks << ", expect_seq:" << expect_seq << endl;
                //cout << "ERROR: unfinished recv." << endl;
                //return 0;
                if(this->use_message)
                    start_restran = true;
            }
        }
        else{
            //cout << n << endl;
            for (int i = 0; i < n; i++)
            {
                t_last_recv = high_resolution_clock::now();
                if (wc[i].status != IBV_WC_SUCCESS || wc[i].opcode != IBV_WC_RECV)
                {
                    fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n",
                            wc[i].status, wc[i].vendor_err);
                }
                auto &buffer = buffers[wc[i].wr_id];
                auto buff = std::get<0>(buffer);
                recv_bytes += wc[i].byte_len;
		//cout << "recv_bytes: " << recv_bytes << ", num: " << recv_num << endl;
		//cout  << *(int*)(buff) << endl;
		        recv_id = *(uint32_t*)buff;
                postRecvWr(wc[i].wr_id);
                if(use_message)
                {
                    if(recv_id < expect_seq)
                        continue;
                    if(recv_id == expect_seq)
                    {
                        if(expect_seq > recv_max_seq)
                        {
                            expect_seq ++;
                            recv_max_seq = recv_id;
                        }
                        else
                        {
                            // cout << "expect_seq:" << expect_seq << ", recv_max_seq:" << recv_max_seq << endl;
                            assert(expect_seq != recv_max_seq);
                            assert(!unrecv_packet_seqs.empty());
                            assert(unrecv_packet_seqs[0].seq == recv_id);
                            unrecv_packet_seqs[0].recved = true;
                            while(!unrecv_packet_seqs.empty() && unrecv_packet_seqs[0].recved == true)
                            {
                                unrecv_packet_seqs.pop_front();
                                expect_seq ++;
                            }
                        }
                    }
                    else
                    {
                        if(recv_id < recv_max_seq)
                        {
                            assert(!unrecv_packet_seqs.empty());
                            if(unrecv_packet_seqs[recv_id -expect_seq].recved)
                                continue;
                            unrecv_packet_seqs[recv_id - expect_seq].recved = true;
                        }
                        else if(recv_id > recv_max_seq)
                        {
                            //cout << "recv_id:" << recv_id << ", recv_max_seq:" << recv_max_seq << endl;
                            for(uint32_t i = recv_max_seq + 1; i < recv_id; i++)
                            {
                                unrecv_packet_seqs.emplace_back(i, false);
                            }
                            unrecv_packet_seqs.emplace_back(recv_id, true);
                            recv_max_seq = recv_id;
                        }
                        else continue;
                    }
                }
                recv_num ++;
                //cout << "receive rate: " << wc[i].byte_len * 8 /(delta * 1e9) <<"Gbps" <<endl;
                // auto io_start = high_resolution_clock::now();
                // write(recv_fd, (const char*)buff, wc[i].byte_len);
            }
        }
        //cout << "recv_max_seq:" << recv_max_seq << ", numOfBlocks:" << numOfBlocks << endl;
        //当最后几个包丢弃时，加判断
        if(use_message)
        {
            if(first_restran && (recv_max_seq == numOfBlocks - 1 || start_restran))
            {
                uint32_t unrecv_num = numOfBlocks - recv_num, loc = sizeof(uint32_t);
                restran_size = (unrecv_num + 1) * sizeof(uint32_t);
                restran_space = new char[restran_size];
                memcpy(restran_space, &unrecv_num, sizeof(uint32_t));
                if(recv_max_seq < numOfBlocks - 1)
                {
                    for(uint32_t i = recv_max_seq + 1; i < numOfBlocks; i++)
                    {
                        unrecv_packet_seqs.emplace_back(i, false);
                    }
                }
                for(int i = 0; i < unrecv_packet_seqs.size(); i ++)
                {
                    if(unrecv_packet_seqs[i].recved == false)
                    {
                        memcpy(restran_space + loc, &unrecv_packet_seqs[i].seq, sizeof(uint32_t));
                        loc += sizeof(uint32_t);
                    }
                    if(loc == restran_size)
                        break;
                }
                first_restran = false;
                t_last_recv = high_resolution_clock::now();
                start_restran = true;
            }
            else if(start_restran || expect_seq == numOfBlocks)
            {
                uint32_t unrecv_num = numOfBlocks - recv_num, loc = sizeof(uint32_t);
                restran_size = (unrecv_num + 1) * sizeof(uint32_t);
                memcpy(restran_space, &unrecv_num, sizeof(uint32_t));
                for(int i = 0; i < unrecv_packet_seqs.size(); i ++)
                {
                    if(unrecv_packet_seqs[i].recved == false)
                    {
                        memcpy(restran_space + loc, &unrecv_packet_seqs[i].seq, sizeof(uint32_t));
                        loc += sizeof(uint32_t);
                    }
                    if(loc == restran_size)
                        break;
                }
                t_last_recv = high_resolution_clock::now();
                start_restran = true;
            }
            if(start_restran)
            {
                uint32_t loc = 0;
                int rc = 0;
                while(loc < restran_size)
                {
                    rc = send(this->peer_fd,restran_space + loc, restran_size - loc, MSG_NOSIGNAL);
                    if (rc == 0 || (rc < 0 && errno != EINTR))
                    {
                        cout << "ERROR: Failed writing data during start_restran." << endl;
                        cout << "errno: " << errno << endl;
                        cout << "strerror: " << strerror(errno) << endl;
                        cout << "rc: " << rc << endl;
                        return -2;
                    }
                    loc += rc;
                }
                cout << "restran " << *(uint32_t*)restran_space << " packets" << endl;
                start_restran = false;
            }
        }
    }
    if(restran_space != nullptr)
        delete[] restran_space;
    this->rateController->pauseRecv();
    delta = duration_cast<duration<double>>(t_last_recv - t).count();
    cout << "total recv_num: " << recv_num << endl;
    // cout << "I/O write rate: " << remote_file_info.file_size * 8/(delta_io * 1e9) << "Gbps" << endl;
    cout << "recv rate: " << (double)remote_file_info.file_size * 8/(delta * 1e9) << "Gbps" << endl;
    cout << "finish receive file:" << remote_file_info.file_path << "(" << (double)remote_file_info.file_size/1e9 << "GB)" << endl;
    return 0;
}
int StreamControl::postSendFile(uint64_t file_size, int rate_sock)
{
    assert(file_size > 0);
    char file_name[256] = "[VIRTUAL_NO_IO_FILE]";
    FileInfo file_info, remote_file_info;
    strcpy(file_info.file_path, file_name);
    file_info.file_size = file_size;
    if (sockSyncData(sizeof(file_info), (char *)&file_info, (char *)&remote_file_info) != 0)
    {
        cout << "ERROR: synchronous failed before post file." << endl;
        return -2;
    }
    if (strcmp(remote_file_info.file_path, "READY_TO_RECEIVE") != 0)
    {
        cout << "ERROR: remote not ready to receive." << endl;
        return -1;
    }
    char sync_char = 'Y';
    struct ibv_wc *wc = new ibv_wc[buffers.size()];
    std::shared_ptr<int> x(NULL, [&](int *){
        delete[] wc;
        });
    double filesize_GB = (double)(file_info.file_size) * 1.0E-9;
    cout << "Sending file: " << file_name << "(" << filesize_GB << " GB)" << endl;
    struct ibv_send_wr wr, *bad_wr = nullptr;
    struct ibv_sge sge;
    bzero(&wr, sizeof(wr));
    bzero(&sge, sizeof(sge));
    wr.opcode = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED,
    wr.next = NULL,
    sge.lkey = this->mr->lkey;

    uint64_t bytes_left = file_size;
    uint64_t ack_bytes = 0;
    uint32_t Noutstanding_writes = 0;
    double delta_t_io = 0.0;
    
    uint64_t sendcnt = 0, compcnt = 0;
    std::list<uint64_t> uncomplete_bytes;
    uint64_t i = 0;

    auto buff_size = std::get<1>(buffers[0]);
    sge.addr = (uint64_t)std::get<0>(buffers[0]);
    *(uint32_t*)(sge.addr) = 0; 
    wr.wr_id = 0;
    auto bytes_payload = buff_size; // < bytes_left ? buff_size : bytes_left;
    sge.length = bytes_payload;

    // bool unread = false;
    //for(; j < buffers.size() && buff_size * j < file_info.file_size;)
        //readahead(fd,(j++) * buff_size, buff_size);
    if(sockSyncData(1, (char *)&sync_char, (char *)&sync_char))
        return -2;
    cout <<"start sending file, sync_char: " << sync_char << endl;
    if(sync_char != 'Y')
    {
        cout << "WARNING: remote not ready to receive." << endl;
        return 0;
    }
    int remaining_recv_wqe = remote_qp_info.block_num;
    bool recv_finished = false, start_restran = false, first_restran = true;
    uint32_t restran_num = 0, restran_size = 0, restraned_num = 0;
    uint32_t numOfBlocks = file_size / this->block_size + ((file_size % this->block_size == 0) ? 0 : 1);
    char* restran_space = nullptr;
    auto t1 = high_resolution_clock::now();
    auto t2 = t1, t_io = t1;
    int count = 0;
    double duration_time = 0, duration_io = 0;
    this->rateController->runSend();
    cout << "use_message: " << this->use_message << endl;
    std::thread statistic_thread(&StreamControl::statistic, this, &ack_bytes, bytes_left, rate_sock);
    while (1)
    { 
        // cout << "try to recv restran" << endl;
        if(this->use_message && bytes_left == 0)
        {
            int rc = recv(this->peer_fd, &restran_num, sizeof(uint32_t), MSG_DONTWAIT);
            if(rc < 0 && (errno == EWOULDBLOCK || errno == EINTR));
            else if(rc <= 0) 
            {
                cout << "ERROR: Failed writing data during start_restran." << endl;
                cout << "errno: " << errno << endl;
                cout << "strerror: " << strerror(errno) << endl;
                cout << "rc: " << rc << endl;
                return -2;
            }
            else
            {
                assert(rc == sizeof(uint32_t));
                cout << "start restran " << restran_num << " packets" << endl;
                if(restran_num == 0)
                    recv_finished = true;
                else
                {
                    assert(start_restran == false);
                    if(first_restran == true)
                    {
                        restran_space = new char[restran_num * sizeof(uint32_t)];
                        first_restran = false;
                    }
                    uint32_t loc = 0;
                    restran_size = restran_num * sizeof(uint32_t);
                    while(loc < restran_size)
                    {
                        rc = recv(this->peer_fd, restran_space + loc, restran_size - loc, 0);
                        if(rc == 0 || (rc < 0 && errno != EINTR))
                        {
                            cout << "ERROR: Failed writing data during start_restran." << endl;
                            cout << "errno: " << errno << endl;
                            cout << "strerror: " << strerror(errno) << endl;
                            cout << "rc: " << rc << endl;
                            return -2;
                        }
                        loc += rc;
                    }
                    start_restran = true;
                    *(uint32_t*)(sge.addr) = *(uint32_t*)restran_space;
                    assert(bytes_left == 0);
                    bytes_left += restran_num * this->block_size;
                    restraned_num = 0;
                }
            }
            if(recv_finished) break;
        }
        if(this->use_message && this->rateController->getRate() == 0.0) continue;
        if(this->use_message && !this->rateController->timeToSend());
        else
        {
            // cout << "try to send" << endl;
            if(this->use_message && !start_restran && bytes_left == 0) continue; 
            // Calculate bytes to be sent in this buffer
           //_io = high_resolution_clock::now();
            // read(fd, (char *)sge.addr, bytes_payload);
            //duration_io += duration_cast<duration<double>>(high_resolution_clock::now() - t_io).count();
            if(this->use_message)
                this->rateController->updateNextSend();
            auto ret = ibv_post_send(qp, &wr, &bad_wr);
            uncomplete_bytes.push_back(bytes_payload); 
            if (ret != 0)
            {
                cout << "WARNING: ibv_post_send returned non zero value (" << ret << ")" << endl;
                break;
            }
            bytes_left -= bytes_payload;
            Noutstanding_writes++;
            remaining_recv_wqe --;
            sendcnt++;
            i++;
            auto id = i % buffers.size();
            auto &buffer = buffers[id];
            auto buff = std::get<0>(buffer);
            auto buff_size = std::get<1>(buffer);
            sge.addr = (uint64_t)buff;
            wr.wr_id = id;
            bytes_payload = buff_size; // < bytes_left ? buff_size : bytes_left;
            sge.length = bytes_payload;
            if(this->use_message && start_restran)
            {
                restraned_num += 1;
                if(restraned_num < restran_num)
                    *(uint32_t*)(sge.addr) = *(uint32_t*)(restran_space + restraned_num * sizeof(uint32_t));
                else
                    start_restran = false;
            }
            else
                *(uint32_t*)(sge.addr) = sendcnt;
            // if((i % this->remote_qp_info.recv_depth) == 0) unread = true; //wait for sync
        }
        
        do
        {
            int n = ibv_poll_cq(cq,16, wc);
            // cout << Noutstanding_writes << endl;
            // cout << n << endl;
            if (n < 0)
            {
                cout << "ERROR: ibv_poll_cq returned " << n << " - closing connection";
                return -1;
            }
            else if (n > 0)
            {
                for (int i = 0; i < n; i++)
                {
                    if (wc[i].status != IBV_WC_SUCCESS)
                    {
                        fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n",
                                wc[i].status, wc[i].vendor_err);
                        return -1;
                    }
                    //compcnt++;
                    Noutstanding_writes--;
                    ack_bytes += uncomplete_bytes.front();
                    t1 = t2;
                    t2 = high_resolution_clock::now();
                    auto period = duration_cast<duration<double>>(t2 - t1).count();
                    duration_time += period;
                    // int ret = upload_thread->caculateTransferInfo(ack_bytes, period, uncomplete_bytes.front());
                    uncomplete_bytes.pop_front();
                }
            }
        } while (Noutstanding_writes >= buffers.size() || (bytes_left == 0 && Noutstanding_writes > 0));
        if(!this->use_message && bytes_left == 0) break;
    }
    if(restran_space != nullptr)
        delete[] restran_space;
    statistic_thread.join();
    this->rateController->pauseSend();
    t2 = high_resolution_clock::now();
    cout << endl;

    // duration<double> delta_t = duration_cast<duration<double>>(t2 - t1);
    double rate_Gbps = (double)file_info.file_size/ duration_time * 8.0 / 1.0E9;
    //double rate_io_Gbps = (double)file_info.file_size / duration_io * 8.0 / 1.0E9;
    // cout << duration_cast<duration<double>>(t2 - t1).count() <<  "sec" << endl;
#ifndef DEBUG
    if (file_info.file_size > 2E8)
    {
        cout << "  Transferred " << (((double)file_info.file_size) * 1.0E-9) << " GB in " << duration_time << " sec  (" << rate_Gbps << " Gbps)" << endl;
        //cout << "  I/O rate reading from file: " << duration_io << " sec  (" << rate_io_Gbps << " Gbps)" << endl;
    }
    else
    {
        cout << "  Transferred " << (((double)file_info.file_size) * 1.0E-6) << " MB in " << duration_time << " sec  (" << rate_Gbps * 1000.0 << " Mbps)" << endl;
        //cout << "  I/O rate reading from file: " << duration_io << " sec  (" << rate_io_Gbps * 1000.0 << " Mbps)" << endl;
    }
#endif

    // while(remaining_recv_wqe < remote_qp_info.block_num)
    // {
    //     int nb = recv(this->peer_fd, &sync_char, 1, MSG_DONTWAIT);
    //     if(nb == 0) return -2;
    //     else if(nb < 0 && (errno == EWOULDBLOCK || errno == EINTR))
    //         continue;
    //     else if(nb < 0)
    //         return -2;
    //     else 
    //         remaining_recv_wqe++;
    // }
    return 0;
}

int StreamControl::postRecvWr(uint64_t id)
{
    auto &buffer = buffers[id];
    auto buff = std::get<0>(buffer);
    auto buff_size = std::get<1>(buffer);

    struct ibv_recv_wr wr, *bad_wr;
    struct ibv_sge sge;
    bzero(&wr, sizeof(wr));
    bzero(&sge, sizeof(sge));
    wr.wr_id = id;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    sge.addr = (uint64_t)buff;
    sge.length = buff_size;
    sge.lkey = mr->lkey;
    auto ret = ibv_post_recv(qp, &wr, &bad_wr);
    if (ret != 0)
    {
        cout << "ERROR: ibv_post_recv returned non zero value (" << ret << ")" << endl;
        return -1;
    }
    return 0;
}

int recvData(HwRdma *hwrdma, int peer_fd,  LocalConf* local_conf, ClientList* client_list)
{
    std::shared_ptr<int> x(NULL, [&](int *)
                           {    
                                close(peer_fd); 
                                client_list->removeClient(peer_fd);
                            });
    char use_message_str = 'Y';
    int rc = recv(peer_fd,&use_message_str, 1, 0);
    if(rc == 0 || (rc < 0 && errno != EINTR)) return -1;
    cout << "recv str " << use_message_str << endl;
    StreamControl stream_control(hwrdma, peer_fd, local_conf,  client_list->getClientInfo(peer_fd).ip, true, use_message_str == 'Y'? true : false);
    if (stream_control.createLucpContext())
        return -1;
    if (stream_control.connectPeer())
        return -1;
    if (stream_control.bindMemoryRegion())
        return -1;
    if (stream_control.createBufferPool())
        return -1;
    if( stream_control.prepareRecv())
        return -1;
    while (!stream_control.postRecvFile());
        return -1;
    return 0;
}

void StreamControl::statistic(uint64_t* bytes, uint64_t total, int rate_sock)
{
    std::ofstream rate_out("rate.log");
    int seq_num = 0;
    RateInfo rate_info = {
        .seq_num = seq_num,
        .rate = 0.0
    };
    int ret = send(rate_sock, (char*)&rate_info, sizeof(rate_info), MSG_NOSIGNAL);
    if(ret == 0 || (ret < 0 && errno != EINTR))
    {
        cout << "ERROR: Failed to send init rate info." << endl;
        close(rate_sock);
        return;
    }
    uint64_t last_bytes = 0;
    double interval = 0.01; // 20ms
    auto timeout = high_resolution_clock::now() + duration_cast<nanoseconds>(duration<double>(interval)); // 50ms timeout
    while(*bytes < total)
    {
        auto t = high_resolution_clock::now();
        if(t >= timeout)
        {
            rate_info.rate = ((*bytes - last_bytes) * 8.0 / (/*duration_cast<duration<double>>(t - timeout).count() +*/ interval)) * 1e-6; // Gbps
            rate_out << seq_num << ":" << rate_info.rate << " Mbps " << this->rateController->getRate() << " " <<  *bytes  << " " << total <<  endl;
            cout << seq_num << ":" << rate_info.rate << " Mbps" << endl;
            rate_info.seq_num = ++seq_num;
            int ret = send(rate_sock, (char*)&rate_info, sizeof(rate_info), MSG_NOSIGNAL);
            if(ret == 0 || (ret < 0 && errno != EINTR))
            {
                cout << "ERROR: Failed to send rate info." << endl;
                close(rate_sock);
                return;
            }
            timeout += duration_cast<nanoseconds>(duration<double>(interval)); // 10ms timeout
            last_bytes = *bytes;
	    //cout << "send_byte: " << *bytes << " bytes" << endl;
        }
        else
            usleep(duration_cast<duration<double>>(timeout - t).count() * 1e6);
    }
    rate_info.seq_num = ++seq_num;
    rate_info.rate = -1;
    ret = send(rate_sock, (char*)&rate_info, sizeof(rate_info), MSG_NOSIGNAL);
    if(ret == 0 || (ret < 0 && errno != EINTR))
    {
        cout << "ERROR: Failed to send rate info." << endl;
        close(rate_sock);
        return;
    }
    close(rate_sock);
}
