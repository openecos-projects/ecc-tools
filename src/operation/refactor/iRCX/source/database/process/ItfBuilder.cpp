// ***************************************************************************************
// Copyright (c) 2023-2025 Peng Cheng Laboratory
// Copyright (c) 2023-2025 Institute of Computing Technology, Chinese Academy of Sciences
// Copyright (c) 2023-2025 Beijing Institute of Open Source Chip
//
// iEDA is licensed under Mulan PSL v2.
// You can use this software according to the terms and conditions of the Mulan PSL v2.
// You may obtain a copy of Mulan PSL v2 at:
// http://license.coscl.org.cn/MulanPSL2
//
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
// EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
// MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
//
// See the Mulan PSL v2 for more details.
// ***************************************************************************************
/**
 * @file ItfBuilder.cpp
 * @brief ITF builder and parser callback adapter implementation.
 */
#include "ItfBuilder.hpp"

#include <cstdio>
#include <memory>
#include <optional>

#include "itfrReader.hpp"
#include "log/Log.hh"

namespace ircx
{

namespace
{
using itf::itfCallBackType;
using itf::itfiConductor;
using itf::itfiDielectric;
using itf::itfiUserData;
using itf::itfiVia;

class ParserSessionGuard {
 public:
  ParserSessionGuard()
  {
    itf::itfrInit();
  }

  ~ParserSessionGuard()
  {
    itf::itfrClear();
  }

  ParserSessionGuard(const ParserSessionGuard&) = delete;
  ParserSessionGuard& operator=(const ParserSessionGuard&) = delete;
};

class ItfParseSession {
 public:
  std::optional<ProcessCorner> read(const std::string& file_name);

 private:
  static int technologyCb(itfCallBackType,
                          const char*,
                          itfiUserData);
  static int processFoundryCb(itfCallBackType,
                              const char*,
                              itfiUserData);
  static int processNodeCb(itfCallBackType,
                           F64,
                           itfiUserData);
  static int processTypeCb(itfCallBackType,
                           const char*,
                           itfiUserData);
  static int processVersionCb(itfCallBackType,
                              F64,
                              itfiUserData);
  static int processCornerCb(itfCallBackType,
                             const char*,
                             itfiUserData);
  static int referenceDirectionCb(itfCallBackType,
                                  const char*,
                                  itfiUserData);
  static int globalTemperatureCb(itfCallBackType,
                                 F64,
                                 itfiUserData);
  static int backgroundErCb(itfCallBackType,
                            F64,
                            itfiUserData);
  static int halfNodeScaleFactorCb(itfCallBackType,
                                   F64,
                                   itfiUserData);
  static int useSiDensityCb(itfCallBackType,
                            int,
                            itfiUserData);
  static int dropFactorLateralSpacingCb(itfCallBackType,
                                        F64,
                                        itfiUserData);
  static int dielectricCb(itfCallBackType,
                          itfiDielectric*,
                          itfiUserData);
  static int conductorCb(itfCallBackType,
                         itfiConductor*,
                         itfiUserData);
  static int viaCb(itfCallBackType,
                   itfiVia*,
                   itfiUserData);

  int parseTechnology(const char*);
  int parseProcessFoundry(const char*);
  int parseProcessNode(F64);
  int parseProcessType(const char*);
  int parseProcessVersion(F64);
  int parseProcessCorner(const char*);
  int parseReferenceDirection(const char*);
  int parseGlobalTemperature(F64);
  int parseBackgroundEr(F64);
  int parseHalfNodeScaleFactor(F64);
  int parseUseSiDensity(int);
  int parseDropFactorLateralSpacing(F64);
  int parseDielectric(const itfiDielectric&);
  int parseConductor(const itfiConductor&);
  int parseVia(const itfiVia&);

  std::optional<ProcessCorner> _process_corner;
};

bool
requireCallbackType(itfCallBackType actual, itfCallBackType expected, const char* expected_name)
{
  if (actual == expected) {
    return true;
  }

  LOG_ERROR << "Callback type error, needs " << expected_name;
  return false;
}

ItfParseSession*
requireSession(itfiUserData user_data)
{
  auto* session = static_cast<ItfParseSession*>(user_data);
  if (session == nullptr) {
    LOG_ERROR << "ITF callback user data is nullptr";
  }
  return session;
}

template <typename Parser>
int
dispatchStringCallback(itfCallBackType actual,
                       itfCallBackType expected,
                       const char* expected_name,
                       const char* value,
                       itfiUserData user_data,
                       bool require_value,
                       Parser parser)
{
  if (!requireCallbackType(actual, expected, expected_name)) {
    return 1;
  }
  if (require_value && value == nullptr) {
    LOG_ERROR << "string is nullptr";
    return 1;
  }

  auto* session = requireSession(user_data);
  if (session == nullptr) {
    return 1;
  }

  return (session->*parser)(value ? value : "");
}

template <typename Value, typename Parser>
int
dispatchValueCallback(itfCallBackType actual,
                      itfCallBackType expected,
                      const char* expected_name,
                      Value value,
                      itfiUserData user_data,
                      Parser parser)
{
  if (!requireCallbackType(actual, expected, expected_name)) {
    return 1;
  }

  auto* session = requireSession(user_data);
  if (session == nullptr) {
    return 1;
  }

  return (session->*parser)(value);
}

template <typename Object, typename Parser>
int
dispatchObjectCallback(itfCallBackType actual,
                       itfCallBackType expected,
                       const char* expected_name,
                       Object* object,
                       const char* object_name,
                       itfiUserData user_data,
                       Parser parser)
{
  if (!requireCallbackType(actual, expected, expected_name)) {
    return 1;
  }
  if (object == nullptr) {
    LOG_ERROR << object_name << " is nullptr";
    return 1;
  }

  auto* session = requireSession(user_data);
  if (session == nullptr) {
    return 1;
  }

  return (session->*parser)(*object);
}

std::optional<ProcessCorner>
ItfParseSession::read(const std::string& file_name)
{
  ParserSessionGuard parser_session;

  itf::itfrSetTechnologyCb(technologyCb);
  itf::itfrSetProcessFoundryCb(processFoundryCb);
  itf::itfrSetProcessNodeCb(processNodeCb);
  itf::itfrSetProcessTypeCb(processTypeCb);
  itf::itfrSetProcessVersionCb(processVersionCb);
  itf::itfrSetProcessCornerCb(processCornerCb);
  itf::itfrSetReferenceDirectionCb(referenceDirectionCb);
  itf::itfrSetGlobalTemperatureCb(globalTemperatureCb);
  itf::itfrSetBackgroundErCb(backgroundErCb);
  itf::itfrSetHalfNodeScaleFactorCb(halfNodeScaleFactorCb);
  itf::itfrSetUseSiDensityCb(useSiDensityCb);
  itf::itfrSetDropFactorLateralSpacingCb(dropFactorLateralSpacingCb);
  itf::itfrSetDielectricCb(dielectricCb);
  itf::itfrSetConductorCb(conductorCb);
  itf::itfrSetViaCb(viaCb);

  auto close_file = [](FILE* file) {
    if (file != nullptr) {
      fclose(file);
    }
  };
  std::unique_ptr<FILE, decltype(close_file)> file(fopen(file_name.c_str(), "r"), close_file);
  if (file.get() == nullptr) {
    LOG_ERROR << "fail to open: " << file_name;
    return std::nullopt;
  }

  _process_corner.emplace();
  const int ret = itf::itfrRead(file.get(), file_name.c_str(), this);
  if (ret != 0) {
    _process_corner.reset();
    return std::nullopt;
  }

  _process_corner->updateLayersHeight();
  return std::move(_process_corner);
}

int
ItfParseSession::technologyCb(itfCallBackType c,
                              const char* string,
                              itfiUserData user_data)
{
  return dispatchStringCallback(c, itfCallBackType::kTechnologyCbType, "kTechnologyCbType",
                                string, user_data, true, &ItfParseSession::parseTechnology);
}

int
ItfParseSession::processFoundryCb(itfCallBackType c,
                                  const char* string,
                                  itfiUserData user_data)
{
  return dispatchStringCallback(c, itfCallBackType::kProcessFoundryCbType, "kProcessFoundryCbType",
                                string, user_data, false, &ItfParseSession::parseProcessFoundry);
}

int
ItfParseSession::processNodeCb(itfCallBackType c,
                               F64 value,
                               itfiUserData user_data)
{
  return dispatchValueCallback(c, itfCallBackType::kProcessNodeCbType, "kProcessNodeCbType",
                               value, user_data, &ItfParseSession::parseProcessNode);
}

int
ItfParseSession::processTypeCb(itfCallBackType c,
                               const char* string,
                               itfiUserData user_data)
{
  return dispatchStringCallback(c, itfCallBackType::kProcessTypeCbType, "kProcessTypeCbType",
                                string, user_data, false, &ItfParseSession::parseProcessType);
}

int
ItfParseSession::processVersionCb(itfCallBackType c,
                                  F64 value,
                                  itfiUserData user_data)
{
  return dispatchValueCallback(c, itfCallBackType::kProcessVersionCbType, "kProcessVersionCbType",
                               value, user_data, &ItfParseSession::parseProcessVersion);
}

int
ItfParseSession::processCornerCb(itfCallBackType c,
                                 const char* string,
                                 itfiUserData user_data)
{
  return dispatchStringCallback(
      c,
      itfCallBackType::kProcessCornerCbType,
      "kProcessCornerCbType",
      string,
      user_data,
      false,
      &ItfParseSession::parseProcessCorner);
}

int
ItfParseSession::referenceDirectionCb(itfCallBackType c,
                                      const char* string,
                                      itfiUserData user_data)
{
  return dispatchStringCallback(
      c,
      itfCallBackType::kReferenceDirectionCbType,
      "kReferenceDirectionCbType",
      string,
      user_data,
      false,
      &ItfParseSession::parseReferenceDirection);
}

int
ItfParseSession::globalTemperatureCb(itfCallBackType c,
                                     F64 temperature,
                                     itfiUserData user_data)
{
  return dispatchValueCallback(
      c,
      itfCallBackType::kGlobalTemperatureCbType,
      "kGlobalTemperatureCbType",
      temperature,
      user_data,
      &ItfParseSession::parseGlobalTemperature);
}

int
ItfParseSession::backgroundErCb(itfCallBackType c,
                                F64 background_er,
                                itfiUserData user_data)
{
  return dispatchValueCallback(
      c,
      itfCallBackType::kBackgroundErCbType,
      "kBackgroundErCbType",
      background_er,
      user_data,
      &ItfParseSession::parseBackgroundEr);
}

int
ItfParseSession::halfNodeScaleFactorCb(itfCallBackType c,
                                       F64 factor,
                                       itfiUserData user_data)
{
  return dispatchValueCallback(
      c,
      itfCallBackType::kHalfNodeScaleFactorCbType,
      "kHalfNodeScaleFactorCbType",
      factor,
      user_data,
      &ItfParseSession::parseHalfNodeScaleFactor);
}

int
ItfParseSession::useSiDensityCb(itfCallBackType c,
                                int use,
                                itfiUserData user_data)
{
  return dispatchValueCallback(
      c,
      itfCallBackType::kUseSiDensityCbType,
      "kUseSiDensityCbType",
      use,
      user_data,
      &ItfParseSession::parseUseSiDensity);
}

int
ItfParseSession::dropFactorLateralSpacingCb(itfCallBackType c,
                                            F64 factor,
                                            itfiUserData user_data)
{
  return dispatchValueCallback(
      c,
      itfCallBackType::kDropFactorLateralSpacingCbType,
      "kDropFactorLateralSpacingCbType",
      factor,
      user_data,
      &ItfParseSession::parseDropFactorLateralSpacing);
}

int
ItfParseSession::dielectricCb(itfCallBackType c,
                              itfiDielectric* dielectric,
                              itfiUserData user_data)
{
  return dispatchObjectCallback(
      c,
      itfCallBackType::kDielectricCbType,
      "kDielectricCbType",
      dielectric,
      "itf Dielectric",
      user_data,
      &ItfParseSession::parseDielectric);
}

int
ItfParseSession::conductorCb(itfCallBackType c,
                             itfiConductor* conductor,
                             itfiUserData user_data)
{
  return dispatchObjectCallback(
      c,
      itfCallBackType::kConductorCbType,
      "kConductorCbType",
      conductor,
      "itf Conductor",
      user_data,
      &ItfParseSession::parseConductor);
}

int
ItfParseSession::viaCb(itfCallBackType c,
                       itfiVia* via,
                       itfiUserData user_data)
{
  return dispatchObjectCallback(c, itfCallBackType::kViaCbType, "kViaCbType",
                                via, "itf Via", user_data, &ItfParseSession::parseVia);
}

int
ItfParseSession::parseTechnology(const char* v)
{
  _process_corner->set_technology(v);
  return 0;
}

int
ItfParseSession::parseProcessFoundry(const char* v)
{
  _process_corner->set_process_foundry(v);
  return 0;
}

int
ItfParseSession::parseProcessNode(F64 v)
{
  _process_corner->set_process_node(v);
  return 0;
}

int
ItfParseSession::parseProcessType(const char* v)
{
  _process_corner->set_process_type(v);
  return 0;
}

int
ItfParseSession::parseProcessVersion(F64 v)
{
  _process_corner->set_process_version(v);
  return 0;
}

int
ItfParseSession::parseProcessCorner(const char* v)
{
  _process_corner->set_process_corner(v);
  return 0;
}

int
ItfParseSession::parseReferenceDirection(const char* v)
{
  _process_corner->set_reference_direction(v);
  return 0;
}

int
ItfParseSession::parseGlobalTemperature(F64 v)
{
  _process_corner->set_global_temperature(v);
  return 0;
}

int
ItfParseSession::parseBackgroundEr(F64 v)
{
  _process_corner->set_background_er(v);
  return 0;
}

int
ItfParseSession::parseHalfNodeScaleFactor(F64 v)
{
  _process_corner->set_half_node_scale_factor(v);
  return 0;
}

int
ItfParseSession::parseUseSiDensity(int v)
{
  _process_corner->set_use_si_density(v);
  return 0;
}

int
ItfParseSession::parseDropFactorLateralSpacing(F64 v)
{
  _process_corner->set_drop_factor_lateral_spacing(v);
  return 0;
}

int
ItfParseSession::parseDielectric(const itfiDielectric& dielectric)
{
  auto& layers = _process_corner->get_layers();
  layers.addDielectric(dielectric);
  return 0;
}

int
ItfParseSession::parseConductor(const itfiConductor& conductor)
{
  auto& layers = _process_corner->get_layers();
  layers.addConductor(conductor);
  return 0;
}

int
ItfParseSession::parseVia(const itfiVia& via)
{
  auto& layers = _process_corner->get_layers();
  layers.addVia(via);
  return 0;
}

} // namespace

void
ItfBuilder::addProcessCorner(ProcessCorner corner)
{
  _process_corners.push_back(std::move(corner));
}

bool
ItfBuilder::build(const std::string& file_name)
{
  ItfParseSession session;
  auto process_corner = session.read(file_name);
  if (!process_corner) {
    return false;
  }

  addProcessCorner(std::move(process_corner.value()));
  return true;
}

const ProcessCorner*
ItfBuilder::findProcessCorner(const std::string& process_name) const
{
  for (const auto& corner : _process_corners) {
    if (corner.get_technology() == process_name) {
      return &corner;
    }
  }

  return nullptr;
}

ProcessCorner*
ItfBuilder::get_last_process_corner()
{
  return _process_corners.empty() ? nullptr : &_process_corners.back();
}

const ProcessCorner*
ItfBuilder::get_last_process_corner() const
{
  return _process_corners.empty() ? nullptr : &_process_corners.back();
}

std::vector<ProcessCorner>
ItfBuilder::takeProcessCorners()
{
  auto ret = std::move(_process_corners);
  _process_corners.clear();
  return ret;
}

std::optional<ProcessCorner>
ItfBuilder::takeLastProcessCorner()
{
  if (_process_corners.empty()) {
    return std::nullopt;
  }

  auto ret = std::move(_process_corners.back());
  _process_corners.pop_back();
  return ret;
}

} // namespace ircx
