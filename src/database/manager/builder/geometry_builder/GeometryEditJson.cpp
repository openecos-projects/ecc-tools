#include "GeometryEditJson.h"

#include "GeometryTypes.h"

#include <optional>
#include <regex>
#include <string>

namespace ecc::geometry {

namespace {

std::optional<int64_t> json_i64(std::string_view json, std::string_view key)
{
  const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*(-?[0-9]+)");
  std::match_results<std::string_view::const_iterator> match;
  if (!std::regex_search(json.begin(), json.end(), match, pattern)) {
    return std::nullopt;
  }

  return std::stoll(match[1].str());
}

std::optional<std::string> json_string(std::string_view json, std::string_view key)
{
  const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*\"([^\"]*)\"");
  std::match_results<std::string_view::const_iterator> match;
  if (!std::regex_search(json.begin(), json.end(), match, pattern)) {
    return std::nullopt;
  }

  return match[1].str();
}

GeometryEditOp parse_edit_op(std::string_view value)
{
  if (value == "resize_rect") {
    return GeometryEditOp::kResizeRect;
  }
  if (value == "replace_line") {
    return GeometryEditOp::kReplaceLine;
  }
  return GeometryEditOp::kMoveShape;
}

}  // namespace

GeometryEditCommand parse_geometry_edit_command_json(std::string_view json)
{
  GeometryEditCommand command;

  command.command_id = static_cast<uint64_t>(json_i64(json, "command_id").value_or(0));
  command.shape_id = static_cast<ShapeId>(json_i64(json, "shape_id").value_or(0));
  command.expected_version = static_cast<ShapeVersion>(json_i64(json, "expected_version").value_or(0));
  command.op = parse_edit_op(json_string(json, "op").value_or("move_shape"));
  command.requested_bbox = normalize(Rect32{
      static_cast<int32_t>(json_i64(json, "lx").value_or(0)),
      static_cast<int32_t>(json_i64(json, "ly").value_or(0)),
      static_cast<int32_t>(json_i64(json, "hx").value_or(0)),
      static_cast<int32_t>(json_i64(json, "hy").value_or(0)),
  });

  return command;
}

}  // namespace ecc::geometry
