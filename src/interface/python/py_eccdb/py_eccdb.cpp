// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2026 Peng Cheng Laboratory

#include "py_eccdb.h"

#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "database/Database.h"
#include "database/DatabaseConfig.h"
#include "EccdbCommands.h"

namespace python_interface {
namespace {

namespace py = pybind11;

std::unique_ptr<eccdb::Database> openLefDef(const std::vector<std::filesystem::path>& lef_files,
                                            const std::filesystem::path& def_file)
{
  eccdb::LefDefInput input;
  input.lef_files = lef_files;
  input.def_file = def_file;
  return std::make_unique<eccdb::Database>(eccdb::Database::open(eccdb::Config{.input = std::move(input)}));
}

std::unique_ptr<eccdb::Database> openLefVerilog(const std::vector<std::filesystem::path>& lef_files,
                                                const std::filesystem::path& verilog_file,
                                                const std::string& top_module)
{
  eccdb::LefVerilogInput input;
  input.lef_files = lef_files;
  input.verilog_file = verilog_file;
  input.top_module = top_module;
  return std::make_unique<eccdb::Database>(eccdb::Database::open(eccdb::Config{.input = std::move(input)}));
}

std::unique_ptr<eccdb::Database> openBinary(const std::filesystem::path& technology,
                                            const std::filesystem::path& library,
                                            const std::filesystem::path& design)
{
  return std::make_unique<eccdb::Database>(
      eccdb::Database::open(eccdb::Config{.input = eccdb::BinaryInput{.files = {technology, library, design}}}));
}

std::string optionsToJson(const py::object& options)
{
  if (options.is_none()) {
    return "{}";
  }
  if (!py::isinstance<py::dict>(options)) {
    throw py::type_error("options must be a dict or None");
  }

  // Let Python's standard encoder preserve normal Python values and paths
  // prepared by the caller. Native work begins only after this conversion.
  return py::module_::import("json").attr("dumps")(options).cast<std::string>();
}

void runTool(eccdb::Database& database,
             const std::string& tool,
             const std::optional<std::filesystem::path>& config,
             const py::object& options)
{
  const auto options_json = optionsToJson(options);
  py::gil_scoped_release release;
  ecc::scripting::runTool(database, tool, config.value_or(std::filesystem::path{}), options_json);
}

py::list diagnostics(const eccdb::Database& database)
{
  py::list result;
  for (const auto& diagnostic : database.diagnostics()) {
    py::dict value;
    value["source"] = diagnostic.source;
    value["statement"] = diagnostic.statement;
    value["occurrence_count"] = diagnostic.occurrence_count;
    result.append(std::move(value));
  }
  return result;
}

py::object info(const eccdb::Database& database)
{
  std::string payload;
  {
    py::gil_scoped_release release;
    payload = ecc::scripting::databaseInfo(database);
  }
  return py::module_::import("json").attr("loads")(payload);
}

void bindToolAlias(py::module_& module, const char* name, const char* tool)
{
  module.def(name,
             [tool](eccdb::Database& database,
                    const std::optional<std::filesystem::path>& config,
                    const py::object& options) { runTool(database, tool, config, options); },
             py::arg("database"), py::arg("config") = py::none(), py::arg("options") = py::none());
}

}  // namespace

void register_eccdb(py::module_& module)
{
  auto eccdb_module = module.def_submodule(
      "eccdb",
      "Optional EccDB-native design database and Operation entry points. "
      "This module does not change the legacy iDB-backed commands.");

  // This class deliberately stays module-local: eccdb_viewer can be imported
  // in the same process without duplicate pybind type registration. Both
  // bindings own the same native C++ Database model, but neither requires the
  // other Python extension at runtime.
  py::class_<eccdb::Database, std::unique_ptr<eccdb::Database>>(eccdb_module, "Database", py::module_local())
      .def("import_verilog", [](eccdb::Database& database,
                                 const std::filesystem::path& file,
                                 const std::string& top_module) {
        eccdb::VerilogNetlistImportResult result;
        {
          py::gil_scoped_release release;
          result = database.importVerilog(file, top_module);
        }
        py::dict summary;
        summary["top_module"] = result.top_module;
        summary["instances"] = result.instances;
        summary["instance_terms"] = result.instance_terms;
        summary["block_terms"] = result.block_terms;
        summary["nets"] = result.nets;
        summary["aliases"] = result.aliases;
        return summary;
      }, py::arg("file"), py::arg("top_module") = "")
      .def("write_def", [](const eccdb::Database& database, const std::filesystem::path& file) {
        py::gil_scoped_release release;
        database.writeDef(file);
      }, py::arg("file"))
      .def("write_binary", [](const eccdb::Database& database,
                               const std::filesystem::path& technology,
                               const std::filesystem::path& library,
                               const std::filesystem::path& design) {
        py::gil_scoped_release release;
        database.writeBinary(eccdb::BinaryFiles{technology, library, design});
      }, py::arg("technology"), py::arg("library"), py::arg("design"))
      .def("diagnostics", &diagnostics)
      .def("info", &info)
      .def("info_json", [](const eccdb::Database& database) { return ecc::scripting::databaseInfo(database); })
      .def("run_tool", &runTool,
           py::arg("tool"), py::arg("config") = py::none(), py::arg("options") = py::none());

  eccdb_module.def("open_lef_def", &openLefDef, py::arg("lef_files"), py::arg("def_file"),
                   py::call_guard<py::gil_scoped_release>());
  eccdb_module.def("open_lef_verilog", &openLefVerilog,
                   py::arg("lef_files"), py::arg("verilog_file"), py::arg("top_module") = "",
                   py::call_guard<py::gil_scoped_release>());
  eccdb_module.def("open_binary", &openBinary,
                   py::arg("technology"), py::arg("library"), py::arg("design"),
                   py::call_guard<py::gil_scoped_release>());
  eccdb_module.def("supports_lef_def", &eccdb::Database::supportsLefDef);
  eccdb_module.def("supported_tools", &ecc::scripting::supportedTools);
  eccdb_module.def("run_tool", &runTool, py::arg("database"), py::arg("tool"),
                   py::arg("config") = py::none(), py::arg("options") = py::none());

  bindToolAlias(eccdb_module, "run_fp", "fp");
  bindToolAlias(eccdb_module, "run_ifp", "fp");
  bindToolAlias(eccdb_module, "run_cts", "cts");
  bindToolAlias(eccdb_module, "run_rt", "rt");
  bindToolAlias(eccdb_module, "run_drc", "drc");
  bindToolAlias(eccdb_module, "run_sta", "sta");
  bindToolAlias(eccdb_module, "run_pw", "pw");
  bindToolAlias(eccdb_module, "run_emir", "emir");
  bindToolAlias(eccdb_module, "run_mj", "mj");
  bindToolAlias(eccdb_module, "run_lvs", "lvs");
}

}  // namespace python_interface
