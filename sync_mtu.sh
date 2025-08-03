ip=(11 12 13 21 22 23)
name=("server1" "server2" "server3" "server4" "server5" "server6")
for i in ${!ip[@]}; do
    sshpass -p "123456" ssh root@192.168.${ip[$i]}.2 "ip link set mtu 4200 dev enp1s0f0np0"
    echo "set mtu 4200 for 192.168.${ip[$i]}.2"
done