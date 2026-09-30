// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <filesystem>

namespace drt {
class frDesign;
namespace io {
void writeNativeRouteSnapshot(const frDesign& design,
                              const std::filesystem::path& path,
                              bool include_routes,
                              bool include_runtime_details = true);
}
}
