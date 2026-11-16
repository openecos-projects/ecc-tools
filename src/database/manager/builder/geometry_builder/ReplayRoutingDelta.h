#pragma once
#include "json.hpp"

namespace ecc::replay {
// A single producer's complete routing observations become transport patches.
// The first observation replaces routing; non-routing frames break the chain.
// Consumers already support explicit patch/remove, including shape multiplicity.
class RoutingDelta
{
 public:
  void reset() { _previous = nullptr; }
  void compact(nlohmann::json& frame)
  {
    if (frame.at("scope") != "routing") {
      reset();
      return;
    }
    auto& current = frame.at("objects");
    if (_previous.is_null()) {
      _previous = current;
      return;
    }
    auto changed = nlohmann::json::object();
    auto removed = nlohmann::json::array();
    for (auto it = current.begin(); it != current.end(); ++it) {
      const auto old = _previous.find(it.key());
      if (old == _previous.end() || *old != it.value())
        changed[it.key()] = it.value();
    }
    for (auto it = _previous.begin(); it != _previous.end(); ++it)
      if (!current.contains(it.key()))
        removed.push_back(it.key());
    _previous.swap(current);
    current = std::move(changed);
    frame["remove"] = std::move(removed);
    frame["scope"] = "patch";
  }

 private:
  nlohmann::json _previous;
};
}  // namespace ecc::replay
