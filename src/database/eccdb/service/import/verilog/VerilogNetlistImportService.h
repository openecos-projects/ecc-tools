// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#pragma once

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

#include "context/DatabaseContext.h"

namespace eccdb {

struct VerilogNetlistImportOptions
{
  std::filesystem::path file;
  std::string top_module;
  bool flatten_hierarchy = true;
};

struct VerilogNetlistImportResult
{
  std::string top_module;
  std::size_t instances = 0;
  std::size_t instance_terms = 0;
  std::size_t block_terms = 0;
  std::size_t nets = 0;
  std::size_t aliases = 0;
};

class VerilogNetlistImportError : public std::runtime_error
{
 public:
  VerilogNetlistImportError(std::size_t line, std::string message);

  [[nodiscard]] std::size_t line() const noexcept { return _line; }

 private:
  std::size_t _line;
};

// Imports a structural (post-synthesis) Verilog module into the EccDB design
// netlist. The parser is deliberately kept separate from the database: this
// service owns the semantic projection and all cross-object mutations.
class VerilogNetlistImportService
{
 public:
  explicit VerilogNetlistImportService(DatabaseContext& context) noexcept : _context(context) {}

  [[nodiscard]] VerilogNetlistImportResult import(const VerilogNetlistImportOptions& options);
  [[nodiscard]] VerilogNetlistImportResult import(const std::filesystem::path& file,
                                                  std::string_view top_module = {});

 private:
  DatabaseContext& _context;
};

}  // namespace eccdb
