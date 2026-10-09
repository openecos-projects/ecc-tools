// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once
#include <string>
#include <string_view>
namespace idb::verilog {
bool isKeyword(std::string_view name);
bool isSimpleIdentifier(std::string_view name);
// Accepts a canonical name (no lexical delimiter). Escaped output includes its
// mandatory trailing whitespace, and preserves all internal backslashes.
std::string encodeIdentifier(std::string_view name);
}  // namespace idb::verilog
