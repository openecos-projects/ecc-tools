// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogValue.hh"

#include <algorithm>
#include <boost/multiprecision/cpp_int.hpp>
#include <cctype>
#include <limits>
#include <stdexcept>

namespace idb::verilog {
namespace {
using boost::multiprecision::cpp_int;

void checkWidth(uint64_t width)
{
  if (width == 0 || width > BitVector::kMaxWidth) {
    throw std::invalid_argument("integer width must be between 1 and " + std::to_string(BitVector::kMaxWidth));
  }
}

std::string_view trim(std::string_view text)
{
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
    text.remove_prefix(1);
  while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
    text.remove_suffix(1);
  return text;
}

std::string digits(std::string_view text)
{
  if (text.empty() || text.front() == '_')
    throw std::invalid_argument("missing integer digits");
  std::string result;
  for (const auto ch : text) {
    if (ch != '_')
      result += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return result;
}

cpp_int unsignedValue(std::string_view bits)
{
  cpp_int value = 0;
  for (char ch : bits) {
    value <<= 1;
    if (ch == '1')
      ++value;
  }
  return value;
}

cpp_int signedValue(const BitVector& value)
{
  cpp_int number = unsignedValue(value.bits());
  if (value.isSigned() && value.bits().front() == '1')
    number -= cpp_int(1) << value.width();
  return number;
}

BitVector fromValue(cpp_int value, uint32_t width, bool is_signed)
{
  checkWidth(width);
  const cpp_int modulus = cpp_int(1) << width;
  value %= modulus;
  if (value < 0)
    value += modulus;
  std::string bits(width, '0');
  for (uint32_t index = 0; index < width; ++index) {
    if (boost::multiprecision::bit_test(value, index))
      bits[width - 1 - index] = '1';
  }
  return BitVector(std::move(bits), is_signed);
}

char bitAnd(char a, char b)
{
  if (a == '0' || b == '0')
    return '0';
  return a == '1' && b == '1' ? '1' : 'x';
}

char bitOr(char a, char b)
{
  if (a == '1' || b == '1')
    return '1';
  return a == '0' && b == '0' ? '0' : 'x';
}

char bitXor(char a, char b)
{
  if (a == 'x' || a == 'z' || b == 'x' || b == 'z')
    return 'x';
  return a == b ? '0' : '1';
}

char bitNot(char bit)
{
  return bit == '0' ? '1' : bit == '1' ? '0' : 'x';
}
}  // namespace

BitVector::BitVector(std::string bits, bool is_signed, bool unsized) : _bits(std::move(bits)), _signed(is_signed), _unsized(unsized)
{
  checkWidth(_bits.size());
  if (_bits.find_first_not_of("01xz") != std::string::npos)
    throw std::invalid_argument("invalid four-state bit");
}

BitVector BitVector::parse(std::string_view literal)
{
  literal = trim(literal);
  const size_t quote = literal.find('\'');
  uint32_t width = 0;
  bool is_signed = quote == std::string_view::npos;
  bool unsized = true;
  char base = 'd';
  std::string value_digits;
  if (quote == std::string_view::npos) {
    value_digits = digits(literal);
  } else {
    auto size_text = trim(literal.substr(0, quote));
    if (!size_text.empty()) {
      if (size_text.front() < '1' || size_text.front() > '9')
        throw std::invalid_argument("literal size must be nonzero");
      const auto clean_size = digits(size_text);
      uint64_t size = 0;
      for (char ch : clean_size) {
        if (ch < '0' || ch > '9')
          throw std::invalid_argument("invalid literal size");
        size = size * 10 + static_cast<unsigned>(ch - '0');
        checkWidth(size);
      }
      width = static_cast<uint32_t>(size);
      unsized = false;
    }
    auto suffix = literal.substr(quote + 1);
    if (!suffix.empty() && (suffix.front() == 's' || suffix.front() == 'S')) {
      is_signed = true;
      suffix.remove_prefix(1);
    }
    if (suffix.empty())
      throw std::invalid_argument("missing literal base");
    base = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix.front())));
    suffix.remove_prefix(1);
    if (base != 'b' && base != 'o' && base != 'd' && base != 'h')
      throw std::invalid_argument("invalid Verilog-2005 literal base");
    value_digits = digits(trim(suffix));
  }

  std::string bits;
  if (base == 'd') {
    if (value_digits.size() == 1 && (value_digits[0] == 'x' || value_digits[0] == 'z' || value_digits[0] == '?')
        && quote != std::string_view::npos) {
      bits.assign(width ? width : 32, value_digits[0] == '?' ? 'z' : value_digits[0]);
    } else {
      cpp_int value = 0;
      for (char ch : value_digits) {
        if (ch < '0' || ch > '9')
          throw std::invalid_argument("invalid decimal digit");
        value = value * 10 + static_cast<unsigned>(ch - '0');
        if (value != 0 && boost::multiprecision::msb(value) >= kMaxWidth)
          throw std::invalid_argument("integer exceeds width limit");
      }
      const uint32_t significant = value == 0 ? 1 : boost::multiprecision::msb(value) + 1;
      if (!width)
        width = std::max(32U, significant + (is_signed && quote == std::string_view::npos ? 1U : 0U));
      bits = std::string(fromValue(value, width, is_signed).bits());
    }
  } else {
    const unsigned group = base == 'b' ? 1 : base == 'o' ? 3 : 4;
    if (value_digits.size() > kMaxWidth / group)
      throw std::invalid_argument("integer exceeds width limit");
    for (char ch : value_digits) {
      if (ch == 'x' || ch == 'z' || ch == '?') {
        bits.append(group, ch == '?' ? 'z' : ch);
      } else {
        const unsigned value = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : 16;
        if (value >= (1U << group))
          throw std::invalid_argument("digit outside literal base");
        for (unsigned shift = group; shift > 0; --shift)
          bits += (value & (1U << (shift - 1))) ? '1' : '0';
      }
    }
  }
  if (!width)
    width = std::max(32U, static_cast<uint32_t>(bits.size()));
  checkWidth(width);
  const char padding = bits.front() == 'x' || bits.front() == 'z' ? bits.front() : '0';
  if (bits.size() < width)
    bits.insert(bits.begin(), width - bits.size(), padding);
  if (bits.size() > width)
    bits.erase(0, bits.size() - width);
  return BitVector(std::move(bits), is_signed, unsized);
}

BitVector BitVector::fromInteger(int64_t value, uint32_t width, bool is_signed)
{
  return fromValue(value, width, is_signed);
}

bool BitVector::known() const
{
  return _bits.find_first_of("xz") == std::string::npos;
}

std::optional<int64_t> BitVector::integer() const
{
  if (!known())
    return std::nullopt;
  const cpp_int value = signedValue(*this);
  if (value < std::numeric_limits<int64_t>::min() || value > std::numeric_limits<int64_t>::max())
    return std::nullopt;
  return value.convert_to<int64_t>();
}

BitVector BitVector::resized(uint32_t width) const
{
  return resized(width, _signed);
}

BitVector BitVector::resized(uint32_t width, bool sign_extend) const
{
  checkWidth(width);
  auto bits = _bits;
  if (width < bits.size())
    bits.erase(0, bits.size() - width);
  if (width > bits.size())
    bits.insert(bits.begin(), width - bits.size(),
                sign_extend || (_unsized && (bits.front() == 'x' || bits.front() == 'z')) ? bits.front() : '0');
  return BitVector(std::move(bits), _signed, _unsized);
}

char BitVector::truth() const
{
  if (_bits.find('1') != std::string::npos)
    return '1';
  return known() ? '0' : 'x';
}

BitVector BitVector::unary(std::string_view op) const
{
  if (op == "+")
    return *this;
  if (op == "!")
    return BitVector(std::string(1, bitNot(truth())));
  if (op == "-")
    return known() ? fromValue(-signedValue(*this), width(), _signed) : BitVector(std::string(width(), 'x'), _signed);
  if (op == "~") {
    auto bits = _bits;
    for (auto& bit : bits)
      bit = bitNot(bit);
    return BitVector(std::move(bits), _signed);
  }
  if (op.empty())
    throw std::invalid_argument("empty unary operator");
  const bool inverted = op == "~&" || op == "~|" || op == "~^" || op == "^~";
  const char operation = op == "^~" ? '^' : op.back();
  if (operation != '&' && operation != '|' && operation != '^')
    throw std::invalid_argument("unsupported unary operator");
  char value = operation == '&' ? '1' : '0';
  for (const char bit : _bits)
    value = operation == '&' ? bitAnd(value, bit) : operation == '|' ? bitOr(value, bit) : bitXor(value, bit);
  return BitVector(std::string(1, inverted ? bitNot(value) : value));
}

BitVector BitVector::binary(std::string_view op, const BitVector& rhs) const
{
  if (op == "&&" || op == "||")
    return BitVector(std::string(1, op == "&&" ? bitAnd(truth(), rhs.truth()) : bitOr(truth(), rhs.truth())));
  const bool shift = op == "<<" || op == ">>" || op == "<<<" || op == ">>>";
  const auto size = shift || op == "**" ? width() : std::max(width(), rhs.width());
  const bool sign = shift || op == "**" ? _signed : _signed && rhs._signed;
  BitVector left = resized(size, sign);
  BitVector right = shift || op == "**" ? rhs : rhs.resized(size, sign);
  left._signed = sign;
  if (!shift && op != "**")
    right._signed = sign;
  if (op == "===" || op == "!==")
    return BitVector(std::string(1, (left._bits == right._bits) == (op == "===") ? '1' : '0'));
  if (op == "&" || op == "|" || op == "^" || op == "~^" || op == "^~") {
    std::string bits(size, 'x');
    for (uint32_t i = 0; i < size; ++i) {
      bits[i] = op == "&"   ? bitAnd(left._bits[i], right._bits[i])
                : op == "|" ? bitOr(left._bits[i], right._bits[i])
                            : bitXor(left._bits[i], right._bits[i]);
      if (op == "~^" || op == "^~")
        bits[i] = bitNot(bits[i]);
    }
    return BitVector(std::move(bits), sign);
  }
  if (shift) {
    if (!rhs.known())
      return BitVector(std::string(size, 'x'), sign);
    const cpp_int distance = unsignedValue(rhs.bits());
    const char fill = op == ">>>" && sign ? left._bits.front() : '0';
    if (distance >= size)
      return BitVector(std::string(size, fill), sign);
    const auto count = distance.convert_to<unsigned>();
    if (op == "<<" || op == "<<<")
      return BitVector(left._bits.substr(count) + std::string(count, '0'), sign);
    return BitVector(std::string(count, fill) + left._bits.substr(0, size - count), sign);
  }
  if (op == "==" || op == "!=") {
    bool unknown = false;
    for (uint32_t i = 0; i < size; ++i) {
      const char a = left._bits[i], b = right._bits[i];
      if ((a == '0' || a == '1') && (b == '0' || b == '1')) {
        if (a != b)
          return BitVector(op == "==" ? "0" : "1");
      } else
        unknown = true;
    }
    return BitVector(unknown ? "x" : op == "==" ? "1" : "0");
  }
  const bool compare = op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
  if (!left.known() || !right.known())
    return BitVector(std::string(compare ? 1 : size, 'x'), compare ? false : sign);
  const cpp_int a = signedValue(left);
  const cpp_int b = signedValue(right);
  if (compare) {
    const bool result = op == "==" ? a == b : op == "!=" ? a != b : op == "<" ? a < b : op == "<=" ? a <= b : op == ">" ? a > b : a >= b;
    return BitVector(result ? "1" : "0");
  }
  cpp_int result;
  if (op == "+")
    result = a + b;
  else if (op == "-")
    result = a - b;
  else if (op == "*")
    result = a * b;
  else if (op == "/" || op == "%") {
    if (b == 0)
      return BitVector(std::string(size, 'x'), sign);
    if (op == "/")
      result = a / b;
    else
      result = a % b;
  } else if (op == "**") {
    if (b < 0) {
      if (a == 0)
        return BitVector(std::string(size, 'x'), sign);
      result = a == 1 ? 1 : a == -1 ? (b % 2 == 0 ? 1 : -1) : 0;
    } else {
      cpp_int exponent = b;
      cpp_int factor = a;
      const cpp_int modulus = cpp_int(1) << size;
      result = 1;
      while (exponent > 0) {
        if ((exponent & 1) != 0)
          result = (result * factor) % modulus;
        factor = (factor * factor) % modulus;
        exponent >>= 1;
      }
    }
  } else
    throw std::invalid_argument("unsupported binary operator");
  return fromValue(std::move(result), size, sign);
}
}  // namespace idb::verilog
