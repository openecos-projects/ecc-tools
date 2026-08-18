#include "file_zh.h"
#include "ZHInterface.hpp"

#include <iomanip>
#include <iostream>
#include <map>
#include <vector>

namespace iplf {

bool FileZHManager::readFile()
{
  return false;
}

bool FileZHManager::saveFileData()
{
  return saveJson();
}

bool FileZHManager::saveJson()
{
  auto path = get_data_path();
  std::string tail_str = path.substr(path.length() - 4);
  if (tail_str != "json") {
    return false;
  }
  std::cout << std::endl << "Begin save feature json, path = " << path << std::endl;

  json ant_json;
  ant_json["file_path"] = path;

  int total = 0;
  json json_distribution;

  const auto& violations = ZHI.getAntennaViolations();
  std::map<std::string, std::map<std::string, std::vector<izh::ZHInterface::AntennaViolation>>> detail_rule_map;

  for (const auto& v : violations) {
    detail_rule_map[v.type][v.layer_name].push_back(v);
  }

  for (auto& [type, ant_list_map] : detail_rule_map) {
    json json_rule;

    int ant_list_num = 0;
    for (auto& [layer_name, ant_list] : ant_list_map) {
      ant_list_num += ant_list.size();
    }

    json_rule["number"] = ant_list_num;
    json_rule["layers"] = {};

    total += ant_list_num;

    for (auto& [layer_name, ant_list] : ant_list_map) {
      json json_layer;
      json_layer["number"] = ant_list.size();
      json json_list = json::array();

      for (auto& v : ant_list) {
        json json_ant;
        json_ant["net"] = json::array();
        json_ant["net"].push_back(v.net_name);

        json_ant["inst"] = json::array();

        json_ant["llx"] = v.lx;
        json_ant["lly"] = v.ly;
        json_ant["urx"] = v.hx;
        json_ant["ury"] = v.hy;
        json_ant["ratio"] = v.ratio;
        json_ant["threshold"] = v.threshold;

        json_list.push_back(json_ant);
      }

      json_layer["list"] = json_list;
      json_rule["layers"][layer_name] = json_layer;
    }

    json_distribution[type] = json_rule;
  }

  ant_json["antenna"]["number"] = total;
  ant_json["antenna"]["distribution"] = json_distribution;

  std::ofstream file_stream(path);
  file_stream << std::setw(4) << ant_json;
  file_stream.close();

  std::cout << std::endl << "Save feature json success, path = " << path << " total violation : " << total << std::endl;
  return true;
}

}  // namespace iplf
