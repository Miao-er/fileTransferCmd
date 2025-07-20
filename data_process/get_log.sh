sshpass -p "123456" scp server1@192.168.11.2:~/fileTransferCmd/message.log ./message.log.1
sshpass -p "123456" scp server1@192.168.11.2:~/fileTransferCmd/rate.log ./rate.log.1
sshpass -p "123456" scp server3@192.168.13.2:~/fileTransferCmd/message.log ./message.log.3
sshpass -p "123456" scp server3@192.168.13.2:~/fileTransferCmd/rate.log ./rate.log.3
cp ../message.log ./message.log.2
cp ../rate.log ./rate.log.2