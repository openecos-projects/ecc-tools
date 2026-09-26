#include "design/routing/view/WireRoutingView.h"

#include <stdexcept>

namespace eccdb {

DesignWirePoint DesignWirePointRange::operator[](std::size_t index) const
{
  if (index >= size()) {
    throw std::out_of_range("wire point index is out of range");
  }
  return _values[index];
}

DesignWireVia DesignWireViaRange::operator[](std::size_t index) const
{
  if (index >= size()) {
    throw std::out_of_range("wire VIA index is out of range");
  }
  return _values[index];
}

}  // namespace eccdb
