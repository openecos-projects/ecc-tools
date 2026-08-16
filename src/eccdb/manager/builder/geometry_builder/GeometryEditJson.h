#pragma once

#include "GeometryEdit.h"

#include <string_view>

namespace ecc::geometry {

GeometryEditCommand parse_geometry_edit_command_json(std::string_view json);

}  // namespace ecc::geometry
