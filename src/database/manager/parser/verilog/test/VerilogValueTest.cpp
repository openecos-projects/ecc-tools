// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include <iostream>
#include <stdexcept>
#include <string>

#include "VerilogValue.hh"

using idb::verilog::BitVector;

namespace {
void require(bool condition, const std::string& message)
{
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void numbers()
{
  require(BitVector::parse("16'hx").bits() == std::string(16, 'x'), "unknown padding lost");
  require(BitVector::parse("12'hz3").bits() == "zzzzzzzz0011", "high impedance padding lost");
  require(BitVector::parse("8 'h A_f").bits() == "10101111", "based literal whitespace/underscore");
  require(BitVector::parse("4'b101001").bits() == "1001", "literal truncation must discard high bits");
  require(BitVector::parse("4'shf").resized(8).bits() == "11111111", "signed extension");
  require(BitVector::parse("4'hf").resized(8).bits() == "00001111", "unsigned extension");
  require(BitVector::parse("'h1").width() >= 32 && !BitVector::parse("'h1").isSigned(), "unsized based width/type");
  require(BitVector::parse("1_5").integer() == 15 && BitVector::parse("1_5").isSigned(), "unsized decimal semantics");
  require(BitVector::parse("4294967295").integer() == 4294967295LL, "unsized decimal was narrowed to int32");
  require(BitVector::parse("128'd340282366920938463463374607431768211455").bits() == std::string(128, '1'),
          "wide decimal overflowed native integer");
  require(BitVector::parse("128'h0123456789abcdef0123456789abcdef").resized(4).bits() == "1111", "wide truncation");
  require(BitVector::parse("8'd?").bits() == std::string(8, 'z'), "decimal high impedance");
  require(!BitVector::parse("4'bx001").integer(), "unknown integer must not become zero");
}

void operations()
{
  require(BitVector::parse("4'sd15").unary("-").bits() == "0001", "unary minus is two's complement at operand width");
  require(BitVector::parse("4'b10xz").unary("~").bits() == "01xx", "four-state inversion");
  require(BitVector::parse("4'b000x").unary("!").bits() == "x", "unknown boolean");
  require(BitVector::parse("4'b001x").unary("!").bits() == "0", "known one dominates boolean conversion");
  require(BitVector::parse("4'b0000").binary("&", BitVector::parse("4'bxxxx")).bits() == "0000", "zero dominates and");
  require(BitVector::parse("4'b1111").binary("|", BitVector::parse("4'bzzzz")).bits() == "1111", "one dominates or");
  require(BitVector::parse("4'shf").binary("+", BitVector::parse("8'sd1")).bits() == "00000000", "signed arithmetic width");
  require(BitVector::parse("4'shf").binary("+", BitVector::parse("8'd1")).bits() == "00010000", "mixed signed/unsigned arithmetic");
  require(BitVector::parse("4'sh8").binary(">>>", BitVector::parse("2")).bits() == "1110", "arithmetic right shift");
  require(BitVector::parse("4'h8").binary(">>>", BitVector::parse("2")).bits() == "0010", "unsigned arithmetic shift");
  require(BitVector::parse("8'd7").binary("/", BitVector::parse("8'd0")).bits() == std::string(8, 'x'), "division by zero");
  require(BitVector::parse("4'b10xz").binary("===", BitVector::parse("4'b10xz")).bits() == "1", "case equality");
}

void fourStateOperators()
{
  require(BitVector::parse("4'bx001").binary("<<", BitVector::parse("1")).bits() == "0010",
          "shift must move x/z bits without poisoning known bits");
  require(BitVector::parse("4'bz001").binary(">>", BitVector::parse("1")).bits() == "0z00", "logical shift must preserve z");
  require(BitVector::parse("4'sd2").binary("**", BitVector::parse("2'd3")).bits() == "1000",
          "power exponent must retain its own signedness");
  require(BitVector::parse("2'b0x").binary("==", BitVector::parse("2'b1x")).bits() == "0", "definite mismatch must make equality false");
}

void rejected()
{
  for (const auto* text : {"0'b0", "4'b2", "8'd1x", "4'b_1", "4' h1", "8'h", "_15", "4'b1__2", "4.0", "'0"}) {
    bool failed = false;
    try {
      (void) BitVector::parse(text);
    } catch (const std::exception&) {
      failed = true;
    }
    require(failed, std::string("accepted invalid Verilog-2005 integer: ") + text);
  }
}
}  // namespace

int main()
{
  try {
    numbers();
    operations();
    fourStateOperators();
    rejected();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
