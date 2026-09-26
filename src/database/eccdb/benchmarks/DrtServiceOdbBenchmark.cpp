// SPDX-License-Identifier: Apache-2.0
//
// Native OpenDB side of the TritonRoute-facing service benchmark.  Parsing and
// ODB snapshot loading happen before every timed run.  Timed work therefore
// measures the ODB operations an adapter would perform after its design is
// available, rather than LEF/DEF parsing or ODB deserialization.

#include "DbOnlyBenchCommon.h"

#include <algorithm>
#include <array>
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
#include "odb/dbShape.h"
#include "utl/Logger.h"

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t kInstanceQuerySeed = 0x11;
constexpr uint64_t kNetQuerySeed = 0x22;
constexpr uint64_t kPinQuerySeed = 0x33;

struct Options
{
  std::filesystem::path preroute_odb;
  std::filesystem::path routed_odb;
  std::filesystem::path fixture;
  std::filesystem::path output;
  std::filesystem::path export_guides;
  uint32_t repeat = 1;
  bool pins_only = false;
};

// `connected_iterms` deliberately means ITerms attached to regular nets.  It
// is the same logical scope as DbOnlyFixture::connections; ITerms on special
// nets are retained in ODB but not represented by that fixture.
struct Counts
{
  uint64_t instances = 0;
  uint64_t regular_nets = 0;
  uint64_t instance_pins = 0;
  uint64_t connected_iterms = 0;
  uint64_t io_pins = 0;
  uint64_t guides = 0;
  uint64_t routed_nets = 0;
  uint64_t route_shapes = 0;
  uint64_t route_vias = 0;
};

struct Timings
{
  uint64_t scan_regular_nets_ns = 0;
  uint64_t find_instances_ns = 0;
  uint64_t find_nets_ns = 0;
  uint64_t find_pins_ns = 0;
  uint64_t scan_net_pins_ns = 0;
  uint64_t scan_guides_ns = 0;
  uint64_t decode_routes_ns = 0;
  uint64_t replace_guides_ns = 0;
};

struct Run
{
  Timings timings;
  uint64_t checksum = 0;
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

struct Snapshot
{
  // Keep the logger alive after the database's destruction: OpenDB's DB_EDIT
  // hooks may consult it while objects are being deleted.
  std::unique_ptr<utl::Logger> logger;
  std::unique_ptr<odb::dbDatabase, DatabaseDeleter> database;
  odb::dbBlock* block = nullptr;
};

struct GuideValue
{
  odb::dbTechLayer* layer = nullptr;
  odb::dbTechLayer* via_layer = nullptr;
  odb::Rect box;
  uint32_t flags = 0;
};

struct GuideNetCache
{
  odb::dbNet* net = nullptr;
  std::vector<GuideValue> guides;
};

struct PinQuery
{
  odb::dbInst* instance = nullptr;
  const char* master_term_name = nullptr;
  odb::dbITerm* expected = nullptr;
};

struct RouteStats
{
  uint64_t routed_nets = 0;
  uint64_t shapes = 0;
  uint64_t vias = 0;
};

struct CachedDesign
{
  Counts counts;
  std::vector<odb::dbInst*> instances_by_fixture;
  std::vector<odb::dbNet*> routed_nets_by_fixture;
  std::vector<odb::dbNet*> preroute_nets_by_fixture;
  std::vector<PinQuery> pin_queries;
  std::vector<size_t> instance_query_order;
  std::vector<size_t> net_query_order;
  std::vector<size_t> pin_query_order;
  std::vector<GuideNetCache> guide_nets;
};

volatile uint64_t g_sink = 0;

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

[[nodiscard]] uint64_t elapsedNs(const Clock::time_point begin)
{
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

[[nodiscard]] uint64_t mix(const uint64_t checksum, const uint64_t value)
{
  return checksum ^ (value + UINT64_C(0x9e3779b97f4a7c15) + (checksum << 6) + (checksum >> 2));
}

[[nodiscard]] uint64_t pointerValue(const void* value)
{
  return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(value));
}

[[nodiscard]] uint64_t signedValue(const int value)
{
  return static_cast<uint64_t>(static_cast<int64_t>(value));
}

[[nodiscard]] uint64_t rectangleValue(const odb::Rect& box)
{
  uint64_t value = 0;
  value = mix(value, signedValue(box.xMin()));
  value = mix(value, signedValue(box.yMin()));
  value = mix(value, signedValue(box.xMax()));
  return mix(value, signedValue(box.yMax()));
}

[[nodiscard]] uint64_t hashText(const std::string_view value)
{
  uint64_t hash = UINT64_C(1469598103934665603);
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

template <typename Operation>
[[nodiscard]] uint64_t measure(Operation&& operation, uint64_t& checksum)
{
  const auto begin = Clock::now();
  const uint64_t phase_checksum = std::forward<Operation>(operation)();
  const uint64_t elapsed = elapsedNs(begin);
  // Do not charge the volatile store to the measured phase, while still
  // making all reads in the phase observable to the optimizer.
  g_sink = g_sink ^ phase_checksum;
  checksum = mix(checksum, phase_checksum);
  return elapsed;
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

    if (argument == "--preroute-odb") {
      options.preroute_odb = std::string(value(argument));
    } else if (argument == "--routed-odb") {
      options.routed_odb = std::string(value(argument));
    } else if (argument == "--fixture") {
      options.fixture = std::string(value(argument));
    } else if (argument == "--repeat") {
      options.repeat = static_cast<uint32_t>(parseRepeat(value(argument)));
    } else if (argument == "--pins-only") {
      options.pins_only = true;
    } else if (argument == "--output") {
      options.output = std::string(value(argument));
    } else if (argument == "--export-guides") {
      options.export_guides = std::string(value(argument));
    } else if (argument == "--help" || argument == "-h") {
      std::cout
          << "usage: odb_drt_service_benchmark --preroute-odb PRE.odb --routed-odb ROUTED.odb "
             "--fixture FIXTURE.json [--repeat ODD] [--pins-only] [--output JSON]\n"
             "       odb_drt_service_benchmark --preroute-odb PRE.odb --export-guides GUIDES.jsonl\n";
      std::exit(0);
    } else {
      fail("unknown argument: " + std::string(argument));
    }
  }

  if (!std::filesystem::is_regular_file(options.preroute_odb)) {
    fail("--preroute-odb must name a regular file");
  }
  if (!options.export_guides.empty()) {
    if (!options.output.empty()) {
      fail("--output cannot be combined with --export-guides");
    }
    return options;
  }
  if (!std::filesystem::is_regular_file(options.routed_odb)) {
    fail("--routed-odb must name a regular file");
  }
  if (!std::filesystem::is_regular_file(options.fixture)) {
    fail("--fixture must name a regular file");
  }
  return options;
}

[[nodiscard]] Snapshot loadSnapshot(const std::filesystem::path& path, const std::string_view label)
{
  Snapshot snapshot;
  snapshot.logger = std::make_unique<utl::Logger>();
  snapshot.database.reset(odb::dbDatabase::create());
  require(snapshot.database != nullptr, "OpenDB could not create " + std::string(label) + " database");

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    fail("cannot read " + std::string(label) + " ODB snapshot: " + path.string());
  }
  snapshot.database->read(input);
  snapshot.database->setLogger(snapshot.logger.get());
  odb::dbChip* chip = snapshot.database->getChip();
  require(chip != nullptr, std::string(label) + " ODB snapshot has no chip");
  snapshot.block = chip->getBlock();
  require(snapshot.block != nullptr, std::string(label) + " ODB snapshot has no top block");
  return snapshot;
}

[[nodiscard]] std::vector<odb::dbNet*> sortedRegularNets(odb::dbBlock& block)
{
  std::vector<odb::dbNet*> nets;
  for (odb::dbNet* net : block.getNets()) {
    if (!net->isSpecial()) {
      nets.push_back(net);
    }
  }
  std::sort(nets.begin(), nets.end(), [](const odb::dbNet* lhs, const odb::dbNet* rhs) {
    return std::string_view(lhs->getConstName()) < std::string_view(rhs->getConstName());
  });
  for (size_t index = 1; index < nets.size(); ++index) {
    require(std::string_view(nets[index - 1]->getConstName()) != std::string_view(nets[index]->getConstName()),
            "OpenDB snapshot has duplicate regular-net names");
  }
  return nets;
}

[[nodiscard]] uint64_t countBlockIterms(odb::dbBlock& block)
{
  uint64_t count = 0;
  for (odb::dbITerm* pin : block.getITerms()) {
    static_cast<void>(pin);
    ++count;
  }
  return count;
}

[[nodiscard]] uint64_t countBlockBterms(odb::dbBlock& block)
{
  uint64_t count = 0;
  for (odb::dbBTerm* pin : block.getBTerms()) {
    static_cast<void>(pin);
    ++count;
  }
  return count;
}

[[nodiscard]] std::vector<std::optional<size_t>> expectedRegularNetsByFixturePin(const DbOnlyFixture& fixture)
{
  std::vector<std::optional<size_t>> result;
  result.reserve(fixture.instances.size() * 4);
  std::vector<size_t> offsets;
  offsets.reserve(fixture.instances.size() + 1);
  offsets.push_back(0);
  for (const DbOnlyFixture::Instance& instance : fixture.instances) {
    offsets.push_back(offsets.back() + fixture.masters[instance.master_index].terms.size());
  }
  result.resize(offsets.back());
  for (const DbOnlyFixture::Connection& connection : fixture.connections) {
    const size_t index = offsets[connection.instance_index] + connection.term_index;
    require(!result[index].has_value(), "fixture connects an instance pin more than once");
    result[index] = connection.net_index;
  }
  return result;
}

struct FixtureSnapshotValidation
{
  std::vector<odb::dbInst*> instances;
  std::vector<odb::dbNet*> regular_nets;
  std::vector<PinQuery> pins;
  uint64_t connected_regular_iterms = 0;
  uint64_t io_pins = 0;
};

[[nodiscard]] FixtureSnapshotValidation validateSnapshotAgainstFixture(const Snapshot& snapshot,
                                                                        const DbOnlyFixture& fixture,
                                                                        const std::string_view label)
{
  odb::dbBlock& block = *snapshot.block;
  FixtureSnapshotValidation result;
  const std::vector<std::optional<size_t>> expected_nets = expectedRegularNetsByFixturePin(fixture);

  result.instances.reserve(fixture.instances.size());
  result.regular_nets.reserve(fixture.nets.size());
  size_t expected_pin_count = 0;
  for (const DbOnlyFixture::Instance& instance : fixture.instances) {
    expected_pin_count += fixture.masters[instance.master_index].terms.size();
  }
  result.pins.reserve(expected_pin_count);

  require(static_cast<uint64_t>(fixture.instances.size()) == [&] {
            uint64_t count = 0;
            for (odb::dbInst* instance : block.getInsts()) {
              static_cast<void>(instance);
              ++count;
            }
            return count;
          }(),
          std::string(label) + " ODB instance count differs from the fixture");

  const std::vector<odb::dbNet*> actual_regular_nets = sortedRegularNets(block);
  require(actual_regular_nets.size() == fixture.nets.size(),
          std::string(label) + " ODB regular-net count differs from the fixture");

  for (size_t net_index = 0; net_index < fixture.nets.size(); ++net_index) {
    odb::dbNet* net = block.findNet(fixture.nets[net_index].c_str());
    require(net != nullptr && !net->isSpecial(),
            std::string(label) + " ODB is missing regular net " + fixture.nets[net_index]);
    require(net == actual_regular_nets[net_index],
            std::string(label) + " ODB regular-net names differ from the fixture");
    result.regular_nets.push_back(net);
  }

  size_t flat_pin_index = 0;
  for (size_t instance_index = 0; instance_index < fixture.instances.size(); ++instance_index) {
    const DbOnlyFixture::Instance& fixture_instance = fixture.instances[instance_index];
    const DbOnlyFixture::Master& fixture_master = fixture.masters[fixture_instance.master_index];
    odb::dbInst* instance = block.findInst(fixture_instance.name.c_str());
    require(instance != nullptr, std::string(label) + " ODB is missing instance " + fixture_instance.name);
    odb::dbMaster* master = instance->getMaster();
    require(master != nullptr && std::string_view(master->getConstName()) == fixture_master.name,
            std::string(label) + " ODB instance has an unexpected master: " + fixture_instance.name);

    std::unordered_set<std::string_view> actual_term_names;
    for (odb::dbMTerm* term : master->getMTerms()) {
      require(term != nullptr, std::string(label) + " ODB master has a null MTerm");
      actual_term_names.emplace(term->getConstName());
    }
    require(actual_term_names.size() == fixture_master.terms.size(),
            std::string(label) + " ODB master-term count differs for " + fixture_master.name);

    result.instances.push_back(instance);
    for (const std::string& term_name : fixture_master.terms) {
      require(actual_term_names.contains(term_name),
              std::string(label) + " ODB master is missing MTerm " + fixture_master.name + "/" + term_name);
      odb::dbITerm* pin = instance->findITerm(term_name.c_str());
      require(pin != nullptr, std::string(label) + " ODB instance is missing ITerm " + fixture_instance.name + "/" + term_name);
      require(pin->getInst() == instance && pin->getMTerm() != nullptr
                  && std::string_view(pin->getMTerm()->getConstName()) == term_name,
              std::string(label) + " ODB ITerm relation differs for " + fixture_instance.name + "/" + term_name);

      const std::optional<size_t>& expected_net = expected_nets[flat_pin_index++];
      odb::dbNet* actual_net = pin->getNet();
      if (expected_net.has_value()) {
        require(actual_net == result.regular_nets[*expected_net],
                std::string(label) + " ODB regular ITerm connection differs for " + fixture_instance.name + "/" + term_name);
      } else if (actual_net != nullptr) {
        require(actual_net->isSpecial(),
                std::string(label) + " ODB has a regular ITerm connection absent from the fixture");
      }
      result.pins.push_back({.instance = instance, .master_term_name = term_name.c_str(), .expected = pin});
    }
  }
  require(flat_pin_index == expected_nets.size(), "internal fixture pin flattening mismatch");
  require(countBlockIterms(block) == result.pins.size(),
          std::string(label) + " ODB ITerm count differs from the fixture masters");

  uint64_t connected_regular_iterms = 0;
  for (odb::dbNet* net : result.regular_nets) {
    for (odb::dbITerm* pin : net->getITerms()) {
      require(pin != nullptr && pin->getNet() == net,
              std::string(label) + " ODB regular net contains an invalid ITerm relation");
      ++connected_regular_iterms;
    }
  }
  require(connected_regular_iterms == fixture.connections.size(),
          std::string(label) + " ODB regular ITerm connection count differs from the fixture");
  result.connected_regular_iterms = connected_regular_iterms;
  result.io_pins = countBlockBterms(block);
  return result;
}

[[nodiscard]] uint32_t guideFlags(const odb::dbGuide& guide)
{
  uint32_t flags = 0;
  if (guide.isCongested()) {
    flags |= 1U;
  }
  if (guide.isJumper()) {
    flags |= 2U;
  }
  if (guide.isConnectedToTerm()) {
    flags |= 4U;
  }
  return flags;
}

[[nodiscard]] GuideValue guideValue(const odb::dbGuide& guide)
{
  odb::dbTechLayer* layer = guide.getLayer();
  require(layer != nullptr, "OpenDB guide has no routing layer");
  return {.layer = layer, .via_layer = guide.getViaLayer(), .box = guide.getBox(), .flags = guideFlags(guide)};
}

[[nodiscard]] bool sameGuide(const odb::dbGuide& guide, const GuideValue& value)
{
  return guide.getLayer() == value.layer && guide.getViaLayer() == value.via_layer && guide.getBox() == value.box
         && guideFlags(guide) == value.flags;
}

[[nodiscard]] std::vector<GuideNetCache> cacheGuides(const std::vector<odb::dbNet*>& nets)
{
  std::vector<GuideNetCache> result;
  result.reserve(nets.size());
  for (odb::dbNet* net : nets) {
    GuideNetCache cached{.net = net, .guides = {}};
    cached.guides.reserve(net->getGuides().size());
    for (odb::dbGuide* guide : net->getGuides()) {
      require(guide != nullptr && guide->getNet() == net, "OpenDB guide has an invalid owner net");
      cached.guides.push_back(guideValue(*guide));
    }
    result.push_back(std::move(cached));
  }
  return result;
}

[[nodiscard]] uint64_t guideCount(const std::vector<GuideNetCache>& guide_nets)
{
  uint64_t count = 0;
  for (const GuideNetCache& net : guide_nets) {
    count += net.guides.size();
  }
  return count;
}

void validateGuideCache(const std::vector<GuideNetCache>& guide_nets)
{
  for (const GuideNetCache& net_cache : guide_nets) {
    require(net_cache.net != nullptr, "cached OpenDB guide net is null");
    const odb::dbSet<odb::dbGuide> guides = net_cache.net->getGuides();
    auto actual = guides.begin();
    const auto end = guides.end();
    for (const GuideValue& expected : net_cache.guides) {
      require(actual != end, "OpenDB guide replacement lost a guide");
      require(*actual != nullptr && sameGuide(**actual, expected),
              "OpenDB guide replacement changed guide values or ordering");
      ++actual;
    }
    require(actual == end, "OpenDB guide replacement created an unexpected guide");
  }
}

[[nodiscard]] RouteStats countRoutes(const std::vector<odb::dbNet*>& regular_nets)
{
  RouteStats result;
  odb::dbWireShapeItr iterator;
  for (odb::dbNet* net : regular_nets) {
    odb::dbWire* wire = net->getWire();
    if (wire == nullptr) {
      continue;
    }
    ++result.routed_nets;
    odb::dbShape shape;
    for (iterator.begin(wire); iterator.next(shape);) {
      ++result.shapes;
      if (shape.isVia()) {
        ++result.vias;
      }
    }
  }
  return result;
}

[[nodiscard]] CachedDesign buildCache(const Snapshot& preroute,
                                      const Snapshot& routed,
                                      const DbOnlyFixture& fixture)
{
  const FixtureSnapshotValidation pre_validation = validateSnapshotAgainstFixture(preroute, fixture, "pre-route");
  const FixtureSnapshotValidation routed_validation = validateSnapshotAgainstFixture(routed, fixture, "routed");

  require(pre_validation.instances.size() == routed_validation.instances.size()
              && pre_validation.regular_nets.size() == routed_validation.regular_nets.size()
              && pre_validation.pins.size() == routed_validation.pins.size(),
          "pre-route and routed ODB logical data differs");
  require(pre_validation.connected_regular_iterms == routed_validation.connected_regular_iterms,
          "pre-route and routed ODB regular ITerm counts differ");
  require(pre_validation.io_pins == routed_validation.io_pins,
          "pre-route and routed ODB IO-pin counts differ");

  CachedDesign cache;
  cache.instances_by_fixture = routed_validation.instances;
  cache.routed_nets_by_fixture = routed_validation.regular_nets;
  cache.preroute_nets_by_fixture = pre_validation.regular_nets;
  cache.pin_queries = routed_validation.pins;
  cache.instance_query_order = shuffledIndices(cache.instances_by_fixture.size(), kInstanceQuerySeed);
  cache.net_query_order = shuffledIndices(cache.routed_nets_by_fixture.size(), kNetQuerySeed);
  cache.pin_query_order = shuffledIndices(cache.pin_queries.size(), kPinQuerySeed);
  cache.guide_nets = cacheGuides(cache.preroute_nets_by_fixture);

  const RouteStats routes = countRoutes(cache.routed_nets_by_fixture);
  cache.counts.instances = cache.instances_by_fixture.size();
  cache.counts.regular_nets = cache.routed_nets_by_fixture.size();
  cache.counts.instance_pins = cache.pin_queries.size();
  cache.counts.connected_iterms = routed_validation.connected_regular_iterms;
  cache.counts.io_pins = routed_validation.io_pins;
  cache.counts.guides = guideCount(cache.guide_nets);
  cache.counts.routed_nets = routes.routed_nets;
  cache.counts.route_shapes = routes.shapes;
  cache.counts.route_vias = routes.vias;
  return cache;
}

void createGuide(const GuideNetCache& net_cache, const GuideValue& value)
{
  // dbGuide::create requires a via-layer pointer.  Current OpenDB snapshots
  // produced by TritonRoute always have one; fail rather than silently turn a
  // missing layer into a different guide representation.
  require(value.layer != nullptr && value.via_layer != nullptr,
          "OpenDB cannot recreate a guide with a null layer or via_layer");
  odb::dbGuide* guide = odb::dbGuide::create(
      net_cache.net, value.layer, value.via_layer, value.box, (value.flags & 1U) != 0);
  require(guide != nullptr, "OpenDB could not recreate a guide");
  guide->setIsJumper((value.flags & 2U) != 0);
  guide->setIsConnectedToTerm((value.flags & 4U) != 0);
}

[[nodiscard]] uint64_t scanGuideValue(const odb::dbGuide& guide)
{
  uint64_t value = pointerValue(&guide);
  value = mix(value, rectangleValue(guide.getBox()));
  value = mix(value, pointerValue(guide.getLayer()));
  value = mix(value, pointerValue(guide.getViaLayer()));
  return mix(value, guideFlags(guide));
}

[[nodiscard]] uint64_t decodeRoutes(const CachedDesign& cache, RouteStats& statistics)
{
  // ODB's native representation is a compressed opcode stream.  Materialize
  // each wire into a vector here so this cost includes the analogous per-net
  // shape-vector construction performed by NetRouteService::snapshot.  The
  // vector dies after the current net, matching a wrapper that consumes each
  // snapshot before requesting the next one rather than retaining the entire
  // routed design at once.
  uint64_t checksum = 0;
  odb::dbWireShapeItr iterator;
  for (odb::dbNet* net : cache.routed_nets_by_fixture) {
    odb::dbWire* wire = net->getWire();
    if (wire == nullptr) {
      continue;
    }
    std::vector<odb::dbShape> shapes;
    odb::dbShape shape;
    for (iterator.begin(wire); iterator.next(shape);) {
      shapes.push_back(shape);
      ++statistics.shapes;
      checksum = mix(checksum, rectangleValue(shape.getBox()));
      if (shape.isVia()) {
        ++statistics.vias;
        checksum = mix(checksum, pointerValue(shape.getTechVia()));
        checksum = mix(checksum, pointerValue(shape.getVia()));
      } else {
        checksum = mix(checksum, pointerValue(shape.getTechLayer()));
      }
    }
    ++statistics.routed_nets;
    checksum = mix(checksum, pointerValue(net));
    checksum = mix(checksum, shapes.size());
  }
  return checksum;
}

[[nodiscard]] Run measureRun(const Snapshot& preroute,
                             const Snapshot& routed,
                             const DbOnlyFixture& fixture,
                             const CachedDesign& cache,
                             const bool pins_only)
{
  Run run;
  if (!pins_only) {
    run.timings.scan_regular_nets_ns = measure([&] {
      uint64_t checksum = 0;
      uint64_t count = 0;
      // Deliberately use the ODB dbSet traversal here, rather than the cached
      // name-sorted pointers used by guide/route phases.  This is the native
      // counterpart of a service-level forEachRegularNet operation.
      for (odb::dbNet* net : routed.block->getNets()) {
        if (net->isSpecial()) {
          continue;
        }
        checksum = mix(checksum, pointerValue(net) ^ hashText(net->getConstName()));
        ++count;
      }
      return mix(checksum, count);
    }, run.checksum);

    run.timings.find_instances_ns = measure([&] {
      uint64_t checksum = 0;
      for (const size_t index : cache.instance_query_order) {
        odb::dbInst* instance = routed.block->findInst(fixture.instances[index].name.c_str());
        checksum = mix(checksum, pointerValue(instance));
      }
      return checksum;
    }, run.checksum);

    run.timings.find_nets_ns = measure([&] {
      uint64_t checksum = 0;
      for (const size_t index : cache.net_query_order) {
        odb::dbNet* net = routed.block->findNet(fixture.nets[index].c_str());
        checksum = mix(checksum, pointerValue(net));
      }
      return checksum;
    }, run.checksum);
  }

  run.timings.find_pins_ns = measure([&] {
    uint64_t checksum = 0;
    for (const size_t index : cache.pin_query_order) {
      const PinQuery& query = cache.pin_queries[index];
      odb::dbITerm* pin = query.instance->findITerm(query.master_term_name);
      checksum = mix(checksum, pointerValue(pin));
    }
    return checksum;
  }, run.checksum);

  run.timings.scan_net_pins_ns = measure([&] {
    uint64_t checksum = 0;
    uint64_t regular_net_count = 0;
    for (odb::dbNet* net : routed.block->getNets()) {
      if (net->isSpecial()) {
        continue;
      }
      ++regular_net_count;
      for (odb::dbITerm* pin : net->getITerms()) {
        odb::dbInst* instance = pin->getInst();
        odb::dbMTerm* term = pin->getMTerm();
        checksum = mix(checksum,
                       pointerValue(net) ^ pointerValue(pin)
                           ^ (instance == nullptr ? 0U : hashText(instance->getConstName()))
                           ^ (term == nullptr ? 0U : hashText(term->getConstName())));
      }
      for (odb::dbBTerm* pin : net->getBTerms()) {
        checksum = mix(checksum, pointerValue(net) ^ pointerValue(pin) ^ hashText(pin->getConstName()));
      }
    }
    return mix(checksum, regular_net_count);
  }, run.checksum);

  if (!pins_only) {
    run.timings.scan_guides_ns = measure([&] {
      uint64_t checksum = 0;
      for (odb::dbNet* net : cache.preroute_nets_by_fixture) {
        for (odb::dbGuide* guide : net->getGuides()) {
          checksum = mix(checksum, pointerValue(net) ^ scanGuideValue(*guide));
        }
      }
      return checksum;
    }, run.checksum);

    RouteStats decoded_stats;
    run.timings.decode_routes_ns = measure([&] { return decodeRoutes(cache, decoded_stats); }, run.checksum);
    require(decoded_stats.routed_nets == cache.counts.routed_nets
                && decoded_stats.shapes == cache.counts.route_shapes && decoded_stats.vias == cache.counts.route_vias,
            "OpenDB route decode changed the cached shape counts");

    run.timings.replace_guides_ns = measure([&] {
      uint64_t checksum = 0;
      for (const GuideNetCache& net_cache : cache.guide_nets) {
        net_cache.net->clearGuides();
        checksum = mix(checksum, pointerValue(net_cache.net));
      }
      for (const GuideNetCache& net_cache : cache.guide_nets) {
        // dbGuide::create prepends.  Reverse creation preserves the original
        // iteration order, which matters to consumers that treat guide order as
        // stable input.
        for (auto guide = net_cache.guides.rbegin(); guide != net_cache.guides.rend(); ++guide) {
          createGuide(net_cache, *guide);
          checksum = mix(checksum, rectangleValue(guide->box));
        }
      }
      return checksum;
    }, run.checksum);
    validateGuideCache(cache.guide_nets);
  }

  static_cast<void>(preroute);
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
  return {.scan_regular_nets_ns = median(runs, &Timings::scan_regular_nets_ns),
          .find_instances_ns = median(runs, &Timings::find_instances_ns),
          .find_nets_ns = median(runs, &Timings::find_nets_ns),
          .find_pins_ns = median(runs, &Timings::find_pins_ns),
          .scan_net_pins_ns = median(runs, &Timings::scan_net_pins_ns),
          .scan_guides_ns = median(runs, &Timings::scan_guides_ns),
          .decode_routes_ns = median(runs, &Timings::decode_routes_ns),
          .replace_guides_ns = median(runs, &Timings::replace_guides_ns)};
}

void writeTimings(std::ostream& output, const Timings& timings)
{
  output << "{\"scan_regular_nets_ns\":" << timings.scan_regular_nets_ns
         << ",\"find_instances_ns\":" << timings.find_instances_ns
         << ",\"find_nets_ns\":" << timings.find_nets_ns << ",\"find_pins_ns\":" << timings.find_pins_ns
         << ",\"scan_net_pins_ns\":" << timings.scan_net_pins_ns
         << ",\"scan_guides_ns\":" << timings.scan_guides_ns
         << ",\"decode_routes_ns\":" << timings.decode_routes_ns
         << ",\"replace_guides_ns\":" << timings.replace_guides_ns << '}';
}

void writeCounts(std::ostream& output, const Counts& counts)
{
  output << "{\"instances\":" << counts.instances << ",\"regular_nets\":" << counts.regular_nets
         << ",\"instance_pins\":" << counts.instance_pins
         << ",\"connected_iterms\":" << counts.connected_iterms << ",\"io_pins\":" << counts.io_pins
         << ",\"guides\":" << counts.guides << ",\"routed_nets\":" << counts.routed_nets
         << ",\"route_shapes\":" << counts.route_shapes << ",\"route_vias\":" << counts.route_vias << '}';
}

void writeTimedOperationDescriptions(std::ostream& output)
{
  output << "{\"scan_regular_nets\":\"iterate routed dbBlock::getNets, filter special nets, consume dbNet pointers and names\""
         << ",\"find_instances\":\"one routed dbBlock::findInst per shuffled fixture instance name\""
         << ",\"find_nets\":\"one routed dbBlock::findNet per shuffled fixture regular-net name\""
         << ",\"find_pins\":\"one routed dbInst::findITerm per shuffled fixture instance/master-term pair\""
         << ",\"scan_net_pins\":\"iterate routed regular nets plus each dbNet::getITerms and dbNet::getBTerms with pin relation names\""
         << ",\"scan_guides\":\"iterate cached name-sorted pre-route regular nets and consume each dbNet::getGuides value\""
         << ",\"decode_routes\":\"iterate cached name-sorted routed regular nets, dbWireShapeItr decode, materialize per-net dbShape vectors\""
         << ",\"replace_guides\":\"clear every cached pre-route regular-net guide list, then recreate all cached guide values\"}";
}

void writeMeasuredFields(std::ostream& output, const bool pins_only)
{
  output << "[";
  if (pins_only) {
    output << "\"find_pins_ns\",\"scan_net_pins_ns\"";
  } else {
    output << "\"scan_regular_nets_ns\",\"find_instances_ns\",\"find_nets_ns\",\"find_pins_ns\",\"scan_net_pins_ns\","
              "\"scan_guides_ns\",\"decode_routes_ns\",\"replace_guides_ns\"";
  }
  output << "]";
}

void writeJson(std::ostream& output,
               const Counts& counts,
               const std::vector<Run>& runs,
               const Timings& medians,
               const bool pins_only)
{
  output << "{\"schema\":\"drt_service_benchmark_v1\",\"backend\":\"odb\",\"validation\":\"passed\",\"scope\":"
            "\"parser and ODB snapshot loading excluded; routed snapshot supplies regular netlist and wire shapes; "
            "pre-route snapshot supplies guides; route decode materializes ODB dbShape vectors; "
            "connected_iterms excludes special-net ITerms; "
         << (pins_only ? "only find_pins_ns and scan_net_pins_ns are timed\"" : "all native service phases are timed\"")
         << ",\"counts\":";
  writeCounts(output, counts);
  output << ",\"measured_fields\":";
  writeMeasuredFields(output, pins_only);
  output << ",\"timed_operations\":";
  writeTimedOperationDescriptions(output);
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

void writeJsonString(std::ostream& output, const std::string_view value)
{
  static constexpr char kHex[] = "0123456789abcdef";
  output.put('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        output << "\\\"";
        break;
      case '\\':
        output << "\\\\";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\r':
        output << "\\r";
        break;
      case '\t':
        output << "\\t";
        break;
      default:
        if (character < 0x20U) {
          output << "\\u00" << kHex[(character >> 4) & 0x0fU] << kHex[character & 0x0fU];
        } else {
          output.put(static_cast<char>(character));
        }
        break;
    }
  }
  output.put('"');
}

void writeGuideJsonLine(std::ostream& output, const odb::dbNet& net)
{
  output << "{\"net\":";
  writeJsonString(output, net.getConstName());
  output << ",\"guides\":[";
  bool first = true;
  for (odb::dbGuide* guide : net.getGuides()) {
    if (!first) {
      output << ',';
    }
    first = false;
    const odb::Rect box = guide->getBox();
    odb::dbTechLayer* layer = guide->getLayer();
    require(layer != nullptr, "OpenDB guide has no routing layer while exporting");
    output << "{\"box\":[" << box.xMin() << ',' << box.yMin() << ',' << box.xMax() << ',' << box.yMax()
           << "],\"layer\":";
    writeJsonString(output, layer->getConstName());
    output << ",\"via_layer\":";
    if (odb::dbTechLayer* via_layer = guide->getViaLayer(); via_layer != nullptr) {
      writeJsonString(output, via_layer->getConstName());
    } else {
      output << "null";
    }
    output << ",\"flags\":" << guideFlags(*guide) << '}';
  }
  output << "]}\n";
}

void exportGuides(const Snapshot& preroute, const std::filesystem::path& output_path)
{
  const std::vector<odb::dbNet*> regular_nets = sortedRegularNets(*preroute.block);
  if (const std::filesystem::path parent = output_path.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(output_path, std::ios::out | std::ios::trunc | std::ios::binary);
  if (!output) {
    fail("cannot open guide output: " + output_path.string());
  }
  for (odb::dbNet* net : regular_nets) {
    writeGuideJsonLine(output, *net);
  }
  if (!output) {
    fail("cannot write guide output: " + output_path.string());
  }
}

void run(const int argc, char** argv)
{
  const Options options = parseOptions(argc, argv);
  if (!options.export_guides.empty()) {
    Snapshot preroute = loadSnapshot(options.preroute_odb, "pre-route");
    exportGuides(preroute, options.export_guides);
    return;
  }

  const DbOnlyFixture fixture = DbOnlyFixture::load(options.fixture);
  std::vector<Run> runs;
  runs.reserve(options.repeat);
  std::optional<Counts> reference_counts;
  for (uint32_t run_index = 0; run_index < options.repeat; ++run_index) {
    // Fresh snapshots make replacement destructive only to the current run.
    // Snapshot reads, fixture parsing, validation, and cache construction are
    // deliberately outside every measured service operation.
    Snapshot preroute = loadSnapshot(options.preroute_odb, "pre-route");
    Snapshot routed = loadSnapshot(options.routed_odb, "routed");
    const CachedDesign cache = buildCache(preroute, routed, fixture);
    if (reference_counts.has_value()) {
      require(cache.counts.instances == reference_counts->instances
                  && cache.counts.regular_nets == reference_counts->regular_nets
                  && cache.counts.instance_pins == reference_counts->instance_pins
                  && cache.counts.connected_iterms == reference_counts->connected_iterms
                  && cache.counts.io_pins == reference_counts->io_pins && cache.counts.guides == reference_counts->guides
                  && cache.counts.routed_nets == reference_counts->routed_nets
                  && cache.counts.route_shapes == reference_counts->route_shapes
                  && cache.counts.route_vias == reference_counts->route_vias,
              "fresh OpenDB snapshots do not have stable counts");
    } else {
      reference_counts = cache.counts;
    }
    runs.push_back(measureRun(preroute, routed, fixture, cache, options.pins_only));
    require(runs.back().checksum != 0, "OpenDB benchmark produced a zero anti-DCE checksum");
  }

  require(reference_counts.has_value(), "benchmark produced no runs");
  const Timings medians = medianTimings(runs);
  if (options.output.empty()) {
    writeJson(std::cout, *reference_counts, runs, medians, options.pins_only);
    return;
  }
  if (const std::filesystem::path parent = options.output.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(parent);
  }
  std::ofstream output(options.output, std::ios::out | std::ios::trunc);
  if (!output) {
    fail("cannot open output: " + options.output.string());
  }
  writeJson(output, *reference_counts, runs, medians, options.pins_only);
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
    std::cerr << "odb_drt_service_benchmark: " << error.what() << '\n';
    return 1;
  }
}
