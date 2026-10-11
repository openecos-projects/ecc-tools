// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once
#include <set>
#include <string>
namespace idb {
class IdbDesign;
class VerilogWriter
{
 public:
  // The legacy spacing flag is retained for callers; Verilog escaped identifiers
  // always require terminating whitespace regardless of that flag.
  VerilogWriter(const char* file_name, const std::set<std::string>& exclude_cell_names, IdbDesign& design,
                bool is_add_space_for_escape_name);
  VerilogWriter(const VerilogWriter&) = delete;
  VerilogWriter& operator=(const VerilogWriter&) = delete;
  void writeModule();

 private:
  std::string _file_name;
  std::set<std::string> _exclude_cell_names;
  IdbDesign& _design;
};
}  // namespace idb
