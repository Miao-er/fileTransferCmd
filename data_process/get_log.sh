algo=""
if [ $1 == "UP" ];then
    algo="lucp"
elif [ $1 == "DOWN" ];then
    algo="dcqcn"
else
    echo "Usage: $0 <UP|DOWN>"
    exit 1
fi
sshpass -p "123456" scp server4@192.168.21.2:~/fileTransferCmd/message.log ./message.log.1.$algo
sshpass -p "123456" scp server4@192.168.21.2:~/fileTransferCmd/rate.log ./rate.log.1.$algo
sshpass -p "123456" scp server5@192.168.22.2:~/fileTransferCmd/message.log ./message.log.2.$algo
sshpass -p "123456" scp server5@192.168.22.2:~/fileTransferCmd/rate.log ./rate.log.2.$algo
sshpass -p "123456" scp server6@192.168.23.2:~/fileTransferCmd/message.log ./message.log.3.$algo
sshpass -p "123456" scp server6@192.168.23.2:~/fileTransferCmd/rate.log ./rate.log.3.$algo