# ip_list=(192.168.11.2 192.168.12.2 192.168.13.2 192.168.21.2 192.168.22.2 192.168.23.2)
ip_list=(10.2.229.111 10.2.229.121 10.2.229.131 10.2.152.211 10.2.152.221 10.2.152.231)
user_list=(server1 server2 server3 server4 server5 server6)
for i in ${!ip_list[@]}; do
    sshpass -p "123456" ssh ${user_list[$i]}@${ip_list[$i]} "sudo bash /home/${user_list[$i]}/fileTransferCmd/init.sh"
    echo "ROCE PFC init for ${ip_list[$i]}"
done