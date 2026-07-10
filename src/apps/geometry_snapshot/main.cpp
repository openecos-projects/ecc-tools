#include "GeometryBuilder.h"
#include "GeometryEditApplier.h"
#include "GeometrySnapshotReader.h"
#include "GeometrySnapshotWriter.h"
#include "GeometryStore.h"
#include "builder.h"
#include "def_write.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CliOptions
{
  std::vector<std::string> tech_lefs;
  std::vector<std::string> lefs;
  std::string def_path;
  std::string output_dir;
  std::string edit_command_path;
  std::string edit_result_path;
  std::string write_def_path;
  std::string mode = "snapshot";
  uint64_t synthetic_shape_count = 100000;
  bool help = false;
};

void print_usage(std::ostream& out)
{
  out << "Usage: ecc-geometry-snapshot --lef <file> [--lef <file> ...] --def <file> --out <dir> [--mode snapshot]\n"
      << "       ecc-geometry-snapshot --tech-lef <file> --lef <file> --def <file> --out <dir>\n"
      << "       ecc-geometry-snapshot --lef <file> --def <file> --out <dir> --mode apply-edit "
         "--edit-command <json> --edit-result <json> [--write-def <file>]\n"
      << "       ecc-geometry-snapshot --mode synthetic --out <dir> [--synthetic-shapes <count>]\n";
}

bool parse_args(int argc, char** argv, CliOptions& options)
{
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      options.help = true;
      return true;
    }

    auto require_value = [&](const char* option_name) -> const char* {
      if (index + 1 >= argc) {
        std::cerr << option_name << " requires a value\n";
        return nullptr;
      }
      return argv[++index];
    };

    if (arg == "--tech-lef") {
      const char* value = require_value("--tech-lef");
      if (value == nullptr) {
        return false;
      }
      options.tech_lefs.emplace_back(value);
    } else if (arg == "--lef") {
      const char* value = require_value("--lef");
      if (value == nullptr) {
        return false;
      }
      options.lefs.emplace_back(value);
    } else if (arg == "--def") {
      const char* value = require_value("--def");
      if (value == nullptr) {
        return false;
      }
      options.def_path = value;
    } else if (arg == "--out") {
      const char* value = require_value("--out");
      if (value == nullptr) {
        return false;
      }
      options.output_dir = value;
    } else if (arg == "--edit-command") {
      const char* value = require_value("--edit-command");
      if (value == nullptr) {
        return false;
      }
      options.edit_command_path = value;
    } else if (arg == "--edit-result") {
      const char* value = require_value("--edit-result");
      if (value == nullptr) {
        return false;
      }
      options.edit_result_path = value;
    } else if (arg == "--write-def") {
      const char* value = require_value("--write-def");
      if (value == nullptr) {
        return false;
      }
      options.write_def_path = value;
    } else if (arg == "--mode") {
      const char* value = require_value("--mode");
      if (value == nullptr) {
        return false;
      }
      options.mode = value;
    } else if (arg == "--synthetic-shapes") {
      const char* value = require_value("--synthetic-shapes");
      if (value == nullptr) {
        return false;
      }
      options.synthetic_shape_count = std::stoull(value);
    } else {
      std::cerr << "unknown option: " << arg << "\n";
      return false;
    }
  }

  return true;
}

bool validate_options(const CliOptions& options)
{
  if (options.help) {
    return true;
  }
  if (options.mode != "snapshot" && options.mode != "apply-edit" && options.mode != "synthetic") {
    std::cerr << "unsupported --mode: " << options.mode << "\n";
    return false;
  }
  if (options.output_dir.empty()) {
    std::cerr << "--out is required\n";
    return false;
  }
  if (options.mode == "synthetic") {
    if (options.synthetic_shape_count == 0) {
      std::cerr << "--synthetic-shapes must be greater than zero\n";
      return false;
    }
    return true;
  }
  if (options.lefs.empty() && options.tech_lefs.empty()) {
    std::cerr << "at least one --lef or --tech-lef is required\n";
    return false;
  }
  if (options.def_path.empty()) {
    std::cerr << "--def is required\n";
    return false;
  }
  if (options.mode == "apply-edit" && (options.edit_command_path.empty() || options.edit_result_path.empty())) {
    std::cerr << "--mode apply-edit requires --edit-command and --edit-result\n";
    return false;
  }
  return true;
}

bool ends_with(std::string_view value, std::string_view suffix)
{
  return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::optional<std::string> read_text_file(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input) {
    return std::nullopt;
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

bool write_text_file(const std::filesystem::path& path, std::string_view content)
{
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path());
  }
  std::ofstream output(path);
  if (!output) {
    return false;
  }

  output << content;
  return static_cast<bool>(output);
}

std::optional<int64_t> json_i64(std::string_view json, std::string_view key)
{
  const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*(-?[0-9]+)");
  std::match_results<std::string_view::const_iterator> match;
  if (!std::regex_search(json.begin(), json.end(), match, pattern)) {
    return std::nullopt;
  }

  return std::stoll(match[1].str());
}

ecc::geometry::GeometryEditCommand parse_edit_command(std::string_view json)
{
  ecc::geometry::GeometryEditCommand command;

  command.command_id = static_cast<uint64_t>(json_i64(json, "command_id").value_or(0));
  command.shape_id = static_cast<ecc::geometry::ShapeId>(json_i64(json, "shape_id").value_or(0));
  command.expected_version = static_cast<ecc::geometry::ShapeVersion>(json_i64(json, "expected_version").value_or(0));
  command.op = ecc::geometry::GeometryEditOp::kMoveShape;
  command.requested_bbox = ecc::geometry::normalize(ecc::geometry::Rect32{
      static_cast<int32_t>(json_i64(json, "lx").value_or(0)),
      static_cast<int32_t>(json_i64(json, "ly").value_or(0)),
      static_cast<int32_t>(json_i64(json, "hx").value_or(0)),
      static_cast<int32_t>(json_i64(json, "hy").value_or(0)),
  });

  return command;
}

std::string edit_status_name(ecc::geometry::GeometryEditStatus status)
{
  switch (status) {
    case ecc::geometry::GeometryEditStatus::kAccepted:
      return "accepted";
    case ecc::geometry::GeometryEditStatus::kAdjustedAccepted:
      return "adjusted_accepted";
    case ecc::geometry::GeometryEditStatus::kRejected:
      return "rejected";
    case ecc::geometry::GeometryEditStatus::kConflict:
      return "conflict";
  }
  return "rejected";
}

uint64_t grid_column_count(uint64_t shape_count)
{
  uint64_t columns = 1;
  while (columns * columns < shape_count) {
    ++columns;
  }
  return columns;
}

ecc::geometry::GeometryBuildResult build_synthetic_geometry(uint64_t shape_count, ecc::geometry::GeometryStore& store)
{
  store.clear();
  store.add_owner_name(ecc::geometry::OwnerType::kNetWireSegment, 1, "synthetic_clk");

  constexpr int32_t kSpacing = 200;
  constexpr int32_t kRectSize = 80;
  constexpr uint64_t kLargeShapePeriod = 4096;

  const uint64_t columns = grid_column_count(shape_count);
  const int32_t full_span = static_cast<int32_t>(columns * kSpacing + kRectSize);

  for (uint64_t index = 0; index < shape_count; ++index) {
    ecc::geometry::OwnerRef owner;
    owner.type = ecc::geometry::OwnerType::kNetWireSegment;
    owner.owner_id = 1;
    owner.path0 = static_cast<uint32_t>(index);

    const ecc::geometry::LayerId layer = static_cast<ecc::geometry::LayerId>(index % 8);
    ecc::geometry::Rect32 rect;
    if (index % kLargeShapePeriod == 0) {
      rect = ecc::geometry::Rect32{0, 0, full_span, full_span};
    } else {
      const int32_t x = static_cast<int32_t>((index % columns) * kSpacing);
      const int32_t y = static_cast<int32_t>((index / columns) * kSpacing);
      rect = ecc::geometry::Rect32{x, y, x + kRectSize, y + kRectSize};
    }
    store.add_rect(layer, rect, owner);
  }

  ecc::geometry::GeometryBuildResult result;
  result.shape_count = shape_count;
  result.net_wire_shape_count = shape_count;
  return result;
}

std::string edit_result_json(const ecc::geometry::GeometryEditResult& result)
{
  std::ostringstream json;
  json << "{\n"
       << "  \"command_id\": " << result.command_id << ",\n"
       << "  \"shape_id\": " << result.shape_id << ",\n"
       << "  \"new_version\": " << result.new_version << ",\n"
       << "  \"status\": \"" << edit_status_name(result.status) << "\",\n"
       << "  \"committed_bbox\": {\n"
       << "    \"lx\": " << result.committed_bbox.lx << ",\n"
       << "    \"ly\": " << result.committed_bbox.ly << ",\n"
       << "    \"hx\": " << result.committed_bbox.hx << ",\n"
       << "    \"hy\": " << result.committed_bbox.hy << "\n"
       << "  }\n"
       << "}\n";
  return json.str();
}

void print_geometry_report(const ecc::geometry::GeometryBuildResult& build_result, const ecc::geometry::GeometryStore& store,
                           std::ostream& out)
{
  out << "shape_count=" << build_result.shape_count << "\n";
  for (const auto& [owner_type, count] : store.count_alive_shapes_by_owner_type()) {
    out << "owner_type." << ecc::geometry::owner_type_label(owner_type) << "=" << count << "\n";
  }
  for (const auto& [layer_id, count] : store.count_alive_shapes_by_layer()) {
    out << "layer." << layer_id << "=" << count << "\n";
  }
}

}  // namespace

int main(int argc, char** argv)
{
  CliOptions options;
  if (!parse_args(argc, argv, options) || !validate_options(options)) {
    print_usage(std::cerr);
    return 1;
  }

  if (options.help) {
    print_usage(std::cout);
    return 0;
  }

  if (options.mode == "synthetic") {
    ecc::geometry::GeometryStore store;
    const ecc::geometry::GeometryBuildResult build_result = build_synthetic_geometry(options.synthetic_shape_count, store);
    ecc::geometry::GeometrySnapshotWriter writer;
    const ecc::geometry::SnapshotWriteResult write_result =
        writer.write(store, ecc::geometry::SnapshotWriteOptions{std::filesystem::path(options.output_dir)});
    if (!write_result.ok) {
      std::cerr << "failed to write geometry snapshot: " << options.output_dir << "\n";
      return 1;
    }

    std::cout << "geometry snapshot written: " << (std::filesystem::path(options.output_dir) / "geometry.manifest").string()
              << "\n";
    print_geometry_report(build_result, store, std::cout);
    return 0;
  }

  idb::IdbBuilder idb_builder;
  if (!options.tech_lefs.empty() && idb_builder.buildLef(options.tech_lefs, true) == nullptr) {
    std::cerr << "failed to read tech lef\n";
    return 1;
  }

  if (!options.lefs.empty() && idb_builder.buildLef(options.lefs) == nullptr) {
    std::cerr << "failed to read lef\n";
    return 1;
  }

  idb::IdbDefService* def_service =
      ends_with(options.def_path, ".gz") ? idb_builder.buildDefGzip(options.def_path) : idb_builder.buildDef(options.def_path);
  if (def_service == nullptr || def_service->get_design() == nullptr || def_service->get_layout() == nullptr) {
    std::cerr << "failed to read def\n";
    return 1;
  }

  ecc::geometry::GeometryStore store;
  const std::filesystem::path existing_manifest = std::filesystem::path(options.output_dir) / "geometry.manifest";
  if (std::filesystem::exists(existing_manifest)) {
    ecc::geometry::GeometrySnapshotReader reader;
    const ecc::geometry::SnapshotReadResult read_result =
        reader.read(ecc::geometry::SnapshotReadOptions{existing_manifest}, store);
    if (!read_result.ok) {
      if (options.mode == "apply-edit") {
        std::cerr << "failed to restore existing geometry snapshot for edit: " << existing_manifest.string() << "\n";
        return 1;
      }
      std::cerr << "warning: failed to restore existing geometry snapshot, rebuilding with fresh shape ids\n";
      store.clear();
    }
  }

  ecc::geometry::GeometryBuilder geometry_builder;
  const ecc::geometry::GeometryBuildResult build_result =
      geometry_builder.rebuild_from_design(*def_service->get_design(), *def_service->get_layout(), store);

  if (options.mode == "apply-edit") {
    const std::optional<std::string> command_json = read_text_file(options.edit_command_path);
    if (!command_json.has_value()) {
      std::cerr << "failed to read edit command: " << options.edit_command_path << "\n";
      return 1;
    }

    const ecc::geometry::GeometryEditCommand command = parse_edit_command(*command_json);
    const ecc::geometry::GeometryEditApplier applier;
    const ecc::geometry::GeometryEditResult result = applier.apply_edit(command, *def_service->get_design(), store);

    if (!write_text_file(options.edit_result_path, edit_result_json(result))) {
      std::cerr << "failed to write edit result: " << options.edit_result_path << "\n";
      return 1;
    }

    if (!options.write_def_path.empty()) {
      idb::DefWrite def_writer(def_service, idb::DefWriteType::kChip);
      if (!def_writer.writeDb(options.write_def_path.c_str())) {
        std::cerr << "failed to write edited def: " << options.write_def_path << "\n";
        return 1;
      }
    }
  }

  ecc::geometry::GeometrySnapshotWriter writer;
  const ecc::geometry::SnapshotWriteResult write_result =
      writer.write(store, ecc::geometry::SnapshotWriteOptions{std::filesystem::path(options.output_dir)});
  if (!write_result.ok) {
    std::cerr << "failed to write geometry snapshot: " << options.output_dir << "\n";
    return 1;
  }

  std::cout << "geometry snapshot written: " << (std::filesystem::path(options.output_dir) / "geometry.manifest").string()
            << "\n";
  print_geometry_report(build_result, store, std::cout);
  return 0;
}
