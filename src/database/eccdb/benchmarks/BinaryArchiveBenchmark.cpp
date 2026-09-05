#include <malloc.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "binary/BinaryDatabaseExporter.h"
#include "def/DefDesignExporter.h"
#include "binary/BinaryDatabaseImporter.h"
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {
namespace {

using Clock = std::chrono::steady_clock;

struct Options
{
  std::filesystem::path lef;
  std::filesystem::path def;
  std::filesystem::path source_archive_dir;
  std::filesystem::path archive_dir;
  std::filesystem::path def_export;
  std::filesystem::path output;
};

struct Memory
{
  uint64_t rss_kib = 0;
  uint64_t peak_rss_kib = 0;
  uint64_t allocator_kib = 0;
};

struct Record
{
  std::string operation;
  uint64_t elapsed_ns = 0;
  uint64_t input_bytes = 0;
  uint64_t output_bytes = 0;
  Memory before;
  Memory after;
};

struct DatabaseCounts
{
  uint64_t layers = 0;
  uint64_t masters = 0;
  uint64_t instances = 0;
  uint64_t instance_pins = 0;
  uint64_t io_pins = 0;
  uint64_t nets = 0;
  uint64_t wires = 0;
  uint64_t paths = 0;
  uint64_t points = 0;
  uint64_t vias = 0;
  uint64_t rectangles = 0;

  bool operator==(const DatabaseCounts&) const = default;
};

uint64_t statusValue(std::string_view field)
{
  std::ifstream input("/proc/self/status");
  std::string line;
  while (std::getline(input, line)) {
    std::istringstream fields(line);
    std::string name;
    uint64_t value = 0;
    std::string unit;
    if ((fields >> name >> value >> unit) && name == field && unit == "kB") return value;
  }
  return 0;
}

Memory memory()
{
  const auto allocator = mallinfo2();
  uint64_t rss_kib = 0;
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

uint64_t fileSize(const std::filesystem::path& path)
{
  return std::filesystem::is_regular_file(path) ? std::filesystem::file_size(path) : 0u;
}

template <typename Function>
void measure(std::vector<Record>& records, std::string operation, uint64_t input_bytes, Function&& function)
{
  std::cerr << "[benchmark] " << operation << "..." << std::endl;
  const Memory before = settledMemory();
  const auto begin = Clock::now();
  function();
  const auto elapsed = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - begin).count());
  const Memory after = settledMemory();
  records.push_back(Record{.operation = std::move(operation),
                           .elapsed_ns = elapsed,
                           .input_bytes = input_bytes,
                           .before = before,
                           .after = after});
  std::cerr << "[benchmark] completed in " << static_cast<double>(elapsed) / 1.0e9 << " s" << std::endl;
}

void addTotal(std::vector<Record>& records, std::string operation, std::size_t first, std::size_t last)
{
  Record total;
  total.operation = std::move(operation);
  total.before = records.at(first).before;
  total.after = records.at(last).after;
  for (std::size_t index = first; index <= last; ++index) {
    total.elapsed_ns += records[index].elapsed_ns;
    total.input_bytes += records[index].input_bytes;
    total.output_bytes += records[index].output_bytes;
  }
  records.push_back(std::move(total));
}

DatabaseCounts databaseCounts(const TechStore& technology, const LibraryStore& library, const DesignStore& design)
{
  const auto routing = design.routingStorage().routingPoolStatistics();
  return DatabaseCounts{.layers = technology.layerSequence().size(),
                        .masters = library.cellMasterStorage().cellMasterCount(),
                        .instances = design.netlistStorage().instanceCount(),
                        .instance_pins = design.netlistStorage().instancePinCount(),
                        .io_pins = design.netlistStorage().ioPinCount(),
                        .nets = design.netlistStorage().netCount(),
                        .wires = design.routingStorage().wireCount(),
                        .paths = routing.paths.count,
                        .points = routing.points.count,
                        .vias = routing.vias.count,
                        .rectangles = routing.rectangles.count};
}

std::string jsonEscape(std::string_view value)
{
  std::string result;
  result.reserve(value.size());
  for (const char character : value) {
    if (character == '\\' || character == '"') result.push_back('\\');
    result.push_back(character);
  }
  return result;
}

void writeRecord(std::ostream& output, std::string_view case_name, const Record& record)
{
  const uint64_t measured_bytes = record.output_bytes != 0u ? record.output_bytes : record.input_bytes;
  const double bytes_per_second
      = record.elapsed_ns == 0u ? 0.0 : static_cast<double>(measured_bytes) * 1.0e9 / static_cast<double>(record.elapsed_ns);
  output << "{\"case\":\"" << jsonEscape(case_name) << "\",\"operation\":\"" << record.operation
         << "\",\"elapsed_ns\":" << record.elapsed_ns << ",\"elapsed_seconds\":"
         << static_cast<double>(record.elapsed_ns) / 1.0e9 << ",\"input_bytes\":" << record.input_bytes
         << ",\"output_bytes\":" << record.output_bytes << ",\"throughput_mib_per_s\":"
         << bytes_per_second / (1024.0 * 1024.0) << ",\"rss_before_kib\":" << record.before.rss_kib
         << ",\"rss_after_kib\":" << record.after.rss_kib << ",\"peak_rss_kib\":" << record.after.peak_rss_kib
         << ",\"allocator_after_kib\":" << record.after.allocator_kib << "}\n";
}

void writeCounts(std::ostream& output, std::string_view case_name, const DatabaseCounts& counts,
                 uint64_t archive_bytes)
{
  output << "{\"case\":\"" << jsonEscape(case_name)
         << "\",\"operation\":\"restored_counts\",\"archive_bytes\":" << archive_bytes << ",\"layers\":"
         << counts.layers << ",\"masters\":" << counts.masters << ",\"instances\":" << counts.instances
         << ",\"instance_pins\":" << counts.instance_pins << ",\"io_pins\":" << counts.io_pins
         << ",\"nets\":" << counts.nets << ",\"wires\":" << counts.wires << ",\"paths\":" << counts.paths
         << ",\"points\":" << counts.points << ",\"vias\":" << counts.vias << ",\"rectangles\":"
         << counts.rectangles << "}\n";
}

Options parseOptions(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto value = [&](std::string_view name) -> std::string {
      if (index + 1 >= argc) throw std::runtime_error("missing value for " + std::string(name));
      return argv[++index];
    };
    if (argument == "--lef") options.lef = value(argument);
    else if (argument == "--def") options.def = value(argument);
    else if (argument == "--source-archive-dir") options.source_archive_dir = value(argument);
    else if (argument == "--archive-dir") options.archive_dir = value(argument);
    else if (argument == "--def-export") options.def_export = value(argument);
    else if (argument == "--output") options.output = value(argument);
    else if (argument == "--help" || argument == "-h") {
      std::cout << "usage: eccdb_binary_archive_benchmark (--lef FILE --def FILE | --source-archive-dir DIR) "
                   "(--archive-dir DIR | --def-export FILE) [--output FILE]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + std::string(argument));
    }
  }
  const bool has_text_source = !options.lef.empty() || !options.def.empty();
  const bool has_archive_source = !options.source_archive_dir.empty();
  if (has_text_source == has_archive_source) {
    throw std::runtime_error("provide exactly one input source: --lef/--def or --source-archive-dir");
  }
  if (has_text_source) {
    if (!std::filesystem::is_regular_file(options.lef)) throw std::runtime_error("invalid LEF: " + options.lef.string());
    if (!std::filesystem::is_regular_file(options.def)) throw std::runtime_error("invalid DEF: " + options.def.string());
  } else {
    for (const auto name : {"technology.edb", "library.edb", "design.edb"}) {
      const auto path = options.source_archive_dir / name;
      if (!std::filesystem::is_regular_file(path)) throw std::runtime_error("invalid source archive: " + path.string());
    }
  }
  if (options.archive_dir.empty() == options.def_export.empty()) {
    throw std::runtime_error("provide exactly one output mode: --archive-dir or --def-export");
  }
  return options;
}

void run(const Options& options)
{
  std::error_code error;
  if (!options.archive_dir.empty()) {
    std::filesystem::create_directories(options.archive_dir, error);
    if (error) throw std::runtime_error("cannot create archive directory: " + options.archive_dir.string());
    if (!options.source_archive_dir.empty() && std::filesystem::equivalent(options.source_archive_dir, options.archive_dir, error)) {
      throw std::runtime_error("source and output archive directories must differ");
    }
    if (error) throw std::runtime_error("cannot compare source and output archive directories");
  }
  if (!options.def_export.empty() && !options.def_export.parent_path().empty()) {
    std::filesystem::create_directories(options.def_export.parent_path(), error);
    if (error) throw std::runtime_error("cannot create DEF output directory: " + options.def_export.parent_path().string());
  }
  if (!options.output.empty() && !options.output.parent_path().empty()) {
    std::filesystem::create_directories(options.output.parent_path(), error);
    if (error) throw std::runtime_error("cannot create output directory: " + options.output.parent_path().string());
  }

  const auto tech_binary = options.archive_dir / "technology.edb";
  const auto library_binary = options.archive_dir / "library.edb";
  const auto design_binary = options.archive_dir / "design.edb";
  std::vector<Record> records;

  std::unique_ptr<TechStore> technology;
  std::unique_ptr<LibraryStore> library;
  std::unique_ptr<DesignStore> design;

  if (options.source_archive_dir.empty()) {
    measure(records, "lef_text_import", fileSize(options.lef), [&]() {
      technology = std::make_unique<TechStore>();
      LefTechImporter(*technology).import(options.lef);
      library = std::make_unique<LibraryStore>(technology->techRegistry());
      LefLibraryImporter(*technology, *library).import(options.lef);
    });
    measure(records, "def_text_import", fileSize(options.def), [&]() {
      design = std::make_unique<DesignStore>(technology->techRegistry(), library->libraryRegistry());
      DefDesignImporter(*design).import(options.def);
    });
  } else {
    const auto source_tech = options.source_archive_dir / "technology.edb";
    const auto source_library = options.source_archive_dir / "library.edb";
    const auto source_design = options.source_archive_dir / "design.edb";
    const std::size_t import_first = records.size();
    measure(records, "source_binary_import_tech_total", fileSize(source_tech),
            [&]() { technology = BinaryDatabaseImporter::loadTech(source_tech); });
    measure(records, "source_binary_import_library_total", fileSize(source_library),
            [&]() { library = BinaryDatabaseImporter::loadLibrary(source_library, *technology); });
    const auto design_index = records.size();
    measure(records, "source_binary_import_design_total", fileSize(source_design), [&]() {
      design = BinaryDatabaseImporter::loadDesign(source_design, *technology, *library);
    });
    addTotal(records, "source_binary_import_total", import_first, design_index);
  }
  const DatabaseCounts expected = databaseCounts(*technology, *library, *design);

  if (!options.def_export.empty()) {
    measure(records, "def_text_export", 0u, [&]() { DefDesignExporter(*design).write(options.def_export); });
    records.back().output_bytes = fileSize(options.def_export);

    std::ostream* destination = &std::cout;
    std::ofstream file;
    if (!options.output.empty()) {
      file.open(options.output, std::ios::out | std::ios::trunc);
      if (!file) throw std::runtime_error("cannot open benchmark output: " + options.output.string());
      destination = &file;
    }
    const auto case_name = options.source_archive_dir.empty() ? options.def.stem().string()
                                                              : options.source_archive_dir.filename().string();
    for (const auto& record : records) writeRecord(*destination, case_name, record);
    writeCounts(*destination, case_name, expected, 0u);
    return;
  }

  const std::size_t export_first = records.size();
  measure(records, "binary_export_tech_total", 0u,
          [&]() { BinaryDatabaseExporter::saveTech(tech_binary, *technology); });
  records.back().output_bytes = fileSize(tech_binary);
  measure(records, "binary_export_library_total", 0u, [&]() {
    BinaryDatabaseExporter::saveLibrary(library_binary, *library);
  });
  records.back().output_bytes = fileSize(library_binary);
  const auto export_design_index = records.size();
  measure(records, "binary_export_design_total", 0u, [&]() {
    BinaryDatabaseExporter::saveDesign(design_binary, *design);
  });
  records.back().output_bytes = fileSize(design_binary);
  addTotal(records, "binary_export_total", export_first, export_design_index);

  design.reset();
  library.reset();
  technology.reset();
  static_cast<void>(settledMemory());

  const std::size_t import_first = records.size();
  measure(records, "binary_import_tech_total", fileSize(tech_binary),
          [&]() { technology = BinaryDatabaseImporter::loadTech(tech_binary); });
  measure(records, "binary_import_library_total", fileSize(library_binary), [&]() {
    library = BinaryDatabaseImporter::loadLibrary(library_binary, *technology);
  });
  const auto import_design_index = records.size();
  measure(records, "binary_import_design_total", fileSize(design_binary), [&]() {
    design = BinaryDatabaseImporter::loadDesign(design_binary, *technology, *library);
  });
  addTotal(records, "binary_import_total", import_first, import_design_index);

  const DatabaseCounts restored = databaseCounts(*technology, *library, *design);
  if (restored != expected) throw std::runtime_error("restored database counts differ from the text-imported database");

  std::ostream* destination = &std::cout;
  std::ofstream file;
  if (!options.output.empty()) {
    file.open(options.output, std::ios::out | std::ios::trunc);
    if (!file) throw std::runtime_error("cannot open benchmark output: " + options.output.string());
    destination = &file;
  }
  std::string case_name = options.def.empty() ? options.source_archive_dir.filename().string() : options.def.stem().string();
  if (case_name.ends_with(".input")) case_name.resize(case_name.size() - std::string_view(".input").size());
  for (const auto& record : records) writeRecord(*destination, case_name, record);
  const uint64_t archive_bytes = fileSize(tech_binary) + fileSize(library_binary) + fileSize(design_binary);
  writeCounts(*destination, case_name, restored, archive_bytes);
  std::cerr << "[benchmark] completed; archive bytes=" << archive_bytes << std::endl;
}

}  // namespace
}  // namespace eccdb

int main(int argc, char** argv)
{
  try {
    eccdb::run(eccdb::parseOptions(argc, argv));
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "binary archive benchmark failed: " << error.what() << '\n';
    return 1;
  }
}
