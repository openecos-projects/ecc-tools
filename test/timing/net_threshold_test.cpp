// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <stdexcept>

#include "Database.hpp"
#include "NetThresholdAdjustment.hpp"

int main()
{
  using namespace ista;
  Database database;
  auto& cells = database.get_timing_library().get_cell_map();
  for (const auto& name : {"driver", "sram"}) {
    auto& instance = database.get_instance_map()[name];
    instance.set_cell_name(name);
    auto& pin = database.get_pin_map()[name];
    pin.set_instance_name(name);
    cells[name].set_library_name(name);
  }
  cells["driver"].set_slew_derate_from_library(0.5);
  auto values = netThresholdAdjustment(database, "driver", "sram");
  if (values != std::array<double, 4>{0, 0, 0.5, 0.5})
    throw std::runtime_error("cross-library slew derate");
  const auto reverse = netThresholdAdjustment(database, "sram", "driver");
  if (reverse != std::array<double, 4>{0, 0, 2, 2})
    throw std::runtime_error("reverse slew derate");
  cells["sram"].set_input_threshold_pct_rise(0.6);
  cells["sram"].set_input_threshold_pct_fall(0.6);
  values = netThresholdAdjustment(database, "driver", "sram");
  if (std::abs(values[0] - 0.25) > 1e-9 || std::abs(values[1] + 0.25) > 1e-9) {
    throw std::runtime_error("rise/fall threshold delay sign");
  }
  if (netThresholdAdjustment(database, "driver", "driver") != std::array<double, 4>{0, 0, 1, 1}) {
    throw std::runtime_error("same-library path must retain its thresholds");
  }
  auto& port = database.get_pin_map()["port"];
  port.set_is_port(true);
  if (netThresholdAdjustment(database, "driver", "port") != std::array<double, 4>{0, 0, 0.5, 0.5}) {
    throw std::runtime_error("top-level port reference library");
  }
}
