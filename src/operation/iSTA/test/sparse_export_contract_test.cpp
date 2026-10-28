// SPDX-License-Identifier: MulanPSL-2.0
#include <cstdlib>
#include <stdexcept>

#include "DataManager.hpp"
#include "SparseTimingSnapshot.hpp"
#include "TimingData.hpp"

int main()
{
  using namespace ista;
  DataManager::initInst();
  auto& database = STADM.getDatabase();
  auto& pin = database.network().pins()["in"];
  pin.set_full_name("in");
  pin.set_is_port(true);
  pin.set_direction(PinDirection::kInput);
  pin.set_net_name("n");
  auto& net = database.network().nets()["n"];
  net.set_net_name("n");
  net.set_driver_pin("in");

  ::setenv("ECC_TIMING_FORCE_SPARSE", "1", 1);
  TimingData snapshot;
  snapshot.clock_names = {"clk"};
  snapshot.pins.push_back(TimingPinData{});
  snapshot.pins.back().name = "in";
  SparseTimingExportAdapter::compile(snapshot);
  ::unsetenv("ECC_TIMING_FORCE_SPARSE");

  if (!snapshot.sparse.enabled || snapshot.schema_version != 3 || snapshot.sparse.domains.size() != 2) {
    DataManager::destroyInst();
    throw std::runtime_error("forced sparse export did not publish the current schema");
  }
  DataManager::destroyInst();
}
