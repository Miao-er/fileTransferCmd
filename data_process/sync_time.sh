for i in 11 12 13 21 22 23; do
    sshpass -p "123456" ssh root@192.168.$i.2 "
        if ! dpkg -l | grep -i ntpdate >/dev/null; then
            apt update && apt install -y ntpdate;
        fi
        ntpdate ntp.fudan.edu.cn
    " &
done
wait
echo "time sync completed on all servers."