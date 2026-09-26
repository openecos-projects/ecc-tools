#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace idb {

struct GdsLayerMapValue
{
  uint32_t layer = 0;
  uint32_t datatype = 0;
};

class GdsLayerMap
{
 public:
  bool load(const std::string& path);
  bool empty() const { return _entries.empty(); }

  bool resolve(const std::string& layer_name, const std::string& purpose, GdsLayerMapValue& value) const;
  const std::string& error() const { return _error; }

 private:
  static std::string normalize(const std::string& value);
  static std::string trim(const std::string& value);
  static bool parse_uint(const std::string& token, uint32_t& value);
  static std::string make_key(const std::string& layer_name, const std::string& purpose);
  bool add_entry(const std::string& layer_name, const std::string& purpose, const GdsLayerMapValue& value, size_t line_number);

  std::unordered_map<std::string, GdsLayerMapValue> _entries;
  std::string _error;
};

}  // namespace idb
