#include "feature_manager.h"
#include "idm.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
struct MatrixShape
{
  int32_t rows = 0;
  int32_t cols = 0;
};

std::string readText(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open: " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

MatrixShape readCsvShape(const fs::path& csv_path)
{
  std::ifstream file(csv_path);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open csv: " + csv_path.string());
  }

  std::string line;
  MatrixShape shape;
  while (std::getline(file, line)) {
    if (line.empty()) {
      continue;
    }
    int32_t cols = 1;
    for (char ch : line) {
      if (ch == ',') {
        ++cols;
      }
    }
    if (shape.cols == 0) {
      shape.cols = cols;
    } else if (shape.cols != cols) {
      throw std::runtime_error("ragged csv: " + csv_path.string());
    }
    ++shape.rows;
  }
  if (shape.rows <= 0 || shape.cols <= 0) {
    throw std::runtime_error("empty csv: " + csv_path.string());
  }
  return shape;
}

int64_t countLines(const fs::path& path)
{
  std::ifstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("failed to open: " + path.string());
  }
  return std::count(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>(), '\n');
}

fs::path firstMapCsv(const fs::path& dir)
{
  std::vector<fs::path> csv_files;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".csv" && entry.path().filename() != "layout.csv") {
      csv_files.push_back(entry.path());
    }
  }
  std::sort(csv_files.begin(), csv_files.end());
  if (csv_files.empty()) {
    throw std::runtime_error("no map csv in: " + dir.string());
  }
  return csv_files.front();
}

std::vector<fs::path> mapCsvFiles(const fs::path& dir)
{
  std::vector<fs::path> csv_files;
  for (const auto& entry : fs::directory_iterator(dir)) {
    if (entry.path().extension() == ".csv" && entry.path().filename() != "layout.csv") {
      csv_files.push_back(entry.path().filename());
    }
  }
  std::sort(csv_files.begin(), csv_files.end());
  return csv_files;
}

void requireExists(const fs::path& path)
{
  if (!fs::exists(path)) {
    throw std::runtime_error("missing required path: " + path.string());
  }
}

void initDatabaseFromPlacedDef(const fs::path& workspace, const fs::path& generated_feature_dir)
{
  const fs::path config_path = workspace.parent_path() / "config" / "db_default_config.json";
  const fs::path def_path = workspace / "output" / "gcd_place.def.gz";
  requireExists(config_path);
  requireExists(def_path);

  dmInst->reset();
  if (!dmInst->readLef(config_path.string())) {
    throw std::runtime_error("failed to read LEF from config: " + config_path.string());
  }

  dmInst->get_config().set_def_path(def_path.string());
  dmInst->get_config().set_feature_path(generated_feature_dir.string());
  dmInst->get_config().set_output_path((generated_feature_dir.parent_path() / "output").string());
  if (!dmInst->readDef(def_path.string())) {
    throw std::runtime_error("failed to read DEF: " + def_path.string());
  }
}

int32_t inferFeatureGridSize(const fs::path& workspace)
{
  const MatrixShape reference_shape = readCsvShape(firstMapCsv(workspace / "feature" / "density_map"));
  const int32_t die_width = dmInst->get_idb_layout()->get_die()->get_bounding_box()->get_width();
  const int32_t row_height = dmInst->get_idb_layout()->get_rows()->get_row_height();
  if (reference_shape.cols <= 0 || die_width <= 0 || row_height <= 0) {
    throw std::runtime_error("failed to infer feature grid size from reference density map");
  }

  const double effective_grid = static_cast<double>(die_width) / static_cast<double>(reference_shape.cols);
  return std::max(1, static_cast<int32_t>(std::llround(effective_grid / row_height)));
}

void regenerateMaps(const fs::path& workspace, const fs::path& generated_feature_dir)
{
  fs::remove_all(generated_feature_dir.parent_path());
  fs::create_directories(generated_feature_dir);

  initDatabaseFromPlacedDef(workspace, generated_feature_dir);

  const int32_t grid_size = inferFeatureGridSize(workspace);
  if (!featureInst->save_pl_eval((generated_feature_dir / "place.map.json").string(), grid_size)) {
    throw std::runtime_error("FeatureManager::save_pl_eval failed");
  }
}

void compareMapDirectory(const fs::path& reference_dir, const fs::path& generated_dir)
{
  requireExists(generated_dir / "layout.csv");

  const MatrixShape shape = readCsvShape(firstMapCsv(generated_dir));
  const int64_t expected_layout_lines = static_cast<int64_t>(shape.rows) * shape.cols + 1;
  const int64_t actual_layout_lines = countLines(generated_dir / "layout.csv");
  if (actual_layout_lines != expected_layout_lines) {
    throw std::runtime_error("layout line count mismatch: " + generated_dir.string());
  }

  const std::vector<fs::path> reference_maps = mapCsvFiles(reference_dir);
  const std::vector<fs::path> generated_maps = mapCsvFiles(generated_dir);
  if (reference_maps != generated_maps) {
    throw std::runtime_error("generated map file list differs from reference: " + generated_dir.string());
  }

  for (const fs::path& csv_name : reference_maps) {
    const fs::path reference_csv = reference_dir / csv_name;
    const fs::path generated_csv = generated_dir / csv_name;
    if (readText(reference_csv) != readText(generated_csv)) {
      throw std::runtime_error("map csv differs from reference: " + generated_csv.string());
    }
  }
}

void verifyReplay(const fs::path& workspace, const fs::path& generated_feature_dir)
{
  const fs::path reference_feature_dir = workspace / "feature";
  const std::vector<std::string> map_dirs = {"density_map", "RUDY_map", "margin_map", "egr_congestion_map"};
  for (const std::string& map_dir : map_dirs) {
    compareMapDirectory(reference_feature_dir / map_dir, generated_feature_dir / map_dir);
  }
}
}  // namespace

int main(int argc, char** argv)
{
  fs::path workspace;
  if (argc >= 2) {
    workspace = argv[1];
  } else {
    workspace = fs::current_path() / "ecc-runs" / "ecos-ecc-cli-gcd-ics55-20260708-133807" / "workspace" / "place_dreamplace";
  }

  fs::path generated_feature_dir;
  if (argc >= 3) {
    generated_feature_dir = argv[2];
  } else {
    generated_feature_dir = fs::current_path() / "verify" / "eval_map_layout_reference_replay" / "place_dreamplace_feature";
  }

  try {
    regenerateMaps(workspace, generated_feature_dir);
    verifyReplay(workspace, generated_feature_dir);
  } catch (const std::exception& error) {
    std::cerr << error.what() << "\n";
    return 1;
  }

  std::cout << "map replay ok: " << generated_feature_dir << "\n";
  return 0;
}
