
#include<string>
#include<iostream>
#include <nlohmann/json.hpp>
#include<fstream>
#include<vector>
int main(int argc, char* argv[])
{
	std::string json_file_path = argv[1];
	std::ifstream json_file(json_file_path);
	if (!json_file) {
                            std::cerr << "Failed to open JSON file: " << json_file_path << std::endl;
                            return 0;
                        }
                        std::string line;
                        std::vector<std::string> list_name = {"task_1_rate", "task_2_rate", "task_3_rate", "total_rate"};
                        while (std::getline(json_file, line)) {
                            // 处理每一行
                            if(line.empty()) continue;
                            auto rate_j = nlohmann::json::parse(line);
                            for(auto & name : list_name) {
                                if (!rate_j[name].is_null()) {
                                    double rate = rate_j[name];
                                    rate = rate / 5;
                                    rate_j[name] = rate;
			    	    std::cout << name << ":" << rate << " ";
                                }
				else 
				    std::cout << name << ":" << "null " ;
                            }
			    std::cout << "timepoint:" << rate_j["timepoint"] << std::endl;
                        }
	return 0;
}
