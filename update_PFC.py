import argparse
import subprocess

def parse_args():
    parser = argparse.ArgumentParser(description="get PFC update")
    parser.add_argument('--case', type=int, required=True, help='test case number')
    parser.add_argument('--enable', type=int, required=True, help='enable LUCP or not')
    parser.add_argument('--stage', '-s', choices=['before', 'after'], required=True, help='stage of the test')
    return parser.parse_args()

def run_command(cmd):
    result = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"Command failed: {cmd}")
        print(f"Error: {result.stderr}")
        return None
    return result.stdout.strip()

def parse_conf(args):
    client_arr = []
    case_conf_path = f'test-case/case{args.case}.conf'
    with open(case_conf_path, 'r') as f:
        for line in f:
            line = line.strip()
            parts = line.split()
            if len(parts) != 6 or parts[0].startswith('#'):
                continue
            client = parts[0]
            net_parts = client.split('.')
            if len(net_parts) != 4:
                print(f"Invalid client IP: {client}")
                continue
            base = 0
            if(net_parts[2] == '152'):
                base = 3
            elif(net_parts[2] == '229'):
                base = 0
            else:
                print(f"Unexpected client IP segment: {net_parts[2]}")
                continue
            idx = (int(net_parts[3]) // 10) % 10
            idx += base 
            client_arr.append({"ip_addr": client, "idx": idx, "server_name": f"server{idx}"})
    return client_arr

def queryPFC(client, args):
        client_ssh_addr = f"{client['server_name']}@{client['ip_addr']}"
        print(f"[{client_ssh_addr}]")
        cmd = f"sshpass -p '123456' ssh {client_ssh_addr} 'ethtool -S enp1s0f0np0 | grep rx_prio3_pause:'"
        print(f"\tRunning command: {cmd}")
        output = run_command(cmd)
        if output is not None:
            print(f"\tOutput: {output}")
        return output


if __name__ == "__main__":
    args = parse_args()
    if args.case != 4 and args.case !=5:
        print("PFC stats query is only for case 4 and 5.")
        exit(1)
    clients = parse_conf(args)
    filename = f"test-case/PFC_case{args.case}_{'up' if args.enable else 'down'}.log"
    if(args.stage == 'before'):
        print("Querying PFC stats before the test:")
    else:
        print("Querying PFC stats after the test:")
     
    if args.stage == 'before': 
        with open(filename, 'w') as f:
            for client in clients:
                output = queryPFC(client, args)
                if output is not None:
                    key, value = output.split(':')
                    f.write(f"{client['ip_addr']} {value.strip()}\n")
    else:
        with open(filename, 'a') as f:
            for client in clients:
                output = queryPFC(client, args)
                if output is not None:
                    key, value = output.split(':')
                    f.write(f"{client['ip_addr']} {value.strip()}\n")
