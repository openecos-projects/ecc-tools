#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ircx {

struct ProcessPosition
{
  std::size_t line = 1;
  std::size_t column = 1;
  std::size_t offset = 0;
};

struct ProcessQuantity
{
  double value = 0;
  std::string suffix;
  ProcessPosition position;
};

// Owning source properties, before unit conversion or model selection.
// Children keep declaration order, including repeated coefficient blocks.
struct ProcessProperty
{
  std::string key;
  std::string name;
  std::vector<std::string> references;
  std::vector<ProcessQuantity> values;
  std::vector<std::string> modifiers;
  std::vector<ProcessProperty> children;
  ProcessPosition position;
};

}  // namespace ircx
