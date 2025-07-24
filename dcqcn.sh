value=1
if [ $1 == "enable" ]; then
    value=1
elif [ $1 == "disable" ]; then
    value=0
else
    echo "Usage: $0 <enable|disable>"
    exit 1
fi

for i in 11 12 13 21 22 23; do
    sshpass -p "123456" ssh root@192.168.$i.2 "ls /sys/class/net/enp1s0f0np0/ecn/roce_np/enable/* | xargs -I {} sh -c 'echo $value > {}'"
    sshpass -p "123456" ssh root@192.168.$i.2 "ls /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/* | xargs -I {} sh -c 'echo $value > {}'"
    echo "$1 ECN for 192.168.$i.2"
done