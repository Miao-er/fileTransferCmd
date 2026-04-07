import json
import os

def convert_json_format(input_file, output_file):
    """
    将图 4 格式的 JSON 转换为图 5 格式
    """
    try:
        with open(input_file, 'r', encoding='utf-8') as infile, \
             open(output_file, 'w', encoding='utf-8') as outfile:
            
            for line in infile:
                line = line.strip()
                if not line:
                    continue
                
                # 解析原始 JSON 对象
                data = json.loads(line)
                
                # 构建新的字典结构
                # 1. 添加图 5 中新增的字段并设为 null
                new_data = {
                    "fairness": None,
                    "stability": None,
                    "task_1_rate": data.get("task_1_rate"),
                    "task_2_rate": data.get("task_2_rate"),
                    "task_3_rate": data.get("task_3_rate"),
                }
                
                # 2. 转换 timepoint 逻辑
                # 假设图 4 的 timepoint (0, 1, 2...) 映射到图 5 的 (0.0, 0.01, 0.02...)
                original_timepoint = data.get("timepoint", 0)
                new_data["timepoint"] = round(float(original_timepoint) * 0.01, 2)
                
                # 3. 保留总速率字段
                new_data["total_rate"] = data.get("total_rate")
                
                # 将处理后的对象写回文件，确保每一行是一个 JSON 对象
                json_line = json.dumps(new_data, separators=(',', ':'))
                outfile.write(json_line + '\n')
                
        print(f"转换完成！结果已保存至: {output_file}")
        
    except Exception as e:
        print(f"转换过程中出错: {e}")

# 使用示例
if __name__ == "__main__":
    # 请根据你的实际文件名修改此处
    input_path = 'rate_lucp.log'   # 对应图 4 格式的文件
    output_path = 'rate_LUCP.log'  # 对应图 5 格式的文件
    
    # 模拟创建一个测试文件（如果不存在）
    if not os.path.exists(input_path):
        with open(input_path, 'w') as f:
            f.write('{"task_1_rate":9.79,"task_2_rate":null,"task_3_rate":null,"timepoint":1,"total_rate":9.79}\n')
            f.write('{"task_1_rate":9.69,"task_2_rate":null,"task_3_rate":null,"timepoint":2,"total_rate":9.69}\n')

    convert_json_format(input_path, output_path)