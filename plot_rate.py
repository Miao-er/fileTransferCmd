import matplotlib.pyplot as plt
import json
import math
import argparse

from collections import defaultdict

collect_interval = 10  # 10ms

def plot_debug_log(log_path, clients, args):
    log_path = f'test-case/rate-case{args.case}.log'
    pfc_log_path = f'test-case/PFC_case{args.case}_{"up" if args.enable else "down"}.log'
    times = []
    client_num = len(clients)
    total_rates = []
    clients_rates = [[] for _ in range(client_num)]

    with open(log_path, 'r') as f:
        for line in f:
            try:
                obj = json.loads(line.strip())
                t = obj.get('timepoint', None)
                if t is None:
                    continue
                times.append(t)  # 单位为s
                for i in range(client_num):
                    key = f"{clients[i]['socket_ip']}_rate"
                    r = obj.get(key, None)
                    clients_rates[i].append(r if r is not None else None)
                total = obj.get('total_rate', None)
                total_rates.append(total if total is not None else None)
            except Exception:
                continue

    # 使用两个子图：上图显示每条单流，下图显示总流量
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(16, 8), sharex=True)

    lines = []
    for i in range(client_num):
        line, = ax1.plot(times, clients_rates[i], label=f'{clients[i]["socket_ip"]}', linewidth=2)
        lines.append(line)

    ax2.plot(times, total_rates, label='total', color='black', linewidth=2, linestyle='--')

    ax1.set_ylabel('rate (Gbps)')
    ax2.set_ylabel('rate (Gbps)')
    ax2.set_xlabel('time(s)')
    fig.suptitle(f'Rate for case{args.case}-{"enabled" if args.enable else "disabled"}')

    # 上图：绘制每个 client 的平均速率（忽略 None），并在图上标注
    for i in range(client_num):
        vals = [v for v in clients_rates[i] if v is not None]
        if not vals:
            continue
        avg = sum(vals) / len(vals)
        color = lines[i].get_color() if i < len(lines) else None

    ax1.legend(loc='upper right')
    ax1.grid(True)

    # 在图右侧空白处显示每个 client 的平均速率
    client_avg_lines = {}
    for i in range(client_num):
        vals = [v for v in clients_rates[i] if v is not None]
        if not vals:
            client_avg_lines[clients[i]["socket_ip"]] = f'{clients[i]["socket_ip"]} N/A'
        else:
            avg = sum(vals) / len(vals)
            client_avg_lines[clients[i]["socket_ip"]] = f'{clients[i]["socket_ip"]}\n    AvgRate:{avg:.3f} Gbps\n'
    client_pause_std = []
    if args.case == 4 or args.case == 5:
        # 读取对应 client 的 PFC pause 帧数
        client_pause_counts = defaultdict(list)
        with open(pfc_log_path, 'r') as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) != 2:
                    continue
                ip_addr, pause_count = parts
                if(client_pause_counts[ip_addr] == []):
                    client_pause_counts[ip_addr].append(int(pause_count))
                else:
                    diff = int(pause_count) - client_pause_counts[ip_addr][-1]
                    client_pause_std.append(int(diff / (clients[i]["file_size"] / (1024**3))))
                    client_avg_lines[ip_addr] +=  f'    PFC-Density: {client_pause_std[-1]} per GB'

    info_text = '\n'.join(client_avg_lines.values())
    # info_text = "Average Rates:\n" + info_text

    # plt.subplots_adjust(left=0.2, bottom=0.2, right=0.8, top=0.8, hspace=0.2, wspace=0.3)
    fig.text(1.0, 0.7, info_text, ha='left', va='center', fontsize=14, bbox=dict(facecolor='white', alpha=0.8))

    # 下图：总流量及其平均值
    tvals = [v for v in total_rates if v is not None]
    if tvals:
        avg_total = sum(tvals) / len(tvals)
        #ax2.axhline(y=avg_total, color='black', linestyle=':', linewidth=2, label=f'total avg: {avg_total:.3f} Gbps')
        if args.case == 4 or args.case == 5:
            fig.text(1.0, 0.3, f'Total\n    AvgRate:{avg_total:.3f} Gbps\n    PFC-Density: {sum(client_pause_std)} per GB', ha='left', va='center', fontsize=14, bbox=dict(facecolor='white', alpha=0.8))
        else:
            fig.text(1.0, 0.3, f'Total AvgRate: {avg_total:.3f} Gbps', ha='left', va='center', fontsize=14, bbox=dict(facecolor='white', alpha=0.8))
    ax2.legend(loc='upper right')
    ax2.grid(True)

    out_path = f'test-case/rate-case{args.case}-{"up" if args.enable else "down"}.png'
    plt.tight_layout()
    fig.savefig(out_path, dpi=300, bbox_inches='tight')
    plt.show()

def parse_args():
    parser = argparse.ArgumentParser(description="Plot rate info")
    parser.add_argument('--case', type=int, help='case number')
    parser.add_argument('--enable', type=int, help='enable lucp or not')
    return parser.parse_args()

def parse_size(size_str):
    if size_str.endswith('G'):
        return float(size_str[:-1]) * 1024 * 1024 * 1024
    elif size_str.endswith('M'):
        return float(size_str[:-1]) * 1024 * 1024
    elif size_str.endswith('K'):
        return float(size_str[:-1]) * 1024
    else:
        return float(size_str)

def parse_conf(args):
    client_arr = []
    case_conf_path = f'test-case/case{args.case}.conf'
    with open(case_conf_path, 'r') as f:
        lines = f.readlines()
    for line in lines:
        res = line.strip().split()
        if len(res) != 6 or res[0].startswith('#'):
            continue
        socket_ip, local_ip, server_ip, port, file_size, delay = res[:6]
        client_arr.append({
            'socket_ip': socket_ip,
            'local_ip': local_ip,
            'server_ip': server_ip,
            'port': int(port),
            'file_size': parse_size(file_size),
            'delay': delay
        })
    return client_arr
        

if __name__ == "__main__":
    args = parse_args()
    clients = parse_conf(args)
    plot_debug_log('test-case/rate-case.log', clients, args)