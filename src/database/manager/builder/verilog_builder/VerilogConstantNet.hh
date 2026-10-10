// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once
#include <string_view>
namespace idb {
// Private import/export convention: IDB has power/ground net types, but no four-state value field.
inline constexpr std::string_view verilogZeroNet = "__ecc_verilog_constant_0";
inline constexpr std::string_view verilogOneNet = "__ecc_verilog_constant_1";
}  // namespace idb
