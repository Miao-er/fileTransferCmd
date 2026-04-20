#!/bin/bash

echo "========================================="
echo "Master Control Script"
echo "========================================="
echo "This script will continuously execute './master UP'"
echo "Type 'q' or 'quit' to exit the loop."
echo "========================================="

# 启动服务
echo "Starting service..."
./service.sh start 0
if [ $? -ne 0 ]; then
    echo "Error: Failed to start service"
    exit 1
fi
echo "Service started successfully."

# 设置信号处理，捕获Ctrl+C
trap 'echo ""; echo "Interrupted by user."; ./service.sh stop; exit 0' INT

while true; do
    echo ""
    echo "-----------------------------------------"
    echo "Executing: ./master UP"
    echo "-----------------------------------------"
    
    # 在后台启动 master UP，并监控是否有按键输入
    ./master UP &
    MASTER_PID=$!
    
    EXIT_FLAG=0
    
    # 等待进程结束或用户输入
    while kill -0 $MASTER_PID 2>/dev/null; do
        # 使用 read -t 设置超时，非阻塞检查用户输入
        if read -t 0.1 -n 1 user_input 2>/dev/null; then
            # 将输入转换为小写进行比较
            user_input_lower=$(echo "$user_input" | tr '[:upper:]' '[:lower:]')
            
            if [ "$user_input_lower" = "q" ] || [ "$user_input_lower" = "quit" ]; then
                echo ""
                echo "Exit command received. Killing master process..."
                kill $MASTER_PID 2>/dev/null
                wait $MASTER_PID 2>/dev/null
                EXIT_FLAG=1
                break
            fi
        fi
    done
    
    # 如果是因为用户输入退出，则停止服务并退出脚本
    if [ $EXIT_FLAG -eq 1 ]; then
        echo "Stopping service..."
        ./service.sh stop 0
        if [ $? -ne 0 ]; then
            echo "Warning: Failed to stop service"
        else
            echo "Service stopped successfully."
        fi
        echo "Script terminated by user."
        exit 0
    fi
    
    # 等待进程完全结束并获取退出状态
    wait $MASTER_PID 2>/dev/null
    EXIT_STATUS=$?
    
    # 检查上一个命令的退出状态
    if [ $EXIT_STATUS -ne 0 ]; then
        echo "Warning: ./master UP exited with non-zero status ($EXIT_STATUS)"
    fi
    
    echo "Run completed. Starting next iteration..."
done