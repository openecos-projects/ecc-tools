#include "NetWeightPre.hh"

namespace imp {

std::unordered_map<std::string, size_t> mapVertexNamesToIds(const HyperGraph<Multilevel>& graph) {
    std::unordered_map<std::string, size_t> vertex_name_to_id;
    for (size_t i = 0; i < graph.vSize(); ++i) {
        auto& vertex = graph.vertex_at(i);
        auto& object = vertex.property();
        std::string vertex_name = object->get_name();
        vertex_name_to_id[vertex_name] = i;
        // std::cout << "Vertex Name: " << vertex_name << ", ID: " << i << std::endl;
        if (!object->isInstance()) {
            std::cout << "Warning: Vertex at position " << i << " is not an instance." << std::endl;
        }
    }
    return vertex_name_to_id;
}

std::vector<TimingPath> parseTimingReport(const std::string& filePath) {
    std::ifstream file(filePath);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    auto blocks = splitByPath(content);
    std::vector<TimingPath> paths;
    int id = 0;
    for (const auto& block : blocks) {
        id++;
        bool skip_path = false;
        if (!block.empty()) {
            auto path = parsePathBlock(block, skip_path);
            if (!skip_path) {
                // Process the strings before pushing the path into the vector
                std::string processedBeginpoint = processBrackets(path.beginpoint);
                std::string processedEndpoint = processBrackets(path.endpoint);

                std::vector<SubPath> processedSubPaths;
                for (auto& subPath : path.sub_paths) {
                    std::string processedOutputPin = processBrackets(subPath.outputPin);
                    std::string processedInputPin = processBrackets(subPath.inputPin);
                    processedSubPaths.push_back(SubPath{processedOutputPin, processedInputPin, subPath.delay});
                }

                path.beginpoint = processedBeginpoint;
                path.endpoint = processedEndpoint;
                path.sub_paths = processedSubPaths;

                paths.push_back(path);
            } else {
                // std::cout << "Skip path " << id << std::endl;
            }
        }
    }
    // for (const auto& path : paths) {
    //   std::cout << "Beginpoint: " << path.beginpoint << std::endl;
    //   std::cout << "Endpoint: " << path.endpoint << std::endl;
    //   std::cout << "Slack Time: " << path.slack_time << std::endl;
    //   std::cout << "Path Depth: " << path.path_depth << std::endl;
    //   for (const auto& subpath : path.sub_paths) {
    //     std::cout << "  Net: " << subpath.outputPin << " to " << subpath.inputPin << ", Delay: " << subpath.delay << std::endl;
    //   }
    //   std::cout << std::endl;
    // }
    return paths;
}

std::vector<std::string> splitByPath(const std::string& str) {
    std::vector<std::string> result;
    size_t pos = 0, startPos = 0, lastPos = 0;

    // 循环直到字符串末尾
    while ((pos = str.find("Path ", startPos)) != std::string::npos) {
        // 检查"Path "后是否紧跟数字
        if (std::isdigit(str[pos + 5])) { // "Path "后的字符位置为pos + 5
            if (lastPos != 0) { // 确保不是第一个找到的“Path ”
                // 保存从上一个“Path ”开始到当前找到的“Path ”之间的字符串
                result.push_back(str.substr(lastPos, pos - lastPos));
            }
            lastPos = pos; // 更新lastPos为当前“Path ”的位置
        }
        startPos = pos + 5; // 移动到下一个字符，准备下一轮查找
    }

    // 处理最后一个段落
    if (lastPos != 0 && lastPos < str.size()) {
        result.push_back(str.substr(lastPos));
    }

    return result;
}

std::vector<std::string> splitString(const std::string& str, char delimiter) {
    std::vector<std::string> parts;
    std::istringstream iss(str);
    std::string token;

    while (std::getline(iss, token, delimiter)) {
        if (!token.empty()) { // 忽略空的token
            parts.push_back(token);
        }
    }

    return parts;
}

std::string trim(const std::string& str) {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        start++;
    }
    auto end = str.end();
    do {
        end--;
    } while (std::distance(start, end) > 0 && std::isspace(*end));

    return std::string(start, end + 1);
}


std::string processBrackets(const std::string& input) {
    std::string result;
    for (char ch : input) {
        if (ch == '[' || ch == ']') {
            result += '\\'; // Add a backslash before the bracket
        }
        result += ch;
    }
    return result;
}

TimingPath parsePathBlock(const std::string& block, bool& skip_path) {
    std::istringstream blockStream(block);
    std::string line;
    TimingPath path;
    bool timing_path_found = false;
    bool beginpoint_found = false;
    bool endpoint_found = false;
    int unit_count = 0;
    int current_line_number = 0;
    int beginpoint_line_number, endpoint_line_number;
    std::vector<std::string> last_line_parts;
    if (skip_path) {
      std::cout<<"skip_path error"<<std::endl;
      exit(0);
    }
    while (getline(blockStream, line)) {
        current_line_number++; // 每次循环开始时增加行号
        auto parts = splitString(line, ' ');
        if (line.find("Endpoint:") != std::string::npos) {
            // 在这里，current_line_number 就是包含 "Endpoint:" 的那一行的行号
            std::string afterColon = line.substr(line.find(":") + 1);
            path.endpoint = splitString(trim(afterColon), ' ')[0];
            if (path.endpoint.find('/') == std::string::npos) {
              skip_path = true;
              break;
            }
        } else if (line.find("Beginpoint:") != std::string::npos) {
            // 在这里，current_line_number 就是包含 "Beginpoint:" 的那一行的行号
            std::string afterColon = line.substr(line.find(":") + 1);
            path.beginpoint = splitString(trim(afterColon), ' ')[0];
            if (path.beginpoint.find('/') == std::string::npos) {
              skip_path = true;
              break;
            }
        } else if (line.find("Slack Time") != std::string::npos) {
            if (!parts.empty()) {
                try {
                    path.slack_time = std::stod(parts.back());
                } catch (const std::invalid_argument& e) {
                    std::cerr << "Invalid argument: " << std::endl;
                } catch (const std::out_of_range& e) {
                    std::cerr << "Out of range: " << std::endl;
                }
            }
        } else if (line.find("Timing Path:") != std::string::npos) {
            timing_path_found = true;
        } else if (timing_path_found) {
            if (line.find(path.beginpoint) != std::string::npos) {
                beginpoint_found = true;
                beginpoint_line_number = current_line_number;
                last_line_parts = parts;
                continue;
            }
            if ((beginpoint_found) && (!endpoint_found)) {
                if (line.find(path.endpoint) != std::string::npos) {
                    endpoint_line_number = current_line_number;
                    endpoint_found = true;
                }
                if (last_line_parts.empty() || parts.empty()) {
                  std::cout<<"Error, should not empty"<<std::endl;
                  exit(0);
                }
                SubPath sub_path;
                auto output = last_line_parts[0];
                auto input = parts[0];
                double delay = std::stod(parts[3]);
                size_t lastOut = output.rfind('/');
                size_t lastIn = input.rfind('/');
                if (lastOut != std::string::npos && lastIn != std::string::npos &&
                    output.substr(0, lastOut) == input.substr(0, lastIn)) {
                    unit_count += 1;
                    last_line_parts = parts;
                    continue;
                } else {
                    sub_path.outputPin = output;
                    sub_path.inputPin = input;
                    sub_path.delay = delay;
                    path.sub_paths.push_back(sub_path);
                }
            }
        }
        last_line_parts = parts;
    }
    path.path_depth = unit_count + 2;
    if (!skip_path) {
      size_t lastSlashPosStart = path.beginpoint.rfind('/');
      size_t lastSlashPosEnd = path.endpoint.rfind('/');
      // std::cout<<"beginsub = "<<path.beginpoint.substr(0, lastSlashPosStart)<<std::endl;
      // std::cout<<"endsub = "<<path.endpoint.substr(0, lastSlashPosEnd)<<std::endl;

      if (lastSlashPosStart != std::string::npos && lastSlashPosEnd != std::string::npos) {
          if (path.beginpoint.substr(0, lastSlashPosStart) == path.endpoint.substr(0, lastSlashPosEnd)) {
            if (beginpoint_line_number + 1 == endpoint_line_number) skip_path = true;
            // std::cout << "Beginpoint and endpoint belong to the same instance." << std::endl;
          }
      } else {
          std::cout << "Error, not skip but not have /. Beginpoint: " << path.beginpoint 
              << ", Endpoint: " << path.endpoint << std::endl;
          std::cout << block << std::endl;
          exit(0);
      }
    }
    
    return path;
}

void updateHedgeSlacks(const std::vector<TimingPath>& paths, const std::unordered_map<std::string, size_t>& vertex_name_to_id, HyperGraph<Multilevel>& graph, std::vector<std::vector<double>>& hedge_slacks, std::vector<std::vector<double>>& hedge_avaliable_delay) {
    for (const auto& path : paths) {
        size_t lastStart = path.beginpoint.rfind('/');
        auto begin_inst = path.beginpoint.substr(0, lastStart);

        auto it = vertex_name_to_id.find(begin_inst);
        size_t vertex_id;
        if (it != vertex_name_to_id.end()) {
            vertex_id = it->second;
            // std::cout << "The ID for vertex '" << begin_inst << "' is: " << vertex_id << std::endl;
        } else {
            std::cout << "Vertex '" << begin_inst << "' not found." << std::endl;
            exit(0);
        }
        auto& vertex = graph.vertex_at(vertex_id);
        bool pinFound = false;
        for (auto it = vertex.ebegin(); it != vertex.eend(); ++it) {
            auto& pin = (*it).property();
            auto pin_name = vertex.property()->get_name() + "/" + processBrackets(pin->get_name());
            // std::cout << "Constructed pin name: " << pin_name << std::endl;
            if (path.beginpoint == pin_name) {
                pinFound = true;
                bool first_pin = true;
                std::weak_ptr<HyperGraph<Multilevel>::Vertex> temp_vertex;
                for (const auto& subPath : path.sub_paths) {
                    if (first_pin) {
                        first_pin = false;
                        auto& currentHedge = (*it).getHedge();
                        hedge_slacks[currentHedge.lock()->pos()].push_back(path.slack_time);
                        hedge_avaliable_delay[currentHedge.lock()->pos()].push_back(path.slack_time/(path.path_depth-1));
                        // currentHedge.lock()->property()->set_net_weight(2.0);
                        bool candidate_pin_found = false;
                        for (auto edge_it = currentHedge.lock()->ebegin(); edge_it != currentHedge.lock()->eend(); ++edge_it) {
                            auto& candidate_pin = (*edge_it).property();
                            auto& candidate_vertex = (*edge_it).getVertex();
                            auto candidate_pin_name = candidate_vertex.lock()->property()->get_name() + "/" + processBrackets(candidate_pin->get_name());
                            if (subPath.inputPin == candidate_pin_name) {
                                candidate_pin_found = true;
                                temp_vertex = candidate_vertex;
                                break;
                            }
                        }
                        if (!candidate_pin_found) {
                            std::cout << "Pin '" << subPath.inputPin << "' not found." << std::endl;
                            exit(0);
                        }
                    } else {
                        auto vertex_ptr = temp_vertex.lock();
                        std::weak_ptr<HyperGraph<Multilevel>::Hedge> currentHedge;
                        for (auto edge_it = vertex_ptr->ebegin(); edge_it != vertex_ptr->eend(); ++edge_it) {
                            auto& candidate_pin = (*edge_it).property();
                            auto& candidate_vertex = (*edge_it).getVertex();
                            auto candidate_pin_name = candidate_vertex.lock()->property()->get_name() + "/" + processBrackets(candidate_pin->get_name());
                            if (subPath.outputPin == candidate_pin_name) {
                                currentHedge = (*edge_it).getHedge();
                                hedge_slacks[currentHedge.lock()->pos()].push_back(path.slack_time);
                                hedge_avaliable_delay[currentHedge.lock()->pos()].push_back(path.slack_time/(path.path_depth-1));
                                // currentHedge.lock()->property()->set_net_weight(2.0);
                                break;
                            }
                        }
                        bool candidate_pin_found = false;
                        for (auto edge_it = currentHedge.lock()->ebegin(); edge_it != currentHedge.lock()->eend(); ++edge_it) {
                            auto& candidate_pin = (*edge_it).property();
                            auto& candidate_vertex = (*edge_it).getVertex();
                            auto candidate_pin_name = candidate_vertex.lock()->property()->get_name() + "/" + processBrackets(candidate_pin->get_name());
                            if (subPath.inputPin == candidate_pin_name) {
                                candidate_pin_found = true;
                                temp_vertex = candidate_vertex;
                                break;
                            }
                        }
                        if (!candidate_pin_found) {
                            std::cout << "Pin '" << subPath.inputPin << "' not found." << std::endl;
                            exit(0);
                        }
                    }
                }
                break;
            }
        }
        if (!pinFound) {
            std::cout << "Pin '" << path.beginpoint << "' not found." << std::endl;
            exit(0);
        }
    }
    // std::unordered_map<size_t, size_t> length_counts;

	// for (const auto& slacks : hedge_slacks) {
	// 		size_t length = slacks.size();
	// 		++length_counts[length];
	// }

	// std::vector<std::pair<size_t, size_t>> sorted_counts(length_counts.begin(), length_counts.end());

	// std::sort(sorted_counts.begin(), sorted_counts.end(), [](const std::pair<size_t, size_t>& a, const std::pair<size_t, size_t>& b) {
	// 		return a.first < b.first;
	// });

	// for (const auto& pair : sorted_counts) {
	// 		std::cout << "Length " << pair.first << " has " << pair.second << " occurrence(s)." << std::endl;
	// }
}

void processCriticalNets(HyperGraph<Multilevel>& graph, std::vector<std::vector<double>>& hedge_slacks, double extra_delay, double clock_period, std::unordered_set<std::string>& critical_nets_name) {
    for (size_t i = 0; i < graph.heSize(); ++i) {
        auto& hedge = graph.hyper_edge_at(i);
        double total_net_cost = 0;
        double worst_net_cost = 0;
        double worst_slack = 999;
        if (hedge_slacks[hedge.pos()].empty()) {
            continue;
        }
        for (auto slack: hedge_slacks[hedge.pos()]) {
            if (slack < extra_delay) {
                // total_net_cost += pow((1 - (slack - extra_delay)/clock_period), 2);
                total_net_cost += std::max(0.0, (1 - (slack - extra_delay) / clock_period));
                critical_nets_name.insert(hedge.property()->get_name());
            }
            if (slack < worst_slack) worst_slack = slack;
        }
        // worst_net_cost = pow((1 - (worst_slack - extra_delay)/clock_period), 2);
        worst_net_cost = exp(5*std::max(0.0, (extra_delay - worst_slack) / clock_period));
        if (worst_slack < extra_delay) hedge.property()->set_net_weight(total_net_cost + worst_net_cost);
    }
}

void processNonCriticalNets(HyperGraph<Multilevel>& graph, std::vector<std::vector<double>>& hedge_available_delay, double extra_delay, std::unordered_set<std::string>& non_critical_nets_name) {
    for (size_t i = 0; i < graph.heSize(); ++i) {
        auto& hedge = graph.hyper_edge_at(i);
        double worst_delay = 999;
        if (hedge_available_delay[hedge.pos()].empty()) {
            continue;
        }
        for (auto delay: hedge_available_delay[hedge.pos()]) {
            if (delay < worst_delay) worst_delay = delay;
        }
        if (worst_delay > (extra_delay + 0.1)) {
            non_critical_nets_name.insert(hedge.property()->get_name());
            double cost = 1 - std::min(0.2, 0.1 * worst_delay);
            hedge.property()->set_net_weight(cost);
        }
    }
}

void calculateAndPrintHedgeWeights(HyperGraph<Multilevel>& graph) {
    double total_net_weight = 0.0;
    size_t hedge_count = 0;

    for (size_t i = 0; i < graph.heSize(); ++i) {
        auto& hedge = graph.hyper_edge_at(i);
        total_net_weight += hedge.property()->get_net_weight();
        // std::cout<<"hedge "<<i<<" net_weight "<<hedge.property()->get_net_weight()<<std::endl;
        ++hedge_count;
    }

    if (hedge_count > 0) {
        double average_net_weight = total_net_weight / hedge_count;
        std::cout << "Total net weight of all hedges: " << total_net_weight << std::endl;
        std::cout << "Average net weight of all hedges: " << average_net_weight << std::endl;
        std::cout << "Hedge count: " << hedge_count << std::endl;
    } else {
        std::cout << "No hedges found." << std::endl;
    }
}

void timingDrivenNetWeight(HyperGraph<Multilevel>& graph, std::unordered_set<std::string>& critical_nets_name, std::unordered_set<std::string>& non_critical_nets_name) {
    std::string file_path = "/data/project_share/benchmark/aimp/input_timing_report/ariane_preCTS_reg2reg.tarpt";

    auto paths = parseTimingReport(file_path);

    // auto& graph = root().netlist();
    std::unordered_map<std::string, size_t> vertex_name_to_id = mapVertexNamesToIds(graph);

    std::vector<std::vector<double>> hedge_slacks(graph.heSize());
    std::vector<std::vector<double>> hedge_avaliable_delay(graph.heSize());
    updateHedgeSlacks(paths, vertex_name_to_id, graph, hedge_slacks, hedge_avaliable_delay);

    double extra_delay = 0.2;
    double clock_period = 1.25;

    processCriticalNets(graph, hedge_slacks, extra_delay, clock_period, critical_nets_name);
    processNonCriticalNets(graph, hedge_avaliable_delay, extra_delay, non_critical_nets_name);
}

} // namespace imp   