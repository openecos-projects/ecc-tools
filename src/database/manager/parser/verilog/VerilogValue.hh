// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace idb::verilog {

// Four-state, most-significant-bit-first value. Width and signedness are
// independent: IEEE 1364-2005 3.5.1 and 5.4/5.5.
class BitVector
{
 public:
  static constexpr uint32_t kMaxWidth = 1U << 20;
  BitVector() = default;
  explicit BitVector(std::string bits, bool is_signed = false, bool unsized = false);
  static BitVector parse(std::string_view literal);
  static BitVector fromInteger(int64_t value, uint32_t width = 32, bool is_signed = true);

  std::string_view bits() const { return _bits; }
  uint32_t width() const { return static_cast<uint32_t>(_bits.size()); }
  bool isSigned() const { return _signed; }
  bool isUnsized() const { return _unsized; }
  bool known() const;
  std::optional<int64_t> integer() const;
  BitVector resized(uint32_t width) const;
  BitVector resized(uint32_t width, bool sign_extend) const;
  BitVector unary(std::string_view op) const;
  BitVector binary(std::string_view op, const BitVector& rhs) const;
  char truth() const;

 private:
  std::string _bits = "0";
  bool _signed = false;
  bool _unsized = false;
};

}  // namespace idb::verilog
