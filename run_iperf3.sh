#!/bin/bash

# iperf3 测试管理脚本
# 用法: ./run_iperf3.sh start [cubic|bbr] 或 ./iperf_manager.sh stop

# 服务器列表 (iperf3 server)
SERVERS=("10.2.229.111" "10.2.229.121")
server_user_list=("server1" "server2")

# 客户端列表 (iperf3 client)，与服务器一一对应
CLIENTS=("10.2.152.241" "10.2.152.211" "10.2.152.221" "10.2.152.231")
client_user_list=("server3" "server4" "server5" "server6")

# iperf3 端口
client_conn_PORT=(6070 6071 6070 6071)
client_conn_server_ip=("10.2.229.111" "10.2.229.111" "10.2.229.121" "10.2.229.121")

# 线程数
PARALLEL=1


# 远程命令执行函数
execute_remote() {
    local user=$1
    local host=$2
    local cmd=$3
    sshpass -p "123456" ssh ${user}@${host} "$cmd"
}

# 启动 iperf3 服务器
start_servers() {
    local congestion_algo=$1
    echo "========================================="
    echo "Starting iperf3 servers with ${congestion_algo} algorithm"
    echo "========================================="
    for i in "${!SERVERS[@]}"; do
        local server=${SERVERS[$i]}
        local user=${server_user_list[$i]}
        echo "Starting server on ${user} @ ${server}..."

        execute_remote ${user} ${server} "sudo sysctl -w net.ipv4.tcp_congestion_control=${congestion_algo}; iperf3 -s -p 6070 -D; iperf3 -s -p 6071 -D"
        
        if [ $? -eq 0 ]; then
            echo "✓ Server started on ${server}"
        else
            echo "✗ Failed to start server on ${server}"
        fi
    done
    echo ""
}

# 启动 iperf3 客户端
start_clients() {
    local congestion_algo=$1
    echo "========================================="
    echo "Starting iperf3 clients with ${congestion_algo} algorithm"
    echo "========================================="
    
    for i in "${!CLIENTS[@]}"; do
        local client=${CLIENTS[$i]}
        local user=${client_user_list[$i]}
        local port=${client_conn_PORT[$i]}
        local server=${client_conn_server_ip[$i]}
        
        echo "Starting client on ${client} -> ${server}..."

        # 设置拥塞控制算法并启动 iperf3 客户端（无限运行，10个线程）
        execute_remote ${user} ${client} "sudo sysctl -w net.ipv4.tcp_congestion_control=${congestion_algo}; iperf3 -w 256M -c ${server} -p ${port} -P ${PARALLEL} -t 0 -C ${congestion_algo}  >/tmp/${user}.log 2>&1 &"
        
        if [ $? -eq 0 ]; then
            echo "✓ Client started on ${client} -> ${server} (${PARALLEL} threads)"
        else
            echo "✗ Failed to start client on ${client}"
        fi
    done
    echo ""
}

# 停止所有 iperf3 进程
stop_all() {
    echo "========================================="
    echo "Stopping all iperf3 processes"
    echo "========================================="
    
    # 停止所有客户端
    echo "Stopping clients..."
    for i in "${!CLIENTS[@]}"; do
        local client=${CLIENTS[$i]}
        local user=${client_user_list[$i]}
        echo "Stopping client on ${client}..."
        execute_remote ${user} ${client} "killall iperf3"
        if [ $? -eq 0 ]; then
            echo "✓ Stopped client on ${client}"
        else
            echo "✗ Failed to stop client on ${client}"
        fi
    done
    echo ""
    
    # 停止所有服务器
    echo "Stopping servers..."
    for i in "${!SERVERS[@]}"; do
        local server=${SERVERS[$i]}
        local user=${server_user_list[$i]}
        echo "Stopping server on ${server}..."
        execute_remote ${user} ${server} "killall iperf3"
        if [ $? -eq 0 ]; then
            echo "✓ Stopped server on ${server}"
        else
            echo "✗ Failed to stop server on ${server}"
        fi
    done
    echo ""
    
    echo "All iperf3 processes stopped."
}

# 主逻辑
case "$1" in
    start)
        # 获取拥塞控制算法，默认为 cubic
        ALGO=${2:-cubic}
        
        # 验证算法参数
        if [[ "$ALGO" != "cubic" && "$ALGO" != "bbr" ]]; then
            echo "Error: Invalid congestion control algorithm '${ALGO}'"
            echo "Usage: $0 start [cubic|bbr]"
            exit 1
        fi
        
        echo "Using congestion control algorithm: ${ALGO}"
        echo ""
        
        start_servers ${ALGO}
        start_clients ${ALGO}
        
        echo "========================================="
        echo "iperf3 test started successfully!"
        echo "Servers: ${SERVERS[*]}"
        echo "Clients: ${CLIENTS[*]}"
        echo "Algorithm: ${ALGO}"
        echo "Parallel threads: ${PARALLEL}"
        echo "========================================="
        ;;
    
    stop)
        stop_all
        ;;
    
    *)
        echo "Usage: $0 {start [cubic|bbr]|stop}"
        echo ""
        echo "Examples:"
        echo "  $0 start          # Start with cubic (default)"
        echo "  $0 start cubic    # Start with cubic algorithm"
        echo "  $0 start bbr      # Start with bbr algorithm"
        echo "  $0 stop           # Stop all iperf3 processes"
        exit 1
        ;;
esac