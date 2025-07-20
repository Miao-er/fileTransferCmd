value=1
if [ $1 == "enable" ]; then
    value=1
elif [ $1 == "disable" ]; then
    value=0
else
    echo "Usage: $0 <enable|disable>"
    exit 1
fi
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/0
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/1
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/2
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/3
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/4
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/5
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/6
echo $value > /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/7
cat /sys/class/net/enp1s0f0np0/ecn/roce_rp/enable/*