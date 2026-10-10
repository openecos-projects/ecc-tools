#pragma once

#include "GeometryLayerMetadata.h"
#include "GeometryStore.h"

#include <cstdint>
#include <filesystem>
#include <span>

namespace ecc::geometry {

bool render_thumbnail_png(const GeometryStore& store, std::span<const GeometryLayerMetadata> layers,
                          const std::filesystem::path& png_path, uint32_t width, uint32_t height);

}  // namespace ecc::geometry
