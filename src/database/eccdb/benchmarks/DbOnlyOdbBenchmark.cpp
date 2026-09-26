// SPDX-License-Identifier: Apache-2.0

// Native OpenDB counterpart to DbOnlyEccdbBenchmark.  The fixture has already
// reduced a design to masters, instances, nets, and logical connections, so
// neither a LEF nor a DEF parser is involved in any timed operation here.

#include "DbOnlyBenchCommon.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "odb/db.h"
#include "utl/Logger.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t kInstanceQuerySeed = 0x11;
constexpr uint64_t kNetQuerySeed = 0x22;
constexpr uint64_t kConnectionQuerySeed = 0x33;

struct Options
{
  std::filesystem::path fixture;
  std::filesystem::path output;
  uint32_t repeat = 5;
};

struct Counts
{
  uint64_t masters = 0;
  uint64_t instances = 0;
  uint64_t instance_pins = 0;
  uint64_t nets = 0;
  uint64_t connections = 0;
};

struct Timings
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
  Timings timings;
  uint64_t checksum = 0;
};

struct QueryIndices
{
  std::vector<size_t> instances;
  std::vector<size_t> nets;
  std::vector<size_t> connections;
};

struct FixtureLayout
{
  Counts counts;
  // Prefix sums into the conceptual flat instance-pin array.  This array is
  // only used for validation; OpenDB's dbITerm iteration order is unrelated
  // to fixture term order.
  std::vector<size_t> instance_pin_offsets;
  std::vector<std::optional<size_t>> connected_net_by_pin;
};

struct DatabaseDeleter
{
  void operator()(odb::dbDatabase* database) const noexcept
  {
    if (database != nullptr) {
      odb::dbDatabase::destroy(database);
    }
  }
};

struct Design
{
  // OpenDB DB_EDIT debug hooks require an installed logger even when debug
  // output is disabled.  Keep it alive until after the database is destroyed.
  std::unique_ptr<utl::Logger> logger;
  std::unique_ptr<odb::dbDatabase, DatabaseDeleter> database;
  odb::dbLib* library = nullptr;
  odb::dbBlock* block = nullptr;
  std::vector<odb::dbMaster*> masters;
  std::vector<std::vector<odb::dbMTerm*>> master_terms;
  std::vector<odb::dbInst*> instances;
  std::vector<odb::dbNet*> nets;
};

volatile uint64_t g_sink = 0;

[[nodiscard]] uint64_t elapsedNs(const Clock::time_point begin)
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

[[nodiscard]] uint64_t mix(const uint64_t checksum, const uint64_t value)
{
  return checksum ^ (value + UINT64_C(0x9e3779b97f4a7c15) + (checksum << 6) + (checksum >> 2));
}

template <typename Operation>
[[nodiscard]] uint64_t measure(Operation&& operation, uint64_t& checksum)
{
  const auto begin = Clock::now();
  const uint64_t phase_checksum = std::forward<Operation>(operation)();
  const uint64_t elapsed = elapsedNs(begin);
  // Make each timed result observable without charging the volatile update to
  // the timed operation itself.
  g_sink = g_sink ^ phase_checksum;
  checksum = mix(checksum, phase_checksum);
  return elapsed;
}

[[noreturn]] void fail(const std::string& message)
{
  throw std::runtime_error(message);
}

void require(const bool condition, const std::string& message)
{
  if (!condition) {
    fail(message);
  }
}

[[nodiscard]] uint64_t parseRepeat(const std::string_view value)
{
  uint64_t parsed = 0;
  const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (error != std::errc{} || end != value.data() + value.size() || parsed == 0
      || parsed > std::numeric_limits<uint32_t>::max() || (parsed % 2) == 0) {
    fail("--repeat must be a positive odd integer");
  }
  return parsed;
}

[[nodiscard]] Options parseOptions(const int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&](const std::string_view name) -> std::string_view {
      if (index + 1 >= argc) {
        fail("missing value for " + std::string(name));
      }
      return argv[++index];
    };

    if (argument == "--fixture") {
      options.fixture = std::string(value(argument));
    } else if (argument == "--repeat") {
      options.repeat = static_cast<uint32_t>(parseRepeat(value(argument)));
    } else if (argument == "--output") {
      options.output = std::string(value(argument));
    } else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: odb_db_only_benchmark --fixture FIXTURE.json [--repeat ODD] [--output JSON]\n";
      std::exit(0);
    } else {
      fail("unknown argument: " + std::string(argument));
    }
  }

  if (!std::filesystem::is_regular_file(options.fixture)) {
    fail("--fixture must name a regular file");
  }
  return options;
}

void requireUniqueNames(const std::vector<std::string>& names, const std::string_view kind)
{
  std::unordered_set<std::string> seen;
  seen.reserve(names.size());
  for (const std::string& name : names) {
    if (!seen.insert(name).second) {
      fail("fixture contains duplicate " + std::string(kind) + " name: " + name);
    }
  }
}

[[nodiscard]] FixtureLayout makeFixtureLayout(const DbOnlyFixture& fixture)
{
  FixtureLayout layout;
  layout.counts.masters = fixture.masters.size();
  layout.counts.instances = fixture.instances.size();
  layout.counts.nets = fixture.nets.size();
  layout.counts.connections = fixture.connections.size();

  std::vector<std::string> master_names;
  master_names.reserve(fixture.masters.size());
  for (const DbOnlyFixture::Master& master : fixture.masters) {
    master_names.push_back(master.name);
    requireUniqueNames(master.terms, "master-term");
  }
  requireUniqueNames(master_names, "master");
  requireUniqueNames(fixture.nets, "net");

  std::vector<std::string> instance_names;
  instance_names.reserve(fixture.instances.size());
  for (const DbOnlyFixture::Instance& instance : fixture.instances) {
    require(instance.master_index < fixture.masters.size(), "instance references an invalid master index");
    instance_names.push_back(instance.name);
  }
  requireUniqueNames(instance_names, "instance");

  layout.instance_pin_offsets.reserve(fixture.instances.size() + 1);
  layout.instance_pin_offsets.push_back(0);
  for (const DbOnlyFixture::Instance& instance : fixture.instances) {
    const size_t term_count = fixture.masters[instance.master_index].terms.size();
    const size_t previous = layout.instance_pin_offsets.back();
    require(term_count <= std::numeric_limits<size_t>::max() - previous,
            "fixture has too many instance pins");
    layout.instance_pin_offsets.push_back(previous + term_count);
  }
  layout.counts.instance_pins = layout.instance_pin_offsets.back();
  layout.connected_net_by_pin.resize(layout.instance_pin_offsets.back());

  for (const DbOnlyFixture::Connection& connection : fixture.connections) {
    require(connection.net_index < fixture.nets.size(), "connection references an invalid net index");
    require(connection.instance_index < fixture.instances.size(),
            "connection references an invalid instance index");
    const DbOnlyFixture::Instance& instance = fixture.instances[connection.instance_index];
    const auto& terms = fixture.masters[instance.master_index].terms;
    require(connection.term_index < terms.size(), "connection references an invalid master-term index");

    const size_t pin_index = layout.instance_pin_offsets[connection.instance_index] + connection.term_index;
    require(!layout.connected_net_by_pin[pin_index].has_value(),
            "fixture connects one instance pin more than once");
    layout.connected_net_by_pin[pin_index] = connection.net_index;
  }

  return layout;
}

[[nodiscard]] QueryIndices makeQueryIndices(const DbOnlyFixture& fixture)
{
  return QueryIndices{.instances = shuffledIndices(fixture.instances.size(), kInstanceQuerySeed),
                      .nets = shuffledIndices(fixture.nets.size(), kNetQuerySeed),
                      .connections = shuffledIndices(fixture.connections.size(), kConnectionQuerySeed)};
}

[[nodiscard]] Design createDesign(const DbOnlyFixture& fixture)
{
  Design design;
  design.logger = std::make_unique<utl::Logger>();
  design.database.reset(odb::dbDatabase::create());
  require(design.database != nullptr, "OpenDB could not create a database");
  design.database->setLogger(design.logger.get());
  design.database->setDbuPerMicron(1000);

  odb::dbTech* technology = odb::dbTech::create(design.database.get(), "db_only_tech");
  require(technology != nullptr, "OpenDB could not create benchmark technology");
  design.library = odb::dbLib::create(design.database.get(), "db_only_lib", technology);
  require(design.library != nullptr, "OpenDB could not create benchmark library");

  design.masters.reserve(fixture.masters.size());
  design.master_terms.reserve(fixture.masters.size());
  for (const DbOnlyFixture::Master& fixture_master : fixture.masters) {
    odb::dbMaster* master = odb::dbMaster::create(design.library, fixture_master.name.c_str());
    require(master != nullptr, "OpenDB could not create master: " + fixture_master.name);

    std::vector<odb::dbMTerm*> terms;
    terms.reserve(fixture_master.terms.size());
    for (const std::string& term_name : fixture_master.terms) {
      odb::dbMTerm* term = odb::dbMTerm::create(
          master, term_name.c_str(), odb::dbIoType::INOUT, odb::dbSigType::SIGNAL);
      require(term != nullptr, "OpenDB could not create master term: " + term_name);
      terms.push_back(term);
    }
    // dbInst construction derives its automatic ITerm array from the frozen
    // MTerm order.  Keep the direct pointers so no benchmark assumes that
    // OpenDB's linked iteration order matches the fixture order.
    master->setFrozen();
    design.masters.push_back(master);
    design.master_terms.push_back(std::move(terms));
  }

  odb::dbChip* chip = odb::dbChip::create(design.database.get(), technology, "db_only_chip");
  require(chip != nullptr, "OpenDB could not create benchmark chip");
  design.block = odb::dbBlock::create(chip, "db_only_block");
  require(design.block != nullptr, "OpenDB could not create benchmark block");
  return design;
}

void validateCreatedInstances(const DbOnlyFixture& fixture, const Design& design)
{
  require(design.instances.size() == fixture.instances.size(), "OpenDB instance creation count differs from fixture");
  for (size_t instance_index = 0; instance_index < fixture.instances.size(); ++instance_index) {
    odb::dbInst* instance = design.instances[instance_index];
    require(instance != nullptr, "OpenDB could not create a fixture instance");
    require(instance->getMaster() == design.masters[fixture.instances[instance_index].master_index],
            "OpenDB instance has an unexpected master");
  }
}

[[nodiscard]] std::vector<odb::dbITerm*> cacheConnectionPins(const DbOnlyFixture& fixture, const Design& design)
{
  std::vector<odb::dbITerm*> result;
  result.reserve(fixture.connections.size());
  for (const DbOnlyFixture::Connection& connection : fixture.connections) {
    const DbOnlyFixture::Instance& instance = fixture.instances[connection.instance_index];
    odb::dbITerm* pin = design.instances[connection.instance_index]->getITerm(
        design.master_terms[instance.master_index][connection.term_index]);
    require(pin != nullptr, "OpenDB could not find an automatic instance pin");
    result.push_back(pin);
  }
  return result;
}

void validateCreatedNets(const DbOnlyFixture& fixture, const Design& design)
{
  require(design.nets.size() == fixture.nets.size(), "OpenDB net creation count differs from fixture");
  for (odb::dbNet* net : design.nets) {
    require(net != nullptr, "OpenDB could not create a fixture net");
  }
}

void validateDesign(const DbOnlyFixture& fixture, const FixtureLayout& layout, const Design& design)
{
  uint64_t master_count = 0;
  for (odb::dbMaster* master : design.library->getMasters()) {
    ++master_count;
    require(master->isFrozen(), "OpenDB benchmark master was not frozen");
  }
  require(master_count == layout.counts.masters, "OpenDB master count differs from fixture");

  uint64_t instance_count = 0;
  for (odb::dbInst* instance : design.block->getInsts()) {
    static_cast<void>(instance);
    ++instance_count;
  }
  require(instance_count == layout.counts.instances, "OpenDB instance count differs from fixture");

  uint64_t net_count = 0;
  for (odb::dbNet* net : design.block->getNets()) {
    static_cast<void>(net);
    ++net_count;
  }
  require(net_count == layout.counts.nets, "OpenDB net count differs from fixture");

  uint64_t instance_pin_count = 0;
  for (odb::dbITerm* pin : design.block->getITerms()) {
    static_cast<void>(pin);
    ++instance_pin_count;
  }
  require(instance_pin_count == layout.counts.instance_pins,
          "OpenDB automatic instance-pin count differs from fixture");

  for (size_t instance_index = 0; instance_index < fixture.instances.size(); ++instance_index) {
    const DbOnlyFixture::Instance& fixture_instance = fixture.instances[instance_index];
    odb::dbInst* instance = design.instances[instance_index];
    require(instance->getMaster() == design.masters[fixture_instance.master_index],
            "OpenDB instance has an unexpected master");

    for (size_t term_index = 0; term_index < fixture.masters[fixture_instance.master_index].terms.size(); ++term_index) {
      odb::dbMTerm* master_term = design.master_terms[fixture_instance.master_index][term_index];
      odb::dbITerm* instance_term = instance->getITerm(master_term);
      require(instance_term != nullptr, "OpenDB instance is missing an automatic instance pin");
      require(instance_term->getInst() == instance && instance_term->getMTerm() == master_term,
              "OpenDB instance pin does not retain its instance/master-term relation");

      const size_t pin_index = layout.instance_pin_offsets[instance_index] + term_index;
      const std::optional<size_t> expected_net = layout.connected_net_by_pin[pin_index];
      odb::dbNet* actual_net = instance_term->getNet();
      if (expected_net.has_value()) {
        require(actual_net == design.nets[*expected_net],
                "OpenDB instance pin does not retain its expected net connection");
      } else {
        require(actual_net == nullptr, "OpenDB has an unexpected instance-pin connection");
      }
    }
  }

  uint64_t connected_pin_count = 0;
  for (odb::dbNet* net : design.block->getNets()) {
    for (odb::dbITerm* pin : net->getITerms()) {
      require(pin->getNet() == net, "OpenDB net traversal returned a pin on a different net");
      ++connected_pin_count;
    }
  }
  require(connected_pin_count == layout.counts.connections,
          "OpenDB connected instance-pin count differs from fixture");

  // Validate each fixture relation through direct references rather than any
  // dbSet order.  OpenDB may order ITerms differently from their MTerms.
  for (const DbOnlyFixture::Connection& connection : fixture.connections) {
    const DbOnlyFixture::Instance& fixture_instance = fixture.instances[connection.instance_index];
    odb::dbInst* instance = design.instances[connection.instance_index];
    odb::dbMTerm* master_term = design.master_terms[fixture_instance.master_index][connection.term_index];
    odb::dbITerm* instance_term = instance->getITerm(master_term);
    require(instance_term != nullptr && instance_term->getNet() == design.nets[connection.net_index],
            "OpenDB fixture connection validation failed");
  }
}

void validateQueries(const DbOnlyFixture& fixture,
                     const QueryIndices& queries,
                     const Design& design)
{
  for (const size_t instance_index : queries.instances) {
    require(design.block->findInst(fixture.instances[instance_index].name.c_str()) == design.instances[instance_index],
            "OpenDB pre-timing findInst validation failed");
  }
  for (const size_t net_index : queries.nets) {
    require(design.block->findNet(fixture.nets[net_index].c_str()) == design.nets[net_index],
            "OpenDB pre-timing findNet validation failed");
  }
  for (const size_t connection_index : queries.connections) {
    const DbOnlyFixture::Connection& connection = fixture.connections[connection_index];
    const DbOnlyFixture::Instance& instance = fixture.instances[connection.instance_index];
    odb::dbMTerm* master_term = design.master_terms[instance.master_index][connection.term_index];
    require(design.instances[connection.instance_index]->findITerm(master_term->getConstName())
                == design.instances[connection.instance_index]->getITerm(master_term),
            "OpenDB pre-timing findITerm validation failed");
  }
}

[[nodiscard]] uint64_t pointerValue(const void* value)
{
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}

[[nodiscard]] Run measureRun(const DbOnlyFixture& fixture,
                             const FixtureLayout& layout,
                             const QueryIndices& queries)
{
  // Database/library/master construction and this fresh chip/block are setup,
  // not a timed phase.  Recreating them per run prevents a previous run's
  // object tables and name hashes from changing a later measurement.
  Design design = createDesign(fixture);
  design.instances.reserve(fixture.instances.size());
  design.nets.reserve(fixture.nets.size());

  Run run;
  run.timings.create_instances_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (const DbOnlyFixture::Instance& fixture_instance : fixture.instances) {
      odb::dbInst* instance = odb::dbInst::create(
          design.block, design.masters[fixture_instance.master_index], fixture_instance.name.c_str());
      design.instances.push_back(instance);
      phase_checksum = mix(phase_checksum, pointerValue(instance));
    }
    return phase_checksum;
  }, run.checksum);

  validateCreatedInstances(fixture, design);
  // Match the EccDB benchmark's cached DesignInstancePin IDs: getITerm is
  // setup here, so connect_ns measures dbITerm::connect alone.
  const std::vector<odb::dbITerm*> connection_pins = cacheConnectionPins(fixture, design);

  run.timings.create_nets_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (const std::string& name : fixture.nets) {
      odb::dbNet* net = odb::dbNet::create(design.block, name.c_str());
      design.nets.push_back(net);
      phase_checksum = mix(phase_checksum, pointerValue(net));
    }
    return phase_checksum;
  }, run.checksum);

  validateCreatedNets(fixture, design);

  run.timings.connect_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (size_t connection_index = 0; connection_index < fixture.connections.size(); ++connection_index) {
      const DbOnlyFixture::Connection& connection = fixture.connections[connection_index];
      odb::dbITerm* pin = connection_pins[connection_index];
      pin->connect(design.nets[connection.net_index]);
      phase_checksum = mix(phase_checksum, pointerValue(pin) ^ pointerValue(design.nets[connection.net_index]));
    }
    return phase_checksum;
  }, run.checksum);

  validateDesign(fixture, layout, design);
  validateQueries(fixture, queries, design);

  run.timings.find_instances_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (const size_t instance_index : queries.instances) {
      odb::dbInst* instance = design.block->findInst(fixture.instances[instance_index].name.c_str());
      phase_checksum = mix(phase_checksum, pointerValue(instance));
    }
    return phase_checksum;
  }, run.checksum);

  run.timings.find_nets_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (const size_t net_index : queries.nets) {
      odb::dbNet* net = design.block->findNet(fixture.nets[net_index].c_str());
      phase_checksum = mix(phase_checksum, pointerValue(net));
    }
    return phase_checksum;
  }, run.checksum);

  run.timings.find_pins_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (const size_t connection_index : queries.connections) {
      const DbOnlyFixture::Connection& connection = fixture.connections[connection_index];
      const DbOnlyFixture::Instance& fixture_instance = fixture.instances[connection.instance_index];
      odb::dbMTerm* master_term = design.master_terms[fixture_instance.master_index][connection.term_index];
      odb::dbITerm* pin = design.instances[connection.instance_index]->findITerm(master_term->getConstName());
      phase_checksum = mix(phase_checksum, pointerValue(pin));
    }
    return phase_checksum;
  }, run.checksum);

  run.timings.traverse_instances_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (odb::dbInst* instance : design.block->getInsts()) {
      phase_checksum = mix(phase_checksum, pointerValue(instance));
    }
    return phase_checksum;
  }, run.checksum);

  run.timings.traverse_nets_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (odb::dbNet* net : design.block->getNets()) {
      phase_checksum = mix(phase_checksum, pointerValue(net));
    }
    return phase_checksum;
  }, run.checksum);

  run.timings.traverse_net_pins_ns = measure([&] {
    uint64_t phase_checksum = 0;
    for (odb::dbNet* net : design.block->getNets()) {
      for (odb::dbITerm* pin : net->getITerms()) {
        phase_checksum = mix(phase_checksum, pointerValue(net) ^ pointerValue(pin));
      }
    }
    return phase_checksum;
  }, run.checksum);
  return run;
}

template <typename Member>
[[nodiscard]] uint64_t median(const std::vector<Run>& runs, Member member)
{
  std::vector<uint64_t> values;
  values.reserve(runs.size());
  for (const Run& run : runs) {
    values.push_back(run.timings.*member);
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

[[nodiscard]] Timings medianTimings(const std::vector<Run>& runs)
{
  require(!runs.empty(), "cannot summarize zero benchmark runs");
  return Timings{.create_instances_ns = median(runs, &Timings::create_instances_ns),
                 .create_nets_ns = median(runs, &Timings::create_nets_ns),
                 .connect_ns = median(runs, &Timings::connect_ns),
                 .find_instances_ns = median(runs, &Timings::find_instances_ns),
                 .find_nets_ns = median(runs, &Timings::find_nets_ns),
                 .find_pins_ns = median(runs, &Timings::find_pins_ns),
                 .traverse_instances_ns = median(runs, &Timings::traverse_instances_ns),
                 .traverse_nets_ns = median(runs, &Timings::traverse_nets_ns),
                 .traverse_net_pins_ns = median(runs, &Timings::traverse_net_pins_ns)};
}

void writeTimings(std::ostream& output, const Timings& timings)
{
  output << "{\"create_instances_ns\":" << timings.create_instances_ns
         << ",\"create_nets_ns\":" << timings.create_nets_ns << ",\"connect_ns\":" << timings.connect_ns
         << ",\"find_instances_ns\":" << timings.find_instances_ns << ",\"find_nets_ns\":" << timings.find_nets_ns
         << ",\"find_pins_ns\":" << timings.find_pins_ns
         << ",\"traverse_instances_ns\":" << timings.traverse_instances_ns
         << ",\"traverse_nets_ns\":" << timings.traverse_nets_ns
         << ",\"traverse_net_pins_ns\":" << timings.traverse_net_pins_ns << '}';
}

void writeCounts(std::ostream& output, const Counts& counts)
{
  output << "{\"masters\":" << counts.masters << ",\"instances\":" << counts.instances
         << ",\"instance_pins\":" << counts.instance_pins << ",\"nets\":" << counts.nets
         << ",\"connections\":" << counts.connections << '}';
}

void writeJson(std::ostream& output,
               const FixtureLayout& layout,
               const std::vector<Run>& runs,
               const Timings& medians)
{
  output << "{\"schema\":\"db_only_benchmark_v1\",\"backend\":\"odb\",\"scope\":"
            "\"logical instances/nets/instance-pin connections only; no LEF/DEF parse, fixture loading, "
            "database/library/master setup, chip/block setup, validation, BTerms, placement, or wire geometry; "
            "timed phases create dbInst (with automatic ITerms), create dbNet, connect dbITerm, find, and traverse\","
            "\"counts\":";
  writeCounts(output, layout.counts);
  output << ",\"runs\":[";
  for (size_t index = 0; index < runs.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    writeTimings(output, runs[index].timings);
  }
  output << "],\"median\":";
  writeTimings(output, medians);
  output << ",\"checksums\":[";
  for (size_t index = 0; index < runs.size(); ++index) {
    if (index != 0) {
      output << ',';
    }
    output << runs[index].checksum;
  }
  output << "]}\n";
}

void run(const int argc, char** argv)
{
  const Options options = parseOptions(argc, argv);
  const DbOnlyFixture fixture = DbOnlyFixture::load(options.fixture);
  const FixtureLayout layout = makeFixtureLayout(fixture);
  const QueryIndices queries = makeQueryIndices(fixture);

  std::vector<Run> runs;
  runs.reserve(options.repeat);
  for (uint32_t run_index = 0; run_index < options.repeat; ++run_index) {
    runs.push_back(measureRun(fixture, layout, queries));
  }
  const Timings medians = medianTimings(runs);

  if (options.output.empty()) {
    writeJson(std::cout, layout, runs, medians);
    return;
  }

  if (const std::filesystem::path parent = options.output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(options.output, std::ios::out | std::ios::trunc);
  if (!output) {
    fail("cannot open output: " + options.output.string());
  }
  writeJson(output, layout, runs, medians);
  if (!output) {
    fail("cannot write output: " + options.output.string());
  }
}

}  // namespace

int main(int argc, char** argv)
{
  try {
    run(argc, argv);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "odb_db_only_benchmark: " << error.what() << '\n';
    return 1;
  }
}
