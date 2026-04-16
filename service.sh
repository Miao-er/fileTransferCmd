if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <start|stop>"
    exit 1
fi

if [ "$1" != "start" ] && [ "$1" != "stop" ]; then
    echo "Invalid argument: $1. Use 'start' or 'stop'."
    exit 1
fi
if [ "$1" == "start" ]; then
	sshpass -p "123456" ssh sw2@10.2.152.201 "cd /home/sw2/fileTransferCmd; sudo ./switch switch2.conf > /dev/null 2>&1 &"
	sshpass -p "123456" ssh sw1@10.2.229.101 "cd /home/sw1/fileTransferCmd; sudo ./switch switch1.conf > /dev/null 2>&1 &"
	echo "switch service start."
elif [ "$1" == "stop" ]; then
	sshpass -p "123456" ssh sw2@10.2.152.201 "sudo killall -e switch"
	sshpass -p "123456" ssh sw1@10.2.229.101 "sudo killall -e switch"
	echo "switch service stop."
fi
# ip_list=(192.168.11.2 192.168.12.2 192.168.13.2 192.168.21.2 192.168.22.2 192.168.23.2)
ip_list=(10.2.229.111 10.2.229.121 10.2.229.131 10.2.152.211 10.2.152.221 10.2.152.231)
user_list=(server1 server2 server3 server4 server5 server6)


for i in ${!ip_list[@]}; do
    if [ $i -lt 3 ]; then
        if [ "$1" == "start" ]; then
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "cd /home/${user_list[$i]}/fileTransferCmd; sudo ./server ${ip_list[$i]} >server.log 2>&1  &"
            echo "server start for ${ip_list[$i]}"
        else 
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "sudo killall -e server"
            echo "server stop for ${ip_list[$i]}"
        fi
    else
        if [ "$1" == "start" ]; then
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "cd /home/${user_list[$i]}/fileTransferCmd; sudo ./client >client.log 2>&1  &"
            echo "client start for ${ip_list[$i]}"
        else 
            sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "sudo killall -e client"
            echo "client stop for ${ip_list[$i]}"
        fi
    fi
done
