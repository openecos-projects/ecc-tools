// ***************************************************************************************
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
#include "api/internal/DatabaseState.h"

#include <stdexcept>
#include <string_view>
#include <utility>

#include "design/DesignStore.h"
#include "binary/BinaryDatabaseExporter.h"
#include "def/DefDesignExporter.h"
#include "geometry/GeometryPool.h"
#include "binary/BinaryDatabaseImporter.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

#if ECCDB_HAS_LEF_DEF
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#endif

namespace eccdb::detail {

namespace {

void requirePath(const std::filesystem::path& path, std::string_view field)
{
  if (path.empty()) {
    throw std::invalid_argument(std::string(field) + " path must not be empty");
  }
}

void validateBinaryFiles(const BinaryFiles& files)
{
  requirePath(files.technology, "technology binary");
  requirePath(files.library, "library binary");
  requirePath(files.design, "design binary");

  const auto technology = files.technology.lexically_normal();
  const auto library = files.library.lexically_normal();
  const auto design = files.design.lexically_normal();
  if (technology == library || technology == design || library == design) {
    throw std::invalid_argument("technology, library and design binaries must use distinct paths");
  }
}

GeometryPoolOptions geometryOptions(const RuntimeOptions& options)
{
  switch (options.polygon_mode) {
    case PolygonMode::kNative:
      return GeometryPoolOptions{.polygon_mode = PolygonStorageMode::kNative};
    case PolygonMode::kRectangularized:
      return GeometryPoolOptions{.polygon_mode = PolygonStorageMode::kRectangularized};
  }
  throw std::invalid_argument("invalid EccDB polygon mode");
}

}  // namespace

DatabaseState::DatabaseState(DesignStore& borrowed_design) noexcept : _design(&borrowed_design) {}

DatabaseState::~DatabaseState() = default;

std::unique_ptr<DatabaseState> DatabaseState::open(const Config& config)
{
  auto state = std::unique_ptr<DatabaseState>(new DatabaseState);

  if (const auto* binary = std::get_if<BinaryInput>(&config.input)) {
    if (config.binary_cache) {
      throw std::invalid_argument("binary_cache is only valid with LEF/DEF input");
    }
    if (config.runtime.polygon_mode != PolygonMode::kNative) {
      throw std::invalid_argument("polygon mode is stored in binary input and cannot be overridden");
    }
    validateBinaryFiles(binary->files);
    state->_technology = BinaryDatabaseImporter::loadTech(binary->files.technology);
    state->_library = BinaryDatabaseImporter::loadLibrary(binary->files.library, *state->_technology);
    state->_owned_design =
        BinaryDatabaseImporter::loadDesign(binary->files.design, *state->_technology, *state->_library);
    state->_design = state->_owned_design.get();
    return state;
  }

  const auto& lef_def = std::get<LefDefInput>(config.input);
  if (lef_def.lef_files.empty()) {
    throw std::invalid_argument("LEF/DEF input requires at least one LEF file");
  }
  for (const auto& lef : lef_def.lef_files) {
    requirePath(lef, "LEF");
  }
  requirePath(lef_def.def_file, "DEF");

#if ECCDB_HAS_LEF_DEF
  const auto geometry = geometryOptions(config.runtime);
  state->_technology = std::make_unique<TechStore>(TechStoreOptions{.geometry = geometry});
  LefTechImporter(*state->_technology).import(lef_def.lef_files);

  state->_library =
      std::make_unique<LibraryStore>(state->_technology->techRegistry(), LibraryStoreOptions{.geometry = geometry});
  LefLibraryImporter library_importer(*state->_technology, *state->_library);
  library_importer.import(lef_def.lef_files);
  for (const auto& diagnostic : library_importer.diagnostics()) {
    state->_diagnostics.push_back(ImportDiagnostic{.source = "LEF", .statement = diagnostic.statement,
                                                   .occurrence_count = diagnostic.occurrence_count});
  }

  state->_owned_design =
      std::make_unique<DesignStore>(state->_technology->techRegistry(), state->_library->libraryRegistry());
  state->_design = state->_owned_design.get();
  DefDesignImporter design_importer(*state->_design);
  design_importer.import(lef_def.def_file);
  for (const auto& diagnostic : design_importer.diagnostics()) {
    state->_diagnostics.push_back(ImportDiagnostic{.source = "DEF", .statement = diagnostic.statement,
                                                   .occurrence_count = diagnostic.occurrence_count});
  }

  if (config.binary_cache) {
    state->writeBinary(*config.binary_cache);
  }
  return state;
#else
  static_cast<void>(config);
  throw std::runtime_error(
      "this EccDB build has no LEF/DEF parser support; configure with ECCDB_STANDALONE_LEF_DEF=ON");
#endif
}

void DatabaseState::writeDef(const std::filesystem::path& file) const
{
  requirePath(file, "DEF output");
  DefDesignExporter(design()).write(file);
}

void DatabaseState::writeBinary(const BinaryFiles& files) const
{
  validateBinaryFiles(files);
  if (!_technology || !_library || !_owned_design) {
    throw std::logic_error("binary export requires an owning API Database");
  }
  BinaryDatabaseExporter::saveTech(files.technology, *_technology);
  BinaryDatabaseExporter::saveLibrary(files.library, *_library);
  BinaryDatabaseExporter::saveDesign(files.design, *_owned_design);
}

}  // namespace eccdb::detail
