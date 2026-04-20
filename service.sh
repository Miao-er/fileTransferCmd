if [ "$#" != 2 ] && [ "$#" != 3 ]; then
    echo "Usage: $0 <start|stop> <0/1> <delay|bw>"
    exit 1
fi

if [ "$1" != "start" ] && [ "$1" != "stop" ]; then
    echo "Invalid argument: $1. Use 'start' or 'stop'."
    exit 1
fi
if [ "$2" -ne 0 ] && [ "$2" -ne 1 ]; then
    echo "Invalid argument: $2. Use '0' or '1'."
    exit 1
fi
if [ "$1" == "start" ] && [ "$2" == "1" ]; then
    if  [ "$3" != "delay" ] && [ "$3" != "bw" ]; then
        echo "Invalid argument: $3. Use 'delay' or 'bw'."
        exit 1
    fi
fi

if [ "$2" -eq 1 ]; then 
    if [ "$1" == "start" ]; then
        conf_file_suffix=""
        if [ "$3" == "bw" ]; then
            conf_file_suffix=""
        elif [ "$3" == "delay" ]; then
            conf_file_suffix=".delay" 
        fi
        sshpass -p "123456" ssh sw2@10.2.152.201 "cd /home/sw2/fileTransferCmd; sudo ./switch switch2.conf${conf_file_suffix} > /dev/null 2>&1 &"
        sshpass -p "123456" ssh sw1@10.2.229.101 "cd /home/sw1/fileTransferCmd; sudo ./switch switch1.conf${conf_file_suffix} > /dev/null 2>&1 &"
        echo "switch service start."
    elif [ "$1" == "stop" ]; then
        sshpass -p "123456" ssh sw2@10.2.152.201 "sudo killall -e switch"
        sshpass -p "123456" ssh sw1@10.2.229.101 "sudo killall -e switch"
        echo "switch service stop."
    fi
    exit 0
fi
# ip_list=(192.168.11.2 192.168.12.2 192.168.13.2 192.168.21.2 192.168.22.2 192.168.23.2)
# ip_list=(10.2.229.111 10.2.229.121 10.2.229.131 10.2.152.211 10.2.152.221 10.2.152.231)
server_ip_list=(10.2.229.111 10.2.229.121)
server_user_list=(server1 server2)
client_ip_list=(10.2.152.241 10.2.152.211 10.2.152.221 10.2.152.231)
client_user_list=(server3 server4 server5 server6)


for i in ${!server_ip_list[@]}; do
    if [ "$1" == "start" ]; then
        sshpass -p "123456" ssh ${server_user_list[$i]}@${server_ip_list[$i]} "cd /home/${server_user_list[$i]}/fileTransferCmd; make server; sudo ./server ${server_ip_list[$i]} >server.log 2>&1  &"
        echo "server start for ${server_ip_list[$i]}"
    else 
        sshpass -p "123456" ssh ${server_user_list[$i]}@${server_ip_list[$i]} "sudo killall -e server"
        echo "server stop for ${server_ip_list[$i]}"
    fi
done

for i in ${!client_ip_list[@]}; do
    if [ "$1" == "start" ]; then
        sshpass -p "123456" ssh ${client_user_list[$i]}@${client_ip_list[$i]} "cd /home/${client_user_list[$i]}/fileTransferCmd; make client; sudo ./client >client.log 2>&1  &"
        echo "client start for ${client_ip_list[$i]}"
    else 
        sshpass -p "123456" ssh ${client_user_list[$i]}@${client_ip_list[$i]} "sudo killall -e client"
        echo "client stop for ${client_ip_list[$i]}"
    fi
done
