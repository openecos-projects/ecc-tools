#include <malloc.h>
#include <sys/resource.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "design/DesignStore.h"
#include "design/netlist/model/NetlistComponents.h"
#include "design/routing/component/RoutingComponents.h"
#include "design/routing/pool/WireRoutingInput.h"
#include "def/DefDesignExporter.h"
#include "lef/LefLibraryExporter.h"
#include "lef/LefTechExporter.h"
#include "geometry/GeometryPool.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "library/cell_master/model/MasterObsComponents.h"
#include "library/master_port/model/MasterPortComponents.h"
#include "library/master_term/model/MasterTermComponents.h"
#include "tech/TechStore.h"
#include "tech/common/TechLayerTypes.h"
#include "tech/via_master/storage/ViaMasterStorage.h"

#if defined(ECCDB_BENCHMARK_HAS_LEGACY_IDB)
#include "idm.h"
#endif

namespace eccdb {
namespace {

using Clock = std::chrono::steady_clock;

struct Memory
{
  uint64_t rss_kib = 0;
  uint64_t peak_rss_kib = 0;
  uint64_t allocator_kib = 0;
};

struct Record
{
  std::string source;
  std::string operation;
  bool ok = true;
  uint64_t input_bytes = 0;
  uint64_t output_bytes = 0;
  uint64_t records = 0;
  uint64_t edges = 0;
  uint64_t shapes = 0;
  uint64_t elapsed_ns = 0;
  Memory before;
  Memory after;
};

struct Options
{
  std::filesystem::path lef;
  std::filesystem::path def;
  std::string source = "entt";
  std::filesystem::path output;
  uint64_t writes = 256;
};

volatile uint64_t g_sink = 0;

uint64_t statusValue(std::string_view field)
{
  std::ifstream input("/proc/self/status");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string name;
    uint64_t value = 0;
    std::string unit;
    if ((fields >> name >> value >> unit) && name == field && unit == "kB") {
      return value;
    }
  }
  return 0;
}

Memory memory()
{
  const auto allocator = mallinfo2();
  uint64_t rss_kib = 0;
  {
    std::ifstream input("/proc/self/smaps_rollup");
    std::string line;
    while (std::getline(input, line)) {
      std::istringstream fields(line);
      std::string name;
      uint64_t value = 0;
      std::string unit;
      if ((fields >> name >> value >> unit) && name == "Rss:" && unit == "kB") {
        rss_kib = value;
        break;
      }
    }
  }
  if (rss_kib == 0u) rss_kib = statusValue("VmRSS:");
  return Memory{.rss_kib = rss_kib,
                .peak_rss_kib = statusValue("VmHWM:"),
                .allocator_kib = (static_cast<uint64_t>(allocator.uordblks) + static_cast<uint64_t>(allocator.hblkhd)) / 1024u};
}

Memory settledMemory()
{
  static_cast<void>(malloc_trim(0));
  return memory();
}

uint64_t elapsedNs(Clock::time_point begin)
{
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
}

uint64_t fileSize(const std::filesystem::path& path)
{
  return std::filesystem::is_regular_file(path) ? std::filesystem::file_size(path) : 0u;
}

void validateInputs(const Options& options)
{
  if (!std::filesystem::is_regular_file(options.lef)) {
    throw std::runtime_error("LEF is not a regular file: " + options.lef.string());
  }
  if (!std::filesystem::is_regular_file(options.def)) {
    throw std::runtime_error("DEF is not a regular file: " + options.def.string());
  }
  if (options.source != "entt" && options.source != "idb" && options.source != "both") {
    throw std::runtime_error("--source must be entt, idb, or both");
  }
}

template <typename Function>
void measure(std::vector<Record>& output, std::string source, std::string operation, uint64_t input_bytes, Function&& function)
{
  const Memory before = settledMemory();
  const auto begin = Clock::now();
  const auto values = function();
  const uint64_t elapsed = elapsedNs(begin);
  const Memory after = settledMemory();
  output.push_back(Record{.source = std::move(source),
                          .operation = std::move(operation),
                          .input_bytes = input_bytes,
                          .records = values.records,
                          .edges = values.edges,
                          .shapes = values.shapes,
                          .elapsed_ns = elapsed,
                          .before = before,
                          .after = after});
}

struct Work
{
  uint64_t records = 0;
  uint64_t edges = 0;
  uint64_t shapes = 0;
};

void writeJson(std::ostream& output, const Record& record, std::string_view case_name)
{
  const auto delta = record.after.rss_kib >= record.before.rss_kib ? record.after.rss_kib - record.before.rss_kib : 0u;
  output << "{\"case\":\"" << case_name << "\",\"source\":\"" << record.source << "\",\"operation\":\""
         << record.operation << "\",\"ok\":" << (record.ok ? "true" : "false") << ",\"input_bytes\":"
         << record.input_bytes << ",\"output_bytes\":" << record.output_bytes
         << ",\"records\":" << record.records << ",\"edges\":" << record.edges << ",\"shapes\":" << record.shapes
         << ",\"elapsed_ns\":" << record.elapsed_ns << ",\"throughput_records_per_s\":"
         << (record.elapsed_ns == 0u ? 0.0 : static_cast<double>(record.records) * 1.0e9 / record.elapsed_ns)
         << ",\"throughput_edges_per_s\":"
         << (record.elapsed_ns == 0u ? 0.0 : static_cast<double>(record.edges) * 1.0e9 / record.elapsed_ns)
         << ",\"throughput_shapes_per_s\":"
         << (record.elapsed_ns == 0u ? 0.0 : static_cast<double>(record.shapes) * 1.0e9 / record.elapsed_ns)
         << ",\"rss_delta_kib\":" << delta << ",\"rss_after_kib\":" << record.after.rss_kib
         << ",\"peak_rss_kib\":" << record.after.peak_rss_kib << ",\"allocator_after_kib\":" << record.after.allocator_kib << "}\n";
}

struct EnttContext
{
  std::unique_ptr<TechStore> technology;
  std::unique_ptr<LibraryStore> library;
  std::unique_ptr<DesignStore> design;
};

struct EnttCounts
{
  uint64_t instances = 0;
  uint64_t instance_pins = 0;
  uint64_t io_pins = 0;
  uint64_t regular_nets = 0;
  uint64_t special_nets = 0;
  uint64_t wires = 0;
  uint64_t paths = 0;
  uint64_t points = 0;
  uint64_t vias = 0;
  uint64_t rectangles = 0;
};

EnttCounts countEntt(const EnttContext& context)
{
  EnttCounts result;
  result.instances = context.design->netlistStorage().instanceCount();
  result.instance_pins = context.design->netlistStorage().instancePinCount();
  result.io_pins = context.design->netlistStorage().ioPinCount();
  const auto special = context.design->netlistStorage().specialNets();
  result.special_nets = special.size();
  result.regular_nets = context.design->netlistStorage().netCount() - result.special_nets;
  const auto routing = context.design->routingStorage().routingPoolStatistics();
  result.wires = context.design->routingStorage().wireCount();
  result.paths = routing.paths.count;
  result.points = routing.points.count;
  result.vias = routing.vias.count;
  result.rectangles = routing.rectangles.count;
  return result;
}

EnttContext loadEntt(const Options& options, std::vector<Record>& records)
{
  EnttContext context;
  measure(records, "entt", "lef_read", fileSize(options.lef), [&]() {
    context.technology = std::make_unique<TechStore>();
    LefTechImporter(*context.technology).import(options.lef);
    context.library = std::make_unique<LibraryStore>(context.technology->techRegistry());
    LefLibraryImporter(*context.technology, *context.library).import(options.lef);
    const auto layers = context.technology->layerSequence().size();
    const auto masters = context.library->cellMasterStorage().cellMasterCount();
    return Work{.records = layers + masters, .shapes = context.library->geometryPool().rectangleCount()
                                      + context.library->geometryPool().pointCount()};
  });
  measure(records, "entt", "def_read", fileSize(options.def), [&]() {
    context.design = std::make_unique<DesignStore>(context.technology->techRegistry(), context.library->libraryRegistry());
    DefDesignImporter importer(*context.design);
    importer.import(options.def);
    const auto counts = countEntt(context);
    return Work{.records = counts.instances + counts.regular_nets + counts.special_nets + counts.wires,
                .edges = counts.instance_pins + counts.io_pins,
                .shapes = counts.points + counts.vias + counts.rectangles};
  });
  return context;
}

void benchmarkEntt(const Options& options, std::vector<Record>& records)
{
  auto context = loadEntt(options, records);
  const auto counts = countEntt(context);
  const uint64_t design_bytes = fileSize(options.def);

  measure(records, "entt", "net_pin_forward", design_bytes, [&]() {
    Work work;
    for (const auto net : context.design->netlistStorage().regularNets()) {
      ++work.records;
      const auto instance_pins = context.design->netlistStorage().instancePins(net);
      const auto io_pins = context.design->netlistStorage().ioPins(net);
      work.edges += instance_pins.size() + io_pins.size();
      g_sink ^= work.edges + net.packed();
    }
    return work;
  });

  measure(records, "entt", "pin_net_reverse", design_bytes, [&]() {
    Work work;
    context.design->netlistStorage().forEachInstancePin([&](DesignInstancePinId pin, const DesignInstancePin& value) {
      ++work.records;
      ++work.edges;
      g_sink ^= value.net.packed() + pin.packed();
    });
    context.design->netlistStorage().forEachIoPin([&](DesignIoPinId pin, const DesignIoPin& value) {
      ++work.records;
      g_sink ^= value.net.packed() + pin.packed();
      ++work.edges;
    });
    return work;
  });

  measure(records, "entt", "placed_geometry", design_bytes, [&]() {
    Work work;
    const auto& library = *context.library;
    const auto& geometry = library.geometryPool();
    for (const auto instance_id : context.design->netlistStorage().instances()) {
      const auto& instance = context.design->netlistStorage().instance(instance_id);
      if (library.cellMasterStorage().hasObs(instance.master)) {
        const auto& obs = library.cellMasterStorage().obs(instance.master);
        for (const auto& clause : obs.layer_clauses) {
          ++work.records;
          work.shapes += geometry.rectangles(clause.geometry).size();
          for (uint32_t index = 0; index < geometry.polygonCount(clause.geometry); ++index) {
            work.shapes += geometry.polygonPoints(clause.geometry, index).size();
          }
        }
        work.shapes += obs.vias.size();
      }
      for (const auto pin_id : context.design->netlistStorage().instancePins(instance_id)) {
        const auto& pin = context.design->netlistStorage().instancePin(pin_id);
        const auto& term = library.masterTermStorage().masterTerm(pin.master_term);
        for (const auto port_id : term.ports) {
          const auto& port = library.masterPortStorage().masterPort(port_id);
          ++work.records;
          for (const auto& layer : port.layer_clauses) {
            work.shapes += geometry.rectangles(layer.geometry).size();
            for (uint32_t index = 0; index < geometry.polygonCount(layer.geometry); ++index) {
              work.shapes += geometry.polygonPoints(layer.geometry, index).size();
            }
          }
          work.shapes += port.vias.size();
        }
      }
    }
    for (const auto pin_id : context.design->netlistStorage().ioPins()) {
      const auto& pin = context.design->netlistStorage().ioPin(pin_id);
      for (const auto& port : pin.ports) {
        ++work.records;
        work.shapes += port.rectangles.size() + port.vias.size();
        for (const auto& polygon : port.polygons) work.shapes += polygon.points.size();
      }
    }
    g_sink ^= work.records + work.shapes;
    return work;
  });

  measure(records, "entt", "regular_route_geometry", design_bytes, [&]() {
    Work work;
    for (const auto net : context.design->netlistStorage().regularNets()) {
      for (const auto wire : context.design->routingStorage().wireIds(net)) {
        ++work.records;
        context.design->routingStorage().forEachPath(wire, [&](DesignWirePathView path) {
          ++work.records;
          const auto points = path.points();
          const auto vias = path.vias();
          const auto rectangles = path.rectangles();
          for (const auto point : points) g_sink ^= static_cast<uint64_t>(point.position.x + point.position.y);
          for (const auto via : vias) g_sink ^= via.point_index;
          for (const auto rectangle : rectangles) g_sink ^= rectangle.point_index;
          work.edges += points.size() + vias.size() + rectangles.size();
          work.shapes += points.size() + vias.size() + rectangles.size();
        });
      }
      if (const auto* geometry = context.design->routingStorage().netGeometry(net); geometry != nullptr) {
        work.shapes += geometry->rectangles.size() + geometry->vias.size();
        for (const auto& polygon : geometry->polygons) work.shapes += polygon.points.size();
      }
    }
    return work;
  });

  measure(records, "entt", "special_route_geometry", design_bytes, [&]() {
    Work work;
    for (const auto net : context.design->netlistStorage().specialNets()) {
      for (const auto wire : context.design->routingStorage().wireIds(net)) {
        ++work.records;
        context.design->routingStorage().forEachPath(wire, [&](DesignWirePathView path) {
          ++work.records;
          const auto points = path.points();
          const auto vias = path.vias();
          const auto rectangles = path.rectangles();
          for (const auto point : points) g_sink ^= static_cast<uint64_t>(point.position.x + point.position.y);
          for (const auto via : vias) g_sink ^= via.point_index;
          for (const auto rectangle : rectangles) g_sink ^= rectangle.point_index;
          work.edges += points.size() + vias.size() + rectangles.size();
          work.shapes += points.size() + vias.size() + rectangles.size();
        });
      }
      if (const auto* geometry = context.design->routingStorage().netGeometry(net); geometry != nullptr) {
        work.shapes += geometry->rectangles.size() + geometry->vias.size();
        for (const auto& polygon : geometry->polygons) work.shapes += polygon.points.size();
      }
    }
    return work;
  });

  measure(records, "entt", "tech_floorplan_access", fileSize(options.lef), [&]() {
    Work work;
    for (const auto layer : context.technology->layerSequence()) {
      const auto& info = context.technology->layerInfo(layer);
      g_sink ^= info.name.size() + context.technology->layerPosition(layer).value_or(0u);
      ++work.records;
    }
    for (const auto via : context.technology->viaMasterStorage().viaMasters()) {
      work.records++;
      work.shapes += context.technology->viaMasterStorage().bottomRects(via).size();
      work.shapes += context.technology->viaMasterStorage().cutRects(via).size();
      work.shapes += context.technology->viaMasterStorage().topRects(via).size();
    }
    for (const auto row : context.design->floorplanStorage().rows()) g_sink ^= row.packed();
    for (const auto grid : context.design->floorplanStorage().trackGrids()) g_sink ^= grid.packed();
    for (const auto grid : context.design->floorplanStorage().gcellGrids()) g_sink ^= grid.packed();
    work.edges = context.design->floorplanStorage().rowCount() + context.design->floorplanStorage().trackGridCount()
                 + context.design->floorplanStorage().gcellGridCount();
    return work;
  });

  const auto temp = std::filesystem::temp_directory_path() / ("idb-eccdb-benchmark-" + std::to_string(getpid()));
  std::filesystem::create_directories(temp);
  {
    measure(records, "entt", "lef_tech_write", fileSize(options.lef), [&]() {
      const auto path = temp / "entt.tech.lef";
      LefTechExporter::write(path, *context.technology);
      return Work{.records = context.technology->layerSequence().size()};
    });
    records.back().output_bytes = fileSize(temp / "entt.tech.lef");
  }
  {
    measure(records, "entt", "lef_library_write", fileSize(options.lef), [&]() {
      const auto path = temp / "entt.library.lef";
      LefLibraryExporter::write(path, *context.technology, *context.library);
      return Work{.records = context.library->cellMasterStorage().cellMasterCount()};
    });
    records.back().output_bytes = fileSize(temp / "entt.library.lef");
  }
  {
    measure(records, "entt", "def_write", fileSize(options.def), [&]() {
      const auto path = temp / "entt.def";
      DefDesignExporter(*context.design).write(path);
      return Work{.records = counts.instances + counts.regular_nets + counts.special_nets, .shapes = counts.points + counts.vias + counts.rectangles};
    });
    records.back().output_bytes = fileSize(temp / "entt.def");
  }
  std::error_code error;
  std::filesystem::remove_all(temp, error);

  if (counts.regular_nets != 0u && options.writes != 0u) {
    measure(records, "entt", "routing_batch_append", design_bytes, [&]() {
      Work work;
      TechRoutingLayerId routing_layer;
      for (const auto layer : context.technology->layerSequence()) {
        const TechRoutingLayerId candidate{layer.entity()};
        if (context.technology->routingLevel(candidate).has_value()) {
          routing_layer = candidate;
          break;
        }
      }
      if (!routing_layer) return work;
      const auto net = context.design->netlistStorage().regularNets().front();
      for (uint64_t index = 0; index < options.writes; ++index) {
        DesignWirePath path;
        path.layer = routing_layer;
        path.points.push_back(DesignWirePoint{.position = Point{static_cast<int32_t>(index), 0}});
        path.points.push_back(DesignWirePoint{.position = Point{static_cast<int32_t>(index + 1u), 0}});
        DesignWireRoutingInput input;
        input.appendPath(std::move(path));
        static_cast<void>(context.design->routingStorage().createWireTrusted(DesignWire{.net = net}, std::move(input)));
        ++work.records;
        work.edges += 2u;
      }
      return work;
    });
  }
}

#if defined(ECCDB_BENCHMARK_HAS_LEGACY_IDB)

struct LegacyContext
{
  std::unique_ptr<idb::IdbBuilder> builder;
  idb::IdbDefService* def_service = nullptr;
};

LegacyContext loadLegacy(const Options& options, std::vector<Record>& records)
{
  LegacyContext context;
  context.builder = std::make_unique<idb::IdbBuilder>();
  measure(records, "idb", "lef_read", fileSize(options.lef), [&]() {
    std::vector<std::string> files{options.lef.string()};
    auto* service = context.builder->buildLef(files, false);
    if (service == nullptr || service->get_layout() == nullptr) throw std::runtime_error("iDB LEF read failed");
    auto* layout = service->get_layout();
    Work work;
    if (layout->get_layers() != nullptr) work.records += layout->get_layers()->get_layers().size();
    if (layout->get_sites() != nullptr) work.records += layout->get_sites()->get_site_list().size();
    if (layout->get_cell_master_list() != nullptr) {
      const auto& masters = layout->get_cell_master_list()->get_cell_master();
      work.records += masters.size();
      for (auto* master : masters) {
        if (master == nullptr) continue;
        work.records += master->get_term_list().size();
        for (auto* term : master->get_term_list()) {
          if (term == nullptr) continue;
          work.records += term->get_port_list().size();
          for (auto* port : term->get_port_list()) {
            if (port == nullptr) continue;
            for (auto* shape : port->get_layer_shape()) {
              if (shape != nullptr) work.shapes += shape->get_rect_list().size();
            }
            for (auto* via : port->get_via_list()) {
              if (via != nullptr) work.shapes += via->get_cut_layer_shape().get_rect_list().size() + 2u;
            }
          }
        }
      }
    }
    return work;
  });
  measure(records, "idb", "def_read", fileSize(options.def), [&]() {
    context.def_service = context.builder->buildDef(options.def.string());
    if (context.def_service == nullptr || context.def_service->get_design() == nullptr) throw std::runtime_error("iDB DEF read failed");
    auto* design = context.def_service->get_design();
    Work work{.records = static_cast<uint64_t>(design->get_instance_list()->get_instance_list().size())
                         + static_cast<uint64_t>(design->get_net_list()->get_num())
                         + static_cast<uint64_t>(design->get_special_net_list()->get_num()),
              .edges = static_cast<uint64_t>(design->get_io_pin_list()->get_pin_num())};
    return work;
  });
  return context;
}

void benchmarkLegacy(const Options& options, std::vector<Record>& records)
{
  auto context = loadLegacy(options, records);
  auto* design = context.def_service->get_design();
  auto* layout = context.def_service->get_layout();
  const uint64_t design_bytes = fileSize(options.def);

  measure(records, "idb", "net_pin_forward", design_bytes, [&]() {
    Work work;
    for (auto* net : design->get_net_list()->get_net_list()) {
      if (net == nullptr) continue;
      ++work.records;
      if (net->get_instance_pin_list() != nullptr) work.edges += net->get_instance_pin_list()->get_pin_list().size();
      if (net->get_io_pins() != nullptr) work.edges += net->get_io_pins()->get_pin_list().size();
      g_sink ^= work.edges;
    }
    return work;
  });

  measure(records, "idb", "pin_net_reverse", design_bytes, [&]() {
    Work work;
    for (auto* instance : design->get_instance_list()->get_instance_list()) {
      if (instance == nullptr || instance->get_pin_list() == nullptr) continue;
      for (auto* pin : instance->get_pin_list()->get_pin_list()) {
        if (pin == nullptr) continue;
        ++work.records;
        ++work.edges;
        g_sink ^= reinterpret_cast<uintptr_t>(pin->get_net());
      }
    }
    for (auto* pin : design->get_io_pin_list()->get_pin_list()) {
      if (pin == nullptr) continue;
      ++work.records;
      ++work.edges;
      g_sink ^= reinterpret_cast<uintptr_t>(pin->get_net());
    }
    return work;
  });

  measure(records, "idb", "placed_geometry", design_bytes, [&]() {
    Work work;
    for (auto* instance : design->get_instance_list()->get_instance_list()) {
      if (instance == nullptr) continue;
      for (auto* shape : instance->get_obs_box_list()) {
        if (shape == nullptr) continue;
        ++work.records;
        work.shapes += shape->get_rect_list().size();
      }
      if (instance->get_pin_list() == nullptr) continue;
      for (auto* pin : instance->get_pin_list()->get_pin_list()) {
        if (pin == nullptr) continue;
        ++work.records;
        for (auto* shape : pin->get_port_box_list()) {
          if (shape != nullptr) work.shapes += shape->get_rect_list().size();
        }
        for (auto* via : pin->get_via_list()) {
          if (via == nullptr) continue;
          work.shapes += via->get_cut_layer_shape().get_rect_list().size() + 2u;
        }
      }
    }
    for (auto* pin : design->get_io_pin_list()->get_pin_list()) {
      if (pin == nullptr) continue;
      ++work.records;
      for (auto* shape : pin->get_port_box_list()) {
        if (shape != nullptr) work.shapes += shape->get_rect_list().size();
      }
    }
    return work;
  });

  measure(records, "idb", "regular_route_geometry", design_bytes, [&]() {
    Work work;
    for (auto* net : design->get_net_list()->get_net_list()) {
      if (net == nullptr || net->get_wire_list() == nullptr) continue;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) continue;
        ++work.records;
        for (auto* segment : wire->get_segment_list()) {
          if (segment == nullptr) continue;
          ++work.records;
          work.edges += segment->get_point_number();
          if (segment->is_via()) {
            for (auto* via : segment->get_via_list()) {
              if (via != nullptr) work.shapes += via->get_cut_layer_shape().get_rect_list().size() + 2u;
            }
          }
          if (segment->is_rect() && segment->get_delta_rect() != nullptr) ++work.shapes;
        }
      }
    }
    return work;
  });

  measure(records, "idb", "special_route_geometry", design_bytes, [&]() {
    Work work;
    for (auto* net : design->get_special_net_list()->get_net_list()) {
      if (net == nullptr || net->get_wire_list() == nullptr) continue;
      for (auto* wire : net->get_wire_list()->get_wire_list()) {
        if (wire == nullptr) continue;
        ++work.records;
        for (auto* segment : wire->get_segment_list()) {
          if (segment == nullptr) continue;
          ++work.records;
          work.edges += segment->get_point_num();
          if (segment->is_via() && segment->get_via() != nullptr) {
            work.shapes += segment->get_via()->get_cut_layer_shape().get_rect_list().size() + 2u;
          }
          if (segment->is_rect() && segment->get_delta_rect() != nullptr) ++work.shapes;
        }
      }
    }
    return work;
  });

  measure(records, "idb", "tech_floorplan_access", fileSize(options.lef), [&]() {
    Work work;
    if (layout != nullptr) {
      if (layout->get_layers() != nullptr) work.records += layout->get_layers()->get_layers().size();
      if (layout->get_via_list() != nullptr) work.records += layout->get_via_list()->get_via_list().size();
      if (layout->get_rows() != nullptr) work.edges += layout->get_rows()->get_row_num();
      if (layout->get_track_grid_list() != nullptr) work.edges += layout->get_track_grid_list()->get_track_grid_num();
      if (layout->get_gcell_grid_list() != nullptr) work.edges += layout->get_gcell_grid_list()->get_gcell_grid_num();
    }
    return work;
  });

  const auto temp = std::filesystem::temp_directory_path() / ("idb-eccdb-benchmark-idb-" + std::to_string(getpid()));
  std::filesystem::create_directories(temp);
  measure(records, "idb", "def_write", fileSize(options.def), [&]() {
    const auto path = temp / "idb.def";
    static_cast<void>(context.builder->saveDef(path.string()));
    return Work{};
  });
  records.back().output_bytes = fileSize(temp / "idb.def");
  records.back().ok = records.back().output_bytes != 0u;
  std::error_code error;
  std::filesystem::remove_all(temp, error);
}

#endif

Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    auto value = [&](std::string_view name) -> std::string {
      if (index + 1 >= argc) throw std::runtime_error("missing value for " + std::string(name));
      return argv[++index];
    };
    if (argument == "--lef") options.lef = value(argument);
    else if (argument == "--def") options.def = value(argument);
    else if (argument == "--source") options.source = value(argument);
    else if (argument == "--output") options.output = value(argument);
    else if (argument == "--writes") options.writes = std::stoull(value(argument));
    else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: eccdb_benchmark --lef FILE --def FILE [--source entt|idb|both] [--output FILE] [--writes N]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(argument));
    }
  }
  validateInputs(options);
  return options;
}

}  // namespace
}  // namespace eccdb

int main(int argc, char** argv)
{
  try {
    const auto options = eccdb::parseOptions(argc, argv);
    std::vector<eccdb::Record> records;
    if (options.source == "entt" || options.source == "both") eccdb::benchmarkEntt(options, records);
#if defined(ECCDB_BENCHMARK_HAS_LEGACY_IDB)
    if (options.source == "idb" || options.source == "both") eccdb::benchmarkLegacy(options, records);
#else
    if (options.source == "idb" || options.source == "both") {
      throw std::runtime_error("this build does not contain the legacy IdbBuilder target");
    }
#endif

    std::string case_name = options.def.stem().string();
    if (case_name.ends_with(".input")) case_name.resize(case_name.size() - std::string_view(".input").size());
    std::ostream* destination = &std::cout;
    std::ofstream file;
    if (!options.output.empty()) {
      if (const auto parent = options.output.parent_path(); !parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) throw std::runtime_error("cannot create benchmark output directory: " + parent.string());
      }
      file.open(options.output, std::ios::out | std::ios::app);
      if (!file) throw std::runtime_error("cannot open benchmark output: " + options.output.string());
      destination = &file;
    }
    for (const auto& record : records) eccdb::writeJson(*destination, record, case_name);
    std::cerr << "benchmark completed: " << records.size() << " measurements, sink=" << eccdb::g_sink << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
