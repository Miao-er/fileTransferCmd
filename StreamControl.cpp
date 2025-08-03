#include <iostream>
#include <chrono>
#include <string>
#include <thread>
#include <sys/stat.h>
#include "StreamControl.h"
#define NO_IO
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
    cq = ibv_create_cq(hwrdma->ctx, local_conf->getBlockNum(), NULL, comp_channel, 0);
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
    qp_init_attr.cap.max_recv_wr = local_conf->getBlockNum();
    qp_init_attr.cap.max_send_sge = 1;
    qp_init_attr.cap.max_recv_sge = 1;
    qp_init_attr.qp_type = IBV_QPT_RC;

    local_qp_info.lid = hwrdma->port_attr.lid;
    local_qp_info.block_num = local_conf->getBlockNum();
    local_qp_info.block_size = local_conf->getBlockSize();
    //local_qp_info.lucp_id = duration_cast<nanoseconds>(high_resolution_clock::now().time_since_epoch()).count(); //TODO
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
        qp_attr.rq_psn = 0,
        qp_attr.max_dest_rd_atomic = 1,
        qp_attr.min_rnr_timer = 0x12,
        // qp_attr.ah_attr.is_global  = 0,
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
                                        IBV_QP_RQ_PSN | IBV_QP_MAX_DEST_RD_ATOMIC |
                                        IBV_QP_MIN_RNR_TIMER);
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
        qp_attr.qp_state = IBV_QPS_RTS,
        qp_attr.timeout = 18,
        qp_attr.retry_cnt = 7,
        qp_attr.rnr_retry = 0,
        qp_attr.sq_psn = 0,
        qp_attr.max_rd_atomic = 1;

        auto ret = ibv_modify_qp(qp, &qp_attr,
                                    IBV_QP_STATE | IBV_QP_TIMEOUT |
                                        IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                                        IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
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
    //net_local_qp_info.lucp_id = htons(local_qp_info.lucp_id);
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
    //remote_qp_info.lucp_id = ntohs(net_remote_qp_info.lucp_id);
    remote_qp_info.qp_num = ntohl(net_remote_qp_info.qp_num);
    remote_qp_info.block_num = ntohl(net_remote_qp_info.block_num);
    remote_qp_info.block_size = ntohl(net_remote_qp_info.block_size);
    //remote_qp_info.recv_depth = ntohl(net_remote_qp_info.recv_depth);
    memcpy(remote_qp_info.gid, net_remote_qp_info.gid, 16);
    //client apply block size from server
    if(!this->server_mode)
        this->block_size = 1024UL * remote_qp_info.block_size;
    else
        this->block_size = 1024UL * local_conf->getBlockSize();
    this->rateController = new RateController(this->default_rate, this->block_size);
    if(this->rateController->initSwap(hwrdma->bind_ip, this->peer_addr) < 0)
    {
        cout << "RateController init failed." << endl;
        return -1;
    }
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
    uint64_t recv_bytes = 0;
    auto t = high_resolution_clock::now();
    auto t_last_recv = t;
    double delta_io = 0,delta = 0;
    int recv_num = 0;
    this->rateController->runRecv();
    while(recv_bytes < remote_file_info.file_size)
    {
        int n = ibv_poll_cq(cq, 1, wc);
        if(n < 0)
        {
            cout << "ERROR: ibv_poll_cq returned " << n << " - closing connection";
            return -1;
        }
        else if(n == 0) //std::this_thread::sleep_for(std::chrono::microseconds(1));
        {
            if( duration_cast<duration<double>>(high_resolution_clock::now() - t_last_recv).count() > 1.0)
            {
                cout << "ERROR: unfinished recv." << endl;
                return 0;
            }
        }
        else{
            //cout << n << endl;
            for (int i = 0; i < n; i++)
            {
                recv_num ++;
                t_last_recv = high_resolution_clock::now();
                if (wc[i].status != IBV_WC_SUCCESS || wc[i].opcode != IBV_WC_RECV)
                {
                    fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n",
                            wc[i].status, wc[i].vendor_err);
                }
                auto &buffer = buffers[wc[i].wr_id];
                auto buff = std::get<0>(buffer);
                recv_bytes += wc[i].byte_len;
                //cout << "receive rate: " << wc[i].byte_len * 8 /(delta * 1e9) <<"Gbps" <<endl;
                // auto io_start = high_resolution_clock::now();
                // write(recv_fd, (const char*)buff, wc[i].byte_len);
                postRecvWr(wc[i].wr_id);
                // auto io_end = high_resolution_clock::now();
                // double delta_io_ = duration_cast<duration<double>>(io_end - io_start).count();
                //cout << "write rate: " << wc[i].byte_len * 8 /(delta_io_ * 1e9) <<"Gbps" <<endl;
                // delta_io += delta_io_;
                // delta += delta_;
                // if(recv_num > 0 && (recv_num % local_qp_info.recv_depth == 0)) //all wqe is in free state
                int rc;
                sync_char = 'A';
                // while((rc = send(this->peer_fd, (char*)&sync_char, 1, MSG_NOSIGNAL)) <= 0)
                // {
                //     if (rc == 0 || (rc < 0 && errno != EINTR))
                //         return -2;
                // }

            }
        }
    }
    this->rateController->pauseRecv();
    delta = duration_cast<duration<double>>(high_resolution_clock::now() - t).count();
    cout << "total recv_num: " << recv_num << endl;
    // cout << "I/O write rate: " << remote_file_info.file_size * 8/(delta_io * 1e9) << "Gbps" << endl;
    cout << "recv rate: " << remote_file_info.file_size * 8/(delta * 1e9) << "Gbps" << endl;
    cout << "finish receive file:" << remote_file_info.file_path << "(" << (double)remote_file_info.file_size/1e9 << "GB)" << endl;
    return 0;
}
int StreamControl::postSendFile(uint64_t file_size)
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
    wr.wr_id = 0;
    auto bytes_payload = buff_size < bytes_left ? buff_size : bytes_left;
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
    auto t1 = high_resolution_clock::now();
    auto t2 = t1, t_io = t1;
    int count = 0;
    double duration_time = 0, duration_io = 0;
    this->rateController->runSend();
    cout << "use_message: " << this->use_message << endl;
    std::thread statistic_thread(statistic, &ack_bytes, bytes_left);
    while (1)
    { 
        // if (unread)
	//cout << "before--" << this->rateController->getRate() << endl;
        if(this->use_message && this->rateController->getRate() == 0.0) continue;
	// else cout << "rate: " << this->rateController->getRate() << endl;
        {
            // while(1)
            // {
            //     int nb = recv(this->peer_fd, &sync_char, 1, MSG_DONTWAIT);
            //     if(nb == 0) return -2;
            //     else if(nb < 0 && errno == EWOULDBLOCK)
            //         break;
            //     else if(nb < 0 && errno == EINTR)
            //         continue;
            //     else if(nb < 0)
            //         return -2;
            //     else 
            //     {
            //         // cout << "sync_char: " << sync_char << endl;
            //         assert(sync_char == 'A');
            //         remaining_recv_wqe++;
            //         break;
            //     }
            // }
        }
        if(this->use_message && !this->rateController->timeToSend());
        // else if(remaining_recv_wqe > 0)
        else
        {
            // Calculate bytes to be sent in this buffer
            t_io = high_resolution_clock::now();
            // read(fd, (char *)sge.addr, bytes_payload);
            duration_io += duration_cast<duration<double>>(high_resolution_clock::now() - t_io).count();

            auto ret = ibv_post_send(qp, &wr, &bad_wr);
            uncomplete_bytes.push_back(bytes_payload); 
            this->rateController->updateNextSend();
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
            bytes_payload = buff_size < bytes_left ? buff_size : bytes_left;
            sge.length = bytes_payload;
            // if((i % this->remote_qp_info.recv_depth) == 0) unread = true; //wait for sync
        }
        
        do
        {
            int n = ibv_poll_cq(cq,1, wc);
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
                    compcnt++;
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
        if(bytes_left == 0) break;
    }
    statistic_thread.join();
    this->rateController->pauseSend();
    t2 = high_resolution_clock::now();
    cout << endl;

    // duration<double> delta_t = duration_cast<duration<double>>(t2 - t1);
    double rate_Gbps = (double)file_info.file_size/ duration_time * 8.0 / 1.0E9;
    double rate_io_Gbps = (double)file_info.file_size / duration_io * 8.0 / 1.0E9;
    // cout << duration_cast<duration<double>>(t2 - t1).count() <<  "sec" << endl;
#ifndef DEBUG
    if (file_info.file_size > 2E8)
    {
        cout << "  Transferred " << (((double)file_info.file_size) * 1.0E-9) << " GB in " << duration_time << " sec  (" << rate_Gbps << " Gbps)" << endl;
        cout << "  I/O rate reading from file: " << duration_io << " sec  (" << rate_io_Gbps << " Gbps)" << endl;
    }
    else
    {
        cout << "  Transferred " << (((double)file_info.file_size) * 1.0E-6) << " MB in " << duration_time << " sec  (" << rate_Gbps * 1000.0 << " Mbps)" << endl;
        cout << "  I/O rate reading from file: " << duration_io << " sec  (" << rate_io_Gbps * 1000.0 << " Mbps)" << endl;
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
    StreamControl stream_control(hwrdma, peer_fd, local_conf,  client_list->getClientInfo(peer_fd).ip, true);
    std::shared_ptr<int> x(NULL, [&](int *)
                           {    
                                close(peer_fd); 
                                client_list->removeClient(peer_fd);
                            });
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

void statistic(uint64_t* bytes, uint64_t total)
{
    std::ofstream rate_out("rate.log");
    uint64_t last_bytes = 0;
    double interval = 0.01; // 20ms
    auto timeout = high_resolution_clock::now() + duration_cast<nanoseconds>(duration<double>(interval)); // 50ms timeout
    while(*bytes < total)
    {
        auto t = high_resolution_clock::now();
        if(t >= timeout)
        {
            rate_out << duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count() << ":" << ((*bytes - last_bytes) * 8.0 / (duration_cast<duration<double>>(t - timeout).count() + interval)) * 1e-9 << " Gbps" << endl;
            timeout = t + duration_cast<nanoseconds>(duration<double>(interval)); // 10ms timeout
            last_bytes = *bytes;
        }
        else
            usleep(duration_cast<duration<double>>(timeout - t).count() * 1e6);
    }
}
