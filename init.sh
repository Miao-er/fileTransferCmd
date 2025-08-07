sudo ip link set mtu 4200 dev enp1s0f0np0
sudo mlxreg -d 01:00.0 --yes --reg_name ROCE_ACCL --set  "roce_adp_retrans_en=0x0,roce_slow_restart_en=0x0"
sudo mlnx_qos -i enp1s0f0np0 --trust=dscp -f 0,0,0,1,0,0,0,0
echo 106 | sudo tee /sys/class/infiniband/mlx5_0/tc/1/traffic_class