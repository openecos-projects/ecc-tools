// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#pragma once

#include <optional>
#include <string_view>
#include <unordered_map>

#include "VerilogAst.hh"
#include "VerilogValue.hh"

namespace idb::verilog::detail {
struct Constant
{
  BitVector value;
  std::optional<std::pair<int32_t, int32_t>> range;
};
using Constants = std::unordered_map<std::string_view, Constant>;
int64_t integer(const BitVector& value, SourceLocation location);
size_t rangeWidth(int64_t left, int64_t right, SourceLocation location);
// Evaluates against one immutable AST. Parameter names borrow from that AST;
// every returned value owns its bits. Caches are local to an evaluation context.
class ConstEvaluator
{
 public:
  explicit ConstEvaluator(const Design& ast) : _ast(ast) {}
  std::optional<BitVector> constant(ExprId id, const Constants& values, uint32_t width = 0);
  BitVector requiredConstant(ExprId id, const Constants& values, uint32_t width = 0);
  std::optional<std::pair<int32_t, int32_t>> bounds(Range range, const Constants& values);
  std::vector<int64_t> selectIndices(const Expression& e, const Constants& values, std::pair<int32_t, int32_t> range);

 private:
  const Expression& expr(ExprId id) const { return _ast.expressions.at(id); }
  using ValueCache = std::unordered_map<ExprId, std::optional<BitVector>>;
  std::optional<BitVector> natural(ExprId id, const Constants& values, ValueCache& cache);
  std::optional<BitVector> naturalImpl(ExprId id, const Constants& values, ValueCache& cache);
  BitVector evaluated(ExprId id, const Constants& values, ValueCache& cache, uint32_t width = 0, std::optional<bool> sign = {});
  const Design& _ast;
};

}  // namespace idb::verilog::detail
