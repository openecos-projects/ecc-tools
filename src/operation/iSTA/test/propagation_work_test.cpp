// SPDX-License-Identifier: MulanPSL-2.0
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

#include "Config.hpp"
#include "Database.hpp"
#include "DelayCalculator.hpp"
#include "sdc/ExceptionMatcher.hpp"
#include "PathChecker.hpp"
#include "RequiredPropagator.hpp"

// Count allocations only inside the operation under test, after fixture setup.
// This executable is single threaded; the allocator has no production hooks.
static bool counting = false;
static std::size_t allocated_bytes = 0;
[[gnu::noinline]] void* operator new(std::size_t size)
{
  if (void* result = std::malloc(size ? size : 1)) {
    if (counting)
      allocated_bytes += size;
    return result;
  }
  throw std::bad_alloc();
}
[[gnu::noinline]] void* operator new[](std::size_t size) { return ::operator new(size); }
[[gnu::noinline]] void operator delete(void* address) noexcept { std::free(address); }
[[gnu::noinline]] void operator delete[](void* address) noexcept { std::free(address); }
[[gnu::noinline]] void operator delete(void* address, std::size_t) noexcept { std::free(address); }
[[gnu::noinline]] void operator delete[](void* address, std::size_t) noexcept { std::free(address); }

using namespace ista;
static void require(bool value, const char* message)
{
  if (!value)
    throw std::runtime_error(message);
}

static void inactiveExceptions()
{
  Database db;
  const std::string sink(1024, 's');
  db.network().pins()[sink].set_net_name("net");
  ExceptionThroughIndex index;
  ExceptionMatcher matcher(db.sdc(), db.network());
  TimingException exception;
  exception.set_through_list({TimingExceptionThrough{}});
  for (int scenario : {0, 1, 2}) {
    if (scenario == 1)
      db.sdc().get_path_exception_list().push_back(exception);
    index.rebuild(db.sdc(), db.network());
    const std::vector<int32_t> state = scenario == 0 ? std::vector<int32_t>{} : std::vector<int32_t>{scenario == 1 ? -1 : 1};
    allocated_bytes = 0;
    counting = true;
    const auto next = matcher.advanceState(index, state, sink, TransType::kRise, "driver");
    counting = false;
    const auto bytes = allocated_bytes;
    require(next == state, "inactive or completed exception changed its progress");
    std::cout << "exception scenario=" << scenario << " bytes=" << bytes << '\n';
    require(bytes <= state.size() * sizeof(int32_t), "inactive/completed exceptions allocated network lookup work");
  }
}

// One capture propagated through a chain. Enlarging its name must have bounded
// allocation cost, rather than multiplying name storage by every data point.
static std::size_t requiredAllocations(std::size_t name_size)
{
  constexpr std::size_t count = 128;
  Database db;
  Config config;
  auto& graph = db.graph();
  const std::string capture = std::string(name_size, 'c') + ":CK";
  auto& clock = db.sdc().get_clock_map()["clk"];
  clock.set_clock_name("clk");
  clock.set_period(10.);
  clock.set_is_propagated(true);
  auto& clock_point = graph.points()[capture];
  clock_point.set_is_clock_point(true);
  auto& waveform = clock_point.get_clock_state("clk");
  waveform.waveforms[TransType::kRise].arrival_map[AnalysisType::kMin][TransType::kRise] = 1.;
  waveform.waveforms[TransType::kRise].arrival_map[AnalysisType::kMax][TransType::kRise] = 1.5;
  waveform.summarize();
  db.network().pins()[capture].set_full_name(capture);
  db.network().pins()[capture].set_load_vertex_idx(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto name = "p" + std::to_string(i);
    auto& pin = db.network().pins()[name];
    pin.set_full_name(name);
    pin.set_load_vertex_idx(i);
    auto& point = graph.points()[name];
    auto& vertex = graph.vertices().emplace_back();
    vertex.set_pin_name(name);
    vertex.set_timing_point(&point);
    graph.vertexOrder().push_back(i);
    auto& path = point.get_path_state_map()[AnalysisType::kMax][PathSourceType::kRegister][TransType::kRise]["clk"];
    path.set_clock_name("clk");
    path.set_start_point("p0");
    path.set_trans_type(TransType::kRise);
    path.set_crpr_clock_pin(capture);
    path.set_crpr_clock_trans_type(TransType::kRise);
    path.set_launch_clock_trans_type(TransType::kRise);
    path.set_arrival(2. + i * .125);
    if (i + 1 < count) {
      Arc arc;
      arc.set_type(ArcType::kNet);
      arc.set_source_pin(name);
      arc.set_sink_pin("p" + std::to_string(i + 1));
      arc.set_source_vertex_idx(i);
      arc.set_sink_vertex_idx(i + 1);
      arc.get_input_output_delay_map()[AnalysisType::kMax][TransType::kRise][TransType::kRise] = .125;
      vertex.get_outgoing_arc_list().push_back(graph.arcs().size());
      graph.arcs().push_back(arc);
    } else {
      pin.set_is_port(true);
      pin.set_direction(PinDirection::kOutput);
      graph.endpoints().push_back(name);
      TimingIoDelay output;
      output.set_analysis_type(AnalysisType::kMax);
      output.set_clock_name("clk");
      output.set_trans_type(TransType::kRise);
      output.set_reference_pin(capture);
      output.set_delay(.25);
      db.sdc().get_port_constraint_map()[name].set_output_delays({output}, false);
    }
  }
  auto& clock_vertex = graph.vertices().emplace_back();
  clock_vertex.set_pin_name(capture);
  clock_vertex.set_timing_point(&clock_point);
  graph.vertexOrder().push_back(count);
  graph.setPathStatesPruned(true);
  DelayCalculator delay(db, config);
  PathChecker checker(db, delay);
  RequiredPropagator required(db, checker);
  allocated_bytes = 0;
  counting = true;
  required.propagate();
  counting = false;
  const auto bytes = allocated_bytes;
  for (std::size_t i = 0; i < count; ++i) {
    const auto& point = graph.points().at("p" + std::to_string(i));
    // 10 period + 1 early capture - .25 output delay + .5 CPPR.
    const double expected = 11.25 - (count - 1 - i) * .125;
    if (std::abs(point.get_required() - expected) >= 1e-9)
      std::cerr << "point=" << i << " required=" << point.get_required() << " expected=" << expected << '\n';
    require(std::abs(point.get_required() - expected) < 1e-9, "capture propagation changed pin required time");
    require(std::abs(point.get_slack() - (expected - (2. + i * .125))) < 1e-9, "capture propagation changed pin slack");
  }
  return bytes;
}

int main(int argc, char** argv)
{
  try {
    if (argc == 2 && std::string_view(argv[1]) == "exceptions") {
      inactiveExceptions();
    } else {
      const auto short_names = requiredAllocations(4);
      const auto long_names = requiredAllocations(1024);
      std::cout << "required short=" << short_names << " long=" << long_names << '\n';
      require(long_names <= short_names + 64 * 1024, "capture name allocations grew with every data point");
    }
  } catch (const std::exception& error) {
    counting = false;
    std::cerr << error.what() << '\n';
    return 1;
  }
}
