value=1
if [ $1 == "enable" ]; then
    value=1
elif [ $1 == "disable" ]; then
    value=0
else
    echo "Usage: $0 <enable|disable>"
    exit 1
fi
#ip_list=(192.168.11.2 192.168.12.2 192.168.13.2 192.168.21.2 192.168.22.2 192.168.23.2)
ip_list=(10.2.229.111 10.2.229.121 10.2.229.131 10.2.152.211 10.2.152.221 10.2.152.231)
for ip in ${ip_list[@]}; do
    sshpass -p "123456" ssh root@$ip "ls /sys/class/net/enp1s0f0np0/ecn/roce_np/enable/* | xargs -I {} sh -c 'echo $value > {}'"
    sshpass -p "123456" ssh root@$ip "ls /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/* | xargs -I {} sh -c 'echo $value > {}'"
    echo "$1 ECN for $ip"
done    