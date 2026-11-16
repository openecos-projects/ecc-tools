#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "ReplayCapture.h"
#include "ReplayRoutingDelta.h"

int main()
{
  // The payload represents expensive geometry traversal/allocation. Off must
  // never enter it, even if accidentally wired at a hot algorithm boundary.
  using Json = nlohmann::json;
  ecc::replay::RoutingDelta delta;
  const Json first = {{"wire:a", {{"bbox", {0, 0, 10, 1}}}}, {"via:a", {{"master", "VIA12"}}}};
  const Json changed = {{"wire:b", {{"bbox", {0, 0, 20, 1}}}}, {"via:a", {{"master", "VIA12"}}}};
  Json restored;
  for (const auto& objects : {first, changed, first}) {
    Json frame = {{"scope", "routing"}, {"objects", objects}};
    delta.compact(frame);
    if (frame["scope"] == "routing")
      restored = frame["objects"];
    else {
      for (const auto& key : frame["remove"])
        restored.erase(key.get<std::string>());
      restored.update(frame["objects"]);
    }
    assert(restored == objects);
  }
  delta.reset();
  Json new_attempt = {{"scope", "routing"}, {"objects", changed}};
  delta.compact(new_attempt);
  assert(new_attempt == Json({{"scope", "routing"}, {"objects", changed}}));
  unsetenv("ECC_REPLAY_CAPTURE_DIR");
  size_t calls = 0;
  for (int i = 0; i < 10000; ++i)
    ecc::replay::guarded([&] {
      ++calls;
      throw std::runtime_error("must not run");
    });
  assert(calls == 0);
  setenv("ECC_REPLAY_CAPTURE_DIR", "", 1);
  ecc::replay::guarded([&] { ++calls; });
  assert(calls == 0);
  auto directory = std::filesystem::temp_directory_path() / "ecc-replay-gate-XXXXXX";
  auto name = directory.string();
  assert(mkdtemp(name.data()));
  setenv("ECC_REPLAY_CAPTURE_DIR", name.c_str(), 1);
  ecc::replay::guarded([&] {
    ++calls;
    ecc::replay::sample("route_test", "run routing", nlohmann::json::object(), "patch");
  });
  assert(calls == 1);
  size_t samples = 0;
  for (const auto& file : std::filesystem::directory_iterator(name)) {
    std::ifstream stream(file.path());
    const auto frame = nlohmann::json::parse(stream);
    assert(frame.at("phase") == "route_test");
    ++samples;
  }
  assert(samples == 1);
  unsetenv("ECC_REPLAY_CAPTURE_DIR");
  std::filesystem::remove_all(name);
}
