#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "SpefParser.hh"

namespace spef {

namespace text {

auto trimView(std::string_view value) -> std::string_view;
auto startsWith(std::string_view value,
                std::string_view prefix) -> bool;
auto takeToken(std::string_view& value) -> std::string_view;
auto parseDouble(std::string_view value) -> std::optional<double>;
auto parseInt(std::string_view value) -> std::optional<int>;
auto joinHeaderValues(const std::vector<std::string>& values) -> std::string;
auto startsWithNameIndex(const std::string& name) -> bool;

}  // namespace text

}  // namespace spef
