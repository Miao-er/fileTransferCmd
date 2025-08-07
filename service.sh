if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <start|stop>"
    exit 1
fi

if [ "$1" != "start" ] && [ "$1" != "stop" ]; then
    echo "Invalid argument: $1. Use 'start' or 'stop'."
    exit 1
fi

ip_list=(192.168.11.2 192.168.12.2 192.168.13.2 192.168.21.2 192.168.22.2 192.168.23.2)
user_list=(server1 server2 server3 server4 server5 server6)


for i in ${!ip_list[@]}; do
    if [ $i -lt 3 ]; then
        if [ "$1" == "start" ]; then
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "cd /home/${user_list[$i]}/fileTransferCmd; sudo ./server ${ip_list[$i]} >/dev/null 2>&1  &"
            echo "server start for ${ip_list[$i]}"
        else 
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "sudo killall -e server"
            echo "server stop for ${ip_list[$i]}"
        fi
    else
        if [ "$1" == "start" ]; then
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "cd /home/${user_list[$i]}/fileTransferCmd; sudo ./client >/dev/null 2>&1  &"
            echo "client start for ${ip_list[$i]}"
        else 
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "sudo killall -e client"
            echo "client stop for ${ip_list[$i]}"
        fi
    fi
done