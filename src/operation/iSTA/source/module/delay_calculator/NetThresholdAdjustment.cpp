// SPDX-License-Identifier: MulanPSL-2.0
#include "NetThresholdAdjustment.hpp"

#include <stdexcept>

#include "Database.hpp"

namespace ista {
namespace {
struct Thresholds
{
  std::string library;
  std::array<double, 2> span;
  std::array<double, 2> input;
  std::array<double, 2> output;
  double derate;
};

template <typename T>
Thresholds thresholds(const T& library, const std::string& name)
{
  return {name,
          {library.get_slew_upper_threshold_pct_rise() - library.get_slew_lower_threshold_pct_rise(),
           library.get_slew_upper_threshold_pct_fall() - library.get_slew_lower_threshold_pct_fall()},
          {library.get_input_threshold_pct_rise(), library.get_input_threshold_pct_fall()},
          {library.get_output_threshold_pct_rise(), library.get_output_threshold_pct_fall()},
          library.get_slew_derate_from_library()};
}

Thresholds pinThresholds(Database& database, const std::string& name)
{
  auto& pin = database.get_pin_map().at(name);
  if (!pin.get_is_port()) {
    auto& instance = database.get_instance_map().at(pin.get_instance_name());
    const auto cell = database.get_timing_library().get_cell_map().find(instance.get_cell_name());
    if (cell != database.get_timing_library().get_cell_map().end())
      return thresholds(cell->second, cell->second.get_library_name());
  }
  // Top-level ports and uncharacterized black boxes use the reference library.
  return thresholds(database.get_timing_library(), "");
}
}  // namespace

std::array<double, 4> netThresholdAdjustment(Database& database, const std::string& source, const std::string& sink)
{
  const auto driver = pinThresholds(database, source);
  const auto load = pinThresholds(database, sink);
  if (driver.library == load.library)
    return {0, 0, 1, 1};
  if (driver.span[0] <= 0 || driver.span[1] <= 0 || load.span[0] <= 0 || load.span[1] <= 0 || driver.derate <= 0 || load.derate <= 0) {
    throw std::runtime_error("invalid Liberty slew thresholds on " + source + " -> " + sink);
  }
  return {(load.input[0] - driver.output[0]) / driver.span[0], -(load.input[1] - driver.output[1]) / driver.span[1],
          (load.span[0] / load.derate) / (driver.span[0] / driver.derate), (load.span[1] / load.derate) / (driver.span[1] / driver.derate)};
}
}  // namespace ista
