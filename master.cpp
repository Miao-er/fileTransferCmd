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
#include <deque>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <condition_variable>
#include <atomic>
#include <librdkafka/rdkafkacpp.h>
#include <nlohmann/json.hpp>  // 添加json库头文件
using json = nlohmann::json;
#define MAX_RATE 100000.0
#define DEBUG
// #define READ_FROM_FILE

#define LOG_ERR_FILE

// #ifndef READ_FROM_FILE
#define LOG_RATE_FILE
// #endif

#ifdef LOG_ERR_FILE
    std::ofstream err_log("error.log");
    #define ERR_LOG(x) err_log << x << std::endl
#else
    #define ERR_LOG(x) std::cerr << x << std::endl
#endif

#ifdef LOG_RATE_FILE
    std::ofstream rate_log("rate.log");
    #define RATE_LOG(x) rate_log << x << std::endl
    #define RESET_LOG() {rate_log.close(); rate_log.open("rate.log", std::ios::trunc);}
#else
    #define RATE_LOG(x)
    #define RESET_LOG()
#endif

void caculator_factor(double& fairness, double& stability) 
{
    std::ifstream read_rate("rate.log");
    std::string line;
    std::vector<std::string> list_name = {"task_1_rate", "task_2_rate", "task_3_rate"};
    std::vector<double> list_rate;

    int fairness_count = 0;
    double fairness_sum = 0;

    std::deque<double> rate_1, rate_2, rate_3;
    rate_1.clear();
    rate_2.clear();
    rate_3.clear();
    std::deque<double>* list_rate_ptr[3];
    list_rate_ptr[0] = &rate_1;
    list_rate_ptr[1] = &rate_2;
    list_rate_ptr[2] = &rate_3;
    std::vector<double> list_osci;
    double stab_list_sum[3] = {0};
    int stab_list_count[3] = {0};
    double stab_sum_rate[3] = {0};
    const int w_size = 100;
    while (std::getline(read_rate, line)) {
        // 处理每一行
        if(line.empty()) continue;
        double fairness_short_sum = 0, fairness_short_pow_sum = 0;
        int fairness_short_count = 0;
        int idx = 0;
        auto rate_j = json::parse(line);
        for(auto & name : list_name) {
            list_osci.clear();
            if (!rate_j[name].is_null()) {
                double rate_value = rate_j[name];
                if(rate_value > 0)
                {
                    //for fairness
                    fairness_short_count += 1;
                    fairness_short_sum += rate_value;
                    fairness_short_pow_sum += rate_value * rate_value;
                    //for stability
                    list_rate_ptr[idx]->push_back(rate_value);
                    stab_sum_rate[idx] += rate_value;
                    if(list_rate_ptr[idx]->size() > w_size)
                    {
                        stab_sum_rate[idx] -= list_rate_ptr[idx]->at(0);
                        list_rate_ptr[idx]->pop_front();
                    }
                    if(list_rate_ptr[idx]->size() < w_size)
                    {
                        idx++;
                        continue;
                    }
                    for(int i = 1; i < list_rate_ptr[idx]->size(); i++)
                        list_osci.push_back(std::abs(list_rate_ptr[idx]->at(i) - list_rate_ptr[idx]->at(i - 1)));
                    std::sort(list_osci.begin(), list_osci.end());
                    double osci_median = list_osci[(w_size - 1)/2];
                    double aver_rate = stab_sum_rate[idx] / w_size;
                    double stab_short = osci_median / std::max(1e-6, aver_rate);
                    stab_list_sum[idx] += stab_short;
                    stab_list_count[idx] += 1;
                }
            }
            idx++;
        }
        if(fairness_short_count >= 2)
        {
            fairness_count += 1;
            fairness_sum += fairness_short_sum * fairness_short_sum / (fairness_short_pow_sum * fairness_short_count);
        }
    }
    if(fairness_count > 0)
    {
        fairness = fairness_sum / fairness_count;
	fairness = std::max(0.0, (fairness - 1/3.0) / (2/3.0));
    }
    for(int i = 0; i < 3; i ++)
    {
        if(stab_list_count[i] > 0)
            stab_list_sum[i] /= stab_list_count[i];
        stab_list_sum[i] = std::exp(-1 * stab_list_sum[i] / 0.1);
        stability += stab_list_sum[i];
    }
    stability /= 3;
    read_rate.close();
}

class KafkaProducer {
public:
    KafkaProducer(const std::string& brokers) {
#ifndef DEBUG
        RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        std::string errstr;
        conf->set("bootstrap.servers", brokers, errstr);

        _producer = RdKafka::Producer::create(conf, errstr);
        if (!_producer) {
            ERR_LOG("Failed to create producer: " << errstr);
            exit(1);
        }
        std::cout << "Created producer " << _producer->name() << std::endl;
        delete conf;
#endif
    }
    void set_topic(const std::string& topic_name) {
        topic = topic_name;
    }

    void send(const json& message) {
        std::string payload = message.dump();
#ifndef DEBUG
        RdKafka::ErrorCode resp = _producer->produce(
            topic,
            RdKafka::Topic::PARTITION_UA,
            RdKafka::Producer::RK_MSG_COPY,
            const_cast<char*>(payload.c_str()),
            payload.size(),
            nullptr, 0,
            0, nullptr
        );
        if (resp != RdKafka::ERR_NO_ERROR) {
            ERR_LOG("Produce failed: " << RdKafka::err2str(resp));
        }
        _producer->poll(0); // 处理回调
#endif
        RATE_LOG(payload);
    }
    ~KafkaProducer() {
#ifndef DEBUG
        _producer->flush(1000);
        delete _producer;
#endif
    }

private:
    RdKafka::Producer* _producer;
    std::string topic;
};

class KafkaConsumer;
struct RateInfo
{
    uint32_t seq_num;
    double rate;
    RateInfo() : seq_num(0), rate(-2.0){}
};
struct RateQueueElem {
    std::vector<RateInfo> rates;
    std::atomic<bool> ready;
    uint32_t seq_num;
    RateQueueElem(size_t n, uint32_t seq) : rates(n), ready(false), seq_num(seq) {}
};

struct RateCollector
{
    std::deque<RateQueueElem> rate_queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;
    size_t client_num;
    std::atomic<uint32_t> wait_seq_num;
    std::atomic<uint32_t> finished_count;
    std::vector<bool> finished_arr;
    std::atomic<bool> finished;
};

#define IP_ADDR_LEN 16
struct ClientInfo {
    char socket_ip[IP_ADDR_LEN];
    char local_ip[IP_ADDR_LEN];
    char server_ip[IP_ADDR_LEN];
    int port;
    uint64_t file_size; // in bytes
    double delay; // in seconds
};
std::vector<ClientInfo> clients = {};

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
        *size = std::stoull(str) * 1024 * 1024 * 1024; // GB to bytes
    } else if (str.back() == 'M' || str.back() == 'm') {
        str.pop_back();
        *size = std::stoull(str) * 1024 * 1024; // MB to bytes
    } else if (str.back() == 'K' || str.back() == 'k') {
        str.pop_back();
        *size = std::stoull(str) * 1024; // KB to bytes
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


// 检查线程
void check_and_send_thread(RateCollector* collector, KafkaProducer& producer) {
    while (collector->finished.load() == false || !collector->rate_queue.empty()) {
        std::unique_lock<std::mutex> lock(collector->queue_mutex);
        collector->queue_cv.wait(lock, [&collector]{
            return collector->finished.load() || (!collector->rate_queue.empty() && collector->rate_queue.front().ready.load());
        });
        if(collector->finished.load() && collector->rate_queue.empty()) break;
        // 取出队头
        auto& elem = collector->rate_queue.front();
        // 序列化为JSON
        json j_obj;
        float total_rate = 0.0;
        for (size_t i = 0; i < elem.rates.size(); ++i) {
            std::string key = "task_" + std::to_string(i + 1) + "_rate";
            if (elem.rates[i].rate < -1.5) {
                j_obj[key] = nullptr;
            } else if(elem.rates[i].rate < -0.5) {
                j_obj[key] = 0.0;
            } else
            {
                j_obj[key] = elem.rates[i].rate / 1000.0;
                total_rate += elem.rates[i].rate / 1000.0;
            }
        }
        j_obj["total_rate"] = total_rate > MAX_RATE/1000.0 ? MAX_RATE/1000.0 : total_rate; // 限制总速率
        j_obj["timepoint"] = (double)elem.seq_num / 100.0;
        if(collector->finished_count.load() == collector->client_num && collector->rate_queue.size() == 1)
        {
            double fairness = 0, stability = 0;
            caculator_factor(fairness,stability);
            j_obj["fairness"] = fairness;
            j_obj["stability"] = stability;
        }
        else
        {
            j_obj["fairness"] = nullptr;
            j_obj["stability"] = nullptr;
        }

        collector->rate_queue.pop_front();
        lock.unlock();
        producer.send(j_obj);
    }
}

int collect_rate_info(RateCollector* collector, int rate_sock, int idx) {
    RateInfo info;
    while (true) {
        int ret = recv(rate_sock, (char*)&info, sizeof(RateInfo), 0);
        if (ret <= 0) break;
        std::unique_lock<std::mutex> lock(collector->queue_mutex);
        int true_seq_num = info.seq_num + idx * 100; // 假设每个客户端的seq_num是连续的
        // 检查是否需要创建新的RateQueueElem
        while (collector->rate_queue.empty() || collector->wait_seq_num <= true_seq_num) {
            collector->rate_queue.emplace_back(collector->client_num, collector->wait_seq_num);
            collector->wait_seq_num ++;
        }
        // 填充对应位置
        uint32_t front_seq_num = collector->rate_queue.front().seq_num;
        collector->rate_queue[true_seq_num - front_seq_num].rates[idx] = info;

        // 检查是否全部填充
        bool all_ready = true;
        for (size_t i = 0; i < collector->client_num; ++i) {
            if(i == idx) continue;
            if(collector->rate_queue[true_seq_num - front_seq_num].seq_num < i * 100)
                break;
            if(collector->finished_arr[i]) continue;
            if (collector->rate_queue[true_seq_num - front_seq_num].rates[i].seq_num == 0 && collector->rate_queue[true_seq_num - front_seq_num].rates[i].rate < 0) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            collector->rate_queue[true_seq_num - front_seq_num].ready = true;
            collector->queue_cv.notify_one();
        }
        if(true_seq_num > idx * 100 + 5 && info.rate < 0)
        {
            collector->finished_count++;
            collector->finished_arr[idx] = true;
            std::cout << "Client " << idx << " finished" << std::endl;
        }
    }
    if (collector->finished_count.load() == collector->client_num){
        collector->finished.store(true);
        collector->queue_cv.notify_all();
    }
    return 0;
}

void send_start_cmd(RateCollector* collector, const ClientInfo& client, const std::string& cmd, int idx) {
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
    collect_rate_info(collector, sockfd, idx);
    close(sockfd);
}

int processCMD(std::string cmd)
{
    std::cout << "[#CMD]" << cmd << std:: endl;
    cmd += " 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        std::cout << "[#Err]Failed to run command: " << cmd << std::endl;
        return -1;
    }
    char buffer[256];
    std::string output;
    while (fgets(buffer, sizeof(buffer), fp)) {
        output += buffer;
    }
    std::cout << output << std::endl;
    int status = pclose(fp);
    if (status != 0) {
        std::cerr << "[#Err]" << output;
        return status;
    }
    return 0;
}

class KafkaConsumer {
public:
    KafkaConsumer(const std::string& brokers, const std::string& group_id) {
        // 配置消费者属性
#ifndef DEBUG
        RdKafka::Conf* conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
        std::string errstr;
        conf->set("bootstrap.servers", brokers, errstr);
        conf->set("group.id", group_id, errstr);
        conf->set("auto.offset.reset", "earliest", errstr);  // 从最早的消息开始消费

        // 创建消费者实例
        _consumer = RdKafka::KafkaConsumer::create(conf, errstr);
        if (!_consumer) {
            ERR_LOG("Failed to create consumer: " << errstr);
            exit(1);
        }
        std::cout << "Created consumer " << _consumer->name() << std::endl;
#endif
    }

    void subscribe(const std::vector<std::string>& topics) {
#ifndef DEBUG
        RdKafka::ErrorCode err = _consumer->subscribe(topics);
        if (err != RdKafka::ERR_NO_ERROR) {
            ERR_LOG("Failed to subscribe to topics: " << RdKafka::err2str(err));
        } else {
            std::cout << "Subscribed to topics." << std::endl;
        }
#endif
    }
#ifndef DEBUG
    void consume(KafkaProducer& producer) {
        while (true) {
            RdKafka::Message* msg = _consumer->consume(1000);  // 超时时间 1000ms
            switch (msg->err()) {
                case RdKafka::ERR_NO_ERROR: {
                    std::string payload(static_cast<const char*>(msg->payload()), msg->len());
                    if (!payload.empty() && payload.front() == '"' && payload.back() == '"') {
                        payload = payload.substr(1, payload.size() - 2);
                    }
                    // 去除所有转义反斜杠
                    payload.erase(std::remove(payload.begin(), payload.end(), '\\'), payload.end());
#else
    void consume(KafkaProducer& producer, std::string cmd) {
                    std::string payload = std::string("{\"DCQCN_ENABLE\": ") + (cmd == "UP" ? "false" : "true") + ", \"START_TRANSFER\": true}"; // 模拟接收到的消息
#endif
                    std::cout << "Received message: " << payload << std::endl;
                    try {
                        auto j = json::parse(payload);
                        bool dcqcn_enable = j.value("DCQCN_ENABLE", false);
                        bool start_transfer = j.value("START_TRANSFER", false);
                        std::cout << "DCQCN_ENABLE: " << std::boolalpha << dcqcn_enable
                                  << ", START_TRANSFER: " << std::boolalpha << start_transfer << std::endl;
#ifdef READ_FROM_FILE
                        if(!start_transfer) return;
                        std::string json_file_path;
                        if(dcqcn_enable)
                            json_file_path =  "rate_DCQCN.log";
                        else
                            json_file_path = "rate_LUCP.log";
                        std::ifstream json_file(json_file_path);
                        if (!json_file) {
                            std::cerr << "Failed to open JSON file: " << json_file_path << std::endl;
                            return;
                        }
                        std::string line;
                        std::vector<std::string> list_name = {"task_1_rate", "task_2_rate", "task_3_rate", "total_rate"};
                        while (std::getline(json_file, line)) {
                            // 处理每一行
                            if(line.empty()) continue;
                            auto rate_j = json::parse(line);
                            producer.send(rate_j);
                            usleep(10000);  // 休眠 10ms
                        }
			std::cout << "finish send rate." << std::endl;
#else
                        int status;
                        if (dcqcn_enable) {
                            status = processCMD("bash ~/fileTransferCmd/dcqcn.sh enable");
                            //system("bash ~/fileTransferCmd/dcqcn.sh enable");
                        } else {
                            status = processCMD("bash ~/fileTransferCmd/dcqcn.sh disable");
                            //system("bash ~/fileTransferCmd/dcqcn.sh disable");
                        }
                        if(status != 0) {
                            std::cerr << "Failed to execute command." << std::endl;
                            return;
                        }
                        if(start_transfer)
                        {
                            RESET_LOG();
                            RateCollector collector;
                            collector.finished = false;
                            collector.finished_count = 0;
                            collector.client_num = clients.size();
                            collector.finished_arr.resize(collector.client_num, false);
                            collector.rate_queue.clear();
                            collector.wait_seq_num = 0;
                            std::thread checker(check_and_send_thread, &collector, std::ref(producer));
                            std::vector<std::thread> threads;
                            int idx = 0;
                            for (const auto& client : clients) {
                                threads.emplace_back(send_start_cmd, &collector, client, dcqcn_enable ? "DOWN" : "UP", idx++);
                            }
                            for (auto& t : threads) t.join();
                            checker.join();
                        }
#endif
                    } catch (const std::exception& e) {
                        ERR_LOG("JSON parse error: " << e.what());
                    }
#ifndef DEBUG
                    break;
                }
                case RdKafka::ERR__TIMED_OUT:
                    break;
                default:
                    ERR_LOG("Error: " << msg->errstr());
            }
            delete msg;
        }
#endif
    }

    ~KafkaConsumer() {
#ifndef DEBUG
        _consumer->close();
        delete _consumer;
#endif
    }

private:
    RdKafka::KafkaConsumer* _consumer;
};

int main(int argc, char* argv[]) {
#ifdef DEBUG
    if(argc < 2 || argv[1] != std::string("UP") && argv[1] != std::string("DOWN")) {
        std::cerr << "Usage: " << argv[0] << " <command>(UP|DOWN)" << std::endl;
        return 1;
    }
#endif
    if(parse_master_config("master.conf", clients) < 0) {
        std::cerr << "Failed to parse master configuration." << std::endl;
        return 1;
    }

    KafkaConsumer consumer("106.75.16.47:9092", "sm-group");
    KafkaProducer producer("106.75.16.47:9092");
    consumer.subscribe({"transmission_task"});
    producer.set_topic("transmission_task_status");
#ifndef DEBUG
    consumer.consume(producer);
#else
    consumer.consume(producer, argv[1]); // 模拟接收到的消息
#endif
    return 0;
}
