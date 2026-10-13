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

#include "database/Database.h"

#include <stdexcept>
#include <string_view>
#include <utility>

#include "binary/BinaryDatabaseExporter.h"
#include "binary/BinaryDatabaseImporter.h"
#include "context/DatabaseContext.h"
#include "def/DefDesignExporter.h"
#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

#if ECCDB_HAS_VERILOG
#include "import/verilog/VerilogNetlistImportService.h"
#endif

#if ECCDB_HAS_LEF_DEF
#include "def/DefDesignImporter.h"
#include "lef/LefLibraryImporter.h"
#include "lef/LefTechImporter.h"
#endif

namespace eccdb {
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

}  // namespace

// Only lifetime state lives here. Keeping its address stable lets native
// services continue borrowing the Context when the owning Database moves.
// Declaration order releases Context before Design, Library and Technology.
struct Database::State
{
  State(std::unique_ptr<TechStore> technology_value,
        std::unique_ptr<LibraryStore> library_value,
        std::unique_ptr<DesignStore> design_value,
        std::vector<ImportDiagnostic> diagnostic_values)
      : technology(std::move(technology_value)),
        library(std::move(library_value)),
        design(std::move(design_value)),
        context(*technology, *library, *design),
        diagnostics(std::move(diagnostic_values))
  {
  }

  std::unique_ptr<TechStore> technology;
  std::unique_ptr<LibraryStore> library;
  std::unique_ptr<DesignStore> design;
  DatabaseContext context;
  std::vector<ImportDiagnostic> diagnostics;
};

Database::Database(std::unique_ptr<State> state) noexcept : _state(std::move(state)) {}

Database::~Database() = default;
Database::Database(Database&&) noexcept = default;
Database& Database::operator=(Database&&) noexcept = default;

Database Database::open(const Config& config)
{
  std::unique_ptr<TechStore> technology;
  std::unique_ptr<LibraryStore> library;
  std::unique_ptr<DesignStore> design;
  std::vector<ImportDiagnostic> diagnostics;

  if (const auto* binary = std::get_if<BinaryInput>(&config.input)) {
    if (config.binary_cache) {
      throw std::invalid_argument("binary_cache is only valid with LEF/DEF input");
    }
    validateBinaryFiles(binary->files);
    technology = BinaryDatabaseImporter::loadTech(binary->files.technology);
    library = BinaryDatabaseImporter::loadLibrary(binary->files.library, *technology);
    design = BinaryDatabaseImporter::loadDesign(binary->files.design, *technology, *library);
  } else if (const auto* lef_verilog = std::get_if<LefVerilogInput>(&config.input)) {
    if (lef_verilog->lef_files.empty()) {
      throw std::invalid_argument("LEF/Verilog input requires at least one LEF file");
    }
    for (const auto& lef : lef_verilog->lef_files) {
      requirePath(lef, "LEF");
    }
    requirePath(lef_verilog->verilog_file, "Verilog");

#if ECCDB_HAS_LEF_DEF
    technology = std::make_unique<TechStore>();
    LefTechImporter(*technology).import(lef_verilog->lef_files);

    library = std::make_unique<LibraryStore>(technology->techRegistry());
    LefLibraryImporter library_importer(*technology, *library);
    library_importer.import(lef_verilog->lef_files);
    for (const auto& diagnostic : library_importer.diagnostics()) {
      diagnostics.push_back(ImportDiagnostic{.source = "LEF",
                                            .statement = diagnostic.statement,
                                            .occurrence_count = diagnostic.occurrence_count});
    }
    design = std::make_unique<DesignStore>(technology->techRegistry(), library->libraryRegistry(),
                                           &technology->geometryRegistry());
#else
    throw std::runtime_error(
        "this EccDB build has no LEF parser support; configure with ECCDB_STANDALONE_LEF_DEF=ON");
#endif
  } else {
    const auto& lef_def = std::get<LefDefInput>(config.input);
    if (lef_def.lef_files.empty()) {
      throw std::invalid_argument("LEF/DEF input requires at least one LEF file");
    }
    for (const auto& lef : lef_def.lef_files) {
      requirePath(lef, "LEF");
    }
    requirePath(lef_def.def_file, "DEF");

#if ECCDB_HAS_LEF_DEF
    technology = std::make_unique<TechStore>();
    LefTechImporter(*technology).import(lef_def.lef_files);

    library = std::make_unique<LibraryStore>(technology->techRegistry());
    LefLibraryImporter library_importer(*technology, *library);
    library_importer.import(lef_def.lef_files);
    for (const auto& diagnostic : library_importer.diagnostics()) {
      diagnostics.push_back(ImportDiagnostic{.source = "LEF",
                                            .statement = diagnostic.statement,
                                            .occurrence_count = diagnostic.occurrence_count});
    }

    design = std::make_unique<DesignStore>(technology->techRegistry(), library->libraryRegistry(),
                                         &technology->geometryRegistry());
    DefDesignImporter design_importer(*design);
    design_importer.import(lef_def.def_file);
    for (const auto& diagnostic : design_importer.diagnostics()) {
      diagnostics.push_back(ImportDiagnostic{.source = "DEF",
                                            .statement = diagnostic.statement,
                                            .occurrence_count = diagnostic.occurrence_count});
    }
#else
    throw std::runtime_error(
        "this EccDB build has no LEF/DEF parser support; configure with ECCDB_STANDALONE_LEF_DEF=ON");
#endif
  }

  Database database{std::make_unique<State>(std::move(technology), std::move(library), std::move(design),
                                          std::move(diagnostics))};
  if (const auto* lef_verilog = std::get_if<LefVerilogInput>(&config.input)) {
    static_cast<void>(database.importVerilog(lef_verilog->verilog_file, lef_verilog->top_module));
  }
  if (config.binary_cache) {
    database.writeBinary(*config.binary_cache);
  }
  return database;
}

bool Database::supportsLefDef() noexcept
{
#if ECCDB_HAS_LEF_DEF
  return true;
#else
  return false;
#endif
}

Database::State& Database::state() const
{
  if (!_state) {
    throw std::logic_error("operation on a moved-from EccDB Database");
  }
  return *_state;
}

DatabaseContext& Database::context() const
{
  return state().context;
}

const std::vector<ImportDiagnostic>& Database::diagnostics() const noexcept
{
  static const std::vector<ImportDiagnostic> empty;
  return _state ? _state->diagnostics : empty;
}

VerilogNetlistImportResult Database::importVerilog(const std::filesystem::path& file,
                                                   std::string_view top_module)
{
#if ECCDB_HAS_VERILOG
  return VerilogNetlistImportService(state().context).import(file, top_module);
#else
  static_cast<void>(file);
  static_cast<void>(top_module);
  throw std::runtime_error("this EccDB build has no Verilog parser support");
#endif
}

void Database::writeDef(const std::filesystem::path& file) const
{
  const auto& owned = state();
  requirePath(file, "DEF output");
  DefDesignExporter(*owned.design).write(file);
}

void Database::writeBinary(const BinaryFiles& files) const
{
  const auto& owned = state();
  validateBinaryFiles(files);
  BinaryDatabaseExporter::saveTech(files.technology, *owned.technology);
  BinaryDatabaseExporter::saveLibrary(files.library, *owned.library);
  BinaryDatabaseExporter::saveDesign(files.design, *owned.design);
}

}  // namespace eccdb
