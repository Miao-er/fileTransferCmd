file_name=$1
if [ -z "$file_name" ]; then
    echo "Usage: $0 <file_name>"
    exit 1
fi

ip=(11 12 13 21 22 23)
name=("server1" "server2" "server3" "server4" "server5" "server6")
for i in ${!ip[@]}; do
    sshpass -p "123456" scp $file_name ${name[$i]}@192.168.${ip[$i]}.2:~/fileTransferCmd/$file_name
    sshpass -p "123456" ssh ${name[$i]}@192.168.${ip[$i]}.2 "cd ~/fileTransferCmd; make clean; make"
    echo "File $file_name copied to 192.168.${ip[$i]}.2"
done
echo "File transfer completed."