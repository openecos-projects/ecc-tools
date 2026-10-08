// ***************************************************************************************
// SPDX-License-Identifier: MulanPSL-2.0
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

//
// Parser-excluded legacy iDB counterpart to DbOnlyEccdbBenchmark.  It reads
// the same deterministic logical fixture and creates only masters, terms,
// instances, nets, and instance-pin connections.  No LEF/DEF parser,
// placement, BTerms, geometry, special nets, or routing data participates.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "DbOnlyBenchCommon.h"
#include "IdbDesign.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Options
{
  std::filesystem::path fixture;
  std::filesystem::path output;
  std::size_t repeat = 5;
};

struct Counts
{
  uint64_t masters = 0;
  uint64_t instances = 0;
  uint64_t instance_pins = 0;
  uint64_t nets = 0;
  uint64_t connections = 0;
};

struct Timing
{
  uint64_t create_instances_ns = 0;
  uint64_t create_nets_ns = 0;
  uint64_t connect_ns = 0;
  uint64_t find_instances_ns = 0;
  uint64_t find_nets_ns = 0;
  uint64_t find_pins_ns = 0;
  uint64_t traverse_instances_ns = 0;
  uint64_t traverse_nets_ns = 0;
  uint64_t traverse_net_pins_ns = 0;
};

struct Run
{
  Timing timing;
  uint64_t checksum = 0;
};

struct QueryIndices
{
  std::vector<std::size_t> instances;
  std::vector<std::size_t> nets;
  std::vector<std::size_t> connections;
};

struct LibraryContext
{
  std::unique_ptr<idb::IdbLayout> layout = std::make_unique<idb::IdbLayout>();
  std::vector<idb::IdbCellMaster*> masters;
  std::vector<std::vector<idb::IdbTerm*>> terms;
};

volatile uint64_t g_sink = 0;

[[noreturn]] void fail(std::string message)
{
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string_view message)
{
  if (!condition) {
    fail(std::string(message));
  }
}

[[nodiscard]] uint64_t elapsedNs(Clock::time_point begin)
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

// Every timed phase returns only a fixture-derived checksum. In particular,
// it never incorporates iDB pointers; it only keeps the operation result
// observable. Semantic equivalence is checked by the untimed validation.
[[nodiscard]] uint64_t foldChecksum(uint64_t seed, uint64_t value)
{
  return seed ^ (value + UINT64_C(0x9e3779b97f4a7c15) + (seed << 6u) + (seed >> 2u));
}

template <typename Function>
[[nodiscard]] uint64_t measure(Function&& function, uint64_t& checksum)
{
  const auto begin = Clock::now();
  const uint64_t value = std::forward<Function>(function)();
  const uint64_t elapsed = elapsedNs(begin);

  // Deliberately outside the timed interval.
  g_sink ^= value;
  checksum = foldChecksum(checksum, value);
  return elapsed;
}

[[nodiscard]] Counts countsFor(const DbOnlyFixture& fixture)
{
  Counts result{.masters = static_cast<uint64_t>(fixture.masters.size()),
                .instances = static_cast<uint64_t>(fixture.instances.size()),
                .nets = static_cast<uint64_t>(fixture.nets.size()),
                .connections = static_cast<uint64_t>(fixture.connections.size())};
  for (const auto& instance : fixture.instances) {
    result.instance_pins += static_cast<uint64_t>(fixture.masters[instance.master_index].terms.size());
  }
  return result;
}

void populateLibrary(LibraryContext& context, const DbOnlyFixture& fixture)
{
  auto* masters = context.layout->get_cell_master_list();
  require(masters != nullptr, "iDB layout has no cell-master list");
  context.masters.reserve(fixture.masters.size());
  context.terms.resize(fixture.masters.size());

  for (std::size_t master_index = 0; master_index < fixture.masters.size(); ++master_index) {
    const auto& source = fixture.masters[master_index];
    auto* master = masters->set_cell_master(source.name);
    require(master != nullptr, "iDB could not create fixture master");
    context.masters.push_back(master);

    auto& terms = context.terms[master_index];
    terms.reserve(source.terms.size());
    for (const auto& name : source.terms) {
      auto* term = master->add_term(name);
      require(term != nullptr, "iDB could not create fixture master term");
      terms.push_back(term);
    }
  }
}

void validateLibrary(LibraryContext& context, const DbOnlyFixture& fixture)
{
  auto* masters = context.layout->get_cell_master_list();
  require(masters != nullptr, "iDB layout lost cell-master list");
  require(masters->get_cell_master().size() == fixture.masters.size(), "iDB master count mismatch");
  require(context.masters.size() == fixture.masters.size(), "iDB master cache count mismatch");

  for (std::size_t master_index = 0; master_index < fixture.masters.size(); ++master_index) {
    const auto& source = fixture.masters[master_index];
    auto* master = context.masters[master_index];
    require(master != nullptr && master->get_name() == source.name, "iDB master name mismatch");
    require(masters->find_cell_master(source.name) == master, "iDB master lookup mismatch");
    require(context.terms[master_index].size() == source.terms.size(), "iDB term cache count mismatch");
    const auto stored_terms = master->get_term_list();
    require(stored_terms.size() == source.terms.size(), "iDB master term count mismatch");
    for (std::size_t term_index = 0; term_index < source.terms.size(); ++term_index) {
      require(context.terms[master_index][term_index] == stored_terms[term_index], "iDB term order mismatch");
      require(stored_terms[term_index] != nullptr && stored_terms[term_index]->get_name() == source.terms[term_index],
              "iDB term name mismatch");
    }
  }
}

[[nodiscard]] QueryIndices makeQueryIndices(const DbOnlyFixture& fixture)
{
  return QueryIndices{.instances = shuffledIndices(fixture.instances.size(), UINT64_C(0x11)),
                      .nets = shuffledIndices(fixture.nets.size(), UINT64_C(0x22)),
                      .connections = shuffledIndices(fixture.connections.size(), UINT64_C(0x33))};
}

[[nodiscard]] std::vector<idb::IdbPin*> cacheConnectionPins(const DbOnlyFixture& fixture,
                                                              const std::vector<idb::IdbInstance*>& instances)
{
  std::vector<idb::IdbPin*> pins;
  pins.reserve(fixture.connections.size());
  for (const auto& connection : fixture.connections) {
    const auto& instance = fixture.instances[connection.instance_index];
    const auto& term = fixture.masters[instance.master_index].terms[connection.term_index];
    auto* pin = instances[connection.instance_index]->get_pin(term);
    require(pin != nullptr, "iDB instance is missing fixture pin");
    pins.push_back(pin);
  }
  return pins;
}

void validateCreatedInstances(const DbOnlyFixture& fixture,
                              idb::IdbDesign& design,
                              const LibraryContext& library,
                              const std::vector<idb::IdbInstance*>& instances)
{
  require(instances.size() == fixture.instances.size(), "iDB instance cache count mismatch");
  require(design.get_instance_list()->get_instance_list().size() == fixture.instances.size(), "iDB instance creation count mismatch");
  for (std::size_t index = 0; index < instances.size(); ++index) {
    const auto& source = fixture.instances[index];
    require(instances[index] != nullptr && instances[index]->get_name() == source.name, "iDB instance creation mismatch");
    require(instances[index]->get_cell_master() == library.masters[source.master_index], "iDB instance master creation mismatch");
  }
}

void validateCreatedNets(const DbOnlyFixture& fixture,
                         idb::IdbDesign& design,
                         const std::vector<idb::IdbNet*>& nets)
{
  require(nets.size() == fixture.nets.size(), "iDB net cache count mismatch");
  require(design.get_net_list()->get_net_list().size() == fixture.nets.size(), "iDB net creation count mismatch");
  for (std::size_t index = 0; index < nets.size(); ++index) {
    require(nets[index] != nullptr && nets[index]->get_net_name() == fixture.nets[index], "iDB net creation mismatch");
  }
}

void validateConstruction(const DbOnlyFixture& fixture,
                          const Counts& counts,
                          idb::IdbDesign& design,
                          const LibraryContext& library,
                          const std::vector<idb::IdbInstance*>& instances,
                          const std::vector<idb::IdbNet*>& nets,
                          const std::vector<idb::IdbPin*>& connection_pins)
{
  auto* instance_list = design.get_instance_list();
  auto* net_list = design.get_net_list();
  require(instance_list != nullptr && net_list != nullptr, "iDB design lists are unavailable");
  require(instance_list->get_instance_list().size() == counts.instances, "iDB instance count mismatch");
  require(net_list->get_net_list().size() == counts.nets, "iDB net count mismatch");
  require(instances.size() == fixture.instances.size(), "iDB instance cache count mismatch");
  require(nets.size() == fixture.nets.size(), "iDB net cache count mismatch");
  require(connection_pins.size() == fixture.connections.size(), "iDB pin cache count mismatch");

  uint64_t instance_pin_count = 0;
  for (std::size_t index = 0; index < fixture.instances.size(); ++index) {
    const auto& source = fixture.instances[index];
    auto* instance = instances[index];
    require(instance != nullptr && instance->get_name() == source.name, "iDB instance name mismatch");
    require(instance->get_cell_master() == library.masters[source.master_index], "iDB instance master mismatch");
    require(instance_list->find_instance(source.name) == instance, "iDB instance lookup mismatch");
    require(instance->get_pin_list() != nullptr, "iDB instance pin list missing");
    const auto& pins = instance->get_pin_list()->get_pin_list();
    require(pins.size() == fixture.masters[source.master_index].terms.size(), "iDB instance pin count mismatch");
    instance_pin_count += pins.size();
  }
  require(instance_pin_count == counts.instance_pins, "iDB total instance-pin count mismatch");

  for (std::size_t index = 0; index < fixture.nets.size(); ++index) {
    require(nets[index] != nullptr && nets[index]->get_net_name() == fixture.nets[index], "iDB net name mismatch");
    require(net_list->find_net(fixture.nets[index]) == nets[index], "iDB net lookup mismatch");
  }

  std::vector<std::size_t> expected_pins_per_net(fixture.nets.size(), 0);
  for (std::size_t index = 0; index < fixture.connections.size(); ++index) {
    const auto& connection = fixture.connections[index];
    const auto& source_instance = fixture.instances[connection.instance_index];
    auto* pin = connection_pins[index];
    require(pin != nullptr && pin->get_net() == nets[connection.net_index], "iDB pin net relation mismatch");
    require(pin->get_instance() == instances[connection.instance_index], "iDB pin instance relation mismatch");
    require(pin->get_term() == library.terms[source_instance.master_index][connection.term_index],
            "iDB pin master-term relation mismatch");
    auto* net_pins = nets[connection.net_index]->get_instance_pin_list();
    require(net_pins != nullptr && net_pins->contains(pin), "iDB net reverse connection membership mismatch");
    ++expected_pins_per_net[connection.net_index];
  }
  for (std::size_t index = 0; index < nets.size(); ++index) {
    auto* pins = nets[index]->get_instance_pin_list();
    require(pins != nullptr && pins->get_pin_list().size() == expected_pins_per_net[index],
            "iDB net reverse connection count mismatch");
  }
}

void validateQueries(const DbOnlyFixture& fixture,
                     const QueryIndices& queries,
                     idb::IdbDesign& design,
                     const std::vector<idb::IdbInstance*>& instances,
                     const std::vector<idb::IdbNet*>& nets,
                     const std::vector<idb::IdbPin*>& connection_pins)
{
  for (const auto index : queries.instances) {
    require(design.get_instance_list()->find_instance(fixture.instances[index].name) == instances[index],
            "iDB post-build instance query mismatch");
  }
  for (const auto index : queries.nets) {
    require(design.get_net_list()->find_net(fixture.nets[index]) == nets[index], "iDB post-build net query mismatch");
  }
  for (const auto index : queries.connections) {
    const auto& connection = fixture.connections[index];
    const auto& instance = fixture.instances[connection.instance_index];
    const auto& term = fixture.masters[instance.master_index].terms[connection.term_index];
    require(instances[connection.instance_index]->get_pin(term) == connection_pins[index],
            "iDB post-build instance-pin query mismatch");
  }
}

[[nodiscard]] Run runOnce(const DbOnlyFixture& fixture,
                          const Counts& counts,
                          const LibraryContext& library,
                          const QueryIndices& queries)
{
  // Just as in the EccDB and OpenDB variants, fresh design construction is
  // setup, not a timed phase. Library/master/term construction is shared and
  // completed before all repetitions.
  idb::IdbDesign design(library.layout.get());
  auto* instance_list = design.get_instance_list();
  auto* net_list = design.get_net_list();
  require(instance_list != nullptr && net_list != nullptr, "iDB design lists are unavailable");
  std::vector<idb::IdbInstance*> instances;
  std::vector<idb::IdbNet*> nets;
  instances.reserve(fixture.instances.size());
  nets.reserve(fixture.nets.size());

  Run result;
  result.timing.create_instances_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        for (std::size_t index = 0; index < fixture.instances.size(); ++index) {
          const auto& source = fixture.instances[index];
          // The fixture has already resolved the master. This matches the
          // EccDB path, which receives a LibraryCellMasterId rather than a
          // master name that must be looked up during the timed phase.
          auto* instance = instance_list->add_instance(source.name);
          if (instance != nullptr) {
            instance->set_cell_master(library.masters[source.master_index]);
          }
          instances.push_back(instance);
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index)
                                                            ^ (static_cast<uint64_t>(instance != nullptr) << 63u));
        }
        return phase_checksum;
      },
      result.checksum);

  validateCreatedInstances(fixture, design, library, instances);

  // iDB's instance pin lookup is intentionally outside connect_ns, matching
  // the paired EccDB benchmark's pre-resolved DesignInstancePin IDs.
  const auto connection_pins = cacheConnectionPins(fixture, instances);

  result.timing.create_nets_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        for (std::size_t index = 0; index < fixture.nets.size(); ++index) {
          // DbOnlyFixture has unique, non-empty net names. add_net performs
          // the same single name-index lookup that EccDB createNet performs;
          // createOrFindNet would do a second lookup before calling add_net.
          auto* net = net_list->add_net(fixture.nets[index], idb::IdbConnectType::kNone);
          nets.push_back(net);
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index)
                                                            ^ (static_cast<uint64_t>(net != nullptr) << 63u));
        }
        return phase_checksum;
      },
      result.checksum);

  validateCreatedNets(fixture, design, nets);

  result.timing.connect_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        for (std::size_t index = 0; index < fixture.connections.size(); ++index) {
          const auto& connection = fixture.connections[index];
          const bool connected = design.connectPinToNet(connection_pins[index], nets[connection.net_index]);
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index)
                                                            ^ (static_cast<uint64_t>(connected) << 63u));
        }
        return phase_checksum;
      },
      result.checksum);

  validateConstruction(fixture, counts, design, library, instances, nets, connection_pins);

  result.timing.find_instances_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        for (const auto index : queries.instances) {
          const bool found = instance_list->find_instance(fixture.instances[index].name) != nullptr;
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index) ^ static_cast<uint64_t>(found));
        }
        return phase_checksum;
      },
      result.checksum);

  result.timing.find_nets_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        for (const auto index : queries.nets) {
          const bool found = net_list->find_net(fixture.nets[index]) != nullptr;
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index) ^ static_cast<uint64_t>(found));
        }
        return phase_checksum;
      },
      result.checksum);

  result.timing.find_pins_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        for (const auto index : queries.connections) {
          const auto& connection = fixture.connections[index];
          const auto& instance = fixture.instances[connection.instance_index];
          const auto& term = fixture.masters[instance.master_index].terms[connection.term_index];
          const bool found = instances[connection.instance_index]->get_pin(term) != nullptr;
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index) ^ static_cast<uint64_t>(found));
        }
        return phase_checksum;
      },
      result.checksum);

  validateQueries(fixture, queries, design, instances, nets, connection_pins);

  result.timing.traverse_instances_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        const auto& values = instance_list->get_instance_list();
        for (std::size_t index = 0; index < values.size(); ++index) {
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index) ^ static_cast<uint64_t>(values[index] != nullptr));
        }
        return phase_checksum;
      },
      result.checksum);

  result.timing.traverse_nets_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        const auto& values = net_list->get_net_list();
        for (std::size_t index = 0; index < values.size(); ++index) {
          phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(index) ^ static_cast<uint64_t>(values[index] != nullptr));
        }
        return phase_checksum;
      },
      result.checksum);

  result.timing.traverse_net_pins_ns = measure(
      [&]() {
        uint64_t phase_checksum = 0;
        std::size_t ordinal = 0;
        for (auto* net : net_list->get_net_list()) {
          if (net == nullptr || net->get_instance_pin_list() == nullptr) continue;
          for (auto* pin : net->get_instance_pin_list()->get_pin_list()) {
            phase_checksum = foldChecksum(phase_checksum, static_cast<uint64_t>(ordinal)
                                                              ^ static_cast<uint64_t>(pin != nullptr));
            ++ordinal;
          }
        }
        return phase_checksum;
      },
      result.checksum);
  return result;
}

[[nodiscard]] uint64_t median(const std::vector<Run>& runs, uint64_t Timing::*field)
{
  std::vector<uint64_t> values;
  values.reserve(runs.size());
  for (const auto& run : runs) values.push_back(run.timing.*field);
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2u);
}

[[nodiscard]] Timing medianTiming(const std::vector<Run>& runs)
{
  return Timing{.create_instances_ns = median(runs, &Timing::create_instances_ns),
                .create_nets_ns = median(runs, &Timing::create_nets_ns),
                .connect_ns = median(runs, &Timing::connect_ns),
                .find_instances_ns = median(runs, &Timing::find_instances_ns),
                .find_nets_ns = median(runs, &Timing::find_nets_ns),
                .find_pins_ns = median(runs, &Timing::find_pins_ns),
                .traverse_instances_ns = median(runs, &Timing::traverse_instances_ns),
                .traverse_nets_ns = median(runs, &Timing::traverse_nets_ns),
                .traverse_net_pins_ns = median(runs, &Timing::traverse_net_pins_ns)};
}

void writeTiming(std::ostream& output, const Timing& timing)
{
  output << "\"create_instances_ns\":" << timing.create_instances_ns << ",\"create_nets_ns\":" << timing.create_nets_ns
         << ",\"connect_ns\":" << timing.connect_ns << ",\"find_instances_ns\":" << timing.find_instances_ns
         << ",\"find_nets_ns\":" << timing.find_nets_ns << ",\"find_pins_ns\":" << timing.find_pins_ns
         << ",\"traverse_instances_ns\":" << timing.traverse_instances_ns
         << ",\"traverse_nets_ns\":" << timing.traverse_nets_ns
         << ",\"traverse_net_pins_ns\":" << timing.traverse_net_pins_ns;
}

void writeJson(std::ostream& output, const Counts& counts, const std::vector<Run>& runs)
{
  output << "{\"schema\":\"db_only_benchmark_v1\",\"backend\":\"idb\",\"scope\":"
            "\"legacy iDB logical-netlist operations only; fixture JSON parsing, IdbLayout/IdbCellMaster/IdbTerm setup, "
            "fresh IdbDesign setup, validation, and output are excluded; timed phases create IdbInstance (with automatic "
            "IdbPin), create IdbNet, connect pre-resolved IdbPin, find, and traverse\",\"counts\":{\"masters\":"
         << counts.masters << ",\"instances\":" << counts.instances << ",\"instance_pins\":" << counts.instance_pins
         << ",\"nets\":" << counts.nets << ",\"connections\":" << counts.connections << "},\"runs\":[";
  for (std::size_t index = 0; index < runs.size(); ++index) {
    if (index != 0u) output << ',';
    output << '{';
    writeTiming(output, runs[index].timing);
    output << '}';
  }
  output << "],\"median\":{";
  writeTiming(output, medianTiming(runs));
  output << "},\"checksums\":[";
  for (std::size_t index = 0; index < runs.size(); ++index) {
    if (index != 0u) output << ',';
    output << runs[index].checksum;
  }
  output << "]}\n";
}

[[nodiscard]] std::size_t parseRepeat(std::string_view value)
{
  uint64_t parsed = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || end != value.data() + value.size() || parsed == 0u
      || parsed > static_cast<uint64_t>(std::numeric_limits<std::size_t>::max()) || (parsed % 2u) == 0u) {
    fail("--repeat must be a positive odd integer");
  }
  return static_cast<std::size_t>(parsed);
}

[[nodiscard]] Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&](std::string_view name) -> std::string_view {
      if (index + 1 >= argc) fail("missing value for " + std::string(name));
      return argv[++index];
    };
    if (argument == "--fixture") options.fixture = std::string(value(argument));
    else if (argument == "--repeat") options.repeat = parseRepeat(value(argument));
    else if (argument == "--output") options.output = std::string(value(argument));
    else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: eccdb_db_only_idb_benchmark --fixture FIXTURE.json [--repeat ODD_INT] [--output JSON]\n";
      std::exit(0);
    } else {
      fail("unknown argument: " + std::string(argument));
    }
  }
  if (!std::filesystem::is_regular_file(options.fixture)) {
    fail("fixture is not a regular file: " + options.fixture.string());
  }
  return options;
}

void run(const Options& options)
{
  const DbOnlyFixture fixture = DbOnlyFixture::load(options.fixture);
  const Counts counts = countsFor(fixture);
  LibraryContext library;
  populateLibrary(library, fixture);
  validateLibrary(library, fixture);
  const QueryIndices queries = makeQueryIndices(fixture);

  std::vector<Run> runs;
  runs.reserve(options.repeat);
  for (std::size_t index = 0; index < options.repeat; ++index) {
    runs.push_back(runOnce(fixture, counts, library, queries));
  }

  if (options.output.empty()) {
    writeJson(std::cout, counts, runs);
    return;
  }
  if (const auto parent = options.output.parent_path(); !parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) fail("cannot create output directory: " + parent.string());
  }
  std::ofstream output(options.output, std::ios::out | std::ios::trunc);
  if (!output) fail("cannot open output: " + options.output.string());
  writeJson(output, counts, runs);
  if (!output) fail("cannot write output: " + options.output.string());
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    run(parseOptions(argc, argv));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "eccdb_db_only_idb_benchmark: " << error.what() << '\n';
    return 1;
  }
}
