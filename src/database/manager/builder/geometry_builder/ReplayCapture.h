#pragma once

#include <exception>
#include <string>

#include "json.hpp"

namespace idb {
class IdbDesign;
class IdbLayout;
class IdbInstance;
}  // namespace idb

namespace ecc::replay {
bool enabled();
void failure(const char* message) noexcept;
template <typename Capture>
void guarded(Capture&& capture) noexcept
{
  if (!enabled())
    return;
  try {
    capture();
  } catch (const std::exception& error) {
    failure(error.what());
  } catch (...) {
    failure("unknown capture failure");
  }
}
nlohmann::json instance(idb::IdbInstance& instance);
nlohmann::json geometry(idb::IdbDesign& design, idb::IdbLayout& layout);
// Invoke only at stable algorithm boundaries. The payload owns its values.
void sample(const std::string& phase, const std::string& subflow, nlohmann::json objects, const std::string& scope = "full",
            const std::string& domain = "algorithm_state", nlohmann::json metrics = nlohmann::json::object()) noexcept;
void database(idb::IdbDesign& design, idb::IdbLayout& layout, const std::string& phase, const std::string& subflow) noexcept;
// Analysis progress has no physical mutation and must not replace the scene.
inline void analysisPhase(const char* phase, const char* subflow) noexcept
{
  guarded([&] { ecc::replay::sample(phase, subflow, nlohmann::json::object(), "patch", "analysis_state"); });
}
}  // namespace ecc::replay
