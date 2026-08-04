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
/**
 * @project		iDB
 * @file		verilog_read.cpp
 * @author		Yell
 * @date		17/11/2021
 * @version		0.1
* @description


        There is a verilog builder to build data structure from .v file.
 *
 */

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// reference code
/*
void testExpandInstance(idb::NetlistReader* nr, string module_name) {
  idb::NetlistModule* refModule = nr->getModuleByName(module_name);

  vector<idb::NetlistInstance*>::iterator instanceIter;
  for (instanceIter = refModule->getInstanceList()->begin(); instanceIter !=
refModule->getInstanceList()->end();
       ++instanceIter) {
    if ((*instanceIter)->isStandCell() == false) {
      idb::NetlistInstance* expandedInstance =
(*instanceIter)->getExpandViewOfInstanceInBit();

      cout << " Before Expand: " << endl;
      (*instanceIter)->Visit();
      cout << " After Expand: " << endl;
      expandedInstance->Visit();

      //            break;
    }
  }
}

void testMakeSingleModule(idb::NetlistReader* nr, string topModuleName) {
  idb::NetlistModule* topModule = nr->makeSingleModule(topModuleName);

  string testStr = "TREE";

  if (testStr == "FLAT") {
    topModule->print();
    // delete nr;
  } else if (testStr == "TREE") {
    nr->printNetlist();
    // delete topModule;
  }
}
*/
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "verilog_read.h"

#include <array>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <regex>
#include <string_view>
#include <vector>
#include <unistd.h>
#include <zlib.h>

#include "IdbDesign.h"
#include "utility/logger/Logger.hpp"

namespace idb {

namespace {

bool writeAll(int fd, const char* data, size_t size)
{
  size_t written_size = 0;
  while (written_size < size) {
    ssize_t result = write(fd, data + written_size, size - written_size);
    if (result < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (result == 0) {
      return false;
    }
    written_size += static_cast<size_t>(result);
  }

  return true;
}

std::optional<std::pair<int, std::string>> createTempVerilogFile()
{
  std::filesystem::path temp_path = std::filesystem::temp_directory_path() / "verilog_read_XXXXXX.v";
  std::string temp_path_template = temp_path.string();
  std::vector<char> temp_path_buffer(temp_path_template.begin(), temp_path_template.end());
  temp_path_buffer.push_back('\0');

  int fd = mkstemps(temp_path_buffer.data(), 2);
  if (fd < 0) {
    return std::nullopt;
  }

  return std::make_pair(fd, std::string(temp_path_buffer.data()));
}

std::optional<std::string> decompressGzipVerilog(const std::string& gzip_file)
{
  auto temp_file = createTempVerilogFile();
  if (!temp_file) {
    ECCLOG.warn(ecc::Loc::current(), "Create temporary verilog file failed for ", gzip_file);
    return std::nullopt;
  }

  auto [temp_fd, temp_path] = *temp_file;
  gzFile gzip_stream = gzopen(gzip_file.c_str(), "rb");
  if (gzip_stream == nullptr) {
    close(temp_fd);
    std::filesystem::remove(temp_path);
    ECCLOG.warn(ecc::Loc::current(), "Open gzip verilog file failed: ", gzip_file);
    return std::nullopt;
  }

  bool success = true;
  std::array<char, 64 * 1024> buffer;
  int read_size = 0;
  while ((read_size = gzread(gzip_stream, buffer.data(), static_cast<unsigned int>(buffer.size()))) > 0) {
    if (!writeAll(temp_fd, buffer.data(), static_cast<size_t>(read_size))) {
      success = false;
      ECCLOG.warn(ecc::Loc::current(), "Write temporary verilog file failed: ", temp_path);
      break;
    }
  }

  if (read_size < 0) {
    success = false;
    ECCLOG.warn(ecc::Loc::current(), "Read gzip verilog file failed: ", gzip_file);
  }

  if (gzclose(gzip_stream) != Z_OK) {
    success = false;
    ECCLOG.warn(ecc::Loc::current(), "Close gzip verilog file failed: ", gzip_file);
  }

  if (close(temp_fd) != 0) {
    success = false;
    ECCLOG.warn(ecc::Loc::current(), "Close temporary verilog file failed: ", temp_path);
  }

  if (!success) {
    std::filesystem::remove(temp_path);
    return std::nullopt;
  }

  return temp_path;
}

std::pair<std::string, std::optional<int>> splitBusName(const char* name)
{
  std::string_view name_view(name);
  if (!name_view.ends_with("]")) {
    return {std::string(name_view), std::nullopt};
  }

  size_t left_bracket_idx = name_view.find('[');
  size_t right_bracket_idx = name_view.find(']', left_bracket_idx);
  if (left_bracket_idx == std::string_view::npos || right_bracket_idx == std::string_view::npos) {
    return {std::string(name_view), std::nullopt};
  }

  int index = std::atoi(std::string(name_view.substr(left_bracket_idx + 1, right_bracket_idx - left_bracket_idx - 1)).c_str());
  return {std::string(name_view.substr(0, left_bracket_idx)), index};
}

std::string makeIndexedName(std::string_view name, int index)
{
  return std::string(name) + "[" + std::to_string(index) + "]";
}

std::string normalizeEscapedName(std::string name)
{
  while (!name.empty() && name.back() == '\n') {
    name.pop_back();
  }
  std::erase(name, '\\');
  std::erase(name, ' ');
  return name;
}

class ScopedReadableVerilogFile
{
 public:
  explicit ScopedReadableVerilogFile(const std::string& file)
      : _read_file(file)
  {
    if (file.find(".gz") != std::string::npos) {
      auto temp_file = decompressGzipVerilog(file);
      if (temp_file) {
        _temp_file = *temp_file;
        _read_file = _temp_file;
      } else {
        _read_file.clear();
      }
    }
  }

  ~ScopedReadableVerilogFile()
  {
    if (!_temp_file.empty()) {
      std::error_code error_code;
      std::filesystem::remove(_temp_file, error_code);
    }
  }

  bool isValid() const { return !_read_file.empty(); }
  const std::string& getReadFile() const { return _read_file; }

 private:
  std::string _read_file;
  std::string _temp_file;
};

}  // namespace

VerilogRead::VerilogRead(IdbDefService* def_service)
{
  _def_service = def_service;
}

VerilogRead::~VerilogRead()
{
}

bool VerilogRead::createDb(std::string file, std::string top_module_name)
{
  ScopedReadableVerilogFile verilog_file(file);
  if (!verilog_file.isValid()) {
    return false;
  }

  if (!_verilog_reader) {
    _verilog_reader = new idb::VerilogReader();
  }
  if (!_verilog_reader->readVerilog(verilog_file.getReadFile().c_str())) {
    return false;
  }
  _verilog_reader->flattenModule(top_module_name.c_str());
  _top_module = _verilog_reader->get_top_module();

  if (_top_module == nullptr) {
    return false;
  }

  IdbDesign* idb_design = _def_service->get_design();
  idb_design->set_design_name(_top_module->module_name);

  // string testStr = "FLAT";

  // if (testStr == "FLAT") {
  //   _top_module->print();
  // } else if (testStr == "TREE") {
  //   _verilog_read->printNetlist();
  // }

  build_pins();
  build_nets();
  build_components();
  build_assign();

  post_process_float_io_pins();

  return true;
}

bool VerilogRead::createDbAutoTop(std::string file)
{
  ScopedReadableVerilogFile verilog_file(file);
  if (!verilog_file.isValid()) {
    return false;
  }

  if (!_verilog_reader) {
    _verilog_reader = new idb::VerilogReader();
  }
  if (!_verilog_reader->readVerilog(verilog_file.getReadFile().c_str())) {
    return false;
  }

  // auto set top module
  if (!_verilog_reader->autoTopModule()) {
    ECCLOG.warn(ecc::Loc::current(), "auto top module is wrong!");
    return false;
  }
  _top_module = _verilog_reader->get_top_module();

  if (_top_module == nullptr) {
    return false;
  }

  IdbDesign* idb_design = _def_service->get_design();
  idb_design->set_design_name(_top_module->module_name);

  build_pins();
  build_nets();
  build_assign();
  build_components();

  return true;
}

/**
 * @brief convert netlist port_direction to idb port_direction.
 *
 * @param port_direction
 * @return IdbConnectDirection
 */
IdbConnectDirection VerilogRead::netlistToIdb(DclType port_direction) const
{
  if (port_direction == DclType::KInput) {
    return IdbConnectDirection::kInput;
  } else if (port_direction == DclType::KOutput) {
    return IdbConnectDirection::kOutput;
  } else if (port_direction == DclType::KInout) {
    return IdbConnectDirection::kInOut;
  } else {
    ECCLOG.warn(ecc::Loc::current(), "not support.");
    return IdbConnectDirection::kNone;
  }
}

/**
 * @brief build pins.
 *
 * @return int32_t
 */
int32_t VerilogRead::build_pins()
{
  IdbDesign* idb_design = _def_service->get_design();

  auto& top_module_stmts = _top_module->module_stmts;

  IdbPins* idb_io_pin_list = idb_design->get_io_pin_list();
  if (!idb_io_pin_list) {
    idb_io_pin_list = new IdbPins();
    idb_design->set_io_pin_list(idb_io_pin_list);
  }
  auto replace_str = [](const string& str, const string& replace_str, const string& new_str) {
    std::regex re(replace_str);
    return std::regex_replace(str, re, new_str);
  };
  // create pin.
  auto dcl_process = [idb_design, &replace_str, this](DclType dcl_type, const char* dcl_name) -> IdbPin* {
    if (dcl_type == DclType::KInput || dcl_type == DclType::KOutput || dcl_type == DclType::KInout) {
      std::string pin_name = dcl_name;
      if (std::string::npos != pin_name.find('\\')) {
        pin_name = replace_str(pin_name, R"(\\)", "");
        pin_name = replace_str(pin_name, R"( )", "");
      }

      IdbPin* idb_io_pin = idb_design->createOrFindIoPin(pin_name);
      if (idb_io_pin == nullptr) {
        return nullptr;
      }
      if (idb_io_pin->get_term() == nullptr) {
        idb_io_pin->set_term();
      }
      idb_io_pin->get_term()->set_name(pin_name);
      idb_io_pin->get_term()->set_direction(netlistToIdb(dcl_type));
      idb_io_pin->get_term()->set_type(IdbConnectType::kSignal);
      idb_io_pin->set_as_io();
      return idb_io_pin;
    }

    return nullptr;
  };

  // process declare statement.
  auto process_dcl_stmt = [&dcl_process, idb_design](auto* verilog_dcl) {
    auto dcl_type = verilog_dcl->dcl_type;
    const auto* dcl_name = verilog_dcl->dcl_name;
    auto dcl_range = verilog_dcl->range;

    if (!dcl_range.has_value) {
      dcl_process(dcl_type, dcl_name);
    } else {
      auto bus_range = std::make_pair(dcl_range.start, dcl_range.end);
      for (int index = bus_range.second; index <= bus_range.first; index++) {
        // for port or wire bus, we split to one bye one port.
        std::string one_name = makeIndexedName(dcl_name, index);
        auto io_pin = dcl_process(dcl_type, one_name.c_str());
        if (io_pin) {
          if (index == bus_range.second) {
            IdbBus io_pin_bus(dcl_name, bus_range.first, bus_range.second);
            io_pin_bus.set_type(IdbBus::kBusType::kBusIo);
            io_pin_bus.addPin(io_pin);

            idb_design->get_bus_list()->addBusObject(std::move(io_pin_bus));

          } else {
            std::string bus_name = dcl_name;
            auto found_pin_bus = idb_design->get_bus_list()->findBus(bus_name);
            assert(found_pin_bus);
            (*found_pin_bus).get().addPin(io_pin);
          }
        }
      }
    }
  };

  int num = 0;

  void* stmt;
  FOREACH_VERILOG_VEC_ELEM(&top_module_stmts, void, stmt)
  {
    if (verilog_is_dcls_stmt(stmt)) {
      ParsedVerilogDcls* verilog_dcls_struct = verilog_convert_dcls(stmt);
      auto verilog_dcls = verilog_dcls_struct->verilog_dcls;
      void* verilog_dcl = nullptr;
      FOREACH_VERILOG_VEC_ELEM(&verilog_dcls, void, verilog_dcl)
      {
        process_dcl_stmt(verilog_convert_dcl(verilog_dcl));
        num++;
        if (num % 1000 == 0) {
          ECCLOG.info(ecc::Loc::current(), "Processed ", num, " pins...");
        }
      }
    }
  }
  return kVerilogSuccess;
}

/**
 * @brief build nets.
 *
 * @return int32_t
 */
int32_t VerilogRead::build_nets()
{
  IdbDesign* idb_design = _def_service->get_design();

  auto& top_module_stmts = _top_module->module_stmts;
  IdbNetList* idb_net_list = idb_design->get_net_list();
  if (!idb_net_list) {
    idb_net_list = new IdbNetList;
    idb_design->set_net_list(idb_net_list);
  }

  auto replace_str = [](const string& str, const string& replace_str, const string& new_str) {
    std::regex re(replace_str);
    return std::regex_replace(str, re, new_str);
  };

  auto add_wire_net = [replace_str, idb_design](std::string net_name) -> IdbNet* {
    if (std::string::npos != net_name.find('\\')) {
      net_name = replace_str(net_name, R"(\\)", "");
      net_name = replace_str(net_name, R"( )", "");
    }

    IdbNet* idb_net = idb_design->createOrFindNet(net_name, IdbConnectType::kSignal);
    if (auto* io_pin = idb_design->get_io_pin_list()->find_pin(net_name); io_pin != nullptr) {
      idb_design->connectPinToNet(io_pin, idb_net);
    }

    return idb_net;
  };

  auto process_dcl_stmt = [&add_wire_net, &replace_str, idb_design](auto* verilog_dcl) {
    auto dcl_type = verilog_dcl->dcl_type;
    const auto* dcl_name = verilog_dcl->dcl_name;
    if (dcl_type == DclType::KWire) {
      std::string net_name = dcl_name;

      if (std::string::npos != net_name.find('\\')) {
        net_name = replace_str(net_name, R"(\\)", "");
        net_name = replace_str(net_name, R"( )", "");
      }

      auto dcl_range = verilog_dcl->range;

      if (!dcl_range.has_value) {
        auto* idb_net = add_wire_net(net_name);

        if (std::string_view(dcl_name).find("\\[") == std::string_view::npos) {
          auto [bus_name, bus_index] = splitBusName(net_name.c_str());
          if (bus_index) {
            if (auto found_pin_bus = idb_design->get_bus_list()->findBus(bus_name); !found_pin_bus) {
              IdbBus io_pin_bus(bus_name, bus_index.value(), 0);
              io_pin_bus.set_type(IdbBus::kBusType::kBusNet);
              io_pin_bus.addNet(idb_net);

              idb_design->get_bus_list()->addBusObject(std::move(io_pin_bus));
            } else {
              (*found_pin_bus).get().updateRange(bus_index.value());
              (*found_pin_bus).get().addNet(idb_net);
            }
          }
        }
      } else {
        auto bus_range = std::make_pair(dcl_range.start, dcl_range.end);
        for (int index = bus_range.second; index <= bus_range.first; index++) {
          // for port or wire bus, we split to one bye one port.
          std::string one_name = makeIndexedName(net_name, index);
          auto idb_net = add_wire_net(one_name);
          if (index == bus_range.second) {
            IdbBus io_pin_bus(net_name, bus_range.first, bus_range.second);
            io_pin_bus.set_type(IdbBus::kBusType::kBusNet);
            io_pin_bus.addNet(idb_net);

            idb_design->get_bus_list()->addBusObject(std::move(io_pin_bus));

          } else {
            std::string bus_name = net_name;
            auto found_pin_bus = idb_design->get_bus_list()->findBus(bus_name);
            assert(found_pin_bus);
            (*found_pin_bus).get().addNet(idb_net);
          }
        }
      }
    }
  };

  int num = 0;

  void* stmt;
  FOREACH_VERILOG_VEC_ELEM(&top_module_stmts, void, stmt)
  {
    if (verilog_is_dcls_stmt(stmt)) {
      ParsedVerilogDcls* verilog_dcls_struct = verilog_convert_dcls(stmt);
      auto verilog_dcls = verilog_dcls_struct->verilog_dcls;
      void* verilog_dcl = nullptr;
      FOREACH_VERILOG_VEC_ELEM(&verilog_dcls, void, verilog_dcl)
      {
        process_dcl_stmt(verilog_convert_dcl(verilog_dcl));
        num++;
        if (num % 1000 == 0) {
          ECCLOG.info(ecc::Loc::current(), "Processed ", num, " nets...");
        }
      }
    }
  }

  return kVerilogSuccess;
}

/**
 * @brief build assign.
 *
 * @return int32_t
 */
int32_t VerilogRead::build_assign()
{
  IdbDesign* idb_design = _def_service->get_design();
  IdbPins* idb_io_pin_list = idb_design->get_io_pin_list();
  IdbNetList* idb_net_list = idb_design->get_net_list();

  auto& top_module_stmts = _top_module->module_stmts;
  void* stmt;

  // record the merge nets.
  std::map<std::string, IdbNet*> remove_to_merge_nets;

  auto process_one_to_one_net
      = [idb_design, idb_io_pin_list, idb_net_list, &remove_to_merge_nets](std::string left_net_name, std::string right_net_name) {
          left_net_name = normalizeEscapedName(std::move(left_net_name));
          right_net_name = normalizeEscapedName(std::move(right_net_name));

          // according to assign's lhs/rhs to connect port to net.

          auto* the_left_idb_net = idb_net_list->find_net(left_net_name);
          if (!the_left_idb_net && remove_to_merge_nets.contains(left_net_name)) {
            the_left_idb_net = remove_to_merge_nets[left_net_name];
          }

          auto* the_right_idb_net = idb_net_list->find_net(right_net_name);
          if (!the_right_idb_net && remove_to_merge_nets.contains(right_net_name)) {
            the_right_idb_net = remove_to_merge_nets[right_net_name];
          }

          auto* the_left_io_pin = idb_io_pin_list->find_pin(left_net_name.c_str());
          auto* the_right_io_pin = idb_io_pin_list->find_pin(right_net_name.c_str());

          // A top-level port can have the same name as an internal net.  Preserve
          // port-to-net assigns before considering a net-to-net merge.
          if (the_left_io_pin && the_left_io_pin->is_io_pin() && the_right_idb_net) {
            // assign output_port = net;
            idb_design->connectPinToNet(the_left_io_pin, the_right_idb_net);
          } else if (the_right_io_pin && the_right_io_pin->is_io_pin() && the_left_idb_net) {
            // assign net = input_port;
            idb_design->connectPinToNet(the_right_io_pin, the_left_idb_net);
          } else if (the_left_idb_net && the_right_idb_net && the_left_idb_net != the_right_idb_net) {
            // assign net = net, need merge two net to one net.

            // ECCLOG.info(ecc::Loc::current(), "Merge ", left_net_name, " = ", right_net_name);

            assert(the_left_idb_net != the_right_idb_net);
            // the remove map to merge net maybe removed, need update the new net.
            for (auto& [remove_net_name, merge_idb_net] : remove_to_merge_nets) {
              if (merge_idb_net == the_left_idb_net) {
                remove_to_merge_nets[remove_net_name] = the_right_idb_net;
              }
            }

            if (idb_design->mergeNetInto(the_right_idb_net->get_net_name(), the_left_idb_net->get_net_name())) {
              remove_to_merge_nets[left_net_name] = the_right_idb_net;
            }

          } else if (the_left_idb_net) {
            if (the_right_io_pin && the_right_io_pin->is_io_pin()) {
              // assign net = input_port;
              idb_design->connectPinToNet(the_right_io_pin, the_left_idb_net);
            } else {
              ECCLOG.warn(ecc::Loc::current(), "assign ", left_net_name, " = ", right_net_name, " is not processed.");
              bool has_b0 = (right_net_name.find("1'b0") != std::string::npos);
              bool has_b1 = (right_net_name.find("1'b1") != std::string::npos);
              if (has_b0 || has_b1) {
                ECCLOG.warn(ecc::Loc::current(), "constant net should connect to tie cell.");
              }
            }
          } else if (the_right_idb_net) {
            if (the_left_io_pin && the_left_io_pin->is_io_pin()) {
               // assign output_port = net;
              idb_design->connectPinToNet(the_left_io_pin, the_right_idb_net);
            } else {
              ECCLOG.warn(ecc::Loc::current(), "assign ", left_net_name, " = ", right_net_name, " is not processed.");
            }
          } else if (!the_left_idb_net && !the_right_idb_net && the_right_io_pin) {
            // assign output_port = input_port;
            IdbNet* idb_net = idb_design->createOrFindNet(right_net_name, IdbConnectType::kSignal);
            if (the_left_io_pin && the_left_io_pin->is_io_pin()) {
              idb_design->connectPinToNet(the_left_io_pin, idb_net);
            }
            if (the_right_io_pin->is_io_pin()) {
              idb_design->connectPinToNet(the_right_io_pin, idb_net);
            }
          } else if (!the_left_idb_net && !the_right_idb_net && !the_right_io_pin) {
            // assign output_port = 1'b0(1'b1);
            IdbNet* idb_net = idb_design->createOrFindNet(left_net_name, IdbConnectType::kSignal);
            if (the_left_io_pin && the_left_io_pin->is_io_pin()) {
              idb_design->connectPinToNet(the_left_io_pin, idb_net);
            } else {
              ECCLOG.warn(ecc::Loc::current(), "assign ", left_net_name, " = ", right_net_name, " is not processed.");
            }
          } else {
            ECCLOG.warn(ecc::Loc::current(), "assign ", left_net_name, " = ", right_net_name, " is not processed.");
          }
        };

  FOREACH_VERILOG_VEC_ELEM(&top_module_stmts, void, stmt)
  {
    if (verilog_is_module_assign_stmt(stmt)) {
      ParsedVerilogAssign* verilog_assign = verilog_convert_assign(stmt);

      auto* left_net_expr = const_cast<void*>(verilog_assign->left_net_expr);
      auto* right_net_expr = const_cast<void*>(verilog_assign->right_net_expr);
      std::string left_net_name;
      std::string right_net_name;
      if (verilog_is_id_expr(left_net_expr) && verilog_is_id_expr(right_net_expr)) {
        // get left_net_name.
        auto* left_net_id = const_cast<void*>(verilog_convert_net_id_expr(left_net_expr)->verilog_id);
        if (verilog_is_id(left_net_id)) {
          left_net_name = verilog_convert_id(left_net_id)->id;
        } else if (verilog_is_bus_index_id(left_net_id)) {
          left_net_name = verilog_convert_index_id(left_net_id)->id;
        } else {
          left_net_name = verilog_convert_slice_id(left_net_id)->id;
        }
        // get right_net_name.
        auto* right_net_id = const_cast<void*>(verilog_convert_net_id_expr(right_net_expr)->verilog_id);
        if (verilog_is_id(right_net_id)) {
          right_net_name = verilog_convert_id(right_net_id)->id;
        } else if (verilog_is_bus_index_id(right_net_id)) {
          right_net_name = verilog_convert_index_id(right_net_id)->id;
        } else {
          right_net_name = verilog_convert_slice_id(right_net_id)->id;
        }

        process_one_to_one_net(left_net_name, right_net_name);
      } else if ((verilog_is_id_expr(left_net_expr) && verilog_is_concat_expr(right_net_expr))
                 || (verilog_is_concat_expr(left_net_expr) && verilog_is_id_expr(right_net_expr))) {
        auto process_id_concat_assign = [&process_one_to_one_net](auto* id_net_expr, auto* concat_net_expr, bool is_first_left) {
          std::string id_net_name;
          std::string concat_net_name;

          // assume left the not concatenation, right is concatenation. such as "assign io_out_arsize = { _41_, _41_, io_in_size };"
          auto* id_net_expr_id = const_cast<void*>(verilog_convert_net_id_expr(id_net_expr)->verilog_id);
          unsigned base_id_index = 0;
          if (verilog_is_id(id_net_expr_id)) {
            id_net_name = verilog_convert_id(id_net_expr_id)->id;
          } else if (verilog_is_bus_slice_id(id_net_expr_id)) {
            auto slice_net_id = verilog_convert_slice_id(id_net_expr_id);
            id_net_name = slice_net_id->base_id;
            base_id_index = slice_net_id->range_base;
          } else {
            ECCLOG.error(ecc::Loc::current(), "left net id should be id or bus slice id");
          }

          auto verilog_id_concat = verilog_convert_net_concat_expr(concat_net_expr)->verilog_id_concat;

          void* one_net_expr;
          FOREACH_VERILOG_VEC_ELEM(&verilog_id_concat, void, one_net_expr)
          {
            assert(verilog_is_id_expr(one_net_expr));
            auto* one_net_id = (void*) (verilog_convert_net_id_expr(one_net_expr)->verilog_id);
            if (verilog_is_id(one_net_id)) {
              std::string one_id_net_name = id_net_name + "[" + std::to_string(base_id_index) + "]";
              std::string one_concat_net_name = verilog_convert_id(one_net_id)->id;
              if (is_first_left) {
                process_one_to_one_net(one_id_net_name, one_concat_net_name);
              } else {
                process_one_to_one_net(one_concat_net_name, one_id_net_name);
              }

            } else if (verilog_is_bus_index_id(one_net_id)) {
              std::string one_id_net_name = id_net_name + "[" + std::to_string(base_id_index) + "]";
              std::string one_concat_net_name = verilog_convert_index_id(one_net_id)->id;
              if (is_first_left) {
                process_one_to_one_net(one_id_net_name, one_concat_net_name);
              } else {
                process_one_to_one_net(one_concat_net_name, one_id_net_name);
              }
            } else {
              auto right_slice_id = verilog_convert_slice_id(one_net_id);
              std::string right_net_base_name = right_slice_id->base_id;
              auto right_base_index = right_slice_id->range_base;
              while (right_base_index <= right_slice_id->range_max) {
                std::string one_id_net_name = id_net_name + "[" + std::to_string(base_id_index) + "]";
                std::string one_concat_net_name = right_net_base_name + "[" + std::to_string(right_base_index) + "]";

                if (is_first_left) {
                  process_one_to_one_net(one_id_net_name, one_concat_net_name);
                } else {
                  process_one_to_one_net(one_concat_net_name, one_id_net_name);
                }

                ++base_id_index;
                ++right_base_index;
              }
            }

            ++base_id_index;
          }
        };

        if (verilog_is_id_expr(left_net_expr) && verilog_is_concat_expr(right_net_expr)) {
          process_id_concat_assign(left_net_expr, right_net_expr, true);
        } else {
          process_id_concat_assign(right_net_expr, left_net_expr, false);
        }

      } else if (verilog_is_concat_expr(left_net_expr) && verilog_is_concat_expr(right_net_expr)) {
        std::function<std::vector<std::string>(VerilogVec&)> get_concat_net_names
            = [&get_concat_net_names](VerilogVec& verilog_id_concat) -> std::vector<std::string> {
          std::vector<std::string> concat_net_names;
          void* one_net_expr;
          FOREACH_VERILOG_VEC_ELEM(&verilog_id_concat, void, one_net_expr)
          {
            if (verilog_is_id_expr(one_net_expr)) {
              auto* one_net_id = (void*) (verilog_convert_net_id_expr(one_net_expr)->verilog_id);
              if (verilog_is_id(one_net_id)) {
                std::string one_concat_net_name = verilog_convert_id(one_net_id)->id;
                concat_net_names.emplace_back(std::move(one_concat_net_name));
              } else if (verilog_is_bus_index_id(one_net_id)) {
                std::string one_concat_net_name = verilog_convert_index_id(one_net_id)->id;
                concat_net_names.emplace_back(std::move(one_concat_net_name));
              } else {
                auto right_slice_id = verilog_convert_slice_id(one_net_id);
                std::string right_net_base_name = right_slice_id->base_id;
                auto right_base_index = right_slice_id->range_base;
                while (right_base_index <= right_slice_id->range_max) {
                  std::string one_concat_net_name = right_net_base_name + "[" + std::to_string(right_base_index) + "]";
                  concat_net_names.emplace_back(std::move(one_concat_net_name));
                  ++right_base_index;
                }
              }
            } else if (verilog_is_concat_expr(one_net_expr)) {
              auto one_net_concat_expr = verilog_convert_net_concat_expr(one_net_expr);
              auto one_net_verilog_id_concat = one_net_concat_expr->verilog_id_concat;
              auto one_concat_net_names = get_concat_net_names(one_net_verilog_id_concat);
              concat_net_names.insert(concat_net_names.end(), one_concat_net_names.begin(), one_concat_net_names.end());

            } else {
              assert(false);
            }
          }

          return concat_net_names;
        };

        auto left_concat_net_expr = verilog_convert_net_concat_expr(left_net_expr);
        auto left_concat_net_names = get_concat_net_names(left_concat_net_expr->verilog_id_concat);

        auto right_concat_net_expr = verilog_convert_net_concat_expr(right_net_expr);
        auto right_concat_net_names = get_concat_net_names(right_concat_net_expr->verilog_id_concat);

        assert(left_concat_net_names.size() == right_concat_net_names.size());

        for (size_t i = 0; i < left_concat_net_names.size(); i++) {
          // process assign net = net, which is concat net.
          std::string left_concat_net_name = left_concat_net_names[i];
          std::string right_concat_net_name = right_concat_net_names[i];

          if (left_concat_net_name == right_concat_net_name) {
            // skip same net name.
            continue;
          }

          process_one_to_one_net(left_concat_net_name, right_concat_net_name);
        }

      } else {
        ECCLOG.error(ecc::Loc::current(), "assign declaration's lhs/rhs is not VerilogNetIDExpr class.");
      }
    }
  }

  return kVerilogSuccess;
}
/**
 * @brief build components.
 *
 * @return int32_t
 */
int32_t VerilogRead::build_components()
{
  IdbDesign* idb_design = _def_service->get_design();
  IdbLayout* idb_layout = _def_service->get_layout();
  IdbCellMasterList* idb_master_list = idb_layout->get_cell_master_list();
  IdbPins* idb_io_pin_list = idb_design->get_io_pin_list();

  auto& top_module_stmts = _top_module->module_stmts;

  IdbInstanceList* idb_instance_list = idb_design->get_instance_list();
  if (!idb_instance_list) {
    idb_instance_list = new IdbInstanceList;
    idb_design->set_instance_list(idb_instance_list);
  }

  auto* idb_net_list = idb_design->get_net_list();

  auto replace_str = [](const string& str, const string& old_str, const string& new_str) {
    std::regex re(old_str);
    return std::regex_replace(str, re, new_str);
  };

  auto add_pin = [idb_net_list, replace_str, idb_io_pin_list, idb_design](const std::string& raw_name, auto* idb_pin) {
    std::string net_name = raw_name;

    // strip \\\ char.
    if (std::string::npos != raw_name.find('\\')) {
      net_name = replace_str(raw_name, R"(\\)", "");
      net_name = replace_str(net_name, R"( )", "");
    }

    auto* idb_net = idb_net_list->find_net(net_name);
    if (!idb_net) {
      // judge whether bus net name.
      auto net_bus = idb_design->get_bus_list()->findBus(net_name);
      if (!net_bus) {
        // not bus net name, create common idb net.
        idb_net = idb_design->createOrFindNet(net_name, IdbConnectType::kSignal);

        // judge whether contain bus index name, if bus name contain \\[, should
        // not treat as bus.
        if (net_name.find("\\[") == std::string::npos) {
          // is bus index net.
          auto [bus_name, bus_index] = splitBusName(net_name.c_str());
          if (bus_index) {
            if (auto found_net_bus = idb_design->get_bus_list()->findBus(bus_name); !found_net_bus) {
              // not found net bus, create it.
              IdbBus created_net_bus(bus_name, bus_index.value(), bus_index.value());
              created_net_bus.set_type(IdbBus::kBusType::kBusNet);
              created_net_bus.addNet(idb_net);

              idb_design->get_bus_list()->addBusObject(std::move(created_net_bus));
            } else {
              // exist net bus, update range.
              (*found_net_bus).get().updateRange(bus_index.value());
              (*found_net_bus).get().addNet(idb_net);
            }
          }
        }
      } else {
        // existed bus net, get one net of bus.
        std::string pin_name = idb_pin->get_pin_name();
        auto [pin_bus_name, pin_bus_index] = splitBusName(pin_name.c_str());
        if (!pin_bus_index) {
          // if net bus only exist one net, get the bus index directly.
          if ((*net_bus).get().get_left() == (*net_bus).get().get_right()) {
            pin_bus_index = (*net_bus).get().get_left();
          }
        }
        assert(pin_bus_index);
        idb_net = (*net_bus).get().getNet(pin_bus_index.value());
      }
    }

    auto* io_pin = idb_io_pin_list->find_pin(net_name);
    if (io_pin) {
      auto* net_io_pins = idb_net->get_io_pins();
      if (net_io_pins) {
        auto& net_io_pin_vec = net_io_pins->get_pin_list();

        if (net_io_pin_vec.end() == std::find(net_io_pin_vec.begin(), net_io_pin_vec.end(), io_pin)) {
          idb_design->connectPinToNet(io_pin, idb_net);
        }
      }
    }

    idb_design->connectPinToNet(idb_pin, idb_net);
  };

  /*lambda function flatten concate net, which maybe nested.*/
  std::function<void(ParsedVerilogNetConcatExpr*, std::vector<void*>&)> flatten_concat_net_expr
      = [&flatten_concat_net_expr](ParsedVerilogNetConcatExpr* net_concat_expr, std::vector<void*>& net_concat_vec) {
          auto verilog_id_concat = net_concat_expr->verilog_id_concat;

          void* verilog_id;
          FOREACH_VERILOG_VEC_ELEM(&verilog_id_concat, void, verilog_id)
          {
            if (verilog_is_concat_expr(verilog_id)) {
              flatten_concat_net_expr(verilog_convert_net_concat_expr(verilog_id), net_concat_vec);
            } else {
              net_concat_vec.push_back(verilog_id);
            }
          }
        };

  // create or found bus, add idb pin to bus.
  auto create_or_found_bus = [idb_design](std::string bus_name, IdbPin* idb_pin, std::optional<int> max_bus_bit, bool is_create) {
    if (is_create) {
      IdbBus pin_bus(bus_name, max_bus_bit.value(), 0);
      pin_bus.set_type(IdbBus::kBusType::kBusInstancePin);
      pin_bus.addPin(idb_pin);

      idb_design->get_bus_list()->addBusObject(std::move(pin_bus));
    } else {
      auto found_pin_bus = idb_design->get_bus_list()->findBus(bus_name);
      assert(found_pin_bus);
      (*found_pin_bus).get().addPin(idb_pin);
    }
  };
  int num = 0;
  void* stmt;
  FOREACH_VERILOG_VEC_ELEM(&top_module_stmts, void, stmt)
  {
    if (verilog_is_module_inst_stmt(stmt)) {
      ParsedVerilogInst* verilog_inst = verilog_convert_inst(stmt);
      std::string inst_name = verilog_inst->inst_name;

      if (std::string::npos != inst_name.find('\\')) {
        inst_name = replace_str(inst_name, R"(\\)", "");
        inst_name = replace_str(inst_name, R"( )", "");
      }

      std::string cell_master_name = verilog_inst->cell_name;

      auto* cell_master = idb_master_list->find_cell_master(cell_master_name);
      if (cell_master == nullptr) {
        ECCLOG.warn(ecc::Loc::current(), "Error : can not find cell master = ", cell_master_name);
        continue;
      }
      IdbInstance* idb_instance = idb_design->createInstance(inst_name, cell_master->get_name());
      if (idb_instance == nullptr) {
        ECCLOG.warn(ecc::Loc::current(), "Error : can not create instance = ", inst_name);
        continue;
      }

      // build instance pin connected net.
      auto& port_connections = verilog_inst->port_connections;
      void* port_connection_handle;
      FOREACH_VERILOG_VEC_ELEM(&port_connections, void, port_connection_handle)
      {
        ParsedVerilogPortRefPortConnect* port_connection = verilog_convert_port_ref_port_connect(port_connection_handle);
        // *const c_void
        void* cell_port_id = const_cast<void*>(port_connection->port_id);
        // *mut c_void
        void* net_expr = port_connection->net_expr;  // get net name

        const char* cell_port_name;
        if (verilog_is_id(cell_port_id)) {
          cell_port_name = verilog_convert_id(cell_port_id)->id;
        } else if (verilog_is_bus_index_id(cell_port_id)) {
          cell_port_name = verilog_convert_index_id(cell_port_id)->id;
        } else {
          cell_port_name = verilog_convert_slice_id(cell_port_id)->id;
        }
        if (!net_expr) {
          continue;
        }

        if (verilog_is_id_expr(net_expr) || verilog_is_constant(net_expr)) {
          // condition for common ID and constant.
          auto* idb_pin = idb_instance->get_pin(cell_port_name);
          if (!idb_pin) {
            // should be pin bus, get bus size first.
            int max_bus_bit = 1;
            std::vector<IdbPin*> bus_pins;
            for (int i = 0;; ++i) {
              std::string bus_pin_name = std::string(cell_port_name) + "[" + std::to_string(i) + "]";
              auto* idb_bus_pin = idb_instance->get_pin(bus_pin_name);

              // assert bus pin exist.
              if (i == 0) {
                assert(idb_bus_pin);
              }

              if (idb_bus_pin) {
                // the net should be net bus too, select bus index net.

                // const char* net_name = net_expr->get_verilog_id()->getBaseName();
                void* net_id = nullptr;
                if (verilog_is_id_expr(net_expr)) {
                  net_id = const_cast<void*>(verilog_convert_net_id_expr(net_expr)->verilog_id);
                } else if (verilog_is_constant(net_expr)) {
                  net_id = const_cast<void*>(verilog_convert_constant_expr(net_expr)->verilog_id);
                }
                const char* net_name = nullptr;
                if (verilog_is_id(net_id)) {
                  net_name = verilog_convert_id(net_id)->id;
                } else if (verilog_is_bus_index_id(net_id)) {
                  net_name = verilog_convert_index_id(net_id)->base_id;
                } else {
                  net_name = verilog_convert_slice_id(net_id)->base_id;
                }

                std::optional<int> net_bus_base_index;
                // if (net_expr->get_verilog_id()->isBusSliceID()) {
                //   net_bus_base_index = dynamic_cast<VerilogSliceID*>(net_expr->get_verilog_id())->get_range_base();
                // }
                if (verilog_is_bus_slice_id(net_id)) {
                  net_bus_base_index = verilog_convert_slice_id(net_id)->range_base;
                }

                std::string bus_net_name = std::string(net_name) + "[" + std::to_string(i + net_bus_base_index.value_or(0)) + "]";
                add_pin(bus_net_name, idb_bus_pin);
                bus_pins.emplace_back(idb_bus_pin);
              } else {
                // found the max bus width, break loop.
                max_bus_bit = i;
                break;
              }
            }

            std::string bus_name = inst_name + "/" + cell_port_name;
            for (int i = 0; auto* idb_bus_pin : bus_pins) {
              if (i == 0) {
                create_or_found_bus(bus_name, idb_bus_pin, max_bus_bit - 1, true);
              } else {
                create_or_found_bus(bus_name, idb_bus_pin, std::nullopt, false);
              }
              ++i;
            }

          } else {
            // exist idb pin, add to net.
            if (!verilog_is_constant(net_expr)) {
              // const char* net_name = net_expr->get_verilog_id()->getName();
              std::string generated_net_name;
              const char* net_name = nullptr;
              void* net_id = nullptr;
              if (verilog_is_id_expr(net_expr)) {
                net_id = const_cast<void*>(verilog_convert_net_id_expr(net_expr)->verilog_id);
              } else if (verilog_is_constant(net_expr)) {
                net_id = const_cast<void*>(verilog_convert_constant_expr(net_expr)->verilog_id);
              }
              if (verilog_is_id(net_id)) {
                net_name = verilog_convert_id(net_id)->id;
              } else if (verilog_is_bus_index_id(net_id)) {
                net_name = verilog_convert_index_id(net_id)->id;
              } else if (verilog_is_bus_slice_id(net_id)) {
                net_name = verilog_convert_slice_id(net_id)->id;
              } else {
                static int index = 0;
                generated_net_name = "ECC_CONST_" + std::to_string(index++);
                net_name = generated_net_name.c_str();
              }
              add_pin(net_name, idb_pin);
            }
          }

        } else {
          // condition for net concat(wire   [23:0] buf11_dout).
          auto* net_concat_expr = verilog_convert_net_concat_expr(net_expr);
          std::vector<void*> verilog_id_concat_vec;
          flatten_concat_net_expr(net_concat_expr, verilog_id_concat_vec);

          std::string bus_name = inst_name + "/" + cell_port_name;

          // found pin bus size.
          std::vector<IdbPin*> bus_pin_vec;
          for (int i = 0;; ++i) {
            std::string pin_name = makeIndexedName(cell_port_name, i);
            auto* idb_pin = idb_instance->get_pin(pin_name);
            if (!idb_pin) {
              break;
            }
            bus_pin_vec.emplace_back(idb_pin);
          }

          // fix verilog bus is only one bus signal and bus base index is not zero.
          if (bus_pin_vec.empty() && (verilog_id_concat_vec.size() == 1)) {
            auto* idb_pin = idb_instance->get_pin_by_term(cell_port_name);
            if (idb_pin) {
              bus_pin_vec.emplace_back(idb_pin);
            }
          }

          for (int i = bus_pin_vec.size() - 1; auto* verilog_id_net_expr : verilog_id_concat_vec) {
            assert(i >= 0);
            auto* idb_pin = bus_pin_vec[i];

            // create pin bus, pin should not be one.
            if (i == static_cast<int>(bus_pin_vec.size() - 1) && (bus_pin_vec.size() != 1)) {
              // first idb pin create instance pin bus.
              create_or_found_bus(bus_name, idb_pin, i, true);
            }

            if (verilog_is_constant(verilog_id_net_expr)) {
              --i;
              // the next pin add to pin bus.
              if (i >= 0) {
                idb_pin = bus_pin_vec[i];
                create_or_found_bus(bus_name, idb_pin, std::nullopt, false);
              }
              continue;
            }

            // create net bus and add net pin.
            // const char* net_name = verilog_id_net_expr->get_verilog_id()->getBaseName();
            void* net_expr_verilog_id = nullptr;
            if (verilog_is_id_expr(verilog_id_net_expr)) {
              net_expr_verilog_id = const_cast<void*>(verilog_convert_net_id_expr(verilog_id_net_expr)->verilog_id);
            } else if (verilog_is_constant(verilog_id_net_expr)) {
              net_expr_verilog_id = const_cast<void*>(verilog_convert_constant_expr(verilog_id_net_expr)->verilog_id);
            }
            const char* net_name = nullptr;
            if (verilog_is_id(net_expr_verilog_id)) {
              net_name = verilog_convert_id(net_expr_verilog_id)->id;
            } else if (verilog_is_bus_index_id(net_expr_verilog_id)) {
              net_name = verilog_convert_index_id(net_expr_verilog_id)->base_id;
            } else {
              net_name = verilog_convert_slice_id(net_expr_verilog_id)->base_id;
            }
            auto net_bus = idb_design->get_bus_list()->findBus(net_name);

            if (net_bus) {
              // for net bus, we need span the bus.
              int bus_left = (*net_bus).get().get_left();
              int bus_right = (*net_bus).get().get_right();

              if (verilog_is_bus_index_id(net_expr_verilog_id)) {
                bus_left = verilog_convert_index_id(net_expr_verilog_id)->index;
                bus_right = bus_left;
              } else if (verilog_is_bus_slice_id(net_expr_verilog_id)) {
                bus_left = verilog_convert_slice_id(net_expr_verilog_id)->range_max;
                bus_right = verilog_convert_slice_id(net_expr_verilog_id)->range_base;
              }

              for (int j = bus_left; j >= bus_right; --j) {
                std::string bus_one_net_name = makeIndexedName(net_name, j);
                auto* idb_net = idb_net_list->find_net(bus_one_net_name);
                assert(idb_net);
                add_pin(bus_one_net_name, idb_pin);
                --i;
                // the next pin add to pin bus.
                if (i >= 0) {
                  idb_pin = bus_pin_vec[i];
                  create_or_found_bus(bus_name, idb_pin, std::nullopt, false);
                }
              }
            } else {
              add_pin(net_name, idb_pin);
              --i;
              // the next pin add to pin bus.
              if (i >= 0) {
                idb_pin = bus_pin_vec[i];
                create_or_found_bus(bus_name, idb_pin, std::nullopt, false);
              }
            }
          }
        }
      }
      num++;
      if (num % 1000 == 0) {
        ECCLOG.info(ecc::Loc::current(), "Processed ", num, " components...");
      }
    }
  }

  return kVerilogSuccess;
}

int32_t VerilogRead::post_process_float_io_pins() {
  IdbDesign* idb_design = _def_service->get_design();
  IdbPins* idb_io_pin_list = idb_design->get_io_pin_list();

  for (auto* io_pin : idb_io_pin_list->get_pin_list()) {
    if (io_pin->is_io_pin() && (io_pin->get_net() == nullptr)) {
      // create a net for float io pin.
      IdbNet* idb_net = idb_design->createOrFindNet(io_pin->get_pin_name(), IdbConnectType::kSignal);
      idb_design->connectPinToNet(io_pin, idb_net);
    }
  }

  return kVerilogSuccess;

}

}  // namespace idb
