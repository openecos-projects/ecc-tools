// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
// iEDA is licensed under Mulan PSL v2.
#pragma once

#include "PowerSource.hpp"

namespace iemir {

class PowerSourceReader
{
 public:
  // The single-file API retains legacy source-name inference. List input
  // treats source names as opaque identities across all absolute PLOC files.
  static std::vector<PowerSource> read(const std::string& path, int32_t micron_dbu);
  static std::vector<PowerSource> read(const std::vector<std::string>& paths, int32_t micron_dbu);

 private:
  // Syntax/units are checked here; the graph builder resolves net identity.
  static PowerSource parseRecord(const std::vector<std::string>& fields, int32_t micron_dbu,
                                 const std::string& context, bool infer_legacy_name);
  static std::vector<PowerSource> readFiles(const std::vector<std::string>& paths, int32_t micron_dbu, bool infer_legacy_name);
};

}  // namespace iemir
