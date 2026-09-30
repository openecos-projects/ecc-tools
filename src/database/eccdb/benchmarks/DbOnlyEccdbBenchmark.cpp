// SPDX-License-Identifier: Apache-2.0

// Logical-netlist benchmark shared with the OpenDB counterpart.  The fixture
// is already parsed before this executable begins its timed work; no LEF or
// DEF parser is involved here.

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
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "DbOnlyBenchCommon.h"
#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/CellMasterComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechStore.h"

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
  std::vector<std::size_t> pins;
};

struct LibraryContext
{
  eccdb::TechStore technology;
  eccdb::LibraryStore library;
  std::vector<eccdb::LibraryCellMasterId> masters;
  std::vector<std::vector<eccdb::LibraryMasterTermId>> terms;

  LibraryContext() : library(technology.techRegistry()) {}
};

volatile uint64_t g_sink = 0;

[[noreturn]] void fail(std::string message)
{
  throw std::runtime_error(std::move(message));
}

void require(bool condition, std::string_view message)
{
  if (!condition) fail(std::string(message));
}

[[nodiscard]] uint64_t elapsedNs(Clock::time_point begin)
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

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

  // The volatile sink makes each loop result observable without charging the
  // sink update itself to the operation being measured.
  g_sink = g_sink ^ value;
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
  context.masters.reserve(fixture.masters.size());
  context.terms.resize(fixture.masters.size());
  for (std::size_t master_index = 0; master_index < fixture.masters.size(); ++master_index) {
    const auto& source_master = fixture.masters[master_index];
    const auto master = context.library.cellMasterStorage().createCellMaster(eccdb::LibraryCellMaster{.name = source_master.name});
    context.masters.push_back(master);

    auto& terms = context.terms[master_index];
    terms.reserve(source_master.terms.size());
    for (const auto& name : source_master.terms) {
      terms.push_back(context.library.masterTermStorage().createMasterTerm(master, eccdb::LibraryMasterTerm{.name = name}));
    }
  }
}

void validateLibrary(const LibraryContext& context, const DbOnlyFixture& fixture)
{
  require(context.library.cellMasterStorage().cellMasterCount() == fixture.masters.size(), "library master count mismatch");
  require(context.masters.size() == fixture.masters.size(), "library master ID count mismatch");
  require(context.terms.size() == fixture.masters.size(), "library term table count mismatch");

  std::size_t expected_terms = 0;
  for (std::size_t master_index = 0; master_index < fixture.masters.size(); ++master_index) {
    const auto& source_master = fixture.masters[master_index];
    const auto master = context.masters[master_index];
    const auto& stored_master = context.library.cellMasterStorage().cellMaster(master);
    require(stored_master.name == source_master.name, "library master name mismatch");
    require(stored_master.terms.size() == source_master.terms.size(), "library master-term count mismatch");
    require(context.library.cellMasterStorage().findCellMaster(source_master.name) == master, "library master lookup mismatch");
    require(context.terms[master_index].size() == source_master.terms.size(), "library master-term ID count mismatch");

    expected_terms += source_master.terms.size();
    for (std::size_t term_index = 0; term_index < source_master.terms.size(); ++term_index) {
      const auto term = context.terms[master_index][term_index];
      require(context.library.masterTermStorage().masterTerm(term).name == source_master.terms[term_index], "library term name mismatch");
      require(context.library.masterTermStorage().findMasterTerm(master, source_master.terms[term_index]) == term,
              "library term lookup mismatch");
    }
  }
  require(context.library.masterTermStorage().masterTermCount() == expected_terms, "library term count mismatch");
}

[[nodiscard]] QueryIndices makeQueryIndices(const DbOnlyFixture& fixture)
{
  return QueryIndices{.instances = shuffledIndices(fixture.instances.size(), UINT64_C(0x11)),
                      .nets = shuffledIndices(fixture.nets.size(), UINT64_C(0x22)),
                      .pins = shuffledIndices(fixture.connections.size(), UINT64_C(0x33))};
}

[[nodiscard]] std::vector<eccdb::DesignInstancePinId> cacheConnectionPins(
    const DbOnlyFixture& fixture, const LibraryContext& library, const eccdb::DesignNetlistStorage& netlist,
    const std::vector<eccdb::DesignInstanceId>& instances)
{
  std::vector<eccdb::DesignInstancePinId> result;
  result.reserve(fixture.connections.size());
  for (const auto& connection : fixture.connections) {
    const auto pins = netlist.instancePins(instances[connection.instance_index]);
    require(connection.term_index < pins.size(), "fixture connection term index is outside instance pin range");
    const auto pin = pins[connection.term_index];
    require(netlist.instancePin(pin).master_term == library.terms[fixture.instances[connection.instance_index].master_index][connection.term_index],
            "instance pin order does not match library term order");
    result.push_back(pin);
  }
  return result;
}

void validateConstruction(const DbOnlyFixture& fixture, const Counts& counts, const LibraryContext& library,
                          const eccdb::DesignNetlistStorage& netlist, const std::vector<eccdb::DesignInstanceId>& instances,
                          const std::vector<eccdb::DesignNetId>& nets,
                          const std::vector<eccdb::DesignInstancePinId>& connection_pins)
{
  require(netlist.instanceCount() == counts.instances, "design instance count mismatch");
  require(netlist.instancePinCount() == counts.instance_pins, "design instance-pin count mismatch");
  require(netlist.netCount() == counts.nets, "design net count mismatch");
  require(instances.size() == fixture.instances.size(), "design instance ID count mismatch");
  require(nets.size() == fixture.nets.size(), "design net ID count mismatch");
  require(connection_pins.size() == fixture.connections.size(), "connection pin cache count mismatch");

  for (std::size_t index = 0; index < fixture.instances.size(); ++index) {
    const auto& source = fixture.instances[index];
    const auto id = instances[index];
    const auto& stored = netlist.instance(id);
    require(stored.name == source.name, "design instance name mismatch");
    require(stored.master == library.masters[source.master_index], "design instance master mismatch");
    require(netlist.findInstance(source.name) == id, "design instance lookup mismatch");
  }
  for (std::size_t index = 0; index < fixture.nets.size(); ++index) {
    const auto id = nets[index];
    require(netlist.net(id).name == fixture.nets[index], "design net name mismatch");
    require(netlist.findRegularNet(fixture.nets[index]) == id, "design net lookup mismatch");
  }

  std::vector<std::size_t> expected_pins_per_net(fixture.nets.size(), 0u);
  for (std::size_t index = 0; index < fixture.connections.size(); ++index) {
    const auto& connection = fixture.connections[index];
    const auto master_index = fixture.instances[connection.instance_index].master_index;
    const auto expected_pin = connection_pins[index];
    const auto found_pin = netlist.findInstancePin(instances[connection.instance_index], library.terms[master_index][connection.term_index]);
    require(found_pin == expected_pin, "design instance-pin lookup mismatch");
    const auto& stored_pin = netlist.instancePin(found_pin);
    require(stored_pin.net == nets[connection.net_index], "design connection net mismatch");
    require(!stored_pin.special_net, "design connection unexpectedly uses a special net");
    ++expected_pins_per_net[connection.net_index];
  }
  for (std::size_t index = 0; index < nets.size(); ++index) {
    require(netlist.instancePins(nets[index]).size() == expected_pins_per_net[index], "design net reverse connection count mismatch");
  }
}

void validateQueries(const DbOnlyFixture& fixture, const QueryIndices& query_indices,
                     const eccdb::DesignNetlistStorage& netlist, const std::vector<eccdb::DesignInstanceId>& instances,
                     const std::vector<eccdb::DesignNetId>& nets,
                     const std::vector<eccdb::DesignInstancePinId>& connection_pins)
{
  for (const auto index : query_indices.instances) {
    require(netlist.findInstance(fixture.instances[index].name) == instances[index], "post-build instance query mismatch");
  }
  for (const auto index : query_indices.nets) {
    require(netlist.findRegularNet(fixture.nets[index]) == nets[index], "post-build net query mismatch");
  }
  for (const auto index : query_indices.pins) {
    const auto& connection = fixture.connections[index];
    const auto master_index = fixture.instances[connection.instance_index].master_index;
    require(netlist.findInstancePin(
                instances[connection.instance_index], fixture.masters[master_index].terms[connection.term_index])
                == connection_pins[index],
            "post-build instance-pin query mismatch");
  }
}

[[nodiscard]] Run runOnce(const DbOnlyFixture& fixture, const Counts& counts, const LibraryContext& library,
                          const QueryIndices& query_indices)
{
  // A new DesignStore isolates every repeat.  Its construction is deliberately
  // outside the nine timed database operations.
  eccdb::DesignStore design(library.technology.techRegistry(), library.library.libraryRegistry());
  auto& netlist = design.netlistStorage();
  std::vector<eccdb::DesignInstanceId> instances;
  std::vector<eccdb::DesignNetId> nets;
  instances.reserve(fixture.instances.size());
  nets.reserve(fixture.nets.size());

  Run result;
  result.timing.create_instances_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        for (const auto& instance : fixture.instances) {
          const auto id = netlist.createInstance(eccdb::DesignInstance{.name = instance.name, .master = library.masters[instance.master_index]});
          instances.push_back(id);
          checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
        }
        return checksum;
      },
      result.checksum);

  const auto connection_pins = cacheConnectionPins(fixture, library, netlist, instances);

  result.timing.create_nets_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        for (const auto& name : fixture.nets) {
          const auto id = netlist.createNet(eccdb::DesignNet{.name = name});
          nets.push_back(id);
          checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
        }
        return checksum;
      },
      result.checksum);

  result.timing.connect_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        for (std::size_t index = 0; index < fixture.connections.size(); ++index) {
          const auto& connection = fixture.connections[index];
          const auto pin = connection_pins[index];
          const auto net = nets[connection.net_index];
          netlist.connect(pin, net);
          checksum = foldChecksum(checksum, static_cast<uint64_t>(pin.packed()) ^ static_cast<uint64_t>(net.packed()));
        }
        return checksum;
      },
      result.checksum);

  validateConstruction(fixture, counts, library, netlist, instances, nets, connection_pins);

  result.timing.find_instances_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        for (const auto index : query_indices.instances) {
          checksum = foldChecksum(checksum, static_cast<uint64_t>(netlist.findInstance(fixture.instances[index].name).packed()));
        }
        return checksum;
      },
      result.checksum);

  result.timing.find_nets_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        for (const auto index : query_indices.nets) {
          checksum = foldChecksum(checksum, static_cast<uint64_t>(netlist.findRegularNet(fixture.nets[index]).packed()));
        }
        return checksum;
      },
      result.checksum);

  result.timing.find_pins_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        for (const auto index : query_indices.pins) {
          const auto& connection = fixture.connections[index];
          const auto master_index = fixture.instances[connection.instance_index].master_index;
          const auto pin = netlist.findInstancePin(
              instances[connection.instance_index], fixture.masters[master_index].terms[connection.term_index]);
          checksum = foldChecksum(checksum, static_cast<uint64_t>(pin.packed()));
        }
        return checksum;
      },
      result.checksum);

  validateQueries(fixture, query_indices, netlist, instances, nets, connection_pins);

  result.timing.traverse_instances_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        netlist.forEachInstance([&](eccdb::DesignInstanceId id, const eccdb::DesignInstance&, const auto) {
          checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
        });
        return checksum;
      },
      result.checksum);

  result.timing.traverse_nets_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        netlist.forEachRegularNet([&](eccdb::DesignNetId id, const eccdb::DesignNet&) {
          checksum = foldChecksum(checksum, static_cast<uint64_t>(id.packed()));
        });
        return checksum;
      },
      result.checksum);

  result.timing.traverse_net_pins_ns = measure(
      [&]() {
        uint64_t checksum = 0;
        netlist.forEachRegularNet([&](eccdb::DesignNetId net, const eccdb::DesignNet&) {
          for (const auto pin : netlist.instancePins(net)) {
            checksum = foldChecksum(checksum, static_cast<uint64_t>(net.packed()) ^ static_cast<uint64_t>(pin.packed()));
          }
        });
        return checksum;
      },
      result.checksum);

  // Check the reverse relation as a multiset, including duplicate/missing
  // entries, after every timer so this does not warm the measured traversals.
  std::vector<std::pair<uint64_t, uint64_t>> expected_connections;
  std::vector<std::pair<uint64_t, uint64_t>> stored_connections;
  expected_connections.reserve(fixture.connections.size());
  stored_connections.reserve(fixture.connections.size());
  for (std::size_t index = 0; index < fixture.connections.size(); ++index) {
    const auto& connection = fixture.connections[index];
    expected_connections.emplace_back(nets[connection.net_index].packed(), connection_pins[index].packed());
  }
  netlist.forEachRegularNet([&](eccdb::DesignNetId net, const eccdb::DesignNet&) {
    for (const auto pin : netlist.instancePins(net)) stored_connections.emplace_back(net.packed(), pin.packed());
  });
  std::sort(expected_connections.begin(), expected_connections.end());
  std::sort(stored_connections.begin(), stored_connections.end());
  require(stored_connections == expected_connections, "design net reverse connection membership mismatch");

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
  output << "{\"schema\":\"db_only_benchmark_v1\",\"backend\":\"eccdb\",\"scope\":"
            "\"native EccDB logical-netlist operations only; fixture JSON parsing, TechStore/LibraryStore/"
            "LibraryCellMaster/LibraryMasterTerm setup, fresh DesignStore setup, validation, and output are "
            "excluded; timed phases create DesignInstance (with automatic DesignInstancePins), create DesignNet, "
            "connect DesignInstancePin (prevalidated IDs), find, and traverse\",\"counts\":{\"masters\":" << counts.masters
         << ",\"instances\":" << counts.instances << ",\"instance_pins\":" << counts.instance_pins << ",\"nets\":" << counts.nets
         << ",\"connections\":" << counts.connections << "},\"runs\":[";
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
    throw std::runtime_error("--repeat must be a positive odd integer");
  }
  return static_cast<std::size_t>(parsed);
}

[[nodiscard]] Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&](std::string_view name) -> std::string_view {
      if (index + 1 >= argc) throw std::runtime_error("missing value for " + std::string(name));
      return argv[++index];
    };
    if (argument == "--fixture") {
      options.fixture = std::string(value(argument));
    } else if (argument == "--repeat") {
      options.repeat = parseRepeat(value(argument));
    } else if (argument == "--output") {
      options.output = std::string(value(argument));
    } else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: eccdb_db_only_benchmark --fixture FIXTURE.json [--repeat ODD_INT] [--output JSON]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(argument));
    }
  }
  if (!std::filesystem::is_regular_file(options.fixture)) {
    throw std::runtime_error("fixture is not a regular file: " + options.fixture.string());
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
  const QueryIndices query_indices = makeQueryIndices(fixture);

  std::vector<Run> runs;
  runs.reserve(options.repeat);
  for (std::size_t index = 0; index < options.repeat; ++index) {
    runs.push_back(runOnce(fixture, counts, library, query_indices));
  }

  if (options.output.empty()) {
    writeJson(std::cout, counts, runs);
    return;
  }
  if (const auto parent = options.output.parent_path(); !parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) throw std::runtime_error("cannot create output directory: " + parent.string());
  }
  std::ofstream output(options.output, std::ios::out | std::ios::trunc);
  if (!output) throw std::runtime_error("cannot open output: " + options.output.string());
  writeJson(output, counts, runs);
  if (!output) throw std::runtime_error("cannot write output: " + options.output.string());
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    run(parseOptions(argc, argv));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "eccdb_db_only_benchmark: " << error.what() << '\n';
    return 1;
  }
}
