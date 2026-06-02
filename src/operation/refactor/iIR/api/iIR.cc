// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of
// Sciences Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan
// PSL v2. You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
// KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file iIR.cc
 * @author shaozheqing (707005020@qq.com)
 * @brief The top interface of the iIR tools.
 * @version 0.1
 * @date 2023-08-18
 *
 */
#include "iIR.hh"

#include <mutex>
#include <optional>
#include <string_view>

#include "ir-solver/IRSolver.hh"
#include "log/Log.hh"
#include "matrix/IRMatrix.hh"
#include "usage/usage.hh"

namespace {

template <typename T>
RustVec MakeRustVec(std::vector<T>& values) {
  RustVec rust_vec;
  rust_vec.data = values.data();
  rust_vec.len = values.size();
  rust_vec.cap = values.capacity();
  rust_vec.type_size = sizeof(T);
  return rust_vec;
}

struct RustSpefNetStorage {
  std::vector<RustSpefConn> conns;
  std::vector<RustSpefResCap> caps;
  std::vector<RustSpefResCap> ress;
};

const void* CreateRcDataFromSpefExchange(const spef::Exchange& exchange) {
  std::vector<RustSpefNetStorage> storage;
  storage.reserve(exchange.nets.size());

  std::vector<RustSpefNet> rust_nets;
  rust_nets.reserve(exchange.nets.size());

  for (const auto& spef_net : exchange.nets) {
    auto& net_storage = storage.emplace_back();
    net_storage.conns.reserve(spef_net.conns.size());
    net_storage.caps.reserve(spef_net.caps.size());
    net_storage.ress.reserve(spef_net.ress.size());

    for (const auto& conn : spef_net.conns) {
      net_storage.conns.push_back(
          RustSpefConn{conn.pin_port_name.c_str(),
                       conn.conn_type == spef::ConnectionType::kExternal});
    }

    for (const auto& cap : spef_net.caps) {
      net_storage.caps.push_back(
          RustSpefResCap{cap.node1.c_str(), cap.node2.c_str(),
                         cap.res_or_cap});
    }

    for (const auto& res : spef_net.ress) {
      net_storage.ress.push_back(
          RustSpefResCap{res.node1.c_str(), res.node2.c_str(),
                         res.res_or_cap});
    }

    rust_nets.push_back(RustSpefNet{spef_net.name.c_str(),
                                    MakeRustVec(net_storage.conns),
                                    MakeRustVec(net_storage.caps),
                                    MakeRustVec(net_storage.ress)});
  }

  return create_rc_data_from_spef(MakeRustVec(rust_nets));
}

const void* ReadSpefRcData(std::string_view spef_file_path) {
  spef::SpefReader spef_parser;
  const std::string spef_path(spef_file_path);
  if (!spef_parser.read(spef_path)) {
    LOG_ERROR << "read spef file " << spef_path << " failed";
    return nullptr;
  }

  spef_parser.expandName();
  auto* spef_file = spef_parser.getSpefFile();
  if (spef_file == nullptr) {
    LOG_ERROR << "read spef file " << spef_path << " produced no SPEF data";
    return nullptr;
  }

  return CreateRcDataFromSpefExchange(*spef_file);
}

}  // namespace

namespace iir {

/**
 * @brief init IR.
 *
 * @return unsigned
 */
unsigned iIR::init() {
  static std::once_flag init_once;
  std::call_once(init_once, [] { init_iir(); });
  return 1;
}

/**
 * @brief read spef file.
 *
 * @param spef_file_path
 * @return
 */
unsigned iIR::readSpef(std::string_view spef_file_path) {
  _rc_data = read_spef(spef_file_path.data());
  return 1;
};

/**
 * @brief read instance power db file to build current vector.
 *
 * @return unsigned
 */
unsigned iIR::readInstancePowerDB(std::string_view instance_power_file_path) {
  _power_data = read_inst_pwr_csv(instance_power_file_path.data());
  return 1;
}

unsigned iIR::setInstancePowerData(
    std::vector<IRInstancePower> instance_power_data) {
  _nominal_voltage = instance_power_data[0]._nominal_voltage;

  RustVec c_instance_power_data;
  c_instance_power_data.data = instance_power_data.data();
  c_instance_power_data.len = instance_power_data.size();
  c_instance_power_data.type_size = sizeof(IRInstancePower);
  c_instance_power_data.cap = instance_power_data.capacity();
  _power_data = set_instance_power_data(c_instance_power_data);
  return 1;
}

/**
 * @brief solve the power net IR drop.
 *
 */
unsigned iIR::solveIRDrop(const char* net_name) {
  if (!_rc_data) {
    LOG_ERROR << "no " << net_name << " RC data to solve IR drop";
    return 0;
  }

  CPU_PROF_START(0);

  LOG_INFO << "solve " << net_name << " IR drop start";

  auto one_net_matrix_data =
      build_one_net_conductance_matrix_data(_rc_data, net_name);

  double sum_resistance = get_sum_resistance(_rc_data, net_name);
  LOG_INFO << "sum resistance: " << sum_resistance;

  IRMatrix ir_matrix;
  auto G_matrix = ir_matrix.buildConductanceMatrix(one_net_matrix_data);

  auto* current_rust_map =
      build_one_net_instance_current_vector(_power_data, _rc_data, net_name);
  auto J_vector = ir_matrix.buildCurrentVector(
      current_rust_map, one_net_matrix_data.node_num, net_name);

  // Get the minimum element of J_vector
  double min_element = J_vector.minCoeff();
  LOG_INFO << "minimum element in J_vector: " << min_element;

  std::unique_ptr<IRSolver> ir_solver;
  if (_solver_method == IRSolverMethod::kLUSolver) {
    LOG_INFO << "Using LU solver";
    ir_solver = std::make_unique<IRLUSolver>();
  } else if (_solver_method == IRSolverMethod::kCGSolver) {
    LOG_INFO << "Using CG solver";
    ir_solver = std::make_unique<IRCGSolver>(_nominal_voltage);
  } else {
    LOG_ERROR << "unknown IR solver method";
    return 0;
  }

  auto grid_voltages = (*ir_solver)(G_matrix, J_vector);

  std::optional<std::pair<std::string, double>> max_ir_drop;
  std::optional<std::pair<std::string, double>> min_ir_drop;

  auto instance_node_ids = get_instance_node_ids(_rc_data, net_name);
  uintptr_t* instance_id;
  FOREACH_VEC_ELEM(&instance_node_ids, uintptr_t, instance_id) {
    double ir_drop = grid_voltages[*instance_id];
    std::string instance_name =
        get_instance_name(_rc_data, net_name, *instance_id);

    // LOG_INFO << "instance: " << instance_name << " ir drop: "
    //          << ir_drop;

    if (!max_ir_drop) {
      max_ir_drop = {instance_name, ir_drop};
      min_ir_drop = {instance_name, ir_drop};
    } else {
      if (ir_drop > max_ir_drop->second) {
        max_ir_drop = {instance_name, ir_drop};
      }

      if (ir_drop < min_ir_drop->second) {
        min_ir_drop = {instance_name, ir_drop};
      }
    }

    _net_to_instance_ir_drop[net_name][instance_name] = ir_drop;
  }

  LOG_INFO << "solve " << net_name << " IR drop end";

  LOG_INFO << "net " << net_name << " max ir drop: " << max_ir_drop->first
           << " : " << max_ir_drop->second;
  LOG_INFO << "net " << net_name << " min ir drop: " << min_ir_drop->first
           << " : " << min_ir_drop->second;

  CPU_PROF_END(0, "solve IR drop");
  return 1;
}

}  // namespace iir
