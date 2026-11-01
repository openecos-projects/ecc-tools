// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
#include "py_feature.h"

#include "feature_manager.h"

namespace python_interface {

bool feature_summary(const std::filesystem::path& path)
{
  return featureInst->save_summary(path.string());
}

bool feature_tool(const std::filesystem::path& path, const std::string& step)
{
  return featureInst->save_tools(path.string(), step);
}

bool feature_eval_map(const std::filesystem::path& path, const int& bin_cnt_x, const int& bin_cnt_y)
{
  return featureInst->save_eval_map(path.string(), bin_cnt_x, bin_cnt_y);
}

bool feature_net_eval(const std::filesystem::path& path)
{
  return featureInst->save_net_eval(path.string());
}

bool feature_route(const std::filesystem::path& path)
{
  return featureInst->save_route_data(path.string());
}

bool feature_route_read(const std::filesystem::path& path)
{
  return featureInst->read_route_data(path.string());
}

bool feature_macro_drc(const std::filesystem::path& path, const std::filesystem::path& drc_path)
{
  return featureInst->feature_macro_drc(path.string(), drc_path.string());
}

bool feature_eval_summary(const std::filesystem::path& path, int32_t grid_size)
{
  return featureInst->save_eval_summary(path.string(), grid_size);
}

bool feature_eval_union(const std::string& jsonl_path, const std::string& csv_path, int32_t grid_size)
{
  return featureInst->save_eval_union(jsonl_path, csv_path, grid_size);
}

bool feature_pl_eval(const std::filesystem::path& json_path, int32_t grid_size)
{
  return featureInst->save_pl_eval(json_path.string(), grid_size);
}

bool feature_cts_eval(const std::filesystem::path& json_path, int32_t grid_size)
{
  return featureInst->save_cts_eval(json_path.string(), grid_size);
}

bool feature_timing_eval_summary(const std::filesystem::path& path)
{
  return featureInst->save_timing_eval_summary(path.string());
}

bool feature_cong_map(const std::string& step, const std::filesystem::path& dir)
{
  return featureInst->save_cong_map(step, dir.string());
}

}  // namespace python_interface