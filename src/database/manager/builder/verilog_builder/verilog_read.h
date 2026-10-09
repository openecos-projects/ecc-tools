// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <string>

namespace idb {
class IdbDefService;
// Compatibility boundary for Builder. Parsing and elaboration have no database or logger dependency.
class VerilogRead
{
 public:
  explicit VerilogRead(IdbDefService* service) : _def_service(service) {}
  IdbDefService* get_service() const { return _def_service; }
  bool createDb(std::string file, std::string top_module_name);
  bool createDbAutoTop(std::string file);

 private:
  IdbDefService* _def_service;
};
}  // namespace idb
