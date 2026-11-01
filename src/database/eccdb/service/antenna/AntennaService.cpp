// SPDX-License-Identifier: MulanPSL-2.0
// Copyright (c) 2023-2025 Peng Cheng Laboratory; Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences; Copyright (c) 2023-2025 Beijing Institute of Open Source Chip

#include "antenna/AntennaService.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "design/DesignStore.h"
#include "library/LibraryStore.h"
#include "tech/TechStore.h"

namespace eccdb {

namespace {

[[nodiscard]] bool sameAntennaValueKey(const AntennaValue& lhs,
                                              const AntennaValue& rhs) noexcept
{
  return lhs.metric == rhs.metric;
}

[[nodiscard]] bool sameAntennaLayerValueKey(const AntennaLayerValue& lhs,
                                                   const AntennaLayerValue& rhs) noexcept
{
  return lhs.metric == rhs.metric && lhs.layer == rhs.layer;
}

[[nodiscard]] bool sameAntennaPwlKey(const AntennaPwl& lhs,
                                            const AntennaPwl& rhs) noexcept
{
  return lhs.metric == rhs.metric;
}

void refreshAntennaModelFlags(AntennaModel& model) noexcept
{
  constexpr uint32_t known_flags = AntennaModelFlag::kHasValues | AntennaModelFlag::kHasLayerValues |
                                    AntennaModelFlag::kHasPwls | AntennaModelFlag::kHasBooleanValues;
  model.flags &= ~known_flags;
  if (!model.values.empty()) {
    model.flags |= AntennaModelFlag::kHasValues;
    if (std::any_of(model.values.begin(), model.values.end(), [](const AntennaValue& value) {
          return (value.flags & AntennaValueFlag::kBoolean) != 0;
        })) {
      model.flags |= AntennaModelFlag::kHasBooleanValues;
    }
  }
  if (!model.layer_values.empty()) model.flags |= AntennaModelFlag::kHasLayerValues;
  if (!model.pwls.empty()) model.flags |= AntennaModelFlag::kHasPwls;
}

void normalizeAntennaModelEntries(AntennaModel& model)
{
  auto normalize = [](auto& entries, auto same_key) {
    for (std::size_t index = 0; index < entries.size(); ++index) {
      for (std::size_t previous = 0; previous < index; ++previous) {
        if (!same_key(entries[previous], entries[index])) continue;
        entries[previous] = std::move(entries[index]);
        entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
        --index;
        break;
      }
    }
  };
  normalize(model.values, sameAntennaValueKey);
  normalize(model.layer_values, sameAntennaLayerValueKey);
  normalize(model.pwls, sameAntennaPwlKey);
  refreshAntennaModelFlags(model);
}

void normalizeAntennaValues(std::vector<AntennaValue>& values)
{
  for (std::size_t index = 0; index < values.size(); ++index) {
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (values[previous].metric != values[index].metric) continue;
      values[previous] = std::move(values[index]);
      values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
      --index;
      break;
    }
  }
}

void normalizeAntennaLayerValues(std::vector<AntennaLayerValue>& values)
{
  for (std::size_t index = 0; index < values.size(); ++index) {
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (!sameAntennaLayerValueKey(values[previous], values[index])) continue;
      values[previous] = std::move(values[index]);
      values.erase(values.begin() + static_cast<std::ptrdiff_t>(index));
      --index;
      break;
    }
  }
}

void mergeAntennaModel(AntennaModel& target, AntennaModel source)
{
  constexpr uint32_t known_flags = AntennaModelFlag::kHasValues | AntennaModelFlag::kHasLayerValues
                                   | AntennaModelFlag::kHasPwls | AntennaModelFlag::kHasBooleanValues;
  target.flags |= source.flags & ~known_flags;
  for (auto& value : source.values) {
    const auto found = std::find_if(target.values.begin(), target.values.end(),
                                    [&value](const AntennaValue& current) {
                                      return sameAntennaValueKey(current, value);
                                    });
    if (found == target.values.end()) target.values.push_back(std::move(value));
    else *found = std::move(value);
  }
  for (auto& value : source.layer_values) {
    const auto found = std::find_if(target.layer_values.begin(), target.layer_values.end(),
                                    [&value](const AntennaLayerValue& current) {
                                      return sameAntennaLayerValueKey(current, value);
                                    });
    if (found == target.layer_values.end()) target.layer_values.push_back(std::move(value));
    else *found = std::move(value);
  }
  for (auto& pwl : source.pwls) {
    const auto found = std::find_if(target.pwls.begin(), target.pwls.end(),
                                    [&pwl](const AntennaPwl& current) {
                                      return sameAntennaPwlKey(current, pwl);
                                    });
    if (found == target.pwls.end()) target.pwls.push_back(std::move(pwl));
    else *found = std::move(pwl);
  }
  normalizeAntennaModelEntries(target);
}

[[nodiscard]] bool sameAntennaOxide(std::string_view lhs, std::string_view rhs) noexcept
{
  const auto logical = [](std::string_view oxide) constexpr -> std::string_view {
    return oxide.empty() ? std::string_view{"OXIDE1"} : oxide;
  };
  return logical(lhs) == logical(rhs);
}

[[nodiscard]] AntennaModel* findAntennaModel(std::vector<AntennaModel>& models,
                                                     std::string_view oxide) noexcept
{
  const auto found = std::find_if(models.begin(), models.end(), [oxide](const AntennaModel& model) {
    return sameAntennaOxide(model.oxide, oxide);
  });
  return found == models.end() ? nullptr : &*found;
}

[[nodiscard]] const AntennaModel* findAntennaModel(const std::vector<AntennaModel>& models,
                                                          std::string_view oxide) noexcept
{
  const auto found = std::find_if(models.begin(), models.end(), [oxide](const AntennaModel& model) {
    return sameAntennaOxide(model.oxide, oxide);
  });
  return found == models.end() ? nullptr : &*found;
}

void normalizeAntennaModels(std::vector<AntennaModel>& models)
{
  for (auto& model : models) normalizeAntennaModelEntries(model);
  for (std::size_t index = 0; index < models.size(); ++index) {
    for (std::size_t previous = 0; previous < index; ++previous) {
      if (!sameAntennaOxide(models[previous].oxide, models[index].oxide)) continue;
      mergeAntennaModel(models[previous], std::move(models[index]));
      models.erase(models.begin() + static_cast<std::ptrdiff_t>(index));
      --index;
      break;
    }
  }
  for (auto& model : models) normalizeAntennaModelEntries(model);
}

AntennaModel& replaceAntennaModel(std::vector<AntennaModel>& models, AntennaModel model)
{
  normalizeAntennaModels(models);
  normalizeAntennaModelEntries(model);
  if (auto* existing = findAntennaModel(models, model.oxide); existing != nullptr) {
    *existing = std::move(model);
    return *existing;
  }
  models.push_back(std::move(model));
  return models.back();
}

bool eraseAntennaModel(std::vector<AntennaModel>& models, std::string_view oxide) noexcept
{
  const auto old_size = models.size();
  models.erase(std::remove_if(models.begin(), models.end(), [oxide](const AntennaModel& model) {
                 return sameAntennaOxide(model.oxide, oxide);
               }), models.end());
  return models.size() != old_size;
}

void syncMasterTermAntennaFlags(LibraryMasterTermAntenna& antenna) noexcept
{
  normalizeAntennaValues(antenna.values);
  normalizeAntennaLayerValues(antenna.layer_values);
  if (antenna.values.empty()) {
    antenna.flags &= ~LibraryMasterTermAntennaFlag::kHasValues;
  } else {
    antenna.flags |= LibraryMasterTermAntennaFlag::kHasValues;
  }
  if (antenna.layer_values.empty()) {
    antenna.flags &= ~LibraryMasterTermAntennaFlag::kHasLayerValues;
  } else {
    antenna.flags |= LibraryMasterTermAntennaFlag::kHasLayerValues;
  }
  if (antenna.models.empty()) {
    antenna.flags &= ~LibraryMasterTermAntennaFlag::kHasModels;
  } else {
    antenna.flags |= LibraryMasterTermAntennaFlag::kHasModels;
  }
}

void syncTechLayerAntennaFlags(TechLayerAntenna& antenna) noexcept
{
  if (antenna.models.empty()) {
    antenna.flags &= ~TechLayerAntennaFlag::kHasModels;
  } else {
    antenna.flags |= TechLayerAntennaFlag::kHasModels;
  }
}

void syncBlockTermAntennaFlags(DesignBlockTermAntenna& antenna) noexcept
{
  normalizeAntennaValues(antenna.values);
  normalizeAntennaLayerValues(antenna.layer_values);
  if (antenna.values.empty()) {
    antenna.flags &= ~DesignBlockTermAntennaFlag::kHasValues;
  } else {
    antenna.flags |= DesignBlockTermAntennaFlag::kHasValues;
  }
  if (antenna.layer_values.empty()) {
    antenna.flags &= ~DesignBlockTermAntennaFlag::kHasLayerValues;
  } else {
    antenna.flags |= DesignBlockTermAntennaFlag::kHasLayerValues;
  }
  if (antenna.models.empty()) {
    antenna.flags &= ~DesignBlockTermAntennaFlag::kHasModels;
  } else {
    antenna.flags |= DesignBlockTermAntennaFlag::kHasModels;
  }
}

}  // namespace

AntennaService::AntennaService(DatabaseContext& context) noexcept
    : _context(&context),
      _design(&context.design()),
      _const_design(&context.design()),
      _library(context.library()),
      _const_library(context.library()),
      _technology(context.technology()),
      _const_technology(context.technology())
{
}

AntennaService::AntennaService(DesignStore& design) noexcept : _design(&design), _const_design(&design) {}
AntennaService::AntennaService(const DesignStore& design) noexcept : _const_design(&design) {}
AntennaService::AntennaService(LibraryStore& library) noexcept : _library(&library), _const_library(&library) {}
AntennaService::AntennaService(const LibraryStore& library) noexcept : _const_library(&library) {}
AntennaService::AntennaService(TechStore& technology) noexcept
    : _technology(&technology), _const_technology(&technology)
{
}
AntennaService::AntennaService(const TechStore& technology) noexcept : _const_technology(&technology) {}

void AntennaService::notify() noexcept
{
  if (_context != nullptr) {
    _context->notifyAfter(DatabaseChange{.domains = DatabaseChangeDomain::kAntenna});
  }
}

const LibraryMasterTermAntenna* AntennaService::masterTermAntenna(LibraryMasterTermId term) const noexcept
{
  const auto* library = _const_library;
  if (library == nullptr || !library->masterTermStorage().contains(term)) return nullptr;
  const auto& registry = library->libraryRegistry().registry();
  return registry.try_get<LibraryMasterTermAntenna>(term.entity());
}

LibraryMasterTermAntenna& AntennaService::ensureMasterTermAntenna(LibraryMasterTermId term)
{
  auto* library = _library;
  if (library == nullptr || !library->masterTermStorage().contains(term)) {
    throw std::invalid_argument("antenna service requires a live library master term");
  }
  auto& registry = library->libraryRegistry().registry();
  return registry.get_or_emplace<LibraryMasterTermAntenna>(term.entity());
}

void AntennaService::setMasterTermAntenna(LibraryMasterTermId term, LibraryMasterTermAntenna value)
{
  normalizeAntennaModels(value.models);
  syncMasterTermAntennaFlags(value);
  auto& target = ensureMasterTermAntenna(term);
  target = std::move(value);
  notify();
}

void AntennaService::clearMasterTermAntenna(LibraryMasterTermId term) noexcept
{
  auto* library = _library;
  if (library == nullptr || !library->masterTermStorage().contains(term)) return;
  library->libraryRegistry().registry().remove<LibraryMasterTermAntenna>(term.entity());
  notify();
}

const AntennaModel* AntennaService::masterTermAntennaModel(LibraryMasterTermId term,
                                                            std::string_view oxide) const noexcept
{
  const auto* antenna = masterTermAntenna(term);
  return antenna == nullptr ? nullptr : findAntennaModel(antenna->models, oxide);
}

void AntennaService::setMasterTermAntennaModel(LibraryMasterTermId term, AntennaModel value)
{
  auto& antenna = ensureMasterTermAntenna(term);
  replaceAntennaModel(antenna.models, std::move(value));
  antenna.flags |= LibraryMasterTermAntennaFlag::kHasModels;
  notify();
}

bool AntennaService::clearMasterTermAntennaModel(LibraryMasterTermId term,
                                                 std::string_view oxide) noexcept
{
  auto* library = _library;
  if (library == nullptr || !library->masterTermStorage().contains(term)) return false;
  auto& registry = library->libraryRegistry().registry();
  auto* antenna = registry.try_get<LibraryMasterTermAntenna>(term.entity());
  if (antenna == nullptr || !eraseAntennaModel(antenna->models, oxide)) return false;
  syncMasterTermAntennaFlags(*antenna);
  notify();
  return true;
}

const TechLayerAntenna* AntennaService::layerAntenna(TechLayerId layer) const noexcept
{
  const auto* technology = _const_technology;
  if (technology == nullptr || !technology->contains(layer)) return nullptr;
  return technology->techRegistry().registry().try_get<TechLayerAntenna>(layer.entity());
}

TechLayerAntenna& AntennaService::ensureLayerAntenna(TechLayerId layer)
{
  auto* technology = _technology;
  if (technology == nullptr || !technology->contains(layer)) {
    throw std::invalid_argument("antenna service requires a live tech layer");
  }
  return technology->techRegistry().registry().get_or_emplace<TechLayerAntenna>(layer.entity());
}

void AntennaService::setLayerAntenna(TechLayerId layer, TechLayerAntenna value)
{
  normalizeAntennaModels(value.models);
  syncTechLayerAntennaFlags(value);
  auto& target = ensureLayerAntenna(layer);
  target = std::move(value);
  notify();
}

void AntennaService::clearLayerAntenna(TechLayerId layer) noexcept
{
  auto* technology = _technology;
  if (technology == nullptr || !technology->contains(layer)) return;
  technology->techRegistry().registry().remove<TechLayerAntenna>(layer.entity());
  notify();
}

const AntennaModel* AntennaService::layerAntennaModel(TechLayerId layer,
                                                      std::string_view oxide) const noexcept
{
  const auto* antenna = layerAntenna(layer);
  return antenna == nullptr ? nullptr : findAntennaModel(antenna->models, oxide);
}

void AntennaService::setLayerAntennaModel(TechLayerId layer, AntennaModel value)
{
  auto& antenna = ensureLayerAntenna(layer);
  replaceAntennaModel(antenna.models, std::move(value));
  antenna.flags |= TechLayerAntennaFlag::kHasModels;
  notify();
}

bool AntennaService::clearLayerAntennaModel(TechLayerId layer, std::string_view oxide) noexcept
{
  auto* technology = _technology;
  if (technology == nullptr || !technology->contains(layer)) return false;
  auto& registry = technology->techRegistry().registry();
  auto* antenna = registry.try_get<TechLayerAntenna>(layer.entity());
  if (antenna == nullptr || !eraseAntennaModel(antenna->models, oxide)) return false;
  syncTechLayerAntennaFlags(*antenna);
  notify();
  return true;
}

const DesignBlockTermAntenna* AntennaService::blockTermAntenna(DesignBlockTermId term) const noexcept
{
  const auto* design = _const_design;
  if (design == nullptr || !design->netlistStorage().contains(term)) return nullptr;
  return design->designRegistry().registry().try_get<DesignBlockTermAntenna>(term.entity());
}

DesignBlockTermAntenna& AntennaService::ensureBlockTermAntenna(DesignBlockTermId term)
{
  if (_design == nullptr || !_design->netlistStorage().contains(term)) {
    throw std::invalid_argument("antenna service requires a live block term");
  }
  return _design->designRegistry().registry().get_or_emplace<DesignBlockTermAntenna>(term.entity());
}

void AntennaService::setBlockTermAntenna(DesignBlockTermId term, DesignBlockTermAntenna value)
{
  normalizeAntennaModels(value.models);
  syncBlockTermAntennaFlags(value);
  auto& target = ensureBlockTermAntenna(term);
  target = std::move(value);
  notify();
}

void AntennaService::clearBlockTermAntenna(DesignBlockTermId term) noexcept
{
  if (_design == nullptr || !_design->netlistStorage().contains(term)) return;
  _design->designRegistry().registry().remove<DesignBlockTermAntenna>(term.entity());
  notify();
}

const AntennaModel* AntennaService::blockTermAntennaModel(DesignBlockTermId term,
                                                          std::string_view oxide) const noexcept
{
  const auto* antenna = blockTermAntenna(term);
  return antenna == nullptr ? nullptr : findAntennaModel(antenna->models, oxide);
}

void AntennaService::setBlockTermAntennaModel(DesignBlockTermId term, AntennaModel value)
{
  auto& antenna = ensureBlockTermAntenna(term);
  replaceAntennaModel(antenna.models, std::move(value));
  antenna.flags |= DesignBlockTermAntennaFlag::kHasModels;
  notify();
}

bool AntennaService::clearBlockTermAntennaModel(DesignBlockTermId term,
                                                std::string_view oxide) noexcept
{
  auto* design = _design;
  if (design == nullptr || !design->netlistStorage().contains(term)) return false;
  auto& registry = design->designRegistry().registry();
  auto* antenna = registry.try_get<DesignBlockTermAntenna>(term.entity());
  if (antenna == nullptr || !eraseAntennaModel(antenna->models, oxide)) return false;
  syncBlockTermAntennaFlags(*antenna);
  notify();
  return true;
}

}  // namespace eccdb
