// iEDA is licensed under Mulan PSL v2. See LICENSE for details.
#include "VerilogConstEval.hh"

#include <algorithm>

namespace idb::verilog::detail {
int64_t integer(const BitVector& value, SourceLocation location)
{
  const auto result = value.integer();
  if (!result)
    throw Error(location, "expected known constant integer within 64-bit index range");
  return *result;
}
size_t rangeWidth(int64_t left, int64_t right, SourceLocation location)
{
  if (left < INT32_MIN || left > INT32_MAX || right < INT32_MIN || right > INT32_MAX)
    throw Error(location, "net index exceeds signed 32-bit implementation limit");
  const auto width = static_cast<uint64_t>(left >= right ? left - right : right - left) + 1;
  if (width > BitVector::kMaxWidth)
    throw Error(location, "net width exceeds frontend limit");
  return static_cast<size_t>(width);
}
std::optional<BitVector> ConstEvaluator::natural(ExprId id, const Constants& values, ValueCache& cache)
{
  if (const auto found = cache.find(id); found != cache.end())
    return found->second;
  auto value = naturalImpl(id, values, cache);
  cache.emplace(id, value);
  return value;
}
std::optional<BitVector> ConstEvaluator::naturalImpl(ExprId id, const Constants& values, ValueCache& cache)
{
  const auto& e = expr(id);
  if (e.kind == ExpressionKind::reference) {
    const auto found = values.find(e.text);
    if (found == values.end())
      return std::nullopt;
    return found->second.value;
  }
  if (e.kind == ExpressionKind::number)
    return BitVector::parse(e.text);
  std::vector<BitVector> operands;
  for (auto operand : e.operands) {
    auto value = natural(operand, values, cache);
    if (!value)
      return std::nullopt;
    operands.push_back(std::move(*value));
  }
  try {
    if (e.kind == ExpressionKind::unary)
      return evaluated(e.operands[0], values, cache).unary(e.text);
    if (e.kind == ExpressionKind::binary) {
      const bool logical = e.text == "&&" || e.text == "||";
      const bool self_right = e.text == "<<" || e.text == ">>" || e.text == "<<<" || e.text == ">>>" || e.text == "**";
      if (logical)
        return evaluated(e.operands[0], values, cache).binary(e.text, evaluated(e.operands[1], values, cache));
      const auto width = self_right ? operands[0].width() : std::max(operands[0].width(), operands[1].width());
      const bool sign = self_right ? operands[0].isSigned() : operands[0].isSigned() && operands[1].isSigned();
      return evaluated(e.operands[0], values, cache, width, sign)
          .binary(e.text, self_right ? evaluated(e.operands[1], values, cache) : evaluated(e.operands[1], values, cache, width, sign));
    }
    if (e.kind == ExpressionKind::concatenate) {
      for (size_t i = 0; i < operands.size(); ++i)
        operands[i] = evaluated(e.operands[i], values, cache);
      std::string bits;
      for (const auto& operand : operands) {
        if (operand.isUnsized())
          throw Error(e.location, "unsized constant in concatenation");
        if (bits.size() + operand.width() > BitVector::kMaxWidth)
          throw Error(e.location, "concatenation width exceeds frontend limit");
        bits += operand.bits();
      }
      return BitVector(std::move(bits));
    }
    if (e.kind == ExpressionKind::repeat) {
      operands[0] = evaluated(e.operands[0], values, cache);
      operands[1] = evaluated(e.operands[1], values, cache);
      const auto count = integer(operands[0], e.location);
      if (count <= 0 || count > BitVector::kMaxWidth / operands[1].width())
        throw Error(e.location, "unsupported zero or excessive repetition count");
      std::string bits;
      bits.reserve(count * operands[1].width());
      for (int64_t i = 0; i < count; ++i)
        bits += operands[1].bits();
      return BitVector(std::move(bits));
    }
    if (e.kind == ExpressionKind::conditional) {
      const auto width = std::max(operands[1].width(), operands[2].width());
      const bool sign = operands[1].isSigned() && operands[2].isSigned();
      const auto yes = operands[1].resized(width, sign);
      const auto no = operands[2].resized(width, sign);
      std::string bits;
      const auto condition = operands[0].truth();
      for (size_t i = 0; i < width; ++i)
        bits += condition == '1' ? yes.bits()[i] : condition == '0' ? no.bits()[i] : yes.bits()[i] == no.bits()[i] ? yes.bits()[i] : 'x';
      return BitVector(std::move(bits), sign);
    }
    const auto& base = expr(e.operands[0]);
    const auto found = values.find(base.text);
    auto bounds = found != values.end() ? found->second.range : std::nullopt;
    if (!bounds)
      bounds = std::pair<int32_t, int32_t>{static_cast<int32_t>(operands[0].width() - 1), 0};
    const auto indices = selectIndices(e, values, *bounds);
    std::string bits;
    for (const auto index : indices) {
      const auto offset = bounds->first >= bounds->second ? int64_t(bounds->first) - index : index - bounds->first;
      bits += offset >= 0 && offset < operands[0].width() ? operands[0].bits()[offset] : 'x';
    }
    return BitVector(std::move(bits));
  } catch (const Error&) {
    throw;
  } catch (const std::exception& error) {
    throw Error(e.location, error.what());
  }
}
BitVector ConstEvaluator::evaluated(ExprId id, const Constants& values, ValueCache& cache, uint32_t width, std::optional<bool> sign)
{
  const auto& e = expr(id);
  const auto base = *natural(id, values, cache);
  width = std::max(width, base.width());
  const bool type = sign.value_or(base.isSigned());
  auto convert = [&](BitVector value) { return BitVector(std::string(value.resized(width, type).bits()), type, value.isUnsized()); };
  if (e.kind == ExpressionKind::unary && (e.text == "+" || e.text == "-" || e.text == "~"))
    return evaluated(e.operands[0], values, cache, width, type).unary(e.text);
  if (e.kind == ExpressionKind::binary) {
    const bool logical = e.text == "&&" || e.text == "||";
    const bool compare = e.text == "==" || e.text == "!=" || e.text == "===" || e.text == "!==" || e.text == "<" || e.text == "<="
                         || e.text == ">" || e.text == ">=";
    const bool self_right = e.text == "<<" || e.text == ">>" || e.text == "<<<" || e.text == ">>>" || e.text == "**";
    if (logical)
      return convert(evaluated(e.operands[0], values, cache).binary(e.text, evaluated(e.operands[1], values, cache)));
    if (compare) {
      const auto left = *natural(e.operands[0], values, cache), right = *natural(e.operands[1], values, cache);
      const auto size = std::max(left.width(), right.width());
      const bool operand_type = left.isSigned() && right.isSigned();
      return convert(evaluated(e.operands[0], values, cache, size, operand_type)
                         .binary(e.text, evaluated(e.operands[1], values, cache, size, operand_type)));
    }
    return evaluated(e.operands[0], values, cache, width, type)
        .binary(e.text, self_right ? evaluated(e.operands[1], values, cache) : evaluated(e.operands[1], values, cache, width, type));
  }
  if (e.kind == ExpressionKind::conditional) {
    const auto condition = evaluated(e.operands[0], values, cache).truth();
    const auto yes = evaluated(e.operands[1], values, cache, width, type), no = evaluated(e.operands[2], values, cache, width, type);
    std::string bits;
    for (size_t i = 0; i < width; ++i)
      bits += condition == '1' ? yes.bits()[i] : condition == '0' ? no.bits()[i] : yes.bits()[i] == no.bits()[i] ? yes.bits()[i] : 'x';
    return BitVector(std::move(bits), type);
  }
  if (e.kind == ExpressionKind::concatenate || e.kind == ExpressionKind::repeat) {
    std::string bits;
    const auto& parts = e.kind == ExpressionKind::repeat ? expr(e.operands[1]).operands : e.operands;
    for (auto part : parts)
      bits += evaluated(part, values, cache).bits();
    if (e.kind == ExpressionKind::repeat) {
      const auto count = integer(evaluated(e.operands[0], values, cache), e.location);
      const auto group = bits;
      for (int64_t i = 1; i < count; ++i)
        bits += group;
    }
    return convert(BitVector(std::move(bits)));
  }
  // Reductions and selections are self-determined. Evaluate their operands before the operation.
  if (e.kind == ExpressionKind::unary)
    return convert(evaluated(e.operands[0], values, cache).unary(e.text));
  return convert(base);
}
std::optional<BitVector> ConstEvaluator::constant(ExprId id, const Constants& values, uint32_t width)
{
  ValueCache cache;
  if (!natural(id, values, cache))
    return std::nullopt;
  return evaluated(id, values, cache, width);
}
BitVector ConstEvaluator::requiredConstant(ExprId id, const Constants& values, uint32_t width)
{
  auto value = constant(id, values, width);
  if (!value)
    throw Error(expr(id).location, "expected constant expression; unknown parameter or nonconstant expression");
  return *value;
}
std::optional<std::pair<int32_t, int32_t>> ConstEvaluator::bounds(Range range, const Constants& values)
{
  if (!range.present())
    return std::nullopt;
  const auto left = integer(requiredConstant(range.left, values), expr(range.left).location);
  const auto right = integer(requiredConstant(range.right, values), expr(range.right).location);
  rangeWidth(left, right, expr(range.left).location);
  return std::pair<int32_t, int32_t>{static_cast<int32_t>(left), static_cast<int32_t>(right)};
}
std::vector<int64_t> ConstEvaluator::selectIndices(const Expression& e, const Constants& values, std::pair<int32_t, int32_t> range)
{
  int64_t left = integer(requiredConstant(e.operands[1], values), e.location);
  int64_t right = left;
  if (e.kind != ExpressionKind::select) {
    right = integer(requiredConstant(e.operands[2], values), e.location);
    if (e.kind == ExpressionKind::indexedUp || e.kind == ExpressionKind::indexedDown) {
      if (right <= 0 || right > BitVector::kMaxWidth)
        throw Error(e.location, "indexed part-select width must be positive and bounded");
      if (left < INT32_MIN || left > INT32_MAX)
        throw Error(e.location, "part-select base index exceeds implementation limit");
      const auto low = e.kind == ExpressionKind::indexedUp ? left : left - right + 1;
      const auto high = low + right - 1;
      left = range.first >= range.second ? high : low;
      right = range.first >= range.second ? low : high;
    } else if ((left < right && range.first > range.second) || (left > right && range.first < range.second))
      throw Error(e.location, "part-select direction differs from declaration");
  }
  const auto width = rangeWidth(left, right, e.location);
  std::vector<int64_t> indices;
  indices.reserve(width);
  for (size_t i = 0; i < width; ++i)
    indices.push_back(left + (left <= right ? int64_t(i) : -int64_t(i)));
  return indices;
}
}  // namespace idb::verilog::detail
